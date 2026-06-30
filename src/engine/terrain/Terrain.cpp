#include "engine/terrain/Terrain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <sstream>
#include <unordered_set>

namespace aine {
namespace {

using json = nlohmann::json;

constexpr int kTerrainSerializationVersion = 4;
constexpr int kMinTerrainResolution = 2;
constexpr int kMaxTerrainResolution = 257;
constexpr float kEpsilon = 0.00001f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 ToVec3(std::array<float, 3> value) {
    return {value[0], value[1], value[2]};
}

std::array<float, 3> ToArray(Vec3 value) {
    return {value.x, value.y, value.z};
}

Vec3 operator+(Vec3 left, Vec3 right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 operator*(Vec3 value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 operator/(Vec3 value, float scalar) {
    return {value.x / scalar, value.y / scalar, value.z / scalar};
}

float Dot(Vec3 left, Vec3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

float Length(Vec3 value) {
    return std::sqrt(std::max(0.0f, Dot(value, value)));
}

Vec3 Normalize(Vec3 value, Vec3 fallback = {0.0f, 1.0f, 0.0f}) {
    const float length = Length(value);
    if (length <= kEpsilon || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}

std::string GetProperty(const Component& component, const std::string& name, std::string fallback = {}) {
    for (const ComponentProperty& property : component.properties) {
        if (property.name == name) {
            return property.value;
        }
    }
    return fallback;
}

void SetProperty(Component* component, const std::string& name, std::string value) {
    if (component == nullptr) {
        return;
    }
    for (ComponentProperty& property : component->properties) {
        if (property.name == name) {
            property.value = std::move(value);
            return;
        }
    }
    component->properties.push_back({name, std::move(value)});
}

void RemoveProperty(Component* component, const std::string& name) {
    if (component == nullptr) {
        return;
    }
    component->properties.erase(
        std::remove_if(component->properties.begin(), component->properties.end(),
                       [&](const ComponentProperty& property) { return property.name == name; }),
        component->properties.end());
}

int ParseInt(const std::string& value, int fallback) {
    std::istringstream stream(value);
    int parsed = fallback;
    return (stream >> parsed) ? parsed : fallback;
}

bool ParseBool(std::string value, bool fallback) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }
    return fallback;
}

std::vector<float> ParseFloatList(std::string value) {
    std::vector<float> result;
    if (value.empty()) {
        return result;
    }

    try {
        const json parsed = json::parse(value);
        if (parsed.is_array()) {
            for (const json& item : parsed) {
                if (item.is_number()) {
                    const float number = item.get<float>();
                    if (std::isfinite(number)) {
                        result.push_back(number);
                    }
                }
            }
            return result;
        }
    } catch (const std::exception&) {
    }

    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream stream(value);
    float parsed = 0.0f;
    while (stream >> parsed) {
        if (std::isfinite(parsed)) {
            result.push_back(parsed);
        }
    }
    return result;
}

std::vector<int> ParseIntList(std::string value) {
    std::vector<int> result;
    if (value.empty()) {
        return result;
    }

    try {
        const json parsed = json::parse(value);
        if (parsed.is_array()) {
            for (const json& item : parsed) {
                if (item.is_number_integer()) {
                    result.push_back(item.get<int>());
                } else if (item.is_boolean()) {
                    result.push_back(item.get<bool>() ? 1 : 0);
                }
            }
            return result;
        }
    } catch (const std::exception&) {
    }

    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream stream(value);
    int parsed = 0;
    while (stream >> parsed) {
        result.push_back(parsed);
    }
    return result;
}

std::array<float, 3> ParseVec3(std::string value, std::array<float, 3> fallback) {
    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream stream(value);
    std::array<float, 3> parsed = fallback;
    if (stream >> parsed[0] >> parsed[1] >> parsed[2] && std::isfinite(parsed[0]) &&
        std::isfinite(parsed[1]) && std::isfinite(parsed[2])) {
        return parsed;
    }
    return fallback;
}

std::array<float, 4> ParseColor4(std::string value, std::array<float, 4> fallback) {
    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream stream(value);
    std::array<float, 4> parsed = fallback;
    if (stream >> parsed[0] >> parsed[1] >> parsed[2]) {
        if (!(stream >> parsed[3])) {
            parsed[3] = fallback[3];
        }
        for (float channel : parsed) {
            if (!std::isfinite(channel)) {
                return fallback;
            }
        }
        return parsed;
    }
    return fallback;
}

std::string Vec3ToString(std::array<float, 3> value) {
    std::ostringstream stream;
    stream.precision(6);
    stream << std::fixed << value[0] << ", " << value[1] << ", " << value[2];
    return stream.str();
}

std::string JsonArrayString(const std::vector<float>& values) {
    json serialized = json::array();
    for (float value : values) {
        serialized.push_back(std::round(value * 100000.0f) / 100000.0f);
    }
    return serialized.dump();
}

std::string JsonArrayString(const std::vector<int>& values) {
    json serialized = json::array();
    for (int value : values) {
        serialized.push_back(value);
    }
    return serialized.dump();
}

std::vector<TerrainLayer> ParseLayers(const std::string& value) {
    std::vector<TerrainLayer> layers;
    if (value.empty()) {
        return layers;
    }

    try {
        const json parsed = json::parse(value);
        if (!parsed.is_array()) {
            return layers;
        }
        for (const json& item : parsed) {
            if (!item.is_object()) {
                continue;
            }
            TerrainLayer layer;
            layer.name = item.value("name", layer.name);
            layer.material = item.value("material", layer.material);
            layer.albedoTexture =
                item.value("albedoTexture", item.value("texture", item.value("texturePath", layer.albedoTexture)));
            layer.normalTexture = item.value("normalTexture", layer.normalTexture);
            layer.maskTexture = item.value("maskTexture", layer.maskTexture);
            layer.tiling = std::max(0.001f, item.value("tiling", layer.tiling));
            layer.roughness = std::clamp(item.value("roughness", layer.roughness), 0.0f, 1.0f);
            layer.metalness = std::clamp(item.value("metalness", layer.metalness), 0.0f, 1.0f);
            layer.detailScale = std::max(0.1f, item.value("detailScale", layer.detailScale));
            layer.detailStrength = std::clamp(item.value("detailStrength", layer.detailStrength), 0.0f, 0.75f);
            if (item.contains("displayColor") && item.at("displayColor").is_array()) {
                const json& color = item.at("displayColor");
                for (size_t i = 0; i < std::min<size_t>(4, color.size()); ++i) {
                    if (color.at(i).is_number()) {
                        layer.displayColor[i] = color.at(i).get<float>();
                    }
                }
            } else {
                layer.displayColor = ParseColor4(item.value("color", std::string{}), layer.displayColor);
            }
            if (!layer.name.empty()) {
                layers.push_back(std::move(layer));
            }
        }
    } catch (const std::exception&) {
    }
    return layers;
}

std::string SerializeLayers(const std::vector<TerrainLayer>& layers) {
    json serialized = json::array();
    for (const TerrainLayer& layer : layers) {
        serialized.push_back({
            {"name", layer.name},
            {"displayColor",
             json::array({layer.displayColor[0], layer.displayColor[1], layer.displayColor[2], layer.displayColor[3]})},
            {"material", layer.material},
            {"albedoTexture", layer.albedoTexture},
            {"normalTexture", layer.normalTexture},
            {"maskTexture", layer.maskTexture},
            {"tiling", layer.tiling},
            {"roughness", layer.roughness},
            {"metalness", layer.metalness},
            {"detailScale", layer.detailScale},
            {"detailStrength", layer.detailStrength},
        });
    }
    return serialized.dump();
}

std::vector<TerrainLayer> DefaultTerrainLayers() {
    TerrainLayer base;
    base.name = "Default";
    base.displayColor = {0.46f, 0.47f, 0.46f, 1.0f};
    base.material = "TerrainDefault";
    base.tiling = 10.0f;
    base.roughness = 0.82f;
    base.detailScale = 16.0f;
    base.detailStrength = 0.10f;

    TerrainLayer rock;
    rock.name = "Rock";
    rock.displayColor = {0.35f, 0.36f, 0.35f, 1.0f};
    rock.material = "TerrainRock";
    rock.tiling = 7.0f;
    rock.roughness = 0.92f;
    rock.detailScale = 30.0f;
    rock.detailStrength = 0.22f;

    TerrainLayer soil;
    soil.name = "Soil";
    soil.displayColor = {0.42f, 0.37f, 0.31f, 1.0f};
    soil.material = "TerrainSoil";
    soil.tiling = 9.0f;
    soil.roughness = 0.96f;
    soil.detailScale = 24.0f;
    soil.detailStrength = 0.18f;
    return {base, rock, soil};
}

int SampleIndex(const TerrainData& data, int x, int z) {
    return z * data.resolution + x;
}

float ClampHeight(const TerrainData& data, float value) {
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, std::max(0.0f, data.size[1]));
}

float CellSizeX(const TerrainData& data) {
    return data.resolution <= 1 ? 1.0f : data.size[0] / static_cast<float>(data.resolution - 1);
}

float CellSizeZ(const TerrainData& data) {
    return data.resolution <= 1 ? 1.0f : data.size[2] / static_cast<float>(data.resolution - 1);
}

float ClampUv(float value) {
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float Fract(float value) {
    return value - std::floor(value);
}

float Hash01(int x, int z, unsigned int seed) {
    unsigned int n = static_cast<unsigned int>(x) * 374761393u + static_cast<unsigned int>(z) * 668265263u + seed * 2246822519u;
    n = (n ^ (n >> 13u)) * 1274126177u;
    n ^= n >> 16u;
    return static_cast<float>(n & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
}

float ValueNoise(float x, float z, unsigned int seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const float tx = Fract(x);
    const float tz = Fract(z);
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sz = tz * tz * (3.0f - 2.0f * tz);
    const float a = Lerp(Hash01(x0, z0, seed), Hash01(x0 + 1, z0, seed), sx);
    const float b = Lerp(Hash01(x0, z0 + 1, seed), Hash01(x0 + 1, z0 + 1, seed), sx);
    return Lerp(a, b, sz) * 2.0f - 1.0f;
}

std::array<float, 4> LayerSurfaceColor(const TerrainLayer& layer, float u, float v,
                                       std::array<float, 3> normal, float heightNormalized,
                                       unsigned int seed) {
    std::array<float, 4> color = layer.displayColor;
    const float scale = std::max(0.1f, layer.detailScale);
    const float n0 = ValueNoise(u * scale, v * scale, seed);
    const float n1 = ValueNoise(u * scale * 2.73f + 19.1f, v * scale * 2.73f - 7.4f, seed + 101u);
    const float roughnessGain = Lerp(0.65f, 1.25f, std::clamp(layer.roughness, 0.0f, 1.0f));
    const float slope = 1.0f - std::clamp(normal[1], 0.0f, 1.0f);
    const float detail = (n0 * 0.68f + n1 * 0.32f) * layer.detailStrength * roughnessGain;
    const float macro = (heightNormalized - 0.5f) * 0.055f + slope * 0.045f;
    const float shade = std::clamp(1.0f + detail + macro, 0.45f, 1.55f);
    for (int channel = 0; channel < 3; ++channel) {
        color[static_cast<size_t>(channel)] =
            std::clamp(color[static_cast<size_t>(channel)] * shade, 0.0f, 1.0f);
    }
    color[3] = 1.0f;
    return color;
}

std::array<float, 4> BlendedLayerColor(const TerrainData& data, int sampleIndex, float u, float v,
                                       std::array<float, 3> normal, float height) {
    std::array<float, 4> color{0.36f, 0.48f, 0.28f, 1.0f};
    if (data.layers.empty() || data.weights.empty()) {
        return color;
    }

    color = {0.0f, 0.0f, 0.0f, 0.0f};
    const int layerCount = TerrainLayerCount(data);
    const int base = sampleIndex * layerCount;
    float total = 0.0f;
    const float heightNormalized = data.size[1] <= kEpsilon ? 0.0f : std::clamp(height / data.size[1], 0.0f, 1.0f);
    for (int layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        const float weight = std::clamp(data.weights[static_cast<size_t>(base + layerIndex)], 0.0f, 1.0f);
        total += weight;
        const TerrainLayer& layer = data.layers[static_cast<size_t>(layerIndex)];
        const std::array<float, 4> layerColor =
            LayerSurfaceColor(layer, u, v, normal, heightNormalized, 982451653u + static_cast<unsigned int>(layerIndex) * 7919u);
        for (int channel = 0; channel < 4; ++channel) {
            color[static_cast<size_t>(channel)] += layerColor[static_cast<size_t>(channel)] * weight;
        }
    }
    if (total <= kEpsilon) {
        return data.layers.front().displayColor;
    }
    for (float& channel : color) {
        channel /= total;
    }
    color[3] = 1.0f;
    return color;
}

float DeterministicNoise(int x, int z, unsigned int seed) {
    unsigned int n = static_cast<unsigned int>(x) * 73856093u ^ static_cast<unsigned int>(z) * 19349663u ^ seed;
    n = (n << 13u) ^ n;
    const unsigned int mixed = (n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu;
    return 1.0f - static_cast<float>(mixed) / 1073741824.0f;
}

void AddUnique(std::vector<int>* values, int value) {
    if (values == nullptr) {
        return;
    }
    if (std::find(values->begin(), values->end(), value) == values->end()) {
        values->push_back(value);
    }
}

bool IsFiniteArray(std::array<float, 3> value) {
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

std::array<float, 3> DefaultTerrainVolumeSize(const TerrainData& data) {
    return {std::max(0.1f, data.size[0]), std::max(6.0f, data.size[1]), std::max(0.1f, data.size[2])};
}

void SeedTerrainVolumeFromHeightfield(TerrainData* data) {
    if (data == nullptr || data->backend != TerrainBackend::Volumetric || !data->volumeEnabled) {
        return;
    }
    TerrainVolumeData& volume = data->volume;
    const int sampleCount = CaveSampleCount(volume);
    if (sampleCount <= 0) {
        return;
    }
    volume.densities.assign(static_cast<size_t>(sampleCount), 1.0f);
    const float stepX = volume.resolution[0] <= 1 ? 1.0f : volume.size[0] / static_cast<float>(volume.resolution[0] - 1);
    const float stepY = volume.resolution[1] <= 1 ? 1.0f : volume.size[1] / static_cast<float>(volume.resolution[1] - 1);
    const float stepZ = volume.resolution[2] <= 1 ? 1.0f : volume.size[2] / static_cast<float>(volume.resolution[2] - 1);
    const float densityScale = std::max(0.05f, std::min({stepX, stepY, stepZ}));
    for (int z = 0; z < volume.resolution[2]; ++z) {
        const float localZ = -volume.size[2] * 0.5f + static_cast<float>(z) * stepZ;
        for (int y = 0; y < volume.resolution[1]; ++y) {
            const float volumeLocalY = -volume.size[1] * 0.5f + static_cast<float>(y) * stepY;
            const float terrainLocalY = volumeLocalY;
            for (int x = 0; x < volume.resolution[0]; ++x) {
                const float localX = -volume.size[0] * 0.5f + static_cast<float>(x) * stepX;
                const TerrainSample surface = SampleTerrainLocal(*data, localX, localZ);
                const float surfaceHeight = surface.valid ? surface.height : 0.0f;
                const float density = (surfaceHeight - terrainLocalY) / densityScale;
                volume.densities[static_cast<size_t>(CaveSampleIndex(volume, x, y, z))] =
                    std::clamp(std::isfinite(density) ? density : 1.0f, -1.0f, 1.0f);
            }
        }
    }
    volume.dirtyChunks = CaveChunksForDirtyRegion(volume, 0, volume.resolution[0] - 1, 0, volume.resolution[1] - 1,
                                                  0, volume.resolution[2] - 1);
}

void LoadTerrainVolumeFromProperties(const Component& component, TerrainData* data) {
    if (data == nullptr) {
        return;
    }
    if (data->backend != TerrainBackend::Volumetric) {
        data->volumeEnabled = false;
        data->volume = {};
        return;
    }
    const bool hasSerializedVolume = !GetProperty(component, "volumeDensities").empty() ||
                                     !GetProperty(component, "volumeResolution").empty() ||
                                     !GetProperty(component, "volumeLayers").empty();
    data->volumeEnabled = ParseBool(GetProperty(component, "volumeEnabled", hasSerializedVolume ? "true" : "false"),
                                    hasSerializedVolume);
    if (!data->volumeEnabled && !hasSerializedVolume) {
        data->volume = {};
        return;
    }

    Component cave{"CaveVolume", {}};
    const std::array<float, 3> defaultSize = DefaultTerrainVolumeSize(*data);
    SetProperty(&cave, "version", GetProperty(component, "volumeVersion", "1"));
    SetProperty(&cave, "resolution", GetProperty(component, "volumeResolution", "25, 17, 25"));
    SetProperty(&cave, "size", GetProperty(component, "volumeSize", Vec3ToString(defaultSize)));
    SetProperty(&cave, "chunkSize", GetProperty(component, "volumeChunkSize", "8"));
    SetProperty(&cave, "collisionEnabled",
                GetProperty(component, "volumeCollisionEnabled", data->collisionEnabled ? "true" : "false"));
    SetProperty(&cave, "collisionUpdateMode", GetProperty(component, "volumeCollisionUpdateMode", "Deferred"));
    SetProperty(&cave, "editRevision", GetProperty(component, "volumeEditRevision", "0"));
    SetProperty(&cave, "materialRevision", GetProperty(component, "volumeMaterialRevision", "0"));
    SetProperty(&cave, "densities", GetProperty(component, "volumeDensities"));
    SetProperty(&cave, "layers", GetProperty(component, "volumeLayers"));
    SetProperty(&cave, "weights", GetProperty(component, "volumeWeights"));
    SetProperty(&cave, "dirtyChunks", GetProperty(component, "volumeDirtyChunks"));
    SetProperty(&cave, "dirtyMaterialChunks", GetProperty(component, "volumeDirtyMaterialChunks"));
    LoadCaveVolumeDataFromComponent(cave, &data->volume);
    data->volumeEnabled = true;
}

void WriteTerrainVolumeProperties(const TerrainData& data, Component* component) {
    if (component == nullptr) {
        return;
    }
    SetProperty(component, "volumeEnabled", TerrainUsesVolumetric(data) ? "true" : "false");
    if (!TerrainUsesVolumetric(data)) {
        const char* volumeProperties[] = {"volumeVersion",       "volumeResolution", "volumeSize",
                                          "volumeChunkSize",     "volumeCollisionEnabled",
                                          "volumeCollisionUpdateMode",
                                          "volumeEditRevision", "volumeMaterialRevision",
                                          "volumeDensities",    "volumeLayers",
                                          "volumeWeights",      "volumeDirtyChunks",
                                          "volumeDirtyMaterialChunks"};
        for (const char* property : volumeProperties) {
            RemoveProperty(component, property);
        }
        return;
    }

    Component cave{"CaveVolume", {}};
    SaveCaveVolumeDataToComponent(data.volume, &cave);
    auto copy = [&](const char* source, const char* destination) {
        SetProperty(component, destination, GetProperty(cave, source));
    };
    copy("version", "volumeVersion");
    copy("resolution", "volumeResolution");
    copy("size", "volumeSize");
    copy("chunkSize", "volumeChunkSize");
    copy("collisionEnabled", "volumeCollisionEnabled");
    copy("collisionUpdateMode", "volumeCollisionUpdateMode");
    copy("editRevision", "volumeEditRevision");
    copy("materialRevision", "volumeMaterialRevision");
    copy("densities", "volumeDensities");
    copy("layers", "volumeLayers");
    copy("weights", "volumeWeights");
    copy("dirtyChunks", "volumeDirtyChunks");
    copy("dirtyMaterialChunks", "volumeDirtyMaterialChunks");
}

} // namespace

Component MakeTerrainComponent(int resolution, std::array<float, 3> size, int chunkSize) {
    TerrainData data;
    data.resolution = resolution;
    data.size = size;
    data.chunkSize = chunkSize;
    data.layers = DefaultTerrainLayers();
    NormalizeTerrainData(&data);
    Component component{"Terrain", {}};
    SaveTerrainDataToComponent(data, &component);
    return component;
}

bool LoadTerrainDataFromComponent(const Component& component, TerrainData* outData, std::string* error) {
    if (outData == nullptr) {
        if (error != nullptr) {
            *error = "Terrain output pointer was null.";
        }
        return false;
    }
    if (component.type != "Terrain") {
        if (error != nullptr) {
            *error = "Component is not Terrain.";
        }
        return false;
    }

    TerrainData data;
    data.version = ParseInt(GetProperty(component, "version", std::to_string(kTerrainSerializationVersion)),
                            kTerrainSerializationVersion);
    data.backend = TerrainBackendFromString(
        GetProperty(component, "backend", GetProperty(component, "terrainBackend", "Heightfield")));
    data.resolution = ParseInt(GetProperty(component, "resolution", "33"), 33);
    data.chunkSize = ParseInt(GetProperty(component, "chunkSize", "16"), 16);
    data.size = ParseVec3(GetProperty(component, "size", "32, 6, 32"), data.size);
    data.collisionEnabled = ParseBool(GetProperty(component, "collisionEnabled", "true"), true);
    data.editRevision = ParseInt(GetProperty(component, "editRevision", "0"), 0);
    data.materialRevision = ParseInt(GetProperty(component, "materialRevision", "0"), 0);
    data.heights = ParseFloatList(GetProperty(component, "heights"));
    data.layers = ParseLayers(GetProperty(component, "layers"));
    data.weights = ParseFloatList(GetProperty(component, "weights"));
    const std::vector<int> holes = ParseIntList(GetProperty(component, "holes"));
    data.holes.reserve(holes.size());
    for (int hole : holes) {
        data.holes.push_back(hole != 0 ? 1u : 0u);
    }
    data.dirtyChunks = ParseIntList(GetProperty(component, "dirtyChunks"));
    data.dirtyMaterialChunks = ParseIntList(GetProperty(component, "dirtyMaterialChunks"));
    LoadTerrainVolumeFromProperties(component, &data);
    NormalizeTerrainData(&data);
    *outData = std::move(data);
    return true;
}

void SaveTerrainDataToComponent(const TerrainData& source, Component* component) {
    if (component == nullptr) {
        return;
    }
    TerrainData data = source;
    NormalizeTerrainData(&data);
    component->type = "Terrain";

    std::vector<int> holes;
    holes.reserve(data.holes.size());
    for (std::uint8_t hole : data.holes) {
        holes.push_back(hole != 0 ? 1 : 0);
    }

    SetProperty(component, "version", std::to_string(data.version));
    SetProperty(component, "backend", TerrainBackendLabel(data.backend));
    SetProperty(component, "resolution", std::to_string(data.resolution));
    SetProperty(component, "size", Vec3ToString(data.size));
    SetProperty(component, "chunkSize", std::to_string(data.chunkSize));
    SetProperty(component, "collisionEnabled", data.collisionEnabled ? "true" : "false");
    SetProperty(component, "editRevision", std::to_string(data.editRevision));
    SetProperty(component, "materialRevision", std::to_string(data.materialRevision));
    SetProperty(component, "heights", JsonArrayString(data.heights));
    SetProperty(component, "layers", SerializeLayers(data.layers));
    SetProperty(component, "weights", JsonArrayString(data.weights));
    SetProperty(component, "holes", JsonArrayString(holes));
    SetProperty(component, "dirtyChunks", JsonArrayString(data.dirtyChunks));
    SetProperty(component, "dirtyMaterialChunks", JsonArrayString(data.dirtyMaterialChunks));
    WriteTerrainVolumeProperties(data, component);
}

void NormalizeTerrainData(TerrainData* data) {
    if (data == nullptr) {
        return;
    }
    data->version = kTerrainSerializationVersion;
    data->resolution = std::clamp(data->resolution, kMinTerrainResolution, kMaxTerrainResolution);
    data->size[0] = std::max(0.1f, std::isfinite(data->size[0]) ? data->size[0] : 32.0f);
    data->size[1] = std::max(0.0f, std::isfinite(data->size[1]) ? data->size[1] : 6.0f);
    data->size[2] = std::max(0.1f, std::isfinite(data->size[2]) ? data->size[2] : 32.0f);
    data->chunkSize = std::clamp(data->chunkSize, 1, std::max(1, data->resolution - 1));
    if (data->layers.empty()) {
        data->layers = DefaultTerrainLayers();
    }
    for (TerrainLayer& layer : data->layers) {
        if (layer.name.empty()) {
            layer.name = "Layer";
        }
        layer.tiling = std::max(0.001f, std::isfinite(layer.tiling) ? layer.tiling : 1.0f);
        layer.roughness = std::clamp(std::isfinite(layer.roughness) ? layer.roughness : 0.85f, 0.0f, 1.0f);
        layer.metalness = std::clamp(std::isfinite(layer.metalness) ? layer.metalness : 0.0f, 0.0f, 1.0f);
        layer.detailScale = std::clamp(std::isfinite(layer.detailScale) ? layer.detailScale : 18.0f, 0.1f, 256.0f);
        layer.detailStrength =
            std::clamp(std::isfinite(layer.detailStrength) ? layer.detailStrength : 0.12f, 0.0f, 0.75f);
        for (float& channel : layer.displayColor) {
            channel = std::clamp(std::isfinite(channel) ? channel : 1.0f, 0.0f, 1.0f);
        }
    }

    const int sampleCount = TerrainSampleCount(*data);
    data->heights.resize(static_cast<size_t>(sampleCount), 0.0f);
    for (float& height : data->heights) {
        height = ClampHeight(*data, height);
    }
    data->holes.resize(static_cast<size_t>(sampleCount), 0u);

    const int layerCount = TerrainLayerCount(*data);
    const size_t requiredWeightCount = static_cast<size_t>(sampleCount * layerCount);
    if (data->weights.size() != requiredWeightCount) {
        std::vector<float> resized(requiredWeightCount, 0.0f);
        const int oldLayerCount = layerCount > 0 && sampleCount > 0
                                      ? static_cast<int>(data->weights.size() / static_cast<size_t>(sampleCount))
                                      : 0;
        if (oldLayerCount > 0) {
            const int copiedLayerCount = std::min(oldLayerCount, layerCount);
            for (int sample = 0; sample < sampleCount; ++sample) {
                for (int layer = 0; layer < copiedLayerCount; ++layer) {
                    resized[static_cast<size_t>(sample * layerCount + layer)] =
                        data->weights[static_cast<size_t>(sample * oldLayerCount + layer)];
                }
            }
        } else {
            for (int sample = 0; sample < sampleCount; ++sample) {
                resized[static_cast<size_t>(sample * layerCount)] = 1.0f;
            }
        }
        data->weights = std::move(resized);
    }
    NormalizeTerrainLayerWeights(data);

    if (data->backend != TerrainBackend::Volumetric) {
        data->backend = TerrainBackend::Heightfield;
        data->volumeEnabled = false;
        data->volume = {};
    } else {
        data->volumeEnabled = true;
    }

    std::vector<int> normalizedDirty;
    const int chunkTotal = TerrainChunkCountX(*data) * TerrainChunkCountZ(*data);
    for (int chunk : data->dirtyChunks) {
        if (chunk >= 0 && chunk < chunkTotal) {
            AddUnique(&normalizedDirty, chunk);
        }
    }
    data->dirtyChunks = std::move(normalizedDirty);
    std::vector<int> normalizedMaterialDirty;
    for (int chunk : data->dirtyMaterialChunks) {
        if (chunk >= 0 && chunk < chunkTotal) {
            AddUnique(&normalizedMaterialDirty, chunk);
        }
    }
    data->dirtyMaterialChunks = std::move(normalizedMaterialDirty);

    if (data->backend == TerrainBackend::Volumetric && data->volumeEnabled) {
        const std::array<float, 3> defaultSize = DefaultTerrainVolumeSize(*data);
        const bool needsHeightfieldSeed =
            data->volume.densities.size() != static_cast<size_t>(CaveSampleCount(data->volume));
        data->volume.size[0] = defaultSize[0];
        data->volume.size[1] = std::max(defaultSize[1], std::isfinite(data->volume.size[1]) ? data->volume.size[1] : defaultSize[1]);
        data->volume.size[2] = defaultSize[2];
        NormalizeCaveVolumeData(&data->volume);
        if (needsHeightfieldSeed) {
            SeedTerrainVolumeFromHeightfield(data);
        }
    }
}

int TerrainSampleCount(const TerrainData& data) {
    return std::max(0, data.resolution * data.resolution);
}

int TerrainLayerCount(const TerrainData& data) {
    return static_cast<int>(data.layers.size());
}

int TerrainChunkCountX(const TerrainData& data) {
    return std::max(1, (data.resolution - 2) / std::max(1, data.chunkSize) + 1);
}

int TerrainChunkCountZ(const TerrainData& data) {
    return TerrainChunkCountX(data);
}

int TerrainChunkIndex(const TerrainData& data, int chunkX, int chunkZ) {
    const int countX = TerrainChunkCountX(data);
    const int countZ = TerrainChunkCountZ(data);
    if (chunkX < 0 || chunkZ < 0 || chunkX >= countX || chunkZ >= countZ) {
        return -1;
    }
    return chunkZ * countX + chunkX;
}

std::vector<int> TerrainChunksForDirtyRegion(const TerrainData& data, int minX, int maxX, int minZ, int maxZ) {
    std::vector<int> chunks;
    if (data.resolution < 2) {
        return chunks;
    }

    minX = std::clamp(minX, 0, data.resolution - 1);
    maxX = std::clamp(maxX, 0, data.resolution - 1);
    minZ = std::clamp(minZ, 0, data.resolution - 1);
    maxZ = std::clamp(maxZ, 0, data.resolution - 1);
    if (minX > maxX || minZ > maxZ) {
        return chunks;
    }

    const int minCellX = std::clamp(minX - 1, 0, data.resolution - 2);
    const int maxCellX = std::clamp(maxX, 0, data.resolution - 2);
    const int minCellZ = std::clamp(minZ - 1, 0, data.resolution - 2);
    const int maxCellZ = std::clamp(maxZ, 0, data.resolution - 2);
    const int chunkSize = std::max(1, data.chunkSize);
    for (int chunkZ = minCellZ / chunkSize; chunkZ <= maxCellZ / chunkSize; ++chunkZ) {
        for (int chunkX = minCellX / chunkSize; chunkX <= maxCellX / chunkSize; ++chunkX) {
            const int chunk = TerrainChunkIndex(data, chunkX, chunkZ);
            if (chunk >= 0) {
                AddUnique(&chunks, chunk);
            }
        }
    }
    return chunks;
}

void ClearTerrainDirtyChunks(Component* component) {
    if (component != nullptr) {
        SetProperty(component, "dirtyChunks", "[]");
    }
}

bool TerrainWorldToUv(const TerrainData& data, const Entity& entity, std::array<float, 3> world,
                      std::array<float, 2>* outUv) {
    if (outUv == nullptr || data.size[0] <= kEpsilon || data.size[2] <= kEpsilon) {
        return false;
    }
    const float scaleX = std::max(std::fabs(entity.scale[0]), kEpsilon);
    const float scaleZ = std::max(std::fabs(entity.scale[2]), kEpsilon);
    const float localX = (world[0] - entity.position[0]) / scaleX;
    const float localZ = (world[2] - entity.position[2]) / scaleZ;
    const float u = localX / data.size[0] + 0.5f;
    const float v = localZ / data.size[2] + 0.5f;
    if (!std::isfinite(u) || !std::isfinite(v)) {
        return false;
    }
    *outUv = {u, v};
    return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
}

std::array<float, 3> TerrainUvToWorld(const TerrainData& data, const Entity& entity, std::array<float, 2> uv,
                                      float localHeight) {
    const float scaleX = std::max(std::fabs(entity.scale[0]), kEpsilon);
    const float scaleY = std::fabs(entity.scale[1]) <= kEpsilon ? 1.0f : entity.scale[1];
    const float scaleZ = std::max(std::fabs(entity.scale[2]), kEpsilon);
    return {
        entity.position[0] + (ClampUv(uv[0]) - 0.5f) * data.size[0] * scaleX,
        entity.position[1] + localHeight * scaleY,
        entity.position[2] + (ClampUv(uv[1]) - 0.5f) * data.size[2] * scaleZ,
    };
}

TerrainSample SampleTerrainLocal(const TerrainData& data, float localX, float localZ) {
    TerrainSample sample;
    if (data.resolution < 2 || data.heights.size() < static_cast<size_t>(TerrainSampleCount(data))) {
        return sample;
    }
    const float u = ClampUv(localX / data.size[0] + 0.5f);
    const float v = ClampUv(localZ / data.size[2] + 0.5f);
    const float fx = u * static_cast<float>(data.resolution - 1);
    const float fz = v * static_cast<float>(data.resolution - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, data.resolution - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, data.resolution - 1);
    const int x1 = std::clamp(x0 + 1, 0, data.resolution - 1);
    const int z1 = std::clamp(z0 + 1, 0, data.resolution - 1);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    const float h00 = TerrainHeightAtSample(data, x0, z0);
    const float h10 = TerrainHeightAtSample(data, x1, z0);
    const float h01 = TerrainHeightAtSample(data, x0, z1);
    const float h11 = TerrainHeightAtSample(data, x1, z1);
    sample.height = Lerp(Lerp(h00, h10, tx), Lerp(h01, h11, tx), tz);
    sample.normal = TerrainNormalAtSample(data, static_cast<int>(std::round(fx)), static_cast<int>(std::round(fz)));
    sample.hole = TerrainIsHoleAtUv(data, {u, v});

    const int layerCount = TerrainLayerCount(data);
    sample.layerWeights.resize(static_cast<size_t>(layerCount), 0.0f);
    for (int layer = 0; layer < layerCount; ++layer) {
        sample.layerWeights[static_cast<size_t>(layer)] = TerrainLayerWeightAtUv(data, layer, {u, v});
    }
    sample.valid = true;
    return sample;
}

TerrainSample SampleTerrainWorld(const TerrainData& data, const Entity& entity, std::array<float, 3> world) {
    std::array<float, 2> uv{};
    if (!TerrainWorldToUv(data, entity, world, &uv)) {
        return {};
    }
    return SampleTerrainLocal(data, (uv[0] - 0.5f) * data.size[0], (uv[1] - 0.5f) * data.size[2]);
}

float TerrainHeightAtSample(const TerrainData& data, int x, int z) {
    if (data.resolution < 1 || data.heights.empty()) {
        return 0.0f;
    }
    x = std::clamp(x, 0, data.resolution - 1);
    z = std::clamp(z, 0, data.resolution - 1);
    const size_t index = static_cast<size_t>(SampleIndex(data, x, z));
    return index < data.heights.size() ? data.heights[index] : 0.0f;
}

std::array<float, 3> TerrainNormalAtSample(const TerrainData& data, int x, int z) {
    x = std::clamp(x, 0, data.resolution - 1);
    z = std::clamp(z, 0, data.resolution - 1);
    const int xl = std::max(0, x - 1);
    const int xr = std::min(data.resolution - 1, x + 1);
    const int zd = std::max(0, z - 1);
    const int zu = std::min(data.resolution - 1, z + 1);
    const float dx = std::max(CellSizeX(data) * static_cast<float>(xr - xl), kEpsilon);
    const float dz = std::max(CellSizeZ(data) * static_cast<float>(zu - zd), kEpsilon);
    const float dhdx = (TerrainHeightAtSample(data, xr, z) - TerrainHeightAtSample(data, xl, z)) / dx;
    const float dhdz = (TerrainHeightAtSample(data, x, zu) - TerrainHeightAtSample(data, x, zd)) / dz;
    return ToArray(Normalize({-dhdx, 1.0f, -dhdz}));
}

bool TerrainRaycastWorld(const TerrainData& data, const Entity& entity, std::array<float, 3> originArray,
                         std::array<float, 3> directionArray, float maxDistance, TerrainRayHit* outHit,
                         bool includeHoles) {
    if (maxDistance <= 0.0f || !IsFiniteArray(originArray) || !IsFiniteArray(directionArray)) {
        return false;
    }
    Vec3 origin = ToVec3(originArray);
    Vec3 direction = Normalize(ToVec3(directionArray), {0.0f, -1.0f, 0.0f});
    if (Length(direction) <= kEpsilon) {
        return false;
    }

    const float step = std::max(0.05f, std::min(CellSizeX(data), CellSizeZ(data)) * 0.45f);
    const int steps = std::max(2, static_cast<int>(std::ceil(maxDistance / step)));
    Vec3 previousPoint = origin;
    float previousDelta = std::numeric_limits<float>::quiet_NaN();
    bool previousInside = false;

    for (int i = 0; i <= steps; ++i) {
        const float distance = std::min(maxDistance, static_cast<float>(i) * step);
        Vec3 point = origin + direction * distance;
        std::array<float, 2> uv{};
        const bool inside = TerrainWorldToUv(data, entity, ToArray(point), &uv);
        if (!inside) {
            previousPoint = point;
            previousInside = false;
            previousDelta = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        const TerrainSample sample = SampleTerrainLocal(data, (uv[0] - 0.5f) * data.size[0], (uv[1] - 0.5f) * data.size[2]);
        if (!sample.valid || (sample.hole && !includeHoles)) {
            previousPoint = point;
            previousInside = false;
            previousDelta = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        const float terrainY = TerrainUvToWorld(data, entity, uv, sample.height)[1];
        const float delta = point.y - terrainY;
        if (delta <= 0.0f && (i == 0 || (previousInside && previousDelta >= 0.0f))) {
            float low = previousInside ? std::max(0.0f, distance - step) : distance;
            float high = distance;
            Vec3 refinedPoint = point;
            TerrainSample refinedSample = sample;
            for (int refine = 0; refine < 8; ++refine) {
                const float mid = (low + high) * 0.5f;
                refinedPoint = origin + direction * mid;
                std::array<float, 2> refinedUv{};
                if (!TerrainWorldToUv(data, entity, ToArray(refinedPoint), &refinedUv)) {
                    low = mid;
                    continue;
                }
                refinedSample =
                    SampleTerrainLocal(data, (refinedUv[0] - 0.5f) * data.size[0], (refinedUv[1] - 0.5f) * data.size[2]);
                if (!refinedSample.valid || (refinedSample.hole && !includeHoles)) {
                    low = mid;
                    continue;
                }
                const float refinedY = TerrainUvToWorld(data, entity, refinedUv, refinedSample.height)[1];
                if (refinedPoint.y > refinedY) {
                    low = mid;
                } else {
                    high = mid;
                }
            }
            const float hitDistance = high;
            Vec3 hitPoint = origin + direction * hitDistance;
            std::array<float, 2> hitUv{};
            TerrainWorldToUv(data, entity, ToArray(hitPoint), &hitUv);
            const TerrainSample hitSample =
                SampleTerrainLocal(data, (hitUv[0] - 0.5f) * data.size[0], (hitUv[1] - 0.5f) * data.size[2]);
            if (!hitSample.valid || (hitSample.hole && !includeHoles)) {
                previousPoint = point;
                previousInside = false;
                previousDelta = std::numeric_limits<float>::quiet_NaN();
                continue;
            }
            const float hitY = TerrainUvToWorld(data, entity, hitUv, hitSample.height)[1];
            hitPoint.y = hitY;
            if (outHit != nullptr) {
                outHit->hit = true;
                outHit->distance = hitDistance;
                outHit->point = ToArray(hitPoint);
                outHit->normal = hitSample.normal;
                outHit->entityId = entity.id;
                outHit->entityName = entity.name;
                outHit->surfaceType = "Heightfield";
            }
            return true;
        }
        previousPoint = point;
        (void)previousPoint;
        previousInside = true;
        previousDelta = delta;
    }
    return false;
}

float TerrainLayerWeightAtUv(const TerrainData& data, int layerIndex, std::array<float, 2> uv) {
    const int layerCount = TerrainLayerCount(data);
    if (layerIndex < 0 || layerIndex >= layerCount || data.weights.empty()) {
        return 0.0f;
    }
    const float fx = ClampUv(uv[0]) * static_cast<float>(data.resolution - 1);
    const float fz = ClampUv(uv[1]) * static_cast<float>(data.resolution - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, data.resolution - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, data.resolution - 1);
    const int x1 = std::clamp(x0 + 1, 0, data.resolution - 1);
    const int z1 = std::clamp(z0 + 1, 0, data.resolution - 1);
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    auto weightAt = [&](int x, int z) {
        const size_t index = static_cast<size_t>(SampleIndex(data, x, z) * layerCount + layerIndex);
        return index < data.weights.size() ? data.weights[index] : 0.0f;
    };
    return Lerp(Lerp(weightAt(x0, z0), weightAt(x1, z0), tx), Lerp(weightAt(x0, z1), weightAt(x1, z1), tx), tz);
}

bool TerrainIsHoleAtUv(const TerrainData& data, std::array<float, 2> uv) {
    if (data.holes.empty()) {
        return false;
    }
    const int x = std::clamp(static_cast<int>(std::round(ClampUv(uv[0]) * static_cast<float>(data.resolution - 1))), 0,
                             data.resolution - 1);
    const int z = std::clamp(static_cast<int>(std::round(ClampUv(uv[1]) * static_cast<float>(data.resolution - 1))), 0,
                             data.resolution - 1);
    const size_t index = static_cast<size_t>(SampleIndex(data, x, z));
    return index < data.holes.size() && data.holes[index] != 0;
}

TerrainBackend TerrainBackendFromString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
                    return std::isspace(c) != 0 || c == '_' || c == '-';
                }),
                value.end());
    if (value == "volumetric" || value == "volume" || value == "terrainvolume" ||
        value == "unifiedvolumetric") {
        return TerrainBackend::Volumetric;
    }
    return TerrainBackend::Heightfield;
}

const char* TerrainBackendLabel(TerrainBackend backend) {
    return backend == TerrainBackend::Volumetric ? "Volumetric" : "Heightfield";
}

bool TerrainHasVolume(const TerrainData& data) {
    return data.volumeEnabled && CaveSampleCount(data.volume) > 0 &&
           data.volume.densities.size() >= static_cast<size_t>(CaveSampleCount(data.volume));
}

bool TerrainUsesVolumetric(const TerrainData& data) {
    return data.backend == TerrainBackend::Volumetric && TerrainHasVolume(data);
}

bool TerrainUsesHeightfield(const TerrainData& data) {
    return data.backend == TerrainBackend::Heightfield || !TerrainUsesVolumetric(data);
}

void EnsureTerrainVolume(TerrainData* data) {
    if (data == nullptr) {
        return;
    }
    data->backend = TerrainBackend::Volumetric;
    if (!data->volumeEnabled) {
        data->volumeEnabled = true;
        data->volume = {};
        data->volume.resolution = {25, 17, 25};
        data->volume.chunkSize = 8;
        data->volume.size = DefaultTerrainVolumeSize(*data);
    }
    const bool needsHeightfieldSeed =
        data->volume.densities.size() != static_cast<size_t>(CaveSampleCount(data->volume));
    data->volume.size[0] = std::max(0.1f, data->size[0]);
    data->volume.size[1] = std::max(DefaultTerrainVolumeSize(*data)[1], data->volume.size[1]);
    data->volume.size[2] = std::max(0.1f, data->size[2]);
    NormalizeCaveVolumeData(&data->volume);
    if (needsHeightfieldSeed) {
        SeedTerrainVolumeFromHeightfield(data);
    }
    NormalizeTerrainData(data);
}

Entity TerrainVolumeProxyEntity(const TerrainData& data, const Entity& terrainEntity) {
    (void)data;
    Entity proxy = terrainEntity;
    return proxy;
}

bool TerrainVolumeWorldToLocal(const TerrainData& data, const Entity& entity, std::array<float, 3> world,
                               std::array<float, 3>* outLocal) {
    if (!TerrainUsesVolumetric(data)) {
        return false;
    }
    return CaveWorldToLocal(data.volume, TerrainVolumeProxyEntity(data, entity), world, outLocal);
}

std::array<float, 3> TerrainVolumeLocalToWorld(const TerrainData& data, const Entity& entity,
                                               std::array<float, 3> local) {
    return CaveLocalToWorld(data.volume, TerrainVolumeProxyEntity(data, entity), local);
}

bool TerrainVolumeRaycastWorld(const TerrainData& data, const Entity& entity, std::array<float, 3> origin,
                               std::array<float, 3> direction, float maxDistance, TerrainVolumeRayHit* outHit) {
    if (!TerrainUsesVolumetric(data)) {
        return false;
    }
    CaveRayHit hit;
    if (!CaveRaycastWorld(data.volume, TerrainVolumeProxyEntity(data, entity), origin, direction, maxDistance, &hit)) {
        return false;
    }
    hit.entityId = entity.id;
    hit.entityName = entity.name;
    if (outHit != nullptr) {
        *outHit = hit;
    }
    return true;
}

bool TerrainVolumeContainsAirAtWorld(const TerrainData& data, const Entity& entity, std::array<float, 3> world) {
    if (!TerrainUsesVolumetric(data)) {
        return false;
    }
    const CaveSample sample = SampleCaveWorld(data.volume, TerrainVolumeProxyEntity(data, entity), world);
    return sample.valid && sample.density < 0.0f;
}

TerrainVolumeBrushResult ApplyTerrainVolumeBrush(TerrainData* data, std::array<float, 3> centerLocal,
                                                 const TerrainVolumeBrushSettings& settings) {
    TerrainVolumeBrushResult result;
    if (data == nullptr) {
        return result;
    }
    NormalizeTerrainData(data);
    if (!TerrainUsesVolumetric(*data)) {
        return result;
    }
    return ApplyCaveBrush(&data->volume, centerLocal, settings);
}

float EvaluateTerrainBrushFalloff(float normalizedDistance, TerrainFalloffCurve curve) {
    const float t = std::clamp(normalizedDistance, 0.0f, 1.0f);
    switch (curve) {
    case TerrainFalloffCurve::Constant:
        return t <= 1.0f ? 1.0f : 0.0f;
    case TerrainFalloffCurve::Linear:
        return 1.0f - t;
    case TerrainFalloffCurve::Bell: {
        const float x = 1.0f - t;
        return x * x;
    }
    case TerrainFalloffCurve::Smooth:
    default: {
        const float x = 1.0f - t;
        return x * x * (3.0f - 2.0f * x);
    }
    }
}

TerrainBrushResult ApplyTerrainBrush(TerrainData* data, std::array<float, 2> centerUv,
                                     const TerrainBrushSettings& settings) {
    TerrainBrushResult result;
    if (data == nullptr) {
        return result;
    }
    NormalizeTerrainData(data);
    centerUv[0] = ClampUv(centerUv[0]);
    centerUv[1] = ClampUv(centerUv[1]);
    const float radius = std::max(0.01f, settings.radius);
    const float strength = std::max(0.0f, settings.strength);
    const float opacity = std::clamp(settings.opacity, 0.0f, 1.0f);
    if (strength <= 0.0f || opacity <= 0.0f) {
        return result;
    }

    std::vector<float> originalHeights;
    if (settings.mode == TerrainBrushMode::Smooth) {
        originalHeights = data->heights;
    }
    const int layerCount = TerrainLayerCount(*data);
    const int activeLayer = std::clamp(settings.activeLayerIndex, 0, std::max(0, layerCount - 1));
    int minX = data->resolution - 1;
    int maxX = 0;
    int minZ = data->resolution - 1;
    int maxZ = 0;

    for (int z = 0; z < data->resolution; ++z) {
        const float v = static_cast<float>(z) / static_cast<float>(data->resolution - 1);
        const float dz = (v - centerUv[1]) * data->size[2];
        for (int x = 0; x < data->resolution; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(data->resolution - 1);
            const float dx = (u - centerUv[0]) * data->size[0];
            const float distance = std::sqrt(dx * dx + dz * dz);
            if (distance > radius) {
                continue;
            }
            const float falloff = EvaluateTerrainBrushFalloff(distance / radius, settings.falloff);
            if (falloff <= 0.0f) {
                continue;
            }
            const int sample = SampleIndex(*data, x, z);
            bool changedSample = false;

            if (settings.mode == TerrainBrushMode::Hole) {
                const float influence = strength * opacity * falloff;
                if (influence < 0.05f || sample < 0 || static_cast<size_t>(sample) >= data->holes.size()) {
                    continue;
                }
                const std::uint8_t desired = settings.invert ? 0u : 1u;
                const std::uint8_t before = data->holes[static_cast<size_t>(sample)];
                if (before != desired) {
                    data->holes[static_cast<size_t>(sample)] = desired;
                    changedSample = true;
                }
            } else if (settings.mode == TerrainBrushMode::Paint) {
                if (layerCount <= 0 || data->weights.empty()) {
                    continue;
                }
                const int weightIndex = sample * layerCount + activeLayer;
                const float before = data->weights[static_cast<size_t>(weightIndex)];
                const float delta = strength * opacity * falloff * (settings.invert ? -1.0f : 1.0f);
                data->weights[static_cast<size_t>(weightIndex)] = std::clamp(before + delta, 0.0f, 1.0f);
                NormalizeTerrainLayerWeightsAt(data, sample);
                changedSample = std::fabs(data->weights[static_cast<size_t>(weightIndex)] - before) > 0.00001f;
            } else {
                float nextHeight = data->heights[static_cast<size_t>(sample)];
                const float influence = std::clamp(strength * opacity * falloff, 0.0f, 1.0f);
                switch (settings.mode) {
                case TerrainBrushMode::RaiseLower:
                    nextHeight += strength * opacity * falloff * (settings.invert ? -1.0f : 1.0f);
                    break;
                case TerrainBrushMode::Flatten:
                    nextHeight = Lerp(nextHeight, settings.targetHeight, influence);
                    break;
                case TerrainBrushMode::SetHeight:
                    nextHeight = Lerp(nextHeight, settings.targetHeight, std::clamp(opacity * falloff, 0.0f, 1.0f));
                    break;
                case TerrainBrushMode::Smooth: {
                    float sum = 0.0f;
                    int count = 0;
                    for (int oz = -1; oz <= 1; ++oz) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            const int sx = std::clamp(x + ox, 0, data->resolution - 1);
                            const int sz = std::clamp(z + oz, 0, data->resolution - 1);
                            sum += originalHeights[static_cast<size_t>(SampleIndex(*data, sx, sz))];
                            ++count;
                        }
                    }
                    nextHeight = Lerp(nextHeight, sum / static_cast<float>(std::max(1, count)), influence);
                    break;
                }
                case TerrainBrushMode::Noise:
                    nextHeight += DeterministicNoise(x, z, settings.noiseSeed) * strength * opacity * falloff *
                                  (settings.invert ? -1.0f : 1.0f);
                    break;
                case TerrainBrushMode::Paint:
                case TerrainBrushMode::Hole:
                case TerrainBrushMode::VolumeTunnel:
                case TerrainBrushMode::VolumeDig:
                case TerrainBrushMode::VolumeFill:
                case TerrainBrushMode::VolumeSmooth:
                case TerrainBrushMode::VolumeFlatten:
                case TerrainBrushMode::VolumePaint:
                    break;
                }
                nextHeight = ClampHeight(*data, nextHeight);
                changedSample = std::fabs(nextHeight - data->heights[static_cast<size_t>(sample)]) > 0.00001f;
                if (changedSample) {
                    data->heights[static_cast<size_t>(sample)] = nextHeight;
                }
            }

            if (changedSample) {
                result.changed = true;
                ++result.affectedSamples;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minZ = std::min(minZ, z);
                maxZ = std::max(maxZ, z);
            }
        }
    }

    if (result.changed) {
        result.materialOnly = settings.mode == TerrainBrushMode::Paint;
        result.dirty.valid = true;
        result.dirty.minX = minX;
        result.dirty.maxX = maxX;
        result.dirty.minZ = minZ;
        result.dirty.maxZ = maxZ;
        result.dirty.chunks = TerrainChunksForDirtyRegion(*data, minX, maxX, minZ, maxZ);
        if (result.materialOnly) {
            data->dirtyMaterialChunks = result.dirty.chunks;
            ++data->materialRevision;
        } else {
            data->dirtyChunks = result.dirty.chunks;
            ++data->editRevision;
        }
    }
    return result;
}

void NormalizeTerrainLayerWeights(TerrainData* data) {
    if (data == nullptr) {
        return;
    }
    const int sampleCount = TerrainSampleCount(*data);
    for (int sample = 0; sample < sampleCount; ++sample) {
        NormalizeTerrainLayerWeightsAt(data, sample);
    }
}

void NormalizeTerrainLayerWeightsAt(TerrainData* data, int sampleIndexValue) {
    if (data == nullptr) {
        return;
    }
    const int layerCount = TerrainLayerCount(*data);
    const int sampleCount = TerrainSampleCount(*data);
    if (layerCount <= 0 || sampleIndexValue < 0 || sampleIndexValue >= sampleCount) {
        return;
    }
    const int base = sampleIndexValue * layerCount;
    float sum = 0.0f;
    for (int layer = 0; layer < layerCount; ++layer) {
        float& weight = data->weights[static_cast<size_t>(base + layer)];
        weight = std::clamp(std::isfinite(weight) ? weight : 0.0f, 0.0f, 1.0f);
        sum += weight;
    }
    if (sum <= kEpsilon) {
        data->weights[static_cast<size_t>(base)] = 1.0f;
        for (int layer = 1; layer < layerCount; ++layer) {
            data->weights[static_cast<size_t>(base + layer)] = 0.0f;
        }
        return;
    }
    for (int layer = 0; layer < layerCount; ++layer) {
        data->weights[static_cast<size_t>(base + layer)] /= sum;
    }
}

TerrainMesh BuildTerrainMesh(const TerrainData& source) {
    TerrainData data = source;
    NormalizeTerrainData(&data);
    TerrainMesh mesh;
    mesh.version = data.version;
    mesh.resolution = data.resolution;
    mesh.chunkSize = data.chunkSize;
    mesh.chunkCountX = TerrainChunkCountX(data);
    mesh.chunkCountZ = TerrainChunkCountZ(data);
    mesh.size = data.size;
    mesh.boundsMin = {-data.size[0] * 0.5f, 0.0f, -data.size[2] * 0.5f};
    mesh.boundsMax = {data.size[0] * 0.5f, data.size[1], data.size[2] * 0.5f};
    mesh.chunks.reserve(static_cast<size_t>(mesh.chunkCountX * mesh.chunkCountZ));
    for (int chunkZ = 0; chunkZ < mesh.chunkCountZ; ++chunkZ) {
        for (int chunkX = 0; chunkX < mesh.chunkCountX; ++chunkX) {
            TerrainMeshChunk chunk = BuildTerrainMeshChunk(data, chunkX, chunkZ);
            for (int axis = 0; axis < 3; ++axis) {
                mesh.boundsMin[static_cast<size_t>(axis)] =
                    std::min(mesh.boundsMin[static_cast<size_t>(axis)], chunk.boundsMin[static_cast<size_t>(axis)]);
                mesh.boundsMax[static_cast<size_t>(axis)] =
                    std::max(mesh.boundsMax[static_cast<size_t>(axis)], chunk.boundsMax[static_cast<size_t>(axis)]);
            }
            mesh.chunks.push_back(std::move(chunk));
        }
    }
    return mesh;
}

TerrainMeshChunk BuildTerrainMeshChunk(const TerrainData& data, int chunkX, int chunkZ) {
    TerrainMeshChunk chunk;
    chunk.chunkX = std::clamp(chunkX, 0, TerrainChunkCountX(data) - 1);
    chunk.chunkZ = std::clamp(chunkZ, 0, TerrainChunkCountZ(data) - 1);
    chunk.index = TerrainChunkIndex(data, chunk.chunkX, chunk.chunkZ);
    const int cellMinX = chunk.chunkX * data.chunkSize;
    const int cellMinZ = chunk.chunkZ * data.chunkSize;
    const int cellMaxX = std::min(data.resolution - 2, cellMinX + data.chunkSize - 1);
    const int cellMaxZ = std::min(data.resolution - 2, cellMinZ + data.chunkSize - 1);
    chunk.sampleMinX = cellMinX;
    chunk.sampleMinZ = cellMinZ;
    chunk.sampleMaxX = cellMaxX + 1;
    chunk.sampleMaxZ = cellMaxZ + 1;
    chunk.boundsMin = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max()};
    chunk.boundsMax = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                       -std::numeric_limits<float>::max()};

    const int width = chunk.sampleMaxX - chunk.sampleMinX + 1;
    const int depth = chunk.sampleMaxZ - chunk.sampleMinZ + 1;
    chunk.vertices.reserve(static_cast<size_t>(width * depth));
    for (int z = chunk.sampleMinZ; z <= chunk.sampleMaxZ; ++z) {
        for (int x = chunk.sampleMinX; x <= chunk.sampleMaxX; ++x) {
            const int sample = SampleIndex(data, x, z);
            const float u = static_cast<float>(x) / static_cast<float>(data.resolution - 1);
            const float v = static_cast<float>(z) / static_cast<float>(data.resolution - 1);
            TerrainMeshVertex vertex;
            const float height = TerrainHeightAtSample(data, x, z);
            vertex.position = {(u - 0.5f) * data.size[0], height, (v - 0.5f) * data.size[2]};
            vertex.normal = TerrainNormalAtSample(data, x, z);
            vertex.uv = {u, v};
            vertex.color = BlendedLayerColor(data, sample, u, v, vertex.normal, height);
            chunk.vertices.push_back(vertex);
            for (int axis = 0; axis < 3; ++axis) {
                chunk.boundsMin[static_cast<size_t>(axis)] =
                    std::min(chunk.boundsMin[static_cast<size_t>(axis)], vertex.position[static_cast<size_t>(axis)]);
                chunk.boundsMax[static_cast<size_t>(axis)] =
                    std::max(chunk.boundsMax[static_cast<size_t>(axis)], vertex.position[static_cast<size_t>(axis)]);
            }
        }
    }

    const int rowWidth = width;
    for (int z = 0; z < depth - 1; ++z) {
        for (int x = 0; x < width - 1; ++x) {
            const int sx = chunk.sampleMinX + x;
            const int sz = chunk.sampleMinZ + z;
            const size_t h00 = static_cast<size_t>(SampleIndex(data, sx, sz));
            const size_t h10 = static_cast<size_t>(SampleIndex(data, sx + 1, sz));
            const size_t h01 = static_cast<size_t>(SampleIndex(data, sx, sz + 1));
            const size_t h11 = static_cast<size_t>(SampleIndex(data, sx + 1, sz + 1));
            if ((h00 < data.holes.size() && data.holes[h00] != 0) || (h10 < data.holes.size() && data.holes[h10] != 0) ||
                (h01 < data.holes.size() && data.holes[h01] != 0) || (h11 < data.holes.size() && data.holes[h11] != 0)) {
                continue;
            }

            const unsigned int i00 = static_cast<unsigned int>(z * rowWidth + x);
            const unsigned int i10 = i00 + 1;
            const unsigned int i01 = static_cast<unsigned int>((z + 1) * rowWidth + x);
            const unsigned int i11 = i01 + 1;
            chunk.indices.push_back(i00);
            chunk.indices.push_back(i01);
            chunk.indices.push_back(i10);
            chunk.indices.push_back(i10);
            chunk.indices.push_back(i01);
            chunk.indices.push_back(i11);
        }
    }
    if (chunk.vertices.empty()) {
        chunk.boundsMin = {0.0f, 0.0f, 0.0f};
        chunk.boundsMax = {0.0f, 0.0f, 0.0f};
    }
    return chunk;
}

bool RebuildTerrainMeshChunks(const TerrainData& data, const std::vector<int>& chunkIndices, TerrainMesh* mesh) {
    if (mesh == nullptr || mesh->chunks.empty() || mesh->resolution != data.resolution ||
        mesh->chunkSize != data.chunkSize || mesh->chunkCountX != TerrainChunkCountX(data) ||
        mesh->chunkCountZ != TerrainChunkCountZ(data)) {
        return false;
    }
    for (int chunkIndex : chunkIndices) {
        if (chunkIndex < 0 || chunkIndex >= static_cast<int>(mesh->chunks.size())) {
            continue;
        }
        const int chunkX = chunkIndex % mesh->chunkCountX;
        const int chunkZ = chunkIndex / mesh->chunkCountX;
        mesh->chunks[static_cast<size_t>(chunkIndex)] = BuildTerrainMeshChunk(data, chunkX, chunkZ);
    }
    mesh->boundsMin = {-data.size[0] * 0.5f, 0.0f, -data.size[2] * 0.5f};
    mesh->boundsMax = {data.size[0] * 0.5f, data.size[1], data.size[2] * 0.5f};
    for (const TerrainMeshChunk& chunk : mesh->chunks) {
        for (int axis = 0; axis < 3; ++axis) {
            mesh->boundsMin[static_cast<size_t>(axis)] =
                std::min(mesh->boundsMin[static_cast<size_t>(axis)], chunk.boundsMin[static_cast<size_t>(axis)]);
            mesh->boundsMax[static_cast<size_t>(axis)] =
                std::max(mesh->boundsMax[static_cast<size_t>(axis)], chunk.boundsMax[static_cast<size_t>(axis)]);
        }
    }
    return true;
}

bool RefreshTerrainMeshChunkMaterials(const TerrainData& data, const std::vector<int>& chunkIndices, TerrainMesh* mesh) {
    if (mesh == nullptr || mesh->chunks.empty() || mesh->resolution != data.resolution ||
        mesh->chunkSize != data.chunkSize || mesh->chunkCountX != TerrainChunkCountX(data) ||
        mesh->chunkCountZ != TerrainChunkCountZ(data)) {
        return false;
    }
    for (int chunkIndex : chunkIndices) {
        if (chunkIndex < 0 || chunkIndex >= static_cast<int>(mesh->chunks.size())) {
            continue;
        }
        TerrainMeshChunk& chunk = mesh->chunks[static_cast<size_t>(chunkIndex)];
        const int width = chunk.sampleMaxX - chunk.sampleMinX + 1;
        const int depth = chunk.sampleMaxZ - chunk.sampleMinZ + 1;
        if (width <= 0 || depth <= 0 ||
            chunk.vertices.size() != static_cast<size_t>(width * depth)) {
            return false;
        }
        for (int localZ = 0; localZ < depth; ++localZ) {
            for (int localX = 0; localX < width; ++localX) {
                const int x = chunk.sampleMinX + localX;
                const int z = chunk.sampleMinZ + localZ;
                if (x < 0 || z < 0 || x >= data.resolution || z >= data.resolution) {
                    return false;
                }
                const size_t vertexIndex = static_cast<size_t>(localZ * width + localX);
                TerrainMeshVertex& vertex = chunk.vertices[vertexIndex];
                const int sample = SampleIndex(data, x, z);
                const float height = TerrainHeightAtSample(data, x, z);
                vertex.color = BlendedLayerColor(data, sample, vertex.uv[0], vertex.uv[1], vertex.normal, height);
            }
        }
    }
    return true;
}

std::string TerrainBrushModeLabel(TerrainBrushMode mode) {
    switch (mode) {
    case TerrainBrushMode::RaiseLower:
        return "Raise/Lower";
    case TerrainBrushMode::Flatten:
        return "Flatten";
    case TerrainBrushMode::Smooth:
        return "Smooth";
    case TerrainBrushMode::SetHeight:
        return "Set Height";
    case TerrainBrushMode::Noise:
        return "Noise";
    case TerrainBrushMode::Paint:
        return "Paint";
    case TerrainBrushMode::Hole:
        return "Hole Cut/Fill";
    case TerrainBrushMode::VolumeTunnel:
        return "Remove Material Tunnel";
    case TerrainBrushMode::VolumeDig:
        return "Remove Material";
    case TerrainBrushMode::VolumeFill:
        return "Add Material";
    case TerrainBrushMode::VolumeSmooth:
        return "Smooth Volume";
    case TerrainBrushMode::VolumeFlatten:
        return "Flatten Density";
    case TerrainBrushMode::VolumePaint:
        return "Paint Material";
    }
    return "Raise/Lower";
}

std::string TerrainFalloffLabel(TerrainFalloffCurve curve) {
    switch (curve) {
    case TerrainFalloffCurve::Smooth:
        return "Smooth";
    case TerrainFalloffCurve::Linear:
        return "Linear";
    case TerrainFalloffCurve::Constant:
        return "Constant";
    case TerrainFalloffCurve::Bell:
        return "Bell";
    }
    return "Smooth";
}

bool RunTerrainSelfTests(std::vector<std::string>* diagnostics) {
    auto fail = [&](const std::string& message) {
        if (diagnostics != nullptr) {
            diagnostics->push_back(message);
        }
        return false;
    };

    TerrainData data;
    data.resolution = 17;
    data.size = {16.0f, 4.0f, 16.0f};
    data.chunkSize = 4;
    data.layers = DefaultTerrainLayers();
    data.layers.front().detailScale = 21.0f;
    data.layers.front().detailStrength = 0.19f;
    data.layers.front().albedoTexture = "Assets/TempTerrainTextures/Test_Ground_Albedo.png";
    data.layers.front().normalTexture = "Assets/TempTerrainTextures/Test_Ground_Normal.png";
    data.layers.front().maskTexture = "Assets/TempTerrainTextures/Test_Ground_Mask.png";
    NormalizeTerrainData(&data);
    if (!TerrainUsesHeightfield(data) || TerrainUsesVolumetric(data) || TerrainHasVolume(data)) {
        return fail("Terrain selftest failed: default terrain did not stay on the heightfield backend.");
    }

    TerrainVolumeBrushSettings accidentalVolumeBrush;
    accidentalVolumeBrush.mode = TerrainVolumeBrushMode::RemoveMaterial;
    accidentalVolumeBrush.radius = 2.0f;
    accidentalVolumeBrush.strength = 1.0f;
    const TerrainVolumeBrushResult accidentalVolumeEdit =
        ApplyTerrainVolumeBrush(&data, {0.0f, -1.0f, 0.0f}, accidentalVolumeBrush);
    if (accidentalVolumeEdit.changed || !TerrainUsesHeightfield(data) || TerrainHasVolume(data)) {
        return fail("Terrain selftest failed: volume brush auto-converted a heightfield terrain.");
    }

    TerrainBrushSettings raise;
    raise.mode = TerrainBrushMode::RaiseLower;
    raise.radius = 2.2f;
    raise.strength = 1.0f;
    TerrainBrushResult raised = ApplyTerrainBrush(&data, {0.5f, 0.5f}, raise);
    if (!raised.changed || raised.affectedSamples <= 0 || raised.dirty.chunks.empty()) {
        return fail("Terrain selftest failed: raise brush did not affect samples/chunks.");
    }
    if (raised.dirty.chunks.size() >= static_cast<size_t>(TerrainChunkCountX(data) * TerrainChunkCountZ(data))) {
        return fail("Terrain selftest failed: focused brush dirtied every chunk.");
    }

    const TerrainSample center = SampleTerrainLocal(data, 0.0f, 0.0f);
    if (!center.valid || center.height <= 0.1f) {
        return fail("Terrain selftest failed: height sampling did not return raised height.");
    }
    if (std::fabs(Length(ToVec3(center.normal)) - 1.0f) > 0.01f) {
        return fail("Terrain selftest failed: sampled normal was not normalized.");
    }
    if (EvaluateTerrainBrushFalloff(0.5f, TerrainFalloffCurve::Linear) < 0.49f ||
        EvaluateTerrainBrushFalloff(1.0f, TerrainFalloffCurve::Smooth) > 0.001f) {
        return fail("Terrain selftest failed: brush falloff returned unexpected values.");
    }

    TerrainBrushSettings smooth;
    smooth.mode = TerrainBrushMode::Smooth;
    smooth.radius = 3.0f;
    smooth.strength = 0.5f;
    const TerrainBrushResult smoothed = ApplyTerrainBrush(&data, {0.5f, 0.5f}, smooth);
    if (!smoothed.changed) {
        return fail("Terrain selftest failed: smooth brush did not modify raised terrain.");
    }

    TerrainBrushSettings paint;
    paint.mode = TerrainBrushMode::Paint;
    paint.radius = 2.5f;
    paint.strength = 0.75f;
    paint.activeLayerIndex = 1;
    const TerrainBrushResult painted = ApplyTerrainBrush(&data, {0.5f, 0.5f}, paint);
    if (!painted.changed || TerrainLayerWeightAtUv(data, 1, {0.5f, 0.5f}) <= 0.1f) {
        return fail("Terrain selftest failed: paint brush did not affect selected layer.");
    }
    const int centerSample = SampleIndex(data, data.resolution / 2, data.resolution / 2);
    float weightSum = 0.0f;
    for (int layer = 0; layer < TerrainLayerCount(data); ++layer) {
        weightSum += data.weights[static_cast<size_t>(centerSample * TerrainLayerCount(data) + layer)];
    }
    if (std::fabs(weightSum - 1.0f) > 0.001f) {
        return fail("Terrain selftest failed: layer weights were not normalized.");
    }

    auto meshIndexCount = [](const TerrainMesh& mesh) {
        size_t count = 0;
        for (const TerrainMeshChunk& chunk : mesh.chunks) {
            count += chunk.indices.size();
        }
        return count;
    };
    const size_t preHoleIndexCount = meshIndexCount(BuildTerrainMesh(data));
    TerrainBrushSettings hole;
    hole.mode = TerrainBrushMode::Hole;
    hole.falloff = TerrainFalloffCurve::Constant;
    hole.radius = 1.2f;
    hole.strength = 1.0f;
    hole.opacity = 1.0f;
    const TerrainBrushResult holed = ApplyTerrainBrush(&data, {0.5f, 0.5f}, hole);
    if (!holed.changed || !TerrainIsHoleAtUv(data, {0.5f, 0.5f})) {
        return fail("Terrain selftest failed: hole brush did not cut the terrain mask.");
    }
    const size_t holedIndexCount = meshIndexCount(BuildTerrainMesh(data));
    if (holedIndexCount >= preHoleIndexCount) {
        return fail("Terrain selftest failed: holed terrain mesh did not remove surface triangles.");
    }

    Component component;
    SaveTerrainDataToComponent(data, &component);
    TerrainData loaded;
    if (!LoadTerrainDataFromComponent(component, &loaded) || loaded.heights.size() != data.heights.size() ||
        loaded.layers.size() != data.layers.size() || std::fabs(SampleTerrainLocal(loaded, 0.0f, 0.0f).height - center.height) > 1.0f) {
        return fail("Terrain selftest failed: serialization roundtrip lost terrain data.");
    }
    if (std::fabs(loaded.layers.front().detailScale - data.layers.front().detailScale) > 0.01f ||
        std::fabs(loaded.layers.front().detailStrength - data.layers.front().detailStrength) > 0.01f) {
        return fail("Terrain selftest failed: serialization roundtrip lost terrain layer detail settings.");
    }
    if (loaded.layers.front().albedoTexture != data.layers.front().albedoTexture ||
        loaded.layers.front().normalTexture != data.layers.front().normalTexture ||
        loaded.layers.front().maskTexture != data.layers.front().maskTexture) {
        return fail("Terrain selftest failed: serialization roundtrip lost terrain texture references.");
    }
    if (!TerrainIsHoleAtUv(loaded, {0.5f, 0.5f})) {
        return fail("Terrain selftest failed: serialization roundtrip lost terrain hole mask.");
    }
    if (!TerrainUsesHeightfield(loaded) || TerrainHasVolume(loaded)) {
        return fail("Terrain selftest failed: heightfield serialization roundtrip gained volume data.");
    }
    Component legacyVolumeComponent = component;
    RemoveProperty(&legacyVolumeComponent, "backend");
    SetProperty(&legacyVolumeComponent, "volumeEnabled", "true");
    SetProperty(&legacyVolumeComponent, "volumeResolution", "3, 3, 3");
    SetProperty(&legacyVolumeComponent, "volumeDensities", "[1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]");
    TerrainData legacyVolumeLoaded;
    if (!LoadTerrainDataFromComponent(legacyVolumeComponent, &legacyVolumeLoaded) ||
        !TerrainUsesHeightfield(legacyVolumeLoaded) || TerrainHasVolume(legacyVolumeLoaded)) {
        return fail("Terrain selftest failed: missing terrain backend did not default to Heightfield.");
    }

    Entity holedTerrainEntity;
    holedTerrainEntity.id = 41;
    holedTerrainEntity.name = "Holed Test Terrain";
    holedTerrainEntity.position = {0.0f, 0.0f, 0.0f};
    holedTerrainEntity.scale = {1.0f, 1.0f, 1.0f};
    TerrainRayHit holedHit;
    if (TerrainRaycastWorld(loaded, holedTerrainEntity, {0.0f, 8.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f, &holedHit)) {
        return fail("Terrain selftest failed: raycast hit a terrain hole.");
    }
    if (!TerrainRaycastWorld(loaded, holedTerrainEntity, {0.0f, 8.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f,
                             &holedHit, true) ||
        !holedHit.hit) {
        return fail("Terrain selftest failed: include-holes raycast could not target a hole for editing.");
    }
    TerrainBrushSettings fillHole = hole;
    fillHole.invert = true;
    const TerrainBrushResult filled = ApplyTerrainBrush(&loaded, {0.5f, 0.5f}, fillHole);
    if (!filled.changed || TerrainIsHoleAtUv(loaded, {0.5f, 0.5f}) ||
        meshIndexCount(BuildTerrainMesh(loaded)) <= holedIndexCount) {
        return fail("Terrain selftest failed: inverted hole brush did not fill the terrain cleanly.");
    }

    TerrainMesh mesh = BuildTerrainMesh(loaded);
    if (mesh.chunks.empty() || mesh.chunks.front().vertices.empty() || mesh.chunks.front().indices.empty()) {
        return fail("Terrain selftest failed: mesh build produced no chunk geometry.");
    }
    for (const TerrainMeshChunk& chunk : mesh.chunks) {
        for (const TerrainMeshVertex& vertex : chunk.vertices) {
            if (std::fabs(Length(ToVec3(vertex.normal)) - 1.0f) > 0.05f) {
                return fail("Terrain selftest failed: mesh vertex normal was not normalized.");
            }
        }
    }

    Entity terrainEntity;
    terrainEntity.id = 42;
    terrainEntity.name = "Test Terrain";
    terrainEntity.position = {0.0f, 0.0f, 0.0f};
    terrainEntity.scale = {1.0f, 1.0f, 1.0f};
    TerrainRayHit hit;
    if (!TerrainRaycastWorld(loaded, terrainEntity, {0.0f, 8.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 20.0f, &hit) ||
        !hit.hit || hit.entityId != terrainEntity.id || hit.point[1] < 0.0f) {
        return fail("Terrain selftest failed: raycast did not hit terrain surface.");
    }

    TerrainData volumeTerrain;
    volumeTerrain.resolution = 33;
    volumeTerrain.chunkSize = 8;
    volumeTerrain.size = {32.0f, 6.0f, 32.0f};
    volumeTerrain.layers = DefaultTerrainLayers();
    NormalizeTerrainData(&volumeTerrain);
    EnsureTerrainVolume(&volumeTerrain);
    volumeTerrain.volume.resolution = {25, 17, 25};
    volumeTerrain.volume.chunkSize = 8;
    volumeTerrain.volume.size = {32.0f, 12.0f, 32.0f};
    volumeTerrain.volume.densities.clear();
    EnsureTerrainVolume(&volumeTerrain);

    Entity volumeTerrainEntity;
    volumeTerrainEntity.id = 88;
    volumeTerrainEntity.name = "Volumetric Test Terrain";
    volumeTerrainEntity.position = {0.0f, 0.0f, 0.0f};
    volumeTerrainEntity.scale = {1.0f, 1.0f, 1.0f};

    if (TerrainVolumeContainsAirAtWorld(volumeTerrain, volumeTerrainEntity,
                                        TerrainVolumeLocalToWorld(volumeTerrain, volumeTerrainEntity,
                                                                  {0.0f, -2.0f, 0.0f}))) {
        return fail("Terrain selftest failed: generated terrain volume was not solid below the surface.");
    }
    if (!TerrainVolumeContainsAirAtWorld(volumeTerrain, volumeTerrainEntity,
                                         TerrainVolumeLocalToWorld(volumeTerrain, volumeTerrainEntity,
                                                                   {0.0f, 2.0f, 0.0f}))) {
        return fail("Terrain selftest failed: generated terrain volume was not air above the surface.");
    }

    CaveBrushSettings tunnel;
    tunnel.mode = CaveBrushMode::Tunnel;
    tunnel.falloff = CaveFalloffCurve::Smooth;
    tunnel.radius = 1.65f;
    tunnel.strength = 1.0f;
    tunnel.opacity = 1.0f;
    tunnel.useSegment = true;
    tunnel.segmentStart = {-11.0f, -2.5f, 0.0f};
    tunnel.segmentEnd = {11.0f, -2.5f, 0.0f};
    CaveBrushResult volumeTunnel = ApplyTerrainVolumeBrush(&volumeTerrain, tunnel.segmentEnd, tunnel);
    if (!volumeTunnel.changed || volumeTunnel.dirty.chunks.empty()) {
        return fail("Terrain selftest failed: terrain-owned tunnel brush did not dirty volume chunks.");
    }
    const int totalVolumeChunks = CaveChunkCountX(volumeTerrain.volume) * CaveChunkCountY(volumeTerrain.volume) *
                                  CaveChunkCountZ(volumeTerrain.volume);
    if (volumeTunnel.dirty.chunks.size() >= static_cast<size_t>(totalVolumeChunks)) {
        return fail("Terrain selftest failed: focused terrain-owned tunnel dirtied every volume chunk.");
    }
    if (volumeTunnel.dirty.chunks.size() < 3) {
        return fail("Terrain selftest failed: multi-chunk terrain-owned tunnel did not cross enough chunks.");
    }

    CaveBrushSettings chamber;
    chamber.mode = CaveBrushMode::Dig;
    chamber.falloff = CaveFalloffCurve::Smooth;
    chamber.radius = 2.2f;
    chamber.strength = 1.0f;
    const CaveBrushResult volumeChamber = ApplyTerrainVolumeBrush(&volumeTerrain, {0.0f, -2.5f, 0.0f}, chamber);
    if (!volumeChamber.changed || volumeChamber.dirty.chunks.empty()) {
        return fail("Terrain selftest failed: terrain-owned chamber brush did not expand carved space.");
    }

    CaveBrushSettings upwardShaft;
    upwardShaft.mode = CaveBrushMode::Tunnel;
    upwardShaft.falloff = CaveFalloffCurve::Smooth;
    upwardShaft.radius = 1.25f;
    upwardShaft.strength = 1.0f;
    upwardShaft.useSegment = true;
    upwardShaft.segmentStart = {0.0f, -2.5f, 0.0f};
    upwardShaft.segmentEnd = {0.0f, 2.5f, 0.0f};
    const CaveBrushResult volumeShaft = ApplyTerrainVolumeBrush(&volumeTerrain, upwardShaft.segmentEnd, upwardShaft);
    if (!volumeShaft.changed ||
        !TerrainVolumeContainsAirAtWorld(volumeTerrain, volumeTerrainEntity,
                                         TerrainVolumeLocalToWorld(volumeTerrain, volumeTerrainEntity,
                                                                   {0.0f, 1.0f, 0.0f}))) {
        return fail("Terrain selftest failed: upward terrain-owned dig did not open a vertical exit.");
    }

    CaveBrushSettings overhang;
    overhang.mode = CaveBrushMode::Fill;
    overhang.falloff = CaveFalloffCurve::Smooth;
    overhang.radius = 1.45f;
    overhang.strength = 2.0f;
    const CaveBrushResult volumeOverhang = ApplyTerrainVolumeBrush(&volumeTerrain, {4.0f, 1.2f, 0.0f}, overhang);
    if (!volumeOverhang.changed ||
        TerrainVolumeContainsAirAtWorld(volumeTerrain, volumeTerrainEntity,
                                        TerrainVolumeLocalToWorld(volumeTerrain, volumeTerrainEntity,
                                                                  {4.0f, 1.2f, 0.0f}))) {
        return fail("Terrain selftest failed: add-material brush did not build solid overhang material.");
    }
    CaveRayHit overhangUndersideHit;
    if (!TerrainVolumeRaycastWorld(volumeTerrain, volumeTerrainEntity,
                                   TerrainVolumeLocalToWorld(volumeTerrain, volumeTerrainEntity, {4.0f, -1.0f, 0.0f}),
                                   {0.0f, 1.0f, 0.0f}, 5.0f, &overhangUndersideHit) ||
        !overhangUndersideHit.hit) {
        return fail("Terrain selftest failed: overhang underside was not generated by the terrain volume.");
    }

    CaveMesh volumeMesh = BuildCaveMesh(volumeTerrain.volume);
    if (volumeMesh.chunks.empty() || volumeMesh.boundsMax[1] <= volumeMesh.boundsMin[1]) {
        return fail("Terrain selftest failed: terrain-owned volume mesh build produced invalid bounds.");
    }
    if (!RebuildCaveMeshChunks(volumeTerrain.volume, volumeTunnel.dirty.chunks, &volumeMesh)) {
        return fail("Terrain selftest failed: terrain-owned volume chunk rebuild failed.");
    }

    const std::array<float, 3> chamberWorld =
        TerrainVolumeLocalToWorld(volumeTerrain, volumeTerrainEntity, {0.0f, -2.5f, 0.0f});
    if (!TerrainVolumeContainsAirAtWorld(volumeTerrain, volumeTerrainEntity, chamberWorld)) {
        return fail("Terrain selftest failed: carved terrain-owned tunnel did not sample as empty air.");
    }
    const std::array<float, 3> tunnelProbeWorld =
        TerrainVolumeLocalToWorld(volumeTerrain, volumeTerrainEntity, {-5.0f, -2.5f, 0.0f});
    if (!TerrainVolumeContainsAirAtWorld(volumeTerrain, volumeTerrainEntity, tunnelProbeWorld)) {
        return fail("Terrain selftest failed: offset terrain-owned tunnel probe was not empty air.");
    }
    CaveRayHit floorHit;
    if (!TerrainVolumeRaycastWorld(volumeTerrain, volumeTerrainEntity, tunnelProbeWorld, {0.0f, -1.0f, 0.0f}, 6.0f,
                                   &floorHit) ||
        !floorHit.hit || floorHit.entityId != volumeTerrainEntity.id) {
        return fail("Terrain selftest failed: terrain-owned volume raycast did not hit tunnel floor.");
    }
    CaveRayHit ceilingHit;
    if (!TerrainVolumeRaycastWorld(volumeTerrain, volumeTerrainEntity, tunnelProbeWorld, {0.0f, 1.0f, 0.0f}, 6.0f,
                                   &ceilingHit) ||
        !ceilingHit.hit) {
        return fail("Terrain selftest failed: terrain-owned volume raycast did not hit tunnel ceiling.");
    }
    CaveRayHit wallHit;
    if (!TerrainVolumeRaycastWorld(volumeTerrain, volumeTerrainEntity, tunnelProbeWorld, {0.0f, 0.0f, 1.0f}, 6.0f,
                                   &wallHit) ||
        !wallHit.hit) {
        return fail("Terrain selftest failed: terrain-owned volume raycast did not hit tunnel wall.");
    }

    Component volumeComponent;
    SaveTerrainDataToComponent(volumeTerrain, &volumeComponent);
    TerrainData reloadedVolumeTerrain;
    if (!LoadTerrainDataFromComponent(volumeComponent, &reloadedVolumeTerrain) ||
        !TerrainUsesVolumetric(reloadedVolumeTerrain) ||
        reloadedVolumeTerrain.volume.densities.size() !=
            static_cast<size_t>(CaveSampleCount(reloadedVolumeTerrain.volume)) ||
        reloadedVolumeTerrain.volume.dirtyChunks.empty()) {
        return fail("Terrain selftest failed: terrain-owned volume serialization roundtrip lost density data.");
    }
    if (!TerrainVolumeContainsAirAtWorld(reloadedVolumeTerrain, volumeTerrainEntity, chamberWorld)) {
        return fail("Terrain selftest failed: reloaded terrain-owned tunnel did not preserve empty space.");
    }

    if (diagnostics != nullptr) {
        diagnostics->push_back("terrain.selftest passed.");
    }
    return true;
}

} // namespace aine

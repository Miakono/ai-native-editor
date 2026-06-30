#include "engine/cave/CaveVolume.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <sstream>

namespace aine {
namespace {

using json = nlohmann::json;

constexpr int kCaveSerializationVersion = 2;
constexpr int kMinCaveResolution = 3;
constexpr int kMaxCaveResolution = 97;
constexpr float kEpsilon = 0.00001f;
constexpr float kSolidDensity = 1.0f;
constexpr float kEmptyDensity = -1.0f;

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

Vec3 operator-(Vec3 left, Vec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
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

float Clamp01(float value) {
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

float ClampDensity(float value) {
    return std::clamp(std::isfinite(value) ? value : kSolidDensity, -1.0f, 1.0f);
}

std::string NormalizeCollisionUpdateMode(std::string mode) {
    if (mode == "ImmediateForSmallEdits" || mode == "Deferred" || mode == "Async" ||
        mode == "DisabledInEditorPreview") {
        return mode;
    }
    return "Deferred";
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

std::array<int, 3> ParseInt3(std::string value, std::array<int, 3> fallback) {
    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream stream(value);
    std::array<int, 3> parsed = fallback;
    if (stream >> parsed[0] >> parsed[1] >> parsed[2]) {
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

std::string Vec3ToString(std::array<float, 3> value) {
    std::ostringstream stream;
    stream.precision(6);
    stream << std::fixed << value[0] << ", " << value[1] << ", " << value[2];
    return stream.str();
}

std::string Int3ToString(std::array<int, 3> value) {
    std::ostringstream stream;
    stream << value[0] << ", " << value[1] << ", " << value[2];
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

std::vector<CaveLayer> DefaultCaveLayers() {
    CaveLayer rock;
    rock.name = "Rock";
    rock.displayColor = {0.42f, 0.41f, 0.39f, 1.0f};
    rock.material = "CaveRock";
    rock.tiling = 8.0f;
    rock.roughness = 0.96f;

    CaveLayer dirt;
    dirt.name = "Dirt";
    dirt.displayColor = {0.38f, 0.31f, 0.25f, 1.0f};
    dirt.material = "CaveDirt";
    dirt.tiling = 9.0f;
    dirt.roughness = 0.98f;

    CaveLayer damp;
    damp.name = "Damp Stone";
    damp.displayColor = {0.32f, 0.34f, 0.34f, 1.0f};
    damp.material = "CaveDampStone";
    damp.tiling = 6.0f;
    damp.roughness = 0.86f;
    return {rock, dirt, damp};
}

std::vector<CaveLayer> ParseLayers(const std::string& value) {
    std::vector<CaveLayer> layers;
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
            CaveLayer layer;
            layer.name = item.value("name", layer.name);
            layer.material = item.value("material", layer.material);
            layer.albedoTexture = item.value("albedoTexture", item.value("texture", layer.albedoTexture));
            layer.tiling = std::max(0.001f, item.value("tiling", layer.tiling));
            layer.roughness = std::clamp(item.value("roughness", layer.roughness), 0.0f, 1.0f);
            layer.metalness = std::clamp(item.value("metalness", layer.metalness), 0.0f, 1.0f);
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

std::string SerializeLayers(const std::vector<CaveLayer>& layers) {
    json serialized = json::array();
    for (const CaveLayer& layer : layers) {
        serialized.push_back({
            {"name", layer.name},
            {"displayColor",
             json::array({layer.displayColor[0], layer.displayColor[1], layer.displayColor[2], layer.displayColor[3]})},
            {"material", layer.material},
            {"albedoTexture", layer.albedoTexture},
            {"tiling", layer.tiling},
            {"roughness", layer.roughness},
            {"metalness", layer.metalness},
        });
    }
    return serialized.dump();
}

float StepX(const CaveVolumeData& data) {
    return data.resolution[0] <= 1 ? 1.0f : data.size[0] / static_cast<float>(data.resolution[0] - 1);
}

float StepY(const CaveVolumeData& data) {
    return data.resolution[1] <= 1 ? 1.0f : data.size[1] / static_cast<float>(data.resolution[1] - 1);
}

float StepZ(const CaveVolumeData& data) {
    return data.resolution[2] <= 1 ? 1.0f : data.size[2] / static_cast<float>(data.resolution[2] - 1);
}

std::array<float, 3> SampleLocalPosition(const CaveVolumeData& data, int x, int y, int z) {
    return {-data.size[0] * 0.5f + static_cast<float>(x) * StepX(data),
            -data.size[1] * 0.5f + static_cast<float>(y) * StepY(data),
            -data.size[2] * 0.5f + static_cast<float>(z) * StepZ(data)};
}

float SampleDensityOnlyLocal(const CaveVolumeData& data, std::array<float, 3> local) {
    std::array<float, 3> uv{};
    if (!CaveLocalToUv(data, local, &uv) || data.densities.size() < static_cast<size_t>(CaveSampleCount(data))) {
        return kEmptyDensity;
    }

    const float fx = uv[0] * static_cast<float>(data.resolution[0] - 1);
    const float fy = uv[1] * static_cast<float>(data.resolution[1] - 1);
    const float fz = uv[2] * static_cast<float>(data.resolution[2] - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, data.resolution[0] - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, data.resolution[1] - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, data.resolution[2] - 1);
    const int x1 = std::min(x0 + 1, data.resolution[0] - 1);
    const int y1 = std::min(y0 + 1, data.resolution[1] - 1);
    const int z1 = std::min(z0 + 1, data.resolution[2] - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float tz = fz - static_cast<float>(z0);
    auto density = [&](int x, int y, int z) {
        return CaveDensityAtSample(data, x, y, z);
    };
    const float c00 = density(x0, y0, z0) * (1.0f - tx) + density(x1, y0, z0) * tx;
    const float c10 = density(x0, y1, z0) * (1.0f - tx) + density(x1, y1, z0) * tx;
    const float c01 = density(x0, y0, z1) * (1.0f - tx) + density(x1, y0, z1) * tx;
    const float c11 = density(x0, y1, z1) * (1.0f - tx) + density(x1, y1, z1) * tx;
    const float c0 = c00 * (1.0f - ty) + c10 * ty;
    const float c1 = c01 * (1.0f - ty) + c11 * ty;
    return c0 * (1.0f - tz) + c1 * tz;
}

float DistancePointSegment(Vec3 point, Vec3 a, Vec3 b) {
    const Vec3 ab = b - a;
    const float lengthSq = Dot(ab, ab);
    if (lengthSq <= kEpsilon) {
        return Length(point - a);
    }
    const float t = std::clamp(Dot(point - a, ab) / lengthSq, 0.0f, 1.0f);
    return Length(point - (a + ab * t));
}

std::array<float, 4> BlendedLayerColor(const CaveVolumeData& data, std::array<float, 3> local) {
    if (data.layers.empty()) {
        return {0.42f, 0.41f, 0.39f, 1.0f};
    }
    std::array<float, 4> color{0.0f, 0.0f, 0.0f, 0.0f};
    float weightSum = 0.0f;
    for (int layerIndex = 0; layerIndex < CaveLayerCount(data); ++layerIndex) {
        const float weight = CaveLayerWeightAtLocal(data, layerIndex, local);
        const CaveLayer& layer = data.layers[static_cast<size_t>(layerIndex)];
        for (int channel = 0; channel < 4; ++channel) {
            color[static_cast<size_t>(channel)] += layer.displayColor[static_cast<size_t>(channel)] * weight;
        }
        weightSum += weight;
    }
    if (weightSum <= kEpsilon) {
        return data.layers.front().displayColor;
    }
    for (float& channel : color) {
        channel = Clamp01(channel / weightSum);
    }
    color[3] = 1.0f;
    return color;
}

CaveMeshVertex InterpolateSurfaceVertex(const CaveVolumeData& data, std::array<float, 3> positionA,
                                        std::array<float, 3> positionB, float densityA, float densityB) {
    const float denominator = densityA - densityB;
    const float t = std::fabs(denominator) <= kEpsilon ? 0.5f : std::clamp(densityA / denominator, 0.0f, 1.0f);
    CaveMeshVertex vertex;
    for (int axis = 0; axis < 3; ++axis) {
        vertex.position[static_cast<size_t>(axis)] =
            positionA[static_cast<size_t>(axis)] +
            (positionB[static_cast<size_t>(axis)] - positionA[static_cast<size_t>(axis)]) * t;
    }
    CaveLocalToUv(data, vertex.position, &vertex.uvw);
    vertex.normal = CaveNormalLocal(data, vertex.position);
    vertex.color = BlendedLayerColor(data, vertex.position);
    return vertex;
}

void AddTriangle(CaveMeshChunk* chunk, CaveMeshVertex a, CaveMeshVertex b, CaveMeshVertex c) {
    if (chunk == nullptr) {
        return;
    }
    const unsigned int base = static_cast<unsigned int>(chunk->vertices.size());
    chunk->vertices.push_back(std::move(a));
    chunk->vertices.push_back(std::move(b));
    chunk->vertices.push_back(std::move(c));
    chunk->indices.push_back(base);
    chunk->indices.push_back(base + 1u);
    chunk->indices.push_back(base + 2u);
}

void AddQuad(CaveMeshChunk* chunk, CaveMeshVertex a, CaveMeshVertex b, CaveMeshVertex c, CaveMeshVertex d) {
    AddTriangle(chunk, a, b, c);
    AddTriangle(chunk, a, c, d);
}

void PolygonizeTetra(const CaveVolumeData& data, const std::array<std::array<float, 3>, 4>& positions,
                     const std::array<float, 4>& densities, CaveMeshChunk* chunk) {
    std::vector<int> solid;
    std::vector<int> empty;
    for (int index = 0; index < 4; ++index) {
        (densities[static_cast<size_t>(index)] > 0.0f ? solid : empty).push_back(index);
    }
    if (solid.empty() || empty.empty()) {
        return;
    }

    auto edgeVertex = [&](int a, int b) {
        return InterpolateSurfaceVertex(data, positions[static_cast<size_t>(a)], positions[static_cast<size_t>(b)],
                                        densities[static_cast<size_t>(a)], densities[static_cast<size_t>(b)]);
    };

    if (solid.size() == 1) {
        AddTriangle(chunk, edgeVertex(solid[0], empty[0]), edgeVertex(solid[0], empty[1]),
                    edgeVertex(solid[0], empty[2]));
    } else if (solid.size() == 3) {
        AddTriangle(chunk, edgeVertex(empty[0], solid[2]), edgeVertex(empty[0], solid[1]),
                    edgeVertex(empty[0], solid[0]));
    } else if (solid.size() == 2) {
        AddQuad(chunk, edgeVertex(solid[0], empty[0]), edgeVertex(solid[0], empty[1]),
                edgeVertex(solid[1], empty[1]), edgeVertex(solid[1], empty[0]));
    }
}

} // namespace

Component MakeCaveVolumeComponent(std::array<int, 3> resolution, std::array<float, 3> size, int chunkSize) {
    CaveVolumeData data;
    data.resolution = resolution;
    data.size = size;
    data.chunkSize = chunkSize;
    data.layers = DefaultCaveLayers();
    NormalizeCaveVolumeData(&data);
    Component component{"CaveVolume", {}};
    SaveCaveVolumeDataToComponent(data, &component);
    return component;
}

bool LoadCaveVolumeDataFromComponent(const Component& component, CaveVolumeData* outData, std::string* error) {
    if (outData == nullptr) {
        if (error != nullptr) {
            *error = "Cave output pointer was null.";
        }
        return false;
    }
    if (component.type != "CaveVolume") {
        if (error != nullptr) {
            *error = "Component is not CaveVolume.";
        }
        return false;
    }

    CaveVolumeData data;
    data.version = ParseInt(GetProperty(component, "version", std::to_string(kCaveSerializationVersion)),
                            kCaveSerializationVersion);
    data.resolution = ParseInt3(GetProperty(component, "resolution", "25, 17, 25"), data.resolution);
    data.chunkSize = ParseInt(GetProperty(component, "chunkSize", "8"), 8);
    data.size = ParseVec3(GetProperty(component, "size", "28, 14, 28"), data.size);
    data.collisionEnabled = ParseBool(GetProperty(component, "collisionEnabled", "true"), true);
    data.collisionUpdateMode =
        NormalizeCollisionUpdateMode(GetProperty(component, "collisionUpdateMode", data.collisionUpdateMode));
    data.editRevision = ParseInt(GetProperty(component, "editRevision", "0"), 0);
    data.materialRevision = ParseInt(GetProperty(component, "materialRevision", "0"), 0);
    data.densities = ParseFloatList(GetProperty(component, "densities"));
    if (data.version < 2) {
        for (float& density : data.densities) {
            density = -density;
        }
    }
    data.layers = ParseLayers(GetProperty(component, "layers"));
    data.weights = ParseFloatList(GetProperty(component, "weights"));
    data.dirtyChunks = ParseIntList(GetProperty(component, "dirtyChunks"));
    data.dirtyMaterialChunks = ParseIntList(GetProperty(component, "dirtyMaterialChunks"));
    NormalizeCaveVolumeData(&data);
    *outData = std::move(data);
    return true;
}

void SaveCaveVolumeDataToComponent(const CaveVolumeData& source, Component* component) {
    if (component == nullptr) {
        return;
    }
    CaveVolumeData data = source;
    NormalizeCaveVolumeData(&data);
    component->type = "CaveVolume";
    SetProperty(component, "version", std::to_string(data.version));
    SetProperty(component, "resolution", Int3ToString(data.resolution));
    SetProperty(component, "size", Vec3ToString(data.size));
    SetProperty(component, "chunkSize", std::to_string(data.chunkSize));
    SetProperty(component, "collisionEnabled", data.collisionEnabled ? "true" : "false");
    SetProperty(component, "collisionUpdateMode", NormalizeCollisionUpdateMode(data.collisionUpdateMode));
    SetProperty(component, "editRevision", std::to_string(data.editRevision));
    SetProperty(component, "materialRevision", std::to_string(data.materialRevision));
    SetProperty(component, "densities", JsonArrayString(data.densities));
    SetProperty(component, "layers", SerializeLayers(data.layers));
    SetProperty(component, "weights", JsonArrayString(data.weights));
    SetProperty(component, "dirtyChunks", JsonArrayString(data.dirtyChunks));
    SetProperty(component, "dirtyMaterialChunks", JsonArrayString(data.dirtyMaterialChunks));
}

void NormalizeCaveVolumeData(CaveVolumeData* data) {
    if (data == nullptr) {
        return;
    }
    data->version = kCaveSerializationVersion;
    for (int& resolution : data->resolution) {
        resolution = std::clamp(resolution, kMinCaveResolution, kMaxCaveResolution);
    }
    for (float& axisSize : data->size) {
        axisSize = std::max(0.1f, std::isfinite(axisSize) ? axisSize : 16.0f);
    }
    data->chunkSize = std::clamp(data->chunkSize, 1, std::max(1, std::max({data->resolution[0], data->resolution[1], data->resolution[2]}) - 1));
    data->collisionUpdateMode = NormalizeCollisionUpdateMode(data->collisionUpdateMode);
    if (data->layers.empty()) {
        data->layers = DefaultCaveLayers();
    }
    for (CaveLayer& layer : data->layers) {
        if (layer.name.empty()) {
            layer.name = "Cave Layer";
        }
        layer.tiling = std::max(0.001f, std::isfinite(layer.tiling) ? layer.tiling : 1.0f);
        layer.roughness = std::clamp(std::isfinite(layer.roughness) ? layer.roughness : 0.94f, 0.0f, 1.0f);
        layer.metalness = std::clamp(std::isfinite(layer.metalness) ? layer.metalness : 0.0f, 0.0f, 1.0f);
        for (float& channel : layer.displayColor) {
            channel = Clamp01(channel);
        }
    }

    const int sampleCount = CaveSampleCount(*data);
    data->densities.resize(static_cast<size_t>(sampleCount), kSolidDensity);
    for (float& density : data->densities) {
        density = ClampDensity(density);
    }

    const int layerCount = CaveLayerCount(*data);
    const size_t requiredWeightCount = static_cast<size_t>(sampleCount * layerCount);
    if (data->weights.size() != requiredWeightCount) {
        std::vector<float> resized(requiredWeightCount, 0.0f);
        for (int sample = 0; sample < sampleCount; ++sample) {
            resized[static_cast<size_t>(sample * layerCount)] = 1.0f;
        }
        data->weights = std::move(resized);
    }
    NormalizeCaveLayerWeights(data);

    std::vector<int> normalizedDirty;
    const int chunkTotal = CaveChunkCountX(*data) * CaveChunkCountY(*data) * CaveChunkCountZ(*data);
    for (int chunk : data->dirtyChunks) {
        if (chunk >= 0 && chunk < chunkTotal &&
            std::find(normalizedDirty.begin(), normalizedDirty.end(), chunk) == normalizedDirty.end()) {
            normalizedDirty.push_back(chunk);
        }
    }
    data->dirtyChunks = std::move(normalizedDirty);
    std::vector<int> normalizedMaterialDirty;
    for (int chunk : data->dirtyMaterialChunks) {
        if (chunk >= 0 && chunk < chunkTotal &&
            std::find(normalizedMaterialDirty.begin(), normalizedMaterialDirty.end(), chunk) ==
                normalizedMaterialDirty.end()) {
            normalizedMaterialDirty.push_back(chunk);
        }
    }
    data->dirtyMaterialChunks = std::move(normalizedMaterialDirty);
}

int CaveSampleCount(const CaveVolumeData& data) {
    return std::max(0, data.resolution[0] * data.resolution[1] * data.resolution[2]);
}

int CaveLayerCount(const CaveVolumeData& data) {
    return static_cast<int>(data.layers.size());
}

int CaveSampleIndex(const CaveVolumeData& data, int x, int y, int z) {
    return (z * data.resolution[1] + y) * data.resolution[0] + x;
}

int CaveChunkCountX(const CaveVolumeData& data) {
    return std::max(1, (data.resolution[0] - 2) / std::max(1, data.chunkSize) + 1);
}

int CaveChunkCountY(const CaveVolumeData& data) {
    return std::max(1, (data.resolution[1] - 2) / std::max(1, data.chunkSize) + 1);
}

int CaveChunkCountZ(const CaveVolumeData& data) {
    return std::max(1, (data.resolution[2] - 2) / std::max(1, data.chunkSize) + 1);
}

int CaveChunkIndex(const CaveVolumeData& data, int chunkX, int chunkY, int chunkZ) {
    const int countX = CaveChunkCountX(data);
    const int countY = CaveChunkCountY(data);
    const int countZ = CaveChunkCountZ(data);
    if (chunkX < 0 || chunkX >= countX || chunkY < 0 || chunkY >= countY || chunkZ < 0 || chunkZ >= countZ) {
        return -1;
    }
    return (chunkZ * countY + chunkY) * countX + chunkX;
}

std::vector<int> CaveChunksForDirtyRegion(const CaveVolumeData& data, int minX, int maxX, int minY, int maxY,
                                          int minZ, int maxZ) {
    std::vector<int> chunks;
    minX = std::clamp(minX, 0, data.resolution[0] - 1);
    maxX = std::clamp(maxX, 0, data.resolution[0] - 1);
    minY = std::clamp(minY, 0, data.resolution[1] - 1);
    maxY = std::clamp(maxY, 0, data.resolution[1] - 1);
    minZ = std::clamp(minZ, 0, data.resolution[2] - 1);
    maxZ = std::clamp(maxZ, 0, data.resolution[2] - 1);
    for (int chunkZ = minZ / data.chunkSize; chunkZ <= maxZ / data.chunkSize; ++chunkZ) {
        for (int chunkY = minY / data.chunkSize; chunkY <= maxY / data.chunkSize; ++chunkY) {
            for (int chunkX = minX / data.chunkSize; chunkX <= maxX / data.chunkSize; ++chunkX) {
                const int chunk = CaveChunkIndex(data, chunkX, chunkY, chunkZ);
                if (chunk >= 0 && std::find(chunks.begin(), chunks.end(), chunk) == chunks.end()) {
                    chunks.push_back(chunk);
                }
            }
        }
    }
    return chunks;
}

bool CaveWorldToLocal(const CaveVolumeData& data, const Entity& entity, std::array<float, 3> world,
                      std::array<float, 3>* outLocal) {
    if (outLocal == nullptr) {
        return false;
    }
    const float scaleX = std::max(std::fabs(entity.scale[0]), kEpsilon);
    const float scaleY = std::max(std::fabs(entity.scale[1]), kEpsilon);
    const float scaleZ = std::max(std::fabs(entity.scale[2]), kEpsilon);
    *outLocal = {(world[0] - entity.position[0]) / scaleX,
                 (world[1] - entity.position[1]) / scaleY,
                 (world[2] - entity.position[2]) / scaleZ};
    return (*outLocal)[0] >= -data.size[0] * 0.5f && (*outLocal)[0] <= data.size[0] * 0.5f &&
           (*outLocal)[1] >= -data.size[1] * 0.5f && (*outLocal)[1] <= data.size[1] * 0.5f &&
           (*outLocal)[2] >= -data.size[2] * 0.5f && (*outLocal)[2] <= data.size[2] * 0.5f;
}

std::array<float, 3> CaveLocalToWorld(const CaveVolumeData&, const Entity& entity, std::array<float, 3> local) {
    return {entity.position[0] + local[0] * entity.scale[0],
            entity.position[1] + local[1] * entity.scale[1],
            entity.position[2] + local[2] * entity.scale[2]};
}

bool CaveLocalToUv(const CaveVolumeData& data, std::array<float, 3> local, std::array<float, 3>* outUv) {
    if (outUv == nullptr || data.size[0] <= kEpsilon || data.size[1] <= kEpsilon || data.size[2] <= kEpsilon) {
        return false;
    }
    *outUv = {local[0] / data.size[0] + 0.5f, local[1] / data.size[1] + 0.5f, local[2] / data.size[2] + 0.5f};
    return (*outUv)[0] >= 0.0f && (*outUv)[0] <= 1.0f && (*outUv)[1] >= 0.0f && (*outUv)[1] <= 1.0f &&
           (*outUv)[2] >= 0.0f && (*outUv)[2] <= 1.0f;
}

std::array<float, 3> CaveUvToLocal(const CaveVolumeData& data, std::array<float, 3> uv) {
    return {(Clamp01(uv[0]) - 0.5f) * data.size[0], (Clamp01(uv[1]) - 0.5f) * data.size[1],
            (Clamp01(uv[2]) - 0.5f) * data.size[2]};
}

CaveSample SampleCaveLocal(const CaveVolumeData& data, std::array<float, 3> local) {
    CaveSample sample;
    std::array<float, 3> uv{};
    if (!CaveLocalToUv(data, local, &uv) || data.densities.size() < static_cast<size_t>(CaveSampleCount(data))) {
        return sample;
    }
    sample.valid = true;
    sample.density = SampleDensityOnlyLocal(data, local);
    sample.normal = CaveNormalLocal(data, local);
    sample.layerWeights.resize(static_cast<size_t>(CaveLayerCount(data)), 0.0f);
    for (int layer = 0; layer < CaveLayerCount(data); ++layer) {
        sample.layerWeights[static_cast<size_t>(layer)] = CaveLayerWeightAtLocal(data, layer, local);
    }
    return sample;
}

CaveSample SampleCaveWorld(const CaveVolumeData& data, const Entity& entity, std::array<float, 3> world) {
    std::array<float, 3> local{};
    if (!CaveWorldToLocal(data, entity, world, &local)) {
        return {};
    }
    return SampleCaveLocal(data, local);
}

float CaveDensityAtSample(const CaveVolumeData& data, int x, int y, int z) {
    x = std::clamp(x, 0, data.resolution[0] - 1);
    y = std::clamp(y, 0, data.resolution[1] - 1);
    z = std::clamp(z, 0, data.resolution[2] - 1);
    const int index = CaveSampleIndex(data, x, y, z);
    if (index < 0 || index >= static_cast<int>(data.densities.size())) {
        return kSolidDensity;
    }
    return data.densities[static_cast<size_t>(index)];
}

std::array<float, 3> CaveNormalLocal(const CaveVolumeData& data, std::array<float, 3> local) {
    const float sx = std::max(StepX(data), 0.01f);
    const float sy = std::max(StepY(data), 0.01f);
    const float sz = std::max(StepZ(data), 0.01f);
    auto sample = [&](std::array<float, 3> p) {
        return SampleDensityOnlyLocal(data, p);
    };
    const float dx = sample({local[0] + sx, local[1], local[2]}) - sample({local[0] - sx, local[1], local[2]});
    const float dy = sample({local[0], local[1] + sy, local[2]}) - sample({local[0], local[1] - sy, local[2]});
    const float dz = sample({local[0], local[1], local[2] + sz}) - sample({local[0], local[1], local[2] - sz});
    return ToArray(Normalize({-dx / (sx * 2.0f), -dy / (sy * 2.0f), -dz / (sz * 2.0f)}, {0.0f, 1.0f, 0.0f}));
}

float CaveLayerWeightAtLocal(const CaveVolumeData& data, int layerIndex, std::array<float, 3> local) {
    const int layerCount = CaveLayerCount(data);
    if (layerIndex < 0 || layerIndex >= layerCount || data.weights.size() < static_cast<size_t>(CaveSampleCount(data) * layerCount)) {
        return layerIndex == 0 ? 1.0f : 0.0f;
    }
    std::array<float, 3> uv{};
    if (!CaveLocalToUv(data, local, &uv)) {
        return 0.0f;
    }
    const int x = std::clamp(static_cast<int>(std::round(uv[0] * static_cast<float>(data.resolution[0] - 1))), 0,
                             data.resolution[0] - 1);
    const int y = std::clamp(static_cast<int>(std::round(uv[1] * static_cast<float>(data.resolution[1] - 1))), 0,
                             data.resolution[1] - 1);
    const int z = std::clamp(static_cast<int>(std::round(uv[2] * static_cast<float>(data.resolution[2] - 1))), 0,
                             data.resolution[2] - 1);
    return data.weights[static_cast<size_t>(CaveSampleIndex(data, x, y, z) * layerCount + layerIndex)];
}

bool CaveRaycastWorld(const CaveVolumeData& data, const Entity& entity, std::array<float, 3> originArray,
                      std::array<float, 3> directionArray, float maxDistance, CaveRayHit* outHit) {
    const Vec3 origin = ToVec3(originArray);
    const Vec3 direction = Normalize(ToVec3(directionArray), {0.0f, -1.0f, 0.0f});
    const float step = std::max(0.05f, std::min({StepX(data), StepY(data), StepZ(data)}) * 0.45f *
                                         std::max({std::fabs(entity.scale[0]), std::fabs(entity.scale[1]),
                                                   std::fabs(entity.scale[2]), 0.001f}));
    bool hasPrevious = false;
    float previousDensity = kEmptyDensity;
    Vec3 previousPoint = origin;

    for (float distance = 0.0f; distance <= maxDistance; distance += step) {
        const Vec3 point = origin + direction * distance;
        CaveSample sample = SampleCaveWorld(data, entity, ToArray(point));
        if (!sample.valid) {
            hasPrevious = false;
            previousDensity = kEmptyDensity;
            previousPoint = point;
            continue;
        }
        if (hasPrevious && previousDensity * sample.density <= 0.0f) {
            float low = std::max(0.0f, distance - step);
            float high = distance;
            CaveSample refined = sample;
            Vec3 refinedPoint = point;
            for (int iteration = 0; iteration < 8; ++iteration) {
                const float mid = (low + high) * 0.5f;
                refinedPoint = origin + direction * mid;
                refined = SampleCaveWorld(data, entity, ToArray(refinedPoint));
                if (!refined.valid) {
                    low = mid;
                    continue;
                }
                if (previousDensity * refined.density <= 0.0f) {
                    high = mid;
                    sample = refined;
                } else {
                    low = mid;
                    previousDensity = refined.density;
                }
            }
            if (outHit != nullptr) {
                outHit->hit = true;
                outHit->distance = high;
                outHit->point = ToArray(refinedPoint);
                outHit->normal = refined.normal;
                outHit->entityId = entity.id;
                outHit->entityName = entity.name;
            }
            return true;
        }
        hasPrevious = true;
        previousDensity = sample.density;
        previousPoint = point;
    }
    (void)previousPoint;
    return false;
}

float EvaluateCaveBrushFalloff(float normalizedDistance, CaveFalloffCurve curve) {
    const float t = std::clamp(normalizedDistance, 0.0f, 1.0f);
    switch (curve) {
    case CaveFalloffCurve::Constant:
        return t <= 1.0f ? 1.0f : 0.0f;
    case CaveFalloffCurve::Linear:
        return 1.0f - t;
    case CaveFalloffCurve::Bell:
        return std::exp(-t * t * 4.0f);
    case CaveFalloffCurve::Smooth:
        return 1.0f - (t * t * (3.0f - 2.0f * t));
    }
    return 1.0f - t;
}

CaveBrushResult ApplyCaveBrush(CaveVolumeData* data, std::array<float, 3> centerLocal,
                               const CaveBrushSettings& settings) {
    CaveBrushResult result;
    if (data == nullptr) {
        return result;
    }
    NormalizeCaveVolumeData(data);
    const float radius = std::max(0.01f, settings.radius);
    const float influenceScale = std::clamp(settings.strength * settings.opacity, 0.0f, 8.0f);
    const Vec3 center = ToVec3(centerLocal);
    const Vec3 segmentA = ToVec3(settings.segmentStart);
    const Vec3 segmentB = ToVec3(settings.segmentEnd);
    const bool useSegment = settings.useSegment || settings.mode == CaveBrushMode::Tunnel;
    const bool dig = (settings.mode == CaveBrushMode::Dig || settings.mode == CaveBrushMode::Tunnel) != settings.invert;
    const int layerCount = CaveLayerCount(*data);
    const int activeLayer = std::clamp(settings.activeLayerIndex, 0, std::max(0, layerCount - 1));
    std::vector<float> original;
    if (settings.mode == CaveBrushMode::Smooth) {
        original = data->densities;
    }

    int minX = data->resolution[0] - 1;
    int minY = data->resolution[1] - 1;
    int minZ = data->resolution[2] - 1;
    int maxX = 0;
    int maxY = 0;
    int maxZ = 0;

    for (int z = 0; z < data->resolution[2]; ++z) {
        for (int y = 0; y < data->resolution[1]; ++y) {
            for (int x = 0; x < data->resolution[0]; ++x) {
                const Vec3 position = ToVec3(SampleLocalPosition(*data, x, y, z));
                const float distance = useSegment ? DistancePointSegment(position, segmentA, segmentB)
                                                  : Length(position - center);
                if (distance > radius) {
                    continue;
                }
                const float falloff = EvaluateCaveBrushFalloff(distance / radius, settings.falloff);
                const float influence = falloff * influenceScale;
                if (influence <= kEpsilon) {
                    continue;
                }
                const int sampleIndex = CaveSampleIndex(*data, x, y, z);
                float before = data->densities[static_cast<size_t>(sampleIndex)];
                float after = before;
                bool changedSample = false;
                switch (settings.mode) {
                case CaveBrushMode::RemoveMaterial:
                case CaveBrushMode::Tunnel:
                case CaveBrushMode::AddMaterial:
                    {
                        const float brushSdf = std::max(0.0f, radius - distance) * std::max(0.01f, influence);
                        after = dig ? std::min(before, -brushSdf) : std::max(before, brushSdf);
                    }
                    changedSample = std::fabs(after - before) > 0.0001f;
                    break;
                case CaveBrushMode::Flatten:
                    after = before + (std::clamp(settings.targetDensity, -1.0f, 1.0f) - before) *
                                         std::clamp(influence, 0.0f, 1.0f);
                    changedSample = std::fabs(after - before) > 0.0001f;
                    break;
                case CaveBrushMode::Smooth: {
                    float sum = 0.0f;
                    int count = 0;
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int nx = std::clamp(x + dx, 0, data->resolution[0] - 1);
                                const int ny = std::clamp(y + dy, 0, data->resolution[1] - 1);
                                const int nz = std::clamp(z + dz, 0, data->resolution[2] - 1);
                                sum += original[static_cast<size_t>(CaveSampleIndex(*data, nx, ny, nz))];
                                ++count;
                            }
                        }
                    }
                    const float average = count <= 0 ? before : sum / static_cast<float>(count);
                    after = before + (average - before) * std::clamp(influence, 0.0f, 1.0f);
                    if (before > kEpsilon && after <= kEpsilon) {
                        after = kEpsilon;
                    } else if (before < -kEpsilon && after >= -kEpsilon) {
                        after = -kEpsilon;
                    }
                    changedSample = std::fabs(after - before) > 0.0001f;
                    break;
                }
                case CaveBrushMode::Paint:
                    if (layerCount > 0 && sampleIndex * layerCount + activeLayer < static_cast<int>(data->weights.size())) {
                        const size_t weightIndex = static_cast<size_t>(sampleIndex * layerCount + activeLayer);
                        const float beforeWeight = data->weights[weightIndex];
                        data->weights[weightIndex] = std::clamp(beforeWeight + influence, 0.0f, 1.0f);
                        NormalizeCaveLayerWeightsAt(data, sampleIndex);
                        changedSample = std::fabs(data->weights[weightIndex] - beforeWeight) > 0.0001f;
                        after = before;
                    }
                    break;
                }
                after = ClampDensity(after);
                if (changedSample) {
                    if (settings.mode != CaveBrushMode::Paint) {
                        data->densities[static_cast<size_t>(sampleIndex)] = after;
                    }
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    minZ = std::min(minZ, z);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                    maxZ = std::max(maxZ, z);
                    result.changed = true;
                    ++result.affectedSamples;
                }
            }
        }
    }

    if (result.changed) {
        result.materialOnly = settings.mode == CaveBrushMode::Paint;
        result.dirty.valid = true;
        result.dirty.minX = minX;
        result.dirty.maxX = maxX;
        result.dirty.minY = minY;
        result.dirty.maxY = maxY;
        result.dirty.minZ = minZ;
        result.dirty.maxZ = maxZ;
        result.dirty.chunks = CaveChunksForDirtyRegion(*data, minX, maxX, minY, maxY, minZ, maxZ);
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

void NormalizeCaveLayerWeights(CaveVolumeData* data) {
    if (data == nullptr) {
        return;
    }
    for (int sample = 0; sample < CaveSampleCount(*data); ++sample) {
        NormalizeCaveLayerWeightsAt(data, sample);
    }
}

void NormalizeCaveLayerWeightsAt(CaveVolumeData* data, int sampleIndexValue) {
    if (data == nullptr) {
        return;
    }
    const int layerCount = CaveLayerCount(*data);
    const int sampleCount = CaveSampleCount(*data);
    if (sampleIndexValue < 0 || sampleIndexValue >= sampleCount || layerCount <= 0 ||
        data->weights.size() < static_cast<size_t>(sampleCount * layerCount)) {
        return;
    }
    const int base = sampleIndexValue * layerCount;
    float sum = 0.0f;
    for (int layer = 0; layer < layerCount; ++layer) {
        float& weight = data->weights[static_cast<size_t>(base + layer)];
        weight = std::max(0.0f, std::isfinite(weight) ? weight : 0.0f);
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

CaveMesh BuildCaveMesh(const CaveVolumeData& source) {
    CaveVolumeData data = source;
    NormalizeCaveVolumeData(&data);
    CaveMesh mesh;
    mesh.version = data.version;
    mesh.resolution = data.resolution;
    mesh.chunkSize = data.chunkSize;
    mesh.chunkCountX = CaveChunkCountX(data);
    mesh.chunkCountY = CaveChunkCountY(data);
    mesh.chunkCountZ = CaveChunkCountZ(data);
    mesh.size = data.size;
    mesh.boundsMin = {-data.size[0] * 0.5f, -data.size[1] * 0.5f, -data.size[2] * 0.5f};
    mesh.boundsMax = {data.size[0] * 0.5f, data.size[1] * 0.5f, data.size[2] * 0.5f};
    for (int z = 0; z < mesh.chunkCountZ; ++z) {
        for (int y = 0; y < mesh.chunkCountY; ++y) {
            for (int x = 0; x < mesh.chunkCountX; ++x) {
                mesh.chunks.push_back(BuildCaveMeshChunk(data, x, y, z));
            }
        }
    }
    return mesh;
}

CaveMeshChunk BuildCaveMeshChunk(const CaveVolumeData& data, int chunkX, int chunkY, int chunkZ) {
    CaveMeshChunk chunk;
    chunk.chunkX = std::clamp(chunkX, 0, CaveChunkCountX(data) - 1);
    chunk.chunkY = std::clamp(chunkY, 0, CaveChunkCountY(data) - 1);
    chunk.chunkZ = std::clamp(chunkZ, 0, CaveChunkCountZ(data) - 1);
    chunk.index = CaveChunkIndex(data, chunk.chunkX, chunk.chunkY, chunk.chunkZ);
    chunk.sampleMinX = chunk.chunkX * data.chunkSize;
    chunk.sampleMinY = chunk.chunkY * data.chunkSize;
    chunk.sampleMinZ = chunk.chunkZ * data.chunkSize;
    chunk.sampleMaxX = std::min(data.resolution[0] - 1, chunk.sampleMinX + data.chunkSize);
    chunk.sampleMaxY = std::min(data.resolution[1] - 1, chunk.sampleMinY + data.chunkSize);
    chunk.sampleMaxZ = std::min(data.resolution[2] - 1, chunk.sampleMinZ + data.chunkSize);
    chunk.boundsMin = SampleLocalPosition(data, chunk.sampleMinX, chunk.sampleMinY, chunk.sampleMinZ);
    chunk.boundsMax = SampleLocalPosition(data, chunk.sampleMaxX, chunk.sampleMaxY, chunk.sampleMaxZ);

    constexpr int cubeCorners[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                                       {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    constexpr int tetrahedra[6][4] = {{0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6},
                                      {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}};

    for (int z = chunk.sampleMinZ; z < chunk.sampleMaxZ; ++z) {
        for (int y = chunk.sampleMinY; y < chunk.sampleMaxY; ++y) {
            for (int x = chunk.sampleMinX; x < chunk.sampleMaxX; ++x) {
                std::array<std::array<float, 3>, 8> positions{};
                std::array<float, 8> densities{};
                for (int corner = 0; corner < 8; ++corner) {
                    const int sx = x + cubeCorners[corner][0];
                    const int sy = y + cubeCorners[corner][1];
                    const int sz = z + cubeCorners[corner][2];
                    positions[static_cast<size_t>(corner)] = SampleLocalPosition(data, sx, sy, sz);
                    densities[static_cast<size_t>(corner)] = CaveDensityAtSample(data, sx, sy, sz);
                }
                for (const auto& tetrahedron : tetrahedra) {
                    std::array<std::array<float, 3>, 4> tetraPositions{};
                    std::array<float, 4> tetraDensities{};
                    for (int i = 0; i < 4; ++i) {
                        tetraPositions[static_cast<size_t>(i)] = positions[static_cast<size_t>(tetrahedron[i])];
                        tetraDensities[static_cast<size_t>(i)] = densities[static_cast<size_t>(tetrahedron[i])];
                    }
                    PolygonizeTetra(data, tetraPositions, tetraDensities, &chunk);
                }
            }
        }
    }
    return chunk;
}

bool RebuildCaveMeshChunks(const CaveVolumeData& data, const std::vector<int>& chunkIndices, CaveMesh* mesh) {
    if (mesh == nullptr || mesh->resolution != data.resolution || mesh->chunkSize != data.chunkSize ||
        mesh->chunkCountX != CaveChunkCountX(data) || mesh->chunkCountY != CaveChunkCountY(data) ||
        mesh->chunkCountZ != CaveChunkCountZ(data)) {
        return false;
    }
    for (int chunkIndex : chunkIndices) {
        if (chunkIndex < 0 || chunkIndex >= static_cast<int>(mesh->chunks.size())) {
            continue;
        }
        const int countX = CaveChunkCountX(data);
        const int countY = CaveChunkCountY(data);
        const int chunkZ = chunkIndex / (countX * countY);
        const int rem = chunkIndex - chunkZ * countX * countY;
        const int chunkY = rem / countX;
        const int chunkX = rem % countX;
        mesh->chunks[static_cast<size_t>(chunkIndex)] = BuildCaveMeshChunk(data, chunkX, chunkY, chunkZ);
    }
    return true;
}

bool RefreshCaveMeshChunkMaterials(const CaveVolumeData& data, const std::vector<int>& chunkIndices, CaveMesh* mesh) {
    if (mesh == nullptr || mesh->resolution != data.resolution || mesh->chunkSize != data.chunkSize ||
        mesh->chunkCountX != CaveChunkCountX(data) || mesh->chunkCountY != CaveChunkCountY(data) ||
        mesh->chunkCountZ != CaveChunkCountZ(data)) {
        return false;
    }
    for (int chunkIndex : chunkIndices) {
        if (chunkIndex < 0 || chunkIndex >= static_cast<int>(mesh->chunks.size())) {
            continue;
        }
        CaveMeshChunk& chunk = mesh->chunks[static_cast<size_t>(chunkIndex)];
        for (CaveMeshVertex& vertex : chunk.vertices) {
            vertex.color = BlendedLayerColor(data, vertex.position);
        }
    }
    return true;
}

TerrainVolumeMesh TerrainSurfaceExtractor::Extract(const TerrainVolumeData& data) {
    return BuildCaveMesh(data);
}

TerrainVolumeMesh TerrainMeshBuilder::Build(const TerrainVolumeData& data) {
    return BuildCaveMesh(data);
}

TerrainVolumeMesh TerrainCollisionBuilder::BuildTriangleCollisionMesh(const TerrainVolumeData& data) {
    return BuildCaveMesh(data);
}

bool TerrainSerializer::Load(const Component& component, TerrainVolumeData* outData, std::string* error) {
    return LoadCaveVolumeDataFromComponent(component, outData, error);
}

void TerrainSerializer::Save(const TerrainVolumeData& data, Component* component) {
    SaveCaveVolumeDataToComponent(data, component);
}

std::string CaveBrushModeLabel(CaveBrushMode mode) {
    switch (mode) {
    case CaveBrushMode::Dig:
        return "Dig";
    case CaveBrushMode::Fill:
        return "Fill";
    case CaveBrushMode::Smooth:
        return "Smooth";
    case CaveBrushMode::Flatten:
        return "Flatten";
    case CaveBrushMode::Tunnel:
        return "Tunnel";
    case CaveBrushMode::Paint:
        return "Paint";
    }
    return "Dig";
}

std::string CaveFalloffLabel(CaveFalloffCurve curve) {
    switch (curve) {
    case CaveFalloffCurve::Smooth:
        return "Smooth";
    case CaveFalloffCurve::Linear:
        return "Linear";
    case CaveFalloffCurve::Constant:
        return "Constant";
    case CaveFalloffCurve::Bell:
        return "Bell";
    }
    return "Smooth";
}

bool RunCaveVolumeSelfTests(std::vector<std::string>* diagnostics) {
    auto fail = [&](const std::string& message) {
        if (diagnostics != nullptr) {
            diagnostics->push_back(message);
        }
        return false;
    };

    CaveVolumeData data;
    data.resolution = {17, 13, 17};
    data.size = {16.0f, 10.0f, 16.0f};
    data.chunkSize = 4;
    NormalizeCaveVolumeData(&data);

    CaveBrushSettings tunnel;
    tunnel.mode = CaveBrushMode::Tunnel;
    tunnel.falloff = CaveFalloffCurve::Smooth;
    tunnel.radius = 2.0f;
    tunnel.strength = 1.2f;
    tunnel.opacity = 1.0f;
    tunnel.useSegment = true;
    tunnel.segmentStart = {-7.0f, 0.0f, 0.0f};
    tunnel.segmentEnd = {7.0f, 1.2f, 0.0f};
    CaveBrushResult tunneled = ApplyCaveBrush(&data, {0.0f, 0.0f, 0.0f}, tunnel);
    if (!tunneled.changed || tunneled.affectedSamples <= 0 || tunneled.dirty.chunks.empty()) {
        return fail("Cave selftest failed: tunnel brush did not affect density/chunks.");
    }
    if (SampleCaveLocal(data, {0.0f, 0.5f, 0.0f}).density >= 0.0f) {
        return fail("Cave selftest failed: tunnel center remained solid.");
    }
    if (EvaluateCaveBrushFalloff(0.5f, CaveFalloffCurve::Linear) < 0.49f ||
        EvaluateCaveBrushFalloff(1.0f, CaveFalloffCurve::Smooth) > 0.001f) {
        return fail("Cave selftest failed: brush falloff returned unexpected values.");
    }

    CaveMesh mesh = BuildCaveMesh(data);
    size_t vertexCount = 0;
    for (const CaveMeshChunk& chunk : mesh.chunks) {
        vertexCount += chunk.vertices.size();
        for (const CaveMeshVertex& vertex : chunk.vertices) {
            if (std::fabs(Length(ToVec3(vertex.normal)) - 1.0f) > 0.08f) {
                return fail("Cave selftest failed: mesh normal was not normalized.");
            }
        }
    }
    if (vertexCount == 0) {
        return fail("Cave selftest failed: mesh extraction produced no cave surface.");
    }
    CaveMesh partial = mesh;
    if (!RebuildCaveMeshChunks(data, tunneled.dirty.chunks, &partial)) {
        return fail("Cave selftest failed: chunk rebuild failed.");
    }
    CaveVolumeData tunneledForRaycast = data;

    CaveBrushSettings paint;
    paint.mode = CaveBrushMode::Paint;
    paint.radius = 2.0f;
    paint.strength = 0.8f;
    paint.activeLayerIndex = 1;
    CaveBrushResult painted = ApplyCaveBrush(&data, {0.0f, 0.5f, 0.0f}, paint);
    if (!painted.changed || CaveLayerWeightAtLocal(data, 1, {0.0f, 0.5f, 0.0f}) <= 0.1f) {
        return fail("Cave selftest failed: paint brush did not affect selected cave layer.");
    }

    CaveBrushSettings smooth;
    smooth.mode = CaveBrushMode::Smooth;
    smooth.radius = 2.5f;
    smooth.strength = 0.45f;
    if (!ApplyCaveBrush(&data, {0.0f, 0.5f, 0.0f}, smooth).changed) {
        return fail("Cave selftest failed: smooth brush did not affect cave density.");
    }

    Component component;
    SaveCaveVolumeDataToComponent(data, &component);
    CaveVolumeData loaded;
    if (!LoadCaveVolumeDataFromComponent(component, &loaded) || loaded.densities.size() != data.densities.size() ||
        loaded.layers.size() != data.layers.size()) {
        return fail("Cave selftest failed: serialization roundtrip lost volume data.");
    }
    if (CaveLayerWeightAtLocal(loaded, 1, {0.0f, 0.5f, 0.0f}) <= 0.1f) {
        return fail("Cave selftest failed: serialization roundtrip lost layer weights.");
    }

    Entity entity;
    entity.id = 77;
    entity.name = "Test Cave";
    entity.position = {0.0f, 0.0f, 0.0f};
    entity.scale = {1.0f, 1.0f, 1.0f};
    CaveRayHit hit;
    if (!CaveRaycastWorld(tunneledForRaycast, entity, {0.0f, 0.6f, 0.0f}, {0.0f, -1.0f, 0.0f}, 6.0f, &hit) ||
        !hit.hit || hit.entityId != entity.id) {
        return fail("Cave selftest failed: raycast did not hit tunnel floor.");
    }

    CaveBrushSettings fill;
    fill.mode = CaveBrushMode::Fill;
    fill.radius = 1.2f;
    fill.strength = 1.0f;
    const CaveBrushResult filled = ApplyCaveBrush(&tunneledForRaycast, {0.0f, 0.5f, 0.0f}, fill);
    if (!filled.changed || SampleCaveLocal(tunneledForRaycast, {0.0f, 0.5f, 0.0f}).density <= 0.5f) {
        return fail("Cave selftest failed: fill brush did not restore solid density.");
    }

    if (diagnostics != nullptr) {
        diagnostics->push_back("cave.selftest passed.");
    }
    return true;
}

} // namespace aine

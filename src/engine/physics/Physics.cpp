#include "Physics.h"

#include "engine/cave/CaveVolume.h"
#include "engine/scene/Entity.h"
#include "engine/terrain/Terrain.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_map>

namespace aine {
namespace {

constexpr float kPi = 3.1415926535f;
constexpr float kDegreesToRadians = kPi / 180.0f;
constexpr float kDefaultFixedDelta = 1.0f / 60.0f;
constexpr int kDefaultSolverIterations = 6;
constexpr float kPositionSlop = 0.005f;
constexpr float kPositionCorrectionPercent = 0.82f;
constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

enum class BodyType {
    Static,
    Dynamic,
    Kinematic
};

enum class Collider2DShape {
    Box,
    Circle
};

enum class Collider3DShape {
    Box,
    Sphere,
    Terrain,
    Cave
};

struct PhysicsSettings {
    bool enabled = true;
    float fixedDeltaSeconds = kDefaultFixedDelta;
    int solverIterations = kDefaultSolverIterations;
    PhysicsVec2 gravity2D{0.0f, -9.81f};
    PhysicsVec3 gravity3D{0.0f, -9.81f, 0.0f};
    std::vector<std::pair<std::string, std::string>> disabledLayerCollisionPairs;
};

struct Aabb2 {
    PhysicsVec2 min;
    PhysicsVec2 max;
};

struct Aabb3 {
    PhysicsVec3 min;
    PhysicsVec3 max;
};

struct Body2D {
    size_t entityIndex = 0;
    size_t componentIndex = 0;
    bool hasComponent = false;
    int entityId = 0;
    std::string entityName;
    BodyType type = BodyType::Static;
    float mass = 1.0f;
    float inverseMass = 0.0f;
    float restitution = 0.12f;
    float friction = 0.55f;
    float gravityScale = 1.0f;
    float damping = 0.02f;
    bool useGravity = true;
    bool awake = true;
    bool lockX = false;
    bool lockY = false;
    bool lockRotation = false;
    PhysicsVec2 lockedPosition;
    PhysicsVec2 position;
    PhysicsVec2 velocity;
    float rotationDegrees = 0.0f;
    float angularVelocity = 0.0f;
};

struct Body3D {
    size_t entityIndex = 0;
    size_t componentIndex = 0;
    bool hasComponent = false;
    int entityId = 0;
    std::string entityName;
    BodyType type = BodyType::Static;
    float mass = 1.0f;
    float inverseMass = 0.0f;
    float restitution = 0.12f;
    float friction = 0.55f;
    float gravityScale = 1.0f;
    float damping = 0.02f;
    bool useGravity = true;
    bool awake = true;
    bool lockX = false;
    bool lockY = false;
    bool lockZ = false;
    bool lockAngularX = false;
    bool lockAngularY = false;
    bool lockAngularZ = false;
    PhysicsVec3 lockedPosition;
    PhysicsVec3 position;
    PhysicsVec3 velocity;
    PhysicsVec3 rotationDegrees;
    PhysicsVec3 angularVelocity;
};

struct Collider2D {
    size_t entityIndex = 0;
    size_t componentIndex = 0;
    bool hasComponent = false;
    int bodyIndex = -1;
    int entityId = 0;
    std::string entityName;
    std::string layer = "Default";
    Collider2DShape shape = Collider2DShape::Box;
    bool trigger = false;
    PhysicsVec2 offset;
    PhysicsVec2 size{1.0f, 1.0f};
    PhysicsVec2 halfExtents{0.5f, 0.5f};
    float localRadius = 0.5f;
    float radius = 0.5f;
    PhysicsVec2 center;
    PhysicsVec2 axisX{1.0f, 0.0f};
    PhysicsVec2 axisY{0.0f, 1.0f};
    Aabb2 bounds;
};

struct Collider3D {
    size_t entityIndex = 0;
    size_t componentIndex = 0;
    bool hasComponent = false;
    int bodyIndex = -1;
    int entityId = 0;
    std::string entityName;
    std::string layer = "Default";
    Collider3DShape shape = Collider3DShape::Box;
    bool trigger = false;
    PhysicsVec3 offset;
    PhysicsVec3 size{1.0f, 1.0f, 1.0f};
    PhysicsVec3 halfExtents{0.5f, 0.5f, 0.5f};
    float localRadius = 0.5f;
    float radius = 0.5f;
    PhysicsVec3 center;
    Aabb3 bounds;
    std::array<float, 3> transformPosition{0.0f, 0.0f, 0.0f};
    std::array<float, 3> transformScale{1.0f, 1.0f, 1.0f};
    TerrainData terrain;
    CaveVolumeData cave;
};

struct Contact2D {
    int colliderA = -1;
    int colliderB = -1;
    PhysicsVec2 normal;
    PhysicsVec2 point;
    float penetration = 0.0f;
    bool trigger = false;
};

struct Contact3D {
    int colliderA = -1;
    int colliderB = -1;
    PhysicsVec3 normal;
    PhysicsVec3 point;
    float penetration = 0.0f;
    bool trigger = false;
};

struct HeavyComponentSignature {
    std::uint64_t hash = kFnvOffsetBasis;
    std::size_t heavyBytes = 0;
    int propertyCount = 0;

    bool operator==(const HeavyComponentSignature& other) const {
        return hash == other.hash && heavyBytes == other.heavyBytes && propertyCount == other.propertyCount;
    }
};

struct CachedTerrainColliderData {
    int entityId = 0;
    size_t componentIndex = 0;
    HeavyComponentSignature signature;
    TerrainData data;
    bool valid = false;
};

struct CachedCaveColliderData {
    int entityId = 0;
    size_t componentIndex = 0;
    HeavyComponentSignature signature;
    CaveVolumeData data;
    bool valid = false;
};

struct PhysicsHeavyColliderCache {
    std::vector<CachedTerrainColliderData> terrain;
    std::vector<CachedCaveColliderData> cave;
};

std::unordered_map<const PhysicsWorld*, PhysicsHeavyColliderCache> gPhysicsHeavyColliderCaches;

void HashByte(std::uint64_t* hash, unsigned char value) {
    if (hash == nullptr) {
        return;
    }
    *hash ^= static_cast<std::uint64_t>(value);
    *hash *= kFnvPrime;
}

void HashSize(std::uint64_t* hash, std::size_t value) {
    for (std::size_t i = 0; i < sizeof(std::size_t); ++i) {
        HashByte(hash, static_cast<unsigned char>((value >> (i * 8)) & 0xffu));
    }
}

void HashString(std::uint64_t* hash, const std::string& value) {
    for (unsigned char ch : value) {
        HashByte(hash, ch);
    }
    HashByte(hash, 0xffu);
}

bool IsHeavySerializedTerrainProperty(const std::string& name) {
    return name == "heights" || name == "weights" || name == "holes" || name == "volumeDensities" ||
           name == "volumeWeights";
}

bool IsHeavySerializedCaveProperty(const std::string& name) {
    return name == "densities" || name == "weights";
}

HeavyComponentSignature BuildHeavyComponentSignature(const Component& component,
                                                     bool (*isHeavyProperty)(const std::string&)) {
    HeavyComponentSignature signature;
    signature.propertyCount = static_cast<int>(component.properties.size());
    HashString(&signature.hash, component.type);
    for (const ComponentProperty& property : component.properties) {
        HashString(&signature.hash, property.name);
        if (isHeavyProperty != nullptr && isHeavyProperty(property.name)) {
            signature.heavyBytes += property.value.size();
            HashSize(&signature.hash, property.value.size());
            if (!property.value.empty()) {
                HashByte(&signature.hash, static_cast<unsigned char>(property.value.front()));
                HashByte(&signature.hash, static_cast<unsigned char>(property.value[property.value.size() / 2]));
                HashByte(&signature.hash, static_cast<unsigned char>(property.value.back()));
            }
            continue;
        }
        HashString(&signature.hash, property.value);
    }
    return signature;
}

bool PhysicsHeavyColliderCacheDisabled() {
    const char* env = std::getenv("AINE_DISABLE_PHYSICS_HEAVY_COLLIDER_CACHE");
    return env != nullptr && std::strcmp(env, "0") != 0;
}

CachedTerrainColliderData* FindCachedTerrainColliderData(PhysicsHeavyColliderCache& cache, int entityId,
                                                         size_t componentIndex) {
    auto it = std::find_if(cache.terrain.begin(), cache.terrain.end(),
                           [entityId, componentIndex](const CachedTerrainColliderData& entry) {
                               return entry.entityId == entityId && entry.componentIndex == componentIndex;
                           });
    return it == cache.terrain.end() ? nullptr : &(*it);
}

CachedCaveColliderData* FindCachedCaveColliderData(PhysicsHeavyColliderCache& cache, int entityId,
                                                   size_t componentIndex) {
    auto it = std::find_if(cache.cave.begin(), cache.cave.end(),
                           [entityId, componentIndex](const CachedCaveColliderData& entry) {
                               return entry.entityId == entityId && entry.componentIndex == componentIndex;
                           });
    return it == cache.cave.end() ? nullptr : &(*it);
}

bool LoadTerrainDataForPhysics(const PhysicsWorld* world, int entityId, size_t componentIndex,
                               const Component& component, TerrainData* outData) {
    if (world == nullptr || PhysicsHeavyColliderCacheDisabled()) {
        return LoadTerrainDataFromComponent(component, outData);
    }

    const HeavyComponentSignature signature = BuildHeavyComponentSignature(component, IsHeavySerializedTerrainProperty);
    PhysicsHeavyColliderCache& cache = gPhysicsHeavyColliderCaches[world];
    CachedTerrainColliderData* cached = FindCachedTerrainColliderData(cache, entityId, componentIndex);
    if (cached != nullptr && cached->valid && cached->signature == signature) {
        if (outData != nullptr) {
            *outData = cached->data;
        }
        return true;
    }

    TerrainData parsed;
    if (!LoadTerrainDataFromComponent(component, &parsed)) {
        return false;
    }
    if (cached == nullptr) {
        cache.terrain.push_back({});
        cached = &cache.terrain.back();
    }
    cached->entityId = entityId;
    cached->componentIndex = componentIndex;
    cached->signature = signature;
    cached->data = parsed;
    cached->valid = true;
    if (outData != nullptr) {
        *outData = parsed;
    }
    return true;
}

bool LoadCaveVolumeDataForPhysics(const PhysicsWorld* world, int entityId, size_t componentIndex,
                                  const Component& component, CaveVolumeData* outData) {
    if (world == nullptr || PhysicsHeavyColliderCacheDisabled()) {
        return LoadCaveVolumeDataFromComponent(component, outData);
    }

    const HeavyComponentSignature signature = BuildHeavyComponentSignature(component, IsHeavySerializedCaveProperty);
    PhysicsHeavyColliderCache& cache = gPhysicsHeavyColliderCaches[world];
    CachedCaveColliderData* cached = FindCachedCaveColliderData(cache, entityId, componentIndex);
    if (cached != nullptr && cached->valid && cached->signature == signature) {
        if (outData != nullptr) {
            *outData = cached->data;
        }
        return true;
    }

    CaveVolumeData parsed;
    if (!LoadCaveVolumeDataFromComponent(component, &parsed)) {
        return false;
    }
    if (cached == nullptr) {
        cache.cave.push_back({});
        cached = &cache.cave.back();
    }
    cached->entityId = entityId;
    cached->componentIndex = componentIndex;
    cached->signature = signature;
    cached->data = parsed;
    cached->valid = true;
    if (outData != nullptr) {
        *outData = parsed;
    }
    return true;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string TrimCopy(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
                    return !std::isspace(c);
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
                    return !std::isspace(c);
                }).base(),
                value.end());
    return value;
}

std::string NormalizeLayerName(std::string value) {
    value = TrimCopy(std::move(value));
    return value.empty() ? std::string{"Default"} : value;
}

std::pair<std::string, std::string> OrderedLayerPair(std::string first, std::string second) {
    first = NormalizeLayerName(std::move(first));
    second = NormalizeLayerName(std::move(second));
    if (second < first) {
        std::swap(first, second);
    }
    return {std::move(first), std::move(second)};
}

std::vector<std::pair<std::string, std::string>> ParseDisabledLayerCollisionPairs(const std::string& value) {
    std::vector<std::pair<std::string, std::string>> pairs;
    std::istringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ';')) {
        const size_t separator = token.find('|');
        if (separator == std::string::npos) {
            continue;
        }
        std::pair<std::string, std::string> pair =
            OrderedLayerPair(token.substr(0, separator), token.substr(separator + 1));
        const bool exists = std::any_of(pairs.begin(), pairs.end(), [&pair](const auto& existing) {
            return existing == pair;
        });
        if (!exists) {
            pairs.push_back(std::move(pair));
        }
    }
    return pairs;
}

bool LayersCanCollide(const PhysicsSettings& settings, const std::string& firstLayer, const std::string& secondLayer) {
    const std::pair<std::string, std::string> pair = OrderedLayerPair(firstLayer, secondLayer);
    return std::none_of(settings.disabledLayerCollisionPairs.begin(), settings.disabledLayerCollisionPairs.end(),
                        [&pair](const auto& disabled) {
                            return disabled == pair;
                        });
}

PhysicsVec2 operator+(PhysicsVec2 left, PhysicsVec2 right) {
    return {left.x + right.x, left.y + right.y};
}

PhysicsVec2 operator-(PhysicsVec2 left, PhysicsVec2 right) {
    return {left.x - right.x, left.y - right.y};
}

PhysicsVec2 operator-(PhysicsVec2 value) {
    return {-value.x, -value.y};
}

PhysicsVec2 operator*(PhysicsVec2 value, float scalar) {
    return {value.x * scalar, value.y * scalar};
}

PhysicsVec2 operator/(PhysicsVec2 value, float scalar) {
    return {value.x / scalar, value.y / scalar};
}

PhysicsVec3 operator+(PhysicsVec3 left, PhysicsVec3 right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

PhysicsVec3 operator-(PhysicsVec3 left, PhysicsVec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

PhysicsVec3 operator-(PhysicsVec3 value) {
    return {-value.x, -value.y, -value.z};
}

PhysicsVec3 operator*(PhysicsVec3 value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

PhysicsVec3 operator/(PhysicsVec3 value, float scalar) {
    return {value.x / scalar, value.y / scalar, value.z / scalar};
}

PhysicsVec3 Cross(PhysicsVec3 left, PhysicsVec3 right) {
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

PhysicsVec2 Rotate(PhysicsVec2 value, float degrees) {
    const float radians = degrees * kDegreesToRadians;
    const float s = std::sin(radians);
    const float c = std::cos(radians);
    return {value.x * c - value.y * s, value.x * s + value.y * c};
}

float SafeAbs(float value) {
    return std::fabs(std::isfinite(value) ? value : 0.0f);
}

float MaxScale2D(const Entity& entity) {
    return std::max(SafeAbs(entity.scale[0]), SafeAbs(entity.scale[1]));
}

float MaxScale3D(const Entity& entity) {
    return std::max({SafeAbs(entity.scale[0]), SafeAbs(entity.scale[1]), SafeAbs(entity.scale[2])});
}

std::array<float, 3> ToArray(PhysicsVec3 value) {
    return {value.x, value.y, value.z};
}

PhysicsVec3 ToPhysicsVec3(std::array<float, 3> value) {
    return {value[0], value[1], value[2]};
}

float TerrainWorldMaxHeight(const TerrainData& terrain, const Entity& entity) {
    const float localHeight = TerrainUsesVolumetric(terrain) ? std::max(terrain.size[1], terrain.volume.size[1] * 0.5f)
                                                             : terrain.size[1];
    return entity.position[1] + localHeight * std::max(SafeAbs(entity.scale[1]), 0.001f);
}

float TerrainWorldMinHeight(const TerrainData& terrain, const Entity& entity) {
    const float localMin = TerrainUsesVolumetric(terrain) ? std::min(0.0f, -terrain.volume.size[1] * 0.5f) : 0.0f;
    return entity.position[1] + localMin * std::max(SafeAbs(entity.scale[1]), 0.001f);
}

Aabb3 TerrainWorldBounds(const TerrainData& terrain, const Entity& entity) {
    const float scaleX = std::max(SafeAbs(entity.scale[0]), 0.001f);
    const float scaleZ = std::max(SafeAbs(entity.scale[2]), 0.001f);
    const float halfX = terrain.size[0] * scaleX * 0.5f;
    const float halfZ = terrain.size[2] * scaleZ * 0.5f;
    return {{entity.position[0] - halfX, TerrainWorldMinHeight(terrain, entity) - 0.02f, entity.position[2] - halfZ},
            {entity.position[0] + halfX, TerrainWorldMaxHeight(terrain, entity) + 0.02f, entity.position[2] + halfZ}};
}

Aabb3 CaveWorldBounds(const CaveVolumeData& cave, const Entity& entity) {
    const float scaleX = std::max(SafeAbs(entity.scale[0]), 0.001f);
    const float scaleY = std::max(SafeAbs(entity.scale[1]), 0.001f);
    const float scaleZ = std::max(SafeAbs(entity.scale[2]), 0.001f);
    const float halfX = cave.size[0] * scaleX * 0.5f;
    const float halfY = cave.size[1] * scaleY * 0.5f;
    const float halfZ = cave.size[2] * scaleZ * 0.5f;
    return {{entity.position[0] - halfX, entity.position[1] - halfY, entity.position[2] - halfZ},
            {entity.position[0] + halfX, entity.position[1] + halfY, entity.position[2] + halfZ}};
}

std::string FormatFloat(float value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

std::string FormatVec2(PhysicsVec2 value) {
    std::ostringstream stream;
    stream << value.x << ", " << value.y;
    return stream.str();
}

std::string FormatVec3(PhysicsVec3 value) {
    std::ostringstream stream;
    stream << value.x << ", " << value.y << ", " << value.z;
    return stream.str();
}

std::string GetProperty(const Component& component, const std::string& propertyName, const std::string& fallback = {}) {
    for (const ComponentProperty& property : component.properties) {
        if (property.name == propertyName) {
            return property.value;
        }
    }
    return fallback;
}

void SetProperty(Component& component, const std::string& propertyName, std::string value) {
    for (ComponentProperty& property : component.properties) {
        if (property.name == propertyName) {
            property.value = std::move(value);
            return;
        }
    }
    component.properties.push_back({propertyName, std::move(value)});
}

bool TryParseFloat(const std::string& value, float* out) {
    if (out == nullptr) {
        return false;
    }
    std::istringstream stream(value);
    float parsed = 0.0f;
    if (!(stream >> parsed) || !std::isfinite(parsed)) {
        return false;
    }
    *out = parsed;
    return true;
}

float GetFloat(const Component& component, const std::string& propertyName, float fallback) {
    float parsed = fallback;
    return TryParseFloat(GetProperty(component, propertyName), &parsed) ? parsed : fallback;
}

int GetInt(const Component& component, const std::string& propertyName, int fallback) {
    std::istringstream stream(GetProperty(component, propertyName));
    int parsed = fallback;
    return (stream >> parsed) ? parsed : fallback;
}

bool GetBool(const Component& component, const std::string& propertyName, bool fallback) {
    const std::string value = ToLower(GetProperty(component, propertyName));
    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }
    return fallback;
}

bool ComponentEnabled(const Component& component) {
    return GetBool(component, "enabled", true);
}

const Entity* FindEntityById(const std::vector<Entity>& entities, int id) {
    auto it = std::find_if(entities.begin(), entities.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    return it == entities.end() ? nullptr : &(*it);
}

bool EntityActiveInHierarchy(const std::vector<Entity>& entities, const Entity& entity) {
    if (!entity.activeSelf) {
        return false;
    }

    std::vector<int> visited;
    const Entity* current = &entity;
    while (current != nullptr) {
        if (!current->activeSelf) {
            return false;
        }
        if (std::find(visited.begin(), visited.end(), current->id) != visited.end()) {
            return false;
        }
        visited.push_back(current->id);
        if (current->parentId == 0) {
            return true;
        }
        current = FindEntityById(entities, current->parentId);
    }
    return true;
}

std::vector<float> ParseFloatList(std::string value) {
    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream stream(value);
    std::vector<float> values;
    float parsed = 0.0f;
    while (stream >> parsed) {
        if (std::isfinite(parsed)) {
            values.push_back(parsed);
        }
    }
    return values;
}

PhysicsVec2 GetVec2(const Component& component, const std::string& propertyName, PhysicsVec2 fallback) {
    const std::vector<float> values = ParseFloatList(GetProperty(component, propertyName));
    if (values.size() < 2) {
        return fallback;
    }
    return {values[0], values[1]};
}

PhysicsVec3 GetVec3(const Component& component, const std::string& propertyName, PhysicsVec3 fallback) {
    const std::vector<float> values = ParseFloatList(GetProperty(component, propertyName));
    if (values.size() < 3) {
        return fallback;
    }
    return {values[0], values[1], values[2]};
}

BodyType ParseBodyType(const std::string& value) {
    const std::string normalized = ToLower(value);
    if (normalized == "dynamic") {
        return BodyType::Dynamic;
    }
    if (normalized == "kinematic") {
        return BodyType::Kinematic;
    }
    return BodyType::Static;
}

Collider2DShape Parse2DShape(const std::string& value) {
    return ToLower(value) == "circle" ? Collider2DShape::Circle : Collider2DShape::Box;
}

Collider3DShape Parse3DShape(const std::string& value) {
    const std::string normalized = ToLower(value);
    if (normalized == "sphere") {
        return Collider3DShape::Sphere;
    }
    if (normalized == "terrain" || normalized == "heightfield") {
        return Collider3DShape::Terrain;
    }
    return Collider3DShape::Box;
}

int FindEnabledComponentIndex(const Entity& entity, const std::string& type) {
    if (!entity.activeSelf) {
        return -1;
    }
    for (size_t i = 0; i < entity.components.size(); ++i) {
        if (entity.components[i].type == type && ComponentEnabled(entity.components[i])) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool HasEnabledComponent(const Entity& entity, const std::string& type) {
    return FindEnabledComponentIndex(entity, type) >= 0;
}

PhysicsSettings ReadSettings(const std::vector<Entity>& entities) {
    PhysicsSettings settings;
    for (const Entity& entity : entities) {
        if (!EntityActiveInHierarchy(entities, entity)) {
            continue;
        }
        for (const Component& component : entity.components) {
            if (component.type != "PhysicsSettings" || !ComponentEnabled(component)) {
                continue;
            }
            settings.enabled = GetBool(component, "enabled", settings.enabled);
            settings.fixedDeltaSeconds =
                std::clamp(GetFloat(component, "fixedDeltaSeconds", settings.fixedDeltaSeconds), 1.0f / 240.0f, 0.05f);
            settings.solverIterations = std::clamp(GetInt(component, "solverIterations", settings.solverIterations), 1, 32);
            settings.gravity2D = GetVec2(component, "gravity2D", settings.gravity2D);
            settings.gravity3D = GetVec3(component, "gravity3D", settings.gravity3D);
            settings.disabledLayerCollisionPairs =
                ParseDisabledLayerCollisionPairs(GetProperty(component, "disabledLayerCollisionPairs"));
            return settings;
        }
    }
    return settings;
}

Body2D MakeImplicitBody2D(const Entity& entity, size_t entityIndex) {
    Body2D body;
    body.entityIndex = entityIndex;
    body.entityId = entity.id;
    body.entityName = entity.name;
    body.type = BodyType::Static;
    body.position = {entity.position[0], entity.position[1]};
    body.lockedPosition = body.position;
    body.rotationDegrees = entity.rotation[2];
    return body;
}

Body3D MakeImplicitBody3D(const Entity& entity, size_t entityIndex) {
    Body3D body;
    body.entityIndex = entityIndex;
    body.entityId = entity.id;
    body.entityName = entity.name;
    body.type = HasEnabledComponent(entity, "CharacterControllerRuntime") ? BodyType::Kinematic : BodyType::Static;
    body.position = {entity.position[0], entity.position[1], entity.position[2]};
    body.lockedPosition = body.position;
    body.rotationDegrees = {entity.rotation[0], entity.rotation[1], entity.rotation[2]};
    return body;
}

Body2D ReadBody2D(const Entity& entity, size_t entityIndex, size_t componentIndex) {
    const Component& component = entity.components[componentIndex];
    Body2D body = MakeImplicitBody2D(entity, entityIndex);
    body.componentIndex = componentIndex;
    body.hasComponent = true;
    body.type = ParseBodyType(GetProperty(component, "bodyType", "Dynamic"));
    body.mass = std::max(0.0001f, GetFloat(component, "mass", 1.0f));
    body.inverseMass = body.type == BodyType::Dynamic ? 1.0f / body.mass : 0.0f;
    body.restitution = std::clamp(GetFloat(component, "restitution", 0.12f), 0.0f, 1.0f);
    body.friction = std::clamp(GetFloat(component, "friction", 0.55f), 0.0f, 2.0f);
    body.gravityScale = GetFloat(component, "gravityScale", 1.0f);
    body.damping = std::max(0.0f, GetFloat(component, "damping", 0.02f));
    body.useGravity = GetBool(component, "useGravity", true);
    body.awake = GetBool(component, "awake", true);
    body.lockX = GetBool(component, "lockX", false);
    body.lockY = GetBool(component, "lockY", false);
    body.lockRotation = GetBool(component, "lockRotation", false);
    body.velocity = GetVec2(component, "velocity", {});
    body.angularVelocity = GetFloat(component, "angularVelocity", 0.0f);
    return body;
}

Body3D ReadBody3D(const Entity& entity, size_t entityIndex, size_t componentIndex) {
    const Component& component = entity.components[componentIndex];
    Body3D body = MakeImplicitBody3D(entity, entityIndex);
    body.componentIndex = componentIndex;
    body.hasComponent = true;
    body.type = ParseBodyType(GetProperty(component, "bodyType", "Dynamic"));
    body.mass = std::max(0.0001f, GetFloat(component, "mass", 1.0f));
    body.inverseMass = body.type == BodyType::Dynamic ? 1.0f / body.mass : 0.0f;
    body.restitution = std::clamp(GetFloat(component, "restitution", 0.12f), 0.0f, 1.0f);
    body.friction = std::clamp(GetFloat(component, "friction", 0.55f), 0.0f, 2.0f);
    body.gravityScale = GetFloat(component, "gravityScale", 1.0f);
    body.damping = std::max(0.0f, GetFloat(component, "damping", 0.02f));
    body.useGravity = GetBool(component, "useGravity", true);
    body.awake = GetBool(component, "awake", true);
    body.lockX = GetBool(component, "lockX", false);
    body.lockY = GetBool(component, "lockY", false);
    body.lockZ = GetBool(component, "lockZ", false);
    body.lockAngularX = GetBool(component, "lockAngularX", false);
    body.lockAngularY = GetBool(component, "lockAngularY", false);
    body.lockAngularZ = GetBool(component, "lockAngularZ", false);
    body.velocity = GetVec3(component, "velocity", {});
    body.angularVelocity = GetVec3(component, "angularVelocity", {});
    return body;
}

void ApplyLocks(Body2D& body) {
    if (body.lockX) {
        body.position.x = body.lockedPosition.x;
        body.velocity.x = 0.0f;
    }
    if (body.lockY) {
        body.position.y = body.lockedPosition.y;
        body.velocity.y = 0.0f;
    }
    if (body.lockRotation) {
        body.angularVelocity = 0.0f;
    }
}

void ApplyLocks(Body3D& body) {
    if (body.lockX) {
        body.position.x = body.lockedPosition.x;
        body.velocity.x = 0.0f;
    }
    if (body.lockY) {
        body.position.y = body.lockedPosition.y;
        body.velocity.y = 0.0f;
    }
    if (body.lockZ) {
        body.position.z = body.lockedPosition.z;
        body.velocity.z = 0.0f;
    }
    if (body.lockAngularX) {
        body.angularVelocity.x = 0.0f;
    }
    if (body.lockAngularY) {
        body.angularVelocity.y = 0.0f;
    }
    if (body.lockAngularZ) {
        body.angularVelocity.z = 0.0f;
    }
}

template <typename Body>
float ImpulseInverseMass(const Body& body) {
    return body.type == BodyType::Dynamic ? body.inverseMass : 0.0f;
}

template <typename Body>
float PositionCorrectionWeight(const Body& body, const Body& other) {
    if (body.type == BodyType::Dynamic) {
        return body.inverseMass;
    }
    if (body.type == BodyType::Kinematic && other.type == BodyType::Static) {
        return 1.0f;
    }
    return 0.0f;
}

void IntegrateBody(Body2D& body, PhysicsVec2 gravity, float dt) {
    if (!body.awake || body.type == BodyType::Static) {
        return;
    }
    if (body.type == BodyType::Dynamic && body.useGravity) {
        body.velocity = body.velocity + gravity * body.gravityScale * dt;
    }
    const float dampingScale = 1.0f / (1.0f + body.damping * dt);
    body.velocity = body.velocity * dampingScale;
    body.position = body.position + body.velocity * dt;
    if (!body.lockRotation) {
        body.rotationDegrees += body.angularVelocity * dt;
    }
    ApplyLocks(body);
}

void IntegrateBody(Body3D& body, PhysicsVec3 gravity, float dt) {
    if (!body.awake || body.type == BodyType::Static) {
        return;
    }
    if (body.type == BodyType::Dynamic && body.useGravity) {
        body.velocity = body.velocity + gravity * body.gravityScale * dt;
    }
    const float dampingScale = 1.0f / (1.0f + body.damping * dt);
    body.velocity = body.velocity * dampingScale;
    body.position = body.position + body.velocity * dt;
    body.rotationDegrees = body.rotationDegrees + body.angularVelocity * dt;
    ApplyLocks(body);
}

Aabb2 BoundsForBox2D(PhysicsVec2 center, PhysicsVec2 axisX, PhysicsVec2 axisY, PhysicsVec2 halfExtents) {
    const PhysicsVec2 corners[4] = {
        center + axisX * halfExtents.x + axisY * halfExtents.y,
        center + axisX * halfExtents.x - axisY * halfExtents.y,
        center - axisX * halfExtents.x + axisY * halfExtents.y,
        center - axisX * halfExtents.x - axisY * halfExtents.y,
    };
    Aabb2 bounds{corners[0], corners[0]};
    for (const PhysicsVec2& corner : corners) {
        bounds.min.x = std::min(bounds.min.x, corner.x);
        bounds.min.y = std::min(bounds.min.y, corner.y);
        bounds.max.x = std::max(bounds.max.x, corner.x);
        bounds.max.y = std::max(bounds.max.y, corner.y);
    }
    return bounds;
}

void UpdateCollider(Collider2D& collider, const std::vector<Body2D>& bodies, const std::vector<Entity>& entities) {
    const Body2D& body = bodies[static_cast<size_t>(collider.bodyIndex)];
    const Entity& entity = entities[collider.entityIndex];
    const float scaleX = SafeAbs(entity.scale[0]);
    const float scaleY = SafeAbs(entity.scale[1]);
    const PhysicsVec2 scaledOffset{collider.offset.x * scaleX, collider.offset.y * scaleY};
    collider.center = body.position + Rotate(scaledOffset, body.rotationDegrees);
    collider.axisX = Normalize(Rotate({1.0f, 0.0f}, body.rotationDegrees));
    collider.axisY = Normalize(Rotate({0.0f, 1.0f}, body.rotationDegrees), {0.0f, 1.0f});
    collider.halfExtents = {std::max(0.001f, SafeAbs(collider.size.x * scaleX) * 0.5f),
                            std::max(0.001f, SafeAbs(collider.size.y * scaleY) * 0.5f)};
    collider.radius = std::max(0.001f, collider.localRadius * std::max(MaxScale2D(entity), 0.001f));
    if (collider.shape == Collider2DShape::Circle) {
        collider.bounds = {{collider.center.x - collider.radius, collider.center.y - collider.radius},
                           {collider.center.x + collider.radius, collider.center.y + collider.radius}};
    } else {
        collider.bounds = BoundsForBox2D(collider.center, collider.axisX, collider.axisY, collider.halfExtents);
    }
}

void UpdateCollider(Collider3D& collider, const std::vector<Body3D>& bodies, const std::vector<Entity>& entities) {
    const Body3D& body = bodies[static_cast<size_t>(collider.bodyIndex)];
    const Entity& entity = entities[collider.entityIndex];
    if (collider.shape == Collider3DShape::Terrain) {
        collider.bounds = TerrainWorldBounds(collider.terrain, entity);
        collider.center = (collider.bounds.min + collider.bounds.max) * 0.5f;
        collider.halfExtents = (collider.bounds.max - collider.bounds.min) * 0.5f;
        collider.radius = std::max(collider.halfExtents.x, collider.halfExtents.z);
        (void)body;
        return;
    }
    if (collider.shape == Collider3DShape::Cave) {
        collider.center = {entity.position[0], entity.position[1], entity.position[2]};
        collider.bounds = CaveWorldBounds(collider.cave, entity);
        collider.halfExtents = (collider.bounds.max - collider.bounds.min) * 0.5f;
        collider.radius = std::max({collider.halfExtents.x, collider.halfExtents.y, collider.halfExtents.z});
        (void)body;
        return;
    }
    const PhysicsVec3 scaledOffset{collider.offset.x * entity.scale[0], collider.offset.y * entity.scale[1],
                                   collider.offset.z * entity.scale[2]};
    collider.center = body.position + scaledOffset;
    collider.halfExtents = {std::max(0.001f, SafeAbs(collider.size.x * entity.scale[0]) * 0.5f),
                            std::max(0.001f, SafeAbs(collider.size.y * entity.scale[1]) * 0.5f),
                            std::max(0.001f, SafeAbs(collider.size.z * entity.scale[2]) * 0.5f)};
    collider.radius = std::max(0.001f, collider.localRadius * std::max(MaxScale3D(entity), 0.001f));
    if (collider.shape == Collider3DShape::Sphere) {
        collider.bounds = {{collider.center.x - collider.radius, collider.center.y - collider.radius,
                            collider.center.z - collider.radius},
                           {collider.center.x + collider.radius, collider.center.y + collider.radius,
                            collider.center.z + collider.radius}};
    } else {
        collider.bounds = {collider.center - collider.halfExtents, collider.center + collider.halfExtents};
    }
}

Collider2D ReadCollider2D(const Entity& entity, size_t entityIndex, size_t componentIndex, int bodyIndex) {
    const Component& component = entity.components[componentIndex];
    Collider2D collider;
    collider.entityIndex = entityIndex;
    collider.componentIndex = componentIndex;
    collider.hasComponent = true;
    collider.bodyIndex = bodyIndex;
    collider.entityId = entity.id;
    collider.entityName = entity.name;
    collider.layer = NormalizeLayerName(entity.layer);
    collider.shape = Parse2DShape(GetProperty(component, "shape", "Box"));
    collider.trigger = GetBool(component, "trigger", false);
    collider.offset = GetVec2(component, "offset", {});
    collider.size = GetVec2(component, "size", {1.0f, 1.0f});
    collider.localRadius = std::max(0.001f, GetFloat(component, "radius", 0.5f));
    collider.radius = collider.localRadius;
    return collider;
}

Collider3D ReadCollider3D(const Entity& entity, size_t entityIndex, size_t componentIndex, int bodyIndex) {
    const Component& component = entity.components[componentIndex];
    Collider3D collider;
    collider.entityIndex = entityIndex;
    collider.componentIndex = componentIndex;
    collider.hasComponent = true;
    collider.bodyIndex = bodyIndex;
    collider.entityId = entity.id;
    collider.entityName = entity.name;
    collider.layer = NormalizeLayerName(entity.layer);
    collider.transformPosition = entity.position;
    collider.transformScale = entity.scale;
    collider.shape = Parse3DShape(GetProperty(component, "shape", "Box"));
    collider.trigger = GetBool(component, "trigger", false);
    collider.offset = GetVec3(component, "offset", {});
    collider.size = GetVec3(component, "size", {1.0f, 1.0f, 1.0f});
    collider.localRadius = std::max(0.001f, GetFloat(component, "radius", 0.5f));
    collider.radius = collider.localRadius;
    return collider;
}

Collider3D ReadTerrainCollider3D(const Entity& entity, size_t entityIndex, size_t componentIndex, int bodyIndex,
                                 const TerrainData& terrain) {
    Collider3D collider;
    collider.entityIndex = entityIndex;
    collider.componentIndex = componentIndex;
    collider.hasComponent = true;
    collider.bodyIndex = bodyIndex;
    collider.entityId = entity.id;
    collider.entityName = entity.name;
    collider.layer = NormalizeLayerName(entity.layer);
    collider.transformPosition = entity.position;
    collider.transformScale = entity.scale;
    collider.shape = Collider3DShape::Terrain;
    collider.trigger = false;
    collider.terrain = terrain;
    return collider;
}

Collider3D ReadCaveCollider3D(const Entity& entity, size_t entityIndex, size_t componentIndex, int bodyIndex,
                              const CaveVolumeData& cave) {
    Collider3D collider;
    collider.entityIndex = entityIndex;
    collider.componentIndex = componentIndex;
    collider.hasComponent = true;
    collider.bodyIndex = bodyIndex;
    collider.entityId = entity.id;
    collider.entityName = entity.name;
    collider.layer = NormalizeLayerName(entity.layer);
    collider.transformPosition = entity.position;
    collider.transformScale = entity.scale;
    collider.shape = Collider3DShape::Cave;
    collider.trigger = false;
    collider.cave = cave;
    return collider;
}

void Build2D(const std::vector<Entity>& entities, std::vector<Body2D>& bodies, std::vector<Collider2D>& colliders) {
    std::vector<int> bodyByEntity(entities.size(), -1);
    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
        const Entity& entity = entities[entityIndex];
        if (!EntityActiveInHierarchy(entities, entity)) {
            continue;
        }
        const int componentIndex = FindEnabledComponentIndex(entity, "Rigidbody2D");
        if (componentIndex < 0) {
            continue;
        }
        bodyByEntity[entityIndex] = static_cast<int>(bodies.size());
        bodies.push_back(ReadBody2D(entity, entityIndex, static_cast<size_t>(componentIndex)));
    }

    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
        const Entity& entity = entities[entityIndex];
        if (!EntityActiveInHierarchy(entities, entity)) {
            continue;
        }
        for (size_t componentIndex = 0; componentIndex < entity.components.size(); ++componentIndex) {
            if (entity.components[componentIndex].type != "Collider2D" ||
                !ComponentEnabled(entity.components[componentIndex])) {
                continue;
            }
            if (bodyByEntity[entityIndex] < 0) {
                bodyByEntity[entityIndex] = static_cast<int>(bodies.size());
                bodies.push_back(MakeImplicitBody2D(entity, entityIndex));
            }
            colliders.push_back(ReadCollider2D(entity, entityIndex, componentIndex, bodyByEntity[entityIndex]));
        }
    }

    for (Collider2D& collider : colliders) {
        UpdateCollider(collider, bodies, entities);
    }
}

void Build3D(const std::vector<Entity>& entities, std::vector<Body3D>& bodies, std::vector<Collider3D>& colliders,
             const PhysicsWorld* cacheOwner) {
    std::vector<int> bodyByEntity(entities.size(), -1);
    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
        const Entity& entity = entities[entityIndex];
        if (!EntityActiveInHierarchy(entities, entity)) {
            continue;
        }
        const int componentIndex = FindEnabledComponentIndex(entity, "Rigidbody3D");
        if (componentIndex < 0) {
            continue;
        }
        bodyByEntity[entityIndex] = static_cast<int>(bodies.size());
        bodies.push_back(ReadBody3D(entity, entityIndex, static_cast<size_t>(componentIndex)));
    }

    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
        const Entity& entity = entities[entityIndex];
        if (!EntityActiveInHierarchy(entities, entity)) {
            continue;
        }
        for (size_t componentIndex = 0; componentIndex < entity.components.size(); ++componentIndex) {
            if (entity.components[componentIndex].type != "Collider3D" ||
                !ComponentEnabled(entity.components[componentIndex])) {
                continue;
            }
            if (bodyByEntity[entityIndex] < 0) {
                bodyByEntity[entityIndex] = static_cast<int>(bodies.size());
                bodies.push_back(MakeImplicitBody3D(entity, entityIndex));
            }
            colliders.push_back(ReadCollider3D(entity, entityIndex, componentIndex, bodyByEntity[entityIndex]));
        }
        for (size_t componentIndex = 0; componentIndex < entity.components.size(); ++componentIndex) {
            if (entity.components[componentIndex].type != "Terrain" ||
                !ComponentEnabled(entity.components[componentIndex])) {
                continue;
            }
            TerrainData terrain;
            if (!LoadTerrainDataForPhysics(cacheOwner, entity.id, componentIndex, entity.components[componentIndex],
                                           &terrain) ||
                !terrain.collisionEnabled) {
                continue;
            }
            if (bodyByEntity[entityIndex] < 0) {
                bodyByEntity[entityIndex] = static_cast<int>(bodies.size());
                bodies.push_back(MakeImplicitBody3D(entity, entityIndex));
            }
            colliders.push_back(
                ReadTerrainCollider3D(entity, entityIndex, componentIndex, bodyByEntity[entityIndex], terrain));
        }
        for (size_t componentIndex = 0; componentIndex < entity.components.size(); ++componentIndex) {
            if (entity.components[componentIndex].type != "CaveVolume" ||
                !ComponentEnabled(entity.components[componentIndex])) {
                continue;
            }
            CaveVolumeData cave;
            if (!LoadCaveVolumeDataForPhysics(cacheOwner, entity.id, componentIndex, entity.components[componentIndex],
                                              &cave) ||
                !cave.collisionEnabled) {
                continue;
            }
            if (bodyByEntity[entityIndex] < 0) {
                bodyByEntity[entityIndex] = static_cast<int>(bodies.size());
                bodies.push_back(MakeImplicitBody3D(entity, entityIndex));
            }
            colliders.push_back(ReadCaveCollider3D(entity, entityIndex, componentIndex, bodyByEntity[entityIndex], cave));
        }
    }

    for (Collider3D& collider : colliders) {
        UpdateCollider(collider, bodies, entities);
    }
}

bool Intersects(Aabb2 left, Aabb2 right) {
    return left.min.x <= right.max.x && left.max.x >= right.min.x && left.min.y <= right.max.y &&
           left.max.y >= right.min.y;
}

bool Intersects(Aabb3 left, Aabb3 right) {
    return left.min.x <= right.max.x && left.max.x >= right.min.x && left.min.y <= right.max.y &&
           left.max.y >= right.min.y && left.min.z <= right.max.z && left.max.z >= right.min.z;
}

void ProjectBox(const Collider2D& box, PhysicsVec2 axis, float* outMin, float* outMax) {
    const float center = Dot(box.center, axis);
    const float radius = box.halfExtents.x * std::fabs(Dot(box.axisX, axis)) +
                         box.halfExtents.y * std::fabs(Dot(box.axisY, axis));
    *outMin = center - radius;
    *outMax = center + radius;
}

bool TestCircleCircle(const Collider2D& a, const Collider2D& b, Contact2D* out) {
    const PhysicsVec2 delta = b.center - a.center;
    const float distanceSq = Dot(delta, delta);
    const float radiusSum = a.radius + b.radius;
    if (distanceSq > radiusSum * radiusSum) {
        return false;
    }
    const float distance = std::sqrt(std::max(distanceSq, 0.0f));
    const PhysicsVec2 normal = distance > 0.0001f ? delta / distance : PhysicsVec2{0.0f, 1.0f};
    if (out != nullptr) {
        out->normal = normal;
        out->penetration = radiusSum - distance;
        out->point = a.center + normal * (a.radius - out->penetration * 0.5f);
    }
    return true;
}

bool TestBoxBox(const Collider2D& a, const Collider2D& b, Contact2D* out) {
    const PhysicsVec2 axes[4] = {a.axisX, a.axisY, b.axisX, b.axisY};
    float bestOverlap = std::numeric_limits<float>::max();
    PhysicsVec2 bestAxis{1.0f, 0.0f};
    for (PhysicsVec2 axis : axes) {
        axis = Normalize(axis);
        float minA = 0.0f;
        float maxA = 0.0f;
        float minB = 0.0f;
        float maxB = 0.0f;
        ProjectBox(a, axis, &minA, &maxA);
        ProjectBox(b, axis, &minB, &maxB);
        const float overlap = std::min(maxA, maxB) - std::max(minA, minB);
        if (overlap < 0.0f) {
            return false;
        }
        if (overlap < bestOverlap) {
            bestOverlap = overlap;
            bestAxis = Dot(b.center - a.center, axis) < 0.0f ? -axis : axis;
        }
    }
    if (out != nullptr) {
        out->normal = bestAxis;
        out->penetration = bestOverlap;
        out->point = (a.center + b.center) * 0.5f;
    }
    return true;
}

bool TestCircleBox(const Collider2D& circle, const Collider2D& box, Contact2D* outBoxToCircle) {
    const PhysicsVec2 toCircle = circle.center - box.center;
    const float localX = Dot(toCircle, box.axisX);
    const float localY = Dot(toCircle, box.axisY);
    const float clampedX = std::clamp(localX, -box.halfExtents.x, box.halfExtents.x);
    const float clampedY = std::clamp(localY, -box.halfExtents.y, box.halfExtents.y);
    const PhysicsVec2 closest = box.center + box.axisX * clampedX + box.axisY * clampedY;
    PhysicsVec2 boxToCircle = circle.center - closest;
    float distance = Length(boxToCircle);
    float penetration = circle.radius - distance;

    if (distance <= 0.0001f) {
        const float xGap = box.halfExtents.x - std::fabs(localX);
        const float yGap = box.halfExtents.y - std::fabs(localY);
        if (xGap < yGap) {
            boxToCircle = box.axisX * (localX < 0.0f ? -1.0f : 1.0f);
            penetration = circle.radius + std::max(0.0f, xGap);
        } else {
            boxToCircle = box.axisY * (localY < 0.0f ? -1.0f : 1.0f);
            penetration = circle.radius + std::max(0.0f, yGap);
        }
        distance = 1.0f;
    }

    if (penetration < 0.0f) {
        return false;
    }
    if (outBoxToCircle != nullptr) {
        outBoxToCircle->normal = Normalize(boxToCircle);
        outBoxToCircle->penetration = penetration;
        outBoxToCircle->point = closest;
    }
    return true;
}

bool TestCollision(const Collider2D& a, const Collider2D& b, Contact2D* out) {
    Contact2D contact;
    bool hit = false;
    if (a.shape == Collider2DShape::Circle && b.shape == Collider2DShape::Circle) {
        hit = TestCircleCircle(a, b, &contact);
    } else if (a.shape == Collider2DShape::Box && b.shape == Collider2DShape::Box) {
        hit = TestBoxBox(a, b, &contact);
    } else if (a.shape == Collider2DShape::Circle && b.shape == Collider2DShape::Box) {
        hit = TestCircleBox(a, b, &contact);
        contact.normal = -contact.normal;
    } else {
        hit = TestCircleBox(b, a, &contact);
    }
    if (!hit) {
        return false;
    }
    if (out != nullptr) {
        *out = contact;
    }
    return true;
}

bool TestSphereSphere(const Collider3D& a, const Collider3D& b, Contact3D* out) {
    const PhysicsVec3 delta = b.center - a.center;
    const float distanceSq = Dot(delta, delta);
    const float radiusSum = a.radius + b.radius;
    if (distanceSq > radiusSum * radiusSum) {
        return false;
    }
    const float distance = std::sqrt(std::max(distanceSq, 0.0f));
    const PhysicsVec3 normal = distance > 0.0001f ? delta / distance : PhysicsVec3{0.0f, 1.0f, 0.0f};
    if (out != nullptr) {
        out->normal = normal;
        out->penetration = radiusSum - distance;
        out->point = a.center + normal * (a.radius - out->penetration * 0.5f);
    }
    return true;
}

bool TestBoxBox(const Collider3D& a, const Collider3D& b, Contact3D* out) {
    const float overlaps[3] = {
        std::min(a.bounds.max.x, b.bounds.max.x) - std::max(a.bounds.min.x, b.bounds.min.x),
        std::min(a.bounds.max.y, b.bounds.max.y) - std::max(a.bounds.min.y, b.bounds.min.y),
        std::min(a.bounds.max.z, b.bounds.max.z) - std::max(a.bounds.min.z, b.bounds.min.z),
    };
    if (overlaps[0] < 0.0f || overlaps[1] < 0.0f || overlaps[2] < 0.0f) {
        return false;
    }
    int axis = 0;
    if (overlaps[1] < overlaps[axis]) {
        axis = 1;
    }
    if (overlaps[2] < overlaps[axis]) {
        axis = 2;
    }
    PhysicsVec3 normal;
    const PhysicsVec3 delta = b.center - a.center;
    if (axis == 0) {
        normal = {delta.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f};
    } else if (axis == 1) {
        normal = {0.0f, delta.y < 0.0f ? -1.0f : 1.0f, 0.0f};
    } else {
        normal = {0.0f, 0.0f, delta.z < 0.0f ? -1.0f : 1.0f};
    }
    if (out != nullptr) {
        out->normal = normal;
        out->penetration = overlaps[axis];
        out->point = (a.center + b.center) * 0.5f;
    }
    return true;
}

bool TestSphereBox(const Collider3D& sphere, const Collider3D& box, Contact3D* outBoxToSphere) {
    const PhysicsVec3 closest{std::clamp(sphere.center.x, box.bounds.min.x, box.bounds.max.x),
                              std::clamp(sphere.center.y, box.bounds.min.y, box.bounds.max.y),
                              std::clamp(sphere.center.z, box.bounds.min.z, box.bounds.max.z)};
    PhysicsVec3 boxToSphere = sphere.center - closest;
    float distance = Length(boxToSphere);
    float penetration = sphere.radius - distance;

    if (distance <= 0.0001f) {
        const float gaps[3] = {
            std::min(std::fabs(sphere.center.x - box.bounds.min.x), std::fabs(box.bounds.max.x - sphere.center.x)),
            std::min(std::fabs(sphere.center.y - box.bounds.min.y), std::fabs(box.bounds.max.y - sphere.center.y)),
            std::min(std::fabs(sphere.center.z - box.bounds.min.z), std::fabs(box.bounds.max.z - sphere.center.z)),
        };
        int axis = 0;
        if (gaps[1] < gaps[axis]) {
            axis = 1;
        }
        if (gaps[2] < gaps[axis]) {
            axis = 2;
        }
        if (axis == 0) {
            boxToSphere = {sphere.center.x < box.center.x ? -1.0f : 1.0f, 0.0f, 0.0f};
        } else if (axis == 1) {
            boxToSphere = {0.0f, sphere.center.y < box.center.y ? -1.0f : 1.0f, 0.0f};
        } else {
            boxToSphere = {0.0f, 0.0f, sphere.center.z < box.center.z ? -1.0f : 1.0f};
        }
        penetration = sphere.radius + gaps[axis];
    }

    if (penetration < 0.0f) {
        return false;
    }
    if (outBoxToSphere != nullptr) {
        outBoxToSphere->normal = Normalize(boxToSphere, {0.0f, 1.0f, 0.0f});
        outBoxToSphere->penetration = penetration;
        outBoxToSphere->point = closest;
    }
    return true;
}

Entity TerrainEntityFromCollider(const Collider3D& terrain) {
    Entity entity;
    entity.id = terrain.entityId;
    entity.name = terrain.entityName;
    entity.position = terrain.transformPosition;
    entity.scale = terrain.transformScale;
    return entity;
}

Entity CaveEntityFromCollider(const Collider3D& cave) {
    Entity entity;
    entity.id = cave.entityId;
    entity.name = cave.entityName;
    entity.position = cave.transformPosition;
    entity.scale = cave.transformScale;
    return entity;
}

bool SampleTerrainColliderAtWorldXZ(const Collider3D& terrain, float worldX, float worldZ, float* outTerrainY,
                                    PhysicsVec3* outNormal) {
    if (worldX < terrain.bounds.min.x || worldX > terrain.bounds.max.x || worldZ < terrain.bounds.min.z ||
        worldZ > terrain.bounds.max.z) {
        return false;
    }

    const Entity entity = TerrainEntityFromCollider(terrain);

    std::array<float, 2> uv{};
    if (!TerrainWorldToUv(terrain.terrain, entity, {worldX, terrain.bounds.max.y, worldZ}, &uv)) {
        return false;
    }
    const TerrainSample sample =
        SampleTerrainLocal(terrain.terrain, (uv[0] - 0.5f) * terrain.terrain.size[0], (uv[1] - 0.5f) * terrain.terrain.size[2]);
    if (!sample.valid || sample.hole) {
        return false;
    }
    const std::array<float, 3> worldPoint = TerrainUvToWorld(terrain.terrain, entity, uv, sample.height);
    if (outTerrainY != nullptr) {
        *outTerrainY = worldPoint[1];
    }
    if (outNormal != nullptr) {
        *outNormal = ToPhysicsVec3(sample.normal);
    }
    return true;
}

bool TerrainColliderContainsVolumeAir(const Collider3D& terrain, PhysicsVec3 point);
bool TestTerrainVolumeSphere(const Collider3D& terrain, const Collider3D& sphere, Contact3D* outTerrainToSphere);
bool TestTerrainVolumeBox(const Collider3D& terrain, const Collider3D& box, Contact3D* outTerrainToBox);

bool TestTerrainSphere(const Collider3D& terrain, const Collider3D& sphere, Contact3D* outTerrainToSphere) {
    if (TerrainUsesVolumetric(terrain.terrain)) {
        return TestTerrainVolumeSphere(terrain, sphere, outTerrainToSphere);
    }
    const bool insideTerrainVolumeAir = TerrainColliderContainsVolumeAir(terrain, sphere.center);
    float terrainY = 0.0f;
    PhysicsVec3 normal{0.0f, 1.0f, 0.0f};
    if (!insideTerrainVolumeAir &&
        SampleTerrainColliderAtWorldXZ(terrain, sphere.center.x, sphere.center.z, &terrainY, &normal)) {
        const float bottom = sphere.center.y - sphere.radius;
        const float penetration = terrainY - bottom;
        if (penetration >= 0.0f) {
            if (outTerrainToSphere != nullptr) {
                outTerrainToSphere->normal = Normalize(normal, {0.0f, 1.0f, 0.0f});
                outTerrainToSphere->penetration = penetration;
                outTerrainToSphere->point = {sphere.center.x, terrainY, sphere.center.z};
            }
            return true;
        }
    }
    return TestTerrainVolumeSphere(terrain, sphere, outTerrainToSphere);
}

bool TestTerrainBox(const Collider3D& terrain, const Collider3D& box, Contact3D* outTerrainToBox) {
    if (TerrainUsesVolumetric(terrain.terrain)) {
        return TestTerrainVolumeBox(terrain, box, outTerrainToBox);
    }
    const bool insideTerrainVolumeAir = TerrainColliderContainsVolumeAir(terrain, box.center);
    const float sampleX = std::clamp(box.center.x, terrain.bounds.min.x, terrain.bounds.max.x);
    const float sampleZ = std::clamp(box.center.z, terrain.bounds.min.z, terrain.bounds.max.z);
    float terrainY = 0.0f;
    PhysicsVec3 normal{0.0f, 1.0f, 0.0f};
    if (!insideTerrainVolumeAir && SampleTerrainColliderAtWorldXZ(terrain, sampleX, sampleZ, &terrainY, &normal)) {
        const float bottom = box.bounds.min.y;
        const float penetration = terrainY - bottom;
        if (penetration >= 0.0f) {
            if (outTerrainToBox != nullptr) {
                outTerrainToBox->normal = Normalize(normal, {0.0f, 1.0f, 0.0f});
                outTerrainToBox->penetration = penetration;
                outTerrainToBox->point = {sampleX, terrainY, sampleZ};
            }
            return true;
        }
    }
    return TestTerrainVolumeBox(terrain, box, outTerrainToBox);
}

float SupportDistanceForDirection(const Collider3D& shape, PhysicsVec3 direction) {
    direction = Normalize(direction, {0.0f, -1.0f, 0.0f});
    if (shape.shape == Collider3DShape::Sphere) {
        return shape.radius;
    }
    return std::fabs(direction.x) * shape.halfExtents.x + std::fabs(direction.y) * shape.halfExtents.y +
           std::fabs(direction.z) * shape.halfExtents.z;
}

bool TestCaveSurfaceContact(const CaveVolumeData& cave, const Entity& caveEntity, const Collider3D& shape,
                            Contact3D* outCaveToShape) {
    constexpr PhysicsVec3 kDirections[] = {{0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},  {1.0f, 0.0f, 0.0f},
                                           {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f}};
    bool found = false;
    Contact3D best;
    float bestPenetration = -std::numeric_limits<float>::max();
    for (PhysicsVec3 direction : kDirections) {
        const float support = SupportDistanceForDirection(shape, direction);
        const float maxDistance = support + 0.35f;
        CaveRayHit hit;
        if (!CaveRaycastWorld(cave, caveEntity, {shape.center.x, shape.center.y, shape.center.z},
                              {direction.x, direction.y, direction.z}, maxDistance, &hit)) {
            continue;
        }
        const float penetration = support - hit.distance;
        if (penetration < 0.0f || penetration <= bestPenetration) {
            continue;
        }
        bestPenetration = penetration;
        best.normal = Normalize(ToPhysicsVec3(hit.normal), {-direction.x, -direction.y, -direction.z});
        best.penetration = penetration;
        best.point = ToPhysicsVec3(hit.point);
        found = true;
    }
    if (found && outCaveToShape != nullptr) {
        *outCaveToShape = best;
    }
    return found;
}

bool TerrainColliderContainsVolumeAir(const Collider3D& terrain, PhysicsVec3 point) {
    return TerrainVolumeContainsAirAtWorld(terrain.terrain, TerrainEntityFromCollider(terrain), ToArray(point));
}

bool TestTerrainVolumeSphere(const Collider3D& terrain, const Collider3D& sphere, Contact3D* outTerrainToSphere) {
    if (!TerrainUsesVolumetric(terrain.terrain)) {
        return false;
    }
    return TestCaveSurfaceContact(terrain.terrain.volume,
                                  TerrainVolumeProxyEntity(terrain.terrain, TerrainEntityFromCollider(terrain)), sphere,
                                  outTerrainToSphere);
}

bool TestTerrainVolumeBox(const Collider3D& terrain, const Collider3D& box, Contact3D* outTerrainToBox) {
    if (!TerrainUsesVolumetric(terrain.terrain)) {
        return false;
    }
    return TestCaveSurfaceContact(terrain.terrain.volume,
                                  TerrainVolumeProxyEntity(terrain.terrain, TerrainEntityFromCollider(terrain)), box,
                                  outTerrainToBox);
}

bool TestCaveSphere(const Collider3D& cave, const Collider3D& sphere, Contact3D* outCaveToSphere) {
    const Entity entity = CaveEntityFromCollider(cave);
    return TestCaveSurfaceContact(cave.cave, entity, sphere, outCaveToSphere);
}

bool TestCaveBox(const Collider3D& cave, const Collider3D& box, Contact3D* outCaveToBox) {
    const Entity entity = CaveEntityFromCollider(cave);
    return TestCaveSurfaceContact(cave.cave, entity, box, outCaveToBox);
}

bool TestCollision(const Collider3D& a, const Collider3D& b, Contact3D* out) {
    Contact3D contact;
    bool hit = false;
    if ((a.shape == Collider3DShape::Terrain || a.shape == Collider3DShape::Cave) &&
        (b.shape == Collider3DShape::Terrain || b.shape == Collider3DShape::Cave)) {
        hit = false;
    } else if (a.shape == Collider3DShape::Terrain) {
        hit = b.shape == Collider3DShape::Sphere ? TestTerrainSphere(a, b, &contact) : TestTerrainBox(a, b, &contact);
    } else if (b.shape == Collider3DShape::Terrain) {
        hit = a.shape == Collider3DShape::Sphere ? TestTerrainSphere(b, a, &contact) : TestTerrainBox(b, a, &contact);
        contact.normal = -contact.normal;
    } else if (a.shape == Collider3DShape::Cave) {
        hit = b.shape == Collider3DShape::Sphere ? TestCaveSphere(a, b, &contact) : TestCaveBox(a, b, &contact);
    } else if (b.shape == Collider3DShape::Cave) {
        hit = a.shape == Collider3DShape::Sphere ? TestCaveSphere(b, a, &contact) : TestCaveBox(b, a, &contact);
        contact.normal = -contact.normal;
    } else if (a.shape == Collider3DShape::Sphere && b.shape == Collider3DShape::Sphere) {
        hit = TestSphereSphere(a, b, &contact);
    } else if (a.shape == Collider3DShape::Box && b.shape == Collider3DShape::Box) {
        hit = TestBoxBox(a, b, &contact);
    } else if (a.shape == Collider3DShape::Sphere && b.shape == Collider3DShape::Box) {
        hit = TestSphereBox(a, b, &contact);
        contact.normal = -contact.normal;
    } else {
        hit = TestSphereBox(b, a, &contact);
    }
    if (!hit) {
        return false;
    }
    if (out != nullptr) {
        *out = contact;
    }
    return true;
}

std::vector<Contact2D> FindContacts2D(const std::vector<Collider2D>& colliders, const PhysicsSettings& settings) {
    std::vector<Contact2D> contacts;
    for (size_t i = 0; i < colliders.size(); ++i) {
        for (size_t j = i + 1; j < colliders.size(); ++j) {
            const Collider2D& a = colliders[i];
            const Collider2D& b = colliders[j];
            if (a.bodyIndex == b.bodyIndex || !LayersCanCollide(settings, a.layer, b.layer) ||
                !Intersects(a.bounds, b.bounds)) {
                continue;
            }
            Contact2D contact;
            if (TestCollision(a, b, &contact)) {
                contact.colliderA = static_cast<int>(i);
                contact.colliderB = static_cast<int>(j);
                contact.trigger = a.trigger || b.trigger;
                contacts.push_back(contact);
            }
        }
    }
    return contacts;
}

std::vector<Contact3D> FindContacts3D(const std::vector<Collider3D>& colliders, const PhysicsSettings& settings) {
    std::vector<Contact3D> contacts;
    for (size_t i = 0; i < colliders.size(); ++i) {
        for (size_t j = i + 1; j < colliders.size(); ++j) {
            const Collider3D& a = colliders[i];
            const Collider3D& b = colliders[j];
            if (a.bodyIndex == b.bodyIndex || !LayersCanCollide(settings, a.layer, b.layer) ||
                !Intersects(a.bounds, b.bounds)) {
                continue;
            }
            Contact3D contact;
            if (TestCollision(a, b, &contact)) {
                contact.colliderA = static_cast<int>(i);
                contact.colliderB = static_cast<int>(j);
                contact.trigger = a.trigger || b.trigger;
                contacts.push_back(contact);
            }
        }
    }
    return contacts;
}

void SolveContact(const Contact2D& contact, std::vector<Body2D>& bodies, const std::vector<Collider2D>& colliders) {
    if (contact.trigger) {
        return;
    }

    Body2D& bodyA = bodies[static_cast<size_t>(colliders[static_cast<size_t>(contact.colliderA)].bodyIndex)];
    Body2D& bodyB = bodies[static_cast<size_t>(colliders[static_cast<size_t>(contact.colliderB)].bodyIndex)];
    const float impulseInverseMassA = ImpulseInverseMass(bodyA);
    const float impulseInverseMassB = ImpulseInverseMass(bodyB);
    const float impulseInverseMassSum = impulseInverseMassA + impulseInverseMassB;
    const PhysicsVec2 relativeVelocity = bodyB.velocity - bodyA.velocity;
    const float velocityAlongNormal = Dot(relativeVelocity, contact.normal);
    if (impulseInverseMassSum > 0.0f && velocityAlongNormal <= 0.0f) {
        const float restitution = std::min(bodyA.restitution, bodyB.restitution);
        const float impulseMagnitude = -(1.0f + restitution) * velocityAlongNormal / impulseInverseMassSum;
        const PhysicsVec2 impulse = contact.normal * impulseMagnitude;
        if (bodyA.type == BodyType::Dynamic) {
            bodyA.velocity = bodyA.velocity - impulse * impulseInverseMassA;
        }
        if (bodyB.type == BodyType::Dynamic) {
            bodyB.velocity = bodyB.velocity + impulse * impulseInverseMassB;
        }

        PhysicsVec2 tangent = relativeVelocity - contact.normal * velocityAlongNormal;
        if (Length(tangent) > 0.0001f) {
            tangent = Normalize(tangent);
            float frictionMagnitude = -Dot(relativeVelocity, tangent) / impulseInverseMassSum;
            const float frictionLimit = impulseMagnitude * std::sqrt(bodyA.friction * bodyB.friction);
            frictionMagnitude = std::clamp(frictionMagnitude, -frictionLimit, frictionLimit);
            const PhysicsVec2 frictionImpulse = tangent * frictionMagnitude;
            if (bodyA.type == BodyType::Dynamic) {
                bodyA.velocity = bodyA.velocity - frictionImpulse * impulseInverseMassA;
            }
            if (bodyB.type == BodyType::Dynamic) {
                bodyB.velocity = bodyB.velocity + frictionImpulse * impulseInverseMassB;
            }
        }
    }

    const float positionWeightA = PositionCorrectionWeight(bodyA, bodyB);
    const float positionWeightB = PositionCorrectionWeight(bodyB, bodyA);
    const float positionWeightSum = positionWeightA + positionWeightB;
    if (positionWeightSum <= 0.0f) {
        return;
    }
    const PhysicsVec2 correction = contact.normal *
                                   (std::max(contact.penetration - kPositionSlop, 0.0f) / positionWeightSum *
                                    kPositionCorrectionPercent);
    if (positionWeightA > 0.0f) {
        bodyA.position = bodyA.position - correction * positionWeightA;
    }
    if (positionWeightB > 0.0f) {
        bodyB.position = bodyB.position + correction * positionWeightB;
    }
    ApplyLocks(bodyA);
    ApplyLocks(bodyB);
}

void SolveContact(const Contact3D& contact, std::vector<Body3D>& bodies, const std::vector<Collider3D>& colliders) {
    if (contact.trigger) {
        return;
    }

    Body3D& bodyA = bodies[static_cast<size_t>(colliders[static_cast<size_t>(contact.colliderA)].bodyIndex)];
    Body3D& bodyB = bodies[static_cast<size_t>(colliders[static_cast<size_t>(contact.colliderB)].bodyIndex)];
    const float impulseInverseMassA = ImpulseInverseMass(bodyA);
    const float impulseInverseMassB = ImpulseInverseMass(bodyB);
    const float impulseInverseMassSum = impulseInverseMassA + impulseInverseMassB;
    const PhysicsVec3 relativeVelocity = bodyB.velocity - bodyA.velocity;
    const float velocityAlongNormal = Dot(relativeVelocity, contact.normal);
    if (impulseInverseMassSum > 0.0f && velocityAlongNormal <= 0.0f) {
        const float restitution = std::min(bodyA.restitution, bodyB.restitution);
        const float impulseMagnitude = -(1.0f + restitution) * velocityAlongNormal / impulseInverseMassSum;
        const PhysicsVec3 impulse = contact.normal * impulseMagnitude;
        if (bodyA.type == BodyType::Dynamic) {
            bodyA.velocity = bodyA.velocity - impulse * impulseInverseMassA;
        }
        if (bodyB.type == BodyType::Dynamic) {
            bodyB.velocity = bodyB.velocity + impulse * impulseInverseMassB;
        }

        PhysicsVec3 tangent = relativeVelocity - contact.normal * velocityAlongNormal;
        if (Length(tangent) > 0.0001f) {
            tangent = Normalize(tangent);
            float frictionMagnitude = -Dot(relativeVelocity, tangent) / impulseInverseMassSum;
            const float frictionLimit = impulseMagnitude * std::sqrt(bodyA.friction * bodyB.friction);
            frictionMagnitude = std::clamp(frictionMagnitude, -frictionLimit, frictionLimit);
            const PhysicsVec3 frictionImpulse = tangent * frictionMagnitude;
            if (bodyA.type == BodyType::Dynamic) {
                bodyA.velocity = bodyA.velocity - frictionImpulse * impulseInverseMassA;
            }
            if (bodyB.type == BodyType::Dynamic) {
                bodyB.velocity = bodyB.velocity + frictionImpulse * impulseInverseMassB;
            }
        }
    }

    const float positionWeightA = PositionCorrectionWeight(bodyA, bodyB);
    const float positionWeightB = PositionCorrectionWeight(bodyB, bodyA);
    const float positionWeightSum = positionWeightA + positionWeightB;
    if (positionWeightSum <= 0.0f) {
        return;
    }
    const PhysicsVec3 correction = contact.normal *
                                   (std::max(contact.penetration - kPositionSlop, 0.0f) / positionWeightSum *
                                    kPositionCorrectionPercent);
    if (positionWeightA > 0.0f) {
        bodyA.position = bodyA.position - correction * positionWeightA;
    }
    if (positionWeightB > 0.0f) {
        bodyB.position = bodyB.position + correction * positionWeightB;
    }
    ApplyLocks(bodyA);
    ApplyLocks(bodyB);
}

void UpdateAllColliders(std::vector<Collider2D>& colliders, const std::vector<Body2D>& bodies,
                        const std::vector<Entity>& entities) {
    for (Collider2D& collider : colliders) {
        UpdateCollider(collider, bodies, entities);
    }
}

void UpdateAllColliders(std::vector<Collider3D>& colliders, const std::vector<Body3D>& bodies,
                        const std::vector<Entity>& entities) {
    for (Collider3D& collider : colliders) {
        UpdateCollider(collider, bodies, entities);
    }
}

PhysicsEvent ToEvent(const Contact2D& contact, const std::vector<Collider2D>& colliders) {
    const Collider2D& a = colliders[static_cast<size_t>(contact.colliderA)];
    const Collider2D& b = colliders[static_cast<size_t>(contact.colliderB)];
    PhysicsEvent event;
    event.dimension = "2D";
    event.type = contact.trigger ? "Trigger" : "Collision";
    event.entityAId = a.entityId;
    event.entityBId = b.entityId;
    event.entityAName = a.entityName;
    event.entityBName = b.entityName;
    event.point = {contact.point.x, contact.point.y, 0.0f};
    event.normal = {contact.normal.x, contact.normal.y, 0.0f};
    event.penetration = contact.penetration;
    event.trigger = contact.trigger;
    return event;
}

PhysicsEvent ToEvent(const Contact3D& contact, const std::vector<Collider3D>& colliders) {
    const Collider3D& a = colliders[static_cast<size_t>(contact.colliderA)];
    const Collider3D& b = colliders[static_cast<size_t>(contact.colliderB)];
    PhysicsEvent event;
    event.dimension = "3D";
    event.type = contact.trigger ? "Trigger" : "Collision";
    event.entityAId = a.entityId;
    event.entityBId = b.entityId;
    event.entityAName = a.entityName;
    event.entityBName = b.entityName;
    event.point = contact.point;
    event.normal = contact.normal;
    event.penetration = contact.penetration;
    event.trigger = contact.trigger;
    return event;
}

void AddUnique(std::vector<int>& values, int value) {
    if (value == 0) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void WriteBodies(std::vector<Entity>& entities, const std::vector<Body2D>& bodies, PhysicsStepResult* result) {
    for (const Body2D& body : bodies) {
        Entity& entity = entities[body.entityIndex];
        const bool transformChanged = std::fabs(entity.position[0] - body.position.x) > 0.00001f ||
                                      std::fabs(entity.position[1] - body.position.y) > 0.00001f ||
                                      std::fabs(entity.rotation[2] - body.rotationDegrees) > 0.00001f;
        entity.position[0] = body.position.x;
        entity.position[1] = body.position.y;
        entity.rotation[2] = body.rotationDegrees;
        if (body.hasComponent && body.componentIndex < entity.components.size()) {
            Component& component = entity.components[body.componentIndex];
            SetProperty(component, "velocity", FormatVec2(body.velocity));
            SetProperty(component, "angularVelocity", FormatFloat(body.angularVelocity));
            SetProperty(component, "awake", body.awake ? "true" : "false");
        }
        if (transformChanged && result != nullptr) {
            AddUnique(result->changedEntityIds, entity.id);
        }
    }
}

void WriteBodies(std::vector<Entity>& entities, const std::vector<Body3D>& bodies, PhysicsStepResult* result) {
    for (const Body3D& body : bodies) {
        Entity& entity = entities[body.entityIndex];
        const bool transformChanged = std::fabs(entity.position[0] - body.position.x) > 0.00001f ||
                                      std::fabs(entity.position[1] - body.position.y) > 0.00001f ||
                                      std::fabs(entity.position[2] - body.position.z) > 0.00001f ||
                                      std::fabs(entity.rotation[0] - body.rotationDegrees.x) > 0.00001f ||
                                      std::fabs(entity.rotation[1] - body.rotationDegrees.y) > 0.00001f ||
                                      std::fabs(entity.rotation[2] - body.rotationDegrees.z) > 0.00001f;
        entity.position = {body.position.x, body.position.y, body.position.z};
        entity.rotation = {body.rotationDegrees.x, body.rotationDegrees.y, body.rotationDegrees.z};
        if (body.hasComponent && body.componentIndex < entity.components.size()) {
            Component& component = entity.components[body.componentIndex];
            SetProperty(component, "velocity", FormatVec3(body.velocity));
            SetProperty(component, "angularVelocity", FormatVec3(body.angularVelocity));
            SetProperty(component, "awake", body.awake ? "true" : "false");
        }
        if (transformChanged && result != nullptr) {
            AddUnique(result->changedEntityIds, entity.id);
        }
    }
}

void WriteEventSummary(std::vector<Entity>& entities, const std::vector<PhysicsEvent>& events) {
    std::unordered_map<int, int> contactCounts;
    for (const PhysicsEvent& event : events) {
        ++contactCounts[event.entityAId];
        ++contactCounts[event.entityBId];
    }

    for (Entity& entity : entities) {
        if (!EntityActiveInHierarchy(entities, entity)) {
            continue;
        }
        const int contactCount = contactCounts[entity.id];
        for (Component& component : entity.components) {
            if (component.type != "Rigidbody2D" && component.type != "Rigidbody3D" && component.type != "Collider2D" &&
                component.type != "Collider3D") {
                continue;
            }
            if (!ComponentEnabled(component)) {
                continue;
            }
            SetProperty(component, "lastEventCount", std::to_string(contactCount));
            if (contactCount == 0) {
                continue;
            }
            for (const PhysicsEvent& event : events) {
                if (event.entityAId != entity.id && event.entityBId != entity.id) {
                    continue;
                }
                const bool entityIsA = event.entityAId == entity.id;
                SetProperty(component, "lastEventType", event.type);
                SetProperty(component, "lastEventOther", entityIsA ? event.entityBName : event.entityAName);
                SetProperty(component, "lastEventNormal", FormatVec3(event.normal));
                SetProperty(component, "lastEventPoint", FormatVec3(event.point));
                break;
            }
        }
    }
}

bool RayCircle(PhysicsVec2 origin, PhysicsVec2 direction, float maxDistance, const Collider2D& circle,
               PhysicsRayHit2D* outHit) {
    const PhysicsVec2 m = origin - circle.center;
    const float b = Dot(m, direction);
    const float c = Dot(m, m) - circle.radius * circle.radius;
    if (c > 0.0f && b > 0.0f) {
        return false;
    }
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return false;
    }
    float distance = -b - std::sqrt(discriminant);
    if (distance < 0.0f) {
        distance = 0.0f;
    }
    if (distance > maxDistance) {
        return false;
    }
    if (outHit != nullptr) {
        outHit->hit = true;
        outHit->entityId = circle.entityId;
        outHit->entityName = circle.entityName;
        outHit->colliderType = "Collider2D.Circle";
        outHit->distance = distance;
        outHit->point = origin + direction * distance;
        outHit->normal = Normalize(outHit->point - circle.center, {0.0f, 1.0f});
    }
    return true;
}

bool RayBox2D(PhysicsVec2 origin, PhysicsVec2 direction, float maxDistance, const Collider2D& box,
              PhysicsRayHit2D* outHit) {
    const PhysicsVec2 localOrigin{Dot(origin - box.center, box.axisX), Dot(origin - box.center, box.axisY)};
    const PhysicsVec2 localDirection{Dot(direction, box.axisX), Dot(direction, box.axisY)};
    float tMin = 0.0f;
    float tMax = maxDistance;
    int hitAxis = 0;
    const float origins[2] = {localOrigin.x, localOrigin.y};
    const float directions[2] = {localDirection.x, localDirection.y};
    const float mins[2] = {-box.halfExtents.x, -box.halfExtents.y};
    const float maxs[2] = {box.halfExtents.x, box.halfExtents.y};
    for (int axis = 0; axis < 2; ++axis) {
        if (std::fabs(directions[axis]) <= 0.00001f) {
            if (origins[axis] < mins[axis] || origins[axis] > maxs[axis]) {
                return false;
            }
            continue;
        }
        float t1 = (mins[axis] - origins[axis]) / directions[axis];
        float t2 = (maxs[axis] - origins[axis]) / directions[axis];
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        if (t1 > tMin) {
            tMin = t1;
            hitAxis = axis;
        }
        tMax = std::min(tMax, t2);
        if (tMin > tMax) {
            return false;
        }
    }
    if (tMin > maxDistance) {
        return false;
    }
    if (outHit != nullptr) {
        outHit->hit = true;
        outHit->entityId = box.entityId;
        outHit->entityName = box.entityName;
        outHit->colliderType = "Collider2D.Box";
        outHit->distance = tMin;
        outHit->point = origin + direction * tMin;
        PhysicsVec2 localNormal = hitAxis == 0 ? PhysicsVec2{localDirection.x > 0.0f ? -1.0f : 1.0f, 0.0f}
                                               : PhysicsVec2{0.0f, localDirection.y > 0.0f ? -1.0f : 1.0f};
        outHit->normal = Normalize(box.axisX * localNormal.x + box.axisY * localNormal.y);
    }
    return true;
}

bool RaySphere(PhysicsVec3 origin, PhysicsVec3 direction, float maxDistance, const Collider3D& sphere,
               PhysicsRayHit3D* outHit) {
    const PhysicsVec3 m = origin - sphere.center;
    const float b = Dot(m, direction);
    const float c = Dot(m, m) - sphere.radius * sphere.radius;
    if (c > 0.0f && b > 0.0f) {
        return false;
    }
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return false;
    }
    float distance = -b - std::sqrt(discriminant);
    if (distance < 0.0f) {
        distance = 0.0f;
    }
    if (distance > maxDistance) {
        return false;
    }
    if (outHit != nullptr) {
        outHit->hit = true;
        outHit->entityId = sphere.entityId;
        outHit->entityName = sphere.entityName;
        outHit->colliderType = "Collider3D.Sphere";
        outHit->distance = distance;
        outHit->point = origin + direction * distance;
        outHit->normal = Normalize(outHit->point - sphere.center, {0.0f, 1.0f, 0.0f});
    }
    return true;
}

bool RayAabb(PhysicsVec3 origin, PhysicsVec3 direction, float maxDistance, const Collider3D& box,
             PhysicsRayHit3D* outHit) {
    float tMin = 0.0f;
    float tMax = maxDistance;
    int hitAxis = 0;
    const float origins[3] = {origin.x, origin.y, origin.z};
    const float directions[3] = {direction.x, direction.y, direction.z};
    const float mins[3] = {box.bounds.min.x, box.bounds.min.y, box.bounds.min.z};
    const float maxs[3] = {box.bounds.max.x, box.bounds.max.y, box.bounds.max.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(directions[axis]) <= 0.00001f) {
            if (origins[axis] < mins[axis] || origins[axis] > maxs[axis]) {
                return false;
            }
            continue;
        }
        float t1 = (mins[axis] - origins[axis]) / directions[axis];
        float t2 = (maxs[axis] - origins[axis]) / directions[axis];
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        if (t1 > tMin) {
            tMin = t1;
            hitAxis = axis;
        }
        tMax = std::min(tMax, t2);
        if (tMin > tMax) {
            return false;
        }
    }
    if (tMin > maxDistance) {
        return false;
    }
    if (outHit != nullptr) {
        outHit->hit = true;
        outHit->entityId = box.entityId;
        outHit->entityName = box.entityName;
        outHit->colliderType = "Collider3D.Box";
        outHit->distance = tMin;
        outHit->point = origin + direction * tMin;
        PhysicsVec3 normal;
        if (hitAxis == 0) {
            normal = {direction.x > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f};
        } else if (hitAxis == 1) {
            normal = {0.0f, direction.y > 0.0f ? -1.0f : 1.0f, 0.0f};
        } else {
            normal = {0.0f, 0.0f, direction.z > 0.0f ? -1.0f : 1.0f};
        }
        outHit->normal = normal;
    }
    return true;
}

bool RayTerrain(PhysicsVec3 origin, PhysicsVec3 direction, float maxDistance, const Collider3D& terrain,
                PhysicsRayHit3D* outHit) {
    const Entity entity = TerrainEntityFromCollider(terrain);
    bool anyHit = false;
    PhysicsRayHit3D closest;
    closest.distance = maxDistance;
    TerrainRayHit terrainHit;
    if (!TerrainUsesVolumetric(terrain.terrain) && TerrainUsesHeightfield(terrain.terrain) &&
        TerrainRaycastWorld(terrain.terrain, entity, ToArray(origin), ToArray(direction), maxDistance, &terrainHit)) {
        closest.hit = true;
        closest.entityId = terrain.entityId;
        closest.entityName = terrain.entityName;
        closest.colliderType = "Terrain.Heightfield";
        closest.distance = terrainHit.distance;
        closest.point = ToPhysicsVec3(terrainHit.point);
        closest.normal = ToPhysicsVec3(terrainHit.normal);
        anyHit = true;
    }
    CaveRayHit volumeHit;
    if (TerrainVolumeRaycastWorld(terrain.terrain, entity, ToArray(origin), ToArray(direction), maxDistance,
                                  &volumeHit) &&
        (!anyHit || volumeHit.distance < closest.distance)) {
        closest.hit = true;
        closest.entityId = terrain.entityId;
        closest.entityName = terrain.entityName;
        closest.colliderType = "Terrain.VolumeSurface";
        closest.distance = volumeHit.distance;
        closest.point = ToPhysicsVec3(volumeHit.point);
        closest.normal = ToPhysicsVec3(volumeHit.normal);
        anyHit = true;
    }
    if (!anyHit) {
        return false;
    }
    if (outHit != nullptr) {
        *outHit = closest;
    }
    return true;
}

bool RayCave(PhysicsVec3 origin, PhysicsVec3 direction, float maxDistance, const Collider3D& cave,
             PhysicsRayHit3D* outHit) {
    const Entity entity = CaveEntityFromCollider(cave);
    CaveRayHit caveHit;
    if (!CaveRaycastWorld(cave.cave, entity, ToArray(origin), ToArray(direction), maxDistance, &caveHit)) {
        return false;
    }
    if (outHit != nullptr) {
        outHit->hit = true;
        outHit->entityId = cave.entityId;
        outHit->entityName = cave.entityName;
        outHit->colliderType = "CaveVolume.Surface";
        outHit->distance = caveHit.distance;
        outHit->point = ToPhysicsVec3(caveHit.point);
        outHit->normal = ToPhysicsVec3(caveHit.normal);
    }
    return true;
}

bool TestSelf(bool condition, const std::string& diagnostic, std::vector<std::string>* diagnostics) {
    if (condition) {
        return true;
    }
    if (diagnostics != nullptr) {
        diagnostics->push_back(diagnostic);
    }
    return false;
}

Entity MakeTestEntity(int id, const std::string& name, std::array<float, 3> position, std::array<float, 3> scale,
                      std::vector<Component> components) {
    Entity entity;
    entity.id = id;
    entity.name = name;
    entity.position = position;
    entity.scale = scale;
    entity.components = std::move(components);
    return entity;
}

} // namespace

float Dot(PhysicsVec2 left, PhysicsVec2 right) {
    return left.x * right.x + left.y * right.y;
}

float Dot(PhysicsVec3 left, PhysicsVec3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

float Length(PhysicsVec2 value) {
    return std::sqrt(Dot(value, value));
}

float Length(PhysicsVec3 value) {
    return std::sqrt(Dot(value, value));
}

PhysicsVec2 Normalize(PhysicsVec2 value, PhysicsVec2 fallback) {
    const float length = Length(value);
    if (length <= 0.00001f || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}

PhysicsVec3 Normalize(PhysicsVec3 value, PhysicsVec3 fallback) {
    const float length = Length(value);
    if (length <= 0.00001f || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
}

PhysicsWorld::~PhysicsWorld() {
    gPhysicsHeavyColliderCaches.erase(this);
}

void PhysicsWorld::Reset() {
    accumulatorSeconds_ = 0.0f;
    lastEvents_.clear();
    lastFixedStepCount_ = 0;
    lastBody2DCount_ = 0;
    lastCollider2DCount_ = 0;
    lastBody3DCount_ = 0;
    lastCollider3DCount_ = 0;
    gPhysicsHeavyColliderCaches.erase(this);
}

PhysicsStepResult PhysicsWorld::AccumulateAndStep(std::vector<Entity>& entities, float deltaSeconds) {
    PhysicsStepResult combined;
    const PhysicsSettings settings = ReadSettings(entities);
    if (!settings.enabled || deltaSeconds <= 0.0f || !std::isfinite(deltaSeconds)) {
        lastEvents_.clear();
        lastFixedStepCount_ = 0;
        return combined;
    }

    accumulatorSeconds_ += std::min(deltaSeconds, 0.25f);
    const int maxSubSteps = 12;
    int subStep = 0;
    while (accumulatorSeconds_ + 0.000001f >= settings.fixedDeltaSeconds && subStep < maxSubSteps) {
        PhysicsStepResult step = StepFixed(entities, settings.fixedDeltaSeconds);
        combined.fixedSteps += step.fixedSteps;
        combined.body2DCount = step.body2DCount;
        combined.collider2DCount = step.collider2DCount;
        combined.body3DCount = step.body3DCount;
        combined.collider3DCount = step.collider3DCount;
        for (int entityId : step.changedEntityIds) {
            AddUnique(combined.changedEntityIds, entityId);
        }
        combined.events.insert(combined.events.end(), step.events.begin(), step.events.end());
        accumulatorSeconds_ -= settings.fixedDeltaSeconds;
        ++subStep;
    }
    if (subStep == maxSubSteps) {
        accumulatorSeconds_ = 0.0f;
    }

    lastEvents_ = combined.events;
    lastFixedStepCount_ = combined.fixedSteps;
    lastBody2DCount_ = combined.body2DCount;
    lastCollider2DCount_ = combined.collider2DCount;
    lastBody3DCount_ = combined.body3DCount;
    lastCollider3DCount_ = combined.collider3DCount;
    return combined;
}

PhysicsStepResult PhysicsWorld::StepFixed(std::vector<Entity>& entities, float fixedDeltaSeconds) {
    PhysicsStepResult result;
    const PhysicsSettings settings = ReadSettings(entities);
    if (!settings.enabled || fixedDeltaSeconds <= 0.0f || !std::isfinite(fixedDeltaSeconds)) {
        lastEvents_.clear();
        return result;
    }

    std::vector<Body2D> bodies2D;
    std::vector<Collider2D> colliders2D;
    std::vector<Body3D> bodies3D;
    std::vector<Collider3D> colliders3D;
    Build2D(entities, bodies2D, colliders2D);
    Build3D(entities, bodies3D, colliders3D, this);

    result.fixedSteps = 1;
    result.body2DCount = static_cast<int>(bodies2D.size());
    result.collider2DCount = static_cast<int>(colliders2D.size());
    result.body3DCount = static_cast<int>(bodies3D.size());
    result.collider3DCount = static_cast<int>(colliders3D.size());

    for (Body2D& body : bodies2D) {
        IntegrateBody(body, settings.gravity2D, fixedDeltaSeconds);
    }
    for (Body3D& body : bodies3D) {
        IntegrateBody(body, settings.gravity3D, fixedDeltaSeconds);
    }

    for (int iteration = 0; iteration < settings.solverIterations; ++iteration) {
        UpdateAllColliders(colliders2D, bodies2D, entities);
        UpdateAllColliders(colliders3D, bodies3D, entities);

        const std::vector<Contact2D> contacts2D = FindContacts2D(colliders2D, settings);
        const std::vector<Contact3D> contacts3D = FindContacts3D(colliders3D, settings);
        if (iteration == 0) {
            for (const Contact2D& contact : contacts2D) {
                result.events.push_back(ToEvent(contact, colliders2D));
            }
            for (const Contact3D& contact : contacts3D) {
                result.events.push_back(ToEvent(contact, colliders3D));
            }
        }

        for (const Contact2D& contact : contacts2D) {
            SolveContact(contact, bodies2D, colliders2D);
        }
        for (const Contact3D& contact : contacts3D) {
            SolveContact(contact, bodies3D, colliders3D);
        }
    }

    WriteBodies(entities, bodies2D, &result);
    WriteBodies(entities, bodies3D, &result);
    WriteEventSummary(entities, result.events);

    lastEvents_ = result.events;
    lastFixedStepCount_ = result.fixedSteps;
    lastBody2DCount_ = result.body2DCount;
    lastCollider2DCount_ = result.collider2DCount;
    lastBody3DCount_ = result.body3DCount;
    lastCollider3DCount_ = result.collider3DCount;
    return result;
}

bool PhysicsWorld::Raycast2D(const std::vector<Entity>& entities, PhysicsVec2 origin, PhysicsVec2 direction,
                             float maxDistance, PhysicsRayHit2D* outHit) const {
    if (maxDistance <= 0.0f || !std::isfinite(maxDistance)) {
        return false;
    }
    direction = Normalize(direction);
    std::vector<Body2D> bodies;
    std::vector<Collider2D> colliders;
    Build2D(entities, bodies, colliders);

    bool anyHit = false;
    PhysicsRayHit2D closest;
    closest.distance = maxDistance;
    for (const Collider2D& collider : colliders) {
        PhysicsRayHit2D candidate;
        const bool hit = collider.shape == Collider2DShape::Circle
                             ? RayCircle(origin, direction, maxDistance, collider, &candidate)
                             : RayBox2D(origin, direction, maxDistance, collider, &candidate);
        if (hit && (!anyHit || candidate.distance < closest.distance)) {
            closest = candidate;
            anyHit = true;
        }
    }
    if (anyHit && outHit != nullptr) {
        *outHit = closest;
    }
    return anyHit;
}

bool PhysicsWorld::Raycast3D(const std::vector<Entity>& entities, PhysicsVec3 origin, PhysicsVec3 direction,
                             float maxDistance, PhysicsRayHit3D* outHit) const {
    if (maxDistance <= 0.0f || !std::isfinite(maxDistance)) {
        return false;
    }
    direction = Normalize(direction);
    std::vector<Body3D> bodies;
    std::vector<Collider3D> colliders;
    Build3D(entities, bodies, colliders, this);

    bool anyHit = false;
    PhysicsRayHit3D closest;
    closest.distance = maxDistance;
    for (const Collider3D& collider : colliders) {
        PhysicsRayHit3D candidate;
        const bool hit = collider.shape == Collider3DShape::Terrain
                             ? RayTerrain(origin, direction, maxDistance, collider, &candidate)
                             : (collider.shape == Collider3DShape::Cave
                                    ? RayCave(origin, direction, maxDistance, collider, &candidate)
                                    : (collider.shape == Collider3DShape::Sphere
                                           ? RaySphere(origin, direction, maxDistance, collider, &candidate)
                                           : RayAabb(origin, direction, maxDistance, collider, &candidate)));
        if (hit && (!anyHit || candidate.distance < closest.distance)) {
            closest = candidate;
            anyHit = true;
        }
    }
    if (anyHit && outHit != nullptr) {
        *outHit = closest;
    }
    return anyHit;
}

bool PhysicsWorld::SphereCast3D(const std::vector<Entity>& entities, PhysicsVec3 origin, PhysicsVec3 direction,
                                float maxDistance, float radius, PhysicsRayHit3D* outHit) const {
    if (maxDistance <= 0.0f || !std::isfinite(maxDistance)) {
        return false;
    }
    radius = std::max(0.0f, radius);
    if (radius <= 0.0001f) {
        return Raycast3D(entities, origin, direction, maxDistance, outHit);
    }

    direction = Normalize(direction);
    const PhysicsVec3 reference = std::fabs(direction.y) < 0.82f ? PhysicsVec3{0.0f, 1.0f, 0.0f}
                                                                  : PhysicsVec3{1.0f, 0.0f, 0.0f};
    const PhysicsVec3 right = Normalize(Cross(reference, direction), {1.0f, 0.0f, 0.0f});
    const PhysicsVec3 up = Normalize(Cross(direction, right), {0.0f, 1.0f, 0.0f});
    const float diagonalRadius = radius * 0.70710678f;
    const std::array<PhysicsVec3, 9> offsets{
        PhysicsVec3{0.0f, 0.0f, 0.0f},
        right * radius,
        right * -radius,
        up * radius,
        up * -radius,
        (right + up) * diagonalRadius,
        (right - up) * diagonalRadius,
        (right * -1.0f + up) * diagonalRadius,
        (right * -1.0f - up) * diagonalRadius,
    };

    bool anyHit = false;
    PhysicsRayHit3D closest;
    closest.distance = maxDistance;
    constexpr float kSweepSkin = 0.015f;
    for (size_t index = 0; index < offsets.size(); ++index) {
        PhysicsRayHit3D candidate;
        if (!Raycast3D(entities, origin + offsets[index], direction, maxDistance, &candidate)) {
            continue;
        }

        const bool centerRay = index == 0;
        const float safeDistance =
            std::max(0.0f, candidate.distance - (centerRay ? radius : 0.0f) - kSweepSkin);
        if (anyHit && safeDistance >= closest.distance) {
            continue;
        }

        closest = candidate;
        closest.distance = safeDistance;
        closest.point = origin + direction * safeDistance;
        closest.colliderType += ".SphereCast";
        anyHit = true;
    }

    if (anyHit && outHit != nullptr) {
        *outHit = closest;
    }
    return anyHit;
}

std::vector<int> PhysicsWorld::OverlapCircle2D(const std::vector<Entity>& entities, PhysicsVec2 center, float radius) const {
    std::vector<int> hits;
    if (radius <= 0.0f || !std::isfinite(radius)) {
        return hits;
    }
    std::vector<Body2D> bodies;
    std::vector<Collider2D> colliders;
    Build2D(entities, bodies, colliders);
    Collider2D query;
    query.shape = Collider2DShape::Circle;
    query.center = center;
    query.radius = radius;
    query.bounds = {{center.x - radius, center.y - radius}, {center.x + radius, center.y + radius}};
    for (const Collider2D& collider : colliders) {
        if (Intersects(query.bounds, collider.bounds) && TestCollision(query, collider, nullptr)) {
            AddUnique(hits, collider.entityId);
        }
    }
    return hits;
}

std::vector<int> PhysicsWorld::OverlapSphere3D(const std::vector<Entity>& entities, PhysicsVec3 center, float radius) const {
    std::vector<int> hits;
    if (radius <= 0.0f || !std::isfinite(radius)) {
        return hits;
    }
    std::vector<Body3D> bodies;
    std::vector<Collider3D> colliders;
    Build3D(entities, bodies, colliders, this);
    Collider3D query;
    query.shape = Collider3DShape::Sphere;
    query.center = center;
    query.radius = radius;
    query.bounds = {{center.x - radius, center.y - radius, center.z - radius},
                    {center.x + radius, center.y + radius, center.z + radius}};
    for (const Collider3D& collider : colliders) {
        if (Intersects(query.bounds, collider.bounds) && TestCollision(query, collider, nullptr)) {
            AddUnique(hits, collider.entityId);
        }
    }
    return hits;
}

Component MakePhysicsSettingsComponent() {
    return {"PhysicsSettings",
            {
                {"enabled", "true"},
                {"fixedDeltaSeconds", "0.0166667"},
                {"solverIterations", "6"},
                {"gravity2D", "0, -9.81"},
                {"gravity3D", "0, -9.81, 0"},
                {"broadphase", "AABB pair pruning"},
                {"collisionLayers", "Default;TransparentFX;Ignore Raycast;Water;UI;Gameplay;Environment;Player;Enemy;Physics"},
                {"disabledLayerCollisionPairs", ""},
            }};
}

Component MakeRigidbody2DComponent(const std::string& bodyType) {
    return {"Rigidbody2D",
            {
                {"bodyType", bodyType.empty() ? "Dynamic" : bodyType},
                {"mass", "1"},
                {"velocity", "0, 0"},
                {"angularVelocity", "0"},
                {"damping", "0.02"},
                {"restitution", "0.12"},
                {"friction", "0.55"},
                {"gravityScale", "1"},
                {"useGravity", "true"},
                {"lockX", "false"},
                {"lockY", "false"},
                {"lockRotation", "false"},
                {"sleepingEnabled", "false"},
                {"awake", "true"},
            }};
}

Component MakeRigidbody3DComponent(const std::string& bodyType) {
    return {"Rigidbody3D",
            {
                {"bodyType", bodyType.empty() ? "Dynamic" : bodyType},
                {"mass", "1"},
                {"velocity", "0, 0, 0"},
                {"angularVelocity", "0, 0, 0"},
                {"damping", "0.02"},
                {"restitution", "0.12"},
                {"friction", "0.55"},
                {"gravityScale", "1"},
                {"useGravity", "true"},
                {"lockX", "false"},
                {"lockY", "false"},
                {"lockZ", "false"},
                {"lockAngularX", "false"},
                {"lockAngularY", "false"},
                {"lockAngularZ", "false"},
                {"sleepingEnabled", "false"},
                {"awake", "true"},
            }};
}

Component MakeCollider2DComponent(const std::string& shape, bool trigger) {
    return {"Collider2D",
            {
                {"shape", shape.empty() ? "Box" : shape},
                {"size", "1, 1"},
                {"radius", "0.5"},
                {"offset", "0, 0"},
                {"trigger", trigger ? "true" : "false"},
            }};
}

Component MakeCollider3DComponent(const std::string& shape, bool trigger) {
    return {"Collider3D",
            {
                {"shape", shape.empty() ? "Box" : shape},
                {"size", "1, 1, 1"},
                {"radius", "0.5"},
                {"offset", "0, 0, 0"},
                {"trigger", trigger ? "true" : "false"},
            }};
}

bool RunPhysicsSelfTests(std::vector<std::string>* diagnostics) {
    bool ok = true;
    ok = TestSelf(std::fabs(Dot(PhysicsVec2{3.0f, 4.0f}, PhysicsVec2{1.0f, 0.0f}) - 3.0f) < 0.0001f,
                  "PhysicsVec2 dot failed.", diagnostics) &&
         ok;
    ok = TestSelf(std::fabs(Length(PhysicsVec3{2.0f, 3.0f, 6.0f}) - 7.0f) < 0.0001f,
                  "PhysicsVec3 length failed.", diagnostics) &&
         ok;

    Component floorCollider3D = MakeCollider3DComponent("Box");
    SetProperty(floorCollider3D, "size", "8, 1, 8");
    Component floorRb3D = MakeRigidbody3DComponent("Static");
    Component ballCollider3D = MakeCollider3DComponent("Sphere");
    SetProperty(ballCollider3D, "radius", "0.5");
    Component ballRb3D = MakeRigidbody3DComponent("Dynamic");
    SetProperty(ballRb3D, "restitution", "0");
    Component triggerCollider3D = MakeCollider3DComponent("Sphere", true);
    SetProperty(triggerCollider3D, "radius", "0.8");
    std::vector<Entity> world3D{
        MakeTestEntity(1, "Floor3D", {0.0f, -0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {floorRb3D, floorCollider3D}),
        MakeTestEntity(2, "Ball3D", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {ballRb3D, ballCollider3D}),
        MakeTestEntity(3, "Trigger3D", {0.0f, 0.65f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {MakeRigidbody3DComponent("Static"), triggerCollider3D}),
    };
    PhysicsWorld physics3D;
    bool saw3DTrigger = false;
    for (int i = 0; i < 180; ++i) {
        const PhysicsStepResult step = physics3D.StepFixed(world3D, kDefaultFixedDelta);
        saw3DTrigger = saw3DTrigger || std::any_of(step.events.begin(), step.events.end(), [](const PhysicsEvent& event) {
                           return event.trigger;
                       });
    }
    ok = TestSelf(world3D[1].position[1] >= 0.45f && world3D[1].position[1] <= 0.75f,
                  "3D body did not settle on the floor.", diagnostics) &&
         ok;
    PhysicsRayHit3D rayHit3D;
    ok = TestSelf(physics3D.Raycast3D(world3D, {0.0f, 4.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, &rayHit3D) &&
                      rayHit3D.entityId != 0,
                  "3D raycast did not hit a physics collider.", diagnostics) &&
         ok;
    ok = TestSelf(!physics3D.OverlapSphere3D(world3D, {0.0f, 0.55f, 0.0f}, 0.75f).empty(),
                  "3D sphere overlap returned no hits.", diagnostics) &&
         ok;
    ok = TestSelf(saw3DTrigger, "3D trigger overlap did not emit an event.", diagnostics) && ok;

    TerrainData terrainData;
    terrainData.resolution = 9;
    terrainData.size = {8.0f, 2.0f, 8.0f};
    terrainData.chunkSize = 4;
    NormalizeTerrainData(&terrainData);
    for (int z = 0; z < terrainData.resolution; ++z) {
        for (int x = 0; x < terrainData.resolution; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(terrainData.resolution - 1);
            terrainData.heights[static_cast<size_t>(z * terrainData.resolution + x)] =
                0.15f + 0.35f * std::max(0.0f, 1.0f - std::fabs(u - 0.5f) * 2.0f);
        }
    }
    terrainData.volume.densities.clear();
    NormalizeTerrainData(&terrainData);
    Component terrainComponent;
    SaveTerrainDataToComponent(terrainData, &terrainComponent);
    Component terrainBall = MakeCollider3DComponent("Sphere");
    SetProperty(terrainBall, "radius", "0.35");
    std::vector<Entity> terrainWorld{
        MakeTestEntity(40, "Unified Terrain", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {terrainComponent}),
        MakeTestEntity(41, "Terrain Ball", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {MakeRigidbody3DComponent("Dynamic"), terrainBall}),
    };
    PhysicsWorld terrainPhysics;
    bool sawTerrainCollision = false;
    for (int i = 0; i < 180; ++i) {
        const PhysicsStepResult step = terrainPhysics.StepFixed(terrainWorld, kDefaultFixedDelta);
        sawTerrainCollision =
            sawTerrainCollision || std::any_of(step.events.begin(), step.events.end(), [](const PhysicsEvent& event) {
                return !event.trigger &&
                       ((event.entityAId == 40 && event.entityBId == 41) ||
                        (event.entityAId == 41 && event.entityBId == 40));
            });
    }
    ok = TestSelf(sawTerrainCollision, "Terrain collider did not emit a 3D collision event.", diagnostics) && ok;
    ok = TestSelf(terrainWorld[1].position[1] >= 0.75f && terrainWorld[1].position[1] <= 1.05f,
                  "3D body did not settle on unified terrain.", diagnostics) &&
         ok;
    PhysicsRayHit3D terrainRayHit;
    ok = TestSelf(terrainPhysics.Raycast3D(terrainWorld, {2.0f, 4.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f,
                                           &terrainRayHit) &&
                      terrainRayHit.entityId == 40 && terrainRayHit.colliderType == "Terrain.VolumeSurface",
                  "3D raycast did not hit unified terrain volume surface.", diagnostics) &&
         ok;

    TerrainData holedTerrainData = terrainData;
    TerrainBrushSettings holeBrush;
    holeBrush.mode = TerrainBrushMode::Hole;
    holeBrush.falloff = TerrainFalloffCurve::Constant;
    holeBrush.radius = 1.2f;
    holeBrush.strength = 1.0f;
    holeBrush.opacity = 1.0f;
    ApplyTerrainBrush(&holedTerrainData, {0.5f, 0.5f}, holeBrush);
    Component holedTerrainComponent;
    SaveTerrainDataToComponent(holedTerrainData, &holedTerrainComponent);
    std::vector<Entity> holedTerrainWorld{
        MakeTestEntity(42, "Legacy Hole Mask Unified Terrain", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {holedTerrainComponent}),
    };
    PhysicsWorld holedTerrainPhysics;
    PhysicsRayHit3D holedTerrainRayHit;
    const bool centerHoleHit = holedTerrainPhysics.Raycast3D(
        holedTerrainWorld, {0.0f, 4.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, &holedTerrainRayHit);
    ok = TestSelf(centerHoleHit && holedTerrainRayHit.entityId == 42 &&
                      holedTerrainRayHit.colliderType == "Terrain.VolumeSurface",
                  "3D terrain raycast did not use unified volume collision over a legacy hole mask.", diagnostics) &&
         ok;
    ok = TestSelf(holedTerrainPhysics.Raycast3D(holedTerrainWorld, {3.0f, 4.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
                                                10.0f, &holedTerrainRayHit) &&
                      holedTerrainRayHit.entityId == 42 &&
                      holedTerrainRayHit.colliderType == "Terrain.VolumeSurface",
                  "3D terrain raycast did not hit unified terrain beside a legacy hole mask.", diagnostics) &&
         ok;

    TerrainData volumeTerrainData = terrainData;
    volumeTerrainData.size = {10.0f, 3.0f, 10.0f};
    EnsureTerrainVolume(&volumeTerrainData);
    volumeTerrainData.volume.resolution = {17, 17, 17};
    volumeTerrainData.volume.size = {10.0f, 10.0f, 10.0f};
    volumeTerrainData.volume.chunkSize = 4;
    volumeTerrainData.volume.densities.clear();
    EnsureTerrainVolume(&volumeTerrainData);
    CaveBrushSettings terrainTunnel;
    terrainTunnel.mode = CaveBrushMode::Tunnel;
    terrainTunnel.radius = 1.35f;
    terrainTunnel.strength = 1.0f;
    terrainTunnel.useSegment = true;
    terrainTunnel.segmentStart = {-4.0f, -2.5f, 0.0f};
    terrainTunnel.segmentEnd = {4.0f, -2.5f, 0.0f};
    ApplyTerrainVolumeBrush(&volumeTerrainData, terrainTunnel.segmentEnd, terrainTunnel);
    CaveBrushSettings terrainVolumeChamber;
    terrainVolumeChamber.mode = CaveBrushMode::Dig;
    terrainVolumeChamber.radius = 1.75f;
    terrainVolumeChamber.strength = 1.0f;
    ApplyTerrainVolumeBrush(&volumeTerrainData, {0.0f, -2.5f, 0.0f}, terrainVolumeChamber);
    Component volumeTerrainComponent;
    SaveTerrainDataToComponent(volumeTerrainData, &volumeTerrainComponent);
    Component terrainVolumeBall = MakeCollider3DComponent("Sphere");
    SetProperty(terrainVolumeBall, "radius", "0.32");
    std::vector<Entity> terrainVolumeWorld{
        MakeTestEntity(44, "Volume Terrain", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {volumeTerrainComponent}),
        MakeTestEntity(45, "Terrain Volume Ball", {2.5f, 2.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {MakeRigidbody3DComponent("Dynamic"), terrainVolumeBall}),
    };
    PhysicsWorld terrainVolumePhysics;
    const std::array<float, 3> volumeChamber =
        TerrainVolumeLocalToWorld(volumeTerrainData, terrainVolumeWorld[0], {0.0f, -2.5f, 0.0f});
    PhysicsRayHit3D terrainVolumeRayHit;
    ok = TestSelf(terrainVolumePhysics.Raycast3D(terrainVolumeWorld,
                                                 {volumeChamber[0], volumeChamber[1], volumeChamber[2]},
                                                 {0.0f, 1.0f, 0.0f}, 5.0f, &terrainVolumeRayHit) &&
                      terrainVolumeRayHit.entityId == 44 &&
                      terrainVolumeRayHit.colliderType == "Terrain.VolumeSurface",
                  "3D raycast did not hit terrain-owned volume ceiling.", diagnostics) &&
         ok;
    ok = TestSelf(terrainVolumePhysics.Raycast3D(terrainVolumeWorld,
                                                 {volumeChamber[0], volumeChamber[1], volumeChamber[2]},
                                                 {0.0f, 0.0f, 1.0f}, 5.0f, &terrainVolumeRayHit) &&
                      terrainVolumeRayHit.entityId == 44 &&
                      terrainVolumeRayHit.colliderType == "Terrain.VolumeSurface",
                  "3D raycast did not hit terrain-owned volume wall.", diagnostics) &&
         ok;
    bool sawTerrainVolumeCollision = false;
    for (int i = 0; i < 180; ++i) {
        const PhysicsStepResult step = terrainVolumePhysics.StepFixed(terrainVolumeWorld, kDefaultFixedDelta);
        sawTerrainVolumeCollision =
            sawTerrainVolumeCollision ||
            std::any_of(step.events.begin(), step.events.end(), [](const PhysicsEvent& event) {
                return !event.trigger &&
                       ((event.entityAId == 44 && event.entityBId == 45) ||
                        (event.entityAId == 45 && event.entityBId == 44));
            });
    }
    ok = TestSelf(sawTerrainVolumeCollision, "Terrain-owned volume collider did not emit a 3D collision event.",
                  diagnostics) &&
         ok;

    CaveVolumeData caveData;
    caveData.resolution = {17, 13, 17};
    caveData.size = {12.0f, 8.0f, 12.0f};
    caveData.chunkSize = 4;
    NormalizeCaveVolumeData(&caveData);
    CaveBrushSettings caveTunnel;
    caveTunnel.mode = CaveBrushMode::Tunnel;
    caveTunnel.radius = 1.8f;
    caveTunnel.strength = 1.3f;
    caveTunnel.useSegment = true;
    caveTunnel.segmentStart = {-5.5f, 0.0f, 0.0f};
    caveTunnel.segmentEnd = {5.5f, 0.4f, 0.0f};
    ApplyCaveBrush(&caveData, {0.0f, 0.0f, 0.0f}, caveTunnel);
    Component caveComponent;
    SaveCaveVolumeDataToComponent(caveData, &caveComponent);
    Component caveBall = MakeCollider3DComponent("Sphere");
    SetProperty(caveBall, "radius", "0.35");
    std::vector<Entity> caveWorld{
        MakeTestEntity(50, "Cave Volume", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {caveComponent}),
        MakeTestEntity(51, "Cave Ball", {0.0f, 1.1f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {MakeRigidbody3DComponent("Dynamic"), caveBall}),
    };
    PhysicsWorld cavePhysics;
    bool sawCaveCollision = false;
    for (int i = 0; i < 180; ++i) {
        const PhysicsStepResult step = cavePhysics.StepFixed(caveWorld, kDefaultFixedDelta);
        sawCaveCollision =
            sawCaveCollision || std::any_of(step.events.begin(), step.events.end(), [](const PhysicsEvent& event) {
                return !event.trigger &&
                       ((event.entityAId == 50 && event.entityBId == 51) ||
                        (event.entityAId == 51 && event.entityBId == 50));
            });
    }
    ok = TestSelf(sawCaveCollision, "Cave volume collider did not emit a 3D collision event.", diagnostics) && ok;
    PhysicsRayHit3D caveRayHit;
    ok = TestSelf(cavePhysics.Raycast3D(caveWorld, {1.2f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 5.0f,
                                        &caveRayHit) &&
                      caveRayHit.entityId == 50 && caveRayHit.colliderType == "CaveVolume.Surface",
                  "3D raycast did not hit cave volume surface.", diagnostics) &&
         ok;

    Component disabledRb3D = MakeRigidbody3DComponent("Dynamic");
    SetProperty(disabledRb3D, "enabled", "false");
    Component disabledCollider3D = MakeCollider3DComponent("Box");
    SetProperty(disabledCollider3D, "enabled", "false");
    std::vector<Entity> disabledWorld3D{
        MakeTestEntity(20, "Inactive3D", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {MakeRigidbody3DComponent("Dynamic"), MakeCollider3DComponent("Box")}),
        MakeTestEntity(21, "DisabledBody3D", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {disabledRb3D}),
        MakeTestEntity(22, "DisabledCollider3D", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {disabledCollider3D}),
        MakeTestEntity(23, "InactiveParent3D", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {}),
        MakeTestEntity(24, "ChildOfInactiveParent3D", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {MakeRigidbody3DComponent("Dynamic"), MakeCollider3DComponent("Box")}),
    };
    disabledWorld3D[0].activeSelf = false;
    disabledWorld3D[3].activeSelf = false;
    disabledWorld3D[4].parentId = disabledWorld3D[3].id;
    PhysicsWorld disabledPhysics3D;
    const PhysicsStepResult disabledStep3D = disabledPhysics3D.StepFixed(disabledWorld3D, kDefaultFixedDelta);
    ok = TestSelf(disabledStep3D.body3DCount == 0 && disabledStep3D.collider3DCount == 0,
                  "Inactive or disabled 3D physics components still participated.", diagnostics) &&
         ok;

    Component floorCollider2D = MakeCollider2DComponent("Box");
    SetProperty(floorCollider2D, "size", "8, 1");
    Component floorRb2D = MakeRigidbody2DComponent("Static");
    Component discCollider2D = MakeCollider2DComponent("Circle");
    SetProperty(discCollider2D, "radius", "0.5");
    Component discRb2D = MakeRigidbody2DComponent("Dynamic");
    SetProperty(discRb2D, "restitution", "0");
    Component triggerCollider2D = MakeCollider2DComponent("Circle", true);
    SetProperty(triggerCollider2D, "radius", "0.8");
    std::vector<Entity> world2D{
        MakeTestEntity(10, "Floor2D", {0.0f, -0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {floorRb2D, floorCollider2D}),
        MakeTestEntity(11, "Disc2D", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {discRb2D, discCollider2D}),
        MakeTestEntity(12, "Trigger2D", {0.0f, 0.65f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {MakeRigidbody2DComponent("Static"), triggerCollider2D}),
    };
    PhysicsWorld physics2D;
    bool sawTrigger = false;
    for (int i = 0; i < 180; ++i) {
        const PhysicsStepResult step = physics2D.StepFixed(world2D, kDefaultFixedDelta);
        sawTrigger = sawTrigger || std::any_of(step.events.begin(), step.events.end(), [](const PhysicsEvent& event) {
                         return event.trigger;
                     });
    }
    ok = TestSelf(world2D[1].position[1] >= 0.45f && world2D[1].position[1] <= 0.75f,
                  "2D body did not settle on the floor.", diagnostics) &&
         ok;
    ok = TestSelf(sawTrigger, "2D trigger overlap did not emit an event.", diagnostics) && ok;
    PhysicsRayHit2D rayHit2D;
    ok = TestSelf(physics2D.Raycast2D(world2D, {0.0f, 4.0f}, {0.0f, -1.0f}, 10.0f, &rayHit2D) &&
                      rayHit2D.entityId != 0,
                  "2D raycast did not hit a physics collider.", diagnostics) &&
         ok;
    ok = TestSelf(!physics2D.OverlapCircle2D(world2D, {0.0f, 0.55f}, 0.75f).empty(),
                  "2D circle overlap returned no hits.", diagnostics) &&
         ok;

    Component disabledRb2D = MakeRigidbody2DComponent("Dynamic");
    SetProperty(disabledRb2D, "enabled", "false");
    Component disabledCollider2D = MakeCollider2DComponent("Box");
    SetProperty(disabledCollider2D, "enabled", "false");
    std::vector<Entity> disabledWorld2D{
        MakeTestEntity(30, "Inactive2D", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {MakeRigidbody2DComponent("Dynamic"), MakeCollider2DComponent("Box")}),
        MakeTestEntity(31, "DisabledBody2D", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {disabledRb2D}),
        MakeTestEntity(32, "DisabledCollider2D", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {disabledCollider2D}),
        MakeTestEntity(33, "InactiveParent2D", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {}),
        MakeTestEntity(34, "ChildOfInactiveParent2D", {0.0f, 3.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
                       {MakeRigidbody2DComponent("Dynamic"), MakeCollider2DComponent("Box")}),
    };
    disabledWorld2D[0].activeSelf = false;
    disabledWorld2D[3].activeSelf = false;
    disabledWorld2D[4].parentId = disabledWorld2D[3].id;
    PhysicsWorld disabledPhysics2D;
    const PhysicsStepResult disabledStep2D = disabledPhysics2D.StepFixed(disabledWorld2D, kDefaultFixedDelta);
    ok = TestSelf(disabledStep2D.body2DCount == 0 && disabledStep2D.collider2DCount == 0,
                  "Inactive or disabled 2D physics components still participated.", diagnostics) &&
         ok;

    if (ok && diagnostics != nullptr) {
        diagnostics->push_back("physics.selftest passed.");
    }
    return ok;
}

} // namespace aine

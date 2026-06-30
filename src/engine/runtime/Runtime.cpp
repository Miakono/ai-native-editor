#include "engine/runtime/Runtime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <utility>

namespace aine {
namespace {

constexpr float kRadiansToDegrees = 57.2957795f;
constexpr size_t kMaxRuntimeLogEntries = 128;

std::string ComponentStringProperty(const Component& component, const std::string& propertyName, std::string fallback = {}) {
    for (const ComponentProperty& property : component.properties) {
        if (property.name == propertyName) {
            return property.value;
        }
    }
    return fallback;
}

float ComponentFloatProperty(const Component& component, const std::string& propertyName, float fallback) {
    for (const ComponentProperty& property : component.properties) {
        if (property.name != propertyName) {
            continue;
        }

        std::istringstream stream(property.value);
        float parsed = fallback;
        if (stream >> parsed && std::isfinite(parsed)) {
            return parsed;
        }
        return fallback;
    }
    return fallback;
}

int ComponentIntProperty(const Component& component, const std::string& propertyName, int fallback) {
    for (const ComponentProperty& property : component.properties) {
        if (property.name != propertyName) {
            continue;
        }

        std::istringstream stream(property.value);
        int parsed = fallback;
        if (stream >> parsed) {
            return parsed;
        }
        return fallback;
    }
    return fallback;
}

bool ComponentBoolProperty(const Component& component, const std::string& propertyName, bool fallback) {
    for (const ComponentProperty& property : component.properties) {
        if (property.name != propertyName) {
            continue;
        }

        std::string value = property.value;
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
    return fallback;
}

bool ComponentEnabled(const Component& component) {
    return ComponentBoolProperty(component, "enabled", true);
}

int CountComponents(const std::vector<Entity>& entities) {
    int count = 0;
    for (const Entity& entity : entities) {
        count += static_cast<int>(entity.components.size());
    }
    return count;
}

int CountEnabledScriptComponents(const std::vector<Entity>& entities) {
    int count = 0;
    for (const Entity& entity : entities) {
        for (const Component& component : entity.components) {
            if (ComponentEnabled(component) && !ComponentStringProperty(component, "scriptPath").empty()) {
                ++count;
            }
        }
    }
    return count;
}

int CountTriggerEvents(const std::vector<PhysicsEvent>& events) {
    return static_cast<int>(std::count_if(events.begin(), events.end(), [](const PhysicsEvent& event) {
        return event.trigger;
    }));
}

bool HasEnabledComponentLocal(const Entity& entity, const std::string& type) {
    if (!entity.activeSelf) {
        return false;
    }
    return std::any_of(entity.components.begin(), entity.components.end(), [&type](const Component& component) {
        return component.type == type && ComponentEnabled(component);
    });
}

bool HasEnabledPhysicsCollider(const Entity& entity) {
    return HasEnabledComponentLocal(entity, "Collider2D") || HasEnabledComponentLocal(entity, "Collider3D");
}

Component* FindComponent(Entity& entity, const std::string& type) {
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [&type](const Component& component) {
        return component.type == type;
    });
    return it == entity.components.end() ? nullptr : &(*it);
}

const Component* FindComponent(const Entity& entity, const std::string& type) {
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [&type](const Component& component) {
        return component.type == type;
    });
    return it == entity.components.end() ? nullptr : &(*it);
}

Component* FindEnabledComponent(Entity& entity, const std::string& type) {
    if (!entity.activeSelf) {
        return nullptr;
    }
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [&type](const Component& component) {
        return component.type == type && ComponentEnabled(component);
    });
    return it == entity.components.end() ? nullptr : &(*it);
}

void SetComponentProperty(Component& component, const std::string& propertyName, std::string value) {
    for (ComponentProperty& property : component.properties) {
        if (property.name == propertyName) {
            property.value = std::move(value);
            return;
        }
    }
    component.properties.push_back({propertyName, std::move(value)});
}

std::array<float, 3> ComponentVec3Property(const Component& component, const std::string& propertyName,
                                           std::array<float, 3> fallback) {
    const std::string value = ComponentStringProperty(component, propertyName);
    if (value.empty()) {
        return fallback;
    }

    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream stream(normalized);
    std::array<float, 3> parsed = fallback;
    if (stream >> parsed[0] >> parsed[1] >> parsed[2] && std::isfinite(parsed[0]) && std::isfinite(parsed[1]) &&
        std::isfinite(parsed[2])) {
        return parsed;
    }
    return fallback;
}

const Entity* FindEntityById(const std::vector<Entity>& entities, int id) {
    auto it = std::find_if(entities.begin(), entities.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    return it == entities.end() ? nullptr : &(*it);
}

Entity* FindEntityByName(std::vector<Entity>& entities, const std::string& name) {
    auto it = std::find_if(entities.rbegin(), entities.rend(), [&name](const Entity& entity) {
        return entity.name == name;
    });
    return it == entities.rend() ? nullptr : &(*it);
}

bool IsEntityActiveInHierarchy(const std::vector<Entity>& entities, int id) {
    const Entity* entity = FindEntityById(entities, id);
    if (entity == nullptr) {
        return false;
    }

    std::vector<int> visited;
    const Entity* current = entity;
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

float SpinRuntimeSpeed(const Entity& entity) {
    for (const Component& component : entity.components) {
        if (component.type == "SpinRuntime" && ComponentEnabled(component)) {
            return ComponentFloatProperty(component, "speedYDegreesPerSecond", 90.0f);
        }
    }
    return 0.0f;
}

float CharacterControllerSpeed(const Entity& entity) {
    for (const Component& component : entity.components) {
        if (component.type == "CharacterControllerRuntime" && ComponentEnabled(component)) {
            return ComponentFloatProperty(component, "speedUnitsPerSecond", 3.25f);
        }
    }
    return 0.0f;
}

float CharacterControllerSprintMultiplier(const Entity& entity) {
    for (const Component& component : entity.components) {
        if (component.type == "CharacterControllerRuntime" && ComponentEnabled(component)) {
            return std::max(1.0f, ComponentFloatProperty(component, "sprintMultiplier", 1.75f));
        }
    }
    return 1.0f;
}

float NormalizeDegrees(float degrees) {
    if (!std::isfinite(degrees)) {
        return 0.0f;
    }
    degrees = std::fmod(degrees, 360.0f);
    if (degrees < 0.0f) {
        degrees += 360.0f;
    }
    return degrees;
}

float DistanceXZ(const Entity& left, const Entity& right) {
    const float dx = left.position[0] - right.position[0];
    const float dz = left.position[2] - right.position[2];
    return std::sqrt(dx * dx + dz * dz);
}

bool HasTriggerEventForPair(const std::vector<PhysicsEvent>& events, int entityAId, int entityBId) {
    return std::any_of(events.begin(), events.end(), [entityAId, entityBId](const PhysicsEvent& event) {
        if (!event.trigger) {
            return false;
        }
        return (event.entityAId == entityAId && event.entityBId == entityBId) ||
               (event.entityAId == entityBId && event.entityBId == entityAId);
    });
}

std::string RuntimePhysicsEventKey(const PhysicsEvent& event) {
    const int firstId = std::min(event.entityAId, event.entityBId);
    const int secondId = std::max(event.entityAId, event.entityBId);
    return std::string(event.trigger ? "trigger:" : "collision:") + std::to_string(firstId) + ":" +
           std::to_string(secondId);
}

bool RuntimeTriggerOverlaps(const Entity& player, const Entity& target, float radius,
                            const std::vector<PhysicsEvent>& physicsEvents) {
    if (HasEnabledPhysicsCollider(player) && HasEnabledPhysicsCollider(target) &&
        HasTriggerEventForPair(physicsEvents, player.id, target.id)) {
        return true;
    }
    return DistanceXZ(player, target) <= radius;
}

bool EvaluateUIStateBindingKey(const std::string& stateKey, EditorPlayState playState, bool playFailed,
                               bool playGoalReached, int playScore) {
    if (stateKey == "playFailed") {
        return playFailed;
    }
    if (stateKey == "playGoalReached") {
        return playGoalReached;
    }
    if (stateKey == "playing") {
        return playState == EditorPlayState::Playing;
    }
    if (stateKey == "paused") {
        return playState == EditorPlayState::Paused;
    }
    if (stateKey == "editMode") {
        return playState == EditorPlayState::EditMode;
    }
    if (stateKey == "scorePositive") {
        return playScore > 0;
    }
    return false;
}

void EmitEditorLog(const RuntimeHostCallbacks& callbacks, LogLevel level, const std::string& message) {
    if (callbacks.editorLog) {
        callbacks.editorLog(level, message);
    }
}

void EmitActivity(const RuntimeHostCallbacks& callbacks, const std::string& label, const std::string& status,
                  const std::string& detail) {
    if (callbacks.activity) {
        callbacks.activity(label, status, detail);
    }
}

void SyncTransform(const RuntimeHostCallbacks& callbacks, Entity& entity) {
    if (callbacks.syncTransform) {
        callbacks.syncTransform(entity);
    }
}

const char* RuntimeLogLevelLabel(LogLevel level) {
    switch (level) {
    case LogLevel::Error:
        return "Error";
    case LogLevel::Warning:
        return "Warning";
    case LogLevel::Info:
    default:
        return "Info";
    }
}

std::string ToLowerCopy(std::string value) {
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

std::vector<std::string> SplitNameList(std::string value) {
    std::replace(value.begin(), value.end(), ',', ';');
    std::replace(value.begin(), value.end(), '|', ';');
    std::vector<std::string> names;
    std::istringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ';')) {
        token = TrimCopy(std::move(token));
        if (!token.empty() && std::find(names.begin(), names.end(), token) == names.end()) {
            names.push_back(std::move(token));
        }
    }
    return names;
}

std::vector<std::string> SplitSpriteFrameList(std::string value) {
    std::replace(value.begin(), value.end(), '|', ';');
    std::vector<std::string> frames;
    std::istringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ';')) {
        token = TrimCopy(std::move(token));
        if (!token.empty()) {
            frames.push_back(std::move(token));
        }
    }
    return frames;
}

bool LooksLikeSpriteRectToken(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    return value.find(',') != std::string::npos &&
           value.find_first_not_of("0123456789.,;+- \t") == std::string::npos;
}

std::array<float, 2> ComponentVec2Property(const Component& component, const std::string& propertyName,
                                           std::array<float, 2> fallback) {
    const std::string value = ComponentStringProperty(component, propertyName);
    if (value.empty()) {
        return fallback;
    }

    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream stream(normalized);
    std::array<float, 2> parsed = fallback;
    if (stream >> parsed[0] >> parsed[1] && std::isfinite(parsed[0]) && std::isfinite(parsed[1])) {
        return parsed;
    }
    return fallback;
}

std::array<float, 3> Vec3Add(std::array<float, 3> left, std::array<float, 3> right) {
    return {left[0] + right[0], left[1] + right[1], left[2] + right[2]};
}

std::array<float, 3> Vec3Sub(std::array<float, 3> left, std::array<float, 3> right) {
    return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

std::array<float, 3> Vec3Mul(std::array<float, 3> value, float scalar) {
    return {value[0] * scalar, value[1] * scalar, value[2] * scalar};
}

float Vec3Dot(std::array<float, 3> left, std::array<float, 3> right) {
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

std::array<float, 3> Vec3Cross(std::array<float, 3> left, std::array<float, 3> right) {
    return {left[1] * right[2] - left[2] * right[1], left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}

float Vec3Length(std::array<float, 3> value) {
    return std::sqrt(Vec3Dot(value, value));
}

std::array<float, 3> Vec3Normalize(std::array<float, 3> value, std::array<float, 3> fallback = {0.0f, 0.0f, -1.0f}) {
    const float length = Vec3Length(value);
    if (length <= 0.00001f || !std::isfinite(length)) {
        return fallback;
    }
    return Vec3Mul(value, 1.0f / length);
}

std::array<float, 3> Vec3Clamp(std::array<float, 3> value, std::array<float, 3> minValue,
                               std::array<float, 3> maxValue) {
    return {std::clamp(value[0], std::min(minValue[0], maxValue[0]), std::max(minValue[0], maxValue[0])),
            std::clamp(value[1], std::min(minValue[1], maxValue[1]), std::max(minValue[1], maxValue[1])),
            std::clamp(value[2], std::min(minValue[2], maxValue[2]), std::max(minValue[2], maxValue[2]))};
}

std::array<float, 3> ForwardFromEuler(std::array<float, 3> rotation) {
    constexpr float kDegreesToRadians = 3.1415926535f / 180.0f;
    const float yaw = rotation[1] * kDegreesToRadians;
    const float pitch = std::clamp(rotation[0], -89.0f, 89.0f) * kDegreesToRadians;
    return Vec3Normalize({std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)});
}

std::array<float, 3> RightFromForward(std::array<float, 3> forward) {
    return Vec3Normalize(Vec3Cross(forward, {0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f});
}

std::array<float, 3> UpFromForward(std::array<float, 3> forward) {
    const std::array<float, 3> right = RightFromForward(forward);
    return Vec3Normalize(Vec3Cross(right, forward), {0.0f, 1.0f, 0.0f});
}

std::array<float, 3> RotationLookingAt(std::array<float, 3> from, std::array<float, 3> to,
                                       std::array<float, 3> fallback) {
    const std::array<float, 3> direction = Vec3Normalize(Vec3Sub(to, from), ForwardFromEuler(fallback));
    const float pitch = std::asin(std::clamp(direction[1], -1.0f, 1.0f)) * kRadiansToDegrees;
    const float yaw = std::atan2(direction[0], -direction[2]) * kRadiansToDegrees;
    return {pitch, yaw, 0.0f};
}

float SmoothAlpha(float deltaSeconds, float smoothTime) {
    if (smoothTime <= 0.0001f || !std::isfinite(smoothTime)) {
        return 1.0f;
    }
    return std::clamp(1.0f - std::exp(-std::max(0.0f, deltaSeconds) / smoothTime), 0.0f, 1.0f);
}

float Lerp(float from, float to, float alpha) {
    return from + (to - from) * alpha;
}

std::array<float, 3> Vec3Lerp(std::array<float, 3> from, std::array<float, 3> to, float alpha) {
    return {Lerp(from[0], to[0], alpha), Lerp(from[1], to[1], alpha), Lerp(from[2], to[2], alpha)};
}

float LerpAngle(float from, float to, float alpha) {
    float delta = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
    return from + delta * alpha;
}

std::array<float, 3> RotationLerp(std::array<float, 3> from, std::array<float, 3> to, float alpha) {
    return {LerpAngle(from[0], to[0], alpha), LerpAngle(from[1], to[1], alpha), LerpAngle(from[2], to[2], alpha)};
}

float RuntimeActionValue(const RuntimeInputState& input, const std::vector<std::string>& names) {
    for (const RuntimeInputAction& action : input.actions) {
        if (!action.active) {
            continue;
        }
        const std::string actionName = ToLowerCopy(action.name);
        const std::string binding = ToLowerCopy(action.binding);
        for (const std::string& name : names) {
            const std::string needle = ToLowerCopy(name);
            if (actionName == needle || binding == needle) {
                return std::isfinite(action.value) ? std::clamp(action.value, -1.0f, 1.0f) : 0.0f;
            }
        }
    }
    return 0.0f;
}

struct CameraTargetInfo {
    bool found = false;
    Entity* primary = nullptr;
    std::array<float, 3> center{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
};

CameraTargetInfo ResolveCameraTarget(std::vector<Entity>& entities, const Component& follow, const std::string& targetOverride) {
    CameraTargetInfo info;
    const std::string targetName =
        targetOverride.empty() ? ComponentStringProperty(follow, "targetName", "Player") : targetOverride;
    std::vector<Entity*> targets;
    if (!targetName.empty()) {
        if (Entity* target = FindEntityByName(entities, targetName);
            target != nullptr && IsEntityActiveInHierarchy(entities, target->id)) {
            targets.push_back(target);
            info.primary = target;
        }
    }
    for (const std::string& name : SplitNameList(ComponentStringProperty(follow, "targetGroupNames"))) {
        if (Entity* target = FindEntityByName(entities, name);
            target != nullptr && IsEntityActiveInHierarchy(entities, target->id) &&
            std::find(targets.begin(), targets.end(), target) == targets.end()) {
            targets.push_back(target);
            if (info.primary == nullptr) {
                info.primary = target;
            }
        }
    }

    if (targets.empty()) {
        return info;
    }

    for (const Entity* target : targets) {
        info.center = Vec3Add(info.center, target->position);
    }
    info.center = Vec3Mul(info.center, 1.0f / static_cast<float>(targets.size()));
    for (const Entity* target : targets) {
        info.radius = std::max(info.radius, Vec3Length(Vec3Sub(target->position, info.center)));
    }
    info.found = true;
    return info;
}

struct CameraShakeProfile {
    float amplitude = 0.08f;
    float duration = 0.22f;
    float frequency = 22.0f;
};

struct CameraRailKeyframe {
    float time = 0.0f;
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
};

bool EvaluateCameraRail(const std::string& railKeyframes, float elapsedSeconds, bool loop,
                        std::array<float, 3>* outPosition, std::array<float, 3>* outRotation) {
    if (outPosition == nullptr || outRotation == nullptr || railKeyframes.empty()) {
        return false;
    }

    std::vector<CameraRailKeyframe> keys;
    std::istringstream stream(railKeyframes);
    std::string token;
    while (std::getline(stream, token, ';')) {
        std::replace(token.begin(), token.end(), ',', ' ');
        std::istringstream keyStream(token);
        CameraRailKeyframe key;
        if (keyStream >> key.position[0] >> key.position[1] >> key.position[2] >> key.rotation[0] >> key.rotation[1] >>
            key.rotation[2] >> key.time) {
            if (std::isfinite(key.time)) {
                keys.push_back(key);
            }
        }
    }
    if (keys.empty()) {
        return false;
    }
    std::sort(keys.begin(), keys.end(), [](const CameraRailKeyframe& left, const CameraRailKeyframe& right) {
        return left.time < right.time;
    });
    if (keys.size() == 1) {
        *outPosition = keys.front().position;
        *outRotation = keys.front().rotation;
        return true;
    }

    float t = std::max(0.0f, elapsedSeconds);
    const float endTime = std::max(keys.back().time, 0.001f);
    if (loop) {
        t = std::fmod(t, endTime);
    }
    if (t <= keys.front().time) {
        *outPosition = keys.front().position;
        *outRotation = keys.front().rotation;
        return true;
    }
    if (t >= keys.back().time) {
        *outPosition = keys.back().position;
        *outRotation = keys.back().rotation;
        return true;
    }

    for (size_t index = 1; index < keys.size(); ++index) {
        if (t > keys[index].time) {
            continue;
        }
        const CameraRailKeyframe& previous = keys[index - 1];
        const CameraRailKeyframe& next = keys[index];
        const float duration = std::max(0.001f, next.time - previous.time);
        const float alpha = std::clamp((t - previous.time) / duration, 0.0f, 1.0f);
        *outPosition = Vec3Lerp(previous.position, next.position, alpha);
        *outRotation = RotationLerp(previous.rotation, next.rotation, alpha);
        return true;
    }
    return false;
}

CameraShakeProfile ShakeProfileByName(const std::string& profileName) {
    const std::string normalized = ToLowerCopy(TrimCopy(profileName));
    if (normalized == "small" || normalized == "light") {
        return {0.045f, 0.16f, 24.0f};
    }
    if (normalized == "large" || normalized == "heavy") {
        return {0.22f, 0.42f, 20.0f};
    }
    if (normalized == "damage" || normalized == "hit") {
        return {0.16f, 0.24f, 28.0f};
    }
    if (normalized == "recoil" || normalized == "kick") {
        return {0.055f, 0.14f, 34.0f};
    }
    return {0.09f, 0.24f, 24.0f};
}

bool CameraRequestMatches(const CameraEffectRequest& request, const Entity& camera) {
    return request.cameraName.empty() || request.cameraName == camera.name;
}

const Entity* FindCameraEntityByName(const std::vector<Entity>& entities, const std::string& cameraName) {
    for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
        if ((cameraName.empty() || it->name == cameraName) && IsEntityActiveInHierarchy(entities, it->id) &&
            HasEnabledComponentLocal(*it, "Camera")) {
            return &(*it);
        }
    }
    return nullptr;
}

} // namespace

const char* PlayStateLabel(EditorPlayState state) {
    switch (state) {
    case EditorPlayState::Playing:
        return "Playing";
    case EditorPlayState::Paused:
        return "Paused";
    case EditorPlayState::EditMode:
    default:
        return "Edit Mode";
    }
}

bool EngineRuntime::Begin(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks,
                          const ScriptRuntimeHostConfig& scriptHostConfig) {
    if (playState_ == EditorPlayState::Playing) {
        return false;
    }
    if (playState_ == EditorPlayState::Paused) {
        return Resume(entities, callbacks);
    }

    ResetForPlay(entities);
    physicsWorld_.Reset();
    ScriptRuntimeHostResult scriptBegin = scriptRuntimeHost_.Begin(entities, scriptHostConfig);
    if (!ConsumeScriptHostResult(scriptBegin, callbacks, true)) {
        runtimeStatus_ = "Script Host Error";
        scriptRuntimeHost_.Reset();
        return false;
    }
    playState_ = EditorPlayState::Playing;
    RunUIStateBindingRuntimeSystem(entities);
    AddRuntimeLog(LogLevel::Info, "play.start", "Started", "Runtime scene snapshot captured.");
    EmitEditorLog(callbacks, LogLevel::Info, "Play Mode started");
    EmitActivity(callbacks, "play.mode", "Playing", "Runtime scene snapshot captured.");
    return true;
}

bool EngineRuntime::Pause(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks) {
    if (playState_ != EditorPlayState::Playing) {
        return false;
    }

    playState_ = EditorPlayState::Paused;
    RunUIStateBindingRuntimeSystem(entities);
    AddRuntimeLog(LogLevel::Info, "play.pause", "Paused", "Runtime updates frozen.");
    EmitEditorLog(callbacks, LogLevel::Info, "Play Mode paused");
    EmitActivity(callbacks, "play.mode", "Paused", "Runtime updates frozen.");
    return true;
}

bool EngineRuntime::Resume(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks) {
    if (playState_ != EditorPlayState::Paused) {
        return false;
    }

    playState_ = EditorPlayState::Playing;
    RunUIStateBindingRuntimeSystem(entities);
    AddRuntimeLog(LogLevel::Info, "play.resume", "Playing", "Runtime updates resumed.");
    EmitEditorLog(callbacks, LogLevel::Info, "Play Mode resumed");
    EmitActivity(callbacks, "play.mode", "Playing", "Runtime updates resumed.");
    return true;
}

bool EngineRuntime::Stop(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks) {
    if (playState_ == EditorPlayState::EditMode) {
        return false;
    }

    ConsumeScriptHostResult(scriptRuntimeHost_.Destroy(entities), callbacks, false);
    AddRuntimeLog(LogLevel::Info, "play.stop", "Stopped", "Edit-mode scene snapshot restored.");
    playElapsedSeconds_ = 0.0f;
    playScore_ = 0;
    playGoalReached_ = false;
    playFailed_ = false;
    controlledRuntimeEntityId_ = 0;
    runtimeStatus_ = "Edit Mode";
    lastRuntimeSystemSummary_.clear();
    characterControllerInput_ = {};
    runtimeInput_ = {};
    physicsWorld_.Reset();
    scriptRuntimeHost_.Reset();
    runtimeLogs_.clear();
    runtimePhysicsEventKeys_.clear();
    cameraStates_.clear();
    pendingCameraEffects_.clear();
    playState_ = EditorPlayState::EditMode;
    EmitEditorLog(callbacks, LogLevel::Info, "Play Mode stopped");
    EmitActivity(callbacks, "play.mode", "Edit Mode", "Edit-mode scene snapshot restored.");
    return true;
}

void EngineRuntime::Update(std::vector<Entity>& entities, float deltaSeconds, const RuntimeHostCallbacks& callbacks) {
    if (playState_ != EditorPlayState::Playing || deltaSeconds <= 0.0f || !std::isfinite(deltaSeconds)) {
        return;
    }

    PerformanceScope runtimeFrame(callbacks.profiler, PerformanceDomain::Runtime, "Runtime.Frame");
    const float simulationDelta = std::min(deltaSeconds, 0.25f);
    playElapsedSeconds_ += simulationDelta;

    if (callbacks.profiler != nullptr && callbacks.profiler->IsCapturing()) {
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.Entities",
                                          static_cast<double>(entities.size()));
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.Components",
                                          static_cast<double>(CountComponents(entities)));
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.ActiveScripts",
                                          static_cast<double>(CountEnabledScriptComponents(entities)));
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.LogEntries",
                                          static_cast<double>(runtimeLogs_.size()));
    }

    {
        PerformanceScope sceneSystems(callbacks.profiler, PerformanceDomain::Runtime, "Runtime.SceneSystems");
        RunAudioRuntimeSystem(entities, callbacks);
        RunAnimationRuntimeSystem(entities, simulationDelta);
        RunSpriteAnimationRuntimeSystem(entities, simulationDelta);
        RunNavigationRuntimeSystem(entities, simulationDelta, callbacks);
        RunSpinRuntimeSystem(entities, simulationDelta, callbacks);
        RunCharacterControllerSystem(entities, simulationDelta, callbacks);
    }

    PhysicsStepResult physics;
    {
        PerformanceScope physicsScope(callbacks.profiler, PerformanceDomain::Runtime, "Runtime.Physics");
        physics = physicsWorld_.AccumulateAndStep(entities, simulationDelta);
    }
    if (callbacks.profiler != nullptr && callbacks.profiler->IsCapturing()) {
        const int triggerEvents = CountTriggerEvents(physics.events);
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.PhysicsSteps",
                                          static_cast<double>(physics.fixedSteps));
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.PhysicsBodies",
                                          static_cast<double>(physics.body2DCount + physics.body3DCount));
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.PhysicsColliders",
                                          static_cast<double>(physics.collider2DCount + physics.collider3DCount));
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.PhysicsEvents",
                                          static_cast<double>(physics.events.size()));
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.TriggerEvents",
                                          static_cast<double>(triggerEvents));
        callbacks.profiler->RecordCounter(PerformanceDomain::Runtime, "Runtime.CollisionEvents",
                                          static_cast<double>(static_cast<int>(physics.events.size()) - triggerEvents));
    }
    for (int entityId : physics.changedEntityIds) {
        auto it = std::find_if(entities.begin(), entities.end(), [entityId](const Entity& entity) {
            return entity.id == entityId;
        });
        if (it != entities.end()) {
            SyncTransform(callbacks, *it);
        }
    }
    for (const PhysicsEvent& event : physics.events) {
        const std::string eventKey = RuntimePhysicsEventKey(event);
        if (std::find(runtimePhysicsEventKeys_.begin(), runtimePhysicsEventKeys_.end(), eventKey) !=
            runtimePhysicsEventKeys_.end()) {
            continue;
        }
        runtimePhysicsEventKeys_.push_back(eventKey);
        if (event.trigger) {
            AddRuntimeLog(LogLevel::Info, "physics.trigger", "Trigger", event.entityAName + " <-> " + event.entityBName);
            EmitActivity(callbacks, "physics.trigger", "Trigger", event.entityAName + " <-> " + event.entityBName);
        } else {
            AddRuntimeLog(LogLevel::Info, "physics.collision", "Collision",
                          event.entityAName + " <-> " + event.entityBName);
        }
    }
    {
        PerformanceScope sceneSystems(callbacks.profiler, PerformanceDomain::Runtime, "Runtime.SceneSystems");
        RunTriggerRuntimeSystem(entities, callbacks);
        RunFallDeathRuntimeSystem(entities, callbacks);
    }
    {
        PerformanceScope scripting(callbacks.profiler, PerformanceDomain::Runtime, "Runtime.Scripting");
        ConsumeScriptHostResult(scriptRuntimeHost_.Update(entities, simulationDelta), callbacks, false);
    }
    {
        PerformanceScope sceneSystems(callbacks.profiler, PerformanceDomain::Runtime, "Runtime.SceneSystems");
        RunUIStateBindingRuntimeSystem(entities);
        RunUIFocusRuntimeSystem(entities);
        RunUIAnimationRuntimeSystem(entities, simulationDelta);
        RunNetworkingRuntimeSystem(entities, callbacks);
        RunCameraFollowRuntimeSystem(entities, simulationDelta, callbacks);
    }

    lastRuntimeSystemSummary_ =
        "InputSystem, AudioRuntimeSystem, AnimationRuntimeSystem, SpriteAnimationRuntimeSystem, NavigationRuntimeSystem, SpinRuntimeSystem, CharacterControllerSystem, PhysicsSystem, TriggerRuntimeSystem, FallDeathRuntimeSystem, ScriptRuntimeHost, UIStateBindingRuntimeSystem, UIFocusRuntimeSystem, UIAnimationRuntimeSystem, NetworkingRuntimeSystem, CameraFollowRuntimeSystem";
}

bool EngineRuntime::StepPhysicsOnce(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks) {
    if (playState_ == EditorPlayState::EditMode) {
        return false;
    }

    const float fixedDeltaSeconds = 1.0f / 60.0f;
    const PhysicsStepResult physics = physicsWorld_.StepFixed(entities, fixedDeltaSeconds);
    for (int entityId : physics.changedEntityIds) {
        auto it = std::find_if(entities.begin(), entities.end(), [entityId](const Entity& entity) {
            return entity.id == entityId;
        });
        if (it != entities.end()) {
            SyncTransform(callbacks, *it);
        }
    }
    playElapsedSeconds_ += fixedDeltaSeconds;
    EmitActivity(callbacks, "physics.step", "Complete",
                 std::to_string(physics.body2DCount + physics.body3DCount) + " bodies, " +
                     std::to_string(physics.collider2DCount + physics.collider3DCount) + " colliders");
    return true;
}

void EngineRuntime::ResetForPlay(std::vector<Entity>& entities) {
    playElapsedSeconds_ = 0.0f;
    playScore_ = 0;
    playGoalReached_ = false;
    playFailed_ = false;
    controlledRuntimeEntityId_ = 0;
    runtimeStatus_ = "Running";
    lastRuntimeSystemSummary_.clear();
    characterControllerInput_ = {};
    runtimeInput_ = {};
    runtimeLogs_.clear();
    runtimePhysicsEventKeys_.clear();
    cameraStates_.clear();
    pendingCameraEffects_.clear();
    physicsWorld_.Reset();
    for (Entity& entity : entities) {
        if (Component* collectible = FindComponent(entity, "CollectibleRuntime")) {
            SetComponentProperty(*collectible, "collected", "false");
        }
        if (Component* goal = FindComponent(entity, "GoalRuntime")) {
            SetComponentProperty(*goal, "completed", "false");
        }
        if (Component* hazard = FindComponent(entity, "HazardRuntime")) {
            SetComponentProperty(*hazard, "triggered", "false");
        }
        if (Component* audio = FindComponent(entity, "AudioSourceRuntime")) {
            SetComponentProperty(*audio, "played", "false");
        }
        if (Component* animator = FindComponent(entity, "AnimatorRuntime")) {
            SetComponentProperty(*animator, "currentTime", "0");
        }
        if (Component* spriteAnimation = FindComponent(entity, "SpriteAnimationRuntime")) {
            SetComponentProperty(*spriteAnimation, "currentTime", "0");
            SetComponentProperty(*spriteAnimation, "currentFrame", "0");
        }
        if (Component* navigation = FindComponent(entity, "NavigationAgentRuntime")) {
            SetComponentProperty(*navigation, "reached", "false");
        }
        if (Component* identity = FindComponent(entity, "NetworkIdentityRuntime")) {
            SetComponentProperty(*identity, "registered", "false");
            SetComponentProperty(*identity, "lastReplicatedTime", "0");
        }
        if (Component* session = FindComponent(entity, "NetworkSessionRuntime")) {
            SetComponentProperty(*session, "status", "Disconnected");
            SetComponentProperty(*session, "tick", "0");
        }
        if (Component* uiFocus = FindComponent(entity, "UIFocusRuntime")) {
            SetComponentProperty(*uiFocus, "focused", "false");
        }
        if (Component* uiAnimation = FindComponent(entity, "UIAnimationRuntime")) {
            SetComponentProperty(*uiAnimation, "currentTime", "0");
            SetComponentProperty(*uiAnimation, "normalizedTime", "0");
        }
    }
}

void EngineRuntime::RunSpinRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds, const RuntimeHostCallbacks& callbacks) {
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        if (!HasEnabledComponentLocal(entity, "SpinRuntime")) {
            continue;
        }
        const float speed = SpinRuntimeSpeed(entity);
        entity.rotation[1] = NormalizeDegrees(entity.rotation[1] + speed * deltaSeconds);
        SyncTransform(callbacks, entity);
    }
}

void EngineRuntime::RunCharacterControllerSystem(std::vector<Entity>& entities, float deltaSeconds,
                                                 const RuntimeHostCallbacks& callbacks) {
    controlledRuntimeEntityId_ = 0;
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        if (HasEnabledComponentLocal(entity, "CharacterControllerRuntime")) {
            controlledRuntimeEntityId_ = entity.id;
            float moveX = std::isfinite(characterControllerInput_.moveX) ? characterControllerInput_.moveX : 0.0f;
            float moveZ = std::isfinite(characterControllerInput_.moveZ) ? characterControllerInput_.moveZ : 0.0f;
            moveX = std::clamp(moveX, -1.0f, 1.0f);
            moveZ = std::clamp(moveZ, -1.0f, 1.0f);

            const float inputLength = std::sqrt(moveX * moveX + moveZ * moveZ);
            if (inputLength > 0.0001f && std::isfinite(inputLength)) {
                if (inputLength > 1.0f) {
                    moveX /= inputLength;
                    moveZ /= inputLength;
                }

                const float sprintMultiplier =
                    characterControllerInput_.sprint ? CharacterControllerSprintMultiplier(entity) : 1.0f;
                const float moveSpeed = CharacterControllerSpeed(entity) * sprintMultiplier;
                entity.position[0] += moveX * moveSpeed * deltaSeconds;
                entity.position[2] += moveZ * moveSpeed * deltaSeconds;
                entity.rotation[1] = NormalizeDegrees(std::atan2(moveX, -moveZ) * kRadiansToDegrees);
                SyncTransform(callbacks, entity);
            }
            return;
        }
    }
}

void EngineRuntime::RunFallDeathRuntimeSystem(const std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks) {
    if (playGoalReached_ || playFailed_) {
        return;
    }

    bool hasRules = false;
    float fallDeathY = -8.0f;
    for (const Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        const Component* rules = FindComponent(entity, "GameRulesRuntime");
        if (rules == nullptr || !ComponentEnabled(*rules)) {
            continue;
        }
        fallDeathY = ComponentFloatProperty(*rules, "fallDeathY", fallDeathY);
        hasRules = true;
        break;
    }
    if (!hasRules) {
        return;
    }

    for (const Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id) || !HasEnabledComponentLocal(entity, "CharacterControllerRuntime")) {
            continue;
        }
        if (entity.position[1] >= fallDeathY) {
            return;
        }
        playFailed_ = true;
        runtimeStatus_ = "Failed";
        RequestHitCameraShake();
        AddRuntimeLog(LogLevel::Warning, "runtime.fallDeath", "Failed",
                      entity.name + " y=" + std::to_string(entity.position[1]));
        EmitEditorLog(callbacks, LogLevel::Warning, entity.name + " fell below the death threshold. Run failed.");
        EmitActivity(callbacks, "runtime.fallDeath", "Failed", entity.name);
        return;
    }
}

void EngineRuntime::RunTriggerRuntimeSystem(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks) {
    if (playGoalReached_ || playFailed_) {
        return;
    }

    Entity* player = nullptr;
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        if (HasEnabledComponentLocal(entity, "CharacterControllerRuntime")) {
            player = &entity;
            break;
        }
    }

    if (player == nullptr) {
        return;
    }

    const std::vector<PhysicsEvent>& physicsEvents = physicsWorld_.LastEvents();
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id) || entity.id == player->id) {
            continue;
        }

        if (Component* collectible = FindEnabledComponent(entity, "CollectibleRuntime")) {
            const bool alreadyCollected = ComponentBoolProperty(*collectible, "collected", false);
            const float radius = std::max(0.05f, ComponentFloatProperty(*collectible, "radius", 0.65f));
            if (!alreadyCollected && RuntimeTriggerOverlaps(*player, entity, radius, physicsEvents)) {
                const int points = std::max(0, ComponentIntProperty(*collectible, "points", 1));
                playScore_ += points;
                SetComponentProperty(*collectible, "collected", "true");
                entity.scale = {0.0f, 0.0f, 0.0f};
                SyncTransform(callbacks, entity);
                AddRuntimeLog(LogLevel::Info, "runtime.collectible", "Collected",
                              entity.name + " +" + std::to_string(points) + " score=" + std::to_string(playScore_));
                EmitEditorLog(callbacks, LogLevel::Info,
                              "Collected " + entity.name + ". Score " + std::to_string(playScore_) + ".");
                EmitActivity(callbacks, "runtime.collectible", "Collected", entity.name + " +" + std::to_string(points));
            }
            continue;
        }

        if (Component* hazard = FindEnabledComponent(entity, "HazardRuntime")) {
            const bool alreadyTriggered = ComponentBoolProperty(*hazard, "triggered", false);
            const float radius = std::max(0.05f, ComponentFloatProperty(*hazard, "radius", 0.55f));
            if (!alreadyTriggered && RuntimeTriggerOverlaps(*player, entity, radius, physicsEvents)) {
                SetComponentProperty(*hazard, "triggered", "true");
                playFailed_ = true;
                runtimeStatus_ = "Failed";
                RequestHitCameraShake();
                AddRuntimeLog(LogLevel::Warning, "runtime.hazard", "Failed", entity.name);
                EmitEditorLog(callbacks, LogLevel::Warning, "Hazard triggered by " + entity.name + ". Run failed.");
                EmitActivity(callbacks, "runtime.hazard", "Failed", entity.name);
                return;
            }
            continue;
        }

        if (Component* goal = FindEnabledComponent(entity, "GoalRuntime")) {
            const int requiredScore = std::max(0, ComponentIntProperty(*goal, "requiredScore", 3));
            const float radius = std::max(0.05f, ComponentFloatProperty(*goal, "radius", 0.9f));
            if (playScore_ >= requiredScore && RuntimeTriggerOverlaps(*player, entity, radius, physicsEvents)) {
                SetComponentProperty(*goal, "completed", "true");
                playGoalReached_ = true;
                runtimeStatus_ = "Goal Complete";
                AddRuntimeLog(LogLevel::Info, "runtime.goal", "Complete",
                              entity.name + " score=" + std::to_string(playScore_));
                EmitEditorLog(callbacks, LogLevel::Info,
                              "Goal reached at " + entity.name + ". Final score " + std::to_string(playScore_) + ".");
                EmitActivity(callbacks, "runtime.goal", "Complete", entity.name + " score=" + std::to_string(playScore_));
                return;
            }
        }
    }
}

void EngineRuntime::RunUIStateBindingRuntimeSystem(std::vector<Entity>& entities) {
    for (Entity& entity : entities) {
        for (const Component& binding : entity.components) {
            if (binding.type != "UIStateBindingRuntime" || !ComponentEnabled(binding)) {
                continue;
            }
            const std::string stateKey = ComponentStringProperty(binding, "stateKey", "playFailed");
            const bool expected = ComponentBoolProperty(binding, "equals", true);
            const bool targetVisible = ComponentBoolProperty(binding, "targetVisible", true);
            const bool stateValue = EvaluateUIStateBindingKey(stateKey, playState_, playFailed_, playGoalReached_, playScore_);
            const bool matched = stateValue == expected;
            entity.activeSelf = matched ? targetVisible : !targetVisible;
            break;
        }
    }
}

void EngineRuntime::RunUIFocusRuntimeSystem(std::vector<Entity>& entities) {
    Entity* focusedEntity = nullptr;
    Component* focusedComponent = nullptr;
    Entity* firstFocusableEntity = nullptr;
    Component* firstFocusableComponent = nullptr;

    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        Component* focus = FindEnabledComponent(entity, "UIFocusRuntime");
        if (focus == nullptr || !ComponentBoolProperty(*focus, "focusable", true)) {
            continue;
        }
        if (firstFocusableEntity == nullptr) {
            firstFocusableEntity = &entity;
            firstFocusableComponent = focus;
        }
        if (ComponentBoolProperty(*focus, "focused", false) && focusedEntity == nullptr) {
            focusedEntity = &entity;
            focusedComponent = focus;
        } else if (ComponentBoolProperty(*focus, "focused", false)) {
            SetComponentProperty(*focus, "focused", "false");
        }
    }

    if (focusedEntity == nullptr && firstFocusableEntity != nullptr && firstFocusableComponent != nullptr) {
        SetComponentProperty(*firstFocusableComponent, "focused", "true");
    } else if (focusedComponent != nullptr) {
        SetComponentProperty(*focusedComponent, "focused", "true");
    }
}

void EngineRuntime::RunUIAnimationRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds) {
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        Component* animation = FindEnabledComponent(entity, "UIAnimationRuntime");
        if (animation == nullptr) {
            continue;
        }
        const float duration = std::max(0.001f, ComponentFloatProperty(*animation, "duration", 1.0f));
        const float speed = ComponentFloatProperty(*animation, "speed", 1.0f);
        float currentTime = ComponentFloatProperty(*animation, "currentTime", 0.0f) + deltaSeconds * speed;
        if (ComponentBoolProperty(*animation, "loop", true)) {
            currentTime = std::fmod(std::max(0.0f, currentTime), duration);
        } else {
            currentTime = std::clamp(currentTime, 0.0f, duration);
        }
        SetComponentProperty(*animation, "currentTime", std::to_string(currentTime));
        SetComponentProperty(*animation, "normalizedTime", std::to_string(std::clamp(currentTime / duration, 0.0f, 1.0f)));
    }
}

void EngineRuntime::RunAudioRuntimeSystem(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks) {
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        Component* audio = FindEnabledComponent(entity, "AudioSourceRuntime");
        if (audio == nullptr || !ComponentBoolProperty(*audio, "playOnStart", true) ||
            ComponentBoolProperty(*audio, "played", false)) {
            continue;
        }
        SetComponentProperty(*audio, "played", "true");
        const std::string clip = ComponentStringProperty(*audio, "clip", "AudioClip");
        AddRuntimeLog(LogLevel::Info, "audio.play", "Playing", entity.name + " clip=" + clip);
        EmitActivity(callbacks, "audio.play", "Playing", entity.name + " clip=" + clip);
    }
}

void EngineRuntime::RunAnimationRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds) {
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        Component* animator = FindEnabledComponent(entity, "AnimatorRuntime");
        if (animator == nullptr) {
            continue;
        }
        const float duration = std::max(0.001f, ComponentFloatProperty(*animator, "duration", 1.0f));
        const float speed = ComponentFloatProperty(*animator, "speed", 1.0f);
        float currentTime = ComponentFloatProperty(*animator, "currentTime", 0.0f) + deltaSeconds * speed;
        if (ComponentBoolProperty(*animator, "loop", true)) {
            currentTime = std::fmod(std::max(0.0f, currentTime), duration);
        } else {
            currentTime = std::clamp(currentTime, 0.0f, duration);
        }
        SetComponentProperty(*animator, "currentTime", std::to_string(currentTime));
    }
}

void EngineRuntime::RunSpriteAnimationRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds) {
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }

        Component* animation = FindEnabledComponent(entity, "SpriteAnimationRuntime");
        Component* spriteRenderer = FindComponent(entity, "SpriteRenderer");
        if (animation == nullptr || spriteRenderer == nullptr || !ComponentEnabled(*spriteRenderer) ||
            !ComponentBoolProperty(*animation, "playOnStart", true)) {
            continue;
        }

        const std::vector<std::string> frames = SplitSpriteFrameList(ComponentStringProperty(*animation, "frames"));
        if (frames.empty()) {
            continue;
        }

        const float frameRate = std::max(0.001f, ComponentFloatProperty(*animation, "frameRate", 12.0f));
        const float duration = static_cast<float>(frames.size()) / frameRate;
        float currentTime = ComponentFloatProperty(*animation, "currentTime", 0.0f) + deltaSeconds;
        if (ComponentBoolProperty(*animation, "loop", true)) {
            currentTime = std::fmod(std::max(0.0f, currentTime), std::max(0.001f, duration));
        } else {
            currentTime = std::clamp(currentTime, 0.0f, std::max(0.0f, duration - 0.0001f));
        }

        const int frameIndex = std::clamp(static_cast<int>(std::floor(currentTime * frameRate)), 0,
                                          static_cast<int>(frames.size()) - 1);
        const std::string& frame = frames[static_cast<size_t>(frameIndex)];
        const size_t split = frame.find('@');
        if (split != std::string::npos) {
            const std::string spriteName = TrimCopy(frame.substr(0, split));
            const std::string rect = TrimCopy(frame.substr(split + 1));
            if (!spriteName.empty()) {
                SetComponentProperty(*spriteRenderer, "sprite", spriteName);
            }
            if (!rect.empty()) {
                SetComponentProperty(*spriteRenderer, "rect", rect);
            }
        } else if (LooksLikeSpriteRectToken(frame)) {
            SetComponentProperty(*spriteRenderer, "rect", frame);
        } else {
            SetComponentProperty(*spriteRenderer, "sprite", frame);
        }

        SetComponentProperty(*animation, "currentTime", std::to_string(currentTime));
        SetComponentProperty(*animation, "currentFrame", std::to_string(frameIndex));
    }
}

void EngineRuntime::RunNavigationRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds,
                                               const RuntimeHostCallbacks& callbacks) {
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        Component* navigation = FindEnabledComponent(entity, "NavigationAgentRuntime");
        if (navigation == nullptr || ComponentBoolProperty(*navigation, "reached", false)) {
            continue;
        }
        Entity* target = FindEntityByName(entities, ComponentStringProperty(*navigation, "targetName"));
        if (target == nullptr || !IsEntityActiveInHierarchy(entities, target->id)) {
            continue;
        }
        const float stoppingDistance = std::max(0.0f, ComponentFloatProperty(*navigation, "stoppingDistance", 0.1f));
        const float speed = std::max(0.0f, ComponentFloatProperty(*navigation, "speed", 2.5f));
        const std::array<float, 3> toTarget = Vec3Sub(target->position, entity.position);
        const float distance = Vec3Length(toTarget);
        if (distance <= stoppingDistance || speed <= 0.0f) {
            SetComponentProperty(*navigation, "reached", "true");
            AddRuntimeLog(LogLevel::Info, "navigation.reached", "Reached", entity.name + " -> " + target->name);
            EmitActivity(callbacks, "navigation.reached", "Reached", entity.name + " -> " + target->name);
            continue;
        }
        const float moveDistance = std::min(distance - stoppingDistance, speed * deltaSeconds);
        entity.position = Vec3Add(entity.position, Vec3Mul(Vec3Normalize(toTarget), moveDistance));
        SyncTransform(callbacks, entity);
    }
}

void EngineRuntime::RunNetworkingRuntimeSystem(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks) {
    bool sessionActive = false;
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        Component* session = FindEnabledComponent(entity, "NetworkSessionRuntime");
        if (session == nullptr) {
            continue;
        }
        const int tick = std::max(0, ComponentIntProperty(*session, "tick", 0)) + 1;
        SetComponentProperty(*session, "status", "Connected");
        SetComponentProperty(*session, "tick", std::to_string(tick));
        sessionActive = true;
        if (tick == 1) {
            AddRuntimeLog(LogLevel::Info, "network.session", "Connected",
                          ComponentStringProperty(*session, "mode", "LocalLoopback"));
            EmitActivity(callbacks, "network.session", "Connected", entity.name);
        }
    }

    if (!sessionActive) {
        return;
    }
    for (Entity& entity : entities) {
        if (!IsEntityActiveInHierarchy(entities, entity.id)) {
            continue;
        }
        Component* identity = FindEnabledComponent(entity, "NetworkIdentityRuntime");
        if (identity == nullptr) {
            continue;
        }
        const bool registered = ComponentBoolProperty(*identity, "registered", false);
        SetComponentProperty(*identity, "registered", "true");
        SetComponentProperty(*identity, "lastReplicatedTime", std::to_string(playElapsedSeconds_));
        if (!registered) {
            AddRuntimeLog(LogLevel::Info, "network.identity", "Registered",
                          entity.name + " id=" + ComponentStringProperty(*identity, "networkId", "net-entity"));
        }
    }
}

void EngineRuntime::RunCameraFollowRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds,
                                                 const RuntimeHostCallbacks& callbacks) {
    deltaSeconds = std::max(0.0f, std::min(deltaSeconds, 0.25f));
    for (Entity& camera : entities) {
        if (!IsEntityActiveInHierarchy(entities, camera.id)) {
            continue;
        }
        Component* follow = FindEnabledComponent(camera, "CameraFollowRuntime");
        Component* cameraComponent = FindEnabledComponent(camera, "Camera");
        if (follow == nullptr || cameraComponent == nullptr) {
            continue;
        }

        CameraRuntimeState& state = cameraStates_[camera.id];
        for (const CameraEffectRequest& request : pendingCameraEffects_) {
            if (!CameraRequestMatches(request, camera)) {
                continue;
            }
            if (request.snap) {
                state.snapNextUpdate = true;
            }
            if (request.pauseControlSet) {
                state.pausedByApi = request.pauseControlValue;
            }
            if (!request.cutTargetName.empty()) {
                state.cutTargetName = request.cutTargetName;
                state.cutRemainingSeconds = std::max(deltaSeconds, request.cutHoldSeconds);
                state.snapNextUpdate = true;
            }
            if (!request.profileName.empty() || request.amplitude > 0.0f || request.frequency > 0.0f) {
                CameraShakeProfile profile = ShakeProfileByName(request.profileName);
                state.shakeElapsed = 0.0f;
                state.shakeDuration = request.duration > 0.0f ? request.duration : profile.duration;
                state.shakeAmplitude = request.amplitude > 0.0f ? request.amplitude : profile.amplitude;
                state.shakeFrequency = request.frequency > 0.0f ? request.frequency : profile.frequency;
            }
            state.positionImpulse = Vec3Add(state.positionImpulse, request.positionImpulse);
            state.rotationImpulse = Vec3Add(state.rotationImpulse, request.rotationImpulse);
            if (Vec3Length(request.positionImpulse) > 0.0001f || Vec3Length(request.rotationImpulse) > 0.0001f) {
                state.impulseElapsed = 0.0f;
                state.impulseDuration = std::max(0.01f, request.duration);
            }
        }

        if (ComponentBoolProperty(*follow, "paused", false) || state.pausedByApi) {
            continue;
        }

        if (state.cutRemainingSeconds > 0.0f) {
            state.cutRemainingSeconds = std::max(0.0f, state.cutRemainingSeconds - deltaSeconds);
        } else {
            state.cutTargetName.clear();
        }

        const std::string mode = ToLowerCopy(TrimCopy(ComponentStringProperty(*follow, "mode", "Follow")));
        const bool freeMode = mode == "free" || mode == "fly";
        const bool fixedMode = mode == "fixed";
        const bool cinematicMode = mode == "cinematic" || mode == "rail";
        const std::string targetOverride = state.cutTargetName;
        CameraTargetInfo targetInfo = ResolveCameraTarget(entities, *follow, targetOverride);
        if (!freeMode && !fixedMode && !cinematicMode && !targetInfo.found) {
            continue;
        }

        const std::array<float, 3> offset = ComponentVec3Property(*follow, "offset", {0.0f, 5.5f, 7.0f});
        const float pitch = ComponentFloatProperty(*follow, "pitchDegrees", -38.0f);
        const float yaw = ComponentFloatProperty(*follow, "yawDegrees", 0.0f);
        const float blendTime = std::max(0.0f, ComponentFloatProperty(*follow, "blendTime", 0.0f));
        float smoothTime = std::max(0.0f, ComponentFloatProperty(*follow, "smoothTime", 0.0f));
        const float rotationSmoothTime = std::max(0.0f, ComponentFloatProperty(*follow, "rotationSmoothTime", smoothTime));
        const float zoomSmoothTime = std::max(0.0f, ComponentFloatProperty(*follow, "zoomSmoothTime", smoothTime));
        const float teleportResetDistance = ComponentFloatProperty(*follow, "teleportResetDistance", 20.0f);
        const std::string targetName = targetInfo.primary == nullptr ? std::string{} : targetInfo.primary->name;
        const bool modeOrTargetChanged = state.initialized && (state.lastMode != mode || state.lastTargetName != targetName);
        if (modeOrTargetChanged && blendTime > 0.0f) {
            smoothTime = blendTime;
        }

        if (!state.initialized) {
            state.initialized = true;
            state.focusPoint = targetInfo.found ? targetInfo.center : camera.position;
            state.lastTargetPosition = state.focusPoint;
            state.currentPosition = camera.position;
            state.currentRotation = camera.rotation;
            state.currentDistance = ComponentFloatProperty(*follow, "distance", 7.0f);
            state.currentOrthographicSize = ComponentFloatProperty(*cameraComponent, "orthographicSize", 5.0f);
        }

        if (targetInfo.found && deltaSeconds > 0.0001f) {
            state.targetVelocity = Vec3Mul(Vec3Sub(targetInfo.center, state.lastTargetPosition), 1.0f / deltaSeconds);
            state.lastTargetPosition = targetInfo.center;
        }

        const float lookAheadTime = std::max(0.0f, ComponentFloatProperty(*follow, "lookAheadTime", 0.0f));
        const float lookAheadMaxDistance = std::max(0.0f, ComponentFloatProperty(*follow, "lookAheadMaxDistance", 0.0f));
        std::array<float, 3> lookAhead = Vec3Mul(state.targetVelocity, lookAheadTime);
        const float lookAheadDistance = Vec3Length(lookAhead);
        if (lookAheadMaxDistance > 0.0f && lookAheadDistance > lookAheadMaxDistance) {
            lookAhead = Vec3Mul(Vec3Normalize(lookAhead), lookAheadMaxDistance);
        }

        std::array<float, 3> desiredFocus = targetInfo.found ? Vec3Add(targetInfo.center, lookAhead) : state.focusPoint;
        const float deadZoneRadius = std::max(0.0f, ComponentFloatProperty(*follow, "deadZoneRadius", 0.0f));
        const float softZoneRadius = std::max(deadZoneRadius, ComponentFloatProperty(*follow, "softZoneRadius", deadZoneRadius));
        const std::array<float, 3> focusDelta = Vec3Sub(desiredFocus, state.focusPoint);
        const float focusDistance = Vec3Length(focusDelta);
        if (focusDistance <= deadZoneRadius) {
            desiredFocus = state.focusPoint;
        } else if (softZoneRadius > deadZoneRadius && focusDistance < softZoneRadius) {
            const float zoneAlpha = (focusDistance - deadZoneRadius) / std::max(0.001f, softZoneRadius - deadZoneRadius);
            desiredFocus = Vec3Add(state.focusPoint, Vec3Mul(focusDelta, zoneAlpha));
        }
        state.focusPoint = Vec3Lerp(state.focusPoint, desiredFocus, SmoothAlpha(deltaSeconds, smoothTime));

        std::array<float, 3> desiredPosition = camera.position;
        std::array<float, 3> desiredRotation = camera.rotation;
        float desiredDistance = std::max(0.001f, ComponentFloatProperty(*follow, "distance", state.currentDistance));
        const float groupPadding = std::max(0.0f, targetInfo.radius * 1.4f);
        desiredDistance = std::max(desiredDistance, desiredDistance + groupPadding);

        if (freeMode) {
            const float moveSpeed = std::max(0.001f, ComponentFloatProperty(*follow, "freeMoveSpeed", 6.0f));
            const float lookSensitivity = std::max(0.001f, ComponentFloatProperty(*follow, "freeLookSensitivity", 90.0f));
            desiredRotation = camera.rotation;
            desiredRotation[1] += RuntimeActionValue(runtimeInput_, {"CameraLookX", "LookX"}) * lookSensitivity * deltaSeconds;
            desiredRotation[0] += RuntimeActionValue(runtimeInput_, {"CameraLookY", "LookY"}) * lookSensitivity * deltaSeconds;
            desiredRotation[0] = std::clamp(desiredRotation[0], -89.0f, 89.0f);
            const std::array<float, 3> forward = ForwardFromEuler(desiredRotation);
            const std::array<float, 3> right = RightFromForward(forward);
            const std::array<float, 3> up = UpFromForward(forward);
            std::array<float, 3> movement = Vec3Add(
                Vec3Add(Vec3Mul(right, RuntimeActionValue(runtimeInput_, {"CameraMoveX", "MoveX"})),
                        Vec3Mul(up, RuntimeActionValue(runtimeInput_, {"CameraMoveY", "MoveY"}))),
                Vec3Mul(forward, RuntimeActionValue(runtimeInput_, {"CameraMoveZ", "MoveZ"})));
            if (Vec3Length(movement) > 1.0f) {
                movement = Vec3Normalize(movement);
            }
            desiredPosition = Vec3Add(camera.position, Vec3Mul(movement, moveSpeed * deltaSeconds));
        } else if (cinematicMode) {
            if (!EvaluateCameraRail(ComponentStringProperty(*follow, "railKeyframes"), playElapsedSeconds_,
                                    ComponentBoolProperty(*follow, "railLoop", false), &desiredPosition, &desiredRotation)) {
                desiredPosition = camera.position;
                desiredRotation = camera.rotation;
            }
        } else if (fixedMode) {
            desiredPosition = camera.position;
            desiredRotation = camera.rotation;
        } else if (mode == "firstperson" || mode == "first-person") {
            const float eyeHeight = ComponentFloatProperty(*follow, "firstPersonEyeHeight", 1.65f);
            desiredPosition = Vec3Add(Vec3Add(targetInfo.center, offset), {0.0f, eyeHeight, 0.0f});
            desiredRotation = targetInfo.primary == nullptr ? std::array<float, 3>{pitch, yaw, 0.0f}
                                                            : Vec3Add(targetInfo.primary->rotation, {pitch, yaw, 0.0f});
        } else if (mode == "thirdperson" || mode == "third-person" || mode == "chase") {
            const float chaseDistance = std::max(0.001f, ComponentFloatProperty(*follow, "chaseDistance", 7.0f));
            const float chaseHeight = ComponentFloatProperty(*follow, "chaseHeight", 3.0f);
            desiredDistance = std::max(chaseDistance, chaseDistance + groupPadding);
            state.currentDistance = Lerp(state.currentDistance, desiredDistance, SmoothAlpha(deltaSeconds, zoomSmoothTime));
            const std::array<float, 3> targetForward =
                targetInfo.primary == nullptr ? ForwardFromEuler({pitch, yaw, 0.0f}) : ForwardFromEuler(targetInfo.primary->rotation);
            desiredPosition = Vec3Add(Vec3Add(state.focusPoint, Vec3Mul(targetForward, -state.currentDistance)),
                                      Vec3Add({0.0f, chaseHeight, 0.0f}, offset));
            desiredRotation = RotationLookingAt(desiredPosition, Vec3Add(state.focusPoint, {0.0f, 1.0f, 0.0f}), camera.rotation);
        } else if (mode == "topdown" || mode == "top-down" || mode == "isometric") {
            const float topDownHeight = std::max(0.001f, ComponentFloatProperty(*follow, "topDownHeight", 12.0f));
            const float isoYaw = ComponentFloatProperty(*follow, "isometricYawDegrees", 45.0f);
            const float isoPitch = ComponentFloatProperty(*follow, "isometricPitchDegrees", -55.0f);
            desiredDistance = std::max(topDownHeight, topDownHeight + groupPadding);
            state.currentDistance = Lerp(state.currentDistance, desiredDistance, SmoothAlpha(deltaSeconds, zoomSmoothTime));
            desiredRotation = {isoPitch, isoYaw, 0.0f};
            desiredPosition = Vec3Add(Vec3Add(state.focusPoint, Vec3Mul(ForwardFromEuler(desiredRotation), -state.currentDistance)),
                                      offset);
        } else if (mode == "sidescroll" || mode == "side-scroll" || mode == "2d") {
            desiredPosition = Vec3Add(state.focusPoint, ComponentVec3Property(*follow, "sideScrollOffset", {0.0f, 2.0f, 10.0f}));
            desiredRotation = {0.0f, 0.0f, 0.0f};
        } else if (mode == "orbit") {
            const float orbitYaw = ComponentFloatProperty(*follow, "orbitYawDegrees", yaw);
            const float orbitPitch = ComponentFloatProperty(*follow, "orbitPitchDegrees", pitch);
            desiredDistance = std::max(desiredDistance, desiredDistance + groupPadding);
            state.currentDistance = Lerp(state.currentDistance, desiredDistance, SmoothAlpha(deltaSeconds, zoomSmoothTime));
            desiredRotation = {orbitPitch, orbitYaw, 0.0f};
            desiredPosition = Vec3Add(Vec3Add(state.focusPoint, Vec3Mul(ForwardFromEuler(desiredRotation), -state.currentDistance)),
                                      offset);
        } else {
            desiredPosition = Vec3Add(state.focusPoint, offset);
            desiredRotation = {pitch, yaw, 0.0f};
        }

        const std::array<float, 2> targetFraming = ComponentVec2Property(*follow, "targetFraming", {0.0f, 0.0f});
        if (!freeMode && !fixedMode && !cinematicMode && (std::fabs(targetFraming[0]) > 0.0001f ||
                                                           std::fabs(targetFraming[1]) > 0.0001f)) {
            const std::array<float, 3> forward = ForwardFromEuler(desiredRotation);
            desiredPosition = Vec3Add(desiredPosition, Vec3Mul(RightFromForward(forward), targetFraming[0]));
            desiredPosition = Vec3Add(desiredPosition, Vec3Mul(UpFromForward(forward), targetFraming[1]));
        }

        if (!freeMode && !fixedMode && !cinematicMode && ComponentBoolProperty(*follow, "collisionAvoidance", false) &&
            targetInfo.found) {
            const std::array<float, 3> toCamera = Vec3Sub(desiredPosition, targetInfo.center);
            const float distance = Vec3Length(toCamera);
            if (distance > 0.001f) {
                const std::array<float, 3> direction = Vec3Normalize(toCamera);
                const std::vector<Entity> filtered = FilterCameraRaycastEntities(
                    entities, ComponentStringProperty(*follow, "collisionMask", "Environment;Default"), camera.id,
                    targetInfo.primary == nullptr ? 0 : targetInfo.primary->id);
                PhysicsRayHit3D hit;
                const float collisionRadius = std::max(0.001f, ComponentFloatProperty(*follow, "collisionRadius", 0.25f));
                if (physicsWorld_.SphereCast3D(filtered, {targetInfo.center[0], targetInfo.center[1], targetInfo.center[2]},
                                               {direction[0], direction[1], direction[2]}, distance, collisionRadius,
                                               &hit)) {
                    desiredPosition = {hit.point.x, hit.point.y, hit.point.z};
                }
            }
        }

        if (ComponentBoolProperty(*follow, "boundsEnabled", false)) {
            desiredPosition = Vec3Clamp(desiredPosition, ComponentVec3Property(*follow, "boundsMin", {-50.0f, -50.0f, -50.0f}),
                                        ComponentVec3Property(*follow, "boundsMax", {50.0f, 50.0f, 50.0f}));
        }

        const bool shouldSnap = state.snapNextUpdate ||
                                (teleportResetDistance > 0.0f &&
                                 Vec3Length(Vec3Sub(state.currentPosition, desiredPosition)) > teleportResetDistance);
        const float positionAlpha = shouldSnap ? 1.0f : SmoothAlpha(deltaSeconds, smoothTime);
        const float rotationAlpha = shouldSnap ? 1.0f : SmoothAlpha(deltaSeconds, rotationSmoothTime);
        state.currentPosition = Vec3Lerp(state.currentPosition, desiredPosition, positionAlpha);
        state.currentRotation = RotationLerp(state.currentRotation, desiredRotation, rotationAlpha);
        state.snapNextUpdate = false;

        std::array<float, 3> outputPosition = state.currentPosition;
        std::array<float, 3> outputRotation = state.currentRotation;
        if (state.shakeDuration > 0.0f && state.shakeElapsed < state.shakeDuration) {
            const float t = state.shakeElapsed;
            const float envelope = 1.0f - std::clamp(t / state.shakeDuration, 0.0f, 1.0f);
            const float phase = t * state.shakeFrequency;
            const std::array<float, 3> forward = ForwardFromEuler(outputRotation);
            outputPosition = Vec3Add(outputPosition,
                                     Vec3Mul(RightFromForward(forward), std::sin(phase * 2.31f) * state.shakeAmplitude * envelope));
            outputPosition = Vec3Add(outputPosition,
                                     Vec3Mul(UpFromForward(forward), std::cos(phase * 1.73f) * state.shakeAmplitude * envelope));
            outputRotation[2] += std::sin(phase * 1.13f) * state.shakeAmplitude * 10.0f * envelope;
            state.shakeElapsed += deltaSeconds;
        }
        if (state.impulseDuration > 0.0f && state.impulseElapsed < state.impulseDuration) {
            const float envelope = 1.0f - std::clamp(state.impulseElapsed / state.impulseDuration, 0.0f, 1.0f);
            outputPosition = Vec3Add(outputPosition, Vec3Mul(state.positionImpulse, envelope));
            outputRotation = Vec3Add(outputRotation, Vec3Mul(state.rotationImpulse, envelope));
            state.impulseElapsed += deltaSeconds;
            if (state.impulseElapsed >= state.impulseDuration) {
                state.positionImpulse = {0.0f, 0.0f, 0.0f};
                state.rotationImpulse = {0.0f, 0.0f, 0.0f};
            }
        }

        if (CameraProjectionModeFromString(ComponentStringProperty(*cameraComponent, "projection", "Perspective")) ==
            CameraProjectionMode::Orthographic) {
            const float desiredOrthoSize =
                std::max(ComponentFloatProperty(*cameraComponent, "orthographicSize", 5.0f), targetInfo.radius * 1.25f);
            state.currentOrthographicSize =
                Lerp(state.currentOrthographicSize, desiredOrthoSize, SmoothAlpha(deltaSeconds, zoomSmoothTime));
            std::ostringstream stream;
            stream << state.currentOrthographicSize;
            SetComponentProperty(*cameraComponent, "orthographicSize", stream.str());
        }

        camera.position = outputPosition;
        camera.rotation = outputRotation;
        state.lastMode = mode;
        state.lastTargetName = targetName;
        SyncTransform(callbacks, camera);
    }
    pendingCameraEffects_.clear();
}

void EngineRuntime::SetCharacterControllerInput(CharacterControllerInput input) {
    input.moveX = std::isfinite(input.moveX) ? std::clamp(input.moveX, -1.0f, 1.0f) : 0.0f;
    input.moveZ = std::isfinite(input.moveZ) ? std::clamp(input.moveZ, -1.0f, 1.0f) : 0.0f;
    characterControllerInput_ = input;
    runtimeInput_.character = input;
}

void EngineRuntime::SetRuntimeInputState(RuntimeInputState input) {
    input.character.moveX = std::isfinite(input.character.moveX) ? std::clamp(input.character.moveX, -1.0f, 1.0f) : 0.0f;
    input.character.moveZ = std::isfinite(input.character.moveZ) ? std::clamp(input.character.moveZ, -1.0f, 1.0f) : 0.0f;
    if (playState_ != EditorPlayState::EditMode && input.captured != runtimeInput_.captured) {
        AddRuntimeLog(LogLevel::Info, "input.capture", input.captured ? "Captured" : "Released",
                      input.captured ? input.captureSurface : "Input focus released.");
    }
    runtimeInput_ = std::move(input);
    characterControllerInput_ = runtimeInput_.character;
}

void EngineRuntime::RequestCameraShake(std::string cameraName, std::string profileName) {
    CameraShakeProfile profile = ShakeProfileByName(profileName);
    CameraEffectRequest request;
    request.cameraName = std::move(cameraName);
    request.profileName = profileName.empty() ? std::string{"Medium"} : std::move(profileName);
    request.amplitude = profile.amplitude;
    request.duration = profile.duration;
    request.frequency = profile.frequency;
    pendingCameraEffects_.push_back(std::move(request));
}

void EngineRuntime::RequestCameraImpulse(std::string cameraName, std::array<float, 3> positionImpulse,
                                         std::array<float, 3> rotationImpulse, float durationSeconds) {
    CameraEffectRequest request;
    request.cameraName = std::move(cameraName);
    request.positionImpulse = positionImpulse;
    request.rotationImpulse = rotationImpulse;
    request.duration = std::max(0.01f, durationSeconds);
    pendingCameraEffects_.push_back(std::move(request));
}

void EngineRuntime::RequestCameraRecoil(std::string cameraName, float pitchDegrees, float distance) {
    RequestCameraShake(cameraName, "Recoil");
    RequestCameraImpulse(std::move(cameraName), {0.0f, 0.0f, std::max(0.0f, distance)}, {pitchDegrees, 0.0f, 0.0f},
                         0.16f);
}

void EngineRuntime::RequestHitCameraShake(std::string cameraName) {
    RequestCameraShake(std::move(cameraName), "Damage");
}

void EngineRuntime::RequestCameraCut(std::string cameraName, std::string targetName, float holdSeconds) {
    CameraEffectRequest request;
    request.cameraName = std::move(cameraName);
    request.cutTargetName = std::move(targetName);
    request.cutHoldSeconds = std::max(0.0f, holdSeconds);
    pendingCameraEffects_.push_back(std::move(request));
}

void EngineRuntime::SnapCamera(std::string cameraName) {
    CameraEffectRequest request;
    request.cameraName = std::move(cameraName);
    request.snap = true;
    pendingCameraEffects_.push_back(std::move(request));
}

void EngineRuntime::SetCameraControlPaused(std::string cameraName, bool paused) {
    CameraEffectRequest request;
    request.cameraName = std::move(cameraName);
    request.pauseControlSet = true;
    request.pauseControlValue = paused;
    pendingCameraEffects_.push_back(std::move(request));
}

bool EngineRuntime::WorldToScreen(const std::vector<Entity>& entities, const std::string& cameraName,
                                  std::array<float, 3> world, std::array<float, 2> viewportSize,
                                  std::array<float, 3>* outScreenDepth) const {
    const Entity* camera = FindCameraEntityByName(entities, cameraName);
    if (camera == nullptr) {
        return false;
    }
    return CameraWorldToScreen(BuildCameraFrameFromEntity(*camera), world, viewportSize, outScreenDepth);
}

bool EngineRuntime::ScreenToWorld(const std::vector<Entity>& entities, const std::string& cameraName,
                                  std::array<float, 2> screen, float depth, std::array<float, 2> viewportSize,
                                  std::array<float, 3>* outWorld) const {
    const Entity* camera = FindCameraEntityByName(entities, cameraName);
    if (camera == nullptr) {
        return false;
    }
    return CameraScreenToWorld(BuildCameraFrameFromEntity(*camera), screen, depth, viewportSize, outWorld);
}

bool EngineRuntime::ScreenPointToRay(const std::vector<Entity>& entities, const std::string& cameraName,
                                     std::array<float, 2> screen, std::array<float, 2> viewportSize,
                                     CameraRay* outRay) const {
    const Entity* camera = FindCameraEntityByName(entities, cameraName);
    if (camera == nullptr) {
        return false;
    }
    return CameraScreenPointToRay(BuildCameraFrameFromEntity(*camera), screen, viewportSize, outRay);
}

bool EngineRuntime::RaycastFromCamera(const std::vector<Entity>& entities, const std::string& cameraName,
                                      std::array<float, 2> screen, std::array<float, 2> viewportSize,
                                      PhysicsRayHit3D* outHit) const {
    const Entity* camera = FindCameraEntityByName(entities, cameraName);
    if (camera == nullptr) {
        return false;
    }
    return CameraRaycast3D(physicsWorld_, entities, BuildCameraFrameFromEntity(*camera), screen, viewportSize, outHit);
}

bool EngineRuntime::ConsumeScriptHostResult(const ScriptRuntimeHostResult& result,
                                            const RuntimeHostCallbacks& callbacks,
                                            bool blockOnHostFailure) {
    if (!result.hostRan && result.diagnostics.empty() && result.invokedCount == 0) {
        return true;
    }

    const std::string eventName = "runtime.script." + (result.phase.empty() ? std::string("host") : result.phase);
    if (result.invokedCount > 0 && result.diagnostics.empty()) {
        const std::string detail = std::to_string(result.invokedCount) + " script instance(s) invoked.";
        AddRuntimeLog(LogLevel::Info, eventName, "Invoked", detail);
        EmitActivity(callbacks, eventName, "Invoked", detail);
    }

    for (const ScriptRuntimeDiagnostic& diagnostic : result.diagnostics) {
        std::string detail = diagnostic.message;
        if (!diagnostic.scriptType.empty()) {
            detail = diagnostic.scriptType + ": " + detail;
        }
        if (!diagnostic.entityName.empty()) {
            detail = diagnostic.entityName + "." + detail;
        }
        AddRuntimeLog(diagnostic.level, eventName, RuntimeLogLevelLabel(diagnostic.level), detail);
        EmitEditorLog(callbacks, diagnostic.level, eventName + " " + RuntimeLogLevelLabel(diagnostic.level) + ": " + detail);
        EmitActivity(callbacks, eventName, RuntimeLogLevelLabel(diagnostic.level), detail);
    }

    if (!result.hostReady) {
        if (result.diagnostics.empty()) {
            const std::string detail = "Script runtime host failed without diagnostics.";
            AddRuntimeLog(LogLevel::Error, eventName, "Error", detail);
            EmitEditorLog(callbacks, LogLevel::Error, eventName + " Error: " + detail);
            EmitActivity(callbacks, eventName, "Error", detail);
        }
        return !blockOnHostFailure;
    }
    return true;
}

void EngineRuntime::AddRuntimeLog(LogLevel level, std::string event, std::string status, std::string detail) {
    runtimeLogs_.push_back({level, playElapsedSeconds_, std::move(event), std::move(status), std::move(detail)});
    if (runtimeLogs_.size() > kMaxRuntimeLogEntries) {
        runtimeLogs_.erase(runtimeLogs_.begin(), runtimeLogs_.begin() +
                                                    static_cast<std::ptrdiff_t>(runtimeLogs_.size() -
                                                                                kMaxRuntimeLogEntries));
    }
}

} // namespace aine

#include "engine/runtime/Camera.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace aine {
namespace {

constexpr float kPi = 3.1415926535f;
constexpr float kDegreesToRadians = kPi / 180.0f;
constexpr float kDefaultNearClip = 0.1f;
constexpr float kDefaultFarClip = 1000.0f;

struct CameraVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

CameraVec3 ToVec3(std::array<float, 3> value) {
    return {value[0], value[1], value[2]};
}

std::array<float, 3> ToArray(CameraVec3 value) {
    return {value.x, value.y, value.z};
}

CameraVec3 operator+(CameraVec3 left, CameraVec3 right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

CameraVec3 operator-(CameraVec3 left, CameraVec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

CameraVec3 operator*(CameraVec3 value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

CameraVec3 operator/(CameraVec3 value, float scalar) {
    return {value.x / scalar, value.y / scalar, value.z / scalar};
}

float Dot(CameraVec3 left, CameraVec3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

CameraVec3 Cross(CameraVec3 left, CameraVec3 right) {
    return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

float Length(CameraVec3 value) {
    return std::sqrt(Dot(value, value));
}

CameraVec3 Normalize(CameraVec3 value, CameraVec3 fallback = {0.0f, 0.0f, -1.0f}) {
    const float length = Length(value);
    if (length <= 0.00001f || !std::isfinite(length)) {
        return fallback;
    }
    return value / length;
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

std::vector<std::string> SplitMask(std::string value) {
    std::replace(value.begin(), value.end(), ',', ';');
    std::replace(value.begin(), value.end(), '|', ';');
    std::vector<std::string> result;
    std::istringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ';')) {
        token = TrimCopy(std::move(token));
        if (!token.empty()) {
            result.push_back(std::move(token));
        }
    }
    return result;
}

std::string ComponentStringProperty(const Component* component, const std::string& propertyName, std::string fallback = {}) {
    if (component == nullptr) {
        return fallback;
    }
    for (const ComponentProperty& property : component->properties) {
        if (property.name == propertyName) {
            return property.value;
        }
    }
    return fallback;
}

float ComponentFloatProperty(const Component* component, const std::string& propertyName, float fallback) {
    const std::string value = ComponentStringProperty(component, propertyName);
    if (value.empty()) {
        return fallback;
    }
    std::istringstream stream(value);
    float parsed = fallback;
    if (stream >> parsed && std::isfinite(parsed)) {
        return parsed;
    }
    return fallback;
}

std::array<float, 3> ComponentVec3Property(const Component* component, const std::string& propertyName,
                                           std::array<float, 3> fallback) {
    std::string value = ComponentStringProperty(component, propertyName);
    if (value.empty()) {
        return fallback;
    }
    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream stream(value);
    std::array<float, 3> parsed = fallback;
    if (stream >> parsed[0] >> parsed[1] >> parsed[2] && std::isfinite(parsed[0]) && std::isfinite(parsed[1]) &&
        std::isfinite(parsed[2])) {
        return parsed;
    }
    return fallback;
}

std::array<float, 4> ComponentVec4Property(const Component* component, const std::string& propertyName,
                                           std::array<float, 4> fallback) {
    std::string value = ComponentStringProperty(component, propertyName);
    if (value.empty()) {
        return fallback;
    }
    std::replace(value.begin(), value.end(), ',', ' ');
    std::istringstream stream(value);
    std::array<float, 4> parsed = fallback;
    if (stream >> parsed[0] >> parsed[1] >> parsed[2] && std::isfinite(parsed[0]) && std::isfinite(parsed[1]) &&
        std::isfinite(parsed[2])) {
        if (!(stream >> parsed[3]) || !std::isfinite(parsed[3])) {
            parsed[3] = fallback[3];
        }
        return parsed;
    }
    return fallback;
}

const Component* FindComponent(const Entity& entity, const std::string& type) {
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [&type](const Component& component) {
        return component.type == type;
    });
    return it == entity.components.end() ? nullptr : &(*it);
}

CameraVec3 ForwardFromEuler(std::array<float, 3> rotation) {
    const float yaw = rotation[1] * kDegreesToRadians;
    const float pitch = std::clamp(rotation[0], -89.0f, 89.0f) * kDegreesToRadians;
    return Normalize({std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)});
}

} // namespace

CameraProjectionMode CameraProjectionModeFromString(const std::string& value) {
    const std::string normalized = ToLower(TrimCopy(value));
    if (normalized == "orthographic" || normalized == "ortho" || normalized == "2d") {
        return CameraProjectionMode::Orthographic;
    }
    return CameraProjectionMode::Perspective;
}

std::string CameraProjectionModeToString(CameraProjectionMode mode) {
    return mode == CameraProjectionMode::Orthographic ? "Orthographic" : "Perspective";
}

CameraViewportRect CameraViewportRectFromComponent(const Component* cameraComponent) {
    const std::array<float, 3> xyz =
        ComponentVec3Property(cameraComponent, "viewportRect", {0.0f, 0.0f, 1.0f});
    const float height = ComponentFloatProperty(cameraComponent, "viewportHeight", 1.0f);
    CameraViewportRect rect;
    rect.x = std::clamp(xyz[0], 0.0f, 1.0f);
    rect.y = std::clamp(xyz[1], 0.0f, 1.0f);
    rect.width = std::clamp(xyz[2], 0.001f, 1.0f);
    rect.height = std::clamp(height, 0.001f, 1.0f);
    if (rect.x + rect.width > 1.0f) {
        rect.width = std::max(0.001f, 1.0f - rect.x);
    }
    if (rect.y + rect.height > 1.0f) {
        rect.height = std::max(0.001f, 1.0f - rect.y);
    }
    return rect;
}

CameraFrame BuildCameraFrameFromEntity(const Entity& cameraEntity, const Component* cameraComponent) {
    if (cameraComponent == nullptr) {
        cameraComponent = FindComponent(cameraEntity, "Camera");
    }

    CameraFrame frame;
    frame.position = cameraEntity.position;
    frame.rotation = cameraEntity.rotation;
    frame.forward = ToArray(ForwardFromEuler(cameraEntity.rotation));
    CameraVec3 right = Normalize(Cross(ToVec3(frame.forward), {0.0f, 1.0f, 0.0f}), {1.0f, 0.0f, 0.0f});
    CameraVec3 up = Normalize(Cross(right, ToVec3(frame.forward)), {0.0f, 1.0f, 0.0f});
    frame.right = ToArray(right);
    frame.up = ToArray(up);
    frame.projection = CameraProjectionModeFromString(ComponentStringProperty(cameraComponent, "projection", "Perspective"));
    frame.fovDegrees = std::clamp(ComponentFloatProperty(cameraComponent, "fov", 60.0f), 1.0f, 175.0f);
    frame.orthographicSize = std::max(0.001f, ComponentFloatProperty(cameraComponent, "orthographicSize", 5.0f));
    frame.nearClip = std::max(0.0001f, ComponentFloatProperty(cameraComponent, "near", kDefaultNearClip));
    frame.farClip = std::max(frame.nearClip + 0.001f, ComponentFloatProperty(cameraComponent, "far", kDefaultFarClip));
    frame.viewport = CameraViewportRectFromComponent(cameraComponent);
    frame.clearColor = ComponentVec4Property(cameraComponent, "clearColor", frame.clearColor);
    frame.cullingMask = ComponentStringProperty(cameraComponent, "cullingMask", "Everything");
    return frame;
}

bool CameraWorldToScreen(const CameraFrame& camera, std::array<float, 3> world,
                         std::array<float, 2> viewportSize, std::array<float, 3>* outScreenDepth) {
    if (outScreenDepth == nullptr || viewportSize[0] <= 1.0f || viewportSize[1] <= 1.0f) {
        return false;
    }

    const CameraVec3 toPoint = ToVec3(world) - ToVec3(camera.position);
    const float depth = Dot(toPoint, ToVec3(camera.forward));
    if (depth < camera.nearClip || depth > camera.farClip) {
        return false;
    }

    const float aspect = viewportSize[0] / viewportSize[1];
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    if (camera.projection == CameraProjectionMode::Orthographic) {
        const float verticalHalf = std::max(0.001f, camera.orthographicSize);
        ndcX = Dot(toPoint, ToVec3(camera.right)) / (verticalHalf * std::max(0.001f, aspect));
        ndcY = Dot(toPoint, ToVec3(camera.up)) / verticalHalf;
    } else {
        const float tanHalfFov = std::tan(camera.fovDegrees * kDegreesToRadians * 0.5f);
        if (tanHalfFov <= 0.00001f) {
            return false;
        }
        ndcX = Dot(toPoint, ToVec3(camera.right)) / (depth * tanHalfFov * std::max(0.001f, aspect));
        ndcY = Dot(toPoint, ToVec3(camera.up)) / (depth * tanHalfFov);
    }

    if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) {
        return false;
    }

    (*outScreenDepth)[0] = (ndcX * 0.5f + 0.5f) * viewportSize[0];
    (*outScreenDepth)[1] = (0.5f - ndcY * 0.5f) * viewportSize[1];
    (*outScreenDepth)[2] = depth;
    return true;
}

bool CameraScreenToWorld(const CameraFrame& camera, std::array<float, 2> screen, float depth,
                         std::array<float, 2> viewportSize, std::array<float, 3>* outWorld) {
    if (outWorld == nullptr || viewportSize[0] <= 1.0f || viewportSize[1] <= 1.0f || !std::isfinite(depth)) {
        return false;
    }

    const float aspect = viewportSize[0] / viewportSize[1];
    const float ndcX = (2.0f * screen[0] / viewportSize[0]) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screen[1] / viewportSize[1]);
    CameraVec3 world = ToVec3(camera.position);
    if (camera.projection == CameraProjectionMode::Orthographic) {
        const float verticalHalf = std::max(0.001f, camera.orthographicSize);
        world = world + ToVec3(camera.right) * (ndcX * verticalHalf * std::max(0.001f, aspect)) +
                ToVec3(camera.up) * (ndcY * verticalHalf) + ToVec3(camera.forward) * depth;
    } else {
        const float tanHalfFov = std::tan(camera.fovDegrees * kDegreesToRadians * 0.5f);
        const CameraVec3 direction =
            Normalize(ToVec3(camera.forward) + ToVec3(camera.right) * (ndcX * aspect * tanHalfFov) +
                      ToVec3(camera.up) * (ndcY * tanHalfFov));
        world = world + direction * depth;
    }
    *outWorld = ToArray(world);
    return std::isfinite((*outWorld)[0]) && std::isfinite((*outWorld)[1]) && std::isfinite((*outWorld)[2]);
}

bool CameraScreenPointToRay(const CameraFrame& camera, std::array<float, 2> screen,
                            std::array<float, 2> viewportSize, CameraRay* outRay) {
    if (outRay == nullptr || viewportSize[0] <= 1.0f || viewportSize[1] <= 1.0f) {
        return false;
    }

    const float aspect = viewportSize[0] / viewportSize[1];
    const float ndcX = (2.0f * screen[0] / viewportSize[0]) - 1.0f;
    const float ndcY = 1.0f - (2.0f * screen[1] / viewportSize[1]);
    CameraVec3 origin = ToVec3(camera.position);
    CameraVec3 direction = ToVec3(camera.forward);
    if (camera.projection == CameraProjectionMode::Orthographic) {
        const float verticalHalf = std::max(0.001f, camera.orthographicSize);
        origin = origin + ToVec3(camera.right) * (ndcX * verticalHalf * std::max(0.001f, aspect)) +
                 ToVec3(camera.up) * (ndcY * verticalHalf);
    } else {
        const float tanHalfFov = std::tan(camera.fovDegrees * kDegreesToRadians * 0.5f);
        direction = Normalize(ToVec3(camera.forward) + ToVec3(camera.right) * (ndcX * aspect * tanHalfFov) +
                              ToVec3(camera.up) * (ndcY * tanHalfFov));
    }

    outRay->origin = ToArray(origin);
    outRay->direction = ToArray(direction);
    return std::isfinite(outRay->direction[0]) && std::isfinite(outRay->direction[1]) &&
           std::isfinite(outRay->direction[2]);
}

bool CameraRaycast3D(const PhysicsWorld& physicsWorld, const std::vector<Entity>& entities, const CameraFrame& camera,
                     std::array<float, 2> screen, std::array<float, 2> viewportSize, PhysicsRayHit3D* outHit) {
    CameraRay ray;
    if (!CameraScreenPointToRay(camera, screen, viewportSize, &ray)) {
        return false;
    }
    return physicsWorld.Raycast3D(entities, {ray.origin[0], ray.origin[1], ray.origin[2]},
                                  {ray.direction[0], ray.direction[1], ray.direction[2]}, camera.farClip, outHit);
}

bool CameraCullingMaskAllowsLayer(const std::string& cullingMask, const std::string& layer) {
    const std::string normalizedMask = ToLower(TrimCopy(cullingMask));
    if (normalizedMask.empty() || normalizedMask == "everything" || normalizedMask == "*" || normalizedMask == "all") {
        return true;
    }
    if (normalizedMask == "nothing" || normalizedMask == "none") {
        return false;
    }

    const std::string normalizedLayer = ToLower(TrimCopy(layer.empty() ? std::string{"Default"} : layer));
    for (std::string token : SplitMask(cullingMask)) {
        token = ToLower(TrimCopy(std::move(token)));
        if (token == normalizedLayer || token == "everything" || token == "all" || token == "*") {
            return true;
        }
    }
    return false;
}

std::vector<Entity> FilterCameraRaycastEntities(const std::vector<Entity>& entities, const std::string& layerMask,
                                                int ignoredEntityA, int ignoredEntityB) {
    std::vector<Entity> filtered;
    filtered.reserve(entities.size());
    for (const Entity& entity : entities) {
        if (entity.id == ignoredEntityA || entity.id == ignoredEntityB) {
            continue;
        }
        if (!CameraCullingMaskAllowsLayer(layerMask, entity.layer)) {
            continue;
        }
        filtered.push_back(entity);
    }
    return filtered;
}

} // namespace aine

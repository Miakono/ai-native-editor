#pragma once

#include "engine/physics/Physics.h"
#include "engine/scene/Component.h"
#include "engine/scene/Entity.h"

#include <array>
#include <string>
#include <vector>

namespace aine {

enum class CameraProjectionMode {
    Perspective,
    Orthographic,
};

struct CameraViewportRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

struct CameraFrame {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::array<float, 3> forward{0.0f, 0.0f, -1.0f};
    std::array<float, 3> right{1.0f, 0.0f, 0.0f};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    CameraProjectionMode projection = CameraProjectionMode::Perspective;
    float fovDegrees = 60.0f;
    float orthographicSize = 5.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    CameraViewportRect viewport;
    std::array<float, 4> clearColor{0.05f, 0.06f, 0.07f, 1.0f};
    std::string cullingMask = "Everything";
};

struct CameraRay {
    std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> direction{0.0f, 0.0f, -1.0f};
};

CameraProjectionMode CameraProjectionModeFromString(const std::string& value);
std::string CameraProjectionModeToString(CameraProjectionMode mode);

CameraViewportRect CameraViewportRectFromComponent(const Component* cameraComponent);
CameraFrame BuildCameraFrameFromEntity(const Entity& cameraEntity, const Component* cameraComponent = nullptr);

bool CameraWorldToScreen(const CameraFrame& camera, std::array<float, 3> world,
                         std::array<float, 2> viewportSize, std::array<float, 3>* outScreenDepth);
bool CameraScreenToWorld(const CameraFrame& camera, std::array<float, 2> screen, float depth,
                         std::array<float, 2> viewportSize, std::array<float, 3>* outWorld);
bool CameraScreenPointToRay(const CameraFrame& camera, std::array<float, 2> screen,
                            std::array<float, 2> viewportSize, CameraRay* outRay);
bool CameraRaycast3D(const PhysicsWorld& physicsWorld, const std::vector<Entity>& entities, const CameraFrame& camera,
                     std::array<float, 2> screen, std::array<float, 2> viewportSize,
                     PhysicsRayHit3D* outHit = nullptr);

bool CameraCullingMaskAllowsLayer(const std::string& cullingMask, const std::string& layer);
std::vector<Entity> FilterCameraRaycastEntities(const std::vector<Entity>& entities, const std::string& layerMask,
                                                int ignoredEntityA = 0, int ignoredEntityB = 0);

} // namespace aine

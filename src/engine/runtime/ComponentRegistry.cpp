#include "ComponentRegistry.h"

#include "EditorState.h"
#include "Physics.h"

#include <algorithm>
#include <sstream>

namespace aine {
namespace {

bool HasComponentProperty(const Component& component, const std::string& name) {
    return std::any_of(component.properties.begin(), component.properties.end(), [&name](const ComponentProperty& property) {
        return property.name == name;
    });
}

ComponentDefinition DefineComponent(ComponentSchema schema, Component defaults, std::string displayName,
                                    bool allowMultiple = false, bool editorVisible = true) {
    return {std::move(schema), std::move(defaults), std::move(displayName), allowMultiple, editorVisible};
}

} // namespace

Component MakeTransformComponent() {
    return {"Transform",
            {
                {"position", "0, 0, 0"},
                {"rotation", "0, 0, 0"},
                {"scale", "1, 1, 1"},
            }};
}

Component MakeCameraComponent() {
    return {"Camera",
            {
                {"enabled", "true"},
                {"projection", "Perspective"},
                {"fov", "60"},
                {"orthographicSize", "5"},
                {"near", "0.1"},
                {"far", "1000"},
                {"clearColor", "0.05, 0.06, 0.07, 1"},
                {"viewportRect", "0, 0, 1"},
                {"viewportHeight", "1"},
                {"cullingMask", "Everything"},
            }};
}

Component MakeFogComponent() {
    return {"Fog",
            {
                {"enabled", "true"},
                {"mode", "Linear"},
                {"color", "0.43, 0.52, 0.60, 1"},
                {"density", "0.035"},
                {"startDistance", "12"},
                {"endDistance", "78"},
                {"heightEnabled", "false"},
                {"heightStart", "1.2"},
                {"heightFalloff", "18"},
                {"skyBlend", "0.62"},
                {"affectSceneView", "true"},
                {"affectGameView", "true"},
            }};
}

Component MakeEnvironmentLightingComponent() {
    return {"EnvironmentLighting",
            {
                {"enabled", "true"},
                {"skyboxMode", "Gradient"},
                {"topColor", "0.18, 0.36, 0.64, 1"},
                {"horizonColor", "0.58, 0.68, 0.78, 1"},
                {"groundColor", "0.22, 0.24, 0.23, 1"},
                {"exposure", "1.0"},
                {"horizonHeight", "0.52"},
                {"ambientColor", "0.62, 0.67, 0.72, 1"},
                {"ambientIntensity", "0.45"},
                {"sunEntity", "Directional Light"},
                {"showSunDisk", "true"},
                {"sunDiskSize", "0.035"},
                {"sunDiskIntensity", "0.9"},
                {"affectSceneView", "true"},
                {"affectGameView", "true"},
            }};
}

Component MakeLightComponent(const std::string& lightType) {
    return {"Light",
            {
                {"type", lightType},
                {"intensity", "1.0"},
                {"color", "1, 0.95, 0.82"},
            }};
}

Component MakeMeshPlaceholderComponent(const std::string& meshName, const std::string& materialName) {
    return {"MeshRenderer",
            {
                {"mesh", meshName},
                {"material", materialName},
            }};
}

Component MakeSpriteRendererComponent(const std::string& textureOrSprite) {
    return {"SpriteRenderer",
            {
                {"enabled", "true"},
                {"sprite", textureOrSprite},
                {"texture", textureOrSprite},
                {"rect", ""},
                {"pixelsPerUnit", "100"},
                {"pivot", "0.5, 0.5"},
                {"color", "1, 1, 1, 1"},
                {"flipX", "false"},
                {"flipY", "false"},
                {"sortingLayer", "Default"},
                {"orderInLayer", "0"},
                {"depthMode", "SortingLayer"},
            }};
}

Component MakeSpinRuntimeComponent(float degreesPerSecond) {
    std::ostringstream stream;
    stream << degreesPerSecond;
    return {"SpinRuntime",
            {
                {"speedYDegreesPerSecond", stream.str()},
            }};
}

Component MakeCharacterControllerRuntimeComponent(float speedUnitsPerSecond, float sprintMultiplier) {
    std::ostringstream speedStream;
    speedStream << speedUnitsPerSecond;
    std::ostringstream sprintStream;
    sprintStream << sprintMultiplier;
    return {"CharacterControllerRuntime",
            {
                {"speedUnitsPerSecond", speedStream.str()},
                {"sprintMultiplier", sprintStream.str()},
                {"input", "InputActionMapRuntime.move"},
                {"plane", "XZ"},
            }};
}

Component MakeInputActionMapRuntimeComponent() {
    return {"InputActionMapRuntime",
            {
                {"move", "KeyboardMove"},
                {"sprint", "KeyboardSprint"},
                {"interact", "KeyboardInteract"},
                {"jump", "KeyboardJump"},
            }};
}

Component MakeCameraFollowRuntimeComponent(const std::string& targetName) {
    return {"CameraFollowRuntime",
            {
                {"enabled", "true"},
                {"mode", "Follow"},
                {"targetName", targetName},
                {"targetGroupNames", ""},
                {"offset", "0, 5.5, 7"},
                {"pitchDegrees", "-38"},
                {"yawDegrees", "0"},
                {"smoothTime", "0.18"},
                {"rotationSmoothTime", "0.16"},
                {"zoomSmoothTime", "0.18"},
                {"lookAheadTime", "0.18"},
                {"lookAheadMaxDistance", "1.75"},
                {"deadZoneRadius", "0.25"},
                {"softZoneRadius", "1.25"},
                {"targetFraming", "0, 0"},
                {"boundsEnabled", "false"},
                {"boundsMin", "-50, -50, -50"},
                {"boundsMax", "50, 50, 50"},
                {"distance", "7"},
                {"orbitYawDegrees", "0"},
                {"orbitPitchDegrees", "-35"},
                {"firstPersonEyeHeight", "1.65"},
                {"chaseDistance", "7"},
                {"chaseHeight", "3"},
                {"topDownHeight", "12"},
                {"isometricYawDegrees", "45"},
                {"isometricPitchDegrees", "-55"},
                {"sideScrollOffset", "0, 2, 10"},
                {"freeMoveSpeed", "6"},
                {"freeLookSensitivity", "90"},
                {"collisionAvoidance", "false"},
                {"collisionRadius", "0.25"},
                {"collisionMask", "Environment;Default"},
                {"teleportResetDistance", "20"},
                {"blendTime", "0.25"},
                {"paused", "false"},
                {"railKeyframes", ""},
                {"railLoop", "false"},
                {"debugGizmos", "true"},
                {"shakeProfile", "Medium"},
            }};
}

Component MakeGameRulesRuntimeComponent(const std::string& title, int requiredScore) {
    return {"GameRulesRuntime",
            {
                {"title", title},
                {"requiredScore", std::to_string(requiredScore)},
                {"objective", "Collect the required score and reach the goal."},
                {"fallDeathY", "-8"},
            }};
}

Component MakeCollectibleRuntimeComponent(int points, float radius) {
    std::ostringstream radiusStream;
    radiusStream << radius;
    return {"CollectibleRuntime",
            {
                {"points", std::to_string(points)},
                {"radius", radiusStream.str()},
                {"collected", "false"},
            }};
}

Component MakeGoalRuntimeComponent(int requiredScore, float radius) {
    std::ostringstream radiusStream;
    radiusStream << radius;
    return {"GoalRuntime",
            {
                {"requiredScore", std::to_string(requiredScore)},
                {"radius", radiusStream.str()},
                {"completed", "false"},
            }};
}

Component MakeHazardRuntimeComponent(float radius) {
    std::ostringstream radiusStream;
    radiusStream << radius;
    return {"HazardRuntime",
            {
                {"radius", radiusStream.str()},
                {"triggered", "false"},
            }};
}

Component MakeUICanvasRuntimeComponent() {
    return {"UICanvasRuntime",
            {
                {"enabled", "true"},
                {"anchorMin", "0, 0"},
                {"anchorMax", "1, 1"},
                {"position", "0, 0"},
                {"size", "1280, 720"},
                {"color", "0, 0, 0, 0"},
                {"visible", "true"},
                {"order", "0"},
            }};
}

Component MakeUIPanelRuntimeComponent() {
    return {"UIPanelRuntime",
            {
                {"enabled", "true"},
                {"canvasName", ""},
                {"anchorMin", "0.5, 0.5"},
                {"anchorMax", "0.5, 0.5"},
                {"position", "0, 0"},
                {"size", "420, 240"},
                {"color", "0.08, 0.10, 0.12, 0.86"},
                {"visible", "true"},
                {"order", "10"},
            }};
}

Component MakeUITextRuntimeComponent(const std::string& text) {
    return {"UITextRuntime",
            {
                {"enabled", "true"},
                {"canvasName", ""},
                {"text", text},
                {"fontSize", "24"},
                {"anchorMin", "0.5, 0.5"},
                {"anchorMax", "0.5, 0.5"},
                {"position", "0, 0"},
                {"size", "360, 48"},
                {"color", "0.92, 0.94, 0.96, 1"},
                {"visible", "true"},
                {"order", "20"},
                {"align", "center"},
            }};
}

Component MakeUIButtonRuntimeComponent(const std::string& text, const std::string& action) {
    return {"UIButtonRuntime",
            {
                {"enabled", "true"},
                {"canvasName", ""},
                {"text", text},
                {"action", action},
                {"anchorMin", "0.5, 0.5"},
                {"anchorMax", "0.5, 0.5"},
                {"position", "0, 0"},
                {"size", "220, 54"},
                {"color", "0.20, 0.44, 0.68, 0.94"},
                {"textColor", "0.95, 0.98, 1, 1"},
                {"visible", "true"},
                {"order", "30"},
            }};
}

Component MakeUIBoxRuntimeComponent() {
    return {"UIBoxRuntime",
            {
                {"enabled", "true"},
                {"canvasName", ""},
                {"image", "placeholder"},
                {"anchorMin", "0.5, 0.5"},
                {"anchorMax", "0.5, 0.5"},
                {"position", "0, 0"},
                {"size", "96, 96"},
                {"color", "0.65, 0.70, 0.78, 0.80"},
                {"visible", "true"},
                {"order", "15"},
            }};
}

Component MakeUIStateBindingRuntimeComponent(const std::string& stateKey, bool equals, bool targetVisible) {
    return {"UIStateBindingRuntime",
            {
                {"enabled", "true"},
                {"stateKey", stateKey},
                {"equals", equals ? "true" : "false"},
                {"targetVisible", targetVisible ? "true" : "false"},
            }};
}

Component MakeUILayoutGroupRuntimeComponent() {
    return {"UILayoutGroupRuntime",
            {
                {"enabled", "true"},
                {"direction", "Vertical"},
                {"spacing", "8"},
                {"padding", "12, 12, 12, 12"},
                {"childAlignment", "Center"},
                {"fitChildren", "false"},
            }};
}

Component MakeUIImageRuntimeComponent(const std::string& image) {
    return {"UIImageRuntime",
            {
                {"enabled", "true"},
                {"canvasName", ""},
                {"image", image},
                {"anchorMin", "0.5, 0.5"},
                {"anchorMax", "0.5, 0.5"},
                {"position", "0, 0"},
                {"size", "96, 96"},
                {"color", "1, 1, 1, 0.88"},
                {"visible", "true"},
                {"order", "15"},
                {"preserveAspect", "true"},
            }};
}

Component MakeUIFocusRuntimeComponent() {
    return {"UIFocusRuntime",
            {
                {"enabled", "true"},
                {"focusable", "true"},
                {"focused", "false"},
                {"navigation", "Automatic"},
                {"submitAction", "none"},
            }};
}

Component MakeUIAnimationRuntimeComponent() {
    return {"UIAnimationRuntime",
            {
                {"enabled", "true"},
                {"animation", "FadePulse"},
                {"duration", "1"},
                {"speed", "1"},
                {"loop", "true"},
                {"currentTime", "0"},
                {"normalizedTime", "0"},
            }};
}

Component MakeUIScriptCallbackRuntimeComponent(const std::string& eventName) {
    return {"UIScriptCallbackRuntime",
            {
                {"enabled", "true"},
                {"event", eventName},
                {"scriptMethod", "OnRuntimeUiEvent"},
                {"payload", ""},
            }};
}

Component MakeAudioSourceRuntimeComponent(const std::string& clip) {
    return {"AudioSourceRuntime",
            {
                {"enabled", "true"},
                {"clip", clip},
                {"eventId", ""},
                {"playOnStart", "true"},
                {"loop", "false"},
                {"volume", "1"},
                {"played", "false"},
            }};
}

Component MakeAnimatorRuntimeComponent(const std::string& stateName) {
    return {"AnimatorRuntime",
            {
                {"enabled", "true"},
                {"state", stateName},
                {"duration", "1"},
                {"speed", "1"},
                {"loop", "true"},
                {"currentTime", "0"},
            }};
}

Component MakeSpriteAnimationRuntimeComponent() {
    return {"SpriteAnimationRuntime",
            {
                {"enabled", "true"},
                {"frames", ""},
                {"frameRate", "12"},
                {"loop", "true"},
                {"playOnStart", "true"},
                {"currentTime", "0"},
                {"currentFrame", "0"},
            }};
}

Component MakeNavigationAgentRuntimeComponent(const std::string& targetName) {
    return {"NavigationAgentRuntime",
            {
                {"enabled", "true"},
                {"targetName", targetName},
                {"speed", "2.5"},
                {"stoppingDistance", "0.1"},
                {"reached", "false"},
            }};
}

Component MakeNetworkIdentityRuntimeComponent(const std::string& networkId) {
    return {"NetworkIdentityRuntime",
            {
                {"enabled", "true"},
                {"networkId", networkId},
                {"authority", "Server"},
                {"replicateTransform", "true"},
                {"registered", "false"},
                {"lastReplicatedTime", "0"},
            }};
}

Component MakeNetworkSessionRuntimeComponent() {
    return {"NetworkSessionRuntime",
            {
                {"enabled", "true"},
                {"mode", "LocalLoopback"},
                {"status", "Disconnected"},
                {"tickRate", "30"},
                {"tick", "0"},
            }};
}

const std::vector<ComponentDefinition>& ComponentRegistry() {
    static const std::vector<ComponentDefinition> definitions{
        DefineComponent({"Transform",
                         "Core",
                         {
                             {"position", "vec3", true},
                             {"rotation", "vec3", true},
                             {"scale", "vec3", true},
                         }},
                        MakeTransformComponent(), "Transform"),
        DefineComponent({"Camera",
                         "Rendering",
                         {
                             {"enabled", "bool", false},
                             {"projection", "string", false},
                             {"fov", "positive_float", false},
                             {"orthographicSize", "positive_float", false},
                             {"near", "positive_float", false},
                             {"far", "positive_float", false},
                             {"clearColor", "color", false},
                             {"viewportRect", "vec3", false},
                             {"viewportHeight", "positive_float", false},
                             {"cullingMask", "string", false},
                         }},
                        MakeCameraComponent(), "Camera"),
        DefineComponent({"Fog",
                         "Rendering",
                         {
                             {"enabled", "bool", false},
                             {"mode", "string", false},
                             {"color", "color", false},
                             {"density", "positive_float", false},
                             {"startDistance", "nonnegative_float", false},
                             {"endDistance", "positive_float", false},
                             {"heightEnabled", "bool", false},
                             {"heightStart", "float", false},
                             {"heightFalloff", "positive_float", false},
                             {"skyBlend", "normalized_float", false},
                             {"affectSceneView", "bool", false},
                             {"affectGameView", "bool", false},
                         }},
                        MakeFogComponent(), "Fog"),
        DefineComponent({"EnvironmentLighting",
                         "Rendering",
                         {
                             {"enabled", "bool", false},
                             {"skyboxMode", "string", false},
                             {"topColor", "color", false},
                             {"horizonColor", "color", false},
                             {"groundColor", "color", false},
                             {"exposure", "positive_float", false},
                             {"horizonHeight", "normalized_float", false},
                             {"ambientColor", "color", false},
                             {"ambientIntensity", "nonnegative_float", false},
                             {"sunEntity", "string", false},
                             {"showSunDisk", "bool", false},
                             {"sunDiskSize", "normalized_float", false},
                             {"sunDiskIntensity", "nonnegative_float", false},
                             {"affectSceneView", "bool", false},
                             {"affectGameView", "bool", false},
                         }},
                        MakeEnvironmentLightingComponent(), "Environment Lighting"),
        DefineComponent({"Light",
                         "Rendering",
                         {
                             {"type", "string", true},
                             {"intensity", "positive_float", true},
                             {"color", "color", true},
                         }},
                        MakeLightComponent("Point"), "Light"),
        DefineComponent({"MeshRenderer",
                         "Rendering",
                         {
                             {"mesh", "string", true},
                             {"material", "string", true},
                         }},
                        MakeMeshPlaceholderComponent("UnitCube"), "Mesh Renderer"),
        DefineComponent({"SpriteRenderer",
                         "Rendering",
                         {
                             {"enabled", "bool", false},
                             {"sprite", "string", false},
                             {"texture", "string", false},
                             {"rect", "string", false},
                             {"pixelsPerUnit", "positive_float", true},
                             {"pivot", "anchor", true},
                             {"color", "color", true},
                             {"flipX", "bool", false},
                             {"flipY", "bool", false},
                             {"sortingLayer", "string", true},
                             {"orderInLayer", "int", true},
                             {"depthMode", "string", false},
                         }},
                        MakeSpriteRendererComponent(), "Sprite Renderer"),
        DefineComponent({"Terrain",
                         "Terrain",
                         {
                             {"version", "int", true},
                             {"backend", "string", true},
                             {"resolution", "int", true},
                             {"size", "vec3", true},
                             {"chunkSize", "int", true},
                             {"collisionEnabled", "bool", true},
                             {"volumeEnabled", "bool", false},
                             {"editRevision", "int", true},
                             {"heights", "string", true},
                             {"layers", "string", true},
                             {"weights", "string", true},
                             {"holes", "string", true},
                             {"dirtyChunks", "string", false},
                             {"volumeVersion", "int", false},
                             {"volumeResolution", "vec3", false},
                             {"volumeSize", "vec3", false},
                             {"volumeChunkSize", "int", false},
                             {"volumeCollisionEnabled", "bool", false},
                             {"volumeCollisionUpdateMode", "string", false},
                             {"volumeEditRevision", "int", false},
                             {"volumeDensities", "string", false},
                             {"volumeLayers", "string", false},
                             {"volumeWeights", "string", false},
                             {"volumeDirtyChunks", "string", false},
                         }},
                        MakeTerrainComponent(), "Terrain"),
        DefineComponent({"CaveVolume",
                         "Terrain",
                         {
                             {"version", "int", true},
                             {"resolution", "vec3", true},
                             {"size", "vec3", true},
                             {"chunkSize", "int", true},
                             {"collisionEnabled", "bool", true},
                             {"collisionUpdateMode", "string", false},
                             {"editRevision", "int", true},
                             {"densities", "string", true},
                             {"layers", "string", true},
                             {"weights", "string", true},
                             {"dirtyChunks", "string", false},
                         }},
                        MakeCaveVolumeComponent(), "Legacy Cave Volume", false, false),
        DefineComponent({"SpinRuntime",
                         "Runtime",
                         {
                             {"speedYDegreesPerSecond", "float", true},
                         }},
                        MakeSpinRuntimeComponent(), "SpinRuntime"),
        DefineComponent({"CharacterControllerRuntime",
                         "Runtime",
                         {
                             {"speedUnitsPerSecond", "positive_float", true},
                             {"sprintMultiplier", "positive_float", true},
                             {"input", "string", false},
                             {"plane", "string", false},
                         }},
                        MakeCharacterControllerRuntimeComponent(), "CharacterControllerRuntime"),
        DefineComponent({"InputActionMapRuntime",
                         "Runtime",
                         {
                             {"move", "string", true},
                             {"sprint", "string", true},
                             {"interact", "string", true},
                             {"jump", "string", true},
                         }},
                        MakeInputActionMapRuntimeComponent(), "InputActionMapRuntime"),
        DefineComponent({"CameraFollowRuntime",
                         "Runtime",
                         {
                             {"enabled", "bool", false},
                             {"mode", "string", false},
                             {"targetName", "string", true},
                             {"targetGroupNames", "string", false},
                             {"offset", "vec3", true},
                             {"pitchDegrees", "float", true},
                             {"yawDegrees", "float", true},
                             {"smoothTime", "float", false},
                             {"rotationSmoothTime", "float", false},
                             {"zoomSmoothTime", "float", false},
                             {"lookAheadTime", "float", false},
                             {"lookAheadMaxDistance", "float", false},
                             {"deadZoneRadius", "float", false},
                             {"softZoneRadius", "float", false},
                             {"targetFraming", "vec2", false},
                             {"boundsEnabled", "bool", false},
                             {"boundsMin", "vec3", false},
                             {"boundsMax", "vec3", false},
                             {"distance", "positive_float", false},
                             {"orbitYawDegrees", "float", false},
                             {"orbitPitchDegrees", "float", false},
                             {"firstPersonEyeHeight", "float", false},
                             {"chaseDistance", "positive_float", false},
                             {"chaseHeight", "float", false},
                             {"topDownHeight", "positive_float", false},
                             {"isometricYawDegrees", "float", false},
                             {"isometricPitchDegrees", "float", false},
                             {"sideScrollOffset", "vec3", false},
                             {"freeMoveSpeed", "positive_float", false},
                             {"freeLookSensitivity", "positive_float", false},
                             {"collisionAvoidance", "bool", false},
                             {"collisionRadius", "positive_float", false},
                             {"collisionMask", "string", false},
                             {"teleportResetDistance", "float", false},
                             {"blendTime", "float", false},
                             {"paused", "bool", false},
                             {"railKeyframes", "string", false},
                             {"railLoop", "bool", false},
                             {"debugGizmos", "bool", false},
                             {"shakeProfile", "string", false},
                         }},
                        MakeCameraFollowRuntimeComponent("Player"), "Camera Controller"),
        DefineComponent({"GameRulesRuntime",
                         "Runtime",
                         {
                             {"title", "string", true},
                             {"requiredScore", "nonnegative_int", true},
                             {"objective", "string", true},
                             {"fallDeathY", "float", false},
                         }},
                        MakeGameRulesRuntimeComponent("Game Rules", 3), "GameRulesRuntime"),
        DefineComponent({"CollectibleRuntime",
                         "Runtime",
                         {
                             {"points", "nonnegative_int", true},
                             {"radius", "positive_float", true},
                             {"collected", "bool", true},
                         }},
                        MakeCollectibleRuntimeComponent(), "CollectibleRuntime"),
        DefineComponent({"GoalRuntime",
                         "Runtime",
                         {
                             {"requiredScore", "nonnegative_int", true},
                             {"radius", "positive_float", true},
                             {"completed", "bool", true},
                         }},
                        MakeGoalRuntimeComponent(), "GoalRuntime"),
        DefineComponent({"HazardRuntime",
                         "Runtime",
                         {
                             {"radius", "positive_float", true},
                             {"triggered", "bool", true},
                         }},
                        MakeHazardRuntimeComponent(), "HazardRuntime"),
        DefineComponent({"UICanvasRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"anchorMin", "anchor", true},
                             {"anchorMax", "anchor", true},
                             {"position", "vec2", true},
                             {"size", "size2", true},
                             {"color", "color", true},
                             {"visible", "bool", true},
                             {"order", "int", true},
                         }},
                        MakeUICanvasRuntimeComponent(), "UI Canvas"),
        DefineComponent({"UIPanelRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"canvasName", "string", false},
                             {"anchorMin", "anchor", true},
                             {"anchorMax", "anchor", true},
                             {"position", "vec2", true},
                             {"size", "size2", true},
                             {"color", "color", true},
                             {"visible", "bool", true},
                             {"order", "int", true},
                         }},
                        MakeUIPanelRuntimeComponent(), "UI Panel"),
        DefineComponent({"UITextRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"canvasName", "string", false},
                             {"text", "string", true},
                             {"fontSize", "positive_float", true},
                             {"anchorMin", "anchor", true},
                             {"anchorMax", "anchor", true},
                             {"position", "vec2", true},
                             {"size", "size2", true},
                             {"color", "color", true},
                             {"visible", "bool", true},
                             {"order", "int", true},
                             {"align", "string", false},
                         }},
                        MakeUITextRuntimeComponent(), "UI Text"),
        DefineComponent({"UIButtonRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"canvasName", "string", false},
                             {"text", "string", true},
                             {"action", "string", true},
                             {"anchorMin", "anchor", true},
                             {"anchorMax", "anchor", true},
                             {"position", "vec2", true},
                             {"size", "size2", true},
                             {"color", "color", true},
                             {"textColor", "color", true},
                             {"visible", "bool", true},
                             {"order", "int", true},
                         }},
                        MakeUIButtonRuntimeComponent(), "UI Button"),
        DefineComponent({"UIBoxRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"canvasName", "string", false},
                             {"image", "string", false},
                             {"anchorMin", "anchor", true},
                             {"anchorMax", "anchor", true},
                             {"position", "vec2", true},
                             {"size", "size2", true},
                             {"color", "color", true},
                             {"visible", "bool", true},
                             {"order", "int", true},
                         }},
                        MakeUIBoxRuntimeComponent(), "UI Box / Image"),
        DefineComponent({"UIStateBindingRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"stateKey", "string", true},
                             {"equals", "bool", true},
                             {"targetVisible", "bool", true},
                         }},
                        MakeUIStateBindingRuntimeComponent(), "UI State Binding"),
        DefineComponent({"UILayoutGroupRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"direction", "string", true},
                             {"spacing", "float", true},
                             {"padding", "string", false},
                             {"childAlignment", "string", false},
                             {"fitChildren", "bool", false},
                         }},
                        MakeUILayoutGroupRuntimeComponent(), "UI Layout Group"),
        DefineComponent({"UIImageRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"canvasName", "string", false},
                             {"image", "string", true},
                             {"anchorMin", "anchor", true},
                             {"anchorMax", "anchor", true},
                             {"position", "vec2", true},
                             {"size", "size2", true},
                             {"color", "color", true},
                             {"visible", "bool", true},
                             {"order", "int", true},
                             {"preserveAspect", "bool", false},
                         }},
                        MakeUIImageRuntimeComponent(), "UI Image"),
        DefineComponent({"UIFocusRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"focusable", "bool", true},
                             {"focused", "bool", true},
                             {"navigation", "string", false},
                             {"submitAction", "string", false},
                         }},
                        MakeUIFocusRuntimeComponent(), "UI Focus"),
        DefineComponent({"UIAnimationRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"animation", "string", true},
                             {"duration", "positive_float", true},
                             {"speed", "float", true},
                             {"loop", "bool", true},
                             {"currentTime", "float", true},
                             {"normalizedTime", "float", true},
                         }},
                        MakeUIAnimationRuntimeComponent(), "UI Animation"),
        DefineComponent({"UIScriptCallbackRuntime",
                         "UI",
                         {
                             {"enabled", "bool", true},
                             {"event", "string", true},
                             {"scriptMethod", "string", true},
                             {"payload", "string", false},
                         }},
                        MakeUIScriptCallbackRuntimeComponent(), "UI Script Callback"),
        DefineComponent({"AudioSourceRuntime",
                         "Runtime",
                         {
                             {"enabled", "bool", true},
                             {"clip", "string", true},
                             {"eventId", "string", false},
                             {"playOnStart", "bool", true},
                             {"loop", "bool", true},
                             {"volume", "positive_float", true},
                             {"played", "bool", true},
                         }},
                        MakeAudioSourceRuntimeComponent(), "Audio Source Runtime"),
        DefineComponent({"AnimatorRuntime",
                         "Runtime",
                         {
                             {"enabled", "bool", true},
                             {"state", "string", true},
                             {"duration", "positive_float", true},
                             {"speed", "float", true},
                             {"loop", "bool", true},
                             {"currentTime", "float", true},
                         }},
                        MakeAnimatorRuntimeComponent(), "Animator Runtime"),
        DefineComponent({"SpriteAnimationRuntime",
                         "Runtime",
                         {
                             {"enabled", "bool", true},
                             {"frames", "string", false},
                             {"frameRate", "positive_float", true},
                             {"loop", "bool", true},
                             {"playOnStart", "bool", false},
                             {"currentTime", "float", true},
                             {"currentFrame", "nonnegative_int", true},
                         }},
                        MakeSpriteAnimationRuntimeComponent(), "Sprite Animation Runtime"),
        DefineComponent({"NavigationAgentRuntime",
                         "Runtime",
                         {
                             {"enabled", "bool", true},
                             {"targetName", "string", true},
                             {"speed", "positive_float", true},
                             {"stoppingDistance", "float", true},
                             {"reached", "bool", true},
                         }},
                        MakeNavigationAgentRuntimeComponent(), "Navigation Agent Runtime"),
        DefineComponent({"NetworkIdentityRuntime",
                         "Runtime",
                         {
                             {"enabled", "bool", true},
                             {"networkId", "string", true},
                             {"authority", "string", true},
                             {"replicateTransform", "bool", true},
                             {"registered", "bool", true},
                             {"lastReplicatedTime", "float", true},
                         }},
                        MakeNetworkIdentityRuntimeComponent(), "Network Identity Runtime"),
        DefineComponent({"NetworkSessionRuntime",
                         "Runtime",
                         {
                             {"enabled", "bool", true},
                             {"mode", "string", true},
                             {"status", "string", true},
                             {"tickRate", "positive_float", true},
                             {"tick", "nonnegative_int", true},
                         }},
                        MakeNetworkSessionRuntimeComponent(), "Network Session Runtime"),
        DefineComponent({"PhysicsSettings",
                         "Physics",
                         {
                             {"enabled", "bool", true},
                             {"fixedDeltaSeconds", "positive_float", true},
                             {"solverIterations", "int", true},
                             {"gravity2D", "vec2", true},
                             {"gravity3D", "vec3", true},
                         }},
                        MakePhysicsSettingsComponent(), "Physics Settings"),
        DefineComponent({"Rigidbody2D",
                         "Physics",
                         {
                             {"bodyType", "string", true},
                             {"mass", "positive_float", false},
                             {"velocity", "vec2", false},
                             {"angularVelocity", "float", false},
                             {"damping", "float", false},
                             {"restitution", "float", false},
                             {"friction", "float", false},
                             {"gravityScale", "float", false},
                             {"useGravity", "bool", false},
                         }},
                        MakeRigidbody2DComponent(), "Rigidbody 2D"),
        DefineComponent({"Collider2D",
                         "Physics",
                         {
                             {"shape", "string", true},
                             {"size", "vec2", false},
                             {"radius", "positive_float", false},
                             {"offset", "vec2", false},
                             {"trigger", "bool", false},
                         }},
                        MakeCollider2DComponent(), "Collider 2D"),
        DefineComponent({"Rigidbody3D",
                         "Physics",
                         {
                             {"bodyType", "string", true},
                             {"mass", "positive_float", false},
                             {"velocity", "vec3", false},
                             {"angularVelocity", "vec3", false},
                             {"damping", "float", false},
                             {"restitution", "float", false},
                             {"friction", "float", false},
                             {"gravityScale", "float", false},
                             {"useGravity", "bool", false},
                         }},
                        MakeRigidbody3DComponent(), "Rigidbody 3D"),
        DefineComponent({"Collider3D",
                         "Physics",
                         {
                             {"shape", "string", true},
                             {"size", "vec3", false},
                             {"radius", "positive_float", false},
                             {"offset", "vec3", false},
                             {"trigger", "bool", false},
                         }},
                        MakeCollider3DComponent(), "Collider 3D"),
    };
    return definitions;
}

const ComponentDefinition* FindComponentDefinition(const std::string& type) {
    const std::vector<ComponentDefinition>& definitions = ComponentRegistry();
    auto it = std::find_if(definitions.begin(), definitions.end(), [&type](const ComponentDefinition& definition) {
        return definition.schema.type == type;
    });
    return it == definitions.end() ? nullptr : &(*it);
}

const std::vector<ComponentSchema>& RuntimeComponentSchemas() {
    static const std::vector<ComponentSchema> schemas = [] {
        std::vector<ComponentSchema> result;
        for (const ComponentDefinition& definition : ComponentRegistry()) {
            result.push_back(definition.schema);
        }
        return result;
    }();
    return schemas;
}

Component MakeDefaultComponentByType(const std::string& type) {
    const ComponentDefinition* definition = FindComponentDefinition(type);
    return definition == nullptr ? Component{type, {}} : definition->defaultComponent;
}

bool MergeComponentDefaults(Component& component) {
    const ComponentDefinition* definition = FindComponentDefinition(component.type);
    if (definition == nullptr) {
        return false;
    }

    bool changed = false;
    for (const ComponentProperty& property : definition->defaultComponent.properties) {
        if (!HasComponentProperty(component, property.name)) {
            component.properties.push_back(property);
            changed = true;
        }
    }
    return changed;
}

bool IsKnownComponentType(const std::string& type) {
    return FindComponentDefinition(type) != nullptr;
}

bool RunComponentRegistrySelfTests(std::vector<std::string>* diagnostics) {
    bool ok = true;
    const std::vector<ComponentDefinition>& definitions = ComponentRegistry();
    if (definitions.empty()) {
        ok = false;
        if (diagnostics != nullptr) {
            diagnostics->push_back("Component registry selftest failed: registry is empty.");
        }
    }

    std::vector<std::string> types;
    for (const ComponentDefinition& definition : definitions) {
        if (definition.schema.type.empty()) {
            ok = false;
            if (diagnostics != nullptr) {
                diagnostics->push_back("Component registry selftest failed: component type is empty.");
            }
            continue;
        }
        if (std::find(types.begin(), types.end(), definition.schema.type) != types.end()) {
            ok = false;
            if (diagnostics != nullptr) {
                diagnostics->push_back("Component registry selftest failed: duplicate component type " +
                                       definition.schema.type + ".");
            }
        }
        types.push_back(definition.schema.type);
        if (definition.defaultComponent.type != definition.schema.type) {
            ok = false;
            if (diagnostics != nullptr) {
                diagnostics->push_back("Component registry selftest failed: default component mismatch for " +
                                       definition.schema.type + ".");
            }
        }
        for (const ComponentSchemaProperty& property : definition.schema.properties) {
            if (property.required && !HasComponentProperty(definition.defaultComponent, property.name)) {
                ok = false;
                if (diagnostics != nullptr) {
                    diagnostics->push_back("Component registry selftest failed: " + definition.schema.type +
                                           " default is missing required property " + property.name + ".");
                }
            }
        }
    }

    const std::vector<std::string> requiredTypes{
        "Transform",
        "Camera",
        "Fog",
        "MeshRenderer",
        "Terrain",
        "CaveVolume",
        "Rigidbody3D",
        "Collider3D",
        "UICanvasRuntime",
        "UIPanelRuntime",
        "UITextRuntime",
        "UIButtonRuntime",
        "UIBoxRuntime",
        "UIStateBindingRuntime",
        "UILayoutGroupRuntime",
        "UIImageRuntime",
        "UIFocusRuntime",
        "UIAnimationRuntime",
        "UIScriptCallbackRuntime",
        "AudioSourceRuntime",
        "AnimatorRuntime",
        "NavigationAgentRuntime",
        "NetworkIdentityRuntime",
        "NetworkSessionRuntime",
    };
    for (const std::string& type : requiredTypes) {
        if (!IsKnownComponentType(type)) {
            ok = false;
            if (diagnostics != nullptr) {
                diagnostics->push_back("Component registry selftest failed: required type missing: " + type + ".");
            }
        }
    }
    if (ok && diagnostics != nullptr) {
        diagnostics->push_back("component registry selftest passed: " + std::to_string(definitions.size()) +
                               " component definition(s).");
    }
    return ok;
}

} // namespace aine

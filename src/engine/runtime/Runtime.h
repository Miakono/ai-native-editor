#pragma once

#include "engine/core/Log.h"
#include "engine/core/PerformanceProfiler.h"
#include "engine/physics/Physics.h"
#include "engine/runtime/Camera.h"
#include "engine/scene/Entity.h"
#include "engine/scripting/ScriptRuntimeHost.h"

#include <array>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace aine {

enum class EditorPlayState {
    EditMode,
    Playing,
    Paused
};

struct RuntimeLogEntry {
    LogLevel level = LogLevel::Info;
    float elapsedSeconds = 0.0f;
    std::string event;
    std::string status;
    std::string detail;
};

struct CharacterControllerInput {
    float moveX = 0.0f;
    float moveZ = 0.0f;
    bool sprint = false;
};

struct RuntimeInputAction {
    std::string name;
    std::string binding;
    float value = 0.0f;
    bool active = false;
};

struct RuntimeInputState {
    CharacterControllerInput character;
    bool captured = false;
    std::string captureSurface;
    std::vector<RuntimeInputAction> actions;
};

struct RuntimeHostCallbacks {
    std::function<void(Entity&)> syncTransform;
    std::function<void(LogLevel, std::string)> editorLog;
    std::function<void(std::string, std::string, std::string)> activity;
    PerformanceProfiler* profiler = nullptr;
};

struct CameraEffectRequest {
    std::string cameraName;
    std::string profileName;
    std::array<float, 3> positionImpulse{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotationImpulse{0.0f, 0.0f, 0.0f};
    float amplitude = 0.0f;
    float duration = 0.0f;
    float frequency = 0.0f;
    bool snap = false;
    bool pauseControlSet = false;
    bool pauseControlValue = false;
    std::string cutTargetName;
    float cutHoldSeconds = 0.0f;
};

class EngineRuntime {
public:
    bool Begin(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks = {},
               const ScriptRuntimeHostConfig& scriptHostConfig = {});
    bool Pause(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks = {});
    bool Resume(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks = {});
    bool Stop(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks = {});
    void Update(std::vector<Entity>& entities, float deltaSeconds, const RuntimeHostCallbacks& callbacks = {});
    bool StepPhysicsOnce(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks = {});

    void ResetForPlay(std::vector<Entity>& entities);
    void SetCharacterControllerInput(CharacterControllerInput input);
    void SetRuntimeInputState(RuntimeInputState input);
    void RequestCameraShake(std::string cameraName = {}, std::string profileName = "Medium");
    void RequestCameraImpulse(std::string cameraName, std::array<float, 3> positionImpulse,
                              std::array<float, 3> rotationImpulse, float durationSeconds = 0.18f);
    void RequestCameraRecoil(std::string cameraName = {}, float pitchDegrees = -2.0f, float distance = 0.08f);
    void RequestHitCameraShake(std::string cameraName = {});
    void RequestCameraCut(std::string cameraName, std::string targetName, float holdSeconds = 0.0f);
    void SnapCamera(std::string cameraName = {});
    void SetCameraControlPaused(std::string cameraName, bool paused);
    bool WorldToScreen(const std::vector<Entity>& entities, const std::string& cameraName, std::array<float, 3> world,
                       std::array<float, 2> viewportSize, std::array<float, 3>* outScreenDepth) const;
    bool ScreenToWorld(const std::vector<Entity>& entities, const std::string& cameraName, std::array<float, 2> screen,
                       float depth, std::array<float, 2> viewportSize, std::array<float, 3>* outWorld) const;
    bool ScreenPointToRay(const std::vector<Entity>& entities, const std::string& cameraName,
                          std::array<float, 2> screen, std::array<float, 2> viewportSize, CameraRay* outRay) const;
    bool RaycastFromCamera(const std::vector<Entity>& entities, const std::string& cameraName,
                           std::array<float, 2> screen, std::array<float, 2> viewportSize,
                           PhysicsRayHit3D* outHit = nullptr) const;

    EditorPlayState PlayState() const { return playState_; }
    float ElapsedSeconds() const { return playElapsedSeconds_; }
    int Score() const { return playScore_; }
    bool GoalReached() const { return playGoalReached_; }
    bool Failed() const { return playFailed_; }
    int ControlledEntityId() const { return controlledRuntimeEntityId_; }
    const std::string& Status() const { return runtimeStatus_; }
    const std::string& LastSystemSummary() const { return lastRuntimeSystemSummary_; }
    const RuntimeInputState& Input() const { return runtimeInput_; }
    const std::vector<RuntimeLogEntry>& Logs() const { return runtimeLogs_; }
    const PhysicsWorld& Physics() const { return physicsWorld_; }
    const std::vector<PhysicsEvent>& PhysicsEvents() const { return physicsWorld_.LastEvents(); }

private:
    struct CameraRuntimeState {
        bool initialized = false;
        bool snapNextUpdate = false;
        bool pausedByApi = false;
        std::string lastMode;
        std::string lastTargetName;
        std::string cutTargetName;
        float cutRemainingSeconds = 0.0f;
        std::array<float, 3> focusPoint{0.0f, 0.0f, 0.0f};
        std::array<float, 3> lastTargetPosition{0.0f, 0.0f, 0.0f};
        std::array<float, 3> targetVelocity{0.0f, 0.0f, 0.0f};
        std::array<float, 3> currentPosition{0.0f, 0.0f, 0.0f};
        std::array<float, 3> currentRotation{0.0f, 0.0f, 0.0f};
        float currentDistance = 7.0f;
        float currentOrthographicSize = 5.0f;
        float shakeElapsed = 0.0f;
        float shakeDuration = 0.0f;
        float shakeAmplitude = 0.0f;
        float shakeFrequency = 0.0f;
        std::array<float, 3> positionImpulse{0.0f, 0.0f, 0.0f};
        std::array<float, 3> rotationImpulse{0.0f, 0.0f, 0.0f};
        float impulseDuration = 0.0f;
        float impulseElapsed = 0.0f;
    };

    void RunSpinRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds, const RuntimeHostCallbacks& callbacks);
    void RunCharacterControllerSystem(std::vector<Entity>& entities, float deltaSeconds, const RuntimeHostCallbacks& callbacks);
    void RunFallDeathRuntimeSystem(const std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks);
    void RunTriggerRuntimeSystem(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks);
    void RunUIStateBindingRuntimeSystem(std::vector<Entity>& entities);
    void RunUIFocusRuntimeSystem(std::vector<Entity>& entities);
    void RunUIAnimationRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds);
    void RunAudioRuntimeSystem(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks);
    void RunAnimationRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds);
    void RunSpriteAnimationRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds);
    void RunNavigationRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds,
                                    const RuntimeHostCallbacks& callbacks);
    void RunNetworkingRuntimeSystem(std::vector<Entity>& entities, const RuntimeHostCallbacks& callbacks);
    void RunCameraFollowRuntimeSystem(std::vector<Entity>& entities, float deltaSeconds,
                                      const RuntimeHostCallbacks& callbacks);
    bool ConsumeScriptHostResult(const ScriptRuntimeHostResult& result, const RuntimeHostCallbacks& callbacks,
                                 bool blockOnHostFailure);
    void AddRuntimeLog(LogLevel level, std::string event, std::string status, std::string detail);

    float playElapsedSeconds_ = 0.0f;
    int playScore_ = 0;
    bool playGoalReached_ = false;
    bool playFailed_ = false;
    int controlledRuntimeEntityId_ = 0;
    std::string runtimeStatus_ = "Edit Mode";
    std::string lastRuntimeSystemSummary_;
    EditorPlayState playState_ = EditorPlayState::EditMode;
    CharacterControllerInput characterControllerInput_;
    RuntimeInputState runtimeInput_;
    PhysicsWorld physicsWorld_;
    ScriptRuntimeHost scriptRuntimeHost_;
    std::vector<RuntimeLogEntry> runtimeLogs_;
    std::vector<std::string> runtimePhysicsEventKeys_;
    std::unordered_map<int, CameraRuntimeState> cameraStates_;
    std::vector<CameraEffectRequest> pendingCameraEffects_;
};

const char* PlayStateLabel(EditorPlayState state);

} // namespace aine

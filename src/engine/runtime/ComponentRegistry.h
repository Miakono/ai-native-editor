#pragma once

#include "engine/scene/Component.h"
#include "engine/cave/CaveVolume.h"
#include "engine/terrain/Terrain.h"

#include <string>
#include <vector>

namespace aine {

const std::vector<ComponentDefinition>& ComponentRegistry();
const ComponentDefinition* FindComponentDefinition(const std::string& type);
const std::vector<ComponentSchema>& RuntimeComponentSchemas();
Component MakeDefaultComponentByType(const std::string& type);
bool MergeComponentDefaults(Component& component);
bool IsKnownComponentType(const std::string& type);
bool RunComponentRegistrySelfTests(std::vector<std::string>* diagnostics);

Component MakeTransformComponent();
Component MakeCameraComponent();
Component MakeFogComponent();
Component MakeEnvironmentLightingComponent();
Component MakeLightComponent(const std::string& lightType);
Component MakeMeshPlaceholderComponent(const std::string& meshName, const std::string& materialName = "PrototypeGray");
Component MakeSpriteRendererComponent(const std::string& textureOrSprite = "");
Component MakeSpinRuntimeComponent(float degreesPerSecond = 90.0f);
Component MakeCharacterControllerRuntimeComponent(float speedUnitsPerSecond = 3.25f, float sprintMultiplier = 1.75f);
Component MakeInputActionMapRuntimeComponent();
Component MakeCameraFollowRuntimeComponent(const std::string& targetName = "Player");
Component MakeGameRulesRuntimeComponent(const std::string& title, int requiredScore);
Component MakeCollectibleRuntimeComponent(int points = 1, float radius = 0.65f);
Component MakeGoalRuntimeComponent(int requiredScore = 3, float radius = 0.9f);
Component MakeHazardRuntimeComponent(float radius = 0.55f);
Component MakeUICanvasRuntimeComponent();
Component MakeUIPanelRuntimeComponent();
Component MakeUITextRuntimeComponent(const std::string& text = "Text");
Component MakeUIButtonRuntimeComponent(const std::string& text = "Button", const std::string& action = "none");
Component MakeUIBoxRuntimeComponent();
Component MakeUIStateBindingRuntimeComponent(const std::string& stateKey = "playFailed", bool equals = true,
                                             bool targetVisible = true);
Component MakeUILayoutGroupRuntimeComponent();
Component MakeUIImageRuntimeComponent(const std::string& image = "placeholder");
Component MakeUIFocusRuntimeComponent();
Component MakeUIAnimationRuntimeComponent();
Component MakeUIScriptCallbackRuntimeComponent(const std::string& eventName = "OnClick");
Component MakeAudioSourceRuntimeComponent(const std::string& clip = "AudioClip");
Component MakeAnimatorRuntimeComponent(const std::string& stateName = "Idle");
Component MakeSpriteAnimationRuntimeComponent();
Component MakeNavigationAgentRuntimeComponent(const std::string& targetName = "Target");
Component MakeNetworkIdentityRuntimeComponent(const std::string& networkId = "net-entity");
Component MakeNetworkSessionRuntimeComponent();

} // namespace aine

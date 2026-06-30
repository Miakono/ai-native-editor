#include "ToolGateway.h"

#include "Physics.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace aine {
namespace {

using json = nlohmann::json;

json Vec3ToJson(const std::array<float, 3>& value) {
    return json::array({value[0], value[1], value[2]});
}

json ComponentToJson(const Component& component) {
    json properties = json::object();
    for (const ComponentProperty& property : component.properties) {
        properties[property.name] = property.value;
    }

    return json{
        {"type", component.type},
        {"properties", properties},
    };
}

std::string Trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(),
                value.end());
    return value;
}

std::string JoinStrings(const std::vector<std::string>& values, const std::string& separator) {
    std::ostringstream stream;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            stream << separator;
        }
        stream << values[i];
    }
    return stream.str();
}

void AppendUnique(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

Component WithProperty(Component component, const std::string& name, const std::string& value) {
    for (ComponentProperty& property : component.properties) {
        if (property.name == name) {
            property.value = value;
            return component;
        }
    }
    component.properties.push_back({name, value});
    return component;
}

bool HasComponentProperty(const Component& component, const std::string& name) {
    return std::any_of(component.properties.begin(), component.properties.end(), [&name](const ComponentProperty& property) {
        return property.name == name;
    });
}

void MergeMissingComponentProperties(Component& component, const Component& defaults) {
    for (const ComponentProperty& property : defaults.properties) {
        if (!HasComponentProperty(component, property.name)) {
            component.properties.push_back(property);
        }
    }
}

bool ComponentLooksKnownOrScript(const Component& component) {
    if (IsKnownComponentType(component.type)) {
        return true;
    }
    return HasComponentProperty(component, "scriptPath");
}

void NormalizeProviderComponentDefaults(Component& component) {
    const Component defaults = MakeDefaultComponentByType(component.type);
    if (!defaults.type.empty()) {
        MergeMissingComponentProperties(component, defaults);
    }
}

void NormalizeProviderCommandDefaults(ToolCommand& command) {
    if (command.type == "scene.addComponent") {
        NormalizeProviderComponentDefaults(command.component);
    } else if (command.type == "scene.createEntity") {
        for (Component& component : command.components) {
            NormalizeProviderComponentDefaults(component);
        }
    }
}

bool ContainsToken(const std::string& value, const std::string& token) {
    return value.find(token) != std::string::npos;
}

bool HasComponentType(const Entity& entity, const std::string& componentType) {
    return std::any_of(entity.components.begin(), entity.components.end(), [&componentType](const Component& component) {
        return component.type == componentType;
    });
}

bool IsSceneUndoCommand(const std::string& type) {
    return type == "scene.create" || type == "scene.open" || type == "scene.createEntity" ||
           type == "scene.deleteEntity" || type == "scene.renameEntity" || type == "scene.setParent" ||
           type == "scene.setTransform" || type == "scene.addComponent" || type == "scene.removeComponent" ||
           type == "scene.setComponentProperty";
}

bool IsEditTimeCommand(const std::string& type) {
    return type == "project.create" || type == "project.open" || type == "project.save" ||
           type == "project.setSaveLocation" || type == "scene.create" || type == "scene.open" ||
           type == "scene.save" || IsSceneUndoCommand(type) || type == "asset.import" ||
           type == "asset.createMaterial" || type == "asset.assignMaterial" || type == "script.create" ||
           type == "script.modify" || type == "script.compile" || type == "script.attachToEntity" ||
           type == "plugin.create" || type == "plugin.compile" || type == "plugin.test" ||
           type == "plugin.install" || type == "plugin.enable" || type == "plugin.disable" ||
           type == "build.packagePlayer" || type == "build.packageWindows";
}

std::string NormalizeBuildTargetPlatformName(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const std::string lower = std::move(value);
    if (lower == "linux") {
        return "Linux";
    }
    if (lower == "macos" || lower == "mac" || lower == "osx" || lower == "darwin") {
        return "macOS";
    }
    return "Windows";
}

std::filesystem::path DefaultPlayerExecutablePathForGateway(const EditorState& state, const std::string& targetOverride = {}) {
    const std::string targetPlatform = NormalizeBuildTargetPlatformName(
        targetOverride.empty() ? state.BuildSettings().targetPlatform : targetOverride);
    const std::string configurationSuffix = state.BuildSettings().configuration == "Release" ? "release" : "debug";
    std::vector<std::filesystem::path> candidates;
    if (targetPlatform == "Windows") {
        const std::filesystem::path preferredPreset =
            state.BuildSettings().configuration == "Release" ? "windows-ninja-release" : "windows-ninja-debug";
        candidates = {
            std::filesystem::current_path() / "build" / preferredPreset / "ai_native_player.exe",
            std::filesystem::current_path() / "build" / "windows-ninja-debug" / "ai_native_player.exe",
            std::filesystem::current_path() / "build" / "windows-ninja-release" / "ai_native_player.exe",
            std::filesystem::current_path() / "ai_native_player.exe",
        };
    } else if (targetPlatform == "Linux") {
        const std::filesystem::path preferredPreset = "linux-ninja-" + configurationSuffix;
        candidates = {
            std::filesystem::current_path() / "build" / preferredPreset / "ai_native_player",
            std::filesystem::current_path() / "build" / "linux-ninja-debug" / "ai_native_player",
            std::filesystem::current_path() / "build" / "linux-ninja-release" / "ai_native_player",
            std::filesystem::current_path() / "ai_native_player",
        };
    } else {
        const std::filesystem::path preferredPreset = "macos-ninja-" + configurationSuffix;
        candidates = {
            std::filesystem::current_path() / "build" / preferredPreset / "ai_native_player",
            std::filesystem::current_path() / "build" / "macos-ninja-debug" / "ai_native_player",
            std::filesystem::current_path() / "build" / "macos-ninja-release" / "ai_native_player",
            std::filesystem::current_path() / "ai_native_player",
        };
    }
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.empty() ? std::filesystem::path{} : candidates.front();
}

void AppendValidationDiagnostics(ToolCommandBatch& batch, EditorState& state, const ToolCommand& command,
                                 const SceneValidationResult& validation) {
    for (const std::string& diagnostic : validation.diagnostics) {
        batch.diagnostics.push_back(diagnostic);
        state.AddLog(validation.ok ? LogLevel::Info : LogLevel::Warning, diagnostic);
    }
    state.AddActivity(command.type, validation.ok ? "Passed" : "Failed", JoinStrings(validation.diagnostics, " "));
}

bool PromptMentionsBlock(const std::string& normalized) {
    return ContainsToken(normalized, "cube") || ContainsToken(normalized, "box") ||
           ContainsToken(normalized, "block");
}

bool PromptRequestsDynamicPhysics(const std::string& normalized) {
    return ContainsToken(normalized, "physics") || ContainsToken(normalized, "gravity") ||
           ContainsToken(normalized, "rigidbody") || ContainsToken(normalized, "rigid body") ||
           ContainsToken(normalized, "dynamic") || ContainsToken(normalized, "fall") ||
           ContainsToken(normalized, "falling") || ContainsToken(normalized, "drop") ||
           ContainsToken(normalized, "simulate") || ContainsToken(normalized, "9.81");
}

bool PromptRequestsMultipleBlocks(const std::string& normalized) {
    return ContainsToken(normalized, "cubes") || ContainsToken(normalized, "boxes") ||
           ContainsToken(normalized, "blocks") || ContainsToken(normalized, "bunch") ||
           ContainsToken(normalized, "several") || ContainsToken(normalized, "multiple") ||
           ContainsToken(normalized, "many") || ContainsToken(normalized, "more") ||
           ContainsToken(normalized, "few");
}

bool PromptLooksLikeUnsupportedGameplayFeatureRequest(const std::string& normalized) {
    const bool asksForCreation = ContainsToken(normalized, "create") || ContainsToken(normalized, "make") ||
                                 ContainsToken(normalized, "build") || ContainsToken(normalized, "add ") ||
                                 ContainsToken(normalized, "set up") || ContainsToken(normalized, "setup");
    const bool mentionsGameplay = ContainsToken(normalized, "player") || ContainsToken(normalized, "character") ||
                                  ContainsToken(normalized, "gameplay") || ContainsToken(normalized, "game objects") ||
                                  ContainsToken(normalized, "game object") || ContainsToken(normalized, "enemy") ||
                                  ContainsToken(normalized, "enemies");
    const bool requiresRuntimeFeature = ContainsToken(normalized, "shoot") || ContainsToken(normalized, "shooting") ||
                                        ContainsToken(normalized, "projectile") || ContainsToken(normalized, "bullet") ||
                                        ContainsToken(normalized, "weapon") || ContainsToken(normalized, "gun") ||
                                        ContainsToken(normalized, "damage") || ContainsToken(normalized, "health") ||
                                        ContainsToken(normalized, "inventory") || ContainsToken(normalized, "quest") ||
                                        ContainsToken(normalized, "ability") || ContainsToken(normalized, "abilities") ||
                                        ContainsToken(normalized, "ai ") || ContainsToken(normalized, "pathfind");
    return asksForCreation && mentionsGameplay && requiresRuntimeFeature;
}

bool PromptLooksLikeBroadGameplaySceneRequest(const std::string& normalized) {
    const bool asksForCreation = ContainsToken(normalized, "create") || ContainsToken(normalized, "make") ||
                                 ContainsToken(normalized, "build") || ContainsToken(normalized, "add ") ||
                                 ContainsToken(normalized, "set up") || ContainsToken(normalized, "setup") ||
                                 ContainsToken(normalized, "look at") || ContainsToken(normalized, "prototype");
    const bool mentionsSceneOrGameplay = ContainsToken(normalized, "scene") || ContainsToken(normalized, "level") ||
                                         ContainsToken(normalized, "gameplay") || ContainsToken(normalized, "game scene") ||
                                         ContainsToken(normalized, "player") || ContainsToken(normalized, "character");
    const bool broadIntent = ContainsToken(normalized, "where the player") ||
                             ContainsToken(normalized, "player can") ||
                             ContainsToken(normalized, "game objects") ||
                             ContainsToken(normalized, "running simulator") ||
                             ContainsToken(normalized, "basic running") ||
                             ContainsToken(normalized, "shoot") ||
                             ContainsToken(normalized, "death screen") ||
                             ContainsToken(normalized, "reset game");
    const bool explicitLocalTemplate = ContainsToken(normalized, "starter scene") ||
                                       ContainsToken(normalized, "running simulator scene template") ||
                                       ContainsToken(normalized, "runner scene template") ||
                                       ContainsToken(normalized, "create running simulator scene template") ||
                                       ContainsToken(normalized, "create the running simulator scene");
    return asksForCreation && mentionsSceneOrGameplay && broadIntent && !explicitLocalTemplate;
}

std::string BuildUnsupportedGameplayFeatureDiagnostic(const std::string& prompt) {
    std::ostringstream stream;
    stream << "Local Tool Gateway did not mutate the scene for this request because it looks like a broad gameplay "
              "or editor feature, not a deterministic supported command. Request: "
           << Trim(prompt)
           << ". Use Codex CLI or another development backend for implementation work, or ask for a specific supported "
              "local command such as add a cube, add a light, add a camera, create a starter scene, or create the "
              "running simulator scene.";
    return stream.str();
}

int RequestedBlockCount(const std::string& normalized, bool wantsMultiple) {
    for (size_t index = 0; index < normalized.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(normalized[index]))) {
            continue;
        }
        size_t end = index + 1;
        while (end < normalized.size() && std::isdigit(static_cast<unsigned char>(normalized[end]))) {
            ++end;
        }
        return std::clamp(std::stoi(normalized.substr(index, end - index)), 1, 24);
    }

    if (ContainsToken(normalized, "dozen")) {
        return 12;
    }
    if (ContainsToken(normalized, "ten")) {
        return 10;
    }
    if (ContainsToken(normalized, "eight")) {
        return 8;
    }
    if (ContainsToken(normalized, "six")) {
        return 6;
    }
    if (ContainsToken(normalized, "five")) {
        return 5;
    }
    if (ContainsToken(normalized, "four")) {
        return 4;
    }
    if (ContainsToken(normalized, "three")) {
        return 3;
    }
    if (ContainsToken(normalized, "two")) {
        return 2;
    }

    return wantsMultiple ? 6 : 1;
}

std::string UniqueBlockName(const EditorState& state, const std::vector<std::string>& reservedNames,
                            int preferredIndex) {
    for (int index = std::max(1, preferredIndex); index < preferredIndex + 200; ++index) {
        std::ostringstream stream;
        stream << "AI Block " << std::setw(2) << std::setfill('0') << index;
        const std::string candidate = stream.str();
        if (state.FindEntityByName(candidate) == nullptr &&
            std::find(reservedNames.begin(), reservedNames.end(), candidate) == reservedNames.end()) {
            return candidate;
        }
    }
    return "AI Block";
}

std::string UniqueSceneName(const EditorState& state, const std::vector<std::string>& reservedNames,
                            const std::string& baseName) {
    if (state.FindEntityByName(baseName) == nullptr &&
        std::find(reservedNames.begin(), reservedNames.end(), baseName) == reservedNames.end()) {
        return baseName;
    }

    for (int index = 2; index < 200; ++index) {
        const std::string candidate = baseName + " " + std::to_string(index);
        if (state.FindEntityByName(candidate) == nullptr &&
            std::find(reservedNames.begin(), reservedNames.end(), candidate) == reservedNames.end()) {
            return candidate;
        }
    }
    return baseName;
}

std::string BuildColliderTroubleshootingDiagnostic(const EditorState& state) {
    int colliderCount = 0;
    int rigidbodyCount = 0;
    int triggerCount = 0;
    int runtimeEffectCount = 0;
    std::vector<std::string> triggerSamples;

    for (const Entity& entity : state.Entities()) {
        if (HasComponentType(entity, "Collider3D") || HasComponentType(entity, "Collider2D")) {
            ++colliderCount;
        }
        if (HasComponentType(entity, "Rigidbody3D") || HasComponentType(entity, "Rigidbody2D")) {
            ++rigidbodyCount;
        }
    }

    const Entity* selected = state.FindEntity(state.SelectedEntityId());
    const bool selectedHasCollider =
        selected != nullptr && (HasComponentType(*selected, "Collider3D") || HasComponentType(*selected, "Collider2D"));
    const bool selectedHasRigidbody =
        selected != nullptr && (HasComponentType(*selected, "Rigidbody3D") || HasComponentType(*selected, "Rigidbody2D"));

    for (const AgentActivity& activity : state.AgentActivities()) {
        if (activity.label == "physics.trigger") {
            ++triggerCount;
            if (triggerSamples.size() < 3) {
                triggerSamples.push_back(activity.detail);
            }
        } else if (activity.label == "runtime.collectible" || activity.label == "runtime.hazard" ||
                   activity.label == "runtime.goal") {
            ++runtimeEffectCount;
        }
    }

    std::ostringstream stream;
    stream << "Collider diagnostic: " << colliderCount << " collider component(s) and " << rigidbodyCount
           << " rigidbody component(s) are present";
    if (selected != nullptr) {
        stream << "; selected entity '" << selected->name << "' "
               << (selectedHasCollider ? "has a collider" : "does not have a collider") << " and "
               << (selectedHasRigidbody ? "has a rigidbody" : "does not have a rigidbody");
    }
    stream << ". ";
    if (rigidbodyCount == 0 && colliderCount > 0) {
        stream << "Collider-only objects are static; falling under gravity requires a Dynamic Rigidbody3D or "
                  "Rigidbody2D with useGravity enabled. ";
    }

    if (triggerCount > 0) {
        stream << "Recent Play Mode activity shows colliders are firing with " << triggerCount
               << " physics.trigger event(s)";
        if (!triggerSamples.empty()) {
            stream << " including " << JoinStrings(triggerSamples, ", ");
        }
        stream << ". ";
        if (runtimeEffectCount > 0) {
            stream << "Runtime systems also reacted to those overlaps with " << runtimeEffectCount
                   << " collectible/hazard/goal event(s). ";
        }
        stream << "If a collider still looks wrong, check whether it is marked trigger, whether the other object has a "
                  "runtime controller/body, and whether Play Mode is running.";
    } else {
        stream << "No recent physics.trigger activity is visible. Colliders only produce runtime events while Play Mode "
                  "is running and active enabled colliders overlap; run Play Mode, move the controlled object into the "
                  "target, then watch Agent Activity and Console for physics.trigger or physics.collision entries.";
    }

    return stream.str();
}

} // namespace

ToolGatewayResult ToolGateway::SubmitPrompt(const std::string& prompt, EditorState& state) {
    ToolGatewayResult result;
    result.batch = BuildCommandBatch(prompt, NextTransactionId(), state);

    state.SetAgentStatus("Running local structured command batch");
    state.SetLastTransactionId(result.batch.transactionId);
    state.AddActivity("Prompt received", "Complete", "Captured user request for Local Tool Gateway");
    state.AddActivity("Command batch proposed", result.batch.approval,
                      result.batch.name + " | " + std::to_string(result.batch.commands.size()) + " commands");
    for (const ToolCommand& command : result.batch.commands) {
        state.AddActivity(command.type, "Proposed", command.summary);
    }

    ApplyCommandBatch(result.batch, state);
    result.assistantMessage = BuildAssistantMessage(result.batch);
    state.SetAgentStatus("Idle");
    return result;
}

ToolGatewayResult ToolGateway::ApplyApprovedCommandBatch(ToolCommandBatch batch, EditorState& state, std::string approval) {
    ToolGatewayResult result;
    result.batch = std::move(batch);
    if (result.batch.transactionId.empty()) {
        result.batch.transactionId = NextTransactionId();
    }
    result.batch.approval = std::move(approval);
    for (ToolCommand& command : result.batch.commands) {
        NormalizeProviderCommandDefaults(command);
    }

    ToolGatewayValidationResult validation = ValidateCommandBatchSchema(result.batch);
    if (!validation.ok) {
        result.batch.ok = false;
        result.batch.applied = false;
        result.batch.diagnostics = validation.diagnostics;
        result.assistantMessage = "Approved command batch was rejected by schema validation: " +
                                  JoinStrings(validation.diagnostics, " ");
        state.AddActivity("approved command batch", "Rejected", result.assistantMessage);
        return result;
    }

    state.SetAgentStatus("Applying approved command batch");
    state.SetLastTransactionId(result.batch.transactionId);
    state.AddActivity("approved command batch", result.batch.approval,
                      result.batch.name + " | " + std::to_string(result.batch.commands.size()) + " commands");
    for (const ToolCommand& command : result.batch.commands) {
        state.AddActivity(command.type, "Approved", command.summary);
    }

    ApplyCommandBatch(result.batch, state);
    result.assistantMessage = BuildAssistantMessage(result.batch);
    state.SetAgentStatus("Idle");
    return result;
}

ToolGatewayValidationResult ToolGateway::ValidateCommandBatchSchema(const ToolCommandBatch& batch) const {
    ToolGatewayValidationResult result;

    auto fail = [&result](const std::string& diagnostic) {
        result.ok = false;
        result.diagnostics.push_back(diagnostic);
    };

    if (batch.name.empty()) {
        fail("command_batch.name must be a non-empty string.");
    }
    if (batch.commands.empty()) {
        fail("command_batch.commands must contain at least one command.");
    }

    for (size_t index = 0; index < batch.commands.size(); ++index) {
        const ToolCommand& command = batch.commands[index];
        const std::string prefix = "command " + std::to_string(index) + " (" + command.type + "): ";
        if (std::find(supportedCommands_.begin(), supportedCommands_.end(), command.type) == supportedCommands_.end()) {
            fail(prefix + "unsupported command type.");
            continue;
        }

        if (command.type == "project.getState" || command.type == "project.save" ||
            command.type == "scene.save" || command.type == "runtime.play" ||
            command.type == "runtime.pause" || command.type == "runtime.stop" ||
            command.type == "runtime.captureScreenshot" || command.type == "runtime.startRecording" ||
            command.type == "runtime.stopRecording" ||
            command.type == "runtime.getLogs" || command.type == "validate.scene" ||
            command.type == "validate.project" || command.type == "validate.assets" ||
            command.type == "validate.scripts" || command.type == "script.compile" || command.type == "asset.find" ||
            command.type == "build.packagePlayer" || command.type == "build.packageWindows" ||
            command.type == "build.runSmokeTest" ||
            command.type == "build.openOutputFolder") {
            continue;
        }

        if (command.type == "project.create" || command.type == "project.open" ||
            command.type == "project.setSaveLocation" || command.type == "scene.create" ||
            command.type == "scene.open" || command.type == "asset.import" ||
            command.type == "script.create" || command.type == "script.modify" ||
            command.type == "script.getDiagnostics") {
            if (command.path.empty()) {
                fail(prefix + "path must be a non-empty string.");
            }
            if (command.type == "script.modify" && command.content.empty()) {
                fail(prefix + "content must be a non-empty string.");
            }
            continue;
        }

        if (command.type == "scene.createEntity") {
            if (command.entityName.empty()) {
                fail(prefix + "name must be a non-empty string.");
            }
            for (size_t componentIndex = 0; componentIndex < command.components.size(); ++componentIndex) {
                if (command.components[componentIndex].type.empty()) {
                    fail(prefix + "components[" + std::to_string(componentIndex) + "].type must be non-empty.");
                } else if (!ComponentLooksKnownOrScript(command.components[componentIndex])) {
                    fail(prefix + "components[" + std::to_string(componentIndex) + "].type '" +
                         command.components[componentIndex].type +
                         "' is not registered and has no scriptPath.");
                }
            }
            continue;
        }

        if (command.type == "scene.renameEntity") {
            if (command.targetEntityName.empty()) {
                fail(prefix + "targetName must be a non-empty string.");
            }
            if (command.newName.empty()) {
                fail(prefix + "newName must be a non-empty string.");
            }
            continue;
        }

        if (command.type == "scene.deleteEntity") {
            if (command.targetEntityName.empty()) {
                fail(prefix + "targetName must be a non-empty string.");
            }
            continue;
        }

        if (command.type == "scene.setParent") {
            if (command.targetEntityName.empty()) {
                fail(prefix + "targetName must be a non-empty string.");
            }
            continue;
        }

        if (command.type == "scene.setTransform") {
            if (command.targetEntityName.empty()) {
                fail(prefix + "targetName must be a non-empty string.");
            }
            if (!command.hasPosition && !command.hasRotation && !command.hasScale) {
                fail(prefix + "transform must include at least one of position, rotation, or scale.");
            }
            auto finiteVec = [&fail, &prefix](const std::array<float, 3>& value, const std::string& label) {
                for (float component : value) {
                    if (!std::isfinite(component)) {
                        fail(prefix + label + " values must be finite numbers.");
                        return;
                    }
                }
            };
            if (command.hasPosition) {
                finiteVec(command.position, "position");
            }
            if (command.hasRotation) {
                finiteVec(command.rotation, "rotation");
            }
            if (command.hasScale) {
                finiteVec(command.scale, "scale");
            }
            continue;
        }

        if (command.type == "scene.addComponent") {
            if (command.targetEntityName.empty()) {
                fail(prefix + "targetName must be a non-empty string.");
            }
            if (command.component.type.empty()) {
                fail(prefix + "component.type must be a non-empty string.");
            } else if (!ComponentLooksKnownOrScript(command.component)) {
                fail(prefix + "component.type '" + command.component.type +
                     "' is not registered and has no scriptPath.");
            }
            continue;
        }

        if (command.type == "scene.removeComponent") {
            if (command.targetEntityName.empty()) {
                fail(prefix + "targetName must be a non-empty string.");
            }
            if (command.componentType.empty()) {
                fail(prefix + "componentType must be a non-empty string.");
            }
            continue;
        }

        if (command.type == "scene.setComponentProperty") {
            if (command.targetEntityName.empty()) {
                fail(prefix + "targetName must be a non-empty string.");
            }
            if (command.componentType.empty() || command.propertyName.empty()) {
                fail(prefix + "componentType and propertyName must be non-empty strings.");
            }
            continue;
        }

        if (command.type == "asset.getMetadata") {
            if (command.assetId.empty() && command.path.empty() && command.entityName.empty()) {
                fail(prefix + "assetId, name, or path must identify an asset.");
            }
            continue;
        }

        if (command.type == "asset.createMaterial") {
            if (command.entityName.empty()) {
                fail(prefix + "name must be a non-empty string.");
            }
            continue;
        }

        if (command.type == "asset.assignMaterial") {
            if (command.targetEntityName.empty() || command.materialName.empty()) {
                fail(prefix + "targetName and material must be non-empty strings.");
            }
            continue;
        }

        if (command.type == "script.attachToEntity") {
            if (command.targetEntityName.empty() || command.path.empty()) {
                fail(prefix + "targetName and path must be non-empty strings.");
            }
            continue;
        }

        if (command.type == "plugin.create" || command.type == "plugin.compile" || command.type == "plugin.test" ||
            command.type == "plugin.install" || command.type == "plugin.enable" || command.type == "plugin.disable") {
            if (command.entityName.empty()) {
                fail(prefix + "name must be a non-empty string.");
            }
            continue;
        }
    }

    return result;
}

std::string ToolGateway::NextTransactionId() {
    std::ostringstream id;
    id << "txn_" << std::setw(4) << std::setfill('0') << nextTransactionNumber_++;
    return id.str();
}

ToolCommandBatch ToolGateway::BuildCommandBatch(const std::string& prompt, const std::string& transactionId,
                                                const EditorState& state) const {
    ToolCommandBatch batch;
    batch.transactionId = transactionId;
    batch.name = "Local prompt command batch";
    batch.commands.push_back(ProjectGetStateCommand());

    const std::string normalized = ToLower(prompt);

    if (PromptLooksLikeColliderTroubleshootingRequest(prompt)) {
        batch.name = "Diagnose collider runtime";
        batch.diagnostics.push_back(BuildColliderTroubleshootingDiagnostic(state));
        batch.commands.push_back(ValidateSceneCommand());
        return batch;
    }

    if (PromptLooksLikeUnsupportedGameplayFeatureRequest(normalized) ||
        PromptLooksLikeBroadGameplaySceneRequest(normalized)) {
        batch.name = "Route gameplay feature to development backend";
        batch.diagnostics.push_back(BuildUnsupportedGameplayFeatureDiagnostic(prompt));
        batch.commands.push_back(ValidateSceneCommand());
        return batch;
    }

    if (PromptLooksLikeRunningSimulatorRequest(prompt)) {
        batch.name = "Create running simulator scene";
        const std::array<float, 3> cameraPosition{0.0f, 4.0f, 9.0f};
        const std::array<float, 3> cameraRotation{-22.0f, 0.0f, 0.0f};
        const std::array<float, 3> lightRotation{45.0f, -35.0f, 0.0f};
        const std::array<float, 3> groundPosition{0.0f, -0.05f, 0.0f};
        const std::array<float, 3> groundScale{8.0f, 0.1f, 18.0f};
        const std::array<float, 3> rulesPosition{-3.2f, 0.25f, -6.0f};
        const std::array<float, 3> playerPosition{0.0f, 0.5f, 5.0f};
        const std::array<float, 3> playerScale{0.8f, 0.8f, 0.8f};
        const std::array<float, 3> goalPosition{0.0f, 0.6f, -6.0f};
        const std::array<float, 3> goalScale{2.5f, 1.4f, 0.3f};

        std::vector<std::string> reservedNames;
        const auto reserveName = [&](const std::string& baseName) {
            const std::string name = UniqueSceneName(state, reservedNames, baseName);
            reservedNames.push_back(name);
            return name;
        };

        const std::string cameraName = reserveName("AI Runner Camera");
        const std::string lightName = reserveName("AI Runner Key Light");
        const std::string groundName = reserveName("AI Runner Track");
        const std::string rulesName = reserveName("AI Runner Game Rules");
        const std::string playerName = reserveName("AI Runner Player");
        const std::string goalName = reserveName("AI Runner Finish Gate");

        batch.commands.push_back(CreateEntityCommand(cameraName, {MakeTransformComponent(), MakeCameraComponent(),
                                                                  MakeCameraFollowRuntimeComponent(playerName)}));
        batch.commands.push_back(SetTransformCommand(cameraName, &cameraPosition, &cameraRotation, nullptr));

        batch.commands.push_back(CreateEntityCommand(lightName, {MakeTransformComponent(), MakeLightComponent("Directional")}));
        batch.commands.push_back(SetTransformCommand(lightName, nullptr, &lightRotation, nullptr));

        batch.commands.push_back(CreateEntityCommand(
            groundName, {MakeTransformComponent(), MakeMeshPlaceholderComponent("GroundPlane", "TrackAsphalt"),
                         MakeCollider3DComponent("Box")}));
        batch.commands.push_back(SetTransformCommand(groundName, &groundPosition, nullptr, &groundScale));

        batch.commands.push_back(CreateEntityCommand(rulesName, {MakeTransformComponent(), MakeInputActionMapRuntimeComponent(),
                                                                 MakeGameRulesRuntimeComponent("Running Simulator", 3)}));
        batch.commands.push_back(SetTransformCommand(rulesName, &rulesPosition, nullptr, nullptr));

        batch.commands.push_back(CreateEntityCommand(
            playerName, {MakeTransformComponent(), MakeMeshPlaceholderComponent("UnitCube", "PlayerBlue"),
                         MakeCharacterControllerRuntimeComponent(4.5f, 1.4f), MakeCollider3DComponent("Box")}));
        batch.commands.push_back(SetTransformCommand(playerName, &playerPosition, nullptr, &playerScale));

        const std::array<float, 3> laneLeftPosition{-1.65f, 0.04f, -0.5f};
        const std::array<float, 3> laneRightPosition{1.65f, 0.04f, -0.5f};
        const std::array<float, 3> laneScale{0.08f, 0.05f, 14.0f};
        const std::string laneLeftName = reserveName("AI Runner Left Lane");
        const std::string laneRightName = reserveName("AI Runner Right Lane");
        batch.commands.push_back(CreateEntityCommand(laneLeftName, {MakeTransformComponent(),
                                                                    MakeMeshPlaceholderComponent("UnitCube", "LaneWhite")}));
        batch.commands.push_back(SetTransformCommand(laneLeftName, &laneLeftPosition, nullptr, &laneScale));
        batch.commands.push_back(CreateEntityCommand(laneRightName, {MakeTransformComponent(),
                                                                     MakeMeshPlaceholderComponent("UnitCube", "LaneWhite")}));
        batch.commands.push_back(SetTransformCommand(laneRightName, &laneRightPosition, nullptr, &laneScale));

        for (int index = 0; index < 3; ++index) {
            const std::string coinName = reserveName("AI Runner Coin " + std::to_string(index + 1));
            const std::array<float, 3> coinPosition{index == 1 ? 0.85f : -0.85f, 0.55f,
                                                    2.5f - static_cast<float>(index) * 2.2f};
            const std::array<float, 3> coinScale{0.32f, 0.32f, 0.32f};
            batch.commands.push_back(CreateEntityCommand(
                coinName, {MakeTransformComponent(), MakeMeshPlaceholderComponent("UnitCube", "CoinGold"),
                           MakeCollectibleRuntimeComponent(1, 0.7f), MakeSpinRuntimeComponent(140.0f),
                           WithProperty(MakeCollider3DComponent("Sphere", true), "radius", "0.7")}));
            batch.commands.push_back(SetTransformCommand(coinName, &coinPosition, nullptr, &coinScale));
        }

        const std::array<float, 3> hazardPosition{0.0f, 0.3f, -2.3f};
        const std::array<float, 3> hazardScale{1.25f, 0.35f, 0.55f};
        const std::string hazardName = reserveName("AI Runner Hurdle");
        batch.commands.push_back(CreateEntityCommand(
            hazardName, {MakeTransformComponent(), MakeMeshPlaceholderComponent("HazardBlock", "HazardRed"),
                         MakeHazardRuntimeComponent(0.75f), MakeCollider3DComponent("Box", true)}));
        batch.commands.push_back(SetTransformCommand(hazardName, &hazardPosition, nullptr, &hazardScale));

        batch.commands.push_back(CreateEntityCommand(
            goalName, {MakeTransformComponent(), MakeMeshPlaceholderComponent("GoalGate", "GoalGreen"),
                       MakeGoalRuntimeComponent(3, 1.0f), MakeCollider3DComponent("Box", true)}));
        batch.commands.push_back(SetTransformCommand(goalName, &goalPosition, nullptr, &goalScale));

        batch.commands.push_back(ValidateSceneCommand());
        batch.commands.push_back(ProjectSaveCommand());
        return batch;
    }

    if (PromptLooksLikeStarterSceneRequest(prompt)) {
        batch.name = "Create starter scene";
        const std::array<float, 3> cameraPosition{0.0f, 4.0f, 9.0f};
        const std::array<float, 3> cameraRotation{-22.0f, 0.0f, 0.0f};
        const std::array<float, 3> lightRotation{45.0f, -35.0f, 0.0f};
        const std::array<float, 3> groundPosition{0.0f, -0.05f, 0.0f};
        const std::array<float, 3> groundScale{8.0f, 0.1f, 8.0f};
        const std::array<float, 3> playerStartPosition{0.0f, 0.5f, -2.0f};

        batch.commands.push_back(CreateEntityCommand(
            "AI Main Camera",
            {MakeTransformComponent(), MakeCameraComponent(), MakeCameraFollowRuntimeComponent("AI Player Spawn")}));
        batch.commands.push_back(SetTransformCommand("AI Main Camera", &cameraPosition, &cameraRotation, nullptr));

        batch.commands.push_back(CreateEntityCommand("AI Key Light", {MakeTransformComponent(), MakeLightComponent("Directional")}));
        batch.commands.push_back(SetTransformCommand("AI Key Light", nullptr, &lightRotation, nullptr));

        batch.commands.push_back(CreateEntityCommand("AI Ground Plane", {MakeTransformComponent()}));
        batch.commands.push_back(SetTransformCommand("AI Ground Plane", &groundPosition, nullptr, &groundScale));
        batch.commands.push_back(AddComponentCommand("AI Ground Plane", MakeMeshPlaceholderComponent("GroundPlane")));

        batch.commands.push_back(CreateEntityCommand("AI Player Start", {MakeTransformComponent(), MakeMeshPlaceholderComponent("SpawnMarker")}));
        batch.commands.push_back(SetTransformCommand("AI Player Start", &playerStartPosition, nullptr, nullptr));
        batch.commands.push_back(RenameEntityCommand("AI Player Start", "AI Player Spawn"));

        batch.commands.push_back(ValidateSceneCommand());
        batch.commands.push_back(ProjectSaveCommand());
        return batch;
    }

    if (normalized.find("save") != std::string::npos) {
        if ((normalized.find("save folder") != std::string::npos ||
             normalized.find("save location") != std::string::npos ||
             normalized.find("save data") != std::string::npos) &&
            !FirstQuotedText(prompt).empty()) {
            batch.name = "Set game save folder";
            batch.commands.push_back(ProjectSetSaveLocationCommand(FirstQuotedText(prompt)));
            batch.commands.push_back(ProjectSaveCommand());
            return batch;
        }
        batch.name = "Save current project";
        batch.commands.push_back(ProjectSaveCommand());
        return batch;
    }

    if (normalized.find("rename selected") != std::string::npos || normalized.find("rename entity") != std::string::npos) {
        const Entity* selected = state.FindEntity(state.SelectedEntityId());
        std::string newName = FirstQuotedText(prompt);
        if (newName.empty()) {
            const size_t toPos = normalized.find(" to ");
            if (toPos != std::string::npos) {
                newName = Trim(prompt.substr(toPos + 4));
            }
        }
        if (selected != nullptr && !newName.empty()) {
            batch.name = "Rename selected entity";
            batch.commands.push_back(RenameEntityCommand(selected->name, newName));
            batch.commands.push_back(ValidateSceneCommand());
            batch.commands.push_back(ProjectSaveCommand());
            return batch;
        }
    }

    const bool mentionsBlock = PromptMentionsBlock(normalized);
    const bool wantsMultipleBlocks = mentionsBlock && PromptRequestsMultipleBlocks(normalized);
    if (wantsMultipleBlocks) {
        batch.name = "Create block placeholders";
        const bool wantsDynamicPhysics = PromptRequestsDynamicPhysics(normalized);
        const bool wantsCollider = wantsDynamicPhysics || normalized.find("collider") != std::string::npos ||
                                   normalized.find("collision") != std::string::npos;
        const int blockCount = RequestedBlockCount(normalized, true);
        const float startX = -static_cast<float>(std::min(blockCount, 4) - 1) * 0.75f;
        const int baseIndex = static_cast<int>(state.Entities().size()) + 1;
        std::vector<std::string> reservedNames;
        for (int index = 0; index < blockCount; ++index) {
            const std::string entityName = UniqueBlockName(state, reservedNames, baseIndex + index);
            reservedNames.push_back(entityName);
            std::vector<Component> components{MakeTransformComponent(), MakeMeshPlaceholderComponent("UnitCube")};
            if (wantsDynamicPhysics) {
                components.push_back(MakeRigidbody3DComponent("Dynamic"));
            }
            if (wantsCollider) {
                components.push_back(MakeCollider3DComponent("Box"));
            }
            const int column = index % 4;
            const int row = index / 4;
            const std::array<float, 3> position{
                startX + static_cast<float>(column) * 1.5f,
                wantsDynamicPhysics ? 4.0f + static_cast<float>(row) * 0.35f : 0.5f,
                1.5f + static_cast<float>(row) * 1.5f,
            };
            batch.commands.push_back(CreateEntityCommand(entityName, std::move(components)));
            batch.commands.push_back(SetTransformCommand(entityName, &position, nullptr, nullptr));
        }
        batch.commands.push_back(ValidateSceneCommand());
        batch.commands.push_back(ProjectSaveCommand());
        return batch;
    }

    if (mentionsBlock) {
        batch.name = "Create cube placeholder";
        const std::string quotedName = FirstQuotedText(prompt);
        const std::string entityName = quotedName.empty() ? "AI Cube" : quotedName;
        const bool wantsDynamicPhysics = PromptRequestsDynamicPhysics(normalized);
        const bool wantsCollider = wantsDynamicPhysics || normalized.find("collider") != std::string::npos ||
                                   normalized.find("collision") != std::string::npos;
        const float offset = static_cast<float>(state.Entities().size() % 5) - 2.0f;
        const std::array<float, 3> position{offset, wantsDynamicPhysics ? 4.0f : 0.5f, 0.0f};
        std::vector<Component> components{MakeTransformComponent(), MakeMeshPlaceholderComponent("UnitCube")};
        if (wantsDynamicPhysics) {
            components.push_back(MakeRigidbody3DComponent("Dynamic"));
        }
        if (wantsCollider) {
            components.push_back(MakeCollider3DComponent("Box"));
        }
        batch.commands.push_back(CreateEntityCommand(entityName, std::move(components)));
        batch.commands.push_back(SetTransformCommand(entityName, &position, nullptr, nullptr));
        batch.commands.push_back(ValidateSceneCommand());
        batch.commands.push_back(ProjectSaveCommand());
        return batch;
    }

    if (normalized.find("light") != std::string::npos) {
        batch.name = "Create light";
        const std::string quotedName = FirstQuotedText(prompt);
        const std::string entityName = quotedName.empty() ? "AI Directional Light" : quotedName;
        const std::array<float, 3> rotation{50.0f, -25.0f, 0.0f};
        batch.commands.push_back(CreateEntityCommand(entityName, {MakeTransformComponent(), MakeLightComponent("Directional")}));
        batch.commands.push_back(SetTransformCommand(entityName, nullptr, &rotation, nullptr));
        batch.commands.push_back(ValidateSceneCommand());
        batch.commands.push_back(ProjectSaveCommand());
        return batch;
    }

    if (normalized.find("camera") != std::string::npos) {
        batch.name = "Create camera";
        const std::string quotedName = FirstQuotedText(prompt);
        const std::string entityName = quotedName.empty() ? "AI Camera" : quotedName;
        const std::array<float, 3> position{0.0f, 2.5f, 7.0f};
        const std::array<float, 3> rotation{-18.0f, 0.0f, 0.0f};
        batch.commands.push_back(CreateEntityCommand(entityName, {MakeTransformComponent(), MakeCameraComponent(),
                                                                  MakeCameraFollowRuntimeComponent("Player")}));
        batch.commands.push_back(SetTransformCommand(entityName, &position, &rotation, nullptr));
        batch.commands.push_back(ValidateSceneCommand());
        batch.commands.push_back(ProjectSaveCommand());
        return batch;
    }

    batch.name = "Inspect project state";
    batch.commands.push_back(ValidateSceneCommand());
    return batch;
}

bool ToolGateway::ApplyCommandBatch(ToolCommandBatch& batch, EditorState& state) const {
    bool ok = true;
    const bool needsUndoSnapshot =
        std::any_of(batch.commands.begin(), batch.commands.end(), [](const ToolCommand& command) {
            return IsSceneUndoCommand(command.type);
        });
    if (needsUndoSnapshot && state.IsEditMode()) {
        state.PushUndoSnapshot(batch.transactionId + " " + batch.name);
    }

    for (const ToolCommand& command : batch.commands) {
        if (command.type == "project.getState") {
            state.AddActivity(command.type, "Complete", state.GetProjectStateSummary());
            continue;
        }

        if (IsEditTimeCommand(command.type) && !state.IsEditMode()) {
            ok = false;
            const std::string diagnostic = command.type + " blocked: edit-time command cannot run during Play Mode.";
            batch.diagnostics.push_back(diagnostic);
            state.AddActivity(command.type, "Blocked", diagnostic);
            continue;
        }

        if (command.type == "project.save") {
            std::string error;
            if (state.SaveProject(&error)) {
                AppendUnique(batch.changedFiles, state.ProjectFilePathString());
                AppendUnique(batch.changedFiles, state.SceneFilePathString());
                state.AddActivity(command.type, "Complete", "Saved project and active scene.");
            } else {
                ok = false;
                batch.diagnostics.push_back("project.save failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "project.create") {
            std::string error;
            if (state.NewProject(command.path, &error)) {
                AppendUnique(batch.changedFiles, state.ProjectFilePathString());
                AppendUnique(batch.changedFiles, state.SceneFilePathString());
                state.AddActivity(command.type, "Complete", "Created project at " + state.ProjectRootPathString());
            } else {
                ok = false;
                batch.diagnostics.push_back("project.create failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "project.open") {
            std::string error;
            if (state.OpenProject(command.path, &error)) {
                state.AddActivity(command.type, "Complete", "Opened project at " + state.ProjectRootPathString());
            } else {
                ok = false;
                batch.diagnostics.push_back("project.open failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "project.setSaveLocation") {
            std::string error;
            if (state.SetGameSaveRootPath(command.path, &error) && state.SaveProject(&error)) {
                AppendUnique(batch.changedFiles, state.ProjectFilePathString());
                state.AddActivity(command.type, "Complete", "Game save folder: " + state.GameSaveRootPathString());
            } else {
                ok = false;
                batch.diagnostics.push_back("project.setSaveLocation failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "scene.create") {
            std::string error;
            if (state.CreateScene(command.path, &error)) {
                AppendUnique(batch.changedFiles, state.SceneFilePathString());
                state.AddActivity(command.type, "Complete", "Created scene " + state.SceneFilePathString());
            } else {
                ok = false;
                batch.diagnostics.push_back("scene.create failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "scene.open") {
            std::string error;
            if (state.OpenScene(command.path, &error)) {
                state.AddActivity(command.type, "Complete", "Opened scene " + state.SceneFilePathString());
            } else {
                ok = false;
                batch.diagnostics.push_back("scene.open failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "scene.save") {
            std::string error;
            if (state.SaveScene(&error)) {
                AppendUnique(batch.changedFiles, state.SceneFilePathString());
                state.AddActivity(command.type, "Complete", "Saved active scene.");
            } else {
                ok = false;
                batch.diagnostics.push_back("scene.save failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "scene.createEntity") {
            Entity& entity = state.CreateEntity(command.entityName, command.components);
            state.SelectEntity(entity.id);
            AppendUnique(batch.changedEntities, entity.name);
            state.AddActivity(command.type, "Applied", entity.name + " id=" + std::to_string(entity.id));
            continue;
        }

        if (command.type == "scene.deleteEntity") {
            Entity* entity = state.FindEntityByName(command.targetEntityName);
            if (entity == nullptr) {
                ok = false;
                batch.diagnostics.push_back("scene.deleteEntity failed: target not found: " + command.targetEntityName);
                state.AddActivity(command.type, "Failed", "Target not found: " + command.targetEntityName);
                continue;
            }
            const std::string oldName = entity->name;
            if (state.DeleteEntity(entity->id)) {
                AppendUnique(batch.changedEntities, oldName);
                state.AddActivity(command.type, "Applied", "Deleted " + oldName);
            }
            continue;
        }

        if (command.type == "scene.renameEntity") {
            Entity* entity = state.FindEntityByName(command.targetEntityName);
            if (entity == nullptr) {
                ok = false;
                batch.diagnostics.push_back("scene.renameEntity failed: target not found: " + command.targetEntityName);
                state.AddActivity(command.type, "Failed", "Target not found: " + command.targetEntityName);
                continue;
            }

            const std::string oldName = entity->name;
            if (state.RenameEntity(entity->id, command.newName)) {
                AppendUnique(batch.changedEntities, oldName + " -> " + command.newName);
                state.AddActivity(command.type, "Applied", oldName + " renamed to " + command.newName);
            }
            continue;
        }

        if (command.type == "scene.setParent") {
            Entity* entity = state.FindEntityByName(command.targetEntityName);
            const Entity* parent = command.parentEntityName.empty() || command.parentEntityName == "Root"
                                       ? nullptr
                                       : state.FindEntityByName(command.parentEntityName);
            if (entity == nullptr || (!command.parentEntityName.empty() && command.parentEntityName != "Root" && parent == nullptr)) {
                ok = false;
                batch.diagnostics.push_back("scene.setParent failed: target or parent not found.");
                state.AddActivity(command.type, "Failed", "Target or parent not found.");
                continue;
            }
            if (state.SetEntityParent(entity->id, parent == nullptr ? 0 : parent->id)) {
                AppendUnique(batch.changedEntities, entity->name);
                state.AddActivity(command.type, "Applied",
                                  entity->name + " parent=" + (parent == nullptr ? "Root" : parent->name));
            }
            continue;
        }

        if (command.type == "scene.setTransform") {
            Entity* entity = state.FindEntityByName(command.targetEntityName);
            if (entity == nullptr) {
                ok = false;
                batch.diagnostics.push_back("scene.setTransform failed: target not found: " + command.targetEntityName);
                state.AddActivity(command.type, "Failed", "Target not found: " + command.targetEntityName);
                continue;
            }

            const std::array<float, 3>* position = command.hasPosition ? &command.position : nullptr;
            const std::array<float, 3>* rotation = command.hasRotation ? &command.rotation : nullptr;
            const std::array<float, 3>* scale = command.hasScale ? &command.scale : nullptr;
            if (state.SetEntityTransform(entity->id, position, rotation, scale)) {
                AppendUnique(batch.changedEntities, entity->name);
                state.AddActivity(command.type, "Applied", "Updated transform on " + entity->name);
            }
            continue;
        }

        if (command.type == "scene.addComponent") {
            Entity* entity = state.FindEntityByName(command.targetEntityName);
            if (entity == nullptr) {
                ok = false;
                batch.diagnostics.push_back("scene.addComponent failed: target not found: " + command.targetEntityName);
                state.AddActivity(command.type, "Failed", "Target not found: " + command.targetEntityName);
                continue;
            }

            if (state.AddComponentToEntity(entity->id, command.component)) {
                AppendUnique(batch.changedEntities, entity->name);
                state.AddActivity(command.type, "Applied", "Added " + command.component.type + " to " + entity->name);
            }
            continue;
        }

        if (command.type == "scene.removeComponent") {
            Entity* entity = state.FindEntityByName(command.targetEntityName);
            if (entity == nullptr) {
                ok = false;
                batch.diagnostics.push_back("scene.removeComponent failed: target not found: " + command.targetEntityName);
                state.AddActivity(command.type, "Failed", "Target not found: " + command.targetEntityName);
                continue;
            }
            if (state.RemoveComponentFromEntity(entity->id, command.componentType)) {
                AppendUnique(batch.changedEntities, entity->name);
                state.AddActivity(command.type, "Applied", "Removed " + command.componentType + " from " + entity->name);
            } else {
                ok = false;
                batch.diagnostics.push_back("scene.removeComponent failed for " + entity->name + ".");
                state.AddActivity(command.type, "Failed", "Could not remove " + command.componentType);
            }
            continue;
        }

        if (command.type == "scene.setComponentProperty") {
            Entity* entity = state.FindEntityByName(command.targetEntityName);
            if (entity == nullptr) {
                ok = false;
                batch.diagnostics.push_back("scene.setComponentProperty failed: target not found: " + command.targetEntityName);
                state.AddActivity(command.type, "Failed", "Target not found: " + command.targetEntityName);
                continue;
            }
            if (state.SetComponentPropertyOnEntity(entity->id, command.componentType, command.propertyName, command.propertyValue)) {
                AppendUnique(batch.changedEntities, entity->name);
                state.AddActivity(command.type, "Applied",
                                  entity->name + "." + command.componentType + "." + command.propertyName);
            } else {
                ok = false;
                batch.diagnostics.push_back("scene.setComponentProperty failed for " + entity->name + ".");
                state.AddActivity(command.type, "Failed", "Could not set component property.");
            }
            continue;
        }

        if (command.type == "runtime.play") {
            if (state.BeginPlayMode()) {
                state.AddActivity(command.type, "Playing", state.RuntimeStatus());
            } else {
                state.AddActivity(command.type, "Noop", state.RuntimeStatus());
            }
            continue;
        }

        if (command.type == "runtime.pause") {
            if (state.PausePlayMode()) {
                state.AddActivity(command.type, "Paused", state.RuntimeStatus());
            } else {
                state.AddActivity(command.type, "Noop", state.RuntimeStatus());
            }
            continue;
        }

        if (command.type == "runtime.stop") {
            if (state.StopPlayMode()) {
                state.AddActivity(command.type, "Stopped", state.RuntimeStatus());
            } else {
                state.AddActivity(command.type, "Noop", state.RuntimeStatus());
            }
            continue;
        }

        if (command.type == "runtime.captureScreenshot" || command.type == "runtime.startRecording" ||
            command.type == "runtime.stopRecording") {
            batch.diagnostics.push_back(command.type + ": editor-owned visual command acknowledged.");
            state.AddActivity(command.type, "Acknowledged",
                              "EditorApp owns framebuffer capture and recording side effects.");
            continue;
        }

        if (command.type == "runtime.getLogs") {
            if (state.RuntimeLogs().empty()) {
                batch.diagnostics.push_back("runtime.getLogs: no runtime log entries are available.");
            }
            for (const RuntimeLogEntry& entry : state.RuntimeLogs()) {
                batch.diagnostics.push_back("runtime." + entry.event + " [" + entry.status + "] " + entry.detail);
            }
            state.AddActivity(command.type, "Complete", std::to_string(state.RuntimeLogs().size()) + " runtime log(s).");
            continue;
        }

        if (command.type == "validate.scene") {
            const SceneValidationResult validation = state.ValidateScene();
            ok = ok && validation.ok;
            AppendValidationDiagnostics(batch, state, command, validation);
            continue;
        }

        if (command.type == "validate.project") {
            const SceneValidationResult validation = state.ValidateProject();
            ok = ok && validation.ok;
            AppendValidationDiagnostics(batch, state, command, validation);
            continue;
        }

        if (command.type == "validate.assets") {
            const SceneValidationResult validation = state.ValidateAssets();
            ok = ok && validation.ok;
            AppendValidationDiagnostics(batch, state, command, validation);
            continue;
        }

        if (command.type == "validate.scripts") {
            const SceneValidationResult validation = state.ValidateScripts();
            ok = ok && validation.ok;
            AppendValidationDiagnostics(batch, state, command, validation);
            continue;
        }

        if (command.type == "asset.import") {
            AssetRecord asset;
            std::string error;
            if (state.ImportAsset(command.path, command.sourceLabel, command.license, &asset, &error)) {
                AppendUnique(batch.changedFiles, asset.importedPath);
                AppendUnique(batch.changedFiles, state.AssetDatabasePathString());
                batch.diagnostics.push_back("asset.import: " + asset.id + " " + asset.importedPath);
                state.AddActivity(command.type, "Complete", asset.id + " " + asset.name);
            } else {
                ok = false;
                batch.diagnostics.push_back("asset.import failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "asset.find") {
            batch.diagnostics.push_back("asset.find: " + std::to_string(state.Assets().size()) + " asset record(s).");
            for (const AssetRecord& asset : state.Assets()) {
                batch.diagnostics.push_back(asset.id + " " + asset.type + " " + asset.name + " -> " + asset.importedPath);
            }
            state.AddActivity(command.type, "Complete", std::to_string(state.Assets().size()) + " asset record(s).");
            continue;
        }

        if (command.type == "asset.getMetadata") {
            const std::string key = !command.assetId.empty() ? command.assetId :
                                    !command.entityName.empty() ? command.entityName : command.path;
            const AssetRecord* asset = state.FindAssetByIdOrName(key);
            if (asset == nullptr) {
                ok = false;
                batch.diagnostics.push_back("asset.getMetadata failed: asset not found: " + key);
                state.AddActivity(command.type, "Failed", "Asset not found: " + key);
            } else {
                batch.diagnostics.push_back("asset.getMetadata: " + asset->id + " type=" + asset->type +
                                            " imported=" + asset->importedPath + " resource=" + asset->resourcePath +
                                            " thumbnail=" + asset->thumbnailPath + " dependencies=" +
                                            std::to_string(asset->dependencies.size()) + " license=" + asset->license);
                state.AddActivity(command.type, "Complete", asset->id + " " + asset->name);
            }
            continue;
        }

        if (command.type == "asset.createMaterial") {
            AssetRecord material;
            std::string error;
            if (state.CreateMaterialAsset(command.entityName, command.propertyValue, &material, &error)) {
                AppendUnique(batch.changedFiles, material.importedPath);
                AppendUnique(batch.changedFiles, state.AssetDatabasePathString());
                batch.diagnostics.push_back("asset.createMaterial: " + material.id + " " + material.name);
                state.AddActivity(command.type, "Complete", material.name);
            } else {
                ok = false;
                batch.diagnostics.push_back("asset.createMaterial failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "asset.assignMaterial") {
            Entity* entity = state.FindEntityByName(command.targetEntityName);
            std::string error;
            if (entity != nullptr && state.AssignMaterialToEntity(entity->id, command.materialName, &error)) {
                AppendUnique(batch.changedEntities, entity->name);
                state.AddActivity(command.type, "Applied", entity->name + " material=" + command.materialName);
            } else {
                ok = false;
                batch.diagnostics.push_back("asset.assignMaterial failed: " + (error.empty() ? "target not found" : error));
                state.AddActivity(command.type, "Failed", error.empty() ? "Target not found." : error);
            }
            continue;
        }

        if (command.type == "script.create" || command.type == "script.modify") {
            std::filesystem::path writtenPath;
            std::string error;
            const bool wrote = command.type == "script.create"
                                   ? state.CreateScript(command.path, command.content, &writtenPath, &error)
                                   : state.ModifyScript(command.path, command.content, &writtenPath, &error);
            if (wrote) {
                AppendUnique(batch.changedFiles, writtenPath.string());
                state.AddActivity(command.type, "Complete", writtenPath.string());
            } else {
                ok = false;
                batch.diagnostics.push_back(command.type + " failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "script.compile") {
            const ScriptCompileResult compile = state.CompileScripts();
            ok = ok && compile.ok;
            for (const std::string& diagnostic : FormatScriptCompileDiagnostics(compile)) {
                batch.diagnostics.push_back(diagnostic);
            }
            if (!compile.manifestPath.empty()) {
                AppendUnique(batch.changedFiles, compile.manifestPath.string());
            }
            if (!compile.projectFilePath.empty()) {
                AppendUnique(batch.changedFiles, compile.projectFilePath.string());
            }
            if (!compile.apiStubPath.empty()) {
                AppendUnique(batch.changedFiles, compile.apiStubPath.string());
            }
            if (!compile.assemblyPath.empty() && std::filesystem::exists(compile.assemblyPath)) {
                AppendUnique(batch.changedFiles, compile.assemblyPath.string());
            }
            state.AddActivity(command.type, compile.ok ? "Passed" : "Failed",
                              compile.ok ? compile.assemblyPath.string() : "Script compile failed.");
            continue;
        }

        if (command.type == "script.getDiagnostics") {
            const std::vector<std::string> diagnostics = state.GetScriptDiagnostics(command.path);
            for (const std::string& diagnostic : diagnostics) {
                batch.diagnostics.push_back("script.getDiagnostics: " + diagnostic);
            }
            state.AddActivity(command.type, "Complete", JoinStrings(diagnostics, " "));
            continue;
        }

        if (command.type == "script.attachToEntity") {
            Entity* entity = state.FindEntityByName(command.targetEntityName);
            std::string error;
            if (entity != nullptr && state.AttachScriptToEntity(entity->id, command.path, command.componentType, &error)) {
                AppendUnique(batch.changedEntities, entity->name);
                state.AddActivity(command.type, "Applied", entity->name + " script=" + command.path);
            } else {
                ok = false;
                batch.diagnostics.push_back("script.attachToEntity failed: " + (error.empty() ? "target not found" : error));
                state.AddActivity(command.type, "Failed", error.empty() ? "Target not found." : error);
            }
            continue;
        }

        if (command.type == "build.packagePlayer" || command.type == "build.packageWindows") {
            ProjectBuildReport report;
            std::string error;
            const bool packageWindows = command.type == "build.packageWindows";
            const std::filesystem::path playerExecutable =
                packageWindows ? DefaultPlayerExecutablePathForGateway(state, "Windows")
                               : DefaultPlayerExecutablePathForGateway(state);
            const bool packaged = packageWindows ? state.PackageWindowsBuild(playerExecutable, &report, &error)
                                                 : state.PackagePlayerBuild(playerExecutable, &report, &error);
            if (packaged) {
                AppendUnique(batch.changedFiles, report.executablePath.string());
                AppendUnique(batch.changedFiles, report.manifestPath.string());
                if (!report.launcherPath.empty()) {
                    AppendUnique(batch.changedFiles, report.launcherPath.string());
                }
                if (!report.packagedProjectFile.empty()) {
                    AppendUnique(batch.changedFiles, report.packagedProjectFile.string());
                }
                state.AddActivity(command.type, "Complete", report.executablePath.string());
            } else {
                ok = false;
                batch.diagnostics.push_back(command.type + " failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "build.runSmokeTest") {
            const SceneValidationResult project = state.ValidateProject();
            const SceneValidationResult scene = state.ValidateScene();
            const SceneValidationResult assets = state.ValidateAssets();
            const SceneValidationResult scripts = state.ValidateScripts();
            const bool smokeOk = project.ok && scene.ok && assets.ok && scripts.ok;
            ok = ok && smokeOk;
            batch.diagnostics.push_back(std::string("build.runSmokeTest: in-editor smoke ") +
                                        (smokeOk ? "passed." : "failed."));
            AppendValidationDiagnostics(batch, state, command, project);
            AppendValidationDiagnostics(batch, state, command, scene);
            AppendValidationDiagnostics(batch, state, command, assets);
            AppendValidationDiagnostics(batch, state, command, scripts);
            continue;
        }

        if (command.type == "build.openOutputFolder") {
            state.AddActivity(command.type, "Ready", "Output folder: " + state.ProjectRootPathString());
            batch.diagnostics.push_back("build.openOutputFolder: " + state.ProjectRootPathString());
            continue;
        }

        if (command.type == "plugin.create") {
            PluginRecord plugin;
            std::string error;
            if (state.CreatePluginSkeleton(command.entityName, &plugin, &error)) {
                AppendUnique(batch.changedFiles, plugin.path + "/plugin.json");
                state.AddActivity(command.type, "Complete", plugin.name);
            } else {
                ok = false;
                batch.diagnostics.push_back("plugin.create failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "plugin.compile") {
            std::filesystem::path reportPath;
            std::string error;
            if (state.CompilePlugin(command.entityName, &reportPath, &error)) {
                AppendUnique(batch.changedFiles, reportPath.string());
                state.AddActivity(command.type, "Compiled", command.entityName);
            } else {
                ok = false;
                batch.diagnostics.push_back("plugin.compile failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "plugin.test") {
            std::filesystem::path reportPath;
            std::string error;
            if (state.TestPlugin(command.entityName, &reportPath, &error)) {
                AppendUnique(batch.changedFiles, reportPath.string());
                state.AddActivity(command.type, "Passed", command.entityName);
            } else {
                ok = false;
                batch.diagnostics.push_back("plugin.test failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "plugin.install") {
            PluginRecord plugin;
            std::string error;
            if (state.InstallPlugin(command.entityName, &plugin, &error)) {
                AppendUnique(batch.changedFiles, plugin.installPath + "/install_manifest.json");
                AppendUnique(batch.changedFiles, state.ProjectFilePathString());
                state.AddActivity(command.type, "Installed", plugin.name);
            } else {
                ok = false;
                batch.diagnostics.push_back("plugin.install failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        if (command.type == "plugin.enable" || command.type == "plugin.disable") {
            std::string error;
            const bool enable = command.type == "plugin.enable";
            if (state.SetPluginEnabled(command.entityName, enable, &error)) {
                AppendUnique(batch.changedFiles, state.ProjectFilePathString());
                state.AddActivity(command.type, enable ? "Enabled" : "Disabled", command.entityName);
            } else {
                ok = false;
                batch.diagnostics.push_back(command.type + " failed: " + error);
                state.AddActivity(command.type, "Failed", error);
            }
            continue;
        }

        ok = false;
        batch.diagnostics.push_back("Unsupported command: " + command.type);
        state.AddActivity(command.type, "Failed", "Unsupported command");
    }

    batch.ok = ok;
    batch.applied = true;
    batch.undoAvailable = state.CanUndoSceneEdit();
    state.AddLog(ok ? LogLevel::Info : LogLevel::Warning,
                 "Tool gateway applied " + batch.transactionId + ": " + batch.name + ".");
    if (!batch.changedEntities.empty()) {
        state.AddLog(LogLevel::Info, "Changed entities: " + JoinStrings(batch.changedEntities, ", "));
    }
    if (!batch.changedFiles.empty()) {
        state.AddLog(LogLevel::Info, "Changed files: " + JoinStrings(batch.changedFiles, ", "));
    }
    return ok;
}

ToolCommand ToolGateway::ProjectGetStateCommand() {
    ToolCommand command;
    command.type = "project.getState";
    command.summary = "Read current project, scene, selection, and log context.";
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::ProjectCreateCommand(std::string path) {
    ToolCommand command;
    command.type = "project.create";
    command.summary = "Create project at " + path + ".";
    command.path = std::move(path);
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::ProjectOpenCommand(std::string path) {
    ToolCommand command;
    command.type = "project.open";
    command.summary = "Open project at " + path + ".";
    command.path = std::move(path);
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::ProjectSaveCommand() {
    ToolCommand command;
    command.type = "project.save";
    command.summary = "Save project metadata and active scene JSON to disk.";
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::ProjectSetSaveLocationCommand(std::string path) {
    ToolCommand command;
    command.type = "project.setSaveLocation";
    command.summary = "Set game save folder to " + path + ".";
    command.path = std::move(path);
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::ValidateSceneCommand() {
    ToolCommand command;
    command.type = "validate.scene";
    command.summary = "Validate required scene data and component basics.";
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::ValidateProjectCommand() {
    ToolCommand command;
    command.type = "validate.project";
    command.summary = "Validate project folders, roots, and active scene paths.";
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::ValidateAssetsCommand() {
    ToolCommand command;
    command.type = "validate.assets";
    command.summary = "Validate imported asset records and imported files.";
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::ValidateScriptsCommand() {
    ToolCommand command;
    command.type = "validate.scripts";
    command.summary = "Validate script component file references.";
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::CreateEntityCommand(std::string name, std::vector<Component> components) {
    ToolCommand command;
    command.type = "scene.createEntity";
    command.summary = "Create entity " + name + ".";
    command.entityName = std::move(name);
    command.components = std::move(components);
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::RenameEntityCommand(std::string targetName, std::string newName) {
    ToolCommand command;
    command.type = "scene.renameEntity";
    command.summary = "Rename " + targetName + " to " + newName + ".";
    command.targetEntityName = std::move(targetName);
    command.newName = std::move(newName);
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::SetTransformCommand(std::string targetName, const std::array<float, 3>* position,
                                             const std::array<float, 3>* rotation, const std::array<float, 3>* scale) {
    ToolCommand command;
    command.type = "scene.setTransform";
    command.summary = "Set transform for " + targetName + ".";
    command.targetEntityName = std::move(targetName);
    if (position != nullptr) {
        command.position = *position;
        command.hasPosition = true;
    }
    if (rotation != nullptr) {
        command.rotation = *rotation;
        command.hasRotation = true;
    }
    if (scale != nullptr) {
        command.scale = *scale;
        command.hasScale = true;
    }
    UpdatePayload(command);
    return command;
}

ToolCommand ToolGateway::AddComponentCommand(std::string targetName, Component component) {
    ToolCommand command;
    command.type = "scene.addComponent";
    command.summary = "Add " + component.type + " to " + targetName + ".";
    command.targetEntityName = std::move(targetName);
    command.component = std::move(component);
    UpdatePayload(command);
    return command;
}

void ToolGateway::UpdatePayload(ToolCommand& command) {
    json payload{{"type", command.type}};

    if (command.type == "project.create" || command.type == "project.open" ||
        command.type == "project.setSaveLocation" || command.type == "scene.create" ||
        command.type == "scene.open" || command.type == "asset.import" ||
        command.type == "script.create" || command.type == "script.modify" ||
        command.type == "script.getDiagnostics") {
        payload["path"] = command.path;
        if (!command.sourceLabel.empty()) {
            payload["source"] = command.sourceLabel;
        }
        if (!command.license.empty()) {
            payload["license"] = command.license;
        }
        if (!command.content.empty()) {
            payload["content"] = command.content;
        }
    } else if (command.type == "scene.createEntity") {
        payload["name"] = command.entityName;
        payload["components"] = json::array();
        for (const Component& component : command.components) {
            payload["components"].push_back(ComponentToJson(component));
        }
    } else if (command.type == "scene.deleteEntity") {
        payload["targetName"] = command.targetEntityName;
    } else if (command.type == "scene.renameEntity") {
        payload["targetName"] = command.targetEntityName;
        payload["newName"] = command.newName;
    } else if (command.type == "scene.setParent") {
        payload["targetName"] = command.targetEntityName;
        payload["parentName"] = command.parentEntityName;
    } else if (command.type == "scene.setTransform") {
        payload["targetName"] = command.targetEntityName;
        json transform = json::object();
        if (command.hasPosition) {
            transform["position"] = Vec3ToJson(command.position);
        }
        if (command.hasRotation) {
            transform["rotation"] = Vec3ToJson(command.rotation);
        }
        if (command.hasScale) {
            transform["scale"] = Vec3ToJson(command.scale);
        }
        payload["transform"] = transform;
    } else if (command.type == "scene.addComponent") {
        payload["targetName"] = command.targetEntityName;
        payload["component"] = ComponentToJson(command.component);
    } else if (command.type == "scene.removeComponent") {
        payload["targetName"] = command.targetEntityName;
        payload["componentType"] = command.componentType;
    } else if (command.type == "scene.setComponentProperty") {
        payload["targetName"] = command.targetEntityName;
        payload["componentType"] = command.componentType;
        payload["propertyName"] = command.propertyName;
        payload["value"] = command.propertyValue;
    } else if (command.type == "asset.getMetadata") {
        payload["assetId"] = command.assetId;
        payload["name"] = command.entityName;
        payload["path"] = command.path;
    } else if (command.type == "asset.createMaterial") {
        payload["name"] = command.entityName;
        payload["color"] = command.propertyValue;
    } else if (command.type == "asset.assignMaterial") {
        payload["targetName"] = command.targetEntityName;
        payload["material"] = command.materialName;
    } else if (command.type == "script.attachToEntity") {
        payload["targetName"] = command.targetEntityName;
        payload["path"] = command.path;
        payload["componentType"] = command.componentType;
    } else if (command.type == "plugin.create" || command.type == "plugin.compile" || command.type == "plugin.test" ||
               command.type == "plugin.install" || command.type == "plugin.enable" || command.type == "plugin.disable") {
        payload["name"] = command.entityName;
    }

    command.payloadJson = payload.dump(2);
}

std::string ToolGateway::BuildAssistantMessage(const ToolCommandBatch& batch) {
    std::ostringstream stream;
    stream << "Local Tool Gateway transaction " << batch.transactionId << " ";
    stream << (batch.ok ? "completed" : "completed with diagnostics") << ".\n\n";
    stream << "Batch: " << batch.name << "\n";
    stream << "Approval: " << batch.approval << "\n";
    stream << "Commands: " << batch.commands.size() << "\n";

    if (!batch.changedEntities.empty()) {
        stream << "Changed entities: " << JoinStrings(batch.changedEntities, ", ") << "\n";
    }
    if (!batch.changedFiles.empty()) {
        stream << "Changed files: " << JoinStrings(batch.changedFiles, ", ") << "\n";
    }
    if (!batch.diagnostics.empty()) {
        stream << "Diagnostics: " << JoinStrings(batch.diagnostics, " ") << "\n";
    }
    stream << "Undo available: " << (batch.undoAvailable ? "yes" : "no") << "\n";

    stream << "\nThese are deterministic editor tool commands applied through the Local Tool Gateway.";
    return stream.str();
}

std::string ToolGateway::FirstQuotedText(const std::string& prompt) {
    const size_t doubleStart = prompt.find('"');
    if (doubleStart != std::string::npos) {
        const size_t doubleEnd = prompt.find('"', doubleStart + 1);
        if (doubleEnd != std::string::npos) {
            return prompt.substr(doubleStart + 1, doubleEnd - doubleStart - 1);
        }
    }

    const size_t singleStart = prompt.find('\'');
    if (singleStart != std::string::npos) {
        const size_t singleEnd = prompt.find('\'', singleStart + 1);
        if (singleEnd != std::string::npos) {
            return prompt.substr(singleStart + 1, singleEnd - singleStart - 1);
        }
    }

    return {};
}

bool ToolGateway::PromptLooksLikeRunningSimulatorRequest(const std::string& prompt) {
    const std::string normalized = ToLower(prompt);
    const bool explicitTemplate = normalized.find("running simulator scene template") != std::string::npos ||
                                  normalized.find("runner scene template") != std::string::npos ||
                                  normalized.find("create running simulator scene template") != std::string::npos ||
                                  normalized.find("create the running simulator scene") != std::string::npos;
    if (!explicitTemplate) {
        return false;
    }
    const bool mentionsRunning = normalized.find("running") != std::string::npos ||
                                 normalized.find("runner") != std::string::npos ||
                                 normalized.find("run ") != std::string::npos ||
                                 normalized.find("run-") != std::string::npos;
    const bool mentionsSimulator = normalized.find("simulator") != std::string::npos ||
                                   normalized.find("simulation") != std::string::npos ||
                                   normalized.find("symulator") != std::string::npos ||
                                   normalized.find("game scene") != std::string::npos;
    const bool mentionsPlayer = normalized.find("player") != std::string::npos ||
                                normalized.find("character") != std::string::npos ||
                                normalized.find("controller") != std::string::npos;
    const bool mentionsBuild = normalized.find("build") != std::string::npos ||
                               normalized.find("create") != std::string::npos ||
                               normalized.find("make") != std::string::npos ||
                               normalized.find("look at") != std::string::npos ||
                               normalized.find("prototype") != std::string::npos;
    return mentionsRunning && mentionsSimulator && mentionsPlayer && mentionsBuild;
}

bool ToolGateway::PromptLooksLikeColliderTroubleshootingRequest(const std::string& prompt) {
    const std::string normalized = ToLower(prompt);
    const bool mentionsCollider = normalized.find("collider") != std::string::npos ||
                                  normalized.find("collision") != std::string::npos ||
                                  normalized.find("trigger") != std::string::npos ||
                                  normalized.find("physics") != std::string::npos;
    const bool asksWhy = normalized.find("why") != std::string::npos ||
                         normalized.find("not working") != std::string::npos ||
                         normalized.find("isn't working") != std::string::npos ||
                         normalized.find("isnt working") != std::string::npos ||
                         normalized.find("aren't working") != std::string::npos ||
                         normalized.find("arent working") != std::string::npos ||
                         normalized.find("broken") != std::string::npos ||
                         normalized.find("pass through") != std::string::npos ||
                         normalized.find("passing through") != std::string::npos ||
                         normalized.find("no hit") != std::string::npos;
    return mentionsCollider && asksWhy;
}

bool ToolGateway::PromptLooksLikeStarterSceneRequest(const std::string& prompt) {
    const std::string normalized = ToLower(prompt);
    const bool asksForCreate = normalized.find("create") != std::string::npos ||
                               normalized.find("make") != std::string::npos ||
                               normalized.find("set up") != std::string::npos ||
                               normalized.find("setup") != std::string::npos ||
                               normalized.find("reset") != std::string::npos;
    const bool explicitStarterScene = normalized.find("starter scene") != std::string::npos ||
                                      normalized.find("start scene") != std::string::npos ||
                                      normalized.find("default scene") != std::string::npos ||
                                      normalized.find("prototype scene") != std::string::npos ||
                                      normalized.find("basic scene") != std::string::npos ||
                                      normalized.find("simple scene") != std::string::npos;
    const bool asksForStarterTemplate = normalized.find("starter") != std::string::npos &&
                                        (normalized.find("camera") != std::string::npos ||
                                         normalized.find("light") != std::string::npos ||
                                         normalized.find("ground") != std::string::npos ||
                                         normalized.find("spawn") != std::string::npos);
    return asksForCreate && (explicitStarterScene || asksForStarterTemplate);
}

std::string ToolGateway::ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace aine

#include "EditorState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

void SetProperty(aine::Component& component, const std::string& name, std::string value) {
    for (aine::ComponentProperty& property : component.properties) {
        if (property.name == name) {
            property.value = std::move(value);
            return;
        }
    }
    component.properties.push_back({name, std::move(value)});
}

bool HasComponentType(const aine::Entity& entity, const std::string& componentType) {
    return std::any_of(entity.components.begin(), entity.components.end(), [&componentType](const aine::Component& component) {
        return component.type == componentType;
    });
}

const aine::Component* FindComponent(const aine::Entity& entity, const std::string& componentType) {
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [&componentType](const aine::Component& component) {
        return component.type == componentType;
    });
    return it == entity.components.end() ? nullptr : &(*it);
}

aine::Component* FindComponent(aine::Entity& entity, const std::string& componentType) {
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [&componentType](const aine::Component& component) {
        return component.type == componentType;
    });
    return it == entity.components.end() ? nullptr : &(*it);
}

aine::Entity* FindEntity(std::vector<aine::Entity>& entities, const std::string& name) {
    auto it = std::find_if(entities.begin(), entities.end(), [&name](const aine::Entity& entity) {
        return entity.name == name;
    });
    return it == entities.end() ? nullptr : &(*it);
}

std::string ComponentProperty(const aine::Component& component, const std::string& name) {
    for (const aine::ComponentProperty& property : component.properties) {
        if (property.name == name) {
            return property.value;
        }
    }
    return {};
}

bool HasTriggerPair(const std::vector<aine::PhysicsEvent>& events, int leftId, int rightId) {
    return std::any_of(events.begin(), events.end(), [leftId, rightId](const aine::PhysicsEvent& event) {
        if (!event.trigger) {
            return false;
        }
        return (event.entityAId == leftId && event.entityBId == rightId) ||
               (event.entityAId == rightId && event.entityBId == leftId);
    });
}

bool HasCollisionPair(const std::vector<aine::PhysicsEvent>& events, int leftId, int rightId) {
    return std::any_of(events.begin(), events.end(), [leftId, rightId](const aine::PhysicsEvent& event) {
        if (event.trigger) {
            return false;
        }
        return (event.entityAId == leftId && event.entityBId == rightId) ||
               (event.entityAId == rightId && event.entityBId == leftId);
    });
}

bool HasRuntimeEvent(const std::vector<aine::RuntimeLogEntry>& logs, const std::string& eventName) {
    return std::any_of(logs.begin(), logs.end(), [&eventName](const aine::RuntimeLogEntry& entry) {
        return entry.event == eventName;
    });
}

bool NearlyEqual(float left, float right, float epsilon = 0.05f) {
    return std::fabs(left - right) <= epsilon;
}

bool HasEditorLogMessage(const aine::EditorState& state, const std::string& fragment) {
    const std::vector<aine::LogEntry>& logs = state.Logs();
    return std::any_of(logs.begin(), logs.end(), [&fragment](const aine::LogEntry& entry) {
        return entry.message.find(fragment) != std::string::npos;
    });
}

bool TestCollectibleUsesPhysicsTriggerEvent(std::vector<std::string>* diagnostics) {
    aine::EditorState state;
    state.ClearScene();

    aine::Component playerBody = aine::MakeRigidbody3DComponent("Kinematic");
    SetProperty(playerBody, "useGravity", "false");
    aine::Component playerCollider = aine::MakeCollider3DComponent("Box", false);
    aine::Entity& player =
        state.CreateEntity("Player", {aine::MakeTransformComponent(), aine::MakeCharacterControllerRuntimeComponent(),
                                      playerBody, playerCollider});
    const std::array<float, 3> playerPosition{0.0f, 0.5f, 0.0f};
    const std::array<float, 3> playerScale{1.0f, 1.0f, 1.0f};
    state.SetEntityTransform(player.id, &playerPosition, nullptr, &playerScale);

    aine::Component coinBody = aine::MakeRigidbody3DComponent("Static");
    SetProperty(coinBody, "useGravity", "false");
    aine::Component coinCollider = aine::MakeCollider3DComponent("Sphere", true);
    SetProperty(coinCollider, "radius", "0.75");
    aine::Entity& coin =
        state.CreateEntity("Physics Coin", {aine::MakeTransformComponent(), aine::MakeCollectibleRuntimeComponent(1, 0.05f),
                                            coinBody, coinCollider});
    const std::array<float, 3> coinPosition{0.6f, 0.5f, 0.0f};
    const std::array<float, 3> coinScale{1.0f, 1.0f, 1.0f};
    state.SetEntityTransform(coin.id, &coinPosition, nullptr, &coinScale);

    if (!state.BeginPlayMode()) {
        diagnostics->push_back("Runtime test failed: play mode did not start.");
        return false;
    }
    if (!HasRuntimeEvent(state.RuntimeLogs(), "play.start")) {
        diagnostics->push_back("Runtime test failed: play.start was not captured.");
        return false;
    }

    state.UpdatePlayMode(1.0f / 60.0f);

    if (!HasTriggerPair(state.PhysicsEvents(), player.id, coin.id)) {
        diagnostics->push_back("Runtime test failed: expected a physics trigger event between player and coin.");
        return false;
    }
    if (state.PlayScore() != 1) {
        diagnostics->push_back("Runtime test failed: collectible trigger event did not increment score.");
        return false;
    }
    if (!HasRuntimeEvent(state.RuntimeLogs(), "physics.trigger")) {
        diagnostics->push_back("Runtime test failed: physics.trigger was not captured.");
        return false;
    }
    if (!HasRuntimeEvent(state.RuntimeLogs(), "runtime.collectible")) {
        diagnostics->push_back("Runtime test failed: runtime.collectible was not captured.");
        return false;
    }
    if (!state.StopPlayMode() || !state.RuntimeLogs().empty()) {
        diagnostics->push_back("Runtime test failed: runtime logs were not cleared after stop.");
        return false;
    }
    return true;
}

bool TestCharacterControllerBlockedByStaticCollider(std::vector<std::string>* diagnostics) {
    aine::EditorState state;
    state.ClearScene();

    aine::Component playerBody = aine::MakeRigidbody3DComponent("Kinematic");
    SetProperty(playerBody, "useGravity", "false");
    aine::Entity& player =
        state.CreateEntity("Player", {aine::MakeTransformComponent(), aine::MakeCharacterControllerRuntimeComponent(),
                                      playerBody, aine::MakeCollider3DComponent("Box")});
    const std::array<float, 3> playerPosition{0.0f, 0.5f, 0.25f};
    const std::array<float, 3> playerScale{1.0f, 1.0f, 1.0f};
    state.SetEntityTransform(player.id, &playerPosition, nullptr, &playerScale);

    aine::Component wallBody = aine::MakeRigidbody3DComponent("Static");
    SetProperty(wallBody, "useGravity", "false");
    aine::Entity& wall =
        state.CreateEntity("Wall", {aine::MakeTransformComponent(), wallBody, aine::MakeCollider3DComponent("Box")});
    const std::array<float, 3> wallPosition{0.0f, 0.5f, -1.0f};
    const std::array<float, 3> wallScale{1.0f, 1.0f, 1.0f};
    state.SetEntityTransform(wall.id, &wallPosition, nullptr, &wallScale);

    if (!state.BeginPlayMode()) {
        diagnostics->push_back("Runtime test failed: play mode did not start for collider blocking test.");
        return false;
    }

    aine::RuntimeInputState input;
    input.character.moveZ = -1.0f;
    input.captured = true;
    input.captureSurface = "Game";
    state.SetRuntimeInputState(input);
    state.UpdatePlayMode(0.25f);

    const aine::Entity* runtimePlayer = state.FindEntity(player.id);
    if (runtimePlayer == nullptr) {
        diagnostics->push_back("Runtime test failed: player disappeared during collider blocking test.");
        return false;
    }
    if (!HasCollisionPair(state.PhysicsEvents(), player.id, wall.id)) {
        diagnostics->push_back("Runtime test failed: expected player and wall collision event.");
        return false;
    }
    if (runtimePlayer->position[2] < -0.02f) {
        diagnostics->push_back("Runtime test failed: kinematic character passed through a static collider.");
        return false;
    }
    return true;
}

bool TestColliderOnlyCharacterControllerBlockedByStaticCollider(std::vector<std::string>* diagnostics) {
    aine::EditorState state;
    state.ClearScene();

    aine::Entity& player =
        state.CreateEntity("Player", {aine::MakeTransformComponent(), aine::MakeCharacterControllerRuntimeComponent(),
                                      aine::MakeCollider3DComponent("Box")});
    const std::array<float, 3> playerPosition{0.0f, 0.5f, 0.25f};
    const std::array<float, 3> playerScale{1.0f, 1.0f, 1.0f};
    state.SetEntityTransform(player.id, &playerPosition, nullptr, &playerScale);

    aine::Entity& wall =
        state.CreateEntity("Wall", {aine::MakeTransformComponent(), aine::MakeCollider3DComponent("Box")});
    const std::array<float, 3> wallPosition{0.0f, 0.5f, -1.0f};
    const std::array<float, 3> wallScale{1.0f, 1.0f, 1.0f};
    state.SetEntityTransform(wall.id, &wallPosition, nullptr, &wallScale);

    if (!state.BeginPlayMode()) {
        diagnostics->push_back("Runtime test failed: play mode did not start for implicit character collider test.");
        return false;
    }

    aine::RuntimeInputState input;
    input.character.moveZ = -1.0f;
    input.captured = true;
    input.captureSurface = "Game";
    state.SetRuntimeInputState(input);
    state.UpdatePlayMode(0.25f);

    const aine::Entity* runtimePlayer = state.FindEntity(player.id);
    if (runtimePlayer == nullptr) {
        diagnostics->push_back("Runtime test failed: implicit character disappeared during collider test.");
        return false;
    }
    if (!HasCollisionPair(state.PhysicsEvents(), player.id, wall.id)) {
        diagnostics->push_back("Runtime test failed: expected implicit player and wall collision event.");
        return false;
    }
    if (runtimePlayer->position[2] < -0.02f) {
        diagnostics->push_back("Runtime test failed: collider-only character passed through a static collider.");
        return false;
    }
    return true;
}

bool TestEditorCreatedDynamicBodyFallsWithGravity(std::vector<std::string>* diagnostics) {
    aine::EditorState state;
    state.ClearScene();

    aine::Entity& cube =
        state.CreateEntity("Falling Editor Cube", {aine::MakeTransformComponent(), aine::MakeMeshPlaceholderComponent("UnitCube")});
    const int cubeId = cube.id;
    const std::array<float, 3> startPosition{0.0f, 5.0f, 0.0f};
    state.SetEntityTransform(cubeId, &startPosition, nullptr, nullptr);

    if (!state.AddComponentToEntity(cubeId, aine::MakeRigidbody3DComponent("Dynamic"))) {
        diagnostics->push_back("Editor physics wiring test failed: could not add Rigidbody3D.");
        return false;
    }

    const aine::Entity* editCube = state.FindEntity(cubeId);
    const aine::Entity* settings = state.FindEntityByName("Physics Settings");
    if (editCube == nullptr || !HasComponentType(*editCube, "Rigidbody3D") || !HasComponentType(*editCube, "Collider3D")) {
        diagnostics->push_back("Editor physics wiring test failed: Rigidbody3D did not produce a usable collider-backed body.");
        return false;
    }
    if (settings == nullptr || !HasComponentType(*settings, "PhysicsSettings")) {
        diagnostics->push_back("Editor physics wiring test failed: physics scene did not get a durable PhysicsSettings entity.");
        return false;
    }

    if (!state.BeginPlayMode()) {
        diagnostics->push_back("Editor physics wiring test failed: play mode did not start.");
        return false;
    }

    for (int step = 0; step < 120; ++step) {
        state.UpdatePlayMode(1.0f / 60.0f);
    }

    const aine::Entity* runtimeCube = state.FindEntity(cubeId);
    if (runtimeCube == nullptr) {
        diagnostics->push_back("Editor physics wiring test failed: runtime cube disappeared.");
        return false;
    }
    if (state.Physics().LastBody3DCount() < 1 || state.Physics().LastCollider3DCount() < 1) {
        diagnostics->push_back("Editor physics wiring test failed: runtime physics did not see the editor-created body/collider.");
        return false;
    }
    if (runtimeCube->position[1] > 1.5f) {
        diagnostics->push_back("Editor physics wiring test failed: dynamic body did not fall under default -9.81 gravity.");
        return false;
    }

    if (!state.StopPlayMode()) {
        diagnostics->push_back("Editor physics wiring test failed: play mode did not stop.");
        return false;
    }
    const aine::Entity* restoredCube = state.FindEntity(cubeId);
    if (restoredCube == nullptr || std::fabs(restoredCube->position[1] - startPosition[1]) > 0.001f) {
        diagnostics->push_back("Editor physics wiring test failed: stop did not restore the edit-mode transform.");
        return false;
    }
    return true;
}

bool TestProjectSettingsTagsLayersAndCollisionMatrix(std::vector<std::string>* diagnostics) {
    const std::filesystem::path projectRoot =
        std::filesystem::current_path() / "build" / "runtime_project_settings_test";

    aine::EditorState state;
    std::string error;
    if (!state.NewProject(projectRoot, &error)) {
        diagnostics->push_back("Project settings test failed: could not create project: " + error);
        return false;
    }

    if (!state.AddProjectTag("Boss", &error) ||
        !state.AddProjectLayer("Actors", &error) ||
        !state.AddProjectLayer("Projectiles", &error) ||
        !state.SetLayerCollisionEnabled("Actors", "Projectiles", false, &error)) {
        diagnostics->push_back("Project settings test failed: could not configure tags, layers, or matrix: " + error);
        return false;
    }

    state.ClearScene();
    aine::Component actorBody = aine::MakeRigidbody3DComponent("Kinematic");
    SetProperty(actorBody, "useGravity", "false");
    aine::Entity& actor =
        state.CreateEntity("Actor", {aine::MakeTransformComponent(), actorBody, aine::MakeCollider3DComponent("Box")});
    actor.tag = "Boss";
    actor.layer = "Actors";
    const std::array<float, 3> actorPosition{0.0f, 0.5f, 0.0f};
    state.SetEntityTransform(actor.id, &actorPosition, nullptr, nullptr);

    aine::Component projectileBody = aine::MakeRigidbody3DComponent("Static");
    SetProperty(projectileBody, "useGravity", "false");
    aine::Entity& projectile = state.CreateEntity(
        "Projectile", {aine::MakeTransformComponent(), projectileBody, aine::MakeCollider3DComponent("Box")});
    projectile.layer = "Projectiles";
    const std::array<float, 3> projectilePosition{0.0f, 0.5f, 0.0f};
    state.SetEntityTransform(projectile.id, &projectilePosition, nullptr, nullptr);
    const int actorId = actor.id;
    const int projectileId = projectile.id;
    state.EnsurePhysicsSettingsEntity();

    if (!state.BeginPlayMode()) {
        diagnostics->push_back("Project settings test failed: play mode did not start with disabled layer pair.");
        return false;
    }
    state.UpdatePlayMode(1.0f / 60.0f);
    if (HasCollisionPair(state.PhysicsEvents(), actorId, projectileId)) {
        diagnostics->push_back("Project settings test failed: disabled layer pair emitted a collision.");
        state.StopPlayMode();
        return false;
    }
    state.StopPlayMode();

    if (!state.SetLayerCollisionEnabled("Actors", "Projectiles", true, &error)) {
        diagnostics->push_back("Project settings test failed: could not re-enable layer pair: " + error);
        return false;
    }
    if (!state.BeginPlayMode()) {
        diagnostics->push_back("Project settings test failed: play mode did not start with enabled layer pair.");
        return false;
    }
    state.UpdatePlayMode(1.0f / 60.0f);
    if (!HasCollisionPair(state.PhysicsEvents(), actorId, projectileId)) {
        diagnostics->push_back("Project settings test failed: enabled layer pair did not emit a collision.");
        state.StopPlayMode();
        return false;
    }
    state.StopPlayMode();

    if (!state.SetLayerCollisionEnabled("Actors", "Projectiles", false, &error) ||
        !state.SaveProject(&error)) {
        diagnostics->push_back("Project settings test failed: could not save matrix: " + error);
        return false;
    }

    aine::EditorState reopened;
    if (!reopened.OpenProject(projectRoot / "AI Native Project.aineproject.json", &error)) {
        diagnostics->push_back("Project settings test failed: could not reopen project: " + error);
        return false;
    }
    const bool hasBossTag =
        std::find(reopened.ProjectTags().begin(), reopened.ProjectTags().end(), "Boss") != reopened.ProjectTags().end();
    const bool hasActorsLayer = std::find(reopened.ProjectLayers().begin(), reopened.ProjectLayers().end(), "Actors") !=
                                reopened.ProjectLayers().end();
    const bool hasProjectilesLayer =
        std::find(reopened.ProjectLayers().begin(), reopened.ProjectLayers().end(), "Projectiles") !=
        reopened.ProjectLayers().end();
    if (!hasBossTag || !hasActorsLayer || !hasProjectilesLayer ||
        reopened.LayerCollisionEnabled("Actors", "Projectiles")) {
        diagnostics->push_back("Project settings test failed: reopened project lost tags, layers, or matrix state.");
        return false;
    }

    return true;
}

bool TestHierarchyParentingRules(std::vector<std::string>* diagnostics) {
    aine::EditorState state;
    state.ClearScene();

    const int parentId = state.CreateEntity("Parent", {aine::MakeTransformComponent()}).id;
    const int childId = state.CreateEntity("Child", {aine::MakeTransformComponent()}).id;
    const int grandchildId = state.CreateEntity("Grandchild", {aine::MakeTransformComponent()}).id;

    if (!state.SetEntityParent(childId, parentId) || !state.SetEntityParent(grandchildId, childId)) {
        diagnostics->push_back("Hierarchy test failed: valid parent chain was rejected.");
        return false;
    }
    if (state.CanSetEntityParent(parentId, grandchildId) || state.SetEntityParent(parentId, grandchildId)) {
        diagnostics->push_back("Hierarchy test failed: cycle parent assignment was accepted.");
        return false;
    }
    if (!state.IsEntityActiveInHierarchy(grandchildId)) {
        diagnostics->push_back("Hierarchy test failed: active child chain reported inactive.");
        return false;
    }

    aine::Entity* parent = state.FindEntity(parentId);
    const aine::Entity* child = state.FindEntity(childId);
    if (parent == nullptr || child == nullptr) {
        diagnostics->push_back("Hierarchy test failed: parent or child disappeared.");
        return false;
    }
    parent->activeSelf = false;
    if (child->activeSelf != true || state.IsEntityActiveInHierarchy(childId) ||
        state.IsEntityActiveInHierarchy(grandchildId)) {
        diagnostics->push_back("Hierarchy test failed: inactive parent did not disable descendants in hierarchy.");
        return false;
    }

    parent->activeSelf = true;
    if (!state.SetEntityParent(childId, 0)) {
        diagnostics->push_back("Hierarchy test failed: moving child to root did not preserve its subtree.");
        return false;
    }
    child = state.FindEntity(childId);
    const aine::Entity* grandchild = state.FindEntity(grandchildId);
    if (child == nullptr || grandchild == nullptr || child->parentId != 0 || grandchild->parentId != childId) {
        diagnostics->push_back("Hierarchy test failed: moving child to root did not preserve its subtree.");
        return false;
    }
    return true;
}

bool TestProjectFolderAndSaveDataRoot(std::vector<std::string>* diagnostics) {
    namespace fs = std::filesystem;
    using json = nlohmann::json;

    const fs::path projectRoot = fs::current_path() / "build" / "runtime-project-folder-test";
    const fs::path expectedSceneRoot = projectRoot / "Assets" / "Scenes";
    const fs::path expectedSceneFile = expectedSceneRoot / "Prototype.scene.json";
    const fs::path expectedSaveRoot = projectRoot / "SavedGames" / "SlotA";

    aine::EditorState state;
    std::string error;
    if (!state.NewProject(projectRoot, &error)) {
        diagnostics->push_back("Project folder test failed: NewProject failed: " + error);
        return false;
    }
    if (fs::absolute(state.SceneRootPathString()).lexically_normal() != fs::absolute(expectedSceneRoot).lexically_normal()) {
        diagnostics->push_back("Project folder test failed: new project did not use Assets/Scenes.");
        return false;
    }
    if (!fs::exists(expectedSceneFile)) {
        diagnostics->push_back("Project folder test failed: active scene was not written under Assets/Scenes.");
        return false;
    }
    if (!fs::exists(state.GameSaveRootPathString())) {
        diagnostics->push_back("Project folder test failed: default game save folder was not created.");
        return false;
    }

    if (!state.SetGameSaveRootPath(fs::path("SavedGames") / "SlotA", &error) || !state.SaveProject(&error)) {
        diagnostics->push_back("Project folder test failed: save data root could not be changed and saved: " + error);
        return false;
    }
    if (!fs::exists(expectedSaveRoot)) {
        diagnostics->push_back("Project folder test failed: custom game save folder was not created.");
        return false;
    }

    try {
        std::ifstream input(state.ProjectFilePathString());
        const json document = json::parse(input);
        const json directories = document.contains("directories") && document.at("directories").is_object()
                                     ? document.at("directories")
                                     : json::object();
        if (document.value("activeScene", std::string{}) != "Assets/Scenes/Prototype.scene.json" ||
            directories.value("assetRoot", std::string{}) != "Assets" ||
            directories.value("sceneRoot", std::string{}) != "Assets/Scenes" ||
            directories.value("saveDataRoot", std::string{}) != "SavedGames/SlotA") {
            diagnostics->push_back("Project folder test failed: project metadata did not persist expected directories.");
            return false;
        }
    } catch (const std::exception& exception) {
        diagnostics->push_back(std::string("Project folder test failed: project metadata parse failed: ") + exception.what());
        return false;
    }

    aine::EditorState reopened;
    if (!reopened.OpenProject(projectRoot, &error)) {
        diagnostics->push_back("Project folder test failed: reopening project root failed: " + error);
        return false;
    }
    if (fs::absolute(reopened.GameSaveRootPathString()).lexically_normal() != fs::absolute(expectedSaveRoot).lexically_normal()) {
        diagnostics->push_back("Project folder test failed: custom save data root did not survive reopen.");
        return false;
    }
    if (!reopened.SaveSceneAs("Second.scene.json", &error)) {
        diagnostics->push_back("Project folder test failed: SaveSceneAs filename failed: " + error);
        return false;
    }
    if (fs::absolute(reopened.SceneFilePathString()).lexically_normal() !=
        fs::absolute(expectedSceneRoot / "Second.scene.json").lexically_normal()) {
        diagnostics->push_back("Project folder test failed: SaveSceneAs filename did not target scene root.");
        return false;
    }

    const fs::path saveAsRoot = fs::current_path() / "build" / "runtime-project-save-as-test";
    if (!reopened.SaveProjectAs(saveAsRoot, &error)) {
        diagnostics->push_back("Project folder test failed: SaveProjectAs failed: " + error);
        return false;
    }
    if (!fs::exists(saveAsRoot / "AI Native Project.aineproject.json") ||
        !fs::exists(saveAsRoot / "Assets" / "Scenes" / "Second.scene.json")) {
        diagnostics->push_back("Project folder test failed: SaveProjectAs did not write project and active scene under the target root.");
        return false;
    }

    aine::EditorState saveAsReopened;
    if (!saveAsReopened.OpenProject(saveAsRoot, &error)) {
        diagnostics->push_back("Project folder test failed: SaveProjectAs target did not reopen: " + error);
        return false;
    }
    if (fs::absolute(saveAsReopened.ProjectRootPathString()).lexically_normal() != fs::absolute(saveAsRoot).lexically_normal() ||
        saveAsReopened.SceneName() != "Second.scene.json") {
        diagnostics->push_back("Project folder test failed: SaveProjectAs target did not preserve project root and active scene.");
        return false;
    }

    const fs::path legacyRoot = fs::current_path() / "build" / "runtime-legacy-scenes-project";
    const fs::path legacySceneRoot = legacyRoot / "Scenes";
    const fs::path legacySceneFile = legacySceneRoot / "Legacy.scene.json";
    try {
        fs::create_directories(legacySceneRoot);
        std::ofstream sceneOutput(legacySceneFile);
        sceneOutput << json{{"format", "aine.scene"},
                            {"version", 1},
                            {"name", "Legacy.scene.json"},
                            {"entities", json::array()}}
                           .dump(2)
                    << '\n';
        std::ofstream projectOutput(legacyRoot / "AI Native Project.aineproject.json");
        projectOutput << json{{"format", "aine.project"},
                              {"version", 1},
                              {"projectName", "Legacy Scenes Project"},
                              {"assetRoot", "Assets"},
                              {"activeScene", "Scenes/Legacy.scene.json"},
                              {"scenes", json::array({"Scenes/Legacy.scene.json"})},
                              {"enabledPlugins", json::array()}}
                             .dump(2)
                      << '\n';
    } catch (const std::exception& exception) {
        diagnostics->push_back(std::string("Project folder test failed: could not write legacy fixture: ") + exception.what());
        return false;
    }

    aine::EditorState legacy;
    if (!legacy.OpenProject(legacyRoot, &error)) {
        diagnostics->push_back("Project folder test failed: legacy root-level Scenes project did not open: " + error);
        return false;
    }
    if (fs::absolute(legacy.SceneRootPathString()).lexically_normal() != fs::absolute(legacySceneRoot).lexically_normal()) {
        diagnostics->push_back("Project folder test failed: legacy scene root was not inferred from activeScene.");
        return false;
    }

    return true;
}

bool TestRuntimeUiSerializationValidationAndBindings(std::vector<std::string>* diagnostics) {
    namespace fs = std::filesystem;

    const fs::path projectRoot = fs::current_path() / "build" / "runtime-ui-serialization-test";
    std::error_code cleanupError;
    fs::remove_all(projectRoot, cleanupError);

    aine::EditorState state;
    std::string error;
    if (!state.NewProject(projectRoot, &error)) {
        diagnostics->push_back("Runtime UI test failed: NewProject failed: " + error);
        return false;
    }
    state.ClearScene();

    aine::Entity& camera = state.CreateEntity("UI Test Camera", {aine::MakeTransformComponent(), aine::MakeCameraComponent()});
    const std::array<float, 3> cameraPosition{0.0f, 2.0f, 8.0f};
    state.SetEntityTransform(camera.id, &cameraPosition, nullptr, nullptr);

    aine::Entity& player =
        state.CreateEntity("UI Test Player",
                           {aine::MakeTransformComponent(), aine::MakeCharacterControllerRuntimeComponent()});
    const std::array<float, 3> playerPosition{0.0f, 0.5f, 0.0f};
    state.SetEntityTransform(player.id, &playerPosition, nullptr, nullptr);

    aine::Entity& rules = state.CreateEntity("UI Test Game Rules",
                                             {aine::MakeTransformComponent(), aine::MakeInputActionMapRuntimeComponent(),
                                              aine::MakeGameRulesRuntimeComponent("Runtime UI Test", 0)});
    state.SetComponentPropertyOnEntity(rules.id, "GameRulesRuntime", "fallDeathY", "-2");
    aine::Entity& goal = state.CreateEntity("UI Test Goal", {aine::MakeTransformComponent(), aine::MakeGoalRuntimeComponent(0, 1.0f)});
    const std::array<float, 3> goalPosition{0.0f, 0.5f, -3.0f};
    state.SetEntityTransform(goal.id, &goalPosition, nullptr, nullptr);

    aine::Entity& canvas = state.CreateEntity("Runtime UI Canvas", {aine::MakeTransformComponent(), aine::MakeUICanvasRuntimeComponent()});
    const int canvasId = canvas.id;
    const std::string canvasName = canvas.name;
    aine::Component panel = aine::MakeUIPanelRuntimeComponent();
    SetProperty(panel, "canvasName", canvasName);
    SetProperty(panel, "size", "480, 280");
    aine::Entity& panelEntity =
        state.CreateEntity("Runtime UI Panel",
                           {aine::MakeTransformComponent(), panel,
                            aine::MakeUIStateBindingRuntimeComponent("playFailed", true, true)});
    const int panelId = panelEntity.id;
    panelEntity.activeSelf = false;
    if (!state.SetEntityParent(panelId, canvasId)) {
        diagnostics->push_back("Runtime UI test failed: panel could not be parented under canvas.");
        return false;
    }

    aine::Component text = aine::MakeUITextRuntimeComponent("Game Over");
    SetProperty(text, "canvasName", canvasName);
    SetProperty(text, "position", "0, -48");
    aine::Entity& textEntity = state.CreateEntity("Runtime UI Text", {aine::MakeTransformComponent(), text});
    const int textId = textEntity.id;
    if (!state.SetEntityParent(textId, panelId)) {
        diagnostics->push_back("Runtime UI test failed: text could not be parented under panel.");
        return false;
    }

    aine::Component button = aine::MakeUIButtonRuntimeComponent("Restart", "restart");
    SetProperty(button, "canvasName", canvasName);
    SetProperty(button, "position", "0, 70");
    aine::Entity& buttonEntity = state.CreateEntity("Runtime UI Button", {aine::MakeTransformComponent(), button});
    const int buttonId = buttonEntity.id;
    if (!state.SetEntityParent(buttonId, panelId)) {
        diagnostics->push_back("Runtime UI test failed: button could not be parented under panel.");
        return false;
    }

    aine::Component box = aine::MakeUIBoxRuntimeComponent();
    SetProperty(box, "canvasName", canvasName);
    SetProperty(box, "position", "0, 8");
    SetProperty(box, "size", "320, 2");
    aine::Entity& boxEntity = state.CreateEntity("Runtime UI Box", {aine::MakeTransformComponent(), box});
    const int boxId = boxEntity.id;
    if (!state.SetEntityParent(boxId, panelId)) {
        diagnostics->push_back("Runtime UI test failed: box could not be parented under panel.");
        return false;
    }

    state.AddComponentToEntity(panelId, aine::MakeUILayoutGroupRuntimeComponent());
    state.AddComponentToEntity(buttonId, aine::MakeUIFocusRuntimeComponent());
    state.AddComponentToEntity(buttonId, aine::MakeUIScriptCallbackRuntimeComponent("OnClick"));
    state.AddComponentToEntity(textId, aine::MakeUIAnimationRuntimeComponent());
    aine::Component image = aine::MakeUIImageRuntimeComponent("SmokeImage");
    SetProperty(image, "canvasName", canvasName);
    SetProperty(image, "position", "0, 24");
    SetProperty(image, "size", "64, 64");
    aine::Entity& imageEntity = state.CreateEntity("Runtime UI Image", {aine::MakeTransformComponent(), image});
    const int imageId = imageEntity.id;
    if (!state.SetEntityParent(imageId, panelId)) {
        diagnostics->push_back("Runtime UI test failed: image could not be parented under panel.");
        return false;
    }

    const aine::SceneValidationResult validation = state.ValidateScene();
    if (!validation.ok) {
        diagnostics->push_back("Runtime UI test failed: valid UI scene did not pass validation: " +
                               (validation.diagnostics.empty() ? std::string{} : validation.diagnostics.front()));
        return false;
    }
    if (!state.SaveProject(&error)) {
        diagnostics->push_back("Runtime UI test failed: SaveProject failed: " + error);
        return false;
    }

    aine::EditorState reopened;
    if (!reopened.OpenProject(projectRoot, &error)) {
        diagnostics->push_back("Runtime UI test failed: reopened project failed: " + error);
        return false;
    }
    const aine::Entity* reopenedPanel = reopened.FindEntityByName("Runtime UI Panel");
    const aine::Entity* reopenedButton = reopened.FindEntityByName("Runtime UI Button");
    const aine::Entity* reopenedText = reopened.FindEntityByName("Runtime UI Text");
    const aine::Entity* reopenedImage = reopened.FindEntityByName("Runtime UI Image");
    if (reopenedPanel == nullptr || reopenedButton == nullptr || reopenedText == nullptr || reopenedImage == nullptr ||
        !HasComponentType(*reopenedPanel, "UIPanelRuntime") ||
        !HasComponentType(*reopenedPanel, "UILayoutGroupRuntime") ||
        !HasComponentType(*reopenedPanel, "UIStateBindingRuntime") ||
        !HasComponentType(*reopenedButton, "UIButtonRuntime") ||
        !HasComponentType(*reopenedButton, "UIFocusRuntime") ||
        !HasComponentType(*reopenedButton, "UIScriptCallbackRuntime") ||
        !HasComponentType(*reopenedText, "UIAnimationRuntime") ||
        !HasComponentType(*reopenedImage, "UIImageRuntime") ||
        reopenedButton->parentId != reopenedPanel->id) {
        diagnostics->push_back("Runtime UI test failed: UI components or hierarchy did not survive reload.");
        return false;
    }
    if (!reopened.ValidateScene().ok) {
        diagnostics->push_back("Runtime UI test failed: reloaded UI scene did not validate.");
        return false;
    }

    if (!reopened.BeginPlayMode()) {
        diagnostics->push_back("Runtime UI test failed: play mode did not start.");
        return false;
    }
    aine::Entity* runtimePlayer = reopened.FindEntityByName("UI Test Player");
    if (runtimePlayer == nullptr) {
        diagnostics->push_back("Runtime UI test failed: player disappeared in play mode.");
        return false;
    }
    const std::array<float, 3> fallenPosition{0.0f, -3.0f, 0.0f};
    reopened.SetEntityTransform(runtimePlayer->id, &fallenPosition, nullptr, nullptr);
    reopened.UpdatePlayMode(1.0f / 60.0f);
    reopenedPanel = reopened.FindEntityByName("Runtime UI Panel");
    if (!reopened.PlayFailed() || reopenedPanel == nullptr || !reopenedPanel->activeSelf) {
        diagnostics->push_back("Runtime UI test failed: playFailed binding did not reveal the panel.");
        return false;
    }
    aine::Entity* runtimeButton = reopened.FindEntityByName("Runtime UI Button");
    aine::Entity* runtimeText = reopened.FindEntityByName("Runtime UI Text");
    const aine::Component* focus = runtimeButton == nullptr ? nullptr : FindComponent(*runtimeButton, "UIFocusRuntime");
    const aine::Component* animation = runtimeText == nullptr ? nullptr : FindComponent(*runtimeText, "UIAnimationRuntime");
    if (focus == nullptr || ComponentProperty(*focus, "focused") != "true" || animation == nullptr ||
        std::stof(ComponentProperty(*animation, "currentTime")) <= 0.0f) {
        diagnostics->push_back("Runtime UI test failed: focus or UI animation state did not update in play mode.");
        return false;
    }

    aine::EditorState missingCanvas;
    missingCanvas.ClearScene();
    missingCanvas.CreateEntity("Loose UI Text", {aine::MakeTransformComponent(), aine::MakeUITextRuntimeComponent("Loose")});
    if (missingCanvas.ValidateScene().ok) {
        diagnostics->push_back("Runtime UI test failed: loose UI text without a canvas validated.");
        return false;
    }

    aine::EditorState emptyButtonAction;
    emptyButtonAction.ClearScene();
    aine::Entity& invalidCanvas =
        emptyButtonAction.CreateEntity("Invalid UI Canvas", {aine::MakeTransformComponent(), aine::MakeUICanvasRuntimeComponent()});
    const int invalidCanvasId = invalidCanvas.id;
    aine::Entity& invalidButton =
        emptyButtonAction.CreateEntity("Invalid Button",
                                       {aine::MakeTransformComponent(), aine::MakeUIButtonRuntimeComponent("Broken", "")});
    emptyButtonAction.SetEntityParent(invalidButton.id, invalidCanvasId);
    if (emptyButtonAction.ValidateScene().ok) {
        diagnostics->push_back("Runtime UI test failed: empty button action validated.");
        return false;
    }

    aine::EditorState badBinding;
    badBinding.ClearScene();
    aine::Entity& bindingCanvas =
        badBinding.CreateEntity("Binding Canvas", {aine::MakeTransformComponent(), aine::MakeUICanvasRuntimeComponent()});
    const int bindingCanvasId = bindingCanvas.id;
    aine::Component badBindingComponent = aine::MakeUIStateBindingRuntimeComponent("missingState", true, true);
    aine::Entity& badPanel =
        badBinding.CreateEntity("Bad Binding Panel", {aine::MakeTransformComponent(), aine::MakeUIPanelRuntimeComponent(),
                                                      badBindingComponent});
    badBinding.SetEntityParent(badPanel.id, bindingCanvasId);
    if (badBinding.ValidateScene().ok) {
        diagnostics->push_back("Runtime UI test failed: unsupported UI state binding validated.");
        return false;
    }

    return true;
}

bool TestRuntimeSubsystemComponentsStepAndPersist(std::vector<std::string>* diagnostics) {
    aine::EditorState state;
    state.ClearScene();

    aine::Entity& target = state.CreateEntity("Subsystem Target", {aine::MakeTransformComponent()});
    const std::array<float, 3> targetPosition{0.0f, 0.0f, 1.0f};
    state.SetEntityTransform(target.id, &targetPosition, nullptr, nullptr);

    aine::Component navigationComponent = aine::MakeNavigationAgentRuntimeComponent("Subsystem Target");
    SetProperty(navigationComponent, "stoppingDistance", "0.2");
    aine::Entity& actor =
        state.CreateEntity("Subsystem Actor",
                           {aine::MakeTransformComponent(),
                            aine::MakeAudioSourceRuntimeComponent("SmokeClip"),
                            aine::MakeAnimatorRuntimeComponent("Run"),
                            navigationComponent,
                            aine::MakeNetworkIdentityRuntimeComponent("smoke-actor")});
    const std::array<float, 3> actorPosition{0.0f, 0.0f, 0.0f};
    state.SetEntityTransform(actor.id, &actorPosition, nullptr, nullptr);

    state.CreateEntity("Subsystem Network Session",
                       {aine::MakeTransformComponent(), aine::MakeNetworkSessionRuntimeComponent()});

    if (!state.BeginPlayMode()) {
        diagnostics->push_back("Runtime subsystem test failed: play mode did not start.");
        return false;
    }
    state.UpdatePlayMode(0.5f);
    state.UpdatePlayMode(0.1f);
    state.UpdatePlayMode(0.1f);

    aine::Entity* runtimeActor = state.FindEntityByName("Subsystem Actor");
    aine::Entity* runtimeSession = state.FindEntityByName("Subsystem Network Session");
    if (runtimeActor == nullptr || runtimeSession == nullptr) {
        diagnostics->push_back("Runtime subsystem test failed: runtime entities are missing.");
        return false;
    }

    const aine::Component* audio = FindComponent(*runtimeActor, "AudioSourceRuntime");
    const aine::Component* animator = FindComponent(*runtimeActor, "AnimatorRuntime");
    const aine::Component* navigation = FindComponent(*runtimeActor, "NavigationAgentRuntime");
    const aine::Component* identity = FindComponent(*runtimeActor, "NetworkIdentityRuntime");
    const aine::Component* session = FindComponent(*runtimeSession, "NetworkSessionRuntime");
    if (audio == nullptr || animator == nullptr || navigation == nullptr || identity == nullptr || session == nullptr) {
        diagnostics->push_back("Runtime subsystem test failed: expected runtime components are missing.");
        return false;
    }
    if (ComponentProperty(*audio, "played") != "true" ||
        std::stof(ComponentProperty(*animator, "currentTime")) <= 0.0f ||
        ComponentProperty(*navigation, "reached") != "true" ||
        ComponentProperty(*identity, "registered") != "true" ||
        std::stof(ComponentProperty(*identity, "lastReplicatedTime")) <= 0.0f ||
        ComponentProperty(*session, "status") != "Connected" ||
        ComponentProperty(*session, "tick") == "0") {
        std::ostringstream stream;
        stream << "Runtime subsystem test failed: component runtime state did not update. "
               << "played=" << ComponentProperty(*audio, "played")
               << " animatorTime=" << ComponentProperty(*animator, "currentTime")
               << " reached=" << ComponentProperty(*navigation, "reached")
               << " registered=" << ComponentProperty(*identity, "registered")
               << " replicated=" << ComponentProperty(*identity, "lastReplicatedTime")
               << " session=" << ComponentProperty(*session, "status")
               << " tick=" << ComponentProperty(*session, "tick");
        diagnostics->push_back(stream.str());
        return false;
    }
    if (!HasRuntimeEvent(state.RuntimeLogs(), "audio.play") ||
        !HasRuntimeEvent(state.RuntimeLogs(), "navigation.reached") ||
        !HasRuntimeEvent(state.RuntimeLogs(), "network.session") ||
        !HasRuntimeEvent(state.RuntimeLogs(), "network.identity")) {
        diagnostics->push_back("Runtime subsystem test failed: expected runtime subsystem logs are missing.");
        return false;
    }
    if (state.LastRuntimeSystemSummary().find("AudioRuntimeSystem") == std::string::npos ||
        state.LastRuntimeSystemSummary().find("NetworkingRuntimeSystem") == std::string::npos) {
        diagnostics->push_back("Runtime subsystem test failed: runtime summary did not include new systems.");
        return false;
    }

    return true;
}

bool TestCameraRuntimeModesEffectsAndBounds(std::vector<std::string>* diagnostics) {
    std::vector<aine::Entity> entities;

    aine::Entity player;
    player.id = 1;
    player.name = "Player";
    player.position = {0.0f, 0.5f, 0.0f};
    player.components = {aine::MakeTransformComponent(), aine::MakeCharacterControllerRuntimeComponent()};
    entities.push_back(player);

    aine::Entity camera;
    camera.id = 2;
    camera.name = "Main Camera";
    camera.position = {0.0f, 4.0f, 8.0f};
    camera.rotation = {-25.0f, 0.0f, 0.0f};
    camera.components = {aine::MakeTransformComponent(), aine::MakeCameraComponent(),
                         aine::MakeCameraFollowRuntimeComponent("Player")};
    aine::Component* follow = FindComponent(camera, "CameraFollowRuntime");
    SetProperty(*follow, "mode", "Follow");
    SetProperty(*follow, "offset", "0, 2, 4");
    SetProperty(*follow, "smoothTime", "0");
    SetProperty(*follow, "rotationSmoothTime", "0");
    SetProperty(*follow, "lookAheadTime", "0");
    SetProperty(*follow, "boundsEnabled", "true");
    SetProperty(*follow, "boundsMin", "-2, 1, -6");
    SetProperty(*follow, "boundsMax", "2, 6, 6");
    entities.push_back(camera);

    aine::EngineRuntime runtime;
    if (!runtime.Begin(entities)) {
        diagnostics->push_back("Camera runtime test failed: runtime did not start.");
        return false;
    }

    runtime.Update(entities, 1.0f / 60.0f);
    aine::Entity* runtimeCamera = FindEntity(entities, "Main Camera");
    if (runtimeCamera == nullptr || !NearlyEqual(runtimeCamera->position[0], 0.0f) ||
        !NearlyEqual(runtimeCamera->position[1], 2.5f) || !NearlyEqual(runtimeCamera->position[2], 4.0f)) {
        diagnostics->push_back("Camera runtime test failed: Follow mode did not apply target offset.");
        return false;
    }

    aine::Entity* runtimePlayer = FindEntity(entities, "Player");
    runtimePlayer->position = {5.0f, 0.5f, 5.0f};
    runtime.SnapCamera("Main Camera");
    runtime.Update(entities, 1.0f / 60.0f);
    if (!NearlyEqual(runtimeCamera->position[0], 2.0f) || !NearlyEqual(runtimeCamera->position[2], 6.0f)) {
        diagnostics->push_back("Camera runtime test failed: camera bounds did not clamp the follow result.");
        return false;
    }

    follow = FindComponent(*runtimeCamera, "CameraFollowRuntime");
    SetProperty(*follow, "mode", "TopDown");
    SetProperty(*follow, "topDownHeight", "10");
    SetProperty(*follow, "isometricPitchDegrees", "-55");
    SetProperty(*follow, "isometricYawDegrees", "45");
    SetProperty(*follow, "boundsEnabled", "false");
    runtime.SnapCamera("Main Camera");
    runtime.Update(entities, 1.0f / 60.0f);
    if (!NearlyEqual(runtimeCamera->rotation[0], -55.0f) || !NearlyEqual(runtimeCamera->rotation[1], 45.0f)) {
        diagnostics->push_back("Camera runtime test failed: TopDown mode did not switch rotation.");
        return false;
    }

    const std::array<float, 3> beforeShake = runtimeCamera->position;
    runtime.RequestHitCameraShake("Main Camera");
    runtime.Update(entities, 1.0f / 60.0f);
    if (NearlyEqual(runtimeCamera->position[0], beforeShake[0]) &&
        NearlyEqual(runtimeCamera->position[1], beforeShake[1]) &&
        NearlyEqual(runtimeCamera->position[2], beforeShake[2])) {
        diagnostics->push_back("Camera runtime test failed: hit shake did not perturb the camera.");
        return false;
    }

    runtime.SetCameraControlPaused("Main Camera", true);
    runtime.Update(entities, 1.0f / 60.0f);
    const std::array<float, 3> pausedPosition = runtimeCamera->position;
    runtimePlayer->position = {-5.0f, 0.5f, -5.0f};
    runtime.Update(entities, 1.0f / 60.0f);
    if (!NearlyEqual(runtimeCamera->position[0], pausedPosition[0]) ||
        !NearlyEqual(runtimeCamera->position[1], pausedPosition[1]) ||
        !NearlyEqual(runtimeCamera->position[2], pausedPosition[2])) {
        diagnostics->push_back("Camera runtime test failed: paused camera control continued moving.");
        return false;
    }

    return true;
}

bool TestCameraCollisionAvoidanceUsesSphereCast(std::vector<std::string>* diagnostics) {
    std::vector<aine::Entity> entities;

    aine::Entity player;
    player.id = 1;
    player.name = "Player";
    player.position = {0.0f, 0.5f, 0.0f};
    player.components = {aine::MakeTransformComponent()};
    entities.push_back(player);

    aine::Entity camera;
    camera.id = 2;
    camera.name = "Main Camera";
    camera.position = {0.0f, 0.5f, 6.0f};
    camera.rotation = {0.0f, 0.0f, 0.0f};
    camera.components = {aine::MakeTransformComponent(), aine::MakeCameraComponent(),
                         aine::MakeCameraFollowRuntimeComponent("Player")};
    aine::Component* follow = FindComponent(camera, "CameraFollowRuntime");
    SetProperty(*follow, "mode", "Follow");
    SetProperty(*follow, "offset", "0, 0, 6");
    SetProperty(*follow, "smoothTime", "0");
    SetProperty(*follow, "rotationSmoothTime", "0");
    SetProperty(*follow, "lookAheadTime", "0");
    SetProperty(*follow, "deadZoneRadius", "0");
    SetProperty(*follow, "softZoneRadius", "0");
    SetProperty(*follow, "collisionAvoidance", "true");
    SetProperty(*follow, "collisionRadius", "0.45");
    SetProperty(*follow, "collisionMask", "Default");
    entities.push_back(camera);

    aine::Entity wall;
    wall.id = 3;
    wall.name = "Offset Wall";
    wall.position = {0.50f, 0.5f, 3.0f};
    wall.scale = {0.20f, 2.0f, 0.20f};
    wall.layer = "Default";
    wall.components = {aine::MakeTransformComponent(), aine::MakeCollider3DComponent("Box")};
    entities.push_back(wall);

    aine::PhysicsWorld physics;
    aine::PhysicsRayHit3D centerRayHit;
    if (physics.Raycast3D(entities, {0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, 6.0f, &centerRayHit)) {
        diagnostics->push_back("Camera collision test failed: setup error, center ray unexpectedly hit the wall.");
        return false;
    }
    aine::PhysicsRayHit3D sphereHit;
    if (!physics.SphereCast3D(entities, {0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, 6.0f, 0.45f,
                              &sphereHit) ||
        sphereHit.entityName != "Offset Wall" || sphereHit.distance >= 3.2f) {
        diagnostics->push_back("Camera collision test failed: sphere cast did not catch the offset obstruction.");
        return false;
    }

    aine::EngineRuntime runtime;
    if (!runtime.Begin(entities)) {
        diagnostics->push_back("Camera collision test failed: runtime did not start.");
        return false;
    }
    runtime.SnapCamera("Main Camera");
    runtime.Update(entities, 1.0f / 60.0f);
    aine::Entity* runtimeCamera = FindEntity(entities, "Main Camera");
    if (runtimeCamera == nullptr || runtimeCamera->position[2] >= 3.2f) {
        diagnostics->push_back("Camera collision test failed: camera did not stop before the offset obstruction.");
        return false;
    }

    return true;
}

bool TestCameraProjectionHelpersAndPicking(std::vector<std::string>* diagnostics) {
    std::vector<aine::Entity> entities;

    aine::Entity camera;
    camera.id = 1;
    camera.name = "Projection Camera";
    camera.position = {0.0f, 0.0f, 10.0f};
    camera.rotation = {0.0f, 0.0f, 0.0f};
    camera.components = {aine::MakeTransformComponent(), aine::MakeCameraComponent()};
    entities.push_back(camera);

    aine::Entity cube;
    cube.id = 2;
    cube.name = "Pick Cube";
    cube.position = {0.0f, 0.0f, 0.0f};
    aine::Component body = aine::MakeRigidbody3DComponent("Static");
    SetProperty(body, "useGravity", "false");
    cube.components = {aine::MakeTransformComponent(), body, aine::MakeCollider3DComponent("Box")};
    entities.push_back(cube);

    aine::EngineRuntime runtime;
    std::array<float, 3> screenDepth{};
    if (!runtime.WorldToScreen(entities, "Projection Camera", {0.0f, 0.0f, 0.0f}, {800.0f, 600.0f}, &screenDepth) ||
        !NearlyEqual(screenDepth[0], 400.0f) || !NearlyEqual(screenDepth[1], 300.0f) || screenDepth[2] <= 0.0f) {
        diagnostics->push_back("Camera projection test failed: world-to-screen center projection was incorrect.");
        return false;
    }

    std::array<float, 3> world{};
    if (!runtime.ScreenToWorld(entities, "Projection Camera", {400.0f, 300.0f}, screenDepth[2], {800.0f, 600.0f},
                               &world) ||
        !NearlyEqual(world[0], 0.0f) || !NearlyEqual(world[1], 0.0f) || !NearlyEqual(world[2], 0.0f)) {
        diagnostics->push_back("Camera projection test failed: screen-to-world did not round-trip center point.");
        return false;
    }

    aine::CameraRay ray;
    if (!runtime.ScreenPointToRay(entities, "Projection Camera", {400.0f, 300.0f}, {800.0f, 600.0f}, &ray) ||
        !NearlyEqual(ray.direction[0], 0.0f) || !NearlyEqual(ray.direction[1], 0.0f) ||
        !NearlyEqual(ray.direction[2], -1.0f)) {
        diagnostics->push_back("Camera projection test failed: center ray was incorrect.");
        return false;
    }

    aine::PhysicsRayHit3D hit;
    if (!runtime.RaycastFromCamera(entities, "Projection Camera", {400.0f, 300.0f}, {800.0f, 600.0f}, &hit) ||
        hit.entityName != "Pick Cube") {
        diagnostics->push_back("Camera projection test failed: camera raycast did not hit the cube.");
        return false;
    }

    aine::Component* cameraComponent = FindComponent(entities.front(), "Camera");
    SetProperty(*cameraComponent, "projection", "Orthographic");
    SetProperty(*cameraComponent, "orthographicSize", "5");
    if (!runtime.WorldToScreen(entities, "Projection Camera", {0.0f, 0.0f, 0.0f}, {800.0f, 600.0f}, &screenDepth) ||
        !NearlyEqual(screenDepth[0], 400.0f) || !NearlyEqual(screenDepth[1], 300.0f)) {
        diagnostics->push_back("Camera projection test failed: orthographic center projection was incorrect.");
        return false;
    }

    return true;
}

bool TestCameraSerializationPreservesPresets(std::vector<std::string>* diagnostics) {
    namespace fs = std::filesystem;

    const fs::path projectRoot = fs::current_path() / "build" / "runtime-camera-serialization-test";
    std::error_code cleanupError;
    fs::remove_all(projectRoot, cleanupError);

    aine::EditorState state;
    std::string error;
    if (!state.NewProject(projectRoot, &error)) {
        diagnostics->push_back("Camera serialization test failed: NewProject failed: " + error);
        return false;
    }
    state.ClearScene();

    aine::Entity& player = state.CreateEntity("Player", {aine::MakeTransformComponent()});
    aine::Entity& camera =
        state.CreateEntity("Main Camera", {aine::MakeTransformComponent(), aine::MakeCameraComponent(),
                                           aine::MakeCameraFollowRuntimeComponent("Player")});
    state.SetComponentPropertyOnEntity(camera.id, "Camera", "projection", "Orthographic");
    state.SetComponentPropertyOnEntity(camera.id, "Camera", "orthographicSize", "7.5");
    state.SetComponentPropertyOnEntity(camera.id, "Camera", "clearColor", "0.1, 0.2, 0.3, 1");
    state.SetComponentPropertyOnEntity(camera.id, "Camera", "viewportRect", "0.1, 0.2, 0.8");
    state.SetComponentPropertyOnEntity(camera.id, "Camera", "viewportHeight", "0.7");
    state.SetComponentPropertyOnEntity(camera.id, "Camera", "cullingMask", "Default;Gameplay");
    state.SetComponentPropertyOnEntity(camera.id, "CameraFollowRuntime", "mode", "Orbit");
    state.SetComponentPropertyOnEntity(camera.id, "CameraFollowRuntime", "targetGroupNames", "Player;Companion");
    state.SetComponentPropertyOnEntity(camera.id, "CameraFollowRuntime", "boundsEnabled", "true");
    state.SetComponentPropertyOnEntity(camera.id, "CameraFollowRuntime", "boundsMin", "-10, 0, -10");
    state.SetComponentPropertyOnEntity(camera.id, "CameraFollowRuntime", "boundsMax", "10, 8, 10");
    (void)player;

    if (!state.SaveProject(&error)) {
        diagnostics->push_back("Camera serialization test failed: SaveProject failed: " + error);
        return false;
    }

    aine::EditorState reopened;
    if (!reopened.OpenProject(projectRoot, &error)) {
        diagnostics->push_back("Camera serialization test failed: OpenProject failed: " + error);
        return false;
    }

    const aine::Entity* reopenedCamera = reopened.FindEntityByName("Main Camera");
    const aine::Component* reopenedCameraComponent = reopenedCamera == nullptr ? nullptr : FindComponent(*reopenedCamera, "Camera");
    const aine::Component* reopenedFollow =
        reopenedCamera == nullptr ? nullptr : FindComponent(*reopenedCamera, "CameraFollowRuntime");
    if (reopenedCameraComponent == nullptr || reopenedFollow == nullptr ||
        ComponentProperty(*reopenedCameraComponent, "projection") != "Orthographic" ||
        ComponentProperty(*reopenedCameraComponent, "cullingMask") != "Default;Gameplay" ||
        ComponentProperty(*reopenedFollow, "mode") != "Orbit" ||
        ComponentProperty(*reopenedFollow, "boundsEnabled") != "true") {
        diagnostics->push_back("Camera serialization test failed: camera settings did not survive reload.");
        return false;
    }

    aine::Entity& addCameraTarget = state.CreateEntity("Camera Add Target", {aine::MakeTransformComponent()});
    const int addCameraTargetId = addCameraTarget.id;
    if (!state.AddComponentToEntity(addCameraTargetId, aine::MakeCameraComponent())) {
        diagnostics->push_back("Camera serialization test failed: editor state did not add Camera.");
        return false;
    }
    const aine::Entity* cameraAddEntity = state.FindEntity(addCameraTargetId);
    if (cameraAddEntity == nullptr || FindComponent(*cameraAddEntity, "Camera") == nullptr ||
        FindComponent(*cameraAddEntity, "CameraFollowRuntime") == nullptr) {
        diagnostics->push_back("Camera serialization test failed: adding Camera did not attach Camera Controller.");
        return false;
    }
    if (state.RemoveComponentFromEntity(addCameraTargetId, "Camera")) {
        diagnostics->push_back("Camera serialization test failed: built-in Camera component was removable.");
        return false;
    }
    cameraAddEntity = state.FindEntity(addCameraTargetId);
    if (cameraAddEntity == nullptr || FindComponent(*cameraAddEntity, "Camera") == nullptr) {
        diagnostics->push_back("Camera serialization test failed: Camera component disappeared after rejected removal.");
        return false;
    }
    if (!state.RemoveComponentFromEntity(addCameraTargetId, "CameraFollowRuntime")) {
        diagnostics->push_back("Camera serialization test failed: optional Camera Controller was not removable.");
        return false;
    }

    return true;
}

bool TestFogComponentValidationAndSerialization(std::vector<std::string>* diagnostics) {
    namespace fs = std::filesystem;

    const aine::ComponentDefinition* definition = aine::FindComponentDefinition("Fog");
    aine::Entity defaultFogProbe;
    if (definition != nullptr) {
        defaultFogProbe.components.push_back(definition->defaultComponent);
    }
    if (definition == nullptr || definition->schema.category != "Rendering" ||
        !HasComponentType(defaultFogProbe, "Fog")) {
        diagnostics->push_back("Fog test failed: Fog component is missing from the registry/defaults.");
        return false;
    }

    const fs::path projectRoot = fs::current_path() / "build" / "runtime-fog-serialization-test";
    std::error_code cleanupError;
    fs::remove_all(projectRoot, cleanupError);

    aine::EditorState state;
    std::string error;
    if (!state.NewProject(projectRoot, &error)) {
        diagnostics->push_back("Fog test failed: NewProject failed: " + error);
        return false;
    }
    state.ClearScene();

    aine::Component fog = aine::MakeFogComponent();
    SetProperty(fog, "mode", "Exponential");
    SetProperty(fog, "color", "0.35, 0.48, 0.62, 1");
    SetProperty(fog, "density", "0.042");
    SetProperty(fog, "startDistance", "4");
    SetProperty(fog, "endDistance", "72");
    SetProperty(fog, "heightEnabled", "true");
    SetProperty(fog, "heightStart", "1.5");
    SetProperty(fog, "heightFalloff", "16");
    SetProperty(fog, "skyBlend", "0.7");
    state.CreateEntity("World Fog", {aine::MakeTransformComponent(), fog});
    state.CreateEntity("Main Camera", {aine::MakeTransformComponent(), aine::MakeCameraComponent()});

    if (!state.ValidateScene().ok) {
        diagnostics->push_back("Fog test failed: valid Fog scene did not validate.");
        return false;
    }

    if (!state.SaveProject(&error)) {
        diagnostics->push_back("Fog test failed: SaveProject failed: " + error);
        return false;
    }

    aine::EditorState reopened;
    if (!reopened.OpenProject(projectRoot, &error)) {
        diagnostics->push_back("Fog test failed: OpenProject failed: " + error);
        return false;
    }
    const aine::Entity* reopenedFog = reopened.FindEntityByName("World Fog");
    const aine::Component* reopenedFogComponent = reopenedFog == nullptr ? nullptr : FindComponent(*reopenedFog, "Fog");
    if (reopenedFogComponent == nullptr || ComponentProperty(*reopenedFogComponent, "mode") != "Exponential" ||
        ComponentProperty(*reopenedFogComponent, "heightEnabled") != "true" ||
        ComponentProperty(*reopenedFogComponent, "skyBlend") != "0.7") {
        diagnostics->push_back("Fog test failed: Fog settings did not survive save/reload.");
        return false;
    }

    auto invalidFogFails = [](aine::Component fogComponent, const std::string& propertyName,
                              const std::string& value) {
        SetProperty(fogComponent, propertyName, value);
        std::vector<aine::Entity> entities;
        aine::Entity fogEntity;
        fogEntity.id = 1;
        fogEntity.name = "Invalid Fog";
        fogEntity.components = {aine::MakeTransformComponent(), fogComponent};
        entities.push_back(fogEntity);
        return !aine::ValidateEngineScene(entities).ok;
    };

    if (!invalidFogFails(aine::MakeFogComponent(), "mode", "Mist") ||
        !invalidFogFails(aine::MakeFogComponent(), "density", "-0.1") ||
        !invalidFogFails(aine::MakeFogComponent(), "color", "2, 0.4, 0.5, 1") ||
        !invalidFogFails(aine::MakeFogComponent(), "skyBlend", "1.5")) {
        diagnostics->push_back("Fog test failed: invalid Fog properties were not rejected.");
        return false;
    }

    aine::Component badDistanceFog = aine::MakeFogComponent();
    SetProperty(badDistanceFog, "startDistance", "40");
    SetProperty(badDistanceFog, "endDistance", "12");
    if (!invalidFogFails(badDistanceFog, "mode", "Linear")) {
        diagnostics->push_back("Fog test failed: endDistance <= startDistance was not rejected.");
        return false;
    }

    return true;
}

bool TestScriptCompilerBuildsAttachedRuntimeScript(std::vector<std::string>* diagnostics) {
    namespace fs = std::filesystem;

    const fs::path projectRoot = fs::current_path() / "build" / "runtime-script-compiler-test";
    std::error_code cleanupError;
    fs::remove_all(projectRoot, cleanupError);

    aine::EditorState state;
    std::string error;
    if (!state.NewProject(projectRoot, &error)) {
        diagnostics->push_back("Script compiler test failed: NewProject failed: " + error);
        return false;
    }
    state.ClearScene();

    aine::Entity& entity = state.CreateEntity("Script Host", {aine::MakeTransformComponent()});
    const std::string scriptSource =
        "using AINative.Runtime;\n"
        "\n"
        "public class SmokeToolScript : ScriptBehaviour\n"
        "{\n"
        "    [ScriptField(\"Points\")]\n"
        "    public int points = 1;\n"
        "\n"
        "    public override void OnUpdate(float deltaSeconds)\n"
        "    {\n"
        "        _ = Entity.Id + points;\n"
        "    }\n"
        "}\n";
    if (!state.CreateScript("Assets/Scripts/Gameplay/SmokeToolScript.cs", scriptSource, nullptr, &error)) {
        diagnostics->push_back("Script compiler test failed: CreateScript failed: " + error);
        return false;
    }
    if (!state.AttachScriptToEntity(entity.id, "Assets/Scripts/Gameplay/SmokeToolScript.cs", "SmokeToolScript", &error)) {
        diagnostics->push_back("Script compiler test failed: AttachScriptToEntity failed: " + error);
        return false;
    }

    const aine::SceneValidationResult validation = state.ValidateScripts();
    if (!validation.ok) {
        diagnostics->push_back("Script compiler test failed: valid script did not compile.");
        for (const std::string& diagnostic : validation.diagnostics) {
            diagnostics->push_back(diagnostic);
        }
        return false;
    }

    if (!fs::exists(state.ScriptCompileManifestPathString()) ||
        !fs::exists(projectRoot / "Library" / "Scripts" / "bin" / "AINative.GameScripts.dll")) {
        diagnostics->push_back("Script compiler test failed: compiler manifest or assembly was not written.");
        return false;
    }

    return true;
}

bool TestScriptRuntimeHostInvokesLifecycleAndFields(std::vector<std::string>* diagnostics) {
    namespace fs = std::filesystem;

    const fs::path projectRoot = fs::current_path() / "build" / "runtime-script-host-test";
    std::error_code cleanupError;
    fs::remove_all(projectRoot, cleanupError);

    aine::EditorState state;
    std::string error;
    if (!state.NewProject(projectRoot, &error)) {
        diagnostics->push_back("Script runtime host test failed: NewProject failed: " + error);
        return false;
    }
    state.ClearScene();

    aine::Entity& entity = state.CreateEntity("Script Host", {aine::MakeTransformComponent()});
    const int scriptEntityId = entity.id;
    const std::string scriptSource =
        "using System;\n"
        "using AINative.Runtime;\n"
        "\n"
        "public class SmokeRuntimeScript : ScriptBehaviour\n"
        "{\n"
        "    [ScriptField(\"Points\")]\n"
        "    public int points = 1;\n"
        "\n"
        "    [ScriptField(\"Phase\")]\n"
        "    public string phase = \"new\";\n"
        "\n"
        "    public override void OnCreate()\n"
        "    {\n"
        "        points += 1;\n"
        "        phase = \"create\";\n"
        "    }\n"
        "\n"
        "    public override void OnStart()\n"
        "    {\n"
        "        points += 2;\n"
        "        phase += \"+start\";\n"
        "    }\n"
        "\n"
        "    public override void OnUpdate(float deltaSeconds)\n"
        "    {\n"
        "        if (Entity.Id <= 0) { throw new InvalidOperationException(\"missing entity binding\"); }\n"
        "        if (deltaSeconds > 0) { points += 3; }\n"
        "        phase += \"+update\";\n"
        "    }\n"
        "\n"
        "    public override void OnDestroy()\n"
        "    {\n"
        "        throw new InvalidOperationException(\"destroy smoke\");\n"
        "    }\n"
        "}\n";
    if (!state.CreateScript("Assets/Scripts/Gameplay/SmokeRuntimeScript.cs", scriptSource, nullptr, &error)) {
        diagnostics->push_back("Script runtime host test failed: CreateScript failed: " + error);
        return false;
    }
    if (!state.AttachScriptToEntity(scriptEntityId, "Assets/Scripts/Gameplay/SmokeRuntimeScript.cs", "SmokeRuntimeScript",
                                    &error)) {
        diagnostics->push_back("Script runtime host test failed: AttachScriptToEntity failed: " + error);
        return false;
    }
    if (!state.SetComponentPropertyOnEntity(scriptEntityId, "SmokeRuntimeScript", "points", "10")) {
        diagnostics->push_back("Script runtime host test failed: could not set exposed script field before play.");
        return false;
    }

    if (!state.BeginPlayMode()) {
        diagnostics->push_back("Script runtime host test failed: Play Mode did not start with a valid script host.");
        return false;
    }

    const aine::Entity* runtimeEntity = state.FindEntity(scriptEntityId);
    const aine::Component* runtimeScript =
        runtimeEntity == nullptr ? nullptr : FindComponent(*runtimeEntity, "SmokeRuntimeScript");
    if (runtimeScript == nullptr || ComponentProperty(*runtimeScript, "points") != "13" ||
        ComponentProperty(*runtimeScript, "phase") != "create+start") {
        diagnostics->push_back("Script runtime host test failed: OnCreate/OnStart did not update exposed fields.");
        return false;
    }
    if (!HasRuntimeEvent(state.RuntimeLogs(), "runtime.script.begin")) {
        diagnostics->push_back("Script runtime host test failed: runtime.script.begin event was not captured.");
        return false;
    }

    state.UpdatePlayMode(1.0f / 60.0f);
    runtimeEntity = state.FindEntity(scriptEntityId);
    runtimeScript = runtimeEntity == nullptr ? nullptr : FindComponent(*runtimeEntity, "SmokeRuntimeScript");
    if (runtimeScript == nullptr || ComponentProperty(*runtimeScript, "points") != "16" ||
        ComponentProperty(*runtimeScript, "phase") != "create+start+update") {
        diagnostics->push_back("Script runtime host test failed: OnUpdate did not update exposed fields.");
        return false;
    }
    if (!HasRuntimeEvent(state.RuntimeLogs(), "runtime.script.update")) {
        diagnostics->push_back("Script runtime host test failed: runtime.script.update event was not captured.");
        return false;
    }

    if (!state.StopPlayMode() || !state.IsEditMode()) {
        diagnostics->push_back("Script runtime host test failed: StopPlayMode failed after script OnDestroy.");
        return false;
    }
    if (!HasEditorLogMessage(state, "destroy smoke")) {
        diagnostics->push_back("Script runtime host test failed: OnDestroy exception was not surfaced to editor logs.");
        return false;
    }

    return true;
}

bool TestScriptRuntimeHostMissingAssemblyBlocksBegin(std::vector<std::string>* diagnostics) {
    std::vector<aine::Entity> entities;
    aine::Entity entity;
    entity.id = 7;
    entity.name = "Missing Assembly Host";
    entity.components.push_back({"MissingScript", {{"enabled", "true"}, {"scriptPath", "Assets/Scripts/MissingScript.cs"}}});
    entities.push_back(entity);

    aine::EngineRuntime runtime;
    aine::ScriptRuntimeHostConfig config;
    config.enabled = true;
    config.projectRoot = std::filesystem::current_path() / "build" / "missing-script-host-test";
    config.assemblyPath = config.projectRoot / "Library" / "Scripts" / "bin" / "AINative.GameScripts.dll";
    config.outputRoot = config.projectRoot / "Library" / "Scripts" / "RuntimeHost";

    bool sawError = false;
    aine::RuntimeHostCallbacks callbacks;
    callbacks.editorLog = [&sawError](aine::LogLevel level, std::string message) {
        sawError = sawError || (level == aine::LogLevel::Error &&
                                message.find("Script assembly is missing") != std::string::npos);
    };

    if (runtime.Begin(entities, callbacks, config)) {
        diagnostics->push_back("Script runtime host missing assembly test failed: runtime began without an assembly.");
        return false;
    }
    if (!sawError) {
        diagnostics->push_back("Script runtime host missing assembly test failed: host load failure was not logged.");
        return false;
    }
    return true;
}

bool TestScriptCompilerRejectsBrokenScriptAndBlocksPlay(std::vector<std::string>* diagnostics) {
    namespace fs = std::filesystem;

    const fs::path projectRoot = fs::current_path() / "build" / "runtime-script-compiler-broken-test";
    std::error_code cleanupError;
    fs::remove_all(projectRoot, cleanupError);

    aine::EditorState state;
    std::string error;
    if (!state.NewProject(projectRoot, &error)) {
        diagnostics->push_back("Broken script compiler test failed: NewProject failed: " + error);
        return false;
    }
    state.ClearScene();

    aine::Entity& entity = state.CreateEntity("Broken Script Host", {aine::MakeTransformComponent()});
    const std::string scriptSource =
        "public class BrokenScript\n"
        "{\n"
        "    public void OnUpdate(float deltaSeconds)\n"
        "    {\n"
        "        if (deltaSeconds > 0) {\n"
        "}\n";
    if (!state.CreateScript("Assets/Scripts/Gameplay/BrokenScript.cs", scriptSource, nullptr, &error)) {
        diagnostics->push_back("Broken script compiler test failed: CreateScript failed: " + error);
        return false;
    }
    if (!state.AttachScriptToEntity(entity.id, "Assets/Scripts/Gameplay/BrokenScript.cs", "BrokenScript", &error)) {
        diagnostics->push_back("Broken script compiler test failed: AttachScriptToEntity failed: " + error);
        return false;
    }

    const aine::SceneValidationResult validation = state.ValidateScripts();
    if (validation.ok) {
        diagnostics->push_back("Broken script compiler test failed: invalid script compiled successfully.");
        return false;
    }

    if (state.BeginPlayMode() || !state.IsEditMode()) {
        diagnostics->push_back("Broken script compiler test failed: Play Mode started despite script compile errors.");
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::vector<std::string> diagnostics;
    bool ok = aine::RunComponentRegistrySelfTests(&diagnostics);
    ok = TestCollectibleUsesPhysicsTriggerEvent(&diagnostics) && ok;
    ok = TestCharacterControllerBlockedByStaticCollider(&diagnostics) && ok;
    ok = TestColliderOnlyCharacterControllerBlockedByStaticCollider(&diagnostics) && ok;
    ok = TestEditorCreatedDynamicBodyFallsWithGravity(&diagnostics) && ok;
    ok = TestProjectSettingsTagsLayersAndCollisionMatrix(&diagnostics) && ok;
    ok = TestHierarchyParentingRules(&diagnostics) && ok;
    ok = TestProjectFolderAndSaveDataRoot(&diagnostics) && ok;
    ok = TestRuntimeUiSerializationValidationAndBindings(&diagnostics) && ok;
    ok = TestRuntimeSubsystemComponentsStepAndPersist(&diagnostics) && ok;
    ok = TestCameraRuntimeModesEffectsAndBounds(&diagnostics) && ok;
    ok = TestCameraCollisionAvoidanceUsesSphereCast(&diagnostics) && ok;
    ok = TestCameraProjectionHelpersAndPicking(&diagnostics) && ok;
    ok = TestCameraSerializationPreservesPresets(&diagnostics) && ok;
    ok = TestFogComponentValidationAndSerialization(&diagnostics) && ok;
    ok = TestScriptCompilerBuildsAttachedRuntimeScript(&diagnostics) && ok;
    ok = TestScriptRuntimeHostInvokesLifecycleAndFields(&diagnostics) && ok;
    ok = TestScriptRuntimeHostMissingAssemblyBlocksBegin(&diagnostics) && ok;
    ok = TestScriptCompilerRejectsBrokenScriptAndBlocksPlay(&diagnostics) && ok;
    if (!ok) {
        for (const std::string& diagnostic : diagnostics) {
            std::cerr << diagnostic << '\n';
        }
        return 1;
    }

    std::cout << "runtime.selftest passed." << '\n';
    return 0;
}

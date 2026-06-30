#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "ComponentRegistry.h"
#include "Physics.h"
#include "engine/core/Log.h"
#include "engine/runtime/Runtime.h"
#include "engine/scene/Entity.h"
#include "engine/scene/SceneValidation.h"
#include "engine/scripting/ScriptCompiler.h"

namespace aine {

enum class ChatSender {
    System,
    User,
    Assistant
};

struct ChatMessage {
    ChatSender sender = ChatSender::System;
    std::string message;
};

struct ChatConversation {
    std::string id;
    std::string title = "New chat";
    std::string projectName;
    bool pinned = false;
    std::vector<ChatMessage> messages;
};

struct AgentActivity {
    std::string label;
    std::string status;
    std::string detail;
};

struct ProjectTreeNode {
    std::string name;
    std::string path;
    bool folder = true;
    bool filesystemBacked = false;
    std::vector<ProjectTreeNode> children;
};

struct AssetRecord {
    std::string id;
    std::string name;
    std::string type;
    std::string sourceLabel;
    std::string sourcePath;
    std::string sourceHash;
    std::string importedPath;
    std::string cookedPath;
    std::string resourcePath;
    std::string metadataPath;
    std::string thumbnailPath;
    std::string license;
    std::string importSettings;
    std::vector<std::string> dependencies;
};

struct PluginRecord {
    std::string name;
    std::string path;
    std::string manifestPath;
    std::string installPath;
    bool enabled = false;
    bool installed = false;
};

struct MultiplayerSettings {
    bool enabled = false;
    std::string topology = "Single Player";
    std::string transport = "Local Loopback";
    std::string bindAddress = "127.0.0.1";
    int port = 7777;
    int maxPlayers = 4;
};

struct ProjectBuildSettings {
    std::string targetPlatform = "Windows";
    std::string architecture = "x64";
    std::string configuration = "Debug";
    std::string outputDirectory = "Builds/Windows";
    std::string executableName = "AINativeGame";
    bool developmentBuild = true;
    bool copyProjectFiles = true;
    bool runPlayerSmokeAfterBuild = false;
    bool generateInstallerManifest = true;
    bool generateUpdaterManifest = true;
    std::string releaseChannel = "internal";
    std::string signingMode = "Unsigned";
    std::vector<std::string> scenes;
    MultiplayerSettings multiplayer;
};

struct ProjectBuildReport {
    bool ok = false;
    std::string targetPlatform;
    std::filesystem::path outputDirectory;
    std::filesystem::path executablePath;
    std::filesystem::path launcherPath;
    std::filesystem::path manifestPath;
    std::filesystem::path packagedProjectFile;
    std::string runtimeSmokeStatus;
    int copiedFileCount = 0;
    std::vector<std::string> diagnostics;
};

struct LayerCollisionPair {
    std::string first;
    std::string second;
};

struct RenderProfileSettings {
    std::string id;
    std::string displayName;
    std::string dimensionMode = "mixed";
    std::string materialModel = "unlit-color";
    std::string lightingModel = "none";
    bool postProcessingEnabled = false;
    std::vector<std::string> postProcessingEffects;
    bool mesh3d = true;
    bool sprites = false;
    bool runtimeUi = true;
    bool editorGizmos = true;
    std::string resourceBackedModels = "placeholder";
    std::vector<std::string> requirements;
};

struct GraphicsBackendSettings {
    std::string preferred = "auto";
    std::vector<std::string> allowed{"opengl"};
    std::string fallback = "opengl";
};

struct ProjectRenderingSettings {
    int schemaVersion = 1;
    std::string activeProfile = "basic-built-in";
    GraphicsBackendSettings graphicsBackend;
    std::vector<RenderProfileSettings> profiles;
};

struct EditorProjectSettings {
    std::vector<std::string> tags;
    std::vector<std::string> layers;
    std::vector<LayerCollisionPair> disabledLayerCollisionPairs;
};

class EditorState {
public:
    EditorState();

    Entity& CreateEntity(std::string name, std::vector<Component> components = {});
    Entity& CreateEntityWithId(int id, std::string name, std::vector<Component> components = {});
    void ClearScene();
    void SeedStarterScene();

    Entity* FindEntity(int id);
    const Entity* FindEntity(int id) const;
    Entity* FindEntityByName(const std::string& name);
    const Entity* FindEntityByName(const std::string& name) const;

    bool RenameEntity(int id, const std::string& newName);
    bool CanSetEntityParent(int childId, int parentId) const;
    bool SetEntityParent(int childId, int parentId);
    bool IsEntityActiveInHierarchy(int id) const;
    bool DeleteEntity(int id);
    bool SetEntityTransform(int id, const std::array<float, 3>* position, const std::array<float, 3>* rotation,
                            const std::array<float, 3>* scale);
    bool AddComponentToEntity(int id, Component component);
    bool RemoveComponentFromEntity(int id, const std::string& componentType);
    bool SetComponentPropertyOnEntity(int id, const std::string& componentType, const std::string& propertyName,
                                      const std::string& value);
    SceneValidationResult ValidateScene() const;
    SceneValidationResult ValidateProject() const;
    SceneValidationResult ValidateAssets() const;
    SceneValidationResult ValidateScripts() const;

    bool PushUndoSnapshot(std::string label);
    bool UndoSceneEdit();
    bool RedoSceneEdit();
    bool CanUndoSceneEdit() const { return !undoStack_.empty(); }
    bool CanRedoSceneEdit() const { return !redoStack_.empty(); }
    bool IsSceneDirty() const { return sceneDirty_; }
    void MarkSceneDirty(std::string reason = {});

    void SelectEntity(int id);
    int SelectedEntityId() const { return selectedEntityId_; }
    EditorPlayState PlayState() const { return runtime_.PlayState(); }
    const char* PlayStateLabel() const;
    bool IsEditMode() const { return runtime_.PlayState() == EditorPlayState::EditMode; }
    bool IsPlaying() const { return runtime_.PlayState() == EditorPlayState::Playing; }
    bool IsPaused() const { return runtime_.PlayState() == EditorPlayState::Paused; }
    float PlayElapsedSeconds() const { return runtime_.ElapsedSeconds(); }
    bool BeginPlayMode();
    bool PausePlayMode();
    bool ResumePlayMode();
    bool StopPlayMode();
    void UpdatePlayMode(float deltaSeconds);
    bool StepPhysicsOnce();
    void SetCharacterControllerInput(CharacterControllerInput input);
    void SetRuntimeInputState(RuntimeInputState input);
    void SetPerformanceProfiler(PerformanceProfiler* profiler) { profiler_ = profiler; }
    const RuntimeInputState& RuntimeInput() const { return runtime_.Input(); }
    int PlayScore() const { return runtime_.Score(); }
    bool PlayGoalReached() const { return runtime_.GoalReached(); }
    bool PlayFailed() const { return runtime_.Failed(); }
    int ControlledRuntimeEntityId() const { return runtime_.ControlledEntityId(); }
    std::string ControlledRuntimeEntityName() const;
    const std::string& RuntimeStatus() const { return runtime_.Status(); }
    const std::string& LastRuntimeSystemSummary() const { return runtime_.LastSystemSummary(); }
    const std::vector<RuntimeLogEntry>& RuntimeLogs() const { return runtime_.Logs(); }
    const PhysicsWorld& Physics() const { return runtime_.Physics(); }
    const std::vector<PhysicsEvent>& PhysicsEvents() const { return runtime_.PhysicsEvents(); }

    void AddLog(LogLevel level, std::string message);
    void AddChat(ChatSender sender, std::string message);
    std::string CreateChatConversation(std::string title = {});
    bool SwitchChatConversation(const std::string& id);
    bool RenameChatConversation(const std::string& id, const std::string& title);
    bool SetChatConversationPinned(const std::string& id, bool pinned);
    bool DeleteChatConversation(const std::string& id);
    void LoadChatConversations(std::vector<ChatConversation> conversations, std::string activeId);
    void AddActivity(std::string label, std::string status, std::string detail);
    void ClearConsole();

    const std::vector<Entity>& Entities() const { return entities_; }
    const std::vector<LogEntry>& Logs() const { return logs_; }
    const std::vector<ChatMessage>& ChatHistory() const;
    const std::vector<ChatConversation>& ChatConversations() const { return chatConversations_; }
    const std::string& ActiveChatConversationId() const { return activeChatConversationId_; }
    const ChatConversation* ActiveChatConversation() const;
    const std::vector<AgentActivity>& AgentActivities() const { return agentActivities_; }
    const ProjectTreeNode& ProjectTree() const { return projectTree_; }

    const std::string& ProjectName() const { return projectName_; }
    const std::string& SceneName() const { return sceneName_; }
    const std::string& AgentStatus() const { return agentStatus_; }
    const std::string& LastTransactionId() const { return lastTransactionId_; }
    std::string ProjectRootPathString() const;
    std::string ProjectFilePathString() const;
    std::string SceneFilePathString() const;
    std::string AssetRootPathString() const;
    std::string SceneRootPathString() const;
    std::string GameSaveRootPathString() const;
    std::string AssetDatabasePathString() const;

    void SetAgentStatus(std::string status) { agentStatus_ = std::move(status); }
    void SetLastTransactionId(std::string transactionId) { lastTransactionId_ = std::move(transactionId); }

    std::filesystem::path DefaultProjectRootPath() const;
    bool NewProject(const std::filesystem::path& projectRoot, std::string* error = nullptr);
    bool OpenProject(const std::filesystem::path& projectFileOrRoot, std::string* error = nullptr);
    bool SaveProject(std::string* error = nullptr);
    bool SaveProjectAs(const std::filesystem::path& projectRoot, std::string* error = nullptr);
    bool CreateScene(const std::filesystem::path& scenePathOrName, std::string* error = nullptr);
    bool OpenScene(const std::filesystem::path& scenePath, std::string* error = nullptr);
    bool SaveScene(std::string* error = nullptr);
    bool SaveSceneAs(const std::filesystem::path& scenePath, std::string* error = nullptr);
    bool SetGameSaveRootPath(const std::filesystem::path& saveRoot, std::string* error = nullptr);
    void RefreshProjectTree();
    std::string GetProjectStateSummary() const;
    const ProjectBuildSettings& BuildSettings() const { return buildSettings_; }
    void SetBuildSettings(ProjectBuildSettings settings);
    std::filesystem::path BuildOutputDirectoryPath() const;
    std::string BuildOutputDirectoryPathString() const;
    bool PackagePlayerBuild(const std::filesystem::path& playerExecutable, ProjectBuildReport* report = nullptr,
                            std::string* error = nullptr);
    bool PackageWindowsBuild(const std::filesystem::path& playerExecutable, ProjectBuildReport* report = nullptr,
                             std::string* error = nullptr);

    const std::vector<AssetRecord>& Assets() const { return assetDatabase_; }
    const AssetRecord* FindAssetByIdOrName(const std::string& idOrName) const;
    bool ImportAsset(const std::filesystem::path& sourcePath, const std::string& sourceLabel,
                     const std::string& license, AssetRecord* importedAsset = nullptr, std::string* error = nullptr);
    bool CreateMaterialAsset(const std::string& materialName, const std::string& color,
                             AssetRecord* materialAsset = nullptr, std::string* error = nullptr);
    bool AssignMaterialToEntity(int id, const std::string& materialNameOrId, std::string* error = nullptr);

    bool CreateScript(const std::filesystem::path& scriptPath, const std::string& content,
                      std::filesystem::path* writtenPath = nullptr, std::string* error = nullptr);
    bool ModifyScript(const std::filesystem::path& scriptPath, const std::string& content,
                      std::filesystem::path* writtenPath = nullptr, std::string* error = nullptr);
    ScriptCompileResult CompileScripts() const;
    std::string ScriptCompileManifestPathString() const;
    std::vector<std::string> GetScriptDiagnostics(const std::filesystem::path& scriptPath = {}) const;
    bool AttachScriptToEntity(int id, const std::filesystem::path& scriptPath, const std::string& componentType,
                              std::string* error = nullptr);

    const std::vector<PluginRecord>& Plugins() const { return plugins_; }
    bool CreatePluginSkeleton(const std::string& pluginName, PluginRecord* plugin = nullptr, std::string* error = nullptr);
    bool CompilePlugin(const std::string& pluginName, std::filesystem::path* reportPath = nullptr,
                       std::string* error = nullptr);
    bool TestPlugin(const std::string& pluginName, std::filesystem::path* reportPath = nullptr,
                    std::string* error = nullptr);
    bool InstallPlugin(const std::string& pluginName, PluginRecord* plugin = nullptr, std::string* error = nullptr);
    bool SetPluginEnabled(const std::string& pluginName, bool enabled, std::string* error = nullptr);

    const EditorProjectSettings& ProjectSettings() const { return projectSettings_; }
    const ProjectRenderingSettings& RenderingSettings() const { return renderingSettings_; }
    const std::vector<std::string>& ProjectTags() const { return projectSettings_.tags; }
    const std::vector<std::string>& ProjectLayers() const { return projectSettings_.layers; }
    const RenderProfileSettings* ActiveRenderProfile() const;
    const RenderProfileSettings* FindRenderProfile(const std::string& id) const;
    std::vector<std::string> RenderProfileIds() const;
    bool SetActiveRenderProfile(const std::string& profileId, std::string* error = nullptr);
    bool AddProjectTag(const std::string& tag, std::string* error = nullptr);
    bool RenameProjectTag(const std::string& oldTag, const std::string& newTag, std::string* error = nullptr);
    bool RemoveProjectTag(const std::string& tag, std::string* error = nullptr);
    bool AddProjectLayer(const std::string& layer, std::string* error = nullptr);
    bool RenameProjectLayer(const std::string& oldLayer, const std::string& newLayer, std::string* error = nullptr);
    bool RemoveProjectLayer(const std::string& layer, std::string* error = nullptr);
    bool LayerCollisionEnabled(const std::string& firstLayer, const std::string& secondLayer) const;
    bool SetLayerCollisionEnabled(const std::string& firstLayer, const std::string& secondLayer, bool enabled,
                                  std::string* error = nullptr);
    void ResetProjectSettingsToDefaults();
    int EnsurePhysicsSettingsEntity();

private:
    struct SceneSnapshot {
        std::string label;
        std::string sceneName;
        std::filesystem::path sceneFilePath;
        int nextEntityId = 1;
        int selectedEntityId = 0;
        std::vector<Entity> entities;
    };

    ProjectTreeNode BuildProjectTree() const;
    void EnsureProjectFolders(std::string* error = nullptr) const;
    bool SaveSceneFile(const std::filesystem::path& scenePath, std::string* error = nullptr) const;
    bool LoadSceneFile(const std::filesystem::path& scenePath, std::string* error = nullptr);
    std::filesystem::path AssetDatabasePath() const;
    bool LoadAssetDatabase(std::string* error = nullptr);
    bool SaveAssetDatabase(std::string* error = nullptr) const;
    void NormalizeBuildSettings();
    SceneSnapshot CaptureSceneSnapshot(std::string label) const;
    void RestoreSceneSnapshot(const SceneSnapshot& snapshot);
    void ClearUndoRedo();
    void SyncTransformComponent(Entity& entity);
    void EnsurePhysicsSettingsForScene();
    void SyncPhysicsSettingsComponents();
    void EnsureSceneTagLayerValuesAreKnown();
    void SetDefaultPathsForRoot(const std::filesystem::path& projectRoot);
    RuntimeHostCallbacks RuntimeCallbacks();

    std::string projectName_ = "Untitled AI Native Project";
    std::string sceneName_ = "Prototype.scene.json";
    std::string agentStatus_ = "Idle";
    std::string lastTransactionId_ = "none";
    std::filesystem::path projectRootPath_;
    std::filesystem::path projectFilePath_;
    std::filesystem::path sceneFilePath_;
    std::filesystem::path assetRootPath_;
    std::filesystem::path sceneRootPath_;
    std::filesystem::path gameSaveRootPath_;
    int nextEntityId_ = 1;
    int selectedEntityId_ = 0;
    int playSnapshotNextEntityId_ = 1;
    int playSnapshotSelectedEntityId_ = 0;
    bool hasPlaySnapshot_ = false;
    EngineRuntime runtime_;
    PerformanceProfiler* profiler_ = nullptr;
    std::vector<Entity> entities_;
    std::vector<Entity> playSnapshotEntities_;
    std::vector<LogEntry> logs_;
    std::vector<ChatConversation> chatConversations_;
    std::string activeChatConversationId_;
    std::vector<AgentActivity> agentActivities_;
    ProjectTreeNode projectTree_;
    std::vector<SceneSnapshot> undoStack_;
    std::vector<SceneSnapshot> redoStack_;
    std::vector<AssetRecord> assetDatabase_;
    std::vector<PluginRecord> plugins_;
    ProjectBuildSettings buildSettings_;
    ProjectRenderingSettings renderingSettings_;
    EditorProjectSettings projectSettings_;
    std::uint64_t nextLogSequence_ = 1;
    int nextAssetId_ = 1;
    int nextChatConversationIndex_ = 1;
    bool sceneDirty_ = false;
};

} // namespace aine

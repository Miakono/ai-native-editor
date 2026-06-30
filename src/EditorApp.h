#pragma once

#include "AgentOrchestrator.h"
#include "EditorState.h"
#include "ToolGateway.h"
#include "engine/cave/CaveVolume.h"
#include "engine/terrain/Terrain.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct GLFWwindow;

namespace aine {

struct EditorAppOptions {
    bool smokeTest = false;
    int smokeTestFrames = 90;
    bool smokeTestSceneIntegration = false;
    bool smokeTestReload = false;
    std::string smokeTestPrompt;
    std::string smokeTestProviderHealth;
    std::string smokeTestProviderNormalizer;
    std::string projectRootOverride;
    std::string openProjectOverride;
    std::string appearanceSettingsOverride;
    bool smokeTestAppearance = false;
    bool smokeTestEditorProfile = false;
    bool smokeTestEditorLayouts = false;
    bool smokeTestPlayMode = false;
    bool smokeTestPhysics = false;
    bool smokeTestFirstGame = false;
    bool smokeTestRuntimeLogs = false;
    bool smokeTestRuntimeScripting = false;
    bool smokeTestRuntimeUi = false;
    bool smokeTestTaskComposer = false;
    bool smokeTestProjectFolder = false;
    bool smokeTestProjectTreeActions = false;
    bool smokeTestConsole = false;
    bool smokeTestPanelTabs = false;
    bool smokeTestAssetStaging = false;
    bool smokeTestAssetPipeline = false;
    bool smokeTestExpandedTools = false;
    bool smokeTestProviderFileChange = false;
    bool smokeTestAgentVisuals = false;
    bool smokeTestTerrain = false;
    bool smokeTestUnifiedTerrainProof = false;
    bool smokeTestTerrainPerformance = false;
    bool smokeTestCaves = false;
    bool smokeTestFog = false;
    bool smokeTestEnvironmentLighting = false;
    bool smokeTestSprites = false;
    bool smokeTestProjectBuilder = false;
    bool smokeTestProjectSettings = false;
    bool smokeTestRenderProfiles = false;
    bool smokeTestCameraEditor = false;
    bool smokeTestProfiler = false;
    std::string smokeTestTaskComposerRequest;
    std::string smokeTestAssetSource;
};

struct EditorAppearanceSettings {
    std::string presetName = "Dark Default";
    ImVec4 accentColor{0.30f, 0.44f, 0.58f, 1.0f};
    ImVec4 panelBackgroundColor{0.128f, 0.133f, 0.140f, 1.0f};
    ImVec4 sceneBackgroundColor{0.092f, 0.098f, 0.106f, 1.0f};
    ImVec4 viewportGridColor{0.185f, 0.196f, 0.210f, 1.0f};
    ImVec4 selectedObjectHighlightColor{0.70f, 0.54f, 0.28f, 1.0f};
    float textBrightness = 1.0f;
    float textContrast = 1.0f;
};

class EditorApp {
public:
    explicit EditorApp(EditorAppOptions options);
    ~EditorApp();

    bool Initialize();
    int Run();
    void Shutdown();

private:
    enum class EditorPanelKind {
        Scene,
        Game,
        Hierarchy,
        Inspector,
        Console,
        Agent,
        Terrain,
        Lighting,
        ProjectSource,
        Profiler
    };

    enum class PendingPanelTabActionType {
        None,
        Duplicate,
        Close,
        CloseOtherSameKind,
        CloseAllSameKind,
        ResetDefaultTabs
    };

    struct EditorPanelInstance {
        EditorPanelKind kind = EditorPanelKind::Scene;
        int id = 0;
        std::string label;
        bool open = true;
        bool focusNextFrame = false;
        unsigned int dockIdHint = 0;
        unsigned int lastDockId = 0;
    };

    struct PendingPanelTabAction {
        PendingPanelTabActionType type = PendingPanelTabActionType::None;
        int sourceTabId = 0;
        EditorPanelKind kind = EditorPanelKind::Scene;
        unsigned int dockIdHint = 0;
    };

    struct EditorLayoutSnapshot {
        std::string id;
        std::string name;
        bool builtin = false;
        nlohmann::json panelTabs = nlohmann::json::array();
        nlohmann::json sceneView = nlohmann::json::object();
        std::string imguiIni;
    };

    struct AgentVisualCapture {
        std::string view;
        std::string path;
        std::string timestampUtc;
        std::string reason;
        int width = 0;
        int height = 0;
        int frameIndex = 0;
        bool recordingFrame = false;
    };

    void DrawFrame();
    void HandleGlobalShortcuts();
    void DrawDockspace();
    void BuildDefaultDockLayout(unsigned int dockspaceId);
    void BuildEditorLayoutDockTree(const std::string& presetId, unsigned int dockspaceId);
    void DrawMainMenuBar();
    void DrawEditorLayoutMenuItems();
    void DrawEditorLayoutPopups();
    void ConfigureDpiAndFonts();
    void ApplyEditorTheme();
    bool BeginPanel(const char* title, ImVec2 minSize, ImGuiWindowFlags flags = 0) const;
    bool BeginPanel(EditorPanelInstance& panel, ImVec2 minSize, ImGuiWindowFlags flags = 0);
    float Scaled(float value) const;
    ImVec2 Scaled(ImVec2 value) const;

    void DrawWorkspacePanel(EditorPanelInstance& panel);
    void DrawViewportPanel(EditorPanelInstance& panel);
    void DrawGameViewportPanel(EditorPanelInstance& panel);
    void DrawHierarchyPanel(EditorPanelInstance& panel);
    void DrawInspectorPanel(EditorPanelInstance& panel);
    void DrawConsolePanel(EditorPanelInstance& panel);
    void DrawAIChatPanel(EditorPanelInstance& panel);
    void DrawProfilerPanel(EditorPanelInstance& panel);
    void DrawAgentConversationSidebar(bool compact);
    void SelectAgentConversation(const std::string& id);
    void StartNewAgentConversation();
    void DrawAgentWorkflow();
    void DrawAgentControlStrip();
    bool DrawAgentCurrentRunCard();
    void DrawAgentConversationAndRuns();
    void DrawAgentPromptComposer();
    void DrawAgentRunCards();
    void DrawAgentRunCard(size_t index);
    void DrawAgentAdvancedDetails();
    void DrawProviderHealthDetails();
    void DrawToolGatewayDetails();
    void DrawRawAgentLogs();
    void DrawAgentChatTab();
    void DrawAgentTaskComposerContents(bool includeHeader);
    void DrawAgentTaskComposerPanel();
    void DrawAgentRunsTab();
    void DrawAgentHistoryPanel();
    void DrawAgentActivityPanel();
    void DrawAgentVisualControls();
    void DrawTerrainPanel(EditorPanelInstance& panel);
    void DrawLightingPanel(EditorPanelInstance& panel);
    void DrawSceneToolStrip();
    void DrawTerrainCreateSection();
    void DrawTerrainInspectorBody(Component* terrainComponent);
    void DrawTerrainSculptSection(Component* terrainComponent);
    void DrawTerrainVolumeSection(Component* terrainComponent);
    void DrawTerrainPaintSection(Component* terrainComponent);
    void DrawTerrainSettingsSection(Component* terrainComponent);
    void DrawCaveVolumeInspectorBody(Component* caveComponent);
    void DrawCaveBrushSection(Component* caveComponent);
    void DrawCaveLayerSection(Component* caveComponent);
    void DrawCaveSettingsSection(Component* caveComponent);
    void DrawProjectTreePanel(EditorPanelInstance& panel);
    void DrawProjectExplorerToolbar(const std::filesystem::path& selectedPath);
    void DrawProjectExplorerBreadcrumb(const std::filesystem::path& selectedPath);
    void DrawProjectExplorerSidebar(const std::filesystem::path& selectedPath);
    void DrawProjectExplorerSidebarNode(const ProjectTreeNode& node, const std::filesystem::path& selectedPath);
    void DrawProjectExplorerFavorite(const char* label, const std::filesystem::path& path,
                                     const std::filesystem::path& selectedPath);
    void DrawProjectExplorerContents(const std::filesystem::path& selectedPath);
    void DrawProjectExplorerEntry(const std::filesystem::directory_entry& entry, int index);
    void DrawProjectExplorerIcon(const std::filesystem::directory_entry& entry, ImVec2 iconMin,
                                 float iconSize, bool highlighted) const;
    void DrawProjectLocationsDialog();
    void DrawBuildSettingsPanel();
    void DrawProjectSettingsPanel();
    void DrawProjectRenderingSettings();
    void DrawProjectTagSettings();
    void DrawProjectLayerSettings();
    void DrawProjectPhysicsSettings();
    void DrawToolGatewayPanel();
    void DrawAppearancePreferencesPanel();
    void DrawProposedActionsSection(bool includeHeader = true);
    void DrawCodexSetupSection();
    void DrawEditorTopToolbar();
    void DrawToolbarPlayModeControls(const char* idPrefix);
    void DrawPlayModeControls(const char* idPrefix, bool showStatus);

    void DrawViewportToolbar();
    void HandleViewportInput(ImVec2 viewportPos, ImVec2 viewportSize);
    bool HandleTerrainViewportInput(ImVec2 viewportPos, ImVec2 viewportSize);
    bool HandleCaveViewportInput(ImVec2 viewportPos, ImVec2 viewportSize);
    bool ApplyCaveTerrainCoupling(Entity& caveEntity, CaveVolumeData* caveData, std::array<float, 3> brushWorld,
                                  const CaveBrushSettings& settings);
    void RenderViewportScene(int width, int height);
    void RenderGameViewportScene(int width, int height);
    bool DrawGameRuntimeUiOverlay(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize, bool handleClicks);
    bool ExecuteRuntimeUIButtonAction(const std::string& action);
    bool EnsureViewportFramebuffer(int width, int height);
    bool EnsureGameFramebuffer(int width, int height);
    void DestroyViewportFramebuffer();
    void DestroyGameFramebuffer();
    void FocusSelectedEntity();
    void ResetViewportCamera();
    void ApplyViewportLookDelta(float yawDeltaDegrees, float pitchDeltaDegrees, bool orbitAroundTarget);
    void MoveViewportCameraBy(const std::array<float, 3>& delta);
    int PickViewportEntity(ImVec2 localMouse, ImVec2 viewportSize) const;
    int PickViewportTerrain(ImVec2 localMouse, ImVec2 viewportSize, TerrainRayHit* outHit = nullptr,
                            bool includeHoles = false) const;
    int PickViewportCave(ImVec2 localMouse, ImVec2 viewportSize, CaveRayHit* outHit = nullptr) const;
    bool RenameSelectedEntityFromInspector(const std::string& requestedName);
    void DeleteSelectedEntity(const char* source);
    int PickMoveGizmoAxis(ImVec2 localMouse, ImVec2 viewportSize) const;
    float ProjectMouseToMoveAxis(int axis, ImVec2 localMouse, ImVec2 viewportSize) const;
    void BeginMoveGizmoDrag(int axis, ImVec2 localMouse, ImVec2 viewportSize);
    void UpdateMoveGizmoDrag(ImVec2 localMouse, ImVec2 viewportSize);
    void EndMoveGizmoDrag();
    void StartOrResumePlayMode();
    void PausePlayMode();
    void StopPlayMode();
    void CompileScriptsFromUi();
    void FocusGameViewportForPlayMode();
    bool IsGameViewportInputActiveForTesting() const { return gameViewportInputActive_; }
    void AddSpinRuntimeToSelected(const char* source);
    void AddCharacterControllerRuntimeToSelected(const char* source);
    void CreateCameraObject(const char* source);
    void Create2DCameraObject(const char* source);
    void CreateSpriteTemplate(const char* source);
    void CreatePlayerControllerSquare(const char* source);
    void CreateCollectibleTemplate(const char* source);
    void CreateGoalTemplate(const char* source);
    void CreateHazardTemplate(const char* source);
    void CreateUICanvasTemplate(const char* source);
    void CreateUIPanelTemplate(const char* source);
    void CreateUITextTemplate(const char* source);
    void CreateUIButtonTemplate(const char* source);
    void CreateHUDLabelTemplate(const char* source);
    void CreateDeathScreenTemplate(const char* source);
    void CreateTerrainTemplate(const char* source);
    void CreateCaveVolumeTemplate(const char* source);
    void CreateFogEnvironmentTemplate(const char* source);
    void CreateEnvironmentLightingTemplate(const char* source);
    RuntimeInputState BuildRuntimeInputState() const;
    void SyncWindowTitle();
    bool EnsureEditModeForAction(const char* actionName);

    void DrawProjectTreeNode(const ProjectTreeNode& node);
    void DrawProjectTreeNodeContextMenu(const ProjectTreeNode& node);
    void DrawProjectTreeDeleteConfirmation();
    bool DeleteProjectTreePath(const std::filesystem::path& path, bool folder, std::string* error = nullptr);
    void CreateFolderInProjectTree(const std::filesystem::path& parentPath);
    std::filesystem::path EnsureProjectExplorerSelection();
    void SelectProjectExplorerPath(const std::filesystem::path& path);
    std::filesystem::path ProjectExplorerVisibleRoot() const;
    bool ProjectExplorerSearchMatches(const std::filesystem::directory_entry& entry) const;
    ProjectTreeNode ProjectExplorerNodeForEntry(const std::filesystem::directory_entry& entry) const;
    bool SendChatPrompt();
    bool SubmitAgentPrompt(std::string prompt, AgentBackend backend, bool fromComposer);
    std::string ActiveAgentPromptForSend() const;
    void ImproveAgentPromptDraft();
    void CreateImplementationBriefFromAgentPrompt();
    void ImproveComposerPrompt();
    void SplitComposerPromptIntoAgents();
    void RunComposerPromptWithCodex();
    bool SaveComposerPromptAsAgentRun(const std::string& eventType);
    std::string BuildAgentImplementationBrief(const std::string& userRequest) const;
    std::string BuildSplitAgentBrief(const std::string& userRequest) const;
    std::filesystem::path AgentMemoryDirectoryPath() const;
    std::filesystem::path AgentRunLogPath() const;
    std::filesystem::path AgentVisualsDirectoryPath() const;
    std::string GenerateAgentRunId() const;
    std::string AgentVisualCaptureSummary(const std::vector<AgentVisualCapture>& captures) const;
    bool CaptureAgentVisualSnapshot(const std::string& reason, const std::string& runId,
                                    std::vector<AgentVisualCapture>* captures = nullptr,
                                    int recordingFrameIndex = 0);
    bool CaptureFramebufferPng(unsigned int framebuffer, int width, int height,
                               const std::filesystem::path& outputPath, std::string* error = nullptr);
    void StartAgentVisualRecording();
    void StopAgentVisualRecording();
    void CaptureAgentRecordingFrameIfNeeded();
    bool ApplyEditorOwnedAgentVisualCommands(const ToolCommandBatch& batch, const std::string& runId);
    bool AppendAgentRunLogEntry(const std::string& eventType, const std::string& runId,
                                const std::string& userRequest, const std::string& prompt,
                                const std::string& summary, const AgentOrchestratorResult* result = nullptr);
    void RecordAgentRunSummary(const std::string& runId, const std::string& userRequest,
                               const std::string& prompt, const AgentOrchestratorResult& result);
    void PollAgentJob();
    void RequestProviderHealthRefresh(const std::string& reason);
    void PollProviderHealthJob();
    AgentOrchestratorResult ApplyAgentResultToUi(AgentOrchestratorResult result, const std::string& prompt,
                                                 bool rerunGatewayFallbackOnMainThread);
    void AddAgentRunCard(const std::string& runId, const std::string& prompt, AgentBackend backend,
                         const std::string& logPath);
    void MarkAgentRunRunning(const std::string& runId, const std::string& stage, const std::string& summary);
    void CompleteAgentRunCard(const std::string& runId, AgentBackend requestedBackend,
                              const AgentOrchestratorResult& result);
    void ResolveEditorProfilePath();
    void LoadEditorProfile();
    bool SaveEditorProfile(std::string* error = nullptr);
    void ApplyLoadedWindowPlacement();
    void ApplyAppearancePreset(const char* presetName);
    bool RunAppearanceSmokeTest();
    bool RunEditorProfileSmokeTest();
    bool RunEditorLayoutSmokeTest();
    bool RunSceneIntegrationSmokeTest();
    bool RunPlayModeSmokeTest();
    bool RunPhysicsSmokeTest();
    bool RunFirstGameSmokeTest();
    bool RunRuntimeLogSmokeTest();
    bool RunRuntimeScriptingSmokeTest();
    bool RunRuntimeUiSmokeTest();
    bool RunProviderHealthSmokeTest();
    bool RunProviderNormalizerSmokeTest();
    bool RunAgentTaskComposerSmokeTest();
    bool RunAgentVisualsSmokeTest();
    bool RunProjectFolderSmokeTest();
    bool RunProjectTreeActionsSmokeTest();
    bool RunConsoleSmokeTest();
    bool RunAssetStagingSmokeTest();
    bool RunAssetPipelineSmokeTest();
    bool RunExpandedToolApiSmokeTest();
    bool RunProviderFileChangeSmokeTest();
    bool RunPanelTabSmokeTest();
    bool RunTerrainSmokeTest();
    bool RunUnifiedTerrainProofSmokeTest();
    bool RunTerrainPerformanceSmokeTest();
    bool RunCaveSmokeTest();
    bool RunFogSmokeTest();
    bool RunEnvironmentLightingSmokeTest();
    bool RunSpriteSmokeTest();
    bool RunProjectBuilderSmokeTest();
    bool RunProjectSettingsSmokeTest();
    bool RunRenderProfileSmokeTest();
    bool RunCameraEditorSmokeTest();
    bool RunProfilerSmokeTest();
    void ResetDefaultPanelTabs();
    void DrawPanelTabContextMenu(EditorPanelInstance& panel);
    void QueuePanelTabAction(PendingPanelTabActionType type, const EditorPanelInstance& panel);
    void ApplyPendingPanelTabAction();
    void RemoveClosedPanelTabs();
    EditorPanelInstance* FindPanelTab(int tabId);
    const EditorPanelInstance* FindPanelTab(int tabId) const;
    EditorPanelInstance& CreatePanelTab(EditorPanelKind kind, std::string label, unsigned int dockIdHint,
                                        bool focusNextFrame);
    int DuplicatePanelTab(int sourceTabId, unsigned int dockIdHint);
    void FocusOrOpenPanelKind(EditorPanelKind kind);
    int PanelTabCount(EditorPanelKind kind) const;
    std::string NextPanelLabel(EditorPanelKind kind) const;
    std::string PanelWindowTitle(const EditorPanelInstance& panel) const;
    std::string PanelBaseTitle(EditorPanelKind kind) const;
    std::string PanelKindId(EditorPanelKind kind) const;
    bool TryParsePanelKindId(const std::string& id, EditorPanelKind* outKind) const;
    int DefaultPanelTabId(EditorPanelKind kind) const;
    bool IsDefaultPanelTab(const EditorPanelInstance& panel) const;
    nlohmann::json PanelTabsToJson() const;
    void LoadPanelTabsFromJson(const nlohmann::json& value);
    void PersistPanelTabsIfDirty();
    nlohmann::json PanelTabsForKinds(const std::vector<EditorPanelKind>& kinds) const;
    nlohmann::json EditorViewToJson() const;
    void ApplyEditorViewFromJson(const nlohmann::json& value);
    EditorLayoutSnapshot CaptureCurrentEditorLayout(std::string id, std::string name, bool builtin = false) const;
    nlohmann::json EditorLayoutToJson(const EditorLayoutSnapshot& layout) const;
    bool LoadEditorLayoutFromJson(const nlohmann::json& value, EditorLayoutSnapshot* outLayout) const;
    nlohmann::json SavedEditorLayoutsToJson() const;
    void LoadSavedEditorLayoutsFromJson(const nlohmann::json& value);
    std::vector<EditorLayoutSnapshot> BuiltInEditorLayoutPresets() const;
    bool TryGetBuiltInEditorLayoutPreset(const std::string& id, EditorLayoutSnapshot* outLayout) const;
    EditorLayoutSnapshot BuildBuiltInEditorLayoutPreset(const std::string& id, const std::string& name) const;
    bool IsCustomEditorLayoutId(const std::string& id) const;
    EditorLayoutSnapshot* FindCustomEditorLayout(const std::string& id);
    const EditorLayoutSnapshot* FindCustomEditorLayout(const std::string& id) const;
    std::string MakeUniqueEditorLayoutId(const std::string& name) const;
    std::string SuggestedEditorLayoutName() const;
    std::string ActiveEditorLayoutDisplayName() const;
    bool IsActiveEditorLayoutDirty() const;
    bool EditorLayoutMatchesSnapshot(const EditorLayoutSnapshot& layout) const;
    bool SaveCurrentEditorLayout(const std::string& requestedName, bool saveAsNew, std::string* error = nullptr);
    bool ApplyEditorLayoutById(const std::string& id, std::string* error = nullptr);
    bool ApplyEditorLayoutSnapshot(const EditorLayoutSnapshot& layout, std::string* error = nullptr);
    bool RenameActiveCustomEditorLayout(const std::string& requestedName, std::string* error = nullptr);
    bool DeleteCustomEditorLayout(const std::string& id, std::string* error = nullptr);
    std::string CaptureCurrentImGuiIni() const;
    void StoreProviderResult(const AgentOrchestratorResult& result);
    void AddProviderOutcomeSystemMessage(const AgentOrchestratorResult& result);
    void ApproveProposedAction(size_t index);
    bool ApplyProposedFileChange(size_t index);
    bool RevertProposedFileChange(size_t index);
    std::string BuildFileChangePreview(const ProposedFileChange& fileChange) const;
    void RejectProposedAction(size_t index);
    void OpenCodexLoginFromUi();
    void UseOpenAIApiInstead();
    void ApplyCodexPathFromUi();
    void RefreshPathInputs();
    void NewProjectFromUi();
    void OpenProjectFromUi();
    void SaveProjectFromUi();
    void SaveProjectAsFromUi();
    void SaveSceneAsFromUi();
    void SaveSceneAsWithDialogFromUi();
    void RefreshBuildSettingsInputs();
    void ApplyBuildSettingsFromUi();
    bool PackageProjectFromUi();
    bool PackageAllTargetsFromUi();
    std::filesystem::path PlayerExecutablePathForCurrentBuild() const;
    bool BrowseBuildOutputFolderFromUi();
    bool BrowseProjectFolderFromUi();
    bool BrowseGameSaveFolderFromUi();
    void ApplyGameSaveFolderFromUi();
    bool StageExternalAssetFolderFromUi();
    bool StageExternalAssetFolder(const std::filesystem::path& sourceFolder,
                                  const std::string& tempProjectName,
                                  std::filesystem::path* stagedProjectFile,
                                  std::string* summary,
                                  std::string* error);
    void ProcessConsoleLogOptions();
    void PersistProjectSettingsFromUi(const std::string& action);
    void RecordProfilerFrameCounters();
    int CountSceneComponentsForProfiler() const;
    int CountRuntimeUiElementsForProfiler() const;
    std::filesystem::path ProfilerDefaultExportPath() const;
    bool ExportProfilerReport(const std::filesystem::path& outputPath, std::string* error = nullptr);

    struct AsyncAgentResult {
        AgentOrchestratorResult result;
        std::vector<AgentActivity> activities;
    };

    struct AsyncAgentJob {
        std::mutex mutex;
        bool complete = false;
        std::vector<AgentActivity> pendingActivities;
        AsyncAgentResult result;
    };

    struct AsyncProviderHealthJob {
        std::mutex mutex;
        bool complete = false;
        AgentOrchestrator orchestrator;
        std::string reason;
    };

    struct AgentRunUiItem {
        std::string runId;
        std::string conversationId;
        std::string status = "Pending";
        AgentBackend backend = AgentBackend::LocalToolGateway;
        std::string backendLabel;
        std::string stage = "Queued";
        std::string summary = "No response received yet.";
        std::string prompt;
        std::string resultText = "No response received yet.";
        std::string outputSummary;
        std::string commandSummary;
        std::string changedSummary;
        std::string failureReason;
        std::string logPath;
        double startedSeconds = 0.0;
        double finishedSeconds = 0.0;
        int exitCode = 0;
        bool usedFallback = false;
    };

    EditorAppOptions options_;
    GLFWwindow* window_ = nullptr;
    EditorState state_;
    ToolGateway gateway_;
    AgentOrchestrator orchestrator_;
    PerformanceProfiler profiler_;
    AgentBackend selectedBackend_ = AgentBackend::CodexCli;
    ToolGatewayResult lastGatewayResult_;
    std::vector<NormalizedProviderResult> proposedActions_;
    EditorAppearanceSettings appearance_;
    std::filesystem::path editorProfilePath_;
    std::filesystem::path legacyAppearanceSettingsPath_;
    std::string imguiIniFilename_;
    std::string profileLastProjectFile_;
    std::filesystem::path pendingProjectTreeDeletePath_;
    std::filesystem::path projectExplorerSelectedPath_;
    std::string pendingProjectTreeDeleteName_;
    std::array<char, 8192> chatInput_{};
    std::array<char, 256> chatSearchInput_{};
    std::array<char, 2048> composerRequestInput_{};
    std::array<char, 32768> composerPromptInput_{};
    std::array<char, 128> inspectorNameInput_{};
    std::array<char, 128> addComponentSearchInput_{};
    std::array<char, 1024> codexPathInput_{};
    std::array<char, 1024> projectRootInput_{};
    std::array<char, 1024> projectFileInput_{};
    std::array<char, 1024> scenePathInput_{};
    std::array<char, 1024> gameSavePathInput_{};
    std::array<char, 1024> buildOutputDirectoryInput_{};
    std::array<char, 256> buildExecutableNameInput_{};
    std::array<char, 128> multiplayerBindAddressInput_{};
    std::array<char, 128> projectSettingsNewTagInput_{};
    std::array<char, 128> projectSettingsRenameTagInput_{};
    std::array<char, 128> projectSettingsNewLayerInput_{};
    std::array<char, 128> projectSettingsRenameLayerInput_{};
    std::array<char, 256> projectExplorerSearchInput_{};
    std::array<char, 256> consoleSearchInput_{};
    std::array<char, 128> editorLayoutNameInput_{};
    std::array<char, 128> editorLayoutRenameInput_{};
    float projectExplorerIconSize_ = 48.0f;
    bool showCodexPathInput_ = false;
    bool hasLastGatewayResult_ = false;
    bool dockLayoutBuilt_ = false;
    bool showImGuiDemo_ = false;
    bool showAppearancePreferences_ = false;
    bool showAgentHistory_ = false;
    bool showBuildSettings_ = false;
    bool showProjectSettings_ = false;
    bool agentAutoApplyCodexCommandBatches_ = true;
    bool agentRouteSceneEditsLocally_ = false;
    bool agentAutoGenerateImplementationBriefs_ = true;
    bool agentAutoCaptureVisuals_ = true;
    bool agentVisualRecording_ = false;
    bool showProjectLocationsDialog_ = false;
    bool focusDefaultActivityPanel_ = true;
    bool panelTabsDirty_ = false;
    bool focusGameViewportNextFrame_ = false;
    bool gameViewportInputActive_ = false;
    bool pendingProjectTreeDeleteIsFolder_ = false;
    bool openProjectTreeDeleteConfirmation_ = false;
    bool consoleShowInfo_ = true;
    bool consoleShowWarnings_ = true;
    bool consoleShowErrors_ = true;
    bool consoleCollapse_ = false;
    bool consoleErrorPause_ = false;
    bool consolePaused_ = false;
    bool consoleClearOnPlay_ = false;
    bool consoleWasPlaying_ = false;
    bool consolePausedSnapshotValid_ = false;
    std::uint64_t consoleLastObservedLogSequence_ = 0;
    std::vector<LogEntry> consolePausedLogs_;
    std::vector<EditorPanelInstance> panelTabs_;
    std::vector<EditorLayoutSnapshot> savedEditorLayouts_;
    PendingPanelTabAction pendingPanelTabAction_;
    std::string activeEditorLayoutId_;
    std::string pendingDockLayoutPresetId_;
    std::string pendingDeleteEditorLayoutId_;
    int nextPanelTabId_ = 1;
    std::vector<AgentRunUiItem> agentRunCards_;
    std::vector<AgentVisualCapture> agentVisualCaptures_;
    std::string activeAgentRunId_;
    std::string agentSurfaceStatus_;
    std::shared_ptr<AsyncAgentJob> agentJob_;
    std::shared_ptr<AsyncProviderHealthJob> providerHealthJob_;
    AgentBackend agentJobBackend_ = AgentBackend::LocalToolGateway;
    std::string agentJobPrompt_;
    bool agentJobPromptFromComposer_ = false;
    std::string agentJobRunLogId_;
    std::string agentJobComposerRequest_;
    double agentJobStartedSeconds_ = 0.0;
    std::string composerRunId_;
    std::string composerStatus_;
    std::string lastComposerLogPath_;
    std::string agentVisualRecordingId_;
    std::string agentVisualRecordingFolder_;
    std::string lastBuildStatus_;
    std::string lastBuildOutputPath_;
    std::string projectSettingsStatus_;
    std::string inspectorNameStatus_;
    std::string inspectorNameCommittedValue_;
    std::string profilerStatus_;
    std::string profilerLastExportPath_;
    int composerSavedPromptCount_ = 0;
    int composerCompletedRunCount_ = 0;
    int agentVisualRecordingFrameCount_ = 0;
    int inspectorNameEntityId_ = 0;
    int buildTargetPlatformIndex_ = 0;
    int buildConfigurationIndex_ = 0;
    int multiplayerTopologyIndex_ = 0;
    int multiplayerTransportIndex_ = 0;
    int multiplayerPort_ = 7777;
    int multiplayerMaxPlayers_ = 4;
    int projectSettingsSelectedTag_ = 0;
    int projectSettingsSelectedLayer_ = 0;
    int profilerDomainFilter_ = 2;
    bool buildDevelopmentBuild_ = true;
    bool buildCopyProjectFiles_ = true;
    bool buildRunPlayerSmokeAfterBuild_ = false;
    bool multiplayerEnabled_ = false;
    float agentPromptPanelHeight_ = 0.0f;
    float uiScale_ = 1.0f;
    double agentVisualRecordingLastFrameSeconds_ = 0.0;
    int exitCode_ = 0;
    bool hasProfileWindowPlacement_ = false;
    int profileWindowX_ = 0;
    int profileWindowY_ = 0;
    int profileWindowWidth_ = 1600;
    int profileWindowHeight_ = 960;
    bool profileWindowMaximized_ = false;

    unsigned int viewportFramebuffer_ = 0;
    unsigned int viewportColorTexture_ = 0;
    unsigned int viewportDepthRenderbuffer_ = 0;
    unsigned int gameFramebuffer_ = 0;
    unsigned int gameColorTexture_ = 0;
    unsigned int gameDepthRenderbuffer_ = 0;
    int viewportFramebufferWidth_ = 0;
    int viewportFramebufferHeight_ = 0;
    int gameFramebufferWidth_ = 0;
    int gameFramebufferHeight_ = 0;
    int viewportResizeCount_ = 0;
    int gameResizeCount_ = 0;
    int viewportLastRenderedCubeCount_ = 0;
    int viewportLastRenderedResourceMeshCount_ = 0;
    int viewportLastRenderedSpriteCount_ = 0;
    int gameLastRenderedCubeCount_ = 0;
    int gameLastRenderedSpriteCount_ = 0;
    int gameLastRenderedUiElementCount_ = 0;
    bool viewportLastFogApplied_ = false;
    bool gameLastFogApplied_ = false;
    bool viewportLastEnvironmentApplied_ = false;
    bool gameLastEnvironmentApplied_ = false;
    bool viewportFramebufferComplete_ = false;
    bool gameFramebufferComplete_ = false;
    bool viewportFramebufferErrorLogged_ = false;
    bool gameFramebufferErrorLogged_ = false;
    bool viewportSmokeSawCubeRender_ = false;
    bool viewportSmokeSawSelectedRenderable_ = false;
    bool viewportSmokeResizeIssued_ = false;
    bool runtimeUiSmokeVerifiedOverlay_ = false;
    bool viewportGridVisible_ = true;
    bool viewportPhysicsDebugVisible_ = false;
    bool viewportSceneGizmosVisible_ = true;
    bool viewportPivotModeCenter_ = true;
    bool viewportTransformLocal_ = false;
    bool viewportSnapEnabled_ = false;
    float viewportSnapStep_ = 0.25f;
    int sceneToolMode_ = 0;
    bool terrainToolActive_ = false;
    bool terrainBrushPreviewVisible_ = true;
    int terrainBrushMode_ = 0;
    int terrainFalloff_ = 0;
    int terrainSelectedLayer_ = 0;
    float terrainBrushRadius_ = 2.5f;
    float terrainBrushStrength_ = 0.45f;
    float terrainBrushOpacity_ = 1.0f;
    float terrainBrushTargetHeight_ = 1.0f;
    float terrainBrushSpacing_ = 0.35f;
    bool terrainBrushInvert_ = false;
    bool terrainBrushDragging_ = false;
    bool terrainBrushStrokeChanged_ = false;
    int terrainBrushEntityId_ = 0;
    std::array<float, 3> terrainBrushPreviewPoint_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> terrainBrushPreviewNormal_{0.0f, 1.0f, 0.0f};
    std::array<float, 3> terrainBrushLastStampPoint_{0.0f, 0.0f, 0.0f};
    bool terrainBrushPreviewHit_ = false;
    bool terrainBrushHasLastStamp_ = false;
    bool caveToolActive_ = false;
    bool caveBrushPreviewVisible_ = true;
    int caveBrushMode_ = 4;
    int caveFalloff_ = 0;
    int caveSelectedLayer_ = 0;
    float caveBrushRadius_ = 2.0f;
    float caveBrushStrength_ = 0.85f;
    float caveBrushOpacity_ = 1.0f;
    float caveBrushTargetDensity_ = 0.0f;
    float caveBrushSpacing_ = 0.35f;
    bool caveBrushInvert_ = false;
    bool caveBrushDragging_ = false;
    bool caveBrushStrokeChanged_ = false;
    int caveBrushEntityId_ = 0;
    std::array<float, 3> caveBrushPreviewPoint_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> caveBrushPreviewNormal_{0.0f, 1.0f, 0.0f};
    std::array<float, 3> caveBrushLastStampPoint_{0.0f, 0.0f, 0.0f};
    bool caveBrushPreviewHit_ = false;
    bool caveBrushHasLastStamp_ = false;
    bool openSaveEditorLayoutPopup_ = false;
    bool openRenameEditorLayoutPopup_ = false;
    bool openDeleteEditorLayoutPopup_ = false;
    int viewportCameraMode_ = 0;
    float viewportCameraYaw_ = -38.0f;
    float viewportCameraPitch_ = 26.0f;
    float viewportCameraDistance_ = 8.0f;
    float viewportFlySpeed_ = 2.6f;
    float viewportLookSensitivity_ = 0.18f;
    float viewportPanSensitivity_ = 1.0f;
    bool viewportFlyLevelMovement_ = false;
    std::array<float, 3> viewportCameraTarget_{0.0f, 0.8f, 0.0f};
    int viewportHotMoveAxis_ = 0;
    int viewportActiveMoveAxis_ = 0;
    int viewportMoveDragEntityId_ = 0;
    float viewportMoveDragStartAxisT_ = 0.0f;
    bool viewportMoveDragMoved_ = false;
    std::array<float, 3> viewportMoveDragStartPosition_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> viewportMoveDragStartPlanePoint_{0.0f, 0.0f, 0.0f};
};

} // namespace aine

#include "EditorApp.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

aine::EditorAppOptions ParseOptions(int argc, char** argv) {
    aine::EditorAppOptions options;
    const std::string smokePromptPrefix = "--smoke-test-prompt=";
    const std::string providerHealthPrefix = "--smoke-test-provider-health=";
    const std::string providerNormalizerPrefix = "--smoke-test-provider-normalizer=";
    const std::string taskComposerRequestPrefix = "--smoke-test-task-composer-request=";
    const std::string assetSourcePrefix = "--smoke-test-asset-source=";
    const std::string projectRootPrefix = "--project-root=";
    const std::string openProjectPrefix = "--open-project=";
    const std::string appearanceSettingsPrefix = "--appearance-settings-path=";
    const std::string editorProfilePrefix = "--editor-profile-path=";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke-test") {
            options.smokeTest = true;
        } else if (arg == "--smoke-test-appearance") {
            options.smokeTest = true;
            options.smokeTestAppearance = true;
        } else if (arg == "--smoke-test-editor-profile") {
            options.smokeTest = true;
            options.smokeTestEditorProfile = true;
        } else if (arg == "--smoke-test-editor-layouts") {
            options.smokeTest = true;
            options.smokeTestEditorLayouts = true;
        } else if (arg == "--smoke-test-panel-tabs") {
            options.smokeTest = true;
            options.smokeTestPanelTabs = true;
        } else if (arg == "--smoke-test-play-mode") {
            options.smokeTest = true;
            options.smokeTestPlayMode = true;
        } else if (arg == "--smoke-test-physics") {
            options.smokeTest = true;
            options.smokeTestPhysics = true;
        } else if (arg == "--smoke-test-first-game") {
            options.smokeTest = true;
            options.smokeTestFirstGame = true;
        } else if (arg == "--smoke-test-runtime-logs") {
            options.smokeTest = true;
            options.smokeTestRuntimeLogs = true;
        } else if (arg == "--smoke-test-runtime-scripting") {
            options.smokeTest = true;
            options.smokeTestRuntimeScripting = true;
        } else if (arg == "--smoke-test-runtime-ui") {
            options.smokeTest = true;
            options.smokeTestRuntimeUi = true;
        } else if (arg == "--smoke-test-task-composer") {
            options.smokeTest = true;
            options.smokeTestTaskComposer = true;
        } else if (arg == "--smoke-test-agent-visuals") {
            options.smokeTest = true;
            options.smokeTestAgentVisuals = true;
        } else if (arg == "--smoke-test-project-folder") {
            options.smokeTest = true;
            options.smokeTestProjectFolder = true;
        } else if (arg == "--smoke-test-project-tree-actions") {
            options.smokeTest = true;
            options.smokeTestProjectTreeActions = true;
        } else if (arg == "--smoke-test-console") {
            options.smokeTest = true;
            options.smokeTestConsole = true;
        } else if (arg == "--smoke-test-asset-staging") {
            options.smokeTest = true;
            options.smokeTestAssetStaging = true;
        } else if (arg == "--smoke-test-asset-pipeline") {
            options.smokeTest = true;
            options.smokeTestAssetPipeline = true;
        } else if (arg == "--smoke-test-expanded-tools") {
            options.smokeTest = true;
            options.smokeTestExpandedTools = true;
        } else if (arg == "--smoke-test-provider-file-change") {
            options.smokeTest = true;
            options.smokeTestProviderFileChange = true;
        } else if (arg == "--smoke-test-terrain") {
            options.smokeTest = true;
            options.smokeTestTerrain = true;
        } else if (arg == "--smoke-test-unified-terrain-proof") {
            options.smokeTest = true;
            options.smokeTestUnifiedTerrainProof = true;
        } else if (arg == "--smoke-test-terrain-performance") {
            options.smokeTest = true;
            options.smokeTestTerrainPerformance = true;
        } else if (arg == "--smoke-test-caves") {
            options.smokeTest = true;
            options.smokeTestCaves = true;
        } else if (arg == "--smoke-test-fog") {
            options.smokeTest = true;
            options.smokeTestFog = true;
        } else if (arg == "--smoke-test-environment-lighting") {
            options.smokeTest = true;
            options.smokeTestEnvironmentLighting = true;
        } else if (arg == "--smoke-test-sprites") {
            options.smokeTest = true;
            options.smokeTestSprites = true;
        } else if (arg == "--smoke-test-project-builder") {
            options.smokeTest = true;
            options.smokeTestProjectBuilder = true;
        } else if (arg == "--smoke-test-project-settings") {
            options.smokeTest = true;
            options.smokeTestProjectSettings = true;
        } else if (arg == "--smoke-test-render-profiles") {
            options.smokeTest = true;
            options.smokeTestRenderProfiles = true;
        } else if (arg == "--smoke-test-camera-editor") {
            options.smokeTest = true;
            options.smokeTestCameraEditor = true;
        } else if (arg == "--smoke-test-profiler") {
            options.smokeTest = true;
            options.smokeTestProfiler = true;
        } else if (arg == "--smoke-test-scene-integration") {
            options.smokeTest = true;
            options.smokeTestSceneIntegration = true;
        } else if (arg == "--smoke-test-reload") {
            options.smokeTest = true;
            options.smokeTestReload = true;
        } else if (arg.rfind("--smoke-test-frames=", 0) == 0) {
            options.smokeTest = true;
            options.smokeTestFrames = std::max(1, std::atoi(arg.substr(20).c_str()));
        } else if (arg.rfind(smokePromptPrefix, 0) == 0) {
            options.smokeTest = true;
            options.smokeTestPrompt = arg.substr(smokePromptPrefix.size());
        } else if (arg.rfind(providerHealthPrefix, 0) == 0) {
            options.smokeTest = true;
            options.smokeTestProviderHealth = arg.substr(providerHealthPrefix.size());
        } else if (arg.rfind(providerNormalizerPrefix, 0) == 0) {
            options.smokeTest = true;
            options.smokeTestProviderNormalizer = arg.substr(providerNormalizerPrefix.size());
        } else if (arg.rfind(taskComposerRequestPrefix, 0) == 0) {
            options.smokeTest = true;
            options.smokeTestTaskComposer = true;
            options.smokeTestTaskComposerRequest = arg.substr(taskComposerRequestPrefix.size());
        } else if (arg.rfind(assetSourcePrefix, 0) == 0) {
            options.smokeTest = true;
            options.smokeTestAssetStaging = true;
            options.smokeTestAssetSource = arg.substr(assetSourcePrefix.size());
        } else if (arg.rfind(projectRootPrefix, 0) == 0) {
            options.projectRootOverride = arg.substr(projectRootPrefix.size());
        } else if (arg.rfind(openProjectPrefix, 0) == 0) {
            options.openProjectOverride = arg.substr(openProjectPrefix.size());
        } else if (arg.rfind(appearanceSettingsPrefix, 0) == 0) {
            options.appearanceSettingsOverride = arg.substr(appearanceSettingsPrefix.size());
        } else if (arg.rfind(editorProfilePrefix, 0) == 0) {
            options.appearanceSettingsOverride = arg.substr(editorProfilePrefix.size());
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    aine::EditorApp app(ParseOptions(argc, argv));
    if (!app.Initialize()) {
        std::cerr << "Failed to initialize AI Native Editor." << std::endl;
        return 1;
    }

    return app.Run();
}

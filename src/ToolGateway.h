#pragma once

#include "EditorState.h"

#include <array>
#include <string>
#include <vector>

namespace aine {

struct ToolCommand {
    std::string type;
    std::string summary;
    std::string payloadJson;
    std::string path;
    std::string entityName;
    std::string targetEntityName;
    std::string parentEntityName;
    std::string newName;
    std::string componentType;
    std::string propertyName;
    std::string propertyValue;
    std::string assetId;
    std::string materialName;
    std::string content;
    std::string sourceLabel;
    std::string license;
    Component component;
    std::vector<Component> components;
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    bool hasPosition = false;
    bool hasRotation = false;
    bool hasScale = false;
};

struct ToolCommandBatch {
    std::string transactionId;
    std::string name;
    std::string approval = "local_auto_approved";
    std::vector<ToolCommand> commands;
    std::vector<std::string> changedEntities;
    std::vector<std::string> changedFiles;
    std::vector<std::string> diagnostics;
    bool ok = true;
    bool applied = false;
    bool undoAvailable = false;
};

struct ToolGatewayResult {
    std::string assistantMessage;
    ToolCommandBatch batch;
};

struct ToolGatewayValidationResult {
    bool ok = true;
    std::vector<std::string> diagnostics;
};

class ToolGateway {
public:
    ToolGatewayResult SubmitPrompt(const std::string& prompt, EditorState& state);
    ToolGatewayResult ApplyApprovedCommandBatch(ToolCommandBatch batch, EditorState& state, std::string approval);
    ToolGatewayValidationResult ValidateCommandBatchSchema(const ToolCommandBatch& batch) const;
    const std::vector<std::string>& SupportedCommands() const { return supportedCommands_; }

private:
    std::string NextTransactionId();
    ToolCommandBatch BuildCommandBatch(const std::string& prompt, const std::string& transactionId, const EditorState& state) const;
    bool ApplyCommandBatch(ToolCommandBatch& batch, EditorState& state) const;
    static ToolCommand ProjectGetStateCommand();
    static ToolCommand ProjectCreateCommand(std::string path);
    static ToolCommand ProjectOpenCommand(std::string path);
    static ToolCommand ProjectSaveCommand();
    static ToolCommand ProjectSetSaveLocationCommand(std::string path);
    static ToolCommand ValidateSceneCommand();
    static ToolCommand ValidateProjectCommand();
    static ToolCommand ValidateAssetsCommand();
    static ToolCommand ValidateScriptsCommand();
    static ToolCommand CreateEntityCommand(std::string name, std::vector<Component> components);
    static ToolCommand RenameEntityCommand(std::string targetName, std::string newName);
    static ToolCommand SetTransformCommand(std::string targetName, const std::array<float, 3>* position,
                                           const std::array<float, 3>* rotation, const std::array<float, 3>* scale);
    static ToolCommand AddComponentCommand(std::string targetName, Component component);
    static void UpdatePayload(ToolCommand& command);
    static std::string BuildAssistantMessage(const ToolCommandBatch& batch);
    static std::string FirstQuotedText(const std::string& prompt);
    static bool PromptLooksLikeColliderTroubleshootingRequest(const std::string& prompt);
    static bool PromptLooksLikeRunningSimulatorRequest(const std::string& prompt);
    static bool PromptLooksLikeStarterSceneRequest(const std::string& prompt);
    static std::string ToLower(std::string value);

    int nextTransactionNumber_ = 1;
    std::vector<std::string> supportedCommands_{
        "project.getState",
        "project.create",
        "project.open",
        "project.setSaveLocation",
        "scene.create",
        "scene.open",
        "scene.save",
        "scene.createEntity",
        "scene.deleteEntity",
        "scene.renameEntity",
        "scene.setParent",
        "scene.setTransform",
        "scene.addComponent",
        "scene.removeComponent",
        "scene.setComponentProperty",
        "runtime.play",
        "runtime.pause",
        "runtime.stop",
        "runtime.captureScreenshot",
        "runtime.startRecording",
        "runtime.stopRecording",
        "runtime.getLogs",
        "validate.project",
        "validate.assets",
        "validate.scripts",
        "asset.import",
        "asset.find",
        "asset.getMetadata",
        "asset.createMaterial",
        "asset.assignMaterial",
        "script.create",
        "script.modify",
        "script.compile",
        "script.getDiagnostics",
        "script.attachToEntity",
        "build.packagePlayer",
        "build.packageWindows",
        "build.runSmokeTest",
        "build.openOutputFolder",
        "plugin.create",
        "plugin.compile",
        "plugin.test",
        "plugin.install",
        "plugin.enable",
        "plugin.disable",
        "project.save",
        "validate.scene",
    };
};

} // namespace aine

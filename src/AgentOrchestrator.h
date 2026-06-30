#pragma once

#include "EditorState.h"
#include "ProviderResult.h"
#include "ToolGateway.h"

#include <functional>
#include <string>
#include <vector>

namespace aine {

enum class AgentBackend {
    LocalToolGateway = 0,
    CodexCli = 1,
    OpenAIApi = 2,
    Claude = 3,
    LocalPromptAssistant = 4,
};

struct ProjectContextSnapshot {
    std::string projectPath;
    std::string projectFilePath;
    std::string sceneFilePath;
    std::string gameSavePath;
    std::string sceneName;
    std::string selectedEntity;
    std::vector<std::string> entities;
    std::vector<std::string> recentChat;
    std::vector<std::string> recentLogs;
    std::vector<std::string> toolGatewayDiagnostics;

    std::string Summary() const;
    std::string ToCompactText() const;
};

struct ProviderHealth {
    AgentBackend backend = AgentBackend::LocalToolGateway;
    std::string id;
    std::string label;
    std::string status;
    std::string detail;
    std::string executablePath;
    std::string source;
    std::vector<std::string> probeDetails;
    bool available = false;
    bool implemented = false;
};

struct AgentOrchestratorResult {
    AgentBackend backend = AgentBackend::LocalToolGateway;
    std::string assistantMessage;
    std::vector<std::string> diagnostics;
    NormalizedProviderResult normalizedResult;
    ToolGatewayResult gatewayResult;
    std::string stdoutText;
    std::string stderrText;
    int exitCode = 0;
    bool ok = true;
    bool usedFallback = false;
    bool hasNormalizedResult = false;
    bool hasGatewayResult = false;
};

using AgentProgressCallback = std::function<void(const AgentActivity& activity)>;

class AgentOrchestrator {
public:
    AgentOrchestrator();

    void RefreshProviderHealth(EditorState* state = nullptr);
    const std::vector<ProviderHealth>& Providers() const { return providers_; }
    const ProviderHealth* FindProvider(AgentBackend backend) const;

    AgentOrchestratorResult SubmitPrompt(AgentBackend backend, const std::string& prompt, EditorState& state,
                                         ToolGateway& gateway, const ToolCommandBatch* lastGatewayBatch,
                                         AgentProgressCallback progress = {});
    NormalizedProviderResult NormalizeProviderOutput(AgentBackend backend, const std::string& output,
                                                     const ToolGateway& gateway) const;

    ProjectContextSnapshot CaptureProjectContext(const EditorState& state, const ToolCommandBatch* lastGatewayBatch) const;
    std::vector<std::string> PromptSuggestions(const std::string& draft, const EditorState& state) const;

    static const char* BackendLabel(AgentBackend backend);
    static const char* BackendId(AgentBackend backend);

private:
    AgentOrchestratorResult SubmitLocalToolGateway(const std::string& prompt, EditorState& state, ToolGateway& gateway);
    AgentOrchestratorResult SubmitCodexCli(const std::string& prompt, const ProjectContextSnapshot& context,
                                           EditorState& state, ToolGateway& gateway,
                                           const std::vector<std::string>& supportedCommands,
                                           AgentProgressCallback progress = {});
    AgentOrchestratorResult SubmitOpenAIApi(const std::string& prompt, const ProjectContextSnapshot& context,
                                            EditorState& state, ToolGateway& gateway,
                                            const std::vector<std::string>& supportedCommands);
    AgentOrchestratorResult SubmitClaudePlaceholder(const std::string& prompt, const ProjectContextSnapshot& context,
                                                    EditorState& state, ToolGateway& gateway);
    AgentOrchestratorResult SubmitLocalPromptAssistant(const std::string& prompt, const ProjectContextSnapshot& context,
                                                       EditorState& state);
    AgentOrchestratorResult SubmitProviderFailure(AgentBackend backend, const std::string& status,
                                                  const std::string& reason, EditorState& state);
    AgentOrchestratorResult RunGatewayFallback(const std::string& prompt, const std::string& reason,
                                               EditorState& state, ToolGateway& gateway);

    ProviderHealth BuildProviderHealth(AgentBackend backend) const;
    std::string BuildCodexPrompt(const std::string& prompt, const ProjectContextSnapshot& context,
                                 const std::vector<std::string>& supportedCommands) const;
    std::string BuildProviderPrompt(const std::string& providerLabel, const std::string& prompt,
                                    const ProjectContextSnapshot& context,
                                    const std::vector<std::string>& supportedCommands) const;

    std::vector<ProviderHealth> providers_;
};

} // namespace aine

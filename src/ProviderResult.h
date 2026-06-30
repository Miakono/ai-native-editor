#pragma once

#include "EditorState.h"
#include "ToolGateway.h"

#include <string>
#include <vector>

namespace aine {

enum class ProviderResultType {
    ChatResponse,
    ProposedCommandBatch,
    ProposedFileChange,
    Diagnostic,
    ProviderError,
};

struct ProposedFileChange {
    std::string path;
    std::string changeType;
    std::string summary;
    std::string content;
    std::string diff;
    std::string sessionId;
    std::string previousContent;
    std::string appliedPath;
    bool previousContentCaptured = false;
    bool previousFileExisted = false;
};

struct NormalizedProviderResult {
    ProviderResultType type = ProviderResultType::ChatResponse;
    std::string title;
    std::string message;
    std::string rawOutput;
    std::vector<std::string> diagnostics;
    ToolCommandBatch commandBatch;
    ProposedFileChange fileChange;
    bool hasCommandBatch = false;
    bool hasFileChange = false;
    bool schemaValid = false;
    bool requiresApproval = false;
    bool canApprove = false;
    bool approved = false;
    bool rejected = false;
    bool applied = false;
};

class ProviderResultNormalizer {
public:
    NormalizedProviderResult Normalize(const std::string& providerLabel, const std::string& output,
                                       const ToolGateway& gateway) const;

    static const char* TypeId(ProviderResultType type);
};

} // namespace aine

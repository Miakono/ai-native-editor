#include "ProviderResult.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace aine {
namespace {

using json = nlohmann::json;

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

bool IsJsonLike(const std::string& value) {
    const std::string trimmed = Trim(value);
    return !trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[');
}

NormalizedProviderResult ErrorResult(const std::string& title, const std::string& message, const std::string& rawOutput = {}) {
    NormalizedProviderResult result;
    result.type = ProviderResultType::ProviderError;
    result.title = title;
    result.message = message;
    result.rawOutput = rawOutput;
    result.schemaValid = false;
    result.diagnostics.push_back(message);
    return result;
}

bool RequireObject(const json& value, const std::string& path, std::vector<std::string>& diagnostics) {
    if (!value.is_object()) {
        diagnostics.push_back(path + " must be an object.");
        return false;
    }
    return true;
}

std::string RequiredString(const json& value, const std::string& key, const std::string& path,
                           std::vector<std::string>& diagnostics) {
    if (!value.contains(key) || !value.at(key).is_string() || value.at(key).get<std::string>().empty()) {
        diagnostics.push_back(path + "." + key + " must be a non-empty string.");
        return {};
    }
    return value.at(key).get<std::string>();
}

std::string OptionalString(const json& value, const std::string& key) {
    if (!value.contains(key) || !value.at(key).is_string()) {
        return {};
    }
    return value.at(key).get<std::string>();
}

std::string JsonValueToString(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_null()) {
        return {};
    }
    return value.dump();
}

std::string OptionalFlexibleString(const json& value, const std::string& key) {
    if (!value.contains(key)) {
        return {};
    }
    return JsonValueToString(value.at(key));
}

bool ParseVec3(const json& value, std::array<float, 3>& out, const std::string& path, std::vector<std::string>& diagnostics) {
    if (!value.is_array() || value.size() != 3) {
        diagnostics.push_back(path + " must be an array of three numbers.");
        return false;
    }

    for (size_t i = 0; i < 3; ++i) {
        if (!value.at(i).is_number()) {
            diagnostics.push_back(path + "[" + std::to_string(i) + "] must be a number.");
            return false;
        }
        out[i] = value.at(i).get<float>();
    }
    return true;
}

Component ParseComponent(const json& value, const std::string& path, std::vector<std::string>& diagnostics) {
    Component component;
    if (!RequireObject(value, path, diagnostics)) {
        return component;
    }

    component.type = RequiredString(value, "type", path, diagnostics);
    if (value.contains("properties")) {
        if (!value.at("properties").is_object()) {
            diagnostics.push_back(path + ".properties must be an object when present.");
        } else {
            for (const auto& [name, propertyValue] : value.at("properties").items()) {
                component.properties.push_back({name, propertyValue.is_string() ? propertyValue.get<std::string>() : propertyValue.dump()});
            }
        }
    }
    return component;
}

ToolCommand ParseCommand(const json& value, size_t index, std::vector<std::string>& diagnostics) {
    ToolCommand command;
    const std::string path = "command_batch.commands[" + std::to_string(index) + "]";
    if (!RequireObject(value, path, diagnostics)) {
        return command;
    }

    command.type = RequiredString(value, "type", path, diagnostics);
    command.summary = OptionalString(value, "summary");

    if (command.type == "project.getState") {
        if (command.summary.empty()) {
            command.summary = "Read current project, scene, selection, and log context.";
        }
    } else if (command.type == "project.create") {
        command.path = RequiredString(value, "path", path, diagnostics);
        if (command.summary.empty()) {
            command.summary = "Create project at " + command.path + ".";
        }
    } else if (command.type == "project.open") {
        command.path = RequiredString(value, "path", path, diagnostics);
        if (command.summary.empty()) {
            command.summary = "Open project at " + command.path + ".";
        }
    } else if (command.type == "project.setSaveLocation") {
        command.path = OptionalString(value, "path");
        if (command.path.empty()) {
            command.path = RequiredString(value, "saveDataRoot", path, diagnostics);
        }
        if (command.summary.empty()) {
            command.summary = "Set game save folder to " + command.path + ".";
        }
    } else if (command.type == "project.save") {
        if (command.summary.empty()) {
            command.summary = "Save project metadata and active scene JSON to disk.";
        }
    } else if (command.type == "scene.create") {
        command.path = OptionalString(value, "path");
        if (command.path.empty()) {
            command.path = OptionalString(value, "name");
        }
        if (command.path.empty()) {
            command.path = RequiredString(value, "scene", path, diagnostics);
        }
        if (command.summary.empty()) {
            command.summary = "Create scene " + command.path + ".";
        }
    } else if (command.type == "scene.open") {
        command.path = RequiredString(value, "path", path, diagnostics);
        if (command.summary.empty()) {
            command.summary = "Open scene " + command.path + ".";
        }
    } else if (command.type == "scene.save") {
        if (command.summary.empty()) {
            command.summary = "Save the active scene.";
        }
    } else if (command.type == "validate.scene") {
        if (command.summary.empty()) {
            command.summary = "Validate required scene data and component basics.";
        }
    } else if (command.type == "validate.project") {
        if (command.summary.empty()) {
            command.summary = "Validate project folders and active paths.";
        }
    } else if (command.type == "validate.assets") {
        if (command.summary.empty()) {
            command.summary = "Validate imported asset records.";
        }
    } else if (command.type == "validate.scripts") {
        if (command.summary.empty()) {
            command.summary = "Validate script references.";
        }
    } else if (command.type == "scene.createEntity") {
        command.entityName = RequiredString(value, "name", path, diagnostics);
        if (value.contains("components")) {
            if (!value.at("components").is_array()) {
                diagnostics.push_back(path + ".components must be an array when present.");
            } else {
                for (size_t componentIndex = 0; componentIndex < value.at("components").size(); ++componentIndex) {
                    command.components.push_back(ParseComponent(value.at("components").at(componentIndex),
                                                                path + ".components[" + std::to_string(componentIndex) + "]",
                                                                diagnostics));
                }
            }
        }
        if (command.summary.empty()) {
            command.summary = "Create entity " + command.entityName + ".";
        }
    } else if (command.type == "scene.deleteEntity") {
        command.targetEntityName = RequiredString(value, "targetName", path, diagnostics);
        if (command.summary.empty()) {
            command.summary = "Delete " + command.targetEntityName + ".";
        }
    } else if (command.type == "scene.renameEntity") {
        command.targetEntityName = RequiredString(value, "targetName", path, diagnostics);
        command.newName = RequiredString(value, "newName", path, diagnostics);
        if (command.summary.empty()) {
            command.summary = "Rename " + command.targetEntityName + " to " + command.newName + ".";
        }
    } else if (command.type == "scene.setParent") {
        command.targetEntityName = RequiredString(value, "targetName", path, diagnostics);
        command.parentEntityName = OptionalString(value, "parentName");
        if (command.parentEntityName.empty()) {
            command.parentEntityName = OptionalString(value, "parentTargetName");
        }
        if (command.summary.empty()) {
            command.summary = "Set parent for " + command.targetEntityName + ".";
        }
    } else if (command.type == "scene.setTransform") {
        command.targetEntityName = RequiredString(value, "targetName", path, diagnostics);
        if (!value.contains("transform") || !value.at("transform").is_object()) {
            diagnostics.push_back(path + ".transform must be an object.");
        } else {
            const json& transform = value.at("transform");
            if (transform.contains("position") &&
                ParseVec3(transform.at("position"), command.position, path + ".transform.position", diagnostics)) {
                command.hasPosition = true;
            }
            if (transform.contains("rotation") &&
                ParseVec3(transform.at("rotation"), command.rotation, path + ".transform.rotation", diagnostics)) {
                command.hasRotation = true;
            }
            if (transform.contains("scale") &&
                ParseVec3(transform.at("scale"), command.scale, path + ".transform.scale", diagnostics)) {
                command.hasScale = true;
            }
        }
        if (command.summary.empty()) {
            command.summary = "Set transform for " + command.targetEntityName + ".";
        }
    } else if (command.type == "scene.addComponent") {
        command.targetEntityName = RequiredString(value, "targetName", path, diagnostics);
        if (!value.contains("component")) {
            diagnostics.push_back(path + ".component must be present.");
        } else {
            command.component = ParseComponent(value.at("component"), path + ".component", diagnostics);
        }
        if (command.summary.empty()) {
            command.summary = "Add " + command.component.type + " to " + command.targetEntityName + ".";
        }
    } else if (command.type == "scene.removeComponent") {
        command.targetEntityName = RequiredString(value, "targetName", path, diagnostics);
        command.componentType = RequiredString(value, "componentType", path, diagnostics);
        if (command.summary.empty()) {
            command.summary = "Remove " + command.componentType + " from " + command.targetEntityName + ".";
        }
    } else if (command.type == "scene.setComponentProperty") {
        command.targetEntityName = RequiredString(value, "targetName", path, diagnostics);
        command.componentType = RequiredString(value, "componentType", path, diagnostics);
        command.propertyName = RequiredString(value, "propertyName", path, diagnostics);
        command.propertyValue = OptionalFlexibleString(value, "value");
        if (command.propertyValue.empty()) {
            command.propertyValue = OptionalFlexibleString(value, "propertyValue");
        }
        if (command.summary.empty()) {
            command.summary = "Set " + command.componentType + "." + command.propertyName + " on " +
                              command.targetEntityName + ".";
        }
    } else if (command.type == "runtime.play" || command.type == "runtime.pause" ||
               command.type == "runtime.stop" || command.type == "runtime.captureScreenshot" ||
               command.type == "runtime.startRecording" || command.type == "runtime.stopRecording" ||
               command.type == "runtime.getLogs") {
        if (command.summary.empty()) {
            command.summary = command.type;
        }
    } else if (command.type == "asset.import") {
        command.path = RequiredString(value, "path", path, diagnostics);
        command.sourceLabel = OptionalString(value, "source");
        command.license = OptionalString(value, "license");
        if (command.summary.empty()) {
            command.summary = "Import asset " + command.path + ".";
        }
    } else if (command.type == "asset.find") {
        command.entityName = OptionalString(value, "name");
        if (command.summary.empty()) {
            command.summary = "Find imported assets.";
        }
    } else if (command.type == "asset.getMetadata") {
        command.assetId = OptionalString(value, "assetId");
        command.entityName = OptionalString(value, "name");
        command.path = OptionalString(value, "path");
        if (command.summary.empty()) {
            command.summary = "Read asset metadata.";
        }
    } else if (command.type == "asset.createMaterial") {
        command.entityName = RequiredString(value, "name", path, diagnostics);
        command.propertyValue = OptionalFlexibleString(value, "color");
        if (command.summary.empty()) {
            command.summary = "Create material " + command.entityName + ".";
        }
    } else if (command.type == "asset.assignMaterial") {
        command.targetEntityName = RequiredString(value, "targetName", path, diagnostics);
        command.materialName = OptionalString(value, "material");
        if (command.materialName.empty()) {
            command.materialName = RequiredString(value, "materialName", path, diagnostics);
        }
        if (command.summary.empty()) {
            command.summary = "Assign material " + command.materialName + " to " + command.targetEntityName + ".";
        }
    } else if (command.type == "script.create" || command.type == "script.modify") {
        command.path = RequiredString(value, "path", path, diagnostics);
        command.content = OptionalString(value, "content");
        if (command.summary.empty()) {
            command.summary = std::string(command.type == "script.create" ? "Create script " : "Modify script ") +
                              command.path + ".";
        }
    } else if (command.type == "script.compile") {
        if (command.summary.empty()) {
            command.summary = "Compile game scripts.";
        }
    } else if (command.type == "script.getDiagnostics") {
        command.path = OptionalString(value, "path");
        if (command.summary.empty()) {
            command.summary = "Read script diagnostics.";
        }
    } else if (command.type == "script.attachToEntity") {
        command.targetEntityName = RequiredString(value, "targetName", path, diagnostics);
        command.path = RequiredString(value, "path", path, diagnostics);
        command.componentType = OptionalString(value, "componentType");
        if (command.summary.empty()) {
            command.summary = "Attach script " + command.path + " to " + command.targetEntityName + ".";
        }
    } else if (command.type == "build.packagePlayer" || command.type == "build.packageWindows" ||
               command.type == "build.runSmokeTest" ||
               command.type == "build.openOutputFolder") {
        command.path = OptionalString(value, "path");
        if (command.summary.empty()) {
            command.summary = command.type;
        }
    } else if (command.type == "plugin.create" || command.type == "plugin.compile" ||
               command.type == "plugin.test" || command.type == "plugin.install" ||
               command.type == "plugin.enable" || command.type == "plugin.disable") {
        command.entityName = RequiredString(value, "name", path, diagnostics);
        command.path = OptionalString(value, "path");
        if (command.summary.empty()) {
            command.summary = command.type + " " + command.entityName + ".";
        }
    }

    command.payloadJson = value.dump(2);
    return command;
}

ToolCommandBatch ParseCommandBatch(const json& value, std::vector<std::string>& diagnostics) {
    ToolCommandBatch batch;
    if (!RequireObject(value, "command_batch", diagnostics)) {
        return batch;
    }

    if (!value.contains("version") || !value.at("version").is_number_integer() || value.at("version").get<int>() != 1) {
        diagnostics.push_back("command_batch.version must be integer 1.");
    }
    batch.name = RequiredString(value, "name", "command_batch", diagnostics);
    batch.approval = "provider_requires_approval";
    if (value.contains("transactionId") && value.at("transactionId").is_string()) {
        batch.transactionId = value.at("transactionId").get<std::string>();
    }

    if (!value.contains("commands") || !value.at("commands").is_array()) {
        diagnostics.push_back("command_batch.commands must be an array.");
        return batch;
    }

    for (size_t index = 0; index < value.at("commands").size(); ++index) {
        batch.commands.push_back(ParseCommand(value.at("commands").at(index), index, diagnostics));
    }
    return batch;
}

} // namespace

NormalizedProviderResult ProviderResultNormalizer::Normalize(const std::string& providerLabel, const std::string& output,
                                                            const ToolGateway& gateway) const {
    const std::string trimmed = Trim(output);
    if (trimmed.empty()) {
        return ErrorResult(providerLabel + " output error", "Provider returned no output.", output);
    }

    if (!IsJsonLike(trimmed)) {
        NormalizedProviderResult result;
        result.type = ProviderResultType::ChatResponse;
        result.title = providerLabel + " chat response";
        result.message = trimmed;
        result.rawOutput = output;
        result.schemaValid = true;
        return result;
    }

    json document;
    try {
        document = json::parse(trimmed);
    } catch (const std::exception& exception) {
        return ErrorResult(providerLabel + " invalid JSON",
                           std::string("Provider output looked like JSON but could not be parsed: ") + exception.what(),
                           output);
    }

    if (!document.is_object()) {
        return ErrorResult(providerLabel + " unsupported JSON", "Provider JSON must be a top-level object.", output);
    }

    std::vector<std::string> diagnostics;
    const std::string resultType = RequiredString(document, "result_type", "provider_result", diagnostics);
    if (!diagnostics.empty()) {
        return ErrorResult(providerLabel + " unsupported JSON", JoinStrings(diagnostics, " "), output);
    }

    if (resultType == "chat_response") {
        NormalizedProviderResult result;
        result.type = ProviderResultType::ChatResponse;
        result.title = providerLabel + " chat response";
        result.message = RequiredString(document, "message", "provider_result", diagnostics);
        result.rawOutput = output;
        result.schemaValid = diagnostics.empty();
        result.diagnostics = diagnostics;
        if (!result.schemaValid) {
            return ErrorResult(providerLabel + " invalid chat_response", JoinStrings(diagnostics, " "), output);
        }
        return result;
    }

    if (resultType == "diagnostic") {
        NormalizedProviderResult result;
        result.type = ProviderResultType::Diagnostic;
        result.title = OptionalString(document, "severity").empty() ? providerLabel + " diagnostic"
                                                                    : providerLabel + " " + OptionalString(document, "severity");
        result.message = RequiredString(document, "message", "provider_result", diagnostics);
        result.rawOutput = output;
        const bool schemaOk = diagnostics.empty();
        if (document.contains("findings") && document.at("findings").is_array()) {
            for (const json& finding : document.at("findings")) {
                if (finding.is_string() && !finding.get<std::string>().empty()) {
                    diagnostics.push_back(finding.get<std::string>());
                }
            }
        }
        const std::string recommendedNextStep = OptionalString(document, "recommended_next_step");
        if (!recommendedNextStep.empty()) {
            diagnostics.push_back("Next step: " + recommendedNextStep);
        }
        result.schemaValid = schemaOk;
        result.diagnostics = diagnostics;
        if (!result.schemaValid) {
            return ErrorResult(providerLabel + " invalid diagnostic", JoinStrings(diagnostics, " "), output);
        }
        return result;
    }

    if (resultType == "provider_error") {
        NormalizedProviderResult result;
        result.type = ProviderResultType::ProviderError;
        result.title = providerLabel + " provider error";
        result.message = RequiredString(document, "message", "provider_result", diagnostics);
        result.rawOutput = output;
        result.schemaValid = diagnostics.empty();
        result.diagnostics = diagnostics;
        if (!result.schemaValid) {
            return ErrorResult(providerLabel + " invalid provider_error", JoinStrings(diagnostics, " "), output);
        }
        return result;
    }

    if (resultType == "proposed_file_change") {
        NormalizedProviderResult result;
        result.type = ProviderResultType::ProposedFileChange;
        result.title = providerLabel + " proposed file change";
        result.message = OptionalString(document, "message");
        result.rawOutput = output;
        if (!document.contains("file_change") || !document.at("file_change").is_object()) {
            diagnostics.push_back("provider_result.file_change must be an object.");
        } else {
            const json& fileChange = document.at("file_change");
            result.fileChange.path = RequiredString(fileChange, "path", "file_change", diagnostics);
            result.fileChange.changeType = OptionalString(fileChange, "change_type");
            result.fileChange.summary = OptionalString(fileChange, "summary");
            result.fileChange.content = OptionalString(fileChange, "content");
            result.fileChange.diff = OptionalString(fileChange, "diff");
            result.fileChange.sessionId = OptionalString(fileChange, "session_id");
            if (result.fileChange.content.empty() && result.fileChange.diff.empty()) {
                diagnostics.push_back("file_change.content or file_change.diff must be present.");
            }
            result.hasFileChange = true;
        }
        result.schemaValid = diagnostics.empty();
        result.diagnostics = diagnostics;
        result.requiresApproval = result.schemaValid;
        result.canApprove = result.schemaValid;
        if (!result.schemaValid) {
            return ErrorResult(providerLabel + " invalid proposed_file_change", JoinStrings(diagnostics, " "), output);
        }
        return result;
    }

    if (resultType == "proposed_command_batch") {
        NormalizedProviderResult result;
        result.type = ProviderResultType::ProposedCommandBatch;
        result.title = providerLabel + " proposed command batch";
        result.message = OptionalString(document, "message");
        result.rawOutput = output;
        if (!document.contains("command_batch")) {
            diagnostics.push_back("provider_result.command_batch must be present.");
        } else {
            result.commandBatch = ParseCommandBatch(document.at("command_batch"), diagnostics);
            result.hasCommandBatch = true;
        }

        if (diagnostics.empty()) {
            ToolGatewayValidationResult validation = gateway.ValidateCommandBatchSchema(result.commandBatch);
            if (!validation.ok) {
                diagnostics.insert(diagnostics.end(), validation.diagnostics.begin(), validation.diagnostics.end());
            }
        }

        result.schemaValid = diagnostics.empty();
        result.diagnostics = diagnostics;
        result.requiresApproval = result.schemaValid;
        result.canApprove = result.schemaValid;
        if (!result.schemaValid) {
            return ErrorResult(providerLabel + " invalid command batch", JoinStrings(diagnostics, " "), output);
        }
        return result;
    }

    return ErrorResult(providerLabel + " unsupported provider result",
                       "Unsupported result_type '" + resultType +
                           "'. Expected chat_response, proposed_command_batch, proposed_file_change, diagnostic, or provider_error.",
                       output);
}

const char* ProviderResultNormalizer::TypeId(ProviderResultType type) {
    switch (type) {
    case ProviderResultType::ProposedCommandBatch:
        return "proposed_command_batch";
    case ProviderResultType::ProposedFileChange:
        return "proposed_file_change";
    case ProviderResultType::Diagnostic:
        return "diagnostic";
    case ProviderResultType::ProviderError:
        return "provider_error";
    case ProviderResultType::ChatResponse:
    default:
        return "chat_response";
    }
}

} // namespace aine

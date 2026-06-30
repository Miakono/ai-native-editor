#pragma once

#include "engine/core/Log.h"
#include "engine/scene/Entity.h"

#include <filesystem>
#include <string>
#include <vector>

namespace aine {

struct ScriptRuntimeHostConfig {
    bool enabled = false;
    std::filesystem::path projectRoot;
    std::filesystem::path manifestPath;
    std::filesystem::path assemblyPath;
    std::filesystem::path outputRoot;
};

struct ScriptRuntimeDiagnostic {
    LogLevel level = LogLevel::Info;
    int entityId = 0;
    std::string entityName;
    std::string scriptType;
    std::string message;
};

struct ScriptRuntimeFieldUpdate {
    int entityId = 0;
    std::string componentType;
    std::string name;
    std::string value;
};

struct ScriptRuntimeHostResult {
    bool ok = true;
    bool hostReady = true;
    bool hostRan = false;
    int exitCode = 0;
    std::string phase;
    std::filesystem::path hostProjectPath;
    std::filesystem::path hostAssemblyPath;
    std::filesystem::path sceneInputPath;
    std::filesystem::path resultPath;
    std::filesystem::path invokeLogPath;
    int invokedCount = 0;
    std::vector<ScriptRuntimeDiagnostic> diagnostics;
    std::vector<ScriptRuntimeFieldUpdate> fieldUpdates;
};

class ScriptRuntimeHost {
public:
    ScriptRuntimeHostResult Begin(std::vector<Entity>& entities, const ScriptRuntimeHostConfig& config);
    ScriptRuntimeHostResult Update(std::vector<Entity>& entities, float deltaSeconds);
    ScriptRuntimeHostResult Destroy(std::vector<Entity>& entities);

    bool Active() const { return active_; }
    void Reset();

private:
    ScriptRuntimeHostResult Invoke(std::vector<Entity>& entities, std::string phase, float deltaSeconds);
    bool EnsureHostBuilt(ScriptRuntimeHostResult& result);

    ScriptRuntimeHostConfig config_;
    bool active_ = false;
};

bool SceneHasEnabledScriptComponents(const std::vector<Entity>& entities);

} // namespace aine

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aine {

enum class ScriptDiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct ScriptDiagnostic {
    ScriptDiagnosticSeverity severity = ScriptDiagnosticSeverity::Info;
    std::string path;
    int line = 0;
    int column = 0;
    std::string code;
    std::string message;
};

struct ScriptSourceUnit {
    std::filesystem::path absolutePath;
    std::string relativePath;
    std::vector<std::string> declarations;
    bool editorOnly = false;
};

struct ScriptCompileOptions {
    std::filesystem::path projectRoot;
    std::filesystem::path assetRoot;
    std::filesystem::path outputRoot;
    std::string assemblyName = "AINative.GameScripts";
    bool runExternalCompiler = true;
};

struct ScriptCompileResult {
    bool ok = true;
    bool compilerRan = false;
    int compilerExitCode = 0;
    std::string compilerName = "dotnet";
    std::string assemblyName = "AINative.GameScripts";
    std::filesystem::path manifestPath;
    std::filesystem::path projectFilePath;
    std::filesystem::path apiStubPath;
    std::filesystem::path assemblyPath;
    std::filesystem::path compilerLogPath;
    std::vector<ScriptSourceUnit> sources;
    std::vector<ScriptDiagnostic> diagnostics;
};

const char* ScriptDiagnosticSeverityLabel(ScriptDiagnosticSeverity severity);
ScriptCompileResult CompileProjectScripts(const ScriptCompileOptions& options);
std::vector<std::string> FormatScriptCompileDiagnostics(const ScriptCompileResult& result);

} // namespace aine

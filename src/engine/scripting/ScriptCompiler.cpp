#include "engine/scripting/ScriptCompiler.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace aine {
namespace {

using json = nlohmann::json;

constexpr const char* kRuntimeApiFileName = "AINative.RuntimeApi.g.cs";
constexpr const char* kProjectFileName = "AINative.GameScripts.csproj";
constexpr const char* kManifestFileName = "compile_manifest.json";
constexpr const char* kCompilerLogFileName = "compile.log";

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string NormalizePathString(std::filesystem::path path) {
    return path.lexically_normal().generic_string();
}

std::string LowerPathString(std::filesystem::path path) {
    return ToLower(NormalizePathString(std::move(path)));
}

bool IsPathInside(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::string rootString = LowerPathString(std::filesystem::absolute(root));
    std::string candidateString = LowerPathString(std::filesystem::absolute(candidate));
    if (!rootString.empty() && rootString.back() != '/') {
        rootString.push_back('/');
    }
    return candidateString == rootString.substr(0, rootString.size() - 1) ||
           candidateString.rfind(rootString, 0) == 0;
}

std::filesystem::path RelativeToProject(const std::filesystem::path& projectRoot, const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path relative = std::filesystem::relative(path, projectRoot, error);
    if (error || relative.is_absolute()) {
        return path.filename();
    }
    for (const std::filesystem::path& part : relative) {
        if (part == ".") {
            continue;
        }
        if (part == "..") {
            return path.filename();
        }
        break;
    }
    return relative;
}

std::string RelativeToProjectString(const std::filesystem::path& projectRoot, const std::filesystem::path& path) {
    return RelativeToProject(projectRoot, path).generic_string();
}

bool IsScriptFileExtension(std::string extension) {
    extension = ToLower(std::move(extension));
    return extension == ".cs";
}

bool HasEditorPathSegment(const std::filesystem::path& path) {
    for (const std::filesystem::path& part : path) {
        if (ToLower(part.string()) == "editor") {
            return true;
        }
    }
    return false;
}

void AddDiagnostic(ScriptCompileResult& result, ScriptDiagnosticSeverity severity, std::string message,
                   std::string path = {}, int line = 0, int column = 0, std::string code = {}) {
    if (severity == ScriptDiagnosticSeverity::Error) {
        result.ok = false;
    }
    result.diagnostics.push_back({severity, std::move(path), line, column, std::move(code), std::move(message)});
}

std::string StripCommentsAndStrings(const std::string& source) {
    std::string stripped;
    stripped.reserve(source.size());

    bool lineComment = false;
    bool blockComment = false;
    bool stringLiteral = false;
    bool charLiteral = false;
    bool escape = false;

    for (size_t index = 0; index < source.size(); ++index) {
        const char current = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : '\0';

        if (lineComment) {
            if (current == '\n') {
                lineComment = false;
                stripped.push_back('\n');
            } else {
                stripped.push_back(' ');
            }
            continue;
        }

        if (blockComment) {
            if (current == '*' && next == '/') {
                stripped.push_back(' ');
                stripped.push_back(' ');
                ++index;
                blockComment = false;
            } else {
                stripped.push_back(current == '\n' ? '\n' : ' ');
            }
            continue;
        }

        if (stringLiteral) {
            stripped.push_back(current == '\n' ? '\n' : ' ');
            if (!escape && current == '"') {
                stringLiteral = false;
            }
            escape = !escape && current == '\\';
            if (current != '\\') {
                escape = false;
            }
            continue;
        }

        if (charLiteral) {
            stripped.push_back(current == '\n' ? '\n' : ' ');
            if (!escape && current == '\'') {
                charLiteral = false;
            }
            escape = !escape && current == '\\';
            if (current != '\\') {
                escape = false;
            }
            continue;
        }

        if (current == '/' && next == '/') {
            stripped.push_back(' ');
            stripped.push_back(' ');
            ++index;
            lineComment = true;
            continue;
        }

        if (current == '/' && next == '*') {
            stripped.push_back(' ');
            stripped.push_back(' ');
            ++index;
            blockComment = true;
            continue;
        }

        if (current == '"') {
            stripped.push_back(' ');
            stringLiteral = true;
            escape = false;
            continue;
        }

        if (current == '\'') {
            stripped.push_back(' ');
            charLiteral = true;
            escape = false;
            continue;
        }

        stripped.push_back(current);
    }

    return stripped;
}

std::vector<std::string> ExtractDeclarations(const std::string& source) {
    static const std::regex declarationPattern(R"(\b(class|struct|interface|record)\s+([A-Za-z_][A-Za-z0-9_]*))");
    const std::string stripped = StripCommentsAndStrings(source);
    std::vector<std::string> declarations;
    for (std::sregex_iterator it(stripped.begin(), stripped.end(), declarationPattern), end; it != end; ++it) {
        const std::string name = (*it)[2].str();
        if (std::find(declarations.begin(), declarations.end(), name) == declarations.end()) {
            declarations.push_back(name);
        }
    }
    return declarations;
}

std::string XmlEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&apos;";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

std::string QuoteForShell(const std::filesystem::path& path) {
    std::string quoted = "\"";
    for (const char character : path.string()) {
        if (character == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('"');
    return quoted;
}

std::vector<ScriptSourceUnit> DiscoverScripts(const std::filesystem::path& projectRoot,
                                              const std::filesystem::path& assetRoot,
                                              ScriptCompileResult& result) {
    std::vector<ScriptSourceUnit> sources;
    std::vector<std::filesystem::path> roots;
    roots.push_back(assetRoot / "Scripts");
    roots.push_back(projectRoot / "Scripts");

    std::vector<std::string> visitedRoots;
    for (const std::filesystem::path& root : roots) {
        const std::filesystem::path absoluteRoot = std::filesystem::absolute(root).lexically_normal();
        const std::string rootKey = LowerPathString(absoluteRoot);
        if (std::find(visitedRoots.begin(), visitedRoots.end(), rootKey) != visitedRoots.end()) {
            continue;
        }
        visitedRoots.push_back(rootKey);

        std::error_code error;
        if (!std::filesystem::exists(absoluteRoot, error) || !std::filesystem::is_directory(absoluteRoot, error)) {
            continue;
        }

        std::filesystem::recursive_directory_iterator it(
            absoluteRoot, std::filesystem::directory_options::skip_permission_denied, error);
        std::filesystem::recursive_directory_iterator end;
        while (!error && it != end) {
            const std::filesystem::directory_entry entry = *it;
            if (entry.is_regular_file(error) && IsScriptFileExtension(entry.path().extension().string())) {
                ScriptSourceUnit unit;
                unit.absolutePath = std::filesystem::absolute(entry.path()).lexically_normal();
                unit.relativePath = RelativeToProjectString(projectRoot, unit.absolutePath);
                unit.editorOnly = HasEditorPathSegment(RelativeToProject(projectRoot, unit.absolutePath));

                std::ifstream input(unit.absolutePath);
                if (!input) {
                    AddDiagnostic(result, ScriptDiagnosticSeverity::Error,
                                  "Script file could not be read.", unit.relativePath);
                } else {
                    std::stringstream buffer;
                    buffer << input.rdbuf();
                    unit.declarations = ExtractDeclarations(buffer.str());
                    if (!unit.editorOnly && unit.declarations.empty()) {
                        AddDiagnostic(result, ScriptDiagnosticSeverity::Error,
                                      "Runtime script has no class, struct, interface, or record declaration.",
                                      unit.relativePath);
                    }
                }
                sources.push_back(std::move(unit));
            }
            it.increment(error);
        }
        if (error) {
            AddDiagnostic(result, ScriptDiagnosticSeverity::Warning,
                          "Script discovery skipped part of " + absoluteRoot.string() + ": " + error.message());
        }
    }

    std::sort(sources.begin(), sources.end(), [](const ScriptSourceUnit& left, const ScriptSourceUnit& right) {
        return ToLower(left.relativePath) < ToLower(right.relativePath);
    });
    return sources;
}

std::string RuntimeApiSource() {
    return R"CS(// Generated by AI Native Editor. Do not edit.
using System;

namespace AINative.Runtime
{
    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
    public sealed class ScriptFieldAttribute : Attribute
    {
        public string DisplayName { get; }

        public ScriptFieldAttribute(string displayName = "")
        {
            DisplayName = displayName;
        }
    }

    public abstract class ScriptBehaviour
    {
        public Entity Entity { get; internal set; } = Entity.Empty;

        public virtual void OnCreate() {}
        public virtual void OnStart() {}
        public virtual void OnUpdate(float deltaSeconds) {}
        public virtual void OnDestroy() {}
        public virtual void OnTriggerEnter(Entity other) {}
        public virtual void OnTriggerExit(Entity other) {}
    }

    public readonly struct Entity
    {
        public static readonly Entity Empty = new Entity(0, string.Empty);

        public int Id { get; }
        public string Name { get; }

        public Entity(int id, string name)
        {
            Id = id;
            Name = name ?? string.Empty;
        }
    }

    public readonly struct Vec2
    {
        public float X { get; }
        public float Y { get; }

        public Vec2(float x, float y)
        {
            X = x;
            Y = y;
        }
    }

    public readonly struct Vec3
    {
        public float X { get; }
        public float Y { get; }
        public float Z { get; }

        public Vec3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }
    }
}
)CS";
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& content, ScriptCompileResult& result,
                   const std::string& label) {
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path);
        output << content;
        return true;
    } catch (const std::exception& exception) {
        AddDiagnostic(result, ScriptDiagnosticSeverity::Error, label + " could not be written: " + exception.what(),
                      path.string());
        return false;
    }
}

std::string BuildProjectFileContent(const ScriptCompileOptions& options, const ScriptCompileResult& result,
                                    const std::vector<const ScriptSourceUnit*>& runtimeSources) {
    std::ostringstream project;
    project << "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
    project << "  <PropertyGroup>\n";
    project << "    <TargetFramework>netstandard2.1</TargetFramework>\n";
    project << "    <AssemblyName>" << XmlEscape(options.assemblyName) << "</AssemblyName>\n";
    project << "    <RootNamespace>AINative.GameScripts</RootNamespace>\n";
    project << "    <Nullable>enable</Nullable>\n";
    project << "    <ImplicitUsings>false</ImplicitUsings>\n";
    project << "    <LangVersion>latest</LangVersion>\n";
    project << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n";
    project << "    <OutputPath>bin/</OutputPath>\n";
    project << "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n";
    project << "  </PropertyGroup>\n";
    project << "  <ItemGroup>\n";
    project << "    <Compile Include=\"" << XmlEscape(result.apiStubPath.generic_string()) << "\" />\n";
    for (const ScriptSourceUnit* source : runtimeSources) {
        project << "    <Compile Include=\"" << XmlEscape(source->absolutePath.generic_string()) << "\" Link=\""
                << XmlEscape(source->relativePath) << "\" />\n";
    }
    project << "  </ItemGroup>\n";
    project << "</Project>\n";
    return project.str();
}

std::vector<std::string> ReadLines(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool TryParseCompilerDiagnostic(const std::string& line, ScriptDiagnostic* outDiagnostic) {
    const size_t errorPos = line.find(": error ");
    const size_t warningPos = line.find(": warning ");
    const size_t markerPos = errorPos != std::string::npos ? errorPos : warningPos;
    if (markerPos == std::string::npos) {
        return false;
    }

    ScriptDiagnostic diagnostic;
    diagnostic.severity = errorPos != std::string::npos ? ScriptDiagnosticSeverity::Error : ScriptDiagnosticSeverity::Warning;
    diagnostic.path = line.substr(0, markerPos);
    const size_t codeStart = markerPos + (diagnostic.severity == ScriptDiagnosticSeverity::Error ? 8 : 10);
    const size_t codeEnd = line.find(':', codeStart);
    if (codeEnd != std::string::npos) {
        diagnostic.code = line.substr(codeStart, codeEnd - codeStart);
        diagnostic.message = line.substr(codeEnd + 1);
        if (!diagnostic.message.empty() && diagnostic.message.front() == ' ') {
            diagnostic.message.erase(diagnostic.message.begin());
        }
    } else {
        diagnostic.message = line.substr(codeStart);
    }

    static const std::regex locationPattern(R"(^(.+)\((\d+),(\d+)\)$)");
    std::smatch match;
    if (std::regex_match(diagnostic.path, match, locationPattern)) {
        diagnostic.path = match[1].str();
        diagnostic.line = std::stoi(match[2].str());
        diagnostic.column = std::stoi(match[3].str());
    }

    *outDiagnostic = std::move(diagnostic);
    return true;
}

void AppendCompilerOutputDiagnostics(ScriptCompileResult& result) {
    const std::vector<std::string> lines = ReadLines(result.compilerLogPath);
    bool parsedDiagnostic = false;
    for (const std::string& line : lines) {
        ScriptDiagnostic diagnostic;
        if (TryParseCompilerDiagnostic(line, &diagnostic)) {
            if (diagnostic.severity == ScriptDiagnosticSeverity::Error) {
                result.ok = false;
            }
            result.diagnostics.push_back(std::move(diagnostic));
            parsedDiagnostic = true;
        }
    }

    if (!result.ok && !parsedDiagnostic) {
        size_t added = 0;
        for (const std::string& line : lines) {
            if (line.empty()) {
                continue;
            }
            AddDiagnostic(result, ScriptDiagnosticSeverity::Error, "Compiler output: " + line);
            if (++added >= 8) {
                break;
            }
        }
        if (added == 0) {
            AddDiagnostic(result, ScriptDiagnosticSeverity::Error,
                          "Compiler exited without readable diagnostics. See " + result.compilerLogPath.string() + ".");
        }
    }
}

json DiagnosticToJson(const ScriptDiagnostic& diagnostic) {
    json value{{"severity", ScriptDiagnosticSeverityLabel(diagnostic.severity)},
               {"message", diagnostic.message},
               {"path", diagnostic.path}};
    if (diagnostic.line > 0) {
        value["line"] = diagnostic.line;
    }
    if (diagnostic.column > 0) {
        value["column"] = diagnostic.column;
    }
    if (!diagnostic.code.empty()) {
        value["code"] = diagnostic.code;
    }
    return value;
}

bool WriteManifest(const ScriptCompileOptions& options, ScriptCompileResult& result) {
    json sources = json::array();
    for (const ScriptSourceUnit& source : result.sources) {
        sources.push_back({{"path", source.relativePath},
                           {"editorOnly", source.editorOnly},
                           {"declarations", source.declarations}});
    }

    json diagnostics = json::array();
    for (const ScriptDiagnostic& diagnostic : result.diagnostics) {
        diagnostics.push_back(DiagnosticToJson(diagnostic));
    }

    json manifest{{"format", "aine.scriptAssembly"},
                  {"version", 1},
                  {"assemblyName", result.assemblyName},
                  {"ok", result.ok},
                  {"compiler", result.compilerName},
                  {"compilerRan", result.compilerRan},
                  {"compilerExitCode", result.compilerExitCode},
                  {"projectRoot", options.projectRoot.generic_string()},
                  {"projectFile", result.projectFilePath.generic_string()},
                  {"apiStub", result.apiStubPath.generic_string()},
                  {"assemblyPath", result.assemblyPath.generic_string()},
                  {"compilerLog", result.compilerLogPath.generic_string()},
                  {"sources", sources},
                  {"diagnostics", diagnostics}};

    return WriteTextFile(result.manifestPath, manifest.dump(2) + "\n", result, "Script compile manifest");
}

} // namespace

const char* ScriptDiagnosticSeverityLabel(ScriptDiagnosticSeverity severity) {
    switch (severity) {
    case ScriptDiagnosticSeverity::Error:
        return "Error";
    case ScriptDiagnosticSeverity::Warning:
        return "Warning";
    case ScriptDiagnosticSeverity::Info:
    default:
        return "Info";
    }
}

ScriptCompileResult CompileProjectScripts(const ScriptCompileOptions& options) {
    ScriptCompileResult result;
    result.assemblyName = options.assemblyName.empty() ? result.assemblyName : options.assemblyName;

    const std::filesystem::path projectRoot = std::filesystem::absolute(options.projectRoot).lexically_normal();
    const std::filesystem::path assetRoot =
        options.assetRoot.empty() ? projectRoot / "Assets" : std::filesystem::absolute(options.assetRoot).lexically_normal();
    const std::filesystem::path outputRoot =
        options.outputRoot.empty() ? projectRoot / "Library" / "Scripts"
                                   : std::filesystem::absolute(options.outputRoot).lexically_normal();

    result.manifestPath = outputRoot / kManifestFileName;
    result.projectFilePath = outputRoot / kProjectFileName;
    result.apiStubPath = outputRoot / "Generated" / kRuntimeApiFileName;
    result.assemblyPath = outputRoot / "bin" / (result.assemblyName + ".dll");
    result.compilerLogPath = outputRoot / kCompilerLogFileName;

    if (projectRoot.empty() || !std::filesystem::exists(projectRoot)) {
        AddDiagnostic(result, ScriptDiagnosticSeverity::Error, "Project root is missing: " + projectRoot.string());
        WriteManifest(options, result);
        return result;
    }
    if (!IsPathInside(projectRoot, assetRoot)) {
        AddDiagnostic(result, ScriptDiagnosticSeverity::Error, "Asset root must stay inside the project: " + assetRoot.string());
        WriteManifest(options, result);
        return result;
    }
    if (!IsPathInside(projectRoot, outputRoot)) {
        AddDiagnostic(result, ScriptDiagnosticSeverity::Error,
                      "Script compiler output must stay inside the project: " + outputRoot.string());
        WriteManifest(options, result);
        return result;
    }

    result.sources = DiscoverScripts(projectRoot, assetRoot, result);
    std::vector<const ScriptSourceUnit*> runtimeSources;
    for (const ScriptSourceUnit& source : result.sources) {
        if (!source.editorOnly) {
            runtimeSources.push_back(&source);
        }
    }

    if (runtimeSources.empty()) {
        AddDiagnostic(result, ScriptDiagnosticSeverity::Info, "script.compile passed: no runtime scripts found.");
        WriteManifest(options, result);
        return result;
    }

    if (!result.ok) {
        WriteManifest(options, result);
        return result;
    }

    if (!WriteTextFile(result.apiStubPath, RuntimeApiSource(), result, "Generated scripting API") ||
        !WriteTextFile(result.projectFilePath, BuildProjectFileContent(options, result, runtimeSources), result,
                       "Generated script project")) {
        WriteManifest(options, result);
        return result;
    }

    if (!options.runExternalCompiler) {
        AddDiagnostic(result, ScriptDiagnosticSeverity::Info,
                      "script.compile prepared: external compiler was not run for " +
                          std::to_string(runtimeSources.size()) + " runtime script(s).");
        WriteManifest(options, result);
        return result;
    }

    result.compilerRan = true;
    std::filesystem::remove(result.assemblyPath);
    std::ostringstream command;
    command << "dotnet build " << QuoteForShell(result.projectFilePath)
            << " --nologo --verbosity:minimal /p:UseSharedCompilation=false > "
            << QuoteForShell(result.compilerLogPath) << " 2>&1";
    result.compilerExitCode = std::system(command.str().c_str());
    if (result.compilerExitCode != 0) {
        result.ok = false;
    }

    AppendCompilerOutputDiagnostics(result);

    if (result.ok && !std::filesystem::exists(result.assemblyPath)) {
        AddDiagnostic(result, ScriptDiagnosticSeverity::Error,
                      "Compiler completed but script assembly is missing: " + result.assemblyPath.string());
    }

    if (result.ok) {
        AddDiagnostic(result, ScriptDiagnosticSeverity::Info,
                      "script.compile passed: " + std::to_string(runtimeSources.size()) +
                          " runtime script(s) compiled to " + result.assemblyPath.string() + ".");
    }

    WriteManifest(options, result);
    return result;
}

std::vector<std::string> FormatScriptCompileDiagnostics(const ScriptCompileResult& result) {
    std::vector<std::string> formatted;
    for (const ScriptDiagnostic& diagnostic : result.diagnostics) {
        std::ostringstream stream;
        stream << "script.compile " << ScriptDiagnosticSeverityLabel(diagnostic.severity) << ": ";
        if (!diagnostic.path.empty()) {
            stream << diagnostic.path;
            if (diagnostic.line > 0) {
                stream << "(" << diagnostic.line;
                if (diagnostic.column > 0) {
                    stream << "," << diagnostic.column;
                }
                stream << ")";
            }
            stream << ": ";
        }
        if (!diagnostic.code.empty()) {
            stream << diagnostic.code << ": ";
        }
        stream << diagnostic.message;
        formatted.push_back(stream.str());
    }
    if (formatted.empty()) {
        formatted.push_back(std::string("script.compile ") + (result.ok ? "passed." : "failed."));
    }
    return formatted;
}

} // namespace aine

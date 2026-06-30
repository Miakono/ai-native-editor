#include "engine/scripting/ScriptRuntimeHost.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>

namespace aine {
namespace {

using json = nlohmann::json;

constexpr const char* kHostProjectName = "AINative.ScriptRuntimeHost.csproj";
constexpr const char* kHostProgramName = "Program.cs";
constexpr const char* kHostAssemblyName = "AINative.ScriptRuntimeHost";

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
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

bool ComponentBoolProperty(const Component& component, const std::string& propertyName, bool fallback) {
    for (const ComponentProperty& property : component.properties) {
        if (property.name != propertyName) {
            continue;
        }

        std::string value = ToLower(property.value);
        if (value == "true" || value == "1" || value == "yes") {
            return true;
        }
        if (value == "false" || value == "0" || value == "no") {
            return false;
        }
        return fallback;
    }
    return fallback;
}

bool ComponentHasScriptPath(const Component& component) {
    return std::any_of(component.properties.begin(), component.properties.end(), [](const ComponentProperty& property) {
        return property.name == "scriptPath" && !property.value.empty();
    });
}

bool ComponentEnabled(const Component& component) {
    return ComponentBoolProperty(component, "enabled", true);
}

void SetComponentProperty(Component& component, const std::string& propertyName, std::string value) {
    for (ComponentProperty& property : component.properties) {
        if (property.name == propertyName) {
            property.value = std::move(value);
            return;
        }
    }
    component.properties.push_back({propertyName, std::move(value)});
}

json EntityToJson(const Entity& entity) {
    json components = json::array();
    for (const Component& component : entity.components) {
        json properties = json::object();
        for (const ComponentProperty& property : component.properties) {
            properties[property.name] = property.value;
        }
        components.push_back({{"type", component.type}, {"properties", properties}});
    }

    return json{{"id", entity.id},
                {"parentId", entity.parentId},
                {"name", entity.name},
                {"activeSelf", entity.activeSelf},
                {"tag", entity.tag},
                {"layer", entity.layer},
                {"position", entity.position},
                {"rotation", entity.rotation},
                {"scale", entity.scale},
                {"components", components}};
}

json SceneToJson(const std::vector<Entity>& entities) {
    json scene;
    scene["entities"] = json::array();
    for (const Entity& entity : entities) {
        scene["entities"].push_back(EntityToJson(entity));
    }
    return scene;
}

bool WriteTextFileIfChanged(const std::filesystem::path& path, const std::string& content, std::string* error) {
    try {
        std::filesystem::create_directories(path.parent_path());
        {
            std::ifstream existing(path, std::ios::binary);
            if (existing) {
                std::ostringstream buffer;
                buffer << existing.rdbuf();
                if (buffer.str() == content) {
                    return true;
                }
            }
        }
        std::ofstream output(path, std::ios::binary);
        output << content;
        return static_cast<bool>(output);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

std::string ReadTextFile(const std::filesystem::path& path, size_t maxBytes = 4096) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::string text;
    text.resize(maxBytes);
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(input.gcount()));
    return text;
}

void AddDiagnostic(ScriptRuntimeHostResult& result, LogLevel level, std::string message, int entityId = 0,
                   std::string entityName = {}, std::string scriptType = {}) {
    if (level == LogLevel::Error) {
        result.ok = false;
    }
    result.diagnostics.push_back({level, entityId, std::move(entityName), std::move(scriptType), std::move(message)});
}

std::string HostProjectSource() {
    return R"XML(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <AssemblyName>AINative.ScriptRuntimeHost</AssemblyName>
    <Nullable>disable</Nullable>
    <ImplicitUsings>false</ImplicitUsings>
    <LangVersion>latest</LangVersion>
    <OutputPath>bin/</OutputPath>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
  </PropertyGroup>
</Project>
)XML";
}

std::string HostProgramSource() {
    return R"CS(using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;

internal static class Program
{
    private sealed class RuntimeResult
    {
        public bool Ok = true;
        public bool HostReady = true;
        public string Phase = "";
        public int InvokedCount = 0;
        public readonly List<Dictionary<string, object>> Diagnostics = new();
        public readonly List<Dictionary<string, object>> FieldUpdates = new();

        public void AddDiagnostic(string severity, string message, int entityId = 0, string entityName = "", string scriptType = "")
        {
            if (severity == "Error")
            {
                Ok = false;
            }
            Diagnostics.Add(new Dictionary<string, object>
            {
                ["severity"] = severity,
                ["message"] = message,
                ["entityId"] = entityId,
                ["entityName"] = entityName,
                ["scriptType"] = scriptType,
            });
        }
    }

    public static int Main(string[] args)
    {
        if (args.Length < 5)
        {
            Console.Error.WriteLine("Usage: <script-assembly> <phase> <delta-seconds> <scene-json> <result-json>");
            return 2;
        }

        var result = new RuntimeResult { Phase = args[1] };
        try
        {
            string assemblyPath = Path.GetFullPath(args[0]);
            string phase = args[1];
            float deltaSeconds = ParseFloat(args[2], 0.0f);
            string scenePath = Path.GetFullPath(args[3]);
            string resultPath = Path.GetFullPath(args[4]);

            if (!File.Exists(assemblyPath))
            {
                result.HostReady = false;
                result.AddDiagnostic("Error", "Script assembly is missing: " + assemblyPath);
                WriteResult(resultPath, result);
                return 0;
            }
            if (!File.Exists(scenePath))
            {
                result.HostReady = false;
                result.AddDiagnostic("Error", "Script host scene input is missing: " + scenePath);
                WriteResult(resultPath, result);
                return 0;
            }

            Assembly assembly = Assembly.LoadFrom(assemblyPath);
            JsonObject scene = JsonNode.Parse(File.ReadAllText(scenePath))?.AsObject() ?? new JsonObject();
            JsonArray entities = scene["entities"] as JsonArray ?? new JsonArray();

            foreach (JsonNode entityNode in entities)
            {
                if (entityNode is not JsonObject entity)
                {
                    continue;
                }

                int entityId = IntValue(entity["id"]);
                string entityName = StringValue(entity["name"]);
                JsonArray components = entity["components"] as JsonArray ?? new JsonArray();
                foreach (JsonNode componentNode in components)
                {
                    if (componentNode is not JsonObject component)
                    {
                        continue;
                    }

                    string componentType = StringValue(component["type"]);
                    Dictionary<string, string> properties = ReadProperties(component);
                    if (!properties.TryGetValue("scriptPath", out string scriptPath) || string.IsNullOrWhiteSpace(scriptPath))
                    {
                        continue;
                    }
                    if (properties.TryGetValue("enabled", out string enabled) && IsFalse(enabled))
                    {
                        continue;
                    }

                    Type scriptType = ResolveScriptType(assembly, componentType);
                    if (scriptType == null)
                    {
                        result.AddDiagnostic("Error", "Script type was not found in assembly.", entityId, entityName, componentType);
                        continue;
                    }

                    object instance;
                    try
                    {
                        instance = Activator.CreateInstance(scriptType);
                    }
                    catch (Exception ex)
                    {
                        result.AddDiagnostic("Error", "Script instance could not be created: " + CleanException(ex), entityId, entityName, componentType);
                        continue;
                    }

                    TryAssignEntity(instance, scriptType, assembly, entityId, entityName, result, componentType);
                    ApplyExposedFields(instance, scriptType, properties, result, entityId, entityName, componentType);

                    if (phase == "begin")
                    {
                        InvokeLifecycle(instance, scriptType, "OnCreate", Array.Empty<object>(), result, entityId, entityName, componentType);
                        InvokeLifecycle(instance, scriptType, "OnStart", Array.Empty<object>(), result, entityId, entityName, componentType);
                    }
                    else if (phase == "update")
                    {
                        InvokeLifecycle(instance, scriptType, "OnUpdate", new object[] { deltaSeconds }, result, entityId, entityName, componentType);
                    }
                    else if (phase == "destroy")
                    {
                        InvokeLifecycle(instance, scriptType, "OnDestroy", Array.Empty<object>(), result, entityId, entityName, componentType);
                    }
                    else
                    {
                        result.AddDiagnostic("Error", "Unsupported script runtime phase: " + phase, entityId, entityName, componentType);
                        continue;
                    }

                    CaptureExposedFields(instance, scriptType, result, entityId, componentType);
                    result.InvokedCount++;
                }
            }

            WriteResult(args[4], result);
            return 0;
        }
        catch (Exception ex)
        {
            result.HostReady = false;
            result.AddDiagnostic("Error", "Script runtime host failed: " + CleanException(ex));
            try
            {
                WriteResult(args.Length >= 5 ? args[4] : "script-runtime-result.json", result);
            }
            catch
            {
                Console.Error.WriteLine(CleanException(ex));
            }
            return 1;
        }
    }

    private static Type ResolveScriptType(Assembly assembly, string componentType)
    {
        return assembly.GetTypes().FirstOrDefault(type =>
            string.Equals(type.FullName, componentType, StringComparison.Ordinal) ||
            string.Equals(type.Name, componentType, StringComparison.Ordinal));
    }

    private static void TryAssignEntity(object instance, Type scriptType, Assembly assembly, int entityId, string entityName,
                                        RuntimeResult result, string componentType)
    {
        PropertyInfo property = scriptType.GetProperty("Entity", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        if (property == null)
        {
            return;
        }
        Type entityType = assembly.GetType("AINative.Runtime.Entity");
        if (entityType == null)
        {
            return;
        }
        try
        {
            object runtimeEntity = Activator.CreateInstance(entityType, entityId, entityName);
            MethodInfo setter = property.GetSetMethod(true);
            setter?.Invoke(instance, new[] { runtimeEntity });
        }
        catch (Exception ex)
        {
            result.AddDiagnostic("Warning", "Entity binding failed: " + CleanException(ex), entityId, entityName, componentType);
        }
    }

    private static void InvokeLifecycle(object instance, Type scriptType, string methodName, object[] args, RuntimeResult result,
                                        int entityId, string entityName, string componentType)
    {
        MethodInfo method = scriptType.GetMethod(methodName, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        if (method == null)
        {
            return;
        }

        ParameterInfo[] parameters = method.GetParameters();
        object[] invokeArgs = parameters.Length == 0 ? Array.Empty<object>() : args;
        if (parameters.Length != invokeArgs.Length)
        {
            result.AddDiagnostic("Warning", methodName + " has an unsupported signature.", entityId, entityName, componentType);
            return;
        }

        try
        {
            method.Invoke(instance, invokeArgs);
        }
        catch (TargetInvocationException ex)
        {
            result.AddDiagnostic("Error", methodName + " threw: " + CleanException(ex.InnerException ?? ex), entityId, entityName, componentType);
        }
        catch (Exception ex)
        {
            result.AddDiagnostic("Error", methodName + " failed: " + CleanException(ex), entityId, entityName, componentType);
        }
    }

    private static void ApplyExposedFields(object instance, Type scriptType, Dictionary<string, string> properties,
                                           RuntimeResult result, int entityId, string entityName, string componentType)
    {
        foreach (FieldInfo field in scriptType.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
        {
            if (!IsScriptField(field, out string displayName))
            {
                continue;
            }
            string value = FindProperty(properties, field.Name, displayName);
            if (value == null)
            {
                continue;
            }
            try
            {
                field.SetValue(instance, ConvertFromString(value, field.FieldType));
            }
            catch (Exception ex)
            {
                result.AddDiagnostic("Warning", "Could not apply script field " + field.Name + ": " + CleanException(ex), entityId, entityName, componentType);
            }
        }

        foreach (PropertyInfo property in scriptType.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
        {
            if (!property.CanWrite || property.GetIndexParameters().Length > 0 || !IsScriptField(property, out string displayName))
            {
                continue;
            }
            string value = FindProperty(properties, property.Name, displayName);
            if (value == null)
            {
                continue;
            }
            try
            {
                property.SetValue(instance, ConvertFromString(value, property.PropertyType));
            }
            catch (Exception ex)
            {
                result.AddDiagnostic("Warning", "Could not apply script property " + property.Name + ": " + CleanException(ex), entityId, entityName, componentType);
            }
        }
    }

    private static void CaptureExposedFields(object instance, Type scriptType, RuntimeResult result, int entityId, string componentType)
    {
        foreach (FieldInfo field in scriptType.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
        {
            if (!IsScriptField(field, out _))
            {
                continue;
            }
            object value = field.GetValue(instance);
            result.FieldUpdates.Add(new Dictionary<string, object>
            {
                ["entityId"] = entityId,
                ["componentType"] = componentType,
                ["name"] = field.Name,
                ["value"] = ConvertToString(value),
            });
        }

        foreach (PropertyInfo property in scriptType.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
        {
            if (!property.CanRead || property.GetIndexParameters().Length > 0 || !IsScriptField(property, out _))
            {
                continue;
            }
            object value = property.GetValue(instance);
            result.FieldUpdates.Add(new Dictionary<string, object>
            {
                ["entityId"] = entityId,
                ["componentType"] = componentType,
                ["name"] = property.Name,
                ["value"] = ConvertToString(value),
            });
        }
    }

    private static bool IsScriptField(MemberInfo member, out string displayName)
    {
        displayName = "";
        foreach (CustomAttributeData attribute in member.CustomAttributes)
        {
            if (attribute.AttributeType.FullName == "AINative.Runtime.ScriptFieldAttribute")
            {
                if (attribute.ConstructorArguments.Count > 0 && attribute.ConstructorArguments[0].Value is string label)
                {
                    displayName = label;
                }
                return true;
            }
        }
        return false;
    }

    private static object ConvertFromString(string value, Type targetType)
    {
        Type type = Nullable.GetUnderlyingType(targetType) ?? targetType;
        if (type == typeof(string))
        {
            return value ?? "";
        }
        if (type == typeof(int))
        {
            return int.Parse(value, CultureInfo.InvariantCulture);
        }
        if (type == typeof(float))
        {
            return float.Parse(value, CultureInfo.InvariantCulture);
        }
        if (type == typeof(double))
        {
            return double.Parse(value, CultureInfo.InvariantCulture);
        }
        if (type == typeof(bool))
        {
            return value.Equals("true", StringComparison.OrdinalIgnoreCase) || value == "1" || value.Equals("yes", StringComparison.OrdinalIgnoreCase);
        }
        if (type.IsEnum)
        {
            return Enum.Parse(type, value, ignoreCase: true);
        }
        return Convert.ChangeType(value, type, CultureInfo.InvariantCulture);
    }

    private static string ConvertToString(object value)
    {
        return value switch
        {
            null => "",
            IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture),
            _ => value.ToString() ?? "",
        };
    }

    private static Dictionary<string, string> ReadProperties(JsonObject component)
    {
        var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (component["properties"] is not JsonObject properties)
        {
            return result;
        }
        foreach (KeyValuePair<string, JsonNode> entry in properties)
        {
            result[entry.Key] = StringValue(entry.Value);
        }
        return result;
    }

    private static string FindProperty(Dictionary<string, string> properties, string memberName, string displayName)
    {
        if (properties.TryGetValue(memberName, out string value))
        {
            return value;
        }
        if (!string.IsNullOrWhiteSpace(displayName) && properties.TryGetValue(displayName, out value))
        {
            return value;
        }
        return null;
    }

    private static bool IsFalse(string value)
    {
        return value.Equals("false", StringComparison.OrdinalIgnoreCase) || value == "0" || value.Equals("no", StringComparison.OrdinalIgnoreCase);
    }

    private static int IntValue(JsonNode node)
    {
        if (node == null)
        {
            return 0;
        }
        try
        {
            return node.GetValue<int>();
        }
        catch
        {
            int.TryParse(StringValue(node), NumberStyles.Integer, CultureInfo.InvariantCulture, out int value);
            return value;
        }
    }

    private static float ParseFloat(string value, float fallback)
    {
        return float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed) ? parsed : fallback;
    }

    private static string StringValue(JsonNode node)
    {
        if (node == null)
        {
            return "";
        }
        try
        {
            return node.GetValue<string>() ?? "";
        }
        catch
        {
            return node.ToJsonString();
        }
    }

    private static string CleanException(Exception ex)
    {
        return ex.GetType().Name + ": " + ex.Message;
    }

    private static void WriteResult(string resultPath, RuntimeResult result)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(resultPath)) ?? ".");
        var payload = new Dictionary<string, object>
        {
            ["ok"] = result.Ok,
            ["hostReady"] = result.HostReady,
            ["phase"] = result.Phase,
            ["invokedCount"] = result.InvokedCount,
            ["diagnostics"] = result.Diagnostics,
            ["fieldUpdates"] = result.FieldUpdates,
        };
        File.WriteAllText(resultPath, JsonSerializer.Serialize(payload, new JsonSerializerOptions { WriteIndented = true }));
    }
}
)CS";
}

LogLevel ParseLogLevel(const std::string& severity) {
    if (severity == "Error") {
        return LogLevel::Error;
    }
    if (severity == "Warning") {
        return LogLevel::Warning;
    }
    return LogLevel::Info;
}

void ApplyFieldUpdates(std::vector<Entity>& entities, const std::vector<ScriptRuntimeFieldUpdate>& updates) {
    for (const ScriptRuntimeFieldUpdate& update : updates) {
        auto entityIt = std::find_if(entities.begin(), entities.end(), [&update](const Entity& entity) {
            return entity.id == update.entityId;
        });
        if (entityIt == entities.end()) {
            continue;
        }
        auto componentIt =
            std::find_if(entityIt->components.begin(), entityIt->components.end(), [&update](const Component& component) {
                return component.type == update.componentType;
            });
        if (componentIt == entityIt->components.end()) {
            continue;
        }
        SetComponentProperty(*componentIt, update.name, update.value);
    }
}

std::filesystem::path HostAssemblyPath(const std::filesystem::path& outputRoot) {
    return outputRoot / "bin" / (std::string(kHostAssemblyName) + ".dll");
}

} // namespace

bool SceneHasEnabledScriptComponents(const std::vector<Entity>& entities) {
    for (const Entity& entity : entities) {
        if (!entity.activeSelf) {
            continue;
        }
        for (const Component& component : entity.components) {
            if (ComponentHasScriptPath(component) && ComponentEnabled(component)) {
                return true;
            }
        }
    }
    return false;
}

void ScriptRuntimeHost::Reset() {
    active_ = false;
    config_ = {};
}

ScriptRuntimeHostResult ScriptRuntimeHost::Begin(std::vector<Entity>& entities, const ScriptRuntimeHostConfig& config) {
    Reset();

    ScriptRuntimeHostResult result;
    result.phase = "begin";
    if (!config.enabled || !SceneHasEnabledScriptComponents(entities)) {
        result.hostReady = true;
        return result;
    }

    config_ = config;
    if (config_.outputRoot.empty()) {
        config_.outputRoot = config_.projectRoot / "Library" / "Scripts" / "RuntimeHost";
    }

    if (config_.assemblyPath.empty() || !std::filesystem::exists(config_.assemblyPath)) {
        result.hostReady = false;
        AddDiagnostic(result, LogLevel::Error, "Script assembly is missing: " + config_.assemblyPath.string());
        return result;
    }

    result = Invoke(entities, "begin", 0.0f);
    active_ = result.hostReady;
    return result;
}

ScriptRuntimeHostResult ScriptRuntimeHost::Update(std::vector<Entity>& entities, float deltaSeconds) {
    if (!active_) {
        ScriptRuntimeHostResult result;
        result.phase = "update";
        return result;
    }
    return Invoke(entities, "update", deltaSeconds);
}

ScriptRuntimeHostResult ScriptRuntimeHost::Destroy(std::vector<Entity>& entities) {
    if (!active_) {
        ScriptRuntimeHostResult result;
        result.phase = "destroy";
        return result;
    }
    ScriptRuntimeHostResult result = Invoke(entities, "destroy", 0.0f);
    Reset();
    return result;
}

ScriptRuntimeHostResult ScriptRuntimeHost::Invoke(std::vector<Entity>& entities, std::string phase, float deltaSeconds) {
    ScriptRuntimeHostResult result;
    result.phase = phase;
    result.hostProjectPath = config_.outputRoot / kHostProjectName;
    result.hostAssemblyPath = HostAssemblyPath(config_.outputRoot);
    result.sceneInputPath = config_.outputRoot / ("scene_" + phase + ".json");
    result.resultPath = config_.outputRoot / ("result_" + phase + ".json");
    result.invokeLogPath = config_.outputRoot / ("invoke_" + phase + ".log");

    if (!EnsureHostBuilt(result)) {
        return result;
    }

    std::string error;
    if (!WriteTextFileIfChanged(result.sceneInputPath, SceneToJson(entities).dump(2) + "\n", &error)) {
        result.hostReady = false;
        AddDiagnostic(result, LogLevel::Error, "Script runtime scene input could not be written: " + error);
        return result;
    }

    std::filesystem::remove(result.resultPath);
    std::ostringstream delta;
    delta << std::setprecision(8) << deltaSeconds;

    std::ostringstream command;
    command << "dotnet " << QuoteForShell(result.hostAssemblyPath) << ' ' << QuoteForShell(config_.assemblyPath) << ' '
            << phase << ' ' << delta.str() << ' ' << QuoteForShell(result.sceneInputPath) << ' '
            << QuoteForShell(result.resultPath) << " > " << QuoteForShell(result.invokeLogPath) << " 2>&1";

    result.hostRan = true;
    result.exitCode = std::system(command.str().c_str());
    if (!std::filesystem::exists(result.resultPath)) {
        result.hostReady = false;
        AddDiagnostic(result, LogLevel::Error,
                      "Script runtime host did not write a result file. Exit code " + std::to_string(result.exitCode) +
                          ". " + ReadTextFile(result.invokeLogPath));
        return result;
    }

    try {
        std::ifstream input(result.resultPath);
        const json payload = json::parse(input);
        result.ok = payload.value("ok", true);
        result.hostReady = payload.value("hostReady", true);
        result.invokedCount = payload.value("invokedCount", 0);

        for (const json& diagnostic : payload.value("diagnostics", json::array())) {
            ScriptRuntimeDiagnostic parsed;
            parsed.level = ParseLogLevel(diagnostic.value("severity", "Info"));
            parsed.entityId = diagnostic.value("entityId", 0);
            parsed.entityName = diagnostic.value("entityName", "");
            parsed.scriptType = diagnostic.value("scriptType", "");
            parsed.message = diagnostic.value("message", "");
            result.diagnostics.push_back(std::move(parsed));
        }

        for (const json& update : payload.value("fieldUpdates", json::array())) {
            result.fieldUpdates.push_back({update.value("entityId", 0),
                                           update.value("componentType", ""),
                                           update.value("name", ""),
                                           update.value("value", "")});
        }
        ApplyFieldUpdates(entities, result.fieldUpdates);
    } catch (const std::exception& exception) {
        result.hostReady = false;
        AddDiagnostic(result, LogLevel::Error,
                      "Script runtime host result could not be parsed: " + std::string(exception.what()));
    }

    return result;
}

bool ScriptRuntimeHost::EnsureHostBuilt(ScriptRuntimeHostResult& result) {
    result.hostProjectPath = config_.outputRoot / kHostProjectName;
    result.hostAssemblyPath = HostAssemblyPath(config_.outputRoot);

    std::string error;
    if (!WriteTextFileIfChanged(result.hostProjectPath, HostProjectSource(), &error) ||
        !WriteTextFileIfChanged(config_.outputRoot / kHostProgramName, HostProgramSource(), &error)) {
        result.hostReady = false;
        AddDiagnostic(result, LogLevel::Error, "Script runtime host source could not be written: " + error);
        return false;
    }

    if (std::filesystem::exists(result.hostAssemblyPath)) {
        return true;
    }

    const std::filesystem::path buildLog = config_.outputRoot / "build.log";
    std::ostringstream command;
    command << "dotnet build " << QuoteForShell(result.hostProjectPath)
            << " --nologo --verbosity:minimal /p:UseSharedCompilation=false > " << QuoteForShell(buildLog) << " 2>&1";
    result.hostRan = true;
    result.exitCode = std::system(command.str().c_str());

    if (result.exitCode != 0 || !std::filesystem::exists(result.hostAssemblyPath)) {
        result.hostReady = false;
        AddDiagnostic(result, LogLevel::Error,
                      "Script runtime host build failed. Exit code " + std::to_string(result.exitCode) + ". " +
                          ReadTextFile(buildLog));
        return false;
    }
    return true;
}

} // namespace aine

#include "EditorState.h"

#include "engine/scene/SceneSerialization.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace aine {
namespace {

using json = nlohmann::json;

constexpr const char* kProjectFileName = "AI Native Project.aineproject.json";
constexpr const char* kDefaultAssetRoot = "Assets";
constexpr const char* kDefaultSceneRoot = "Assets/Scenes";
constexpr const char* kDefaultGameSaveRoot = "Saved";

std::string HostBuildPlatformName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string TrimText(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
                    return !std::isspace(c);
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
                    return !std::isspace(c);
                }).base(),
                value.end());
    return value;
}

std::string ConversationTitleFromMessage(const std::string& message) {
    std::string title = TrimText(message);
    for (char& character : title) {
        if (character == '\n' || character == '\r' || character == '\t') {
            character = ' ';
        }
    }
    while (title.find("  ") != std::string::npos) {
        title.replace(title.find("  "), 2, " ");
    }
    if (title.empty()) {
        return "New chat";
    }
    constexpr size_t kMaxTitleLength = 54;
    if (title.size() > kMaxTitleLength) {
        title = title.substr(0, kMaxTitleLength - 3) + "...";
    }
    return title;
}

ChatConversation* FindConversationById(std::vector<ChatConversation>& conversations, const std::string& id) {
    auto it = std::find_if(conversations.begin(), conversations.end(), [&](const ChatConversation& conversation) {
        return conversation.id == id;
    });
    return it == conversations.end() ? nullptr : &(*it);
}

const ChatConversation* FindConversationById(const std::vector<ChatConversation>& conversations, const std::string& id) {
    auto it = std::find_if(conversations.begin(), conversations.end(), [&](const ChatConversation& conversation) {
        return conversation.id == id;
    });
    return it == conversations.end() ? nullptr : &(*it);
}

std::string NormalizeBuildTargetPlatform(std::string value) {
    const std::string lower = ToLower(std::move(value));
    if (lower == "linux") {
        return "Linux";
    }
    if (lower == "macos" || lower == "mac" || lower == "osx" || lower == "darwin") {
        return "macOS";
    }
    return "Windows";
}

std::string DefaultBuildOutputDirectoryForPlatform(const std::string& platform) {
    const std::string normalized = NormalizeBuildTargetPlatform(platform);
    if (normalized == "Linux") {
        return "Builds/Linux";
    }
    if (normalized == "macOS") {
        return "Builds/macOS";
    }
    return "Builds/Windows";
}

std::string PackageTypeForPlatform(const std::string& platform) {
    const std::string normalized = NormalizeBuildTargetPlatform(platform);
    if (normalized == "Linux") {
        return "LinuxPlayer";
    }
    if (normalized == "macOS") {
        return "macOSPlayer";
    }
    return "WindowsPlayer";
}

std::string LauncherFilenameForPlatform(const std::string& platform) {
    return NormalizeBuildTargetPlatform(platform) == "Windows" ? "run-game.bat" : "run-game.sh";
}

std::string HostLauncherSuffixForPlatform(const std::string& platform) {
    return NormalizeBuildTargetPlatform(platform) == "Windows" ? ".bat" : ".sh";
}

std::string EnsureExecutableFilenameForPlatform(const std::string& executableName, const std::string& platform) {
    std::filesystem::path filename = executableName;
    const std::string normalized = NormalizeBuildTargetPlatform(platform);
    if (normalized == "Windows") {
        if (filename.extension() != ".exe") {
            filename += ".exe";
        }
        return filename.filename().string();
    }
    filename.replace_extension();
    return filename.filename().string();
}

std::string BuildTargetUnavailableMessage(const std::string& platform, const std::filesystem::path& playerExecutable) {
    return "Target " + NormalizeBuildTargetPlatform(platform) + " unavailable: matching ai_native_player runtime was not found at " +
           playerExecutable.string() + ". Build or configure a target-specific player runtime before packaging this platform.";
}

std::string SanitizeIdentifier(std::string value, const std::string& fallback) {
    for (char& character : value) {
        const unsigned char c = static_cast<unsigned char>(character);
        if (!std::isalnum(c) && character != '_' && character != '-') {
            character = '_';
        }
    }
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
                    return c != '_';
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
                    return c != '_';
                }).base(),
                value.end());
    return value.empty() ? fallback : value;
}

std::string BuildIdentifierSegment(std::string value) {
    value = ToLower(SanitizeIdentifier(std::move(value), "ainativegame"));
    for (char& character : value) {
        if (character == '_') {
            character = '-';
        }
    }
    return value;
}

std::string AssetTypeForPath(const std::filesystem::path& path) {
    const std::string extension = ToLower(path.extension().string());
    if (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb") {
        return "Model";
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
        extension == ".bmp" || extension == ".exr" || extension == ".hdr") {
        return "Texture";
    }
    if (extension == ".wav" || extension == ".ogg") {
        return "Audio";
    }
    if (extension == ".mat" || extension == ".material") {
        return "Material";
    }
    return "Generic";
}

std::filesystem::path AssetSubfolderForType(const std::string& type) {
    if (type == "Model") {
        return "Imported/Models";
    }
    if (type == "Texture") {
        return "Imported/Textures";
    }
    if (type == "Audio") {
        return "Imported/Audio";
    }
    if (type == "Material") {
        return "Materials";
    }
    return "Imported/Other";
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

std::filesystem::path ProjectRelativeOrAbsolute(const std::filesystem::path& projectRoot,
                                                const std::filesystem::path& path) {
    if (path.is_absolute()) {
        return path;
    }
    return projectRoot / path;
}

json AssetRecordToJson(const AssetRecord& asset) {
    return json{
        {"id", asset.id},
        {"name", asset.name},
        {"type", asset.type},
        {"sourceLabel", asset.sourceLabel},
        {"sourcePath", asset.sourcePath},
        {"sourceHash", asset.sourceHash},
        {"importedPath", asset.importedPath},
        {"cookedPath", asset.cookedPath},
        {"resourcePath", asset.resourcePath},
        {"metadataPath", asset.metadataPath},
        {"thumbnailPath", asset.thumbnailPath},
        {"license", asset.license},
        {"importSettings", asset.importSettings},
        {"dependencies", asset.dependencies},
    };
}

AssetRecord AssetRecordFromJson(const json& value) {
    AssetRecord asset;
    asset.id = value.value("id", "");
    asset.name = value.value("name", "");
    asset.type = value.value("type", "Generic");
    asset.sourceLabel = value.value("sourceLabel", "");
    asset.sourcePath = value.value("sourcePath", "");
    asset.sourceHash = value.value("sourceHash", "");
    asset.importedPath = value.value("importedPath", "");
    asset.cookedPath = value.value("cookedPath", "");
    asset.resourcePath = value.value("resourcePath", "");
    asset.metadataPath = value.value("metadataPath", "");
    asset.thumbnailPath = value.value("thumbnailPath", "");
    asset.license = value.value("license", "");
    asset.importSettings = value.value("importSettings", "");
    if (value.contains("dependencies") && value.at("dependencies").is_array()) {
        for (const json& dependency : value.at("dependencies")) {
            if (dependency.is_string()) {
                asset.dependencies.push_back(dependency.get<std::string>());
            }
        }
    }
    return asset;
}

json PluginRecordToJson(const PluginRecord& plugin) {
    return json{{"name", plugin.name},
                {"path", plugin.path},
                {"manifestPath", plugin.manifestPath},
                {"installPath", plugin.installPath},
                {"enabled", plugin.enabled},
                {"installed", plugin.installed}};
}

PluginRecord PluginRecordFromJson(const json& value) {
    PluginRecord plugin;
    plugin.name = value.value("name", "");
    plugin.path = value.value("path", "");
    plugin.manifestPath = value.value("manifestPath", "");
    plugin.installPath = value.value("installPath", "");
    plugin.enabled = value.value("enabled", false);
    plugin.installed = value.value("installed", false);
    return plugin;
}

json MultiplayerSettingsToJson(const MultiplayerSettings& settings) {
    return json{
        {"enabled", settings.enabled},
        {"topology", settings.topology},
        {"transport", settings.transport},
        {"bindAddress", settings.bindAddress},
        {"port", settings.port},
        {"maxPlayers", settings.maxPlayers},
    };
}

MultiplayerSettings MultiplayerSettingsFromJson(const json& value) {
    MultiplayerSettings settings;
    if (!value.is_object()) {
        return settings;
    }
    settings.enabled = value.value("enabled", settings.enabled);
    settings.topology = value.value("topology", settings.topology);
    settings.transport = value.value("transport", settings.transport);
    settings.bindAddress = value.value("bindAddress", settings.bindAddress);
    settings.port = value.value("port", settings.port);
    settings.maxPlayers = value.value("maxPlayers", settings.maxPlayers);
    return settings;
}

json BuildSettingsToJson(const ProjectBuildSettings& settings) {
    return json{
        {"targetPlatform", settings.targetPlatform},
        {"architecture", settings.architecture},
        {"configuration", settings.configuration},
        {"outputDirectory", settings.outputDirectory},
        {"executableName", settings.executableName},
        {"developmentBuild", settings.developmentBuild},
        {"copyProjectFiles", settings.copyProjectFiles},
        {"runPlayerSmokeAfterBuild", settings.runPlayerSmokeAfterBuild},
        {"generateInstallerManifest", settings.generateInstallerManifest},
        {"generateUpdaterManifest", settings.generateUpdaterManifest},
        {"releaseChannel", settings.releaseChannel},
        {"signingMode", settings.signingMode},
        {"scenes", settings.scenes},
        {"multiplayer", MultiplayerSettingsToJson(settings.multiplayer)},
    };
}

ProjectBuildSettings BuildSettingsFromJson(const json& value) {
    ProjectBuildSettings settings;
    if (!value.is_object()) {
        return settings;
    }
    settings.targetPlatform = value.value("targetPlatform", settings.targetPlatform);
    settings.architecture = value.value("architecture", settings.architecture);
    settings.configuration = value.value("configuration", settings.configuration);
    settings.outputDirectory = value.value("outputDirectory", settings.outputDirectory);
    settings.executableName = value.value("executableName", settings.executableName);
    settings.developmentBuild = value.value("developmentBuild", settings.developmentBuild);
    settings.copyProjectFiles = value.value("copyProjectFiles", settings.copyProjectFiles);
    settings.runPlayerSmokeAfterBuild = value.value("runPlayerSmokeAfterBuild", settings.runPlayerSmokeAfterBuild);
    settings.generateInstallerManifest = value.value("generateInstallerManifest", settings.generateInstallerManifest);
    settings.generateUpdaterManifest = value.value("generateUpdaterManifest", settings.generateUpdaterManifest);
    settings.releaseChannel = value.value("releaseChannel", settings.releaseChannel);
    settings.signingMode = value.value("signingMode", settings.signingMode);
    settings.scenes.clear();
    if (value.contains("scenes") && value.at("scenes").is_array()) {
        for (const json& scene : value.at("scenes")) {
            if (scene.is_string() && !scene.get<std::string>().empty()) {
                settings.scenes.push_back(scene.get<std::string>());
            }
        }
    }
    if (value.contains("multiplayer")) {
        settings.multiplayer = MultiplayerSettingsFromJson(value.at("multiplayer"));
    }
    return settings;
}

std::vector<std::string> DefaultProjectTags() {
    return {"Untagged", "Player", "MainCamera", "GameController", "Collectible", "Respawn", "Finish", "EditorOnly"};
}

std::vector<std::string> DefaultProjectLayers() {
    return {"Default", "TransparentFX", "Ignore Raycast", "Water", "UI",
            "Gameplay", "Environment", "Player", "Enemy", "Physics"};
}

bool IsDefaultProjectTagName(const std::string& tag) {
    const std::vector<std::string> defaults = DefaultProjectTags();
    return std::find(defaults.begin(), defaults.end(), tag) != defaults.end();
}

bool IsDefaultProjectLayerName(const std::string& layer) {
    const std::vector<std::string> defaults = DefaultProjectLayers();
    return std::find(defaults.begin(), defaults.end(), layer) != defaults.end();
}

std::string TrimCopy(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
                    return !std::isspace(c);
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
                    return !std::isspace(c);
                }).base(),
                value.end());
    return value;
}

std::string NormalizeProjectSettingName(std::string value) {
    value = TrimCopy(std::move(value));
    std::string normalized;
    normalized.reserve(value.size());
    bool lastWasSpace = false;
    for (char character : value) {
        unsigned char c = static_cast<unsigned char>(character);
        if (std::iscntrl(c) || character == '|' || character == ';') {
            c = ' ';
        }
        if (std::isspace(c)) {
            if (!lastWasSpace) {
                normalized.push_back(' ');
                lastWasSpace = true;
            }
            continue;
        }
        normalized.push_back(static_cast<char>(c));
        lastWasSpace = false;
        if (normalized.size() >= 48) {
            break;
        }
    }
    return TrimCopy(std::move(normalized));
}

bool ContainsSettingName(const std::vector<std::string>& values, const std::string& name) {
    return std::find(values.begin(), values.end(), name) != values.end();
}

void AddUniqueSettingName(std::vector<std::string>* values, std::string name) {
    if (values == nullptr) {
        return;
    }
    name = NormalizeProjectSettingName(std::move(name));
    if (!name.empty() && !ContainsSettingName(*values, name)) {
        values->push_back(std::move(name));
    }
}

LayerCollisionPair OrderedLayerCollisionPair(std::string first, std::string second) {
    first = NormalizeProjectSettingName(std::move(first));
    second = NormalizeProjectSettingName(std::move(second));
    if (second < first) {
        std::swap(first, second);
    }
    return {std::move(first), std::move(second)};
}

bool SameLayerCollisionPair(const LayerCollisionPair& left, const LayerCollisionPair& right) {
    return left.first == right.first && left.second == right.second;
}

bool ContainsLayerCollisionPair(const std::vector<LayerCollisionPair>& pairs, const LayerCollisionPair& pair) {
    return std::any_of(pairs.begin(), pairs.end(), [&pair](const LayerCollisionPair& value) {
        return SameLayerCollisionPair(value, pair);
    });
}

std::string SerializeSettingList(const std::vector<std::string>& values) {
    std::ostringstream stream;
    bool first = true;
    for (const std::string& value : values) {
        if (!first) {
            stream << ';';
        }
        stream << value;
        first = false;
    }
    return stream.str();
}

std::string SerializeLayerCollisionPairs(const std::vector<LayerCollisionPair>& pairs) {
    std::ostringstream stream;
    bool first = true;
    for (const LayerCollisionPair& pair : pairs) {
        if (!first) {
            stream << ';';
        }
        stream << pair.first << '|' << pair.second;
        first = false;
    }
    return stream.str();
}

std::string NormalizeRenderProfileId(std::string value) {
    value = ToLower(TrimText(std::move(value)));
    for (char& character : value) {
        if (std::isspace(static_cast<unsigned char>(character)) || character == '_') {
            character = '-';
        }
    }
    while (value.find("--") != std::string::npos) {
        value.replace(value.find("--"), 2, "-");
    }
    if (value == "builtin" || value == "built-in" || value == "basic" || value == "basic-builtin") {
        return "basic-built-in";
    }
    if (value == "2d-renderer" || value == "sprite" || value == "sprites") {
        return "2d";
    }
    if (value == "lightweight" || value == "lightweight3d" || value == "lightweight-3d-renderer") {
        return "lightweight-3d";
    }
    if (value == "high-fidelity" || value == "highfidelity" || value == "high-fidelity-renderer") {
        return "high-fidelity-3d";
    }
    return value;
}

void AddUniqueString(std::vector<std::string>* values, std::string value) {
    if (values == nullptr) {
        return;
    }
    value = TrimText(std::move(value));
    if (value.empty() || std::find(values->begin(), values->end(), value) != values->end()) {
        return;
    }
    values->push_back(std::move(value));
}

RenderProfileSettings MakeRenderProfile(std::string id,
                                         std::string displayName,
                                         std::string dimensionMode,
                                         std::string materialModel,
                                         std::string lightingModel,
                                         bool postProcessingEnabled,
                                         std::vector<std::string> postProcessingEffects,
                                         bool mesh3d,
                                         bool sprites,
                                         bool runtimeUi,
                                         bool editorGizmos,
                                         std::string resourceBackedModels,
                                         std::vector<std::string> requirements = {}) {
    RenderProfileSettings profile;
    profile.id = std::move(id);
    profile.displayName = std::move(displayName);
    profile.dimensionMode = std::move(dimensionMode);
    profile.materialModel = std::move(materialModel);
    profile.lightingModel = std::move(lightingModel);
    profile.postProcessingEnabled = postProcessingEnabled;
    profile.postProcessingEffects = std::move(postProcessingEffects);
    profile.mesh3d = mesh3d;
    profile.sprites = sprites;
    profile.runtimeUi = runtimeUi;
    profile.editorGizmos = editorGizmos;
    profile.resourceBackedModels = std::move(resourceBackedModels);
    profile.requirements = std::move(requirements);
    return profile;
}

std::vector<RenderProfileSettings> DefaultRenderProfiles() {
    return {
        MakeRenderProfile("basic-built-in", "Basic Built-in", "mixed", "unlit-color", "none", true, {"fog"},
                          true, false, true, true, "placeholder"),
        MakeRenderProfile("2d", "2D", "2d", "unlit-sprite", "2d-basic", false, {},
                          false, true, true, true, "not-supported"),
        MakeRenderProfile("lightweight-3d", "Lightweight 3D", "3d", "simple-lit",
                          "single-directional-plus-ambient", true, {"fog", "basic-color-grading"},
                          true, false, true, true, "placeholder"),
        MakeRenderProfile("high-fidelity-3d", "High Fidelity 3D", "3d", "pbr", "clustered-forward", true,
                          {"tonemapping", "bloom", "ssao"}, true, false, true, true, "decoded",
                          {"gpu-buffer-backend"}),
    };
}

const RenderProfileSettings* FindRenderProfileInList(const std::vector<RenderProfileSettings>& profiles,
                                                     const std::string& id) {
    const std::string normalized = NormalizeRenderProfileId(id);
    auto it = std::find_if(profiles.begin(), profiles.end(), [&normalized](const RenderProfileSettings& profile) {
        return NormalizeRenderProfileId(profile.id) == normalized;
    });
    return it == profiles.end() ? nullptr : &(*it);
}

std::vector<std::string> StringVectorFromJson(const json& value) {
    std::vector<std::string> result;
    if (!value.is_array()) {
        return result;
    }
    for (const json& item : value) {
        if (item.is_string()) {
            AddUniqueString(&result, item.get<std::string>());
        }
    }
    return result;
}

json StringVectorToJson(const std::vector<std::string>& values) {
    json array = json::array();
    for (const std::string& value : values) {
        array.push_back(value);
    }
    return array;
}

RenderProfileSettings RenderProfileFromJson(const std::string& fallbackId, const json& value) {
    RenderProfileSettings profile;
    profile.id = NormalizeRenderProfileId(value.value("id", fallbackId));
    profile.displayName = value.value("displayName", profile.id);
    profile.dimensionMode = value.value("dimensionMode", profile.dimensionMode);
    profile.materialModel = value.value("materialModel", profile.materialModel);
    profile.lightingModel = value.value("lightingModel", profile.lightingModel);
    if (value.contains("postProcessing") && value.at("postProcessing").is_object()) {
        const json& post = value.at("postProcessing");
        profile.postProcessingEnabled = post.value("enabled", profile.postProcessingEnabled);
        if (post.contains("effects")) {
            profile.postProcessingEffects = StringVectorFromJson(post.at("effects"));
        }
    }
    if (value.contains("features") && value.at("features").is_object()) {
        const json& features = value.at("features");
        profile.mesh3d = features.value("mesh3d", profile.mesh3d);
        profile.sprites = features.value("sprites", profile.sprites);
        profile.runtimeUi = features.value("runtimeUi", profile.runtimeUi);
        profile.editorGizmos = features.value("editorGizmos", profile.editorGizmos);
        profile.resourceBackedModels = features.value("resourceBackedModels", profile.resourceBackedModels);
    }
    if (value.contains("requires")) {
        profile.requirements = StringVectorFromJson(value.at("requires"));
    }
    return profile;
}

json RenderProfileToJson(const RenderProfileSettings& profile) {
    return json{
        {"id", profile.id},
        {"displayName", profile.displayName},
        {"dimensionMode", profile.dimensionMode},
        {"materialModel", profile.materialModel},
        {"lightingModel", profile.lightingModel},
        {"postProcessing",
         {
             {"enabled", profile.postProcessingEnabled},
             {"effects", StringVectorToJson(profile.postProcessingEffects)},
         }},
        {"features",
         {
             {"mesh3d", profile.mesh3d},
             {"sprites", profile.sprites},
             {"runtimeUi", profile.runtimeUi},
             {"editorGizmos", profile.editorGizmos},
             {"resourceBackedModels", profile.resourceBackedModels},
         }},
        {"requires", StringVectorToJson(profile.requirements)},
    };
}

ProjectRenderingSettings DefaultProjectRenderingSettings() {
    ProjectRenderingSettings settings;
    settings.schemaVersion = 1;
    settings.activeProfile = "basic-built-in";
    settings.graphicsBackend.preferred = "auto";
    settings.graphicsBackend.allowed = {"opengl"};
    settings.graphicsBackend.fallback = "opengl";
    settings.profiles = DefaultRenderProfiles();
    return settings;
}

void NormalizeProjectRenderingSettings(ProjectRenderingSettings* settings) {
    if (settings == nullptr) {
        return;
    }
    settings->schemaVersion = 1;
    settings->activeProfile = NormalizeRenderProfileId(settings->activeProfile);
    if (settings->graphicsBackend.preferred.empty()) {
        settings->graphicsBackend.preferred = "auto";
    }
    if (settings->graphicsBackend.fallback.empty()) {
        settings->graphicsBackend.fallback = "opengl";
    }
    std::vector<std::string> allowedBackends;
    for (const std::string& backend : settings->graphicsBackend.allowed) {
        AddUniqueString(&allowedBackends, ToLower(backend));
    }
    if (allowedBackends.empty()) {
        allowedBackends.push_back("opengl");
    }
    settings->graphicsBackend.allowed = std::move(allowedBackends);

    std::vector<RenderProfileSettings> profiles = DefaultRenderProfiles();
    for (const RenderProfileSettings& profile : settings->profiles) {
        RenderProfileSettings normalized = profile;
        normalized.id = NormalizeRenderProfileId(normalized.id);
        if (normalized.id.empty() || FindRenderProfileInList(profiles, normalized.id) != nullptr) {
            continue;
        }
        if (normalized.displayName.empty()) {
            normalized.displayName = normalized.id;
        }
        profiles.push_back(std::move(normalized));
    }
    if (FindRenderProfileInList(profiles, settings->activeProfile) == nullptr) {
        settings->activeProfile = "basic-built-in";
    }
    settings->profiles = std::move(profiles);
}

json ProjectRenderingToJson(ProjectRenderingSettings settings) {
    NormalizeProjectRenderingSettings(&settings);
    json profileMap = json::object();
    for (const RenderProfileSettings& profile : settings.profiles) {
        profileMap[profile.id] = RenderProfileToJson(profile);
    }
    return json{
        {"schemaVersion", settings.schemaVersion},
        {"activeProfile", settings.activeProfile},
        {"graphicsBackend",
         {
             {"preferred", settings.graphicsBackend.preferred},
             {"allowed", StringVectorToJson(settings.graphicsBackend.allowed)},
             {"fallback", settings.graphicsBackend.fallback},
         }},
        {"profiles", profileMap},
    };
}

ProjectRenderingSettings ProjectRenderingFromJson(const json& value) {
    ProjectRenderingSettings settings = DefaultProjectRenderingSettings();
    if (!value.is_object()) {
        NormalizeProjectRenderingSettings(&settings);
        return settings;
    }
    settings.schemaVersion = value.value("schemaVersion", settings.schemaVersion);
    settings.activeProfile = value.value("activeProfile", settings.activeProfile);
    if (value.contains("graphicsBackend") && value.at("graphicsBackend").is_object()) {
        const json& backend = value.at("graphicsBackend");
        settings.graphicsBackend.preferred = backend.value("preferred", settings.graphicsBackend.preferred);
        settings.graphicsBackend.fallback = backend.value("fallback", settings.graphicsBackend.fallback);
        if (backend.contains("allowed")) {
            settings.graphicsBackend.allowed = StringVectorFromJson(backend.at("allowed"));
        }
    }
    settings.profiles.clear();
    if (value.contains("profiles")) {
        const json& profiles = value.at("profiles");
        if (profiles.is_object()) {
            for (auto it = profiles.begin(); it != profiles.end(); ++it) {
                if (it.value().is_object()) {
                    settings.profiles.push_back(RenderProfileFromJson(it.key(), it.value()));
                }
            }
        } else if (profiles.is_array()) {
            for (const json& profile : profiles) {
                if (profile.is_object()) {
                    settings.profiles.push_back(RenderProfileFromJson("", profile));
                }
            }
        }
    }
    NormalizeProjectRenderingSettings(&settings);
    return settings;
}

EditorProjectSettings DefaultEditorProjectSettings() {
    EditorProjectSettings settings;
    settings.tags = DefaultProjectTags();
    settings.layers = DefaultProjectLayers();
    return settings;
}

void NormalizeEditorProjectSettings(EditorProjectSettings* settings) {
    if (settings == nullptr) {
        return;
    }

    std::vector<std::string> tags;
    for (const std::string& tag : DefaultProjectTags()) {
        AddUniqueSettingName(&tags, tag);
    }
    for (const std::string& tag : settings->tags) {
        AddUniqueSettingName(&tags, tag);
    }

    std::vector<std::string> layers;
    for (const std::string& layer : DefaultProjectLayers()) {
        AddUniqueSettingName(&layers, layer);
    }
    for (const std::string& layer : settings->layers) {
        AddUniqueSettingName(&layers, layer);
    }

    std::vector<LayerCollisionPair> pairs;
    for (const LayerCollisionPair& pair : settings->disabledLayerCollisionPairs) {
        LayerCollisionPair normalized = OrderedLayerCollisionPair(pair.first, pair.second);
        if (normalized.first.empty() || normalized.second.empty() ||
            !ContainsSettingName(layers, normalized.first) || !ContainsSettingName(layers, normalized.second) ||
            ContainsLayerCollisionPair(pairs, normalized)) {
            continue;
        }
        pairs.push_back(std::move(normalized));
    }

    settings->tags = std::move(tags);
    settings->layers = std::move(layers);
    settings->disabledLayerCollisionPairs = std::move(pairs);
}

json ProjectSettingsToJson(EditorProjectSettings settings) {
    NormalizeEditorProjectSettings(&settings);
    json disabledPairs = json::array();
    for (const LayerCollisionPair& pair : settings.disabledLayerCollisionPairs) {
        disabledPairs.push_back({{"first", pair.first}, {"second", pair.second}});
    }
    return json{
        {"tags", settings.tags},
        {"layers", settings.layers},
        {"physics", {{"disabledLayerCollisionPairs", disabledPairs}}},
    };
}

EditorProjectSettings ProjectSettingsFromJson(const json& value) {
    EditorProjectSettings settings = DefaultEditorProjectSettings();
    if (!value.is_object()) {
        NormalizeEditorProjectSettings(&settings);
        return settings;
    }

    if (value.contains("tags") && value.at("tags").is_array()) {
        settings.tags.clear();
        for (const json& tag : value.at("tags")) {
            if (tag.is_string()) {
                settings.tags.push_back(tag.get<std::string>());
            }
        }
    }
    if (value.contains("layers") && value.at("layers").is_array()) {
        settings.layers.clear();
        for (const json& layer : value.at("layers")) {
            if (layer.is_string()) {
                settings.layers.push_back(layer.get<std::string>());
            }
        }
    }

    const json* physics = value.contains("physics") && value.at("physics").is_object() ? &value.at("physics") : &value;
    if (physics->contains("disabledLayerCollisionPairs") && physics->at("disabledLayerCollisionPairs").is_array()) {
        settings.disabledLayerCollisionPairs.clear();
        for (const json& pair : physics->at("disabledLayerCollisionPairs")) {
            if (pair.is_object()) {
                settings.disabledLayerCollisionPairs.push_back(
                    {pair.value("first", ""), pair.value("second", "")});
            } else if (pair.is_array() && pair.size() >= 2 && pair.at(0).is_string() && pair.at(1).is_string()) {
                settings.disabledLayerCollisionPairs.push_back({pair.at(0).get<std::string>(),
                                                                pair.at(1).get<std::string>()});
            }
        }
    }

    NormalizeEditorProjectSettings(&settings);
    return settings;
}

std::string ComponentStringProperty(const Component& component, const std::string& propertyName, std::string fallback = {}) {
    for (const ComponentProperty& property : component.properties) {
        if (property.name == propertyName) {
            return property.value;
        }
    }
    return fallback;
}

bool HasComponent(const Entity& entity, const std::string& type) {
    return std::any_of(entity.components.begin(), entity.components.end(), [&type](const Component& component) {
        return component.type == type;
    });
}

bool IsBuiltInNonRemovableComponent(const std::string& type) {
    return type == "Transform" || type == "Camera";
}

bool IsPhysicsBodyOrColliderComponent(const std::string& type) {
    return type == "Rigidbody2D" || type == "Rigidbody3D" || type == "Collider2D" || type == "Collider3D";
}

bool HasPhysicsBodyOrCollider(const Entity& entity) {
    return std::any_of(entity.components.begin(), entity.components.end(), [](const Component& component) {
        return IsPhysicsBodyOrColliderComponent(component.type);
    });
}

bool HasPhysicsSettings(const Entity& entity) {
    return HasComponent(entity, "PhysicsSettings");
}

bool CompleteEditorPhysicsComponents(Entity& entity) {
    bool changed = false;
    if (HasComponent(entity, "Rigidbody2D") && !HasComponent(entity, "Collider2D")) {
        entity.components.push_back(MakeCollider2DComponent("Box"));
        changed = true;
    }
    if (HasComponent(entity, "Rigidbody3D") && !HasComponent(entity, "Collider3D")) {
        entity.components.push_back(MakeCollider3DComponent("Box"));
        changed = true;
    }
    return changed;
}

Component* FindComponent(Entity& entity, const std::string& type) {
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [&type](const Component& component) {
        return component.type == type;
    });
    return it == entity.components.end() ? nullptr : &(*it);
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

std::array<float, 3> ComponentVec3Property(const Component& component, const std::string& propertyName,
                                           std::array<float, 3> fallback) {
    const std::string value = ComponentStringProperty(component, propertyName);
    if (value.empty()) {
        return fallback;
    }

    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream stream(normalized);
    std::array<float, 3> parsed = fallback;
    if (stream >> parsed[0] >> parsed[1] >> parsed[2] && std::isfinite(parsed[0]) && std::isfinite(parsed[1]) &&
        std::isfinite(parsed[2])) {
        return parsed;
    }
    return fallback;
}

std::string Vec3ToString(const std::array<float, 3>& value) {
    std::ostringstream stream;
    stream << value[0] << ", " << value[1] << ", " << value[2];
    return stream.str();
}

std::filesystem::path AbsoluteOrProjectRelative(const std::filesystem::path& projectRoot, const std::filesystem::path& path) {
    return path.is_absolute() ? path : projectRoot / path;
}

std::filesystem::path RelativeToProject(const std::filesystem::path& projectRoot, const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path relative = std::filesystem::relative(path, projectRoot, error);
    if (error) {
        return path.filename();
    }
    for (const std::filesystem::path& part : relative) {
        if (part == ".") {
            continue;
        }
        if (part == "..") {
            return path;
        }
        break;
    }
    if (relative.is_absolute()) {
        return path;
    }
    return relative;
}

std::string RelativeToProjectString(const std::filesystem::path& projectRoot, const std::filesystem::path& path) {
    return RelativeToProject(projectRoot, path).generic_string();
}

std::string DirectorySetting(const json& document, const std::string& key, const std::string& legacyKey,
                             const std::string& fallback) {
    if (document.contains("directories") && document.at("directories").is_object()) {
        const json& directories = document.at("directories");
        if (directories.contains(key) && directories.at(key).is_string()) {
            return directories.at(key).get<std::string>();
        }
    }
    if (!legacyKey.empty() && document.contains(legacyKey) && document.at(legacyKey).is_string()) {
        return document.at(legacyKey).get<std::string>();
    }
    return fallback;
}

std::filesystem::path SceneRootFallbackFromActiveScene(const std::filesystem::path& activeScene) {
    if (activeScene.has_parent_path()) {
        return activeScene.parent_path();
    }
    return kDefaultSceneRoot;
}

std::filesystem::path ResolveProjectFilePath(const std::filesystem::path& projectFileOrRoot, bool* resolvedPartialPath) {
    if (resolvedPartialPath != nullptr) {
        *resolvedPartialPath = false;
    }

    std::filesystem::path projectFile = projectFileOrRoot;
    if (std::filesystem::is_directory(projectFile)) {
        projectFile /= kProjectFileName;
        return projectFile;
    }

    if (std::filesystem::exists(projectFile)) {
        return projectFile;
    }

    const std::filesystem::path siblingProjectFile = projectFile.parent_path() / kProjectFileName;
    const std::string partialName = projectFile.filename().string();
    const std::string expectedName = siblingProjectFile.filename().string();
    if (!partialName.empty() && expectedName.rfind(partialName, 0) == 0 &&
        std::filesystem::exists(siblingProjectFile)) {
        if (resolvedPartialPath != nullptr) {
            *resolvedPartialPath = true;
        }
        return siblingProjectFile;
    }

    return projectFile;
}

bool HideFromProjectWorkTree(const std::filesystem::directory_entry& entry, int depth);

ProjectTreeNode BuildFilesystemNode(const std::filesystem::path& path, const std::string& label, int depth = 0) {
    ProjectTreeNode node;
    node.name = label;
    node.path = std::filesystem::absolute(path).string();
    node.folder = std::filesystem::is_directory(path);
    node.filesystemBacked = true;
    if (!node.folder || depth >= 4 || !std::filesystem::exists(path)) {
        return node;
    }

    std::vector<std::filesystem::directory_entry> entries;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path, error)) {
        if (!error && !HideFromProjectWorkTree(entry, depth)) {
            entries.push_back(entry);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        const bool leftDir = left.is_directory();
        const bool rightDir = right.is_directory();
        if (leftDir != rightDir) {
            return leftDir && !rightDir;
        }
        return left.path().filename().string() < right.path().filename().string();
    });

    for (const std::filesystem::directory_entry& entry : entries) {
        node.children.push_back(BuildFilesystemNode(entry.path(), entry.path().filename().string(), depth + 1));
    }

    return node;
}

bool HideFromProjectWorkTree(const std::filesystem::directory_entry& entry, int depth) {
    if (depth != 0) {
        return false;
    }

    const std::string name = entry.path().filename().string();
    return name == kProjectFileName || name == "Library" || name == "Temp" || name == "Logs" || name == "Saved";
}

bool SkipProjectSaveAsCopyEntry(const std::filesystem::directory_entry& entry) {
    const std::string name = entry.path().filename().string();
    return name == "Library" || name == "Temp" || name == "Logs";
}

bool LexicallyInsideOrSame(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    const std::filesystem::path normalizedRoot = std::filesystem::absolute(root).lexically_normal();
    const std::filesystem::path normalizedCandidate = std::filesystem::absolute(candidate).lexically_normal();
    const std::filesystem::path relative = normalizedCandidate.lexically_relative(normalizedRoot);
    if (relative.empty() || relative == ".") {
        return true;
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            return false;
        }
        break;
    }
    return !relative.is_absolute();
}

std::string SanitizeFileStem(std::string value, const std::string& fallback) {
    for (char& character : value) {
        const unsigned char c = static_cast<unsigned char>(character);
        if (!std::isalnum(c) && character != '_' && character != '-' && character != ' ') {
            character = '_';
        }
    }
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
                    return !std::isspace(c) && c != '_';
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
                    return !std::isspace(c) && c != '_';
                }).base(),
                value.end());
    return value.empty() ? fallback : value;
}

std::filesystem::path SafeProjectRelativePath(const std::filesystem::path& projectRoot,
                                              const std::filesystem::path& path,
                                              const std::filesystem::path& fallback) {
    std::filesystem::path relative = RelativeToProject(projectRoot, path);
    if (relative.empty() || relative.is_absolute()) {
        return fallback;
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            return fallback;
        }
    }
    return relative;
}

std::string UtcTimestampString() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string QuoteCommandArgument(const std::filesystem::path& path) {
    std::string value = path.string();
    std::string quoted = "\"";
    for (char character : value) {
        if (character == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(character);
        }
    }
    quoted += "\"";
    return quoted;
}

std::string RelativeBuildPathString(const std::filesystem::path& outputDirectory, const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, outputDirectory, error);
    return (error || relative.empty() ? path : relative).generic_string();
}

void AddExecutablePermissions(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::add,
                                 error);
}

std::string HexUint64(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

std::string FileHashString(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::uint64_t hash = 1469598103934665603ull;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
        const std::streamsize count = input.gcount();
        for (std::streamsize index = 0; index < count; ++index) {
            hash ^= static_cast<unsigned char>(buffer[index]);
            hash *= 1099511628211ull;
        }
    }
    return "fnv1a64:" + HexUint64(hash);
}

std::string ReadTextPrefix(const std::filesystem::path& path, size_t maxBytes = 1024 * 1024) {
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

std::string JsonStringValue(const json& value, const std::string& key) {
    if (!value.is_object() || !value.contains(key) || !value.at(key).is_string()) {
        return {};
    }
    return value.at(key).get<std::string>();
}

std::vector<std::string> DiscoverAssetDependencies(const std::filesystem::path& sourcePath, const std::string& type) {
    std::set<std::string> dependencies;
    const std::filesystem::path sourceFolder = sourcePath.parent_path();
    const std::string extension = ToLower(sourcePath.extension().string());

    auto addDependency = [&](const std::string& dependency) {
        if (dependency.empty()) {
            return;
        }
        const std::filesystem::path path = std::filesystem::absolute(sourceFolder / dependency).lexically_normal();
        dependencies.insert(path.string());
    };

    if (extension == ".gltf") {
        try {
            std::ifstream input(sourcePath);
            const json document = json::parse(input, nullptr, false);
            if (!document.is_discarded()) {
                if (document.contains("buffers") && document.at("buffers").is_array()) {
                    for (const json& buffer : document.at("buffers")) {
                        const std::string uri = JsonStringValue(buffer, "uri");
                        if (uri.rfind("data:", 0) != 0) {
                            addDependency(uri);
                        }
                    }
                }
                if (document.contains("images") && document.at("images").is_array()) {
                    for (const json& image : document.at("images")) {
                        const std::string uri = JsonStringValue(image, "uri");
                        if (uri.rfind("data:", 0) != 0) {
                            addDependency(uri);
                        }
                    }
                }
            }
        } catch (...) {
        }
    } else if (extension == ".obj") {
        std::istringstream stream(ReadTextPrefix(sourcePath));
        std::string line;
        while (std::getline(stream, line)) {
            line = TrimCopy(line);
            if (line.rfind("mtllib ", 0) == 0) {
                addDependency(TrimCopy(line.substr(7)));
            }
        }
    } else if (extension == ".mtl") {
        std::istringstream stream(ReadTextPrefix(sourcePath));
        std::string line;
        while (std::getline(stream, line)) {
            line = TrimCopy(line);
            const std::vector<std::string> textureKeys{"map_Kd ", "map_Ks ", "map_Bump ", "bump ", "disp "};
            for (const std::string& key : textureKeys) {
                if (line.rfind(key, 0) == 0) {
                    addDependency(TrimCopy(line.substr(key.size())));
                }
            }
        }
    } else if (type == "Model") {
        const std::vector<std::string> sidecarExtensions{".png", ".jpg", ".jpeg", ".tga", ".mtl"};
        for (const std::string& sidecarExtension : sidecarExtensions) {
            const std::filesystem::path sidecar = sourcePath.parent_path() / (sourcePath.stem().string() + sidecarExtension);
            if (std::filesystem::exists(sidecar)) {
                dependencies.insert(std::filesystem::absolute(sidecar).lexically_normal().string());
            }
        }
    }

    return std::vector<std::string>(dependencies.begin(), dependencies.end());
}

json BuildImportedResourceJson(const AssetRecord& record) {
    json resource{{"format", "aine.importedResource"},
                  {"version", 1},
                  {"id", record.id},
                  {"name", record.name},
                  {"type", record.type},
                  {"sourcePath", record.sourcePath},
                  {"sourceHash", record.sourceHash},
                  {"importedPath", record.importedPath},
                  {"license", record.license},
                  {"dependencies", record.dependencies}};

    if (record.type == "Model") {
        resource["renderer"] = {
            {"meshResource", record.importedPath},
            {"materialSlots", json::array({"Default"})},
            {"runtimeShape", "resource-backed-model"},
        };
    } else if (record.type == "Texture") {
        resource["texture"] = {
            {"uri", record.importedPath},
            {"usage", "albedo"},
            {"filterMode", "Point"},
            {"wrapMode", "Clamp"},
            {"spriteMode", "Single"},
            {"pixelsPerUnit", 100},
            {"pivot", json::array({0.5, 0.5})},
        };
        resource["sprite"] = {
            {"id", record.id + ":sprite"},
            {"name", record.name},
            {"textureAsset", record.id},
            {"textureUri", record.importedPath},
            {"rect", json::array({0, 0, 0, 0})},
            {"pivot", json::array({0.5, 0.5})},
            {"pixelsPerUnit", 100},
            {"packingTag", ""},
            {"atlas", ""},
        };
    } else if (record.type == "Audio") {
        resource["audio"] = {
            {"uri", record.importedPath},
            {"streaming", false},
        };
    } else if (record.type == "Material") {
        resource["material"] = {
            {"uri", record.importedPath},
        };
    }
    return resource;
}

json BuildAssetMetadataJson(const AssetRecord& record) {
    json metadata{{"format", "aine.assetMetadata"},
                  {"version", 1},
                  {"id", record.id},
                  {"name", record.name},
                  {"type", record.type},
                  {"sourceLabel", record.sourceLabel},
                  {"sourcePath", record.sourcePath},
                  {"sourceHash", record.sourceHash},
                  {"license", record.license},
                  {"importSettings", record.importSettings},
                  {"dependencies", record.dependencies},
                  {"importedAtUtc", UtcTimestampString()}};
    if (record.type == "Texture") {
        metadata["textureImport"] = {
            {"filterMode", "Point"},
            {"wrapMode", "Clamp"},
            {"generateMipmaps", false},
            {"spriteMode", "Single"},
            {"pixelsPerUnit", 100},
            {"pivot", json::array({0.5, 0.5})},
            {"slices", json::array()},
            {"packingTag", ""},
        };
    }
    return metadata;
}

json BuildAssetThumbnailJson(const AssetRecord& record) {
    return json{{"format", "aine.assetThumbnail"},
                {"version", 1},
                {"assetId", record.id},
                {"label", record.name},
                {"type", record.type},
                {"sourceHash", record.sourceHash},
                {"previewKind", record.type == "Texture" ? "texture" : record.type == "Model" ? "model" : "metadata"}};
}

bool CopyDirectoryTree(const std::filesystem::path& source,
                       const std::filesystem::path& target,
                       int* copiedFileCount,
                       std::string* error) {
    std::error_code filesystemError;
    if (source.empty() || !std::filesystem::exists(source, filesystemError)) {
        return true;
    }
    if (!std::filesystem::is_directory(source, filesystemError)) {
        if (error != nullptr) {
            *error = "Source is not a directory: " + source.string();
        }
        return false;
    }
    if (LexicallyInsideOrSame(source, target)) {
        if (error != nullptr) {
            *error = "Build output cannot be inside copied source folder: " + source.string();
        }
        return false;
    }

    std::filesystem::create_directories(target, filesystemError);
    if (filesystemError) {
        if (error != nullptr) {
            *error = filesystemError.message();
        }
        return false;
    }

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(source, filesystemError)) {
        if (filesystemError) {
            if (error != nullptr) {
                *error = filesystemError.message();
            }
            return false;
        }
        const std::filesystem::path relative = std::filesystem::relative(entry.path(), source, filesystemError);
        if (filesystemError) {
            if (error != nullptr) {
                *error = filesystemError.message();
            }
            return false;
        }
        const std::filesystem::path destination = target / relative;
        if (entry.is_directory(filesystemError)) {
            std::filesystem::create_directories(destination, filesystemError);
        } else if (entry.is_regular_file(filesystemError)) {
            std::filesystem::create_directories(destination.parent_path(), filesystemError);
            if (!filesystemError) {
                std::filesystem::copy_file(entry.path(), destination,
                                           std::filesystem::copy_options::overwrite_existing, filesystemError);
                if (!filesystemError && copiedFileCount != nullptr) {
                    ++(*copiedFileCount);
                }
            }
        }
        if (filesystemError) {
            if (error != nullptr) {
                *error = filesystemError.message();
            }
            return false;
        }
    }
    return true;
}

} // namespace

EditorState::EditorState() {
    renderingSettings_ = DefaultProjectRenderingSettings();
    projectSettings_ = DefaultEditorProjectSettings();
    SetDefaultPathsForRoot(DefaultProjectRootPath());
    EnsureProjectFolders();
    RefreshProjectTree();

    AddLog(LogLevel::Info, "Editor state initialized.");
    AddChat(ChatSender::System, "Codex chat ready. Local prompt assist is disabled while Codex in-editor responses are hardened.");
    AddActivity("Agent runtime", "Idle", "AgentOrchestrator ready");
    AddActivity("Tool gateway", "Ready", "Deterministic command batches available");

    std::string error;
    if (std::filesystem::exists(projectFilePath_) && OpenProject(projectFilePath_, &error)) {
        AddLog(LogLevel::Info, "Loaded default project from " + ProjectFilePathString() + ".");
    } else {
        if (!error.empty()) {
            AddLog(LogLevel::Warning, "Default project load skipped: " + error);
        }
        SeedStarterScene();
    }
}

Entity& EditorState::CreateEntity(std::string name, std::vector<Component> components) {
    const int id = nextEntityId_++;
    Entity& entity = CreateEntityWithId(id, std::move(name), std::move(components));
    if (CompleteEditorPhysicsComponents(entity)) {
        SyncTransformComponent(entity);
    }
    EnsurePhysicsSettingsForScene();
    Entity* created = FindEntity(id);
    return created == nullptr ? entities_.back() : *created;
}

Entity& EditorState::CreateEntityWithId(int id, std::string name, std::vector<Component> components) {
    Entity entity;
    entity.id = id;
    entity.name = std::move(name);
    entity.components = std::move(components);
    if (!HasComponent(entity, "Transform")) {
        entity.components.insert(entity.components.begin(), MakeTransformComponent());
    }
    SyncTransformComponent(entity);

    entities_.push_back(std::move(entity));
    nextEntityId_ = std::max(nextEntityId_, id + 1);
    if (selectedEntityId_ == 0) {
        selectedEntityId_ = entities_.back().id;
    }
    return entities_.back();
}

void EditorState::ClearScene() {
    entities_.clear();
    selectedEntityId_ = 0;
    nextEntityId_ = 1;
}

void EditorState::SeedStarterScene() {
    ClearScene();

    Entity& camera =
        CreateEntity("Main Camera", {MakeTransformComponent(), MakeCameraComponent(),
                                     MakeCameraFollowRuntimeComponent("Placeholder Cube")});
    camera.position = {0.0f, 3.0f, 8.0f};
    camera.rotation = {-20.0f, 0.0f, 0.0f};
    SetComponentProperty(*FindComponent(camera, "CameraFollowRuntime"), "mode", "Orbit");
    SetComponentProperty(*FindComponent(camera, "CameraFollowRuntime"), "distance", "8");
    SetComponentProperty(*FindComponent(camera, "CameraFollowRuntime"), "orbitPitchDegrees", "-20");
    SyncTransformComponent(camera);

    Entity& sun = CreateEntity("Directional Light", {MakeTransformComponent(), MakeLightComponent("Directional")});
    sun.rotation = {45.0f, -30.0f, 0.0f};
    SyncTransformComponent(sun);

    CreateEntity("Scene Environment", {MakeTransformComponent(), MakeEnvironmentLightingComponent()});

    Entity& cube = CreateEntity("Placeholder Cube", {MakeTransformComponent(), MakeMeshPlaceholderComponent("UnitCube")});
    cube.position = {0.0f, 0.5f, 0.0f};
    SyncTransformComponent(cube);

    AddLog(LogLevel::Info, "Seeded in-memory prototype scene.");
}

Entity* EditorState::FindEntity(int id) {
    auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    return it == entities_.end() ? nullptr : &(*it);
}

const Entity* EditorState::FindEntity(int id) const {
    auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    return it == entities_.end() ? nullptr : &(*it);
}

Entity* EditorState::FindEntityByName(const std::string& name) {
    auto it = std::find_if(entities_.rbegin(), entities_.rend(), [&name](const Entity& entity) {
        return entity.name == name;
    });
    return it == entities_.rend() ? nullptr : &(*it);
}

const Entity* EditorState::FindEntityByName(const std::string& name) const {
    auto it = std::find_if(entities_.rbegin(), entities_.rend(), [&name](const Entity& entity) {
        return entity.name == name;
    });
    return it == entities_.rend() ? nullptr : &(*it);
}

bool EditorState::RenameEntity(int id, const std::string& newName) {
    Entity* entity = FindEntity(id);
    if (entity == nullptr || newName.empty()) {
        return false;
    }
    entity->name = newName;
    return true;
}

bool EditorState::CanSetEntityParent(int childId, int parentId) const {
    if (childId == 0 || childId == parentId || FindEntity(childId) == nullptr) {
        return false;
    }
    if (parentId == 0) {
        return true;
    }

    const Entity* parent = FindEntity(parentId);
    if (parent == nullptr) {
        return false;
    }

    std::vector<int> visited;
    int cursor = parentId;
    while (cursor != 0) {
        if (cursor == childId || std::find(visited.begin(), visited.end(), cursor) != visited.end()) {
            return false;
        }
        visited.push_back(cursor);
        const Entity* current = FindEntity(cursor);
        if (current == nullptr) {
            return false;
        }
        cursor = current->parentId;
    }
    return true;
}

bool EditorState::SetEntityParent(int childId, int parentId) {
    if (!CanSetEntityParent(childId, parentId)) {
        return false;
    }

    Entity* child = FindEntity(childId);
    if (child == nullptr || child->parentId == parentId) {
        return false;
    }
    child->parentId = parentId;
    return true;
}

bool EditorState::IsEntityActiveInHierarchy(int id) const {
    const Entity* entity = FindEntity(id);
    if (entity == nullptr) {
        return false;
    }

    std::vector<int> visited;
    const Entity* current = entity;
    while (current != nullptr) {
        if (!current->activeSelf) {
            return false;
        }
        if (std::find(visited.begin(), visited.end(), current->id) != visited.end()) {
            return false;
        }
        visited.push_back(current->id);
        if (current->parentId == 0) {
            return true;
        }
        current = FindEntity(current->parentId);
    }
    return true;
}

bool EditorState::DeleteEntity(int id) {
    auto selectedIt = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    if (selectedIt == entities_.end()) {
        return false;
    }

    std::vector<int> deleteIds{id};
    bool added = true;
    while (added) {
        added = false;
        for (const Entity& entity : entities_) {
            if (std::find(deleteIds.begin(), deleteIds.end(), entity.id) != deleteIds.end()) {
                continue;
            }
            if (std::find(deleteIds.begin(), deleteIds.end(), entity.parentId) != deleteIds.end()) {
                deleteIds.push_back(entity.id);
                added = true;
            }
        }
    }

    const size_t deletedIndex = static_cast<size_t>(std::distance(entities_.begin(), selectedIt));
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(), [&deleteIds](const Entity& entity) {
                        return std::find(deleteIds.begin(), deleteIds.end(), entity.id) != deleteIds.end();
                    }),
                    entities_.end());

    if (selectedEntityId_ != 0 &&
        std::find(deleteIds.begin(), deleteIds.end(), selectedEntityId_) != deleteIds.end()) {
        if (entities_.empty()) {
            selectedEntityId_ = 0;
        } else {
            selectedEntityId_ = entities_[std::min(deletedIndex, entities_.size() - 1)].id;
        }
    }
    return true;
}

bool EditorState::SetEntityTransform(int id, const std::array<float, 3>* position, const std::array<float, 3>* rotation,
                                     const std::array<float, 3>* scale) {
    Entity* entity = FindEntity(id);
    if (entity == nullptr) {
        return false;
    }
    if (position != nullptr) {
        entity->position = *position;
    }
    if (rotation != nullptr) {
        entity->rotation = *rotation;
    }
    if (scale != nullptr) {
        entity->scale = *scale;
    }
    SyncTransformComponent(*entity);
    return true;
}

bool EditorState::AddComponentToEntity(int id, Component component) {
    Entity* entity = FindEntity(id);
    if (entity == nullptr || component.type.empty()) {
        return false;
    }
    const std::string addedType = component.type;
    if (addedType == "Camera" && HasComponent(*entity, "Camera")) {
        return false;
    }
    entity->components.push_back(std::move(component));
    if (addedType == "Camera" && !HasComponent(*entity, "CameraFollowRuntime")) {
        entity->components.push_back(MakeCameraFollowRuntimeComponent("Player"));
    }
    if (addedType == "Rigidbody2D" || addedType == "Rigidbody3D") {
        CompleteEditorPhysicsComponents(*entity);
    }
    SyncTransformComponent(*entity);
    if (IsPhysicsBodyOrColliderComponent(addedType)) {
        EnsurePhysicsSettingsForScene();
    }
    if (addedType == "PhysicsSettings") {
        SyncPhysicsSettingsComponents();
    }
    return true;
}

void EditorState::EnsurePhysicsSettingsForScene() {
    const bool sceneUsesPhysics = std::any_of(entities_.begin(), entities_.end(), [](const Entity& entity) {
        return HasPhysicsBodyOrCollider(entity);
    });
    if (!sceneUsesPhysics) {
        return;
    }

    EnsurePhysicsSettingsEntity();
}

int EditorState::EnsurePhysicsSettingsEntity() {
    const bool hasSettings = std::any_of(entities_.begin(), entities_.end(), [](const Entity& entity) {
        return HasPhysicsSettings(entity);
    });
    if (hasSettings) {
        SyncPhysicsSettingsComponents();
        for (const Entity& entity : entities_) {
            if (HasPhysicsSettings(entity)) {
                return entity.id;
            }
        }
        return 0;
    }

    std::string settingsName = "Physics Settings";
    for (int suffix = 2; FindEntityByName(settingsName) != nullptr && suffix < 1000; ++suffix) {
        settingsName = "Physics Settings " + std::to_string(suffix);
    }
    Entity& settings =
        CreateEntityWithId(nextEntityId_++, std::move(settingsName), {MakeTransformComponent(), MakePhysicsSettingsComponent()});
    SyncPhysicsSettingsComponents();
    return settings.id;
}

void EditorState::SyncPhysicsSettingsComponents() {
    NormalizeEditorProjectSettings(&projectSettings_);
    const std::string layers = SerializeSettingList(projectSettings_.layers);
    const std::string disabledPairs = SerializeLayerCollisionPairs(projectSettings_.disabledLayerCollisionPairs);
    for (Entity& entity : entities_) {
        for (Component& component : entity.components) {
            if (component.type != "PhysicsSettings") {
                continue;
            }
            SetComponentProperty(component, "collisionLayers", layers);
            SetComponentProperty(component, "disabledLayerCollisionPairs", disabledPairs);
        }
    }
}

void EditorState::EnsureSceneTagLayerValuesAreKnown() {
    NormalizeEditorProjectSettings(&projectSettings_);
    bool changed = false;
    for (const Entity& entity : entities_) {
        const std::string tag = NormalizeProjectSettingName(entity.tag);
        if (!tag.empty() && !ContainsSettingName(projectSettings_.tags, tag)) {
            projectSettings_.tags.push_back(tag);
            changed = true;
        }
        const std::string layer = NormalizeProjectSettingName(entity.layer);
        if (!layer.empty() && !ContainsSettingName(projectSettings_.layers, layer)) {
            projectSettings_.layers.push_back(layer);
            changed = true;
        }
    }
    if (changed) {
        NormalizeEditorProjectSettings(&projectSettings_);
        SyncPhysicsSettingsComponents();
    }
}

bool EditorState::RemoveComponentFromEntity(int id, const std::string& componentType) {
    Entity* entity = FindEntity(id);
    if (entity == nullptr || componentType.empty() || IsBuiltInNonRemovableComponent(componentType)) {
        return false;
    }

    const auto it = std::find_if(entity->components.begin(), entity->components.end(),
                                 [&componentType](const Component& component) {
                                     return component.type == componentType;
                                 });
    if (it == entity->components.end()) {
        return false;
    }

    entity->components.erase(it);
    SyncTransformComponent(*entity);
    return true;
}

bool EditorState::SetComponentPropertyOnEntity(int id, const std::string& componentType,
                                               const std::string& propertyName, const std::string& value) {
    Entity* entity = FindEntity(id);
    if (entity == nullptr || componentType.empty() || propertyName.empty()) {
        return false;
    }

    auto it = std::find_if(entity->components.begin(), entity->components.end(),
                           [&componentType](const Component& component) {
                               return component.type == componentType;
                           });
    if (it == entity->components.end()) {
        return false;
    }

    SetComponentProperty(*it, propertyName, value);
    if (componentType == "Transform") {
        entity->position = ComponentVec3Property(*it, "position", entity->position);
        entity->rotation = ComponentVec3Property(*it, "rotation", entity->rotation);
        entity->scale = ComponentVec3Property(*it, "scale", entity->scale);
    }
    SyncTransformComponent(*entity);
    return true;
}

SceneValidationResult EditorState::ValidateScene() const {
    return ValidateEngineScene(entities_);
}

SceneValidationResult EditorState::ValidateProject() const {
    SceneValidationResult result;
    if (projectRootPath_.empty() || !std::filesystem::exists(projectRootPath_)) {
        result.ok = false;
        result.diagnostics.push_back("Project root is missing: " + projectRootPath_.string());
    }
    if (projectFilePath_.empty()) {
        result.ok = false;
        result.diagnostics.push_back("Project file path is empty.");
    }
    if (sceneFilePath_.empty()) {
        result.ok = false;
        result.diagnostics.push_back("Active scene path is empty.");
    }
    if (assetRootPath_.empty() || !std::filesystem::exists(assetRootPath_)) {
        result.ok = false;
        result.diagnostics.push_back("Asset root is missing: " + assetRootPath_.string());
    }
    if (sceneRootPath_.empty() || !std::filesystem::exists(sceneRootPath_)) {
        result.ok = false;
        result.diagnostics.push_back("Scene root is missing: " + sceneRootPath_.string());
    }
    if (buildSettings_.targetPlatform != "Windows") {
        result.ok = false;
        result.diagnostics.push_back("Build target is unsupported: " + buildSettings_.targetPlatform);
    }
    if (buildSettings_.outputDirectory.empty()) {
        result.ok = false;
        result.diagnostics.push_back("Build output directory is empty.");
    }
    if (buildSettings_.executableName.empty()) {
        result.ok = false;
        result.diagnostics.push_back("Build executable name is empty.");
    }
    for (const std::string& scene : buildSettings_.scenes) {
        const std::filesystem::path scenePath = AbsoluteOrProjectRelative(projectRootPath_, scene);
        if (!std::filesystem::exists(scenePath)) {
            result.ok = false;
            result.diagnostics.push_back("Build scene is missing: " + scene);
        }
    }
    if (buildSettings_.multiplayer.port < 1 || buildSettings_.multiplayer.port > 65535) {
        result.ok = false;
        result.diagnostics.push_back("Multiplayer port must be between 1 and 65535.");
    }
    if (buildSettings_.multiplayer.maxPlayers < 1) {
        result.ok = false;
        result.diagnostics.push_back("Multiplayer maxPlayers must be at least 1.");
    }
    const RenderProfileSettings* profile = ActiveRenderProfile();
    if (profile == nullptr) {
        result.ok = false;
        result.diagnostics.push_back("Active render profile is missing: " + renderingSettings_.activeProfile);
    } else {
        for (const std::string& requirement : profile->requirements) {
            if (requirement == "gpu-buffer-backend") {
                result.ok = false;
                result.diagnostics.push_back(profile->displayName +
                                             " requires a future GPU buffer backend; use Basic Built-in, 2D, or Lightweight 3D for now.");
            }
        }
    }
    if (renderingSettings_.graphicsBackend.allowed.empty()) {
        result.ok = false;
        result.diagnostics.push_back("Rendering backend list is empty.");
    }

    if (result.diagnostics.empty()) {
        result.diagnostics.push_back("validate.project passed: project paths, roots, and rendering settings are present.");
    }
    return result;
}

SceneValidationResult EditorState::ValidateAssets() const {
    SceneValidationResult result;
    for (const AssetRecord& asset : assetDatabase_) {
        if (asset.id.empty() || asset.name.empty()) {
            result.ok = false;
            result.diagnostics.push_back("Asset database contains an asset with missing id or name.");
            continue;
        }
        if (asset.license.empty() || asset.license == "unspecified") {
            result.ok = false;
            result.diagnostics.push_back("Asset " + asset.id + " has no explicit license metadata.");
        }
        if (!asset.importedPath.empty()) {
            const std::filesystem::path imported = ProjectRelativeOrAbsolute(projectRootPath_, asset.importedPath);
            if (!std::filesystem::exists(imported)) {
                result.ok = false;
                result.diagnostics.push_back("Imported asset file is missing for " + asset.id + ": " + asset.importedPath);
            }
        }
        const std::vector<std::string> requiredArtifacts{asset.resourcePath, asset.metadataPath, asset.thumbnailPath};
        for (const std::string& artifact : requiredArtifacts) {
            if (artifact.empty()) {
                result.ok = false;
                result.diagnostics.push_back("Asset " + asset.id + " is missing import artifact metadata.");
                continue;
            }
            const std::filesystem::path artifactPath = ProjectRelativeOrAbsolute(projectRootPath_, artifact);
            if (!std::filesystem::exists(artifactPath)) {
                result.ok = false;
                result.diagnostics.push_back("Asset " + asset.id + " import artifact is missing: " + artifact);
            }
        }
        for (const std::string& dependency : asset.dependencies) {
            const std::filesystem::path dependencyPath = ProjectRelativeOrAbsolute(projectRootPath_, dependency);
            if (!std::filesystem::exists(dependencyPath)) {
                result.ok = false;
                result.diagnostics.push_back("Asset " + asset.id + " dependency is missing: " + dependency);
            }
        }
    }

    if (result.diagnostics.empty()) {
        result.diagnostics.push_back("validate.assets passed: " + std::to_string(assetDatabase_.size()) +
                                     " asset record(s) checked.");
    }
    return result;
}

SceneValidationResult EditorState::ValidateScripts() const {
    SceneValidationResult result;
    const ScriptCompileResult compile = CompileScripts();
    result.ok = compile.ok;
    result.diagnostics = FormatScriptCompileDiagnostics(compile);

    auto findSource = [&compile](const std::string& relativePath) -> const ScriptSourceUnit* {
        const std::string key = ToLower(NormalizePathString(relativePath));
        auto it = std::find_if(compile.sources.begin(), compile.sources.end(), [&key](const ScriptSourceUnit& source) {
            return ToLower(NormalizePathString(source.relativePath)) == key;
        });
        return it == compile.sources.end() ? nullptr : &(*it);
    };

    for (const Entity& entity : entities_) {
        for (const Component& component : entity.components) {
            const std::string scriptPath = ComponentStringProperty(component, "scriptPath");
            if (scriptPath.empty()) {
                continue;
            }

            const std::filesystem::path absoluteScript = ProjectRelativeOrAbsolute(projectRootPath_, scriptPath);
            if (!IsPathInside(projectRootPath_, absoluteScript)) {
                result.ok = false;
                result.diagnostics.push_back(entity.name + "." + component.type + " references a script outside the project.");
                continue;
            }
            if (!std::filesystem::exists(absoluteScript)) {
                result.ok = false;
                result.diagnostics.push_back(entity.name + "." + component.type + " script is missing: " + scriptPath);
                continue;
            }

            const std::string relativeScript = RelativeToProjectString(projectRootPath_, absoluteScript);
            const ScriptSourceUnit* source = findSource(relativeScript);
            if (source == nullptr) {
                result.ok = false;
                result.diagnostics.push_back(entity.name + "." + component.type +
                                             " script was not discovered by the compiler: " + scriptPath);
                continue;
            }
            if (source->editorOnly) {
                result.ok = false;
                result.diagnostics.push_back(entity.name + "." + component.type +
                                             " references an editor-only script: " + scriptPath);
                continue;
            }
            if (std::find(source->declarations.begin(), source->declarations.end(), component.type) ==
                source->declarations.end()) {
                result.ok = false;
                result.diagnostics.push_back(entity.name + "." + component.type +
                                             " does not match any declaration in " + scriptPath + ".");
            }
        }
    }

    if (result.ok) {
        result.diagnostics.push_back("validate.scripts passed: scripts compiled and script references are valid.");
    }
    return result;
}

EditorState::SceneSnapshot EditorState::CaptureSceneSnapshot(std::string label) const {
    SceneSnapshot snapshot;
    snapshot.label = std::move(label);
    snapshot.sceneName = sceneName_;
    snapshot.sceneFilePath = sceneFilePath_;
    snapshot.nextEntityId = nextEntityId_;
    snapshot.selectedEntityId = selectedEntityId_;
    snapshot.entities = entities_;
    return snapshot;
}

void EditorState::RestoreSceneSnapshot(const SceneSnapshot& snapshot) {
    sceneName_ = snapshot.sceneName;
    sceneFilePath_ = snapshot.sceneFilePath;
    nextEntityId_ = snapshot.nextEntityId;
    selectedEntityId_ = snapshot.selectedEntityId;
    entities_ = snapshot.entities;
}

bool EditorState::PushUndoSnapshot(std::string label) {
    if (!IsEditMode()) {
        return false;
    }
    undoStack_.push_back(CaptureSceneSnapshot(std::move(label)));
    if (undoStack_.size() > 64) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
    return true;
}

bool EditorState::UndoSceneEdit() {
    if (!IsEditMode() || undoStack_.empty()) {
        return false;
    }
    redoStack_.push_back(CaptureSceneSnapshot("redo"));
    const SceneSnapshot snapshot = undoStack_.back();
    undoStack_.pop_back();
    RestoreSceneSnapshot(snapshot);
    AddActivity("scene.undo", "Complete", snapshot.label);
    AddLog(LogLevel::Info, "Undo restored scene snapshot: " + snapshot.label);
    return true;
}

bool EditorState::RedoSceneEdit() {
    if (!IsEditMode() || redoStack_.empty()) {
        return false;
    }
    undoStack_.push_back(CaptureSceneSnapshot("undo before redo"));
    const SceneSnapshot snapshot = redoStack_.back();
    redoStack_.pop_back();
    RestoreSceneSnapshot(snapshot);
    AddActivity("scene.redo", "Complete", snapshot.label);
    AddLog(LogLevel::Info, "Redo restored scene snapshot.");
    return true;
}

void EditorState::MarkSceneDirty(std::string reason) {
    sceneDirty_ = true;
    if (!reason.empty()) {
        AddActivity("scene.dirty", "Unsaved", std::move(reason));
    }
}

void EditorState::SelectEntity(int id) {
    selectedEntityId_ = id;
}

const char* EditorState::PlayStateLabel() const {
    return aine::PlayStateLabel(runtime_.PlayState());
}

bool EditorState::BeginPlayMode() {
    if (runtime_.PlayState() == EditorPlayState::Playing) {
        return false;
    }
    if (runtime_.PlayState() == EditorPlayState::Paused) {
        return ResumePlayMode();
    }

    const SceneValidationResult scripts = ValidateScripts();
    if (!scripts.ok) {
        for (const std::string& diagnostic : scripts.diagnostics) {
            AddLog(LogLevel::Error, diagnostic);
        }
        AddActivity("play.mode", "Blocked", "Script compile failed.");
        return false;
    }

    playSnapshotEntities_ = entities_;
    playSnapshotNextEntityId_ = nextEntityId_;
    playSnapshotSelectedEntityId_ = selectedEntityId_;
    hasPlaySnapshot_ = true;

    ScriptRuntimeHostConfig scriptHostConfig;
    scriptHostConfig.enabled = true;
    scriptHostConfig.projectRoot = projectRootPath_;
    scriptHostConfig.manifestPath = projectRootPath_ / "Library" / "Scripts" / "compile_manifest.json";
    scriptHostConfig.assemblyPath = projectRootPath_ / "Library" / "Scripts" / "bin" / "AINative.GameScripts.dll";
    scriptHostConfig.outputRoot = projectRootPath_ / "Library" / "Scripts" / "RuntimeHost";

    if (!runtime_.Begin(entities_, RuntimeCallbacks(), scriptHostConfig)) {
        entities_ = playSnapshotEntities_;
        nextEntityId_ = playSnapshotNextEntityId_;
        selectedEntityId_ = playSnapshotSelectedEntityId_;
        playSnapshotEntities_.clear();
        hasPlaySnapshot_ = false;
        AddActivity("play.mode", "Blocked", "Script runtime host failed.");
        return false;
    }
    return true;
}

bool EditorState::PausePlayMode() {
    return runtime_.Pause(entities_, RuntimeCallbacks());
}

bool EditorState::ResumePlayMode() {
    return runtime_.Resume(entities_, RuntimeCallbacks());
}

bool EditorState::StopPlayMode() {
    if (runtime_.PlayState() == EditorPlayState::EditMode) {
        return false;
    }

    const bool stopped = runtime_.Stop(entities_, RuntimeCallbacks());

    if (hasPlaySnapshot_) {
        entities_ = playSnapshotEntities_;
        nextEntityId_ = playSnapshotNextEntityId_;
        selectedEntityId_ = playSnapshotSelectedEntityId_;
    }
    playSnapshotEntities_.clear();
    hasPlaySnapshot_ = false;
    return stopped;
}

void EditorState::UpdatePlayMode(float deltaSeconds) {
    PerformanceScope scope(profiler_, PerformanceDomain::Runtime, "Runtime.UpdatePlayMode");
    runtime_.Update(entities_, deltaSeconds, RuntimeCallbacks());
}

bool EditorState::StepPhysicsOnce() {
    return runtime_.StepPhysicsOnce(entities_, RuntimeCallbacks());
}

void EditorState::SetCharacterControllerInput(CharacterControllerInput input) {
    runtime_.SetCharacterControllerInput(input);
}

void EditorState::SetRuntimeInputState(RuntimeInputState input) {
    runtime_.SetRuntimeInputState(std::move(input));
}

std::string EditorState::ControlledRuntimeEntityName() const {
    const Entity* controlled = FindEntity(runtime_.ControlledEntityId());
    return controlled == nullptr ? std::string{} : controlled->name;
}

RuntimeHostCallbacks EditorState::RuntimeCallbacks() {
    RuntimeHostCallbacks callbacks;
    callbacks.profiler = profiler_;
    callbacks.syncTransform = [this](Entity& entity) {
        SyncTransformComponent(entity);
    };
    callbacks.editorLog = [this](LogLevel level, std::string message) {
        AddLog(level, std::move(message));
    };
    callbacks.activity = [this](std::string label, std::string status, std::string detail) {
        AddActivity(std::move(label), std::move(status), std::move(detail));
    };
    return callbacks;
}

void EditorState::AddLog(LogLevel level, std::string message) {
    logs_.push_back({nextLogSequence_++, std::chrono::system_clock::now(), level, std::move(message)});
}

void EditorState::AddChat(ChatSender sender, std::string message) {
    if (chatConversations_.empty() || FindConversationById(chatConversations_, activeChatConversationId_) == nullptr) {
        CreateChatConversation("New chat");
    }
    ChatConversation* conversation = FindConversationById(chatConversations_, activeChatConversationId_);
    if (conversation == nullptr) {
        return;
    }
    if (sender == ChatSender::User && (conversation->title.empty() || conversation->title == "New chat")) {
        conversation->title = ConversationTitleFromMessage(message);
    }
    conversation->messages.push_back({sender, std::move(message)});
}

std::string EditorState::CreateChatConversation(std::string title) {
    ChatConversation conversation;
    conversation.id = "chat-" + std::to_string(nextChatConversationIndex_++);
    conversation.title = TrimText(title).empty() ? "New chat" : ConversationTitleFromMessage(title);
    conversation.projectName = ProjectName();
    chatConversations_.push_back(std::move(conversation));
    activeChatConversationId_ = chatConversations_.back().id;
    return activeChatConversationId_;
}

bool EditorState::SwitchChatConversation(const std::string& id) {
    if (FindConversationById(chatConversations_, id) == nullptr) {
        return false;
    }
    activeChatConversationId_ = id;
    return true;
}

bool EditorState::RenameChatConversation(const std::string& id, const std::string& title) {
    ChatConversation* conversation = FindConversationById(chatConversations_, id);
    if (conversation == nullptr) {
        return false;
    }
    conversation->title = ConversationTitleFromMessage(title);
    return true;
}

bool EditorState::SetChatConversationPinned(const std::string& id, bool pinned) {
    ChatConversation* conversation = FindConversationById(chatConversations_, id);
    if (conversation == nullptr) {
        return false;
    }
    conversation->pinned = pinned;
    return true;
}

bool EditorState::DeleteChatConversation(const std::string& id) {
    if (chatConversations_.size() <= 1) {
        return false;
    }
    const auto it = std::find_if(chatConversations_.begin(), chatConversations_.end(), [&](const ChatConversation& conversation) {
        return conversation.id == id;
    });
    if (it == chatConversations_.end()) {
        return false;
    }
    const bool deletingActive = it->id == activeChatConversationId_;
    chatConversations_.erase(it);
    if (deletingActive) {
        activeChatConversationId_ = chatConversations_.empty() ? std::string{} : chatConversations_.front().id;
    }
    if (chatConversations_.empty()) {
        CreateChatConversation("New chat");
    }
    return true;
}

void EditorState::LoadChatConversations(std::vector<ChatConversation> conversations, std::string activeId) {
    chatConversations_.clear();
    for (ChatConversation& conversation : conversations) {
        if (conversation.id.empty()) {
            conversation.id = "chat-" + std::to_string(nextChatConversationIndex_++);
        }
        if (conversation.title.empty()) {
            conversation.title = "New chat";
        }
        if (conversation.projectName.empty()) {
            conversation.projectName = ProjectName();
        }
        chatConversations_.push_back(std::move(conversation));
    }

    int maxIndex = 0;
    for (const ChatConversation& conversation : chatConversations_) {
        if (conversation.id.rfind("chat-", 0) == 0) {
            try {
                maxIndex = std::max(maxIndex, std::stoi(conversation.id.substr(5)));
            } catch (const std::exception&) {
            }
        }
    }
    nextChatConversationIndex_ = std::max(nextChatConversationIndex_, maxIndex + 1);

    if (chatConversations_.empty()) {
        CreateChatConversation("New chat");
        return;
    }
    activeChatConversationId_ = FindConversationById(chatConversations_, activeId) == nullptr
                                    ? chatConversations_.front().id
                                    : std::move(activeId);
}

void EditorState::AddActivity(std::string label, std::string status, std::string detail) {
    agentActivities_.push_back({std::move(label), std::move(status), std::move(detail)});
    if (agentActivities_.size() > 64) {
        agentActivities_.erase(agentActivities_.begin());
    }
}

void EditorState::ClearConsole() {
    logs_.clear();
}

const std::vector<ChatMessage>& EditorState::ChatHistory() const {
    const ChatConversation* conversation = FindConversationById(chatConversations_, activeChatConversationId_);
    if (conversation != nullptr) {
        return conversation->messages;
    }
    static const std::vector<ChatMessage> empty;
    return empty;
}

const ChatConversation* EditorState::ActiveChatConversation() const {
    return FindConversationById(chatConversations_, activeChatConversationId_);
}

std::string EditorState::ProjectRootPathString() const {
    return projectRootPath_.string();
}

std::string EditorState::ProjectFilePathString() const {
    return projectFilePath_.string();
}

std::string EditorState::SceneFilePathString() const {
    return sceneFilePath_.string();
}

std::string EditorState::AssetRootPathString() const {
    return assetRootPath_.string();
}

std::string EditorState::SceneRootPathString() const {
    return sceneRootPath_.string();
}

std::string EditorState::GameSaveRootPathString() const {
    return gameSaveRootPath_.string();
}

std::string EditorState::AssetDatabasePathString() const {
    return AssetDatabasePath().string();
}

std::filesystem::path EditorState::DefaultProjectRootPath() const {
    return std::filesystem::current_path() / "projects" / "DefaultProject";
}

bool EditorState::NewProject(const std::filesystem::path& projectRoot, std::string* error) {
    SetDefaultPathsForRoot(projectRoot.empty() ? DefaultProjectRootPath() : projectRoot);
    buildSettings_ = ProjectBuildSettings{};
    NormalizeBuildSettings();
    renderingSettings_ = DefaultProjectRenderingSettings();
    projectSettings_ = DefaultEditorProjectSettings();
    EnsureProjectFolders(error);
    assetDatabase_.clear();
    plugins_.clear();
    nextAssetId_ = 1;
    ClearUndoRedo();
    SeedStarterScene();
    RefreshProjectTree();
    return SaveProject(error);
}

bool EditorState::OpenProject(const std::filesystem::path& projectFileOrRoot, std::string* error) {
    bool resolvedPartialPath = false;
    std::filesystem::path projectFile = ResolveProjectFilePath(projectFileOrRoot, &resolvedPartialPath);

    if (!std::filesystem::exists(projectFile)) {
        if (error != nullptr) {
            *error = "Project file does not exist: " + projectFile.string() +
                     ". Open a project root folder or the full file path: " + std::string(kProjectFileName) + ".";
        }
        return false;
    }

    try {
        std::ifstream input(projectFile);
        json document = json::parse(input);

        projectFilePath_ = std::filesystem::absolute(projectFile);
        projectRootPath_ = projectFilePath_.parent_path();
        projectName_ = document.value("projectName", "Untitled AI Native Project");

        const std::filesystem::path activeScene =
            document.value<std::string>("activeScene", std::string(kDefaultSceneRoot) + "/Prototype.scene.json");
        const std::string assetRoot = DirectorySetting(document, "assetRoot", "assetRoot", kDefaultAssetRoot);
        const std::string sceneRoot =
            DirectorySetting(document, "sceneRoot", "sceneRoot", SceneRootFallbackFromActiveScene(activeScene).generic_string());
        const std::string gameSaveRoot =
            DirectorySetting(document, "saveDataRoot", "gameSaveRoot", kDefaultGameSaveRoot);
        assetRootPath_ = std::filesystem::absolute(AbsoluteOrProjectRelative(projectRootPath_, assetRoot));
        sceneRootPath_ = std::filesystem::absolute(AbsoluteOrProjectRelative(projectRootPath_, sceneRoot));
        gameSaveRootPath_ = std::filesystem::absolute(AbsoluteOrProjectRelative(projectRootPath_, gameSaveRoot));
        buildSettings_ = document.contains("buildSettings") ? BuildSettingsFromJson(document.at("buildSettings"))
                                                            : ProjectBuildSettings{};
        renderingSettings_ = document.contains("rendering") ? ProjectRenderingFromJson(document.at("rendering"))
                                                            : DefaultProjectRenderingSettings();
        projectSettings_ = document.contains("projectSettings") ? ProjectSettingsFromJson(document.at("projectSettings"))
                                                                : DefaultEditorProjectSettings();

        plugins_.clear();
        if (document.contains("plugins") && document.at("plugins").is_array()) {
            for (const json& pluginJson : document.at("plugins")) {
                PluginRecord plugin = PluginRecordFromJson(pluginJson);
                if (!plugin.name.empty()) {
                    plugins_.push_back(plugin);
                }
            }
        } else if (document.contains("enabledPlugins") && document.at("enabledPlugins").is_array()) {
            for (const json& pluginName : document.at("enabledPlugins")) {
                if (pluginName.is_string() && !pluginName.get<std::string>().empty()) {
                    const std::string name = pluginName.get<std::string>();
                    const std::filesystem::path pluginPath = std::filesystem::path("Plugins") / "GeneratedTools" / name;
                    PluginRecord record;
                    record.name = name;
                    record.path = pluginPath.generic_string();
                    record.manifestPath = (pluginPath / "plugin.json").generic_string();
                    record.installPath = record.path;
                    record.enabled = true;
                    record.installed = true;
                    plugins_.push_back(std::move(record));
                }
            }
        }

        sceneFilePath_ = AbsoluteOrProjectRelative(projectRootPath_, activeScene);
        NormalizeBuildSettings();
        if (!LoadSceneFile(sceneFilePath_, error)) {
            return false;
        }
        EnsureSceneTagLayerValuesAreKnown();
        SyncPhysicsSettingsComponents();

        EnsureProjectFolders(error);
        if (!LoadAssetDatabase(error)) {
            return false;
        }
        ClearUndoRedo();
        RefreshProjectTree();
        if (resolvedPartialPath) {
            AddLog(LogLevel::Warning,
                   "Open project path was incomplete; resolved to " + projectFilePath_.string() + ".");
        }
        AddLog(LogLevel::Info, "Opened project " + projectFilePath_.string() + ".");
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::SaveProject(std::string* error) {
    try {
        NormalizeBuildSettings();
        NormalizeProjectRenderingSettings(&renderingSettings_);
        EnsureSceneTagLayerValuesAreKnown();
        SyncPhysicsSettingsComponents();
        EnsureProjectFolders(error);
        if (!SaveSceneFile(sceneFilePath_, error)) {
            return false;
        }

        const std::string relativeAssetRoot = RelativeToProjectString(projectRootPath_, assetRootPath_);
        const std::string relativeSceneRoot = RelativeToProjectString(projectRootPath_, sceneRootPath_);
        const std::string relativeGameSaveRoot = RelativeToProjectString(projectRootPath_, gameSaveRootPath_);
        const std::filesystem::path relativeScene = RelativeToProject(projectRootPath_, sceneFilePath_);
        json plugins = json::array();
        json enabledPlugins = json::array();
        for (const PluginRecord& plugin : plugins_) {
            plugins.push_back(PluginRecordToJson(plugin));
            if (plugin.enabled) {
                enabledPlugins.push_back(plugin.name);
            }
        }
        json project = {
            {"format", "aine.project"},
            {"version", 1},
            {"projectName", projectName_},
            {"engineVersion", "0.1.0"},
            {"assetRoot", relativeAssetRoot},
            {"sceneRoot", relativeSceneRoot},
            {"gameSaveRoot", relativeGameSaveRoot},
            {"directories",
             {
                 {"assetRoot", relativeAssetRoot},
                 {"sceneRoot", relativeSceneRoot},
                 {"saveDataRoot", relativeGameSaveRoot},
             }},
            {"activeScene", relativeScene.generic_string()},
            {"scenes", json::array({relativeScene.generic_string()})},
            {"assetDatabase", RelativeToProjectString(projectRootPath_, AssetDatabasePath())},
            {"rendering", ProjectRenderingToJson(renderingSettings_)},
            {"buildSettings", BuildSettingsToJson(buildSettings_)},
            {"projectSettings", ProjectSettingsToJson(projectSettings_)},
            {"buildTargets",
             json::array({
                 {
                     {"platform", buildSettings_.targetPlatform},
                     {"architecture", buildSettings_.architecture},
                     {"configuration", buildSettings_.configuration},
                     {"outputDirectory", buildSettings_.outputDirectory},
                 },
             })},
            {"enabledPlugins", enabledPlugins},
            {"plugins", plugins},
        };

        if (!SaveAssetDatabase(error)) {
            return false;
        }

        std::ofstream output(projectFilePath_);
        output << project.dump(2) << '\n';

        sceneDirty_ = false;
        RefreshProjectTree();
        AddLog(LogLevel::Info, "Saved project file: " + projectFilePath_.string());
        AddLog(LogLevel::Info, "Saved scene file: " + sceneFilePath_.string());
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::SaveProjectAs(const std::filesystem::path& projectRoot, std::string* error) {
    if (projectRoot.empty()) {
        if (error != nullptr) {
            *error = "Project Save As folder path is empty.";
        }
        return false;
    }

    const std::filesystem::path previousProjectRoot = projectRootPath_;
    const std::filesystem::path previousProjectFile = projectFilePath_;
    const std::filesystem::path previousAssetRoot = assetRootPath_;
    const std::filesystem::path previousSceneRoot = sceneRootPath_;
    const std::filesystem::path previousGameSaveRoot = gameSaveRootPath_;
    const std::filesystem::path previousSceneFile = sceneFilePath_;
    const std::string previousSceneName = sceneName_;
    const bool previousSceneDirty = sceneDirty_;

    auto restorePreviousPaths = [&]() {
        projectRootPath_ = previousProjectRoot;
        projectFilePath_ = previousProjectFile;
        assetRootPath_ = previousAssetRoot;
        sceneRootPath_ = previousSceneRoot;
        gameSaveRootPath_ = previousGameSaveRoot;
        sceneFilePath_ = previousSceneFile;
        sceneName_ = previousSceneName;
        sceneDirty_ = previousSceneDirty;
        RefreshProjectTree();
    };

    try {
        std::filesystem::path targetRoot = projectRoot;
        if (targetRoot.filename() == kProjectFileName) {
            targetRoot = targetRoot.parent_path();
        }
        targetRoot = std::filesystem::absolute(targetRoot).lexically_normal();

        const bool sameRoot = LowerPathString(targetRoot) == LowerPathString(previousProjectRoot);
        if (!sameRoot) {
            if (LexicallyInsideOrSame(previousProjectRoot, targetRoot) ||
                LexicallyInsideOrSame(targetRoot, previousProjectRoot)) {
                if (error != nullptr) {
                    *error = "Project Save As target must be outside the current project folder and cannot contain it.";
                }
                return false;
            }

            std::filesystem::create_directories(targetRoot);
            if (std::filesystem::exists(previousProjectRoot)) {
                const std::filesystem::copy_options options =
                    std::filesystem::copy_options::recursive |
                    std::filesystem::copy_options::overwrite_existing |
                    std::filesystem::copy_options::skip_symlinks;
                for (const std::filesystem::directory_entry& entry :
                     std::filesystem::directory_iterator(previousProjectRoot)) {
                    if (SkipProjectSaveAsCopyEntry(entry)) {
                        continue;
                    }
                    std::filesystem::copy(entry.path(), targetRoot / entry.path().filename(), options);
                }
            }
        }

        const std::filesystem::path assetRootRelative = RelativeToProject(previousProjectRoot, previousAssetRoot);
        const std::filesystem::path sceneRootRelative = RelativeToProject(previousProjectRoot, previousSceneRoot);
        const std::filesystem::path gameSaveRootRelative = RelativeToProject(previousProjectRoot, previousGameSaveRoot);
        const std::filesystem::path sceneFileRelative = RelativeToProject(previousProjectRoot, previousSceneFile);

        projectRootPath_ = targetRoot;
        projectFilePath_ = projectRootPath_ / kProjectFileName;
        assetRootPath_ = std::filesystem::absolute(AbsoluteOrProjectRelative(projectRootPath_, assetRootRelative));
        sceneRootPath_ = std::filesystem::absolute(AbsoluteOrProjectRelative(projectRootPath_, sceneRootRelative));
        gameSaveRootPath_ = std::filesystem::absolute(AbsoluteOrProjectRelative(projectRootPath_, gameSaveRootRelative));
        sceneFilePath_ = std::filesystem::absolute(AbsoluteOrProjectRelative(projectRootPath_, sceneFileRelative));
        sceneName_ = sceneFilePath_.filename().string();

        if (!SaveProject(error)) {
            restorePreviousPaths();
            return false;
        }

        AddLog(LogLevel::Info, "Saved project as: " + projectRootPath_.string());
        return true;
    } catch (const std::exception& exception) {
        restorePreviousPaths();
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::CreateScene(const std::filesystem::path& scenePathOrName, std::string* error) {
    std::filesystem::path scenePath = scenePathOrName.empty() ? std::filesystem::path("Prototype.scene.json") : scenePathOrName;
    if (scenePath.is_absolute()) {
        sceneFilePath_ = scenePath;
    } else if (scenePath.has_parent_path()) {
        sceneFilePath_ = projectRootPath_ / scenePath;
    } else {
        sceneFilePath_ = sceneRootPath_ / scenePath;
    }
    if (sceneFilePath_.extension().empty()) {
        sceneFilePath_ += ".scene.json";
    }
    sceneRootPath_ = sceneFilePath_.parent_path();
    sceneName_ = sceneFilePath_.filename().string();
    SeedStarterScene();
    ClearUndoRedo();
    return SaveProject(error);
}

bool EditorState::OpenScene(const std::filesystem::path& scenePath, std::string* error) {
    if (scenePath.empty()) {
        if (error != nullptr) {
            *error = "Scene path is empty.";
        }
        return false;
    }

    const std::filesystem::path resolved = scenePath.is_absolute() ? scenePath : ProjectRelativeOrAbsolute(projectRootPath_, scenePath);
    if (!LoadSceneFile(resolved, error)) {
        return false;
    }
    ClearUndoRedo();
    RefreshProjectTree();
    return true;
}

bool EditorState::SaveScene(std::string* error) {
    return SaveProject(error);
}

bool EditorState::SaveSceneAs(const std::filesystem::path& scenePath, std::string* error) {
    if (scenePath.empty()) {
        if (error != nullptr) {
            *error = "Scene path is empty.";
        }
        return false;
    }

    if (scenePath.is_absolute()) {
        sceneFilePath_ = scenePath;
    } else if (scenePath.has_parent_path()) {
        sceneFilePath_ = projectRootPath_ / scenePath;
    } else {
        sceneFilePath_ = sceneRootPath_ / scenePath;
    }
    sceneRootPath_ = sceneFilePath_.parent_path();
    sceneName_ = sceneFilePath_.filename().string();
    return SaveProject(error);
}

bool EditorState::SetGameSaveRootPath(const std::filesystem::path& saveRoot, std::string* error) {
    if (saveRoot.empty()) {
        if (error != nullptr) {
            *error = "Game save folder path is empty.";
        }
        return false;
    }

    try {
        gameSaveRootPath_ = std::filesystem::absolute(AbsoluteOrProjectRelative(projectRootPath_, saveRoot));
        std::filesystem::create_directories(gameSaveRootPath_);
        AddLog(LogLevel::Info, "Game save folder set to: " + GameSaveRootPathString());
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

void EditorState::RefreshProjectTree() {
    projectTree_ = BuildProjectTree();
}

std::string EditorState::GetProjectStateSummary() const {
    std::ostringstream stream;
    stream << projectName_ << " | scene=" << sceneName_ << " | entities=" << entities_.size();
    if (!gameSaveRootPath_.empty()) {
        stream << " | saves=" << RelativeToProjectString(projectRootPath_, gameSaveRootPath_);
    }
    stream << " | build=" << buildSettings_.targetPlatform << " "
           << buildSettings_.configuration << " -> " << buildSettings_.outputDirectory;
    if (const RenderProfileSettings* profile = ActiveRenderProfile()) {
        stream << " | renderer=" << profile->displayName;
    } else {
        stream << " | renderer=" << renderingSettings_.activeProfile;
    }
    if (buildSettings_.multiplayer.enabled) {
        stream << " | multiplayer=" << buildSettings_.multiplayer.topology
               << " " << buildSettings_.multiplayer.bindAddress << ":" << buildSettings_.multiplayer.port
               << " max=" << buildSettings_.multiplayer.maxPlayers;
    }
    if (selectedEntityId_ != 0) {
        const Entity* selected = FindEntity(selectedEntityId_);
        stream << " | selected=" << (selected == nullptr ? "none" : selected->name);
    }
    return stream.str();
}

void EditorState::SetBuildSettings(ProjectBuildSettings settings) {
    buildSettings_ = std::move(settings);
    NormalizeBuildSettings();
}

const RenderProfileSettings* EditorState::ActiveRenderProfile() const {
    return FindRenderProfileInList(renderingSettings_.profiles, renderingSettings_.activeProfile);
}

const RenderProfileSettings* EditorState::FindRenderProfile(const std::string& id) const {
    return FindRenderProfileInList(renderingSettings_.profiles, id);
}

std::vector<std::string> EditorState::RenderProfileIds() const {
    std::vector<std::string> ids;
    for (const RenderProfileSettings& profile : renderingSettings_.profiles) {
        ids.push_back(profile.id);
    }
    return ids;
}

bool EditorState::SetActiveRenderProfile(const std::string& profileId, std::string* error) {
    const std::string normalized = NormalizeRenderProfileId(profileId);
    if (FindRenderProfileInList(renderingSettings_.profiles, normalized) == nullptr) {
        if (error != nullptr) {
            *error = "Unknown render profile: " + profileId;
        }
        return false;
    }
    ProjectRenderingSettings updated = renderingSettings_;
    updated.activeProfile = normalized;
    NormalizeProjectRenderingSettings(&updated);
    renderingSettings_ = std::move(updated);
    return true;
}

std::filesystem::path EditorState::BuildOutputDirectoryPath() const {
    if (buildSettings_.outputDirectory.empty()) {
        return projectRootPath_ / DefaultBuildOutputDirectoryForPlatform(buildSettings_.targetPlatform);
    }
    const std::filesystem::path output = buildSettings_.outputDirectory;
    return output.is_absolute() ? output : projectRootPath_ / output;
}

std::string EditorState::BuildOutputDirectoryPathString() const {
    return BuildOutputDirectoryPath().string();
}

void EditorState::NormalizeBuildSettings() {
    buildSettings_.targetPlatform = NormalizeBuildTargetPlatform(buildSettings_.targetPlatform);
    if (buildSettings_.architecture.empty()) {
        buildSettings_.architecture = "x64";
    }
    if (buildSettings_.configuration != "Release" && buildSettings_.configuration != "Debug") {
        buildSettings_.configuration = "Debug";
    }
    if (buildSettings_.outputDirectory.empty()) {
        buildSettings_.outputDirectory = DefaultBuildOutputDirectoryForPlatform(buildSettings_.targetPlatform);
    }
    if (buildSettings_.releaseChannel.empty()) {
        buildSettings_.releaseChannel = "internal";
    }
    if (buildSettings_.signingMode.empty()) {
        buildSettings_.signingMode = "Unsigned";
    }
    buildSettings_.executableName = SanitizeFileStem(buildSettings_.executableName, SanitizeFileStem(projectName_, "AINativeGame"));
    if (buildSettings_.scenes.empty() && !sceneFilePath_.empty()) {
        buildSettings_.scenes.push_back(RelativeToProjectString(projectRootPath_, sceneFilePath_));
    }
    if (buildSettings_.multiplayer.topology.empty()) {
        buildSettings_.multiplayer.topology = buildSettings_.multiplayer.enabled ? "Host Client" : "Single Player";
    }
    if (!buildSettings_.multiplayer.enabled) {
        buildSettings_.multiplayer.topology = "Single Player";
    } else if (buildSettings_.multiplayer.topology == "Single Player") {
        buildSettings_.multiplayer.topology = "Host Client";
    }
    if (buildSettings_.multiplayer.transport.empty()) {
        buildSettings_.multiplayer.transport = "Local Loopback";
    }
    if (buildSettings_.multiplayer.bindAddress.empty()) {
        buildSettings_.multiplayer.bindAddress = "127.0.0.1";
    }
    buildSettings_.multiplayer.port = std::clamp(buildSettings_.multiplayer.port, 1, 65535);
    buildSettings_.multiplayer.maxPlayers = std::clamp(buildSettings_.multiplayer.maxPlayers, 1, 256);
}

bool EditorState::PackagePlayerBuild(const std::filesystem::path& playerExecutable,
                                     ProjectBuildReport* report,
                                     std::string* error) {
    NormalizeBuildSettings();
    ProjectBuildReport localReport;
    localReport.targetPlatform = buildSettings_.targetPlatform;
    localReport.outputDirectory = BuildOutputDirectoryPath();
    auto finish = [&](bool ok, const std::string& message = {}) {
        localReport.ok = ok;
        if (!message.empty()) {
            localReport.diagnostics.push_back(message);
        }
        if (report != nullptr) {
            *report = localReport;
        }
        if (!ok && error != nullptr) {
            *error = message.empty() ? "Build package failed." : message;
        }
        return ok;
    };

    if (playerExecutable.empty() || !std::filesystem::exists(playerExecutable)) {
        return finish(false, BuildTargetUnavailableMessage(buildSettings_.targetPlatform, playerExecutable));
    }

    SceneValidationResult projectValidation = ValidateProject();
    if (!projectValidation.ok) {
        return finish(false, "Project validation failed before build: " +
                                 (projectValidation.diagnostics.empty() ? std::string{} : projectValidation.diagnostics.front()));
    }

    std::string saveError;
    if (!SaveProject(&saveError)) {
        return finish(false, "Could not save project before build: " + saveError);
    }

    try {
        const std::filesystem::path outputDirectory = std::filesystem::absolute(BuildOutputDirectoryPath()).lexically_normal();
        const std::string targetPlatform = buildSettings_.targetPlatform;
        const std::string executableFilename =
            EnsureExecutableFilenameForPlatform(buildSettings_.executableName, targetPlatform);
        const bool isWindowsTarget = targetPlatform == "Windows";
        const bool isMacTarget = targetPlatform == "macOS";
        const std::filesystem::path appBundlePath =
            isMacTarget ? outputDirectory / (std::filesystem::path(buildSettings_.executableName).stem().string() + ".app")
                        : std::filesystem::path{};
        const std::filesystem::path dataDirectory =
            isMacTarget ? appBundlePath / "Contents" / "Resources" / "Data" : outputDirectory / "Data";
        std::filesystem::create_directories(outputDirectory);
        std::filesystem::create_directories(dataDirectory);
        std::filesystem::create_directories(outputDirectory / "Logs");

        localReport.outputDirectory = outputDirectory;
        if (isMacTarget) {
            std::filesystem::create_directories(appBundlePath / "Contents" / "MacOS");
            std::filesystem::create_directories(appBundlePath / "Contents" / "Resources");
            localReport.executablePath = appBundlePath / "Contents" / "MacOS" / executableFilename;
        } else {
            localReport.executablePath = outputDirectory / executableFilename;
        }
        std::filesystem::copy_file(playerExecutable, localReport.executablePath,
                                   std::filesystem::copy_options::overwrite_existing);
        ++localReport.copiedFileCount;
        if (!isWindowsTarget) {
            AddExecutablePermissions(localReport.executablePath);
        }
        if (isMacTarget) {
            const std::filesystem::path infoPlistPath = appBundlePath / "Contents" / "Info.plist";
            std::ofstream infoPlist(infoPlistPath);
            infoPlist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            infoPlist << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                         "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
            infoPlist << "<plist version=\"1.0\">\n<dict>\n";
            infoPlist << "  <key>CFBundleExecutable</key>\n  <string>" << executableFilename << "</string>\n";
            infoPlist << "  <key>CFBundleIdentifier</key>\n  <string>com.ainative."
                      << BuildIdentifierSegment(buildSettings_.executableName) << "</string>\n";
            infoPlist << "  <key>CFBundleName</key>\n  <string>" << buildSettings_.executableName << "</string>\n";
            infoPlist << "  <key>CFBundlePackageType</key>\n  <string>APPL</string>\n";
            infoPlist << "  <key>CFBundleShortVersionString</key>\n  <string>0.1.0</string>\n";
            infoPlist << "</dict>\n</plist>\n";
            localReport.diagnostics.push_back("macOS app bundle metadata written: " + infoPlistPath.string());
        }

        if (buildSettings_.copyProjectFiles) {
            const std::filesystem::path packagedAssetRoot =
                dataDirectory / SafeProjectRelativePath(projectRootPath_, assetRootPath_, "Assets");
            if (!CopyDirectoryTree(assetRootPath_, packagedAssetRoot, &localReport.copiedFileCount, error)) {
                return finish(false, error == nullptr ? "Asset copy failed." : *error);
            }

            if (!LexicallyInsideOrSame(assetRootPath_, sceneRootPath_)) {
                const std::filesystem::path packagedSceneRoot =
                    dataDirectory / SafeProjectRelativePath(projectRootPath_, sceneRootPath_, "Scenes");
                if (!CopyDirectoryTree(sceneRootPath_, packagedSceneRoot, &localReport.copiedFileCount, error)) {
                    return finish(false, error == nullptr ? "Scene copy failed." : *error);
                }
            }

            const std::filesystem::path projectSettings = projectRootPath_ / "ProjectSettings";
            if (std::filesystem::exists(projectSettings) &&
                !CopyDirectoryTree(projectSettings, dataDirectory / "ProjectSettings", &localReport.copiedFileCount, error)) {
                return finish(false, error == nullptr ? "ProjectSettings copy failed." : *error);
            }

            const std::filesystem::path plugins = projectRootPath_ / "Plugins";
            if (std::filesystem::exists(plugins) &&
                !CopyDirectoryTree(plugins, dataDirectory / "Plugins", &localReport.copiedFileCount, error)) {
                return finish(false, error == nullptr ? "Plugin copy failed." : *error);
            }

            std::filesystem::create_directories(dataDirectory / SafeProjectRelativePath(projectRootPath_, gameSaveRootPath_, "Saved"));
            localReport.packagedProjectFile = dataDirectory / kProjectFileName;
            std::filesystem::copy_file(projectFilePath_, localReport.packagedProjectFile,
                                       std::filesystem::copy_options::overwrite_existing);
            ++localReport.copiedFileCount;
        }

        localReport.manifestPath = outputDirectory / "build-manifest.ainebuild.json";
        const std::filesystem::path multiplayerSettingsPath = dataDirectory / "ProjectSettings" / "MultiplayerSettings.json";
        std::filesystem::create_directories(multiplayerSettingsPath.parent_path());
        std::ofstream multiplayerOutput(multiplayerSettingsPath);
        multiplayerOutput << json{
            {"format", "aine.multiplayerSettings"},
            {"version", 1},
            {"runtimeStatus", buildSettings_.multiplayer.enabled
                                  ? "Local runtime networking components simulate sessions and replicated identities; internet transport is not implemented yet."
                                  : "Single-player runtime."},
            {"settings", MultiplayerSettingsToJson(buildSettings_.multiplayer)},
        }.dump(2) << '\n';

        localReport.launcherPath = outputDirectory / LauncherFilenameForPlatform(targetPlatform);
        const std::string manifestExecutablePath = RelativeBuildPathString(outputDirectory, localReport.executablePath);
        const std::string manifestLauncherPath = RelativeBuildPathString(outputDirectory, localReport.launcherPath);
        const std::string manifestBundlePath = isMacTarget ? RelativeBuildPathString(outputDirectory, appBundlePath) : "";
        const std::string manifestDataProject =
            buildSettings_.copyProjectFiles ? RelativeBuildPathString(outputDirectory, localReport.packagedProjectFile) : "";
        json manifest = {
            {"format", "aine.buildManifest"},
            {"version", 1},
            {"builtAtUtc", UtcTimestampString()},
            {"projectName", projectName_},
            {"engineVersion", "0.1.0"},
            {"releaseChannel", buildSettings_.releaseChannel},
            {"packageType", PackageTypeForPlatform(targetPlatform)},
            {"executable", manifestExecutablePath},
            {"executableHash", FileHashString(localReport.executablePath)},
            {"launcher", manifestLauncherPath},
            {"appBundle", manifestBundlePath},
            {"dataProject", manifestDataProject},
            {"copiedFileCount", localReport.copiedFileCount},
            {"signing",
             {
                 {"mode", buildSettings_.signingMode},
                 {"signed", buildSettings_.signingMode != "Unsigned"},
                 {"note", buildSettings_.signingMode == "Unsigned"
                              ? "Unsigned internal package; public release still needs a real certificate-backed signing step."
                              : "Signing mode was requested by build settings; verify the external signing step before public distribution."},
             }},
            {"target",
             {
                 {"platform", buildSettings_.targetPlatform},
                 {"architecture", buildSettings_.architecture},
                 {"configuration", buildSettings_.configuration},
                 {"developmentBuild", buildSettings_.developmentBuild},
                 {"hostPlatform", HostBuildPlatformName()},
             }},
            {"scenes", buildSettings_.scenes},
            {"multiplayer", MultiplayerSettingsToJson(buildSettings_.multiplayer)},
            {"runtimeNotes",
             json::array({
                 "Packaged executable uses a target-specific ai_native_player runtime.",
                 "C# gameplay scripts compile and run through the engine-owned runtime scripting host in Play Mode.",
                 "Multiplayer settings and network runtime components are persisted and simulated locally; internet transport is not implemented yet.",
                 "Cross-platform packages are only produced when a matching target runtime exists; missing target runtimes are reported as unavailable.",
             })},
        };
        std::ofstream manifestOutput(localReport.manifestPath);
        manifestOutput << manifest.dump(2) << '\n';

        if (buildSettings_.generateInstallerManifest) {
            const std::filesystem::path installerManifestPath = outputDirectory / "installer-manifest.aineinstaller.json";
            std::ofstream installer(installerManifestPath);
            installer << json{
                {"format", "aine.installerManifest"},
                {"version", 1},
                {"projectName", projectName_},
                {"releaseChannel", buildSettings_.releaseChannel},
                {"installScope", "per-user"},
                {"targetPlatform", targetPlatform},
                {"entryPoint", isMacTarget ? manifestBundlePath : manifestExecutablePath},
                {"entryPointExecutable", manifestExecutablePath},
                {"entryPointHash", FileHashString(localReport.executablePath)},
                {"requiresAdmin", false},
                {"signingMode", buildSettings_.signingMode},
                {"generatedAtUtc", UtcTimestampString()},
            }.dump(2) << '\n';
            localReport.diagnostics.push_back("Installer manifest written: " + installerManifestPath.string());
        }

        if (buildSettings_.generateUpdaterManifest) {
            const std::filesystem::path updaterManifestPath = outputDirectory / "latest.aineupdate.json";
            std::ofstream updater(updaterManifestPath);
            updater << json{
                {"format", "aine.updateManifest"},
                {"version", 1},
                {"projectName", projectName_},
                {"releaseChannel", buildSettings_.releaseChannel},
                {"packageManifest", localReport.manifestPath.filename().generic_string()},
                {"targetPlatform", targetPlatform},
                {"executable", manifestExecutablePath},
                {"launcher", manifestLauncherPath},
                {"executableHash", FileHashString(localReport.executablePath)},
                {"publishedAtUtc", UtcTimestampString()},
                {"notes", "Local reproducible package metadata; remote publishing is a separate release step."},
            }.dump(2) << '\n';
            localReport.diagnostics.push_back("Updater manifest written: " + updaterManifestPath.string());
        }

        std::ofstream launcher(localReport.launcherPath);
        if (isWindowsTarget) {
            launcher << "@echo off\n";
            launcher << "cd /d \"%~dp0\"\n";
            if (buildSettings_.copyProjectFiles) {
                launcher << QuoteCommandArgument(localReport.executablePath.filename())
                         << " --project=\"Data\\" << kProjectFileName << "\" %*\n";
            } else {
                launcher << QuoteCommandArgument(localReport.executablePath.filename()) << " %*\n";
            }
        } else {
            launcher << "#!/bin/sh\n";
            launcher << "SCRIPT_DIR=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\"\n";
            launcher << "\"" << "$SCRIPT_DIR/" << manifestExecutablePath << "\"";
            if (buildSettings_.copyProjectFiles) {
                launcher << " --project=\"" << "$SCRIPT_DIR/" << manifestDataProject << "\"";
            }
            launcher << " \"$@\"\n";
            AddExecutablePermissions(localReport.launcherPath);
        }

        if (buildSettings_.multiplayer.enabled) {
            const std::filesystem::path hostPath = outputDirectory / ("run-host" + HostLauncherSuffixForPlatform(targetPlatform));
            std::ofstream host(hostPath);
            if (isWindowsTarget) {
                host << "@echo off\ncd /d \"%~dp0\"\n";
                host << QuoteCommandArgument(localReport.executablePath.filename())
                     << " --project=\"Data\\" << kProjectFileName << "\" --multiplayer-role=host"
                     << " --bind=" << buildSettings_.multiplayer.bindAddress
                     << " --port=" << buildSettings_.multiplayer.port << " %*\n";
            } else {
                host << "#!/bin/sh\n";
                host << "SCRIPT_DIR=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\"\n";
                host << "\"" << "$SCRIPT_DIR/" << manifestExecutablePath << "\" --project=\"" << "$SCRIPT_DIR/"
                     << manifestDataProject << "\" --multiplayer-role=host"
                     << " --bind=" << buildSettings_.multiplayer.bindAddress
                     << " --port=" << buildSettings_.multiplayer.port << " \"$@\"\n";
                AddExecutablePermissions(hostPath);
            }

            const std::filesystem::path clientPath = outputDirectory / ("run-client" + HostLauncherSuffixForPlatform(targetPlatform));
            std::ofstream client(clientPath);
            if (isWindowsTarget) {
                client << "@echo off\ncd /d \"%~dp0\"\n";
                client << QuoteCommandArgument(localReport.executablePath.filename())
                       << " --project=\"Data\\" << kProjectFileName << "\" --multiplayer-role=client"
                       << " --connect=" << buildSettings_.multiplayer.bindAddress
                       << " --port=" << buildSettings_.multiplayer.port << " %*\n";
            } else {
                client << "#!/bin/sh\n";
                client << "SCRIPT_DIR=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\"\n";
                client << "\"" << "$SCRIPT_DIR/" << manifestExecutablePath << "\" --project=\"" << "$SCRIPT_DIR/"
                       << manifestDataProject << "\" --multiplayer-role=client"
                       << " --connect=" << buildSettings_.multiplayer.bindAddress
                       << " --port=" << buildSettings_.multiplayer.port << " \"$@\"\n";
                AddExecutablePermissions(clientPath);
            }
        }

        if (buildSettings_.runPlayerSmokeAfterBuild && buildSettings_.copyProjectFiles) {
            const std::filesystem::path smokeLog = outputDirectory / "Logs" / "player-smoke.log";
            if (HostBuildPlatformName() == targetPlatform) {
                std::string command;
                if (isWindowsTarget) {
                    command = "cmd /d /s /c \"\"" + localReport.executablePath.string() +
                              "\" --project=\"" + localReport.packagedProjectFile.string() +
                              "\" --smoke --frames=240 > \"" + smokeLog.string() + "\" 2>&1\"";
                } else {
                    command = QuoteCommandArgument(localReport.executablePath) + " --project=" +
                              QuoteCommandArgument(localReport.packagedProjectFile) +
                              " --smoke --frames=240 > " + QuoteCommandArgument(smokeLog) + " 2>&1";
                }
                const int exitCode = std::system(command.c_str());
                if (exitCode != 0) {
                    return finish(false, "Packaged player smoke failed with exit code " + std::to_string(exitCode) +
                                             ". Log: " + smokeLog.string());
                }
                localReport.runtimeSmokeStatus = "launch-smoked on host " + HostBuildPlatformName() + ": " + smokeLog.string();
                localReport.diagnostics.push_back("Packaged player smoke passed: " + smokeLog.string());
            } else {
                localReport.runtimeSmokeStatus = "not executable on this host: host=" + HostBuildPlatformName() +
                                                 " target=" + targetPlatform;
                localReport.diagnostics.push_back("Packaged player smoke skipped: " + localReport.runtimeSmokeStatus);
            }
        } else if (buildSettings_.runPlayerSmokeAfterBuild && !buildSettings_.copyProjectFiles) {
            localReport.runtimeSmokeStatus = "skipped because Copy Project Data is disabled.";
            localReport.diagnostics.push_back("Packaged player smoke skipped: " + localReport.runtimeSmokeStatus);
        }

        AddActivity("build.packagePlayer", "Complete", targetPlatform + " " + localReport.executablePath.string());
        AddLog(LogLevel::Info, "Packaged " + targetPlatform + " player: " + localReport.executablePath.string());
        RefreshProjectTree();
        return finish(true, "Packaged " + targetPlatform + " player at " + localReport.executablePath.string());
    } catch (const std::exception& exception) {
        return finish(false, exception.what());
    }
}

bool EditorState::PackageWindowsBuild(const std::filesystem::path& playerExecutable,
                                      ProjectBuildReport* report,
                                      std::string* error) {
    const ProjectBuildSettings originalSettings = buildSettings_;
    buildSettings_.targetPlatform = "Windows";
    if (buildSettings_.outputDirectory.empty() ||
        buildSettings_.outputDirectory == DefaultBuildOutputDirectoryForPlatform(originalSettings.targetPlatform)) {
        buildSettings_.outputDirectory = DefaultBuildOutputDirectoryForPlatform("Windows");
    }
    const bool ok = PackagePlayerBuild(playerExecutable, report, error);
    buildSettings_ = originalSettings;
    NormalizeBuildSettings();
    std::string restoreError;
    SaveProject(&restoreError);
    return ok;
}

const AssetRecord* EditorState::FindAssetByIdOrName(const std::string& idOrName) const {
    auto it = std::find_if(assetDatabase_.begin(), assetDatabase_.end(), [&idOrName](const AssetRecord& asset) {
        return asset.id == idOrName || asset.name == idOrName || asset.importedPath == idOrName;
    });
    return it == assetDatabase_.end() ? nullptr : &(*it);
}

bool EditorState::ImportAsset(const std::filesystem::path& sourcePath, const std::string& sourceLabel,
                              const std::string& license, AssetRecord* importedAsset, std::string* error) {
    if (sourcePath.empty()) {
        if (error != nullptr) {
            *error = "Asset source path is empty.";
        }
        return false;
    }

    try {
        const std::filesystem::path absoluteSource = std::filesystem::absolute(sourcePath);
        if (!std::filesystem::exists(absoluteSource) || !std::filesystem::is_regular_file(absoluteSource)) {
            if (error != nullptr) {
                *error = "Asset source file does not exist: " + absoluteSource.string();
            }
            return false;
        }

        const std::string type = AssetTypeForPath(absoluteSource);
        const std::filesystem::path importFolder = assetRootPath_ / AssetSubfolderForType(type);
        std::filesystem::create_directories(importFolder);
        std::filesystem::create_directories(projectRootPath_ / "Library" / "ImportedAssets" / "Resources");
        std::filesystem::create_directories(projectRootPath_ / "Library" / "ImportedAssets" / "Metadata");
        std::filesystem::create_directories(projectRootPath_ / "Library" / "ImportedAssets" / "Thumbnails");
        std::filesystem::path importedPath = importFolder / absoluteSource.filename();
        if (std::filesystem::exists(importedPath) && std::filesystem::equivalent(absoluteSource, importedPath)) {
            importedPath = absoluteSource;
        } else {
            std::filesystem::copy_file(absoluteSource, importedPath, std::filesystem::copy_options::overwrite_existing);
        }

        std::ostringstream id;
        id << "asset_" << std::setw(4) << std::setfill('0') << nextAssetId_++;
        AssetRecord record;
        record.id = id.str();
        record.name = absoluteSource.stem().string();
        record.type = type;
        record.sourceLabel = sourceLabel.empty() ? "manual-import" : sourceLabel;
        record.sourcePath = absoluteSource.string();
        record.sourceHash = FileHashString(absoluteSource);
        record.importedPath = RelativeToProjectString(projectRootPath_, importedPath);
        record.cookedPath = record.importedPath;
        record.resourcePath = RelativeToProjectString(projectRootPath_,
                                                      projectRootPath_ / "Library" / "ImportedAssets" / "Resources" /
                                                          (record.id + ".resource.json"));
        record.metadataPath = RelativeToProjectString(projectRootPath_,
                                                      projectRootPath_ / "Library" / "ImportedAssets" / "Metadata" /
                                                          (record.id + ".metadata.json"));
        record.thumbnailPath = RelativeToProjectString(projectRootPath_,
                                                       projectRootPath_ / "Library" / "ImportedAssets" / "Thumbnails" /
                                                           (record.id + ".thumbnail.json"));
        record.license = license.empty() ? "unspecified" : license;
        record.importSettings = "importer=neutral;source=" + record.sourceLabel;
        if (record.type == "Texture") {
            record.importSettings =
                "importer=texture;source=" + record.sourceLabel +
                ";spriteMode=Single;pixelsPerUnit=100;pivot=0.5,0.5;filterMode=Point;wrapMode=Clamp;packingTag=";
        }

        const std::vector<std::string> sourceDependencies = DiscoverAssetDependencies(absoluteSource, type);
        for (const std::string& sourceDependency : sourceDependencies) {
            const std::filesystem::path dependencyPath = std::filesystem::absolute(sourceDependency).lexically_normal();
            if (!std::filesystem::exists(dependencyPath) || !std::filesystem::is_regular_file(dependencyPath)) {
                record.dependencies.push_back(dependencyPath.string());
                continue;
            }
            std::filesystem::path importedDependency = importFolder / dependencyPath.filename();
            if (!std::filesystem::exists(importedDependency) || !std::filesystem::equivalent(dependencyPath, importedDependency)) {
                std::filesystem::copy_file(dependencyPath, importedDependency, std::filesystem::copy_options::overwrite_existing);
            }
            record.dependencies.push_back(RelativeToProjectString(projectRootPath_, importedDependency));
        }

        std::ofstream(projectRootPath_ / record.resourcePath) << BuildImportedResourceJson(record).dump(2) << '\n';
        std::ofstream(projectRootPath_ / record.metadataPath) << BuildAssetMetadataJson(record).dump(2) << '\n';
        std::ofstream(projectRootPath_ / record.thumbnailPath) << BuildAssetThumbnailJson(record).dump(2) << '\n';
        assetDatabase_.push_back(record);

        if (!SaveAssetDatabase(error)) {
            return false;
        }
        RefreshProjectTree();
        AddActivity("asset.import", "Complete", record.id + " " + record.importedPath);
        if (importedAsset != nullptr) {
            *importedAsset = record;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::CreateMaterialAsset(const std::string& materialName, const std::string& color,
                                      AssetRecord* materialAsset, std::string* error) {
    const std::string safeName = SanitizeIdentifier(materialName, "Material");
    try {
        std::filesystem::create_directories(assetRootPath_ / "Materials");
        std::filesystem::create_directories(projectRootPath_ / "Library" / "ImportedAssets" / "Resources");
        std::filesystem::create_directories(projectRootPath_ / "Library" / "ImportedAssets" / "Metadata");
        std::filesystem::create_directories(projectRootPath_ / "Library" / "ImportedAssets" / "Thumbnails");
        const std::filesystem::path materialPath = assetRootPath_ / "Materials" / (safeName + ".material.json");
        json material{{"format", "aine.material"}, {"version", 1}, {"name", safeName}, {"color", color.empty() ? "1, 1, 1" : color}};
        std::ofstream output(materialPath);
        output << material.dump(2) << '\n';

        std::ostringstream id;
        id << "asset_" << std::setw(4) << std::setfill('0') << nextAssetId_++;
        AssetRecord record;
        record.id = id.str();
        record.name = safeName;
        record.type = "Material";
        record.sourceLabel = "generated-material";
        record.sourcePath = "generated";
        record.sourceHash = FileHashString(materialPath);
        record.importedPath = RelativeToProjectString(projectRootPath_, materialPath);
        record.cookedPath = record.importedPath;
        record.resourcePath = RelativeToProjectString(projectRootPath_,
                                                      projectRootPath_ / "Library" / "ImportedAssets" / "Resources" /
                                                          (record.id + ".resource.json"));
        record.metadataPath = RelativeToProjectString(projectRootPath_,
                                                      projectRootPath_ / "Library" / "ImportedAssets" / "Metadata" /
                                                          (record.id + ".metadata.json"));
        record.thumbnailPath = RelativeToProjectString(projectRootPath_,
                                                       projectRootPath_ / "Library" / "ImportedAssets" / "Thumbnails" /
                                                           (record.id + ".thumbnail.json"));
        record.license = "project-generated";
        record.importSettings = "importer=material;color=" + (color.empty() ? "1, 1, 1" : color);
        std::ofstream(projectRootPath_ / record.resourcePath) << BuildImportedResourceJson(record).dump(2) << '\n';
        std::ofstream(projectRootPath_ / record.metadataPath) << BuildAssetMetadataJson(record).dump(2) << '\n';
        std::ofstream(projectRootPath_ / record.thumbnailPath) << BuildAssetThumbnailJson(record).dump(2) << '\n';
        assetDatabase_.push_back(record);
        if (!SaveAssetDatabase(error)) {
            return false;
        }
        RefreshProjectTree();
        if (materialAsset != nullptr) {
            *materialAsset = record;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::AssignMaterialToEntity(int id, const std::string& materialNameOrId, std::string* error) {
    Entity* entity = FindEntity(id);
    if (entity == nullptr) {
        if (error != nullptr) {
            *error = "Target entity not found.";
        }
        return false;
    }

    std::string materialName = materialNameOrId;
    if (const AssetRecord* asset = FindAssetByIdOrName(materialNameOrId)) {
        materialName = asset->name;
    }
    if (materialName.empty()) {
        if (error != nullptr) {
            *error = "Material name or id is empty.";
        }
        return false;
    }

    auto it = std::find_if(entity->components.begin(), entity->components.end(), [](const Component& component) {
        return component.type == "MeshRenderer";
    });
    if (it == entity->components.end()) {
        entity->components.push_back(MakeMeshPlaceholderComponent("UnitCube", materialName));
    } else {
        SetComponentProperty(*it, "material", materialName);
    }
    SyncTransformComponent(*entity);
    return true;
}

bool EditorState::CreateScript(const std::filesystem::path& scriptPath, const std::string& content,
                               std::filesystem::path* writtenPath, std::string* error) {
    std::filesystem::path target = scriptPath;
    if (target.empty()) {
        target = "GeneratedScript.cs";
    }
    if (!target.is_absolute()) {
        target = target.has_parent_path() ? projectRootPath_ / target : assetRootPath_ / "Scripts" / "Gameplay" / target;
    }
    if (target.extension().empty()) {
        target += ".cs";
    }
    if (!IsPathInside(projectRootPath_, target)) {
        if (error != nullptr) {
            *error = "Script path must stay inside the project.";
        }
        return false;
    }

    try {
        std::filesystem::create_directories(target.parent_path());
        const std::string className = SanitizeIdentifier(target.stem().string(), "GeneratedScript");
        const std::string scriptContent = content.empty()
            ? "public class " + className + "\n{\n    public bool enabled = true;\n}\n"
            : content;
        std::ofstream output(target);
        output << scriptContent;
        if (writtenPath != nullptr) {
            *writtenPath = target;
        }
        RefreshProjectTree();
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::ModifyScript(const std::filesystem::path& scriptPath, const std::string& content,
                               std::filesystem::path* writtenPath, std::string* error) {
    std::filesystem::path target = scriptPath.is_absolute() ? scriptPath : projectRootPath_ / scriptPath;
    if (!IsPathInside(projectRootPath_, target)) {
        if (error != nullptr) {
            *error = "Script path must stay inside the project.";
        }
        return false;
    }
    try {
        std::filesystem::create_directories(target.parent_path());
        std::ofstream output(target);
        output << content;
        if (writtenPath != nullptr) {
            *writtenPath = target;
        }
        RefreshProjectTree();
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

ScriptCompileResult EditorState::CompileScripts() const {
    ScriptCompileOptions options;
    options.projectRoot = projectRootPath_;
    options.assetRoot = assetRootPath_;
    options.outputRoot = projectRootPath_ / "Library" / "Scripts";
    return CompileProjectScripts(options);
}

std::string EditorState::ScriptCompileManifestPathString() const {
    return (projectRootPath_ / "Library" / "Scripts" / "compile_manifest.json").string();
}

std::vector<std::string> EditorState::GetScriptDiagnostics(const std::filesystem::path& scriptPath) const {
    std::vector<std::string> diagnostics;
    auto inspectScript = [&diagnostics, this](const std::filesystem::path& path) -> bool {
        const std::filesystem::path absolute = path.is_absolute() ? path : projectRootPath_ / path;
        if (!IsPathInside(projectRootPath_, absolute)) {
            diagnostics.push_back("Script path is outside the project: " + path.string());
            return false;
        }
        if (!std::filesystem::exists(absolute)) {
            diagnostics.push_back("Script file is missing: " + path.string());
            return false;
        }
        return true;
    };

    if (!scriptPath.empty()) {
        if (!inspectScript(scriptPath)) {
            return diagnostics;
        }
    }

    const ScriptCompileResult compile = CompileScripts();
    diagnostics = FormatScriptCompileDiagnostics(compile);
    if (diagnostics.empty()) {
        diagnostics.push_back("script diagnostics passed.");
    }
    return diagnostics;
}

bool EditorState::AttachScriptToEntity(int id, const std::filesystem::path& scriptPath,
                                       const std::string& componentType, std::string* error) {
    Entity* entity = FindEntity(id);
    if (entity == nullptr) {
        if (error != nullptr) {
            *error = "Target entity not found.";
        }
        return false;
    }
    const std::filesystem::path absolute = scriptPath.is_absolute() ? scriptPath : projectRootPath_ / scriptPath;
    if (!IsPathInside(projectRootPath_, absolute) || !std::filesystem::exists(absolute)) {
        if (error != nullptr) {
            *error = "Script file does not exist inside project: " + scriptPath.string();
        }
        return false;
    }

    const std::string type = componentType.empty() ? SanitizeIdentifier(absolute.stem().string(), "ScriptComponent")
                                                   : componentType;
    const std::string relativePath = RelativeToProjectString(projectRootPath_, absolute);
    entity->components.push_back({type, {{"enabled", "true"}, {"scriptPath", relativePath}}});
    return true;
}

bool EditorState::CreatePluginSkeleton(const std::string& pluginName, PluginRecord* plugin, std::string* error) {
    const std::string safeName = SanitizeIdentifier(pluginName, "GeneratedPlugin");
    try {
        const std::filesystem::path pluginRoot = projectRootPath_ / "Plugins" / "GeneratedTools" / safeName;
        std::filesystem::create_directories(pluginRoot / "Source");
        const std::string sourceFile = (std::filesystem::path("Source") / (safeName + ".cpp")).generic_string();
        json manifest{
            {"format", "aine.plugin"},
            {"version", 1},
            {"name", safeName},
            {"id", "generated." + safeName},
            {"enabledByDefault", false},
            {"entryPoint", "Register" + safeName + "Plugin"},
            {"sourceFiles", json::array({sourceFile})},
            {"tests", json::array({"manifest", "source-exists"})},
            {"install", json{{"target", (std::filesystem::path("Library") / "Plugins" / safeName).generic_string()}}},
        };
        std::ofstream(pluginRoot / "plugin.json") << manifest.dump(2) << '\n';
        std::ofstream(pluginRoot / "Source" / (safeName + ".cpp"))
            << "// Generated AI Native Editor plugin skeleton.\n"
            << "void Register" << safeName << "Plugin() {}\n";

        auto it = std::find_if(plugins_.begin(), plugins_.end(), [&safeName](const PluginRecord& existing) {
            return existing.name == safeName;
        });
        PluginRecord record;
        record.name = safeName;
        record.path = RelativeToProjectString(projectRootPath_, pluginRoot);
        record.manifestPath = RelativeToProjectString(projectRootPath_, pluginRoot / "plugin.json");
        record.installPath = {};
        record.enabled = false;
        record.installed = false;
        if (it == plugins_.end()) {
            plugins_.push_back(record);
        } else {
            *it = record;
        }
        if (plugin != nullptr) {
            *plugin = record;
        }
        RefreshProjectTree();
        return SaveProject(error);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::CompilePlugin(const std::string& pluginName, std::filesystem::path* reportPath, std::string* error) {
    auto it = std::find_if(plugins_.begin(), plugins_.end(), [&pluginName](const PluginRecord& plugin) {
        return plugin.name == pluginName;
    });
    if (it == plugins_.end()) {
        if (error != nullptr) {
            *error = "Plugin not found: " + pluginName;
        }
        return false;
    }

    try {
        const std::filesystem::path pluginRoot = projectRootPath_ / it->path;
        const std::filesystem::path manifestPath = projectRootPath_ / it->manifestPath;
        std::ifstream input(manifestPath);
        if (!input) {
            if (error != nullptr) {
                *error = "Plugin manifest not found: " + manifestPath.string();
            }
            return false;
        }
        const json manifest = json::parse(input);
        if (!manifest.is_object() || manifest.value("format", std::string{}) != "aine.plugin" ||
            manifest.value("version", 0) != 1 || manifest.value("entryPoint", std::string{}).empty()) {
            if (error != nullptr) {
                *error = "Plugin manifest is invalid: " + manifestPath.string();
            }
            return false;
        }

        json sourceReports = json::array();
        for (const json& sourceJson : manifest.value("sourceFiles", json::array())) {
            if (!sourceJson.is_string()) {
                if (error != nullptr) {
                    *error = "Plugin manifest sourceFiles contains a non-string entry.";
                }
                return false;
            }
            const std::filesystem::path relativeSource = sourceJson.get<std::string>();
            const std::filesystem::path sourcePath = (pluginRoot / relativeSource).lexically_normal();
            if (!LexicallyInsideOrSame(pluginRoot, sourcePath) || !std::filesystem::exists(sourcePath)) {
                if (error != nullptr) {
                    *error = "Plugin source file missing or outside plugin root: " + relativeSource.generic_string();
                }
                return false;
            }
            sourceReports.push_back(json{{"path", relativeSource.generic_string()}, {"hash", FileHashString(sourcePath)}});
        }
        if (sourceReports.empty()) {
            if (error != nullptr) {
                *error = "Plugin manifest has no source files: " + manifestPath.string();
            }
            return false;
        }

        const std::filesystem::path outputRoot = projectRootPath_ / "Library" / "Plugins" / it->name;
        std::filesystem::create_directories(outputRoot);
        const std::filesystem::path outputReport = outputRoot / "compile_report.json";
        std::ofstream(outputReport) << json{
            {"format", "aine.pluginCompileReport"},
            {"version", 1},
            {"plugin", it->name},
            {"compiledAtUtc", UtcTimestampString()},
            {"status", "manifest-validated"},
            {"entryPoint", manifest.value("entryPoint", std::string{})},
            {"sources", sourceReports},
            {"note", "Native plugin dynamic linking is not enabled yet; compile validates manifest/source contract."},
        }.dump(2) << '\n';
        if (reportPath != nullptr) {
            *reportPath = outputReport;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::TestPlugin(const std::string& pluginName, std::filesystem::path* reportPath, std::string* error) {
    auto it = std::find_if(plugins_.begin(), plugins_.end(), [&pluginName](const PluginRecord& plugin) {
        return plugin.name == pluginName;
    });
    if (it == plugins_.end()) {
        if (error != nullptr) {
            *error = "Plugin not found: " + pluginName;
        }
        return false;
    }

    try {
        const std::filesystem::path outputRoot = projectRootPath_ / "Library" / "Plugins" / it->name;
        const std::filesystem::path compileReport = outputRoot / "compile_report.json";
        if (!std::filesystem::exists(compileReport)) {
            if (error != nullptr) {
                *error = "Plugin compile report is missing; run plugin.compile first: " + pluginName;
            }
            return false;
        }
        const std::filesystem::path outputReport = outputRoot / "test_report.json";
        std::ofstream(outputReport) << json{
            {"format", "aine.pluginTestReport"},
            {"version", 1},
            {"plugin", it->name},
            {"testedAtUtc", UtcTimestampString()},
            {"status", "passed"},
            {"tests", json::array({"manifest", "source-exists", "compile-report"})},
        }.dump(2) << '\n';
        if (reportPath != nullptr) {
            *reportPath = outputReport;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::InstallPlugin(const std::string& pluginName, PluginRecord* plugin, std::string* error) {
    auto it = std::find_if(plugins_.begin(), plugins_.end(), [&pluginName](const PluginRecord& plugin) {
        return plugin.name == pluginName;
    });
    if (it == plugins_.end()) {
        if (error != nullptr) {
            *error = "Plugin not found: " + pluginName;
        }
        return false;
    }

    try {
        const std::filesystem::path pluginRoot = projectRootPath_ / it->path;
        const std::filesystem::path manifestPath = projectRootPath_ / it->manifestPath;
        std::ifstream input(manifestPath);
        if (!input) {
            if (error != nullptr) {
                *error = "Plugin manifest not found: " + manifestPath.string();
            }
            return false;
        }
        const json manifest = json::parse(input);
        if (!manifest.is_object() || manifest.value("format", std::string{}) != "aine.plugin" ||
            manifest.value("version", 0) != 1 || manifest.value("name", std::string{}) != it->name ||
            manifest.value("entryPoint", std::string{}).empty()) {
            if (error != nullptr) {
                *error = "Plugin manifest is invalid: " + manifestPath.string();
            }
            return false;
        }
        if (!manifest.contains("sourceFiles") || !manifest.at("sourceFiles").is_array() ||
            manifest.at("sourceFiles").empty()) {
            if (error != nullptr) {
                *error = "Plugin manifest has no sourceFiles: " + manifestPath.string();
            }
            return false;
        }

        const std::filesystem::path installRoot = projectRootPath_ / "Library" / "Plugins" / it->name;
        std::filesystem::create_directories(installRoot / "Source");
        std::filesystem::copy_file(manifestPath, installRoot / "plugin.json",
                                   std::filesystem::copy_options::overwrite_existing);

        json copiedSources = json::array();
        for (const json& sourceJson : manifest.at("sourceFiles")) {
            if (!sourceJson.is_string() || sourceJson.get<std::string>().empty()) {
                if (error != nullptr) {
                    *error = "Plugin manifest has an invalid sourceFiles entry.";
                }
                return false;
            }
            const std::filesystem::path relativeSource = sourceJson.get<std::string>();
            const std::filesystem::path sourcePath = (pluginRoot / relativeSource).lexically_normal();
            if (!LexicallyInsideOrSame(pluginRoot, sourcePath) || !std::filesystem::exists(sourcePath)) {
                if (error != nullptr) {
                    *error = "Plugin source file missing or outside plugin root: " + relativeSource.generic_string();
                }
                return false;
            }
            const std::filesystem::path destination = installRoot / relativeSource;
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(sourcePath, destination, std::filesystem::copy_options::overwrite_existing);
            copiedSources.push_back(relativeSource.generic_string());
        }

        json installManifest{
            {"format", "aine.pluginInstall"},
            {"version", 1},
            {"plugin", it->name},
            {"manifestPath", it->manifestPath},
            {"installedAtUtc", UtcTimestampString()},
            {"entryPoint", manifest.value("entryPoint", std::string{})},
            {"sourceFiles", copiedSources},
            {"tests", manifest.value("tests", json::array())},
            {"compile", json{{"status", "manifest-validated"}, {"note", "Native plugin compilation is not linked into the editor process yet."}}},
        };
        std::ofstream(installRoot / "install_manifest.json") << installManifest.dump(2) << '\n';

        it->installed = true;
        it->installPath = RelativeToProjectString(projectRootPath_, installRoot);
        if (plugin != nullptr) {
            *plugin = *it;
        }
        RefreshProjectTree();
        return SaveProject(error);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::SetPluginEnabled(const std::string& pluginName, bool enabled, std::string* error) {
    auto it = std::find_if(plugins_.begin(), plugins_.end(), [&pluginName](const PluginRecord& plugin) {
        return plugin.name == pluginName;
    });
    if (it == plugins_.end()) {
        if (error != nullptr) {
            *error = "Plugin not found: " + pluginName;
        }
        return false;
    }
    if (enabled && !it->installed) {
        if (error != nullptr) {
            *error = "Plugin must be installed before it can be enabled: " + pluginName;
        }
        return false;
    }
    if (enabled) {
        const std::filesystem::path installManifest = projectRootPath_ / it->installPath / "install_manifest.json";
        if (it->installPath.empty() || !std::filesystem::exists(installManifest)) {
            if (error != nullptr) {
                *error = "Installed plugin manifest is missing: " + pluginName;
            }
            return false;
        }
    }
    it->enabled = enabled;
    return SaveProject(error);
}

bool EditorState::AddProjectTag(const std::string& tag, std::string* error) {
    const std::string normalized = NormalizeProjectSettingName(tag);
    if (normalized.empty()) {
        if (error != nullptr) {
            *error = "Tag name is empty.";
        }
        return false;
    }
    NormalizeEditorProjectSettings(&projectSettings_);
    if (ContainsSettingName(projectSettings_.tags, normalized)) {
        if (error != nullptr) {
            *error = "Tag already exists: " + normalized;
        }
        return false;
    }
    projectSettings_.tags.push_back(normalized);
    return true;
}

bool EditorState::RenameProjectTag(const std::string& oldTag, const std::string& newTag, std::string* error) {
    const std::string oldName = NormalizeProjectSettingName(oldTag);
    const std::string newName = NormalizeProjectSettingName(newTag);
    if (IsDefaultProjectTagName(oldName)) {
        if (error != nullptr) {
            *error = "Built-in tags cannot be renamed: " + oldName;
        }
        return false;
    }
    if (newName.empty()) {
        if (error != nullptr) {
            *error = "Tag name is empty.";
        }
        return false;
    }
    if (oldName == newName) {
        return true;
    }
    NormalizeEditorProjectSettings(&projectSettings_);
    auto it = std::find(projectSettings_.tags.begin(), projectSettings_.tags.end(), oldName);
    if (it == projectSettings_.tags.end()) {
        if (error != nullptr) {
            *error = "Tag not found: " + oldName;
        }
        return false;
    }
    if (ContainsSettingName(projectSettings_.tags, newName)) {
        if (error != nullptr) {
            *error = "Tag already exists: " + newName;
        }
        return false;
    }
    *it = newName;
    for (Entity& entity : entities_) {
        if (entity.tag == oldName) {
            entity.tag = newName;
        }
    }
    NormalizeEditorProjectSettings(&projectSettings_);
    return true;
}

bool EditorState::RemoveProjectTag(const std::string& tag, std::string* error) {
    const std::string normalized = NormalizeProjectSettingName(tag);
    if (IsDefaultProjectTagName(normalized)) {
        if (error != nullptr) {
            *error = "Built-in tags cannot be removed: " + normalized;
        }
        return false;
    }
    NormalizeEditorProjectSettings(&projectSettings_);
    const auto oldSize = projectSettings_.tags.size();
    projectSettings_.tags.erase(std::remove(projectSettings_.tags.begin(), projectSettings_.tags.end(), normalized),
                                projectSettings_.tags.end());
    if (projectSettings_.tags.size() == oldSize) {
        if (error != nullptr) {
            *error = "Tag not found: " + normalized;
        }
        return false;
    }
    for (Entity& entity : entities_) {
        if (entity.tag == normalized) {
            entity.tag = "Untagged";
        }
    }
    return true;
}

bool EditorState::AddProjectLayer(const std::string& layer, std::string* error) {
    const std::string normalized = NormalizeProjectSettingName(layer);
    if (normalized.empty()) {
        if (error != nullptr) {
            *error = "Layer name is empty.";
        }
        return false;
    }
    NormalizeEditorProjectSettings(&projectSettings_);
    if (ContainsSettingName(projectSettings_.layers, normalized)) {
        if (error != nullptr) {
            *error = "Layer already exists: " + normalized;
        }
        return false;
    }
    projectSettings_.layers.push_back(normalized);
    SyncPhysicsSettingsComponents();
    return true;
}

bool EditorState::RenameProjectLayer(const std::string& oldLayer, const std::string& newLayer, std::string* error) {
    const std::string oldName = NormalizeProjectSettingName(oldLayer);
    const std::string newName = NormalizeProjectSettingName(newLayer);
    if (IsDefaultProjectLayerName(oldName)) {
        if (error != nullptr) {
            *error = "Built-in layers cannot be renamed: " + oldName;
        }
        return false;
    }
    if (newName.empty()) {
        if (error != nullptr) {
            *error = "Layer name is empty.";
        }
        return false;
    }
    if (oldName == newName) {
        return true;
    }
    NormalizeEditorProjectSettings(&projectSettings_);
    auto it = std::find(projectSettings_.layers.begin(), projectSettings_.layers.end(), oldName);
    if (it == projectSettings_.layers.end()) {
        if (error != nullptr) {
            *error = "Layer not found: " + oldName;
        }
        return false;
    }
    if (ContainsSettingName(projectSettings_.layers, newName)) {
        if (error != nullptr) {
            *error = "Layer already exists: " + newName;
        }
        return false;
    }
    *it = newName;
    for (Entity& entity : entities_) {
        if (entity.layer == oldName) {
            entity.layer = newName;
        }
    }
    for (LayerCollisionPair& pair : projectSettings_.disabledLayerCollisionPairs) {
        if (pair.first == oldName) {
            pair.first = newName;
        }
        if (pair.second == oldName) {
            pair.second = newName;
        }
    }
    NormalizeEditorProjectSettings(&projectSettings_);
    SyncPhysicsSettingsComponents();
    return true;
}

bool EditorState::RemoveProjectLayer(const std::string& layer, std::string* error) {
    const std::string normalized = NormalizeProjectSettingName(layer);
    if (IsDefaultProjectLayerName(normalized)) {
        if (error != nullptr) {
            *error = "Built-in layers cannot be removed: " + normalized;
        }
        return false;
    }
    NormalizeEditorProjectSettings(&projectSettings_);
    const auto oldSize = projectSettings_.layers.size();
    projectSettings_.layers.erase(std::remove(projectSettings_.layers.begin(), projectSettings_.layers.end(), normalized),
                                  projectSettings_.layers.end());
    if (projectSettings_.layers.size() == oldSize) {
        if (error != nullptr) {
            *error = "Layer not found: " + normalized;
        }
        return false;
    }
    for (Entity& entity : entities_) {
        if (entity.layer == normalized) {
            entity.layer = "Default";
        }
    }
    projectSettings_.disabledLayerCollisionPairs.erase(
        std::remove_if(projectSettings_.disabledLayerCollisionPairs.begin(),
                       projectSettings_.disabledLayerCollisionPairs.end(),
                       [&normalized](const LayerCollisionPair& pair) {
                           return pair.first == normalized || pair.second == normalized;
                       }),
        projectSettings_.disabledLayerCollisionPairs.end());
    SyncPhysicsSettingsComponents();
    return true;
}

bool EditorState::LayerCollisionEnabled(const std::string& firstLayer, const std::string& secondLayer) const {
    const LayerCollisionPair pair = OrderedLayerCollisionPair(firstLayer, secondLayer);
    return !ContainsLayerCollisionPair(projectSettings_.disabledLayerCollisionPairs, pair);
}

bool EditorState::SetLayerCollisionEnabled(const std::string& firstLayer, const std::string& secondLayer, bool enabled,
                                           std::string* error) {
    NormalizeEditorProjectSettings(&projectSettings_);
    LayerCollisionPair pair = OrderedLayerCollisionPair(firstLayer, secondLayer);
    if (pair.first.empty() || pair.second.empty() ||
        !ContainsSettingName(projectSettings_.layers, pair.first) ||
        !ContainsSettingName(projectSettings_.layers, pair.second)) {
        if (error != nullptr) {
            *error = "Both layers must exist before editing the collision matrix.";
        }
        return false;
    }
    if (enabled) {
        projectSettings_.disabledLayerCollisionPairs.erase(
            std::remove_if(projectSettings_.disabledLayerCollisionPairs.begin(),
                           projectSettings_.disabledLayerCollisionPairs.end(),
                           [&pair](const LayerCollisionPair& value) {
                               return SameLayerCollisionPair(value, pair);
                           }),
            projectSettings_.disabledLayerCollisionPairs.end());
    } else if (!ContainsLayerCollisionPair(projectSettings_.disabledLayerCollisionPairs, pair)) {
        projectSettings_.disabledLayerCollisionPairs.push_back(std::move(pair));
    }
    SyncPhysicsSettingsComponents();
    return true;
}

void EditorState::ResetProjectSettingsToDefaults() {
    projectSettings_ = DefaultEditorProjectSettings();
    for (Entity& entity : entities_) {
        if (!ContainsSettingName(projectSettings_.tags, entity.tag)) {
            entity.tag = "Untagged";
        }
        if (!ContainsSettingName(projectSettings_.layers, entity.layer)) {
            entity.layer = "Default";
        }
    }
    SyncPhysicsSettingsComponents();
}

ProjectTreeNode EditorState::BuildProjectTree() const {
    ProjectTreeNode projectNode = BuildFilesystemNode(projectRootPath_, "Project: " + projectRootPath_.filename().string());

    ProjectTreeNode root;
    root.name = "Workspace";
    root.folder = true;
    root.filesystemBacked = false;
    root.children = {projectNode};
    return root;
}

void EditorState::EnsureProjectFolders(std::string* error) const {
    try {
        std::filesystem::create_directories(assetRootPath_ / "Materials");
        std::filesystem::create_directories(assetRootPath_ / "Meshes");
        std::filesystem::create_directories(assetRootPath_ / "Prefabs");
        std::filesystem::create_directories(assetRootPath_ / "Imported" / "Models");
        std::filesystem::create_directories(assetRootPath_ / "Imported" / "Textures");
        std::filesystem::create_directories(assetRootPath_ / "Imported" / "Audio");
        std::filesystem::create_directories(assetRootPath_ / "Imported" / "Other");
        std::filesystem::create_directories(sceneRootPath_);
        std::filesystem::create_directories(assetRootPath_ / "Scripts" / "Gameplay");
        std::filesystem::create_directories(assetRootPath_ / "Scripts" / "Editor");
        std::filesystem::create_directories(assetRootPath_ / "Templates");
        std::filesystem::create_directories(assetRootPath_ / "Textures");
        std::filesystem::create_directories(projectRootPath_ / "Plugins" / "GeneratedTools");
        std::filesystem::create_directories(projectRootPath_ / "ProjectSettings");
        std::filesystem::create_directories(projectRootPath_ / "Library");
        std::filesystem::create_directories(projectRootPath_ / "Temp");
        std::filesystem::create_directories(projectRootPath_ / "Logs");
        std::filesystem::create_directories(gameSaveRootPath_);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
    }
}

std::filesystem::path EditorState::AssetDatabasePath() const {
    return projectRootPath_ / "ProjectSettings" / "AssetDatabase.json";
}

bool EditorState::LoadAssetDatabase(std::string* error) {
    assetDatabase_.clear();
    nextAssetId_ = 1;
    const std::filesystem::path path = AssetDatabasePath();
    if (!std::filesystem::exists(path)) {
        return true;
    }

    try {
        std::ifstream input(path);
        const json document = json::parse(input);
        const json assets = document.contains("assets") ? document.at("assets") : json::array();
        if (assets.is_array()) {
            for (const json& assetJson : assets) {
                AssetRecord asset = AssetRecordFromJson(assetJson);
                if (asset.id.empty()) {
                    continue;
                }
                assetDatabase_.push_back(asset);
                const std::string prefix = "asset_";
                if (asset.id.rfind(prefix, 0) == 0) {
                    std::istringstream stream(asset.id.substr(prefix.size()));
                    int parsed = 0;
                    if (stream >> parsed) {
                        nextAssetId_ = std::max(nextAssetId_, parsed + 1);
                    }
                }
            }
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::SaveAssetDatabase(std::string* error) const {
    try {
        std::filesystem::create_directories(AssetDatabasePath().parent_path());
        json assets = json::array();
        for (const AssetRecord& asset : assetDatabase_) {
            assets.push_back(AssetRecordToJson(asset));
        }
        json document{{"format", "aine.assetDatabase"}, {"version", 1}, {"assets", assets}};
        std::ofstream output(AssetDatabasePath());
        output << document.dump(2) << '\n';
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

void EditorState::ClearUndoRedo() {
    undoStack_.clear();
    redoStack_.clear();
}

bool EditorState::SaveSceneFile(const std::filesystem::path& scenePath, std::string* error) const {
    try {
        std::filesystem::create_directories(scenePath.parent_path());
        std::ofstream output(scenePath);
        output << SerializeSceneToJson(sceneName_, entities_).dump(2) << '\n';
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool EditorState::LoadSceneFile(const std::filesystem::path& scenePath, std::string* error) {
    if (!std::filesystem::exists(scenePath)) {
        if (error != nullptr) {
            *error = "Scene file does not exist: " + scenePath.string();
        }
        return false;
    }

    try {
        std::ifstream input(scenePath);
        const json scene = json::parse(input);
        EngineSceneDocument document = DeserializeSceneFromJson(scene, scenePath.filename().string());

        ClearScene();
        sceneFilePath_ = std::filesystem::absolute(scenePath);
        sceneName_ = document.name.empty() ? sceneFilePath_.filename().string() : document.name;
        entities_ = std::move(document.entities);
        nextEntityId_ = std::max(1, document.nextEntityId);

        for (Entity& entity : entities_) {
            SyncTransformComponent(entity);
        }

        for (Entity& entity : entities_) {
            const int loadedParentId = entity.parentId;
            if (loadedParentId != 0 && !CanSetEntityParent(entity.id, loadedParentId)) {
                entity.parentId = 0;
            }
        }
        EnsurePhysicsSettingsForScene();
        EnsureSceneTagLayerValuesAreKnown();
        SyncPhysicsSettingsComponents();
        sceneDirty_ = false;

        if (!entities_.empty()) {
            selectedEntityId_ = entities_.front().id;
        }

        AddLog(LogLevel::Info, "Loaded scene file: " + sceneFilePath_.string());
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

void EditorState::SyncTransformComponent(Entity& entity) {
    Component* transform = FindComponent(entity, "Transform");
    if (transform == nullptr) {
        entity.components.insert(entity.components.begin(), MakeTransformComponent());
        transform = &entity.components.front();
    }

    transform->properties = {
        {"position", Vec3ToString(entity.position)},
        {"rotation", Vec3ToString(entity.rotation)},
        {"scale", Vec3ToString(entity.scale)},
    };
}

void EditorState::SetDefaultPathsForRoot(const std::filesystem::path& projectRoot) {
    projectRootPath_ = std::filesystem::absolute(projectRoot);
    projectFilePath_ = projectRootPath_ / kProjectFileName;
    assetRootPath_ = projectRootPath_ / kDefaultAssetRoot;
    sceneRootPath_ = projectRootPath_ / kDefaultSceneRoot;
    gameSaveRootPath_ = projectRootPath_ / kDefaultGameSaveRoot;
    sceneFilePath_ = sceneRootPath_ / "Prototype.scene.json";
    sceneName_ = sceneFilePath_.filename().string();
}

} // namespace aine

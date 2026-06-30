#include "engine/runtime/Runtime.h"
#include "engine/scene/SceneSerialization.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr const char* kProjectFileName = "AI Native Project.aineproject.json";

struct PlayerOptions {
    std::filesystem::path inputPath;
    int frames = 120;
    float deltaSeconds = 1.0f / 60.0f;
    bool smoke = false;
};

std::string Vec3ToString(const std::array<float, 3>& value) {
    std::ostringstream stream;
    stream << value[0] << ", " << value[1] << ", " << value[2];
    return stream.str();
}

aine::Component* FindComponent(aine::Entity& entity, const std::string& type) {
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [&type](const aine::Component& component) {
        return component.type == type;
    });
    return it == entity.components.end() ? nullptr : &(*it);
}

void SyncTransformComponent(aine::Entity& entity) {
    aine::Component* transform = FindComponent(entity, "Transform");
    if (transform == nullptr) {
        entity.components.insert(entity.components.begin(), aine::Component{"Transform", {}});
        transform = &entity.components.front();
    }
    transform->properties = {
        {"position", Vec3ToString(entity.position)},
        {"rotation", Vec3ToString(entity.rotation)},
        {"scale", Vec3ToString(entity.scale)},
    };
}

std::filesystem::path ExecutableDirectory(const char* argv0) {
    if (argv0 == nullptr || argv0[0] == '\0') {
        return std::filesystem::current_path();
    }
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::absolute(argv0, error);
    if (error) {
        return std::filesystem::current_path();
    }
    return executable.parent_path();
}

std::filesystem::path ResolveProjectFile(std::filesystem::path inputPath, const std::filesystem::path& executableDirectory) {
    if (inputPath.empty()) {
        const std::vector<std::filesystem::path> packagedCandidates{
            std::filesystem::current_path() / "Data" / kProjectFileName,
            executableDirectory / "Data" / kProjectFileName,
            std::filesystem::current_path() / kProjectFileName,
            executableDirectory / kProjectFileName,
        };
        for (const std::filesystem::path& candidate : packagedCandidates) {
            if (std::filesystem::exists(candidate)) {
                inputPath = candidate;
                break;
            }
        }
    }
    if (inputPath.empty()) {
        inputPath = std::filesystem::current_path() / "projects" / "FirstGame" / kProjectFileName;
    }
    if (std::filesystem::is_directory(inputPath)) {
        return inputPath / kProjectFileName;
    }
    return inputPath;
}

std::filesystem::path ResolveScenePath(const std::filesystem::path& inputPath,
                                       const std::filesystem::path& executableDirectory) {
    const std::filesystem::path resolved = ResolveProjectFile(inputPath, executableDirectory);
    if (resolved.extension() == ".json" && resolved.filename() != kProjectFileName) {
        return resolved;
    }

    std::ifstream projectInput(resolved);
    if (!projectInput) {
        return resolved;
    }

    const json project = json::parse(projectInput);
    std::filesystem::path activeScene = project.value("activeScene", std::string{"Assets/Scenes/Prototype.scene.json"});
    if (!activeScene.is_absolute()) {
        activeScene = resolved.parent_path() / activeScene;
    }
    return activeScene;
}

bool LoadScene(const std::filesystem::path& scenePath, aine::EngineSceneDocument* outScene, std::string* error) {
    try {
        std::ifstream input(scenePath);
        if (!input) {
            if (error != nullptr) {
                *error = "could not open scene: " + scenePath.string();
            }
            return false;
        }
        const json sceneJson = json::parse(input);
        *outScene = aine::DeserializeSceneFromJson(sceneJson, scenePath.filename().string());
        for (aine::Entity& entity : outScene->entities) {
            SyncTransformComponent(entity);
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

PlayerOptions ParseOptions(int argc, char** argv) {
    PlayerOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke") {
            options.smoke = true;
        } else if (arg.rfind("--frames=", 0) == 0) {
            options.frames = std::max(1, std::atoi(arg.substr(9).c_str()));
        } else if (arg.rfind("--delta=", 0) == 0) {
            options.deltaSeconds = std::max(0.001f, static_cast<float>(std::atof(arg.substr(8).c_str())));
        } else if (arg.rfind("--project=", 0) == 0) {
            options.inputPath = arg.substr(10);
        } else if (arg.rfind("--scene=", 0) == 0) {
            options.inputPath = arg.substr(8);
        } else if (options.inputPath.empty()) {
            options.inputPath = arg;
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    const PlayerOptions options = ParseOptions(argc, argv);
    const std::filesystem::path scenePath = ResolveScenePath(options.inputPath, ExecutableDirectory(argc > 0 ? argv[0] : nullptr));

    aine::EngineSceneDocument scene;
    std::string error;
    if (!LoadScene(scenePath, &scene, &error)) {
        std::cerr << "ai_native_player failed: " << error << '\n';
        return 1;
    }

    aine::EngineRuntime runtime;
    aine::RuntimeHostCallbacks callbacks;
    callbacks.syncTransform = [](aine::Entity& entity) {
        SyncTransformComponent(entity);
    };
    callbacks.editorLog = [](aine::LogLevel, std::string message) {
        std::cout << message << '\n';
    };

    if (!runtime.Begin(scene.entities, callbacks)) {
        std::cerr << "ai_native_player failed: runtime did not start.\n";
        return 1;
    }

    aine::RuntimeInputState input;
    input.captured = true;
    input.captureSurface = "Player";
    input.character.moveZ = -1.0f;
    input.actions.push_back({"Move Z", "PlayerSmoke", -1.0f, true});
    runtime.SetRuntimeInputState(input);

    for (int frame = 0; frame < options.frames && !runtime.Failed() && !(options.smoke && runtime.GoalReached()); ++frame) {
        runtime.Update(scene.entities, options.deltaSeconds, callbacks);
    }

    std::cout << "ai_native_player scene=" << scene.name << " entities=" << scene.entities.size()
              << " elapsed=" << runtime.ElapsedSeconds() << " score=" << runtime.Score()
              << " status=" << runtime.Status() << '\n';

    if (options.smoke && runtime.Failed()) {
        std::cerr << "ai_native_player smoke failed: runtime entered Failed state.\n";
        return 1;
    }
    return 0;
}

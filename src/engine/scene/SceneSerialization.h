#pragma once

#include "engine/scene/Entity.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aine {

struct EngineSceneDocument {
    std::string name;
    int nextEntityId = 1;
    std::vector<Entity> entities;
};

nlohmann::json SerializeComponentToJson(const Component& component);
Component DeserializeComponentFromJson(const nlohmann::json& value);
nlohmann::json SerializeEntityToJson(const Entity& entity);
Entity DeserializeEntityFromJson(const nlohmann::json& value, int fallbackId);
nlohmann::json SerializeSceneToJson(const std::string& sceneName, const std::vector<Entity>& entities);
EngineSceneDocument DeserializeSceneFromJson(const nlohmann::json& scene, const std::string& fallbackName);

} // namespace aine

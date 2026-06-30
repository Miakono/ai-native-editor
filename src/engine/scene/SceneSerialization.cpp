#include "engine/scene/SceneSerialization.h"

#include <algorithm>

namespace aine {
namespace {

using json = nlohmann::json;

json Vec3ToJson(const std::array<float, 3>& value) {
    return json::array({value[0], value[1], value[2]});
}

std::array<float, 3> Vec3FromJson(const json& value, std::array<float, 3> fallback) {
    if (!value.is_array() || value.size() != 3) {
        return fallback;
    }

    return {
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>(),
    };
}

} // namespace

json SerializeComponentToJson(const Component& component) {
    json properties = json::object();
    for (const ComponentProperty& property : component.properties) {
        properties[property.name] = property.value;
    }

    return json{
        {"type", component.type},
        {"properties", properties},
    };
}

Component DeserializeComponentFromJson(const json& value) {
    Component component;
    component.type = value.value("type", "Component");
    if (value.contains("properties") && value.at("properties").is_object()) {
        for (const auto& [name, propertyValue] : value.at("properties").items()) {
            component.properties.push_back({name, propertyValue.is_string() ? propertyValue.get<std::string>() : propertyValue.dump()});
        }
    }
    return component;
}

json SerializeEntityToJson(const Entity& entity) {
    json components = json::array();
    for (const Component& component : entity.components) {
        components.push_back(SerializeComponentToJson(component));
    }

    return json{
        {"id", entity.id},
        {"parentId", entity.parentId},
        {"name", entity.name},
        {"active", entity.activeSelf},
        {"tag", entity.tag},
        {"layer", entity.layer},
        {"isStatic", entity.isStatic},
        {"transform",
         {{"position", Vec3ToJson(entity.position)}, {"rotation", Vec3ToJson(entity.rotation)}, {"scale", Vec3ToJson(entity.scale)}}},
        {"components", components},
    };
}

Entity DeserializeEntityFromJson(const json& value, int fallbackId) {
    Entity entity;
    entity.id = value.value("id", fallbackId);
    entity.parentId = value.value("parentId", 0);
    entity.name = value.value("name", "Entity");
    entity.activeSelf = value.value("active", true);
    entity.tag = value.value("tag", "Untagged");
    entity.layer = value.value("layer", "Default");
    entity.isStatic = value.value("isStatic", false);

    if (value.contains("components") && value.at("components").is_array()) {
        for (const json& componentJson : value.at("components")) {
            entity.components.push_back(DeserializeComponentFromJson(componentJson));
        }
    }

    if (value.contains("transform")) {
        const json& transform = value.at("transform");
        entity.position = Vec3FromJson(transform.value("position", json::array()), entity.position);
        entity.rotation = Vec3FromJson(transform.value("rotation", json::array()), entity.rotation);
        entity.scale = Vec3FromJson(transform.value("scale", json::array()), entity.scale);
    }

    return entity;
}

json SerializeSceneToJson(const std::string& sceneName, const std::vector<Entity>& entities) {
    json serializedEntities = json::array();
    for (const Entity& entity : entities) {
        serializedEntities.push_back(SerializeEntityToJson(entity));
    }

    return json{
        {"format", "aine.scene"},
        {"version", 1},
        {"name", sceneName},
        {"entities", serializedEntities},
    };
}

EngineSceneDocument DeserializeSceneFromJson(const json& scene, const std::string& fallbackName) {
    EngineSceneDocument document;
    document.name = scene.value("name", fallbackName);
    document.nextEntityId = 1;

    if (scene.contains("entities") && scene.at("entities").is_array()) {
        for (const json& entityJson : scene.at("entities")) {
            Entity entity = DeserializeEntityFromJson(entityJson, document.nextEntityId);
            document.nextEntityId = std::max(document.nextEntityId, entity.id + 1);
            document.entities.push_back(std::move(entity));
        }
    }

    return document;
}

} // namespace aine

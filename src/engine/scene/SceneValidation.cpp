#include "engine/scene/SceneValidation.h"

#include "engine/runtime/ComponentRegistry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <sstream>

namespace aine {
namespace {

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

const Component* FindComponent(const Entity& entity, const std::string& type) {
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [&type](const Component& component) {
        return component.type == type;
    });
    return it == entity.components.end() ? nullptr : &(*it);
}

int ComponentIntProperty(const Component& component, const std::string& propertyName, int fallback) {
    for (const ComponentProperty& property : component.properties) {
        if (property.name != propertyName) {
            continue;
        }

        std::istringstream stream(property.value);
        int parsed = fallback;
        if (stream >> parsed) {
            return parsed;
        }
        return fallback;
    }
    return fallback;
}

bool TryParseFiniteFloat(const std::string& value, float* out) {
    if (out == nullptr || value.empty()) {
        return false;
    }
    std::istringstream stream(value);
    float parsed = 0.0f;
    if (!(stream >> parsed) || !std::isfinite(parsed)) {
        return false;
    }
    *out = parsed;
    return true;
}

std::string ToLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
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

std::array<float, 2> ComponentVec2Property(const Component& component, const std::string& propertyName,
                                           std::array<float, 2> fallback) {
    const std::string value = ComponentStringProperty(component, propertyName);
    if (value.empty()) {
        return fallback;
    }

    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream stream(normalized);
    std::array<float, 2> parsed = fallback;
    if (stream >> parsed[0] >> parsed[1] && std::isfinite(parsed[0]) && std::isfinite(parsed[1])) {
        return parsed;
    }
    return fallback;
}

bool IsPhysicsBodyOrColliderComponent(const std::string& type) {
    return type == "Rigidbody2D" || type == "Rigidbody3D" || type == "Collider2D" || type == "Collider3D" ||
           type == "Terrain";
}

bool HasPhysicsBodyOrCollider(const Entity& entity) {
    return std::any_of(entity.components.begin(), entity.components.end(), [](const Component& component) {
        return IsPhysicsBodyOrColliderComponent(component.type);
    });
}

bool HasPhysicsSettings(const Entity& entity) {
    return HasComponent(entity, "PhysicsSettings");
}

const ComponentSchema* FindSchema(const std::string& type) {
    const ComponentDefinition* definition = FindComponentDefinition(type);
    return definition == nullptr ? nullptr : &definition->schema;
}

bool HasProperty(const Component& component, const std::string& propertyName) {
    return std::any_of(component.properties.begin(), component.properties.end(),
                       [&propertyName](const ComponentProperty& property) {
                           return property.name == propertyName;
                       });
}

bool ValidatePropertyKind(const Component& component, const ComponentSchemaProperty& schemaProperty,
                          std::string* diagnostic) {
    const std::string value = ComponentStringProperty(component, schemaProperty.name);
    if (schemaProperty.kind == "string") {
        if (schemaProperty.required && value.empty()) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be a non-empty string.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "float") {
        std::istringstream stream(value);
        float parsed = 0.0f;
        if (!(stream >> parsed) || !std::isfinite(parsed)) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be a finite number.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "positive_float") {
        std::istringstream stream(value);
        float parsed = 0.0f;
        if (!(stream >> parsed) || !std::isfinite(parsed) || parsed <= 0.0f) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be a positive number.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "nonnegative_float") {
        std::istringstream stream(value);
        float parsed = 0.0f;
        if (!(stream >> parsed) || !std::isfinite(parsed) || parsed < 0.0f) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be a non-negative number.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "normalized_float") {
        std::istringstream stream(value);
        float parsed = 0.0f;
        if (!(stream >> parsed) || !std::isfinite(parsed) || parsed < 0.0f || parsed > 1.0f) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be a normalized number from 0 to 1.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "int") {
        std::istringstream stream(value);
        int parsed = 0;
        if (!(stream >> parsed)) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be an integer.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "nonnegative_int") {
        std::istringstream stream(value);
        int parsed = 0;
        if (!(stream >> parsed) || parsed < 0) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be a non-negative integer.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "bool") {
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower != "true" && lower != "false" && lower != "0" && lower != "1") {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be true or false.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "vec3") {
        const std::array<float, 3> sentinel{98765.0f, 98765.0f, 98765.0f};
        if (ComponentVec3Property(component, schemaProperty.name, sentinel) == sentinel) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be three finite numbers.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "vec2") {
        const std::array<float, 2> sentinel{98765.0f, 98765.0f};
        if (ComponentVec2Property(component, schemaProperty.name, sentinel) == sentinel) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be two finite numbers.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "anchor") {
        const std::array<float, 2> sentinel{98765.0f, 98765.0f};
        const std::array<float, 2> parsed = ComponentVec2Property(component, schemaProperty.name, sentinel);
        if (parsed == sentinel || parsed[0] < 0.0f || parsed[0] > 1.0f || parsed[1] < 0.0f || parsed[1] > 1.0f) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be two normalized numbers from 0 to 1.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "size2") {
        const std::array<float, 2> parsed = ComponentVec2Property(component, schemaProperty.name, {0.0f, 0.0f});
        if (parsed[0] <= 0.0f || parsed[1] <= 0.0f || !std::isfinite(parsed[0]) || !std::isfinite(parsed[1])) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be two positive finite numbers.";
            }
            return false;
        }
        return true;
    }

    if (schemaProperty.kind == "color") {
        std::string normalized = value;
        std::replace(normalized.begin(), normalized.end(), ',', ' ');
        std::istringstream stream(normalized);
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
        if (!(stream >> r >> g >> b) || !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " must be three or four finite color numbers.";
            }
            return false;
        }
        if (stream >> a && !std::isfinite(a)) {
            if (diagnostic != nullptr) {
                *diagnostic = schemaProperty.name + " alpha must be finite.";
            }
            return false;
        }
        return true;
    }

    return true;
}

bool IsUiCanvasComponent(const std::string& type) {
    return type == "UICanvasRuntime";
}

bool IsUiElementComponent(const std::string& type) {
    return type == "UIPanelRuntime" || type == "UITextRuntime" || type == "UIButtonRuntime" ||
           type == "UIBoxRuntime" || type == "UIImageRuntime";
}

bool IsUiBindingComponent(const std::string& type) {
    return type == "UIStateBindingRuntime" || type == "UILayoutGroupRuntime" || type == "UIFocusRuntime" ||
           type == "UIAnimationRuntime" || type == "UIScriptCallbackRuntime";
}

bool IsUiComponent(const std::string& type) {
    return IsUiCanvasComponent(type) || IsUiElementComponent(type) || IsUiBindingComponent(type);
}

bool ValidateFogComponent(const Entity& entity, const Component& component, SceneValidationResult* result) {
    bool ok = true;
    auto fail = [&](const std::string& message) {
        ok = false;
        if (result != nullptr) {
            result->ok = false;
            result->diagnostics.push_back(entity.name + ".Fog: " + message);
        }
    };

    const std::string mode = ToLowerCopy(ComponentStringProperty(component, "mode", "Linear"));
    if (mode != "linear" && mode != "exponential") {
        fail("mode must be Linear or Exponential.");
    }

    const std::string colorValue = ComponentStringProperty(component, "color", "0, 0, 0, 1");
    std::string normalizedColor = colorValue;
    std::replace(normalizedColor.begin(), normalizedColor.end(), ',', ' ');
    std::istringstream colorStream(normalizedColor);
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    if (!(colorStream >> r >> g >> b) || !std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) ||
        r < 0.0f || r > 1.0f || g < 0.0f || g > 1.0f || b < 0.0f || b > 1.0f) {
        fail("color must contain RGB values from 0 to 1.");
    } else if (colorStream >> a && (!std::isfinite(a) || a < 0.0f || a > 1.0f)) {
        fail("color alpha must be from 0 to 1.");
    }

    float startDistance = 0.0f;
    float endDistance = 0.0f;
    const bool parsedStart = TryParseFiniteFloat(ComponentStringProperty(component, "startDistance"), &startDistance);
    const bool parsedEnd = TryParseFiniteFloat(ComponentStringProperty(component, "endDistance"), &endDistance);
    if (parsedStart && parsedEnd && endDistance <= startDistance) {
        fail("endDistance must be greater than startDistance.");
    }

    return ok;
}

bool ValidateEnvironmentLightingComponent(const Entity& entity, const Component& component, SceneValidationResult* result) {
    bool ok = true;
    auto fail = [&](const std::string& message) {
        ok = false;
        if (result != nullptr) {
            result->ok = false;
            result->diagnostics.push_back(entity.name + ".EnvironmentLighting: " + message);
        }
    };

    const std::string mode = ToLowerCopy(ComponentStringProperty(component, "skyboxMode", "Gradient"));
    if (mode != "gradient" && mode != "solid" && mode != "custom" && mode != "none") {
        fail("skyboxMode must be Gradient, Solid, Custom, or None.");
    }

    float exposure = 1.0f;
    if (TryParseFiniteFloat(ComponentStringProperty(component, "exposure", "1"), &exposure) && exposure <= 0.0f) {
        fail("exposure must be greater than zero.");
    }

    float horizonHeight = 0.52f;
    if (TryParseFiniteFloat(ComponentStringProperty(component, "horizonHeight", "0.52"), &horizonHeight) &&
        (horizonHeight < 0.0f || horizonHeight > 1.0f)) {
        fail("horizonHeight must be from 0 to 1.");
    }

    float ambientIntensity = 0.45f;
    if (TryParseFiniteFloat(ComponentStringProperty(component, "ambientIntensity", "0.45"), &ambientIntensity) &&
        ambientIntensity < 0.0f) {
        fail("ambientIntensity must be non-negative.");
    }

    float sunDiskSize = 0.035f;
    if (TryParseFiniteFloat(ComponentStringProperty(component, "sunDiskSize", "0.035"), &sunDiskSize) &&
        (sunDiskSize < 0.0f || sunDiskSize > 1.0f)) {
        fail("sunDiskSize must be from 0 to 1.");
    }

    float sunDiskIntensity = 0.9f;
    if (TryParseFiniteFloat(ComponentStringProperty(component, "sunDiskIntensity", "0.9"), &sunDiskIntensity) &&
        sunDiskIntensity < 0.0f) {
        fail("sunDiskIntensity must be non-negative.");
    }

    return ok;
}

const Component* FindFirstUiComponent(const Entity& entity) {
    auto it = std::find_if(entity.components.begin(), entity.components.end(), [](const Component& component) {
        return IsUiComponent(component.type);
    });
    return it == entity.components.end() ? nullptr : &(*it);
}

bool HasUiComponent(const Entity& entity) {
    return FindFirstUiComponent(entity) != nullptr;
}

bool HasUiCanvas(const Entity& entity) {
    return HasComponent(entity, "UICanvasRuntime");
}

bool HasUiElement(const Entity& entity) {
    return std::any_of(entity.components.begin(), entity.components.end(), [](const Component& component) {
        return IsUiElementComponent(component.type);
    });
}

const Entity* FindEntityById(const std::vector<Entity>& entities, int id) {
    auto it = std::find_if(entities.begin(), entities.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    return it == entities.end() ? nullptr : &(*it);
}

bool HasUiCanvasAncestor(const std::vector<Entity>& entities, const Entity& entity) {
    std::vector<int> visited;
    int cursor = entity.parentId;
    while (cursor != 0) {
        if (std::find(visited.begin(), visited.end(), cursor) != visited.end()) {
            return false;
        }
        visited.push_back(cursor);
        const Entity* candidate = FindEntityById(entities, cursor);
        if (candidate == nullptr) {
            return false;
        }
        if (HasUiCanvas(*candidate)) {
            return true;
        }
        cursor = candidate->parentId;
    }
    return false;
}

bool IsSupportedUIStateBindingKey(const std::string& stateKey) {
    return stateKey == "playFailed" || stateKey == "playGoalReached" || stateKey == "playing" ||
           stateKey == "paused" || stateKey == "editMode" || stateKey == "scorePositive";
}

} // namespace

SceneValidationResult ValidateEngineScene(const std::vector<Entity>& entities) {
    SceneValidationResult result;
    if (entities.empty()) {
        result.ok = false;
        result.diagnostics.push_back("Scene has no entities.");
        return result;
    }

    int cameraCount = 0;
    int playerControllerCount = 0;
    int gameRulesCount = 0;
    int inputMapCount = 0;
    int goalCount = 0;
    int collectiblePointTotal = 0;
    int requiredScore = 0;
    int physicsSettingsCount = 0;
    int physicsBodyOrColliderCount = 0;
    int uiCanvasCount = 0;
    int uiElementCount = 0;
    int uiBindingCount = 0;

    for (const Entity& entity : entities) {
        if (entity.name.empty()) {
            result.ok = false;
            result.diagnostics.push_back("Entity " + std::to_string(entity.id) + " has an empty name.");
        }
        if (!HasComponent(entity, "Transform")) {
            result.ok = false;
            result.diagnostics.push_back(entity.name + " is missing a Transform component.");
        }

        if (HasComponent(entity, "Camera")) {
            ++cameraCount;
        }
        if (HasComponent(entity, "CharacterControllerRuntime")) {
            ++playerControllerCount;
        }
        if (const Component* gameRules = FindComponent(entity, "GameRulesRuntime")) {
            ++gameRulesCount;
            requiredScore = std::max(requiredScore, ComponentIntProperty(*gameRules, "requiredScore", 0));
        }
        if (HasComponent(entity, "InputActionMapRuntime")) {
            ++inputMapCount;
        }
        if (HasComponent(entity, "GoalRuntime")) {
            ++goalCount;
        }
        if (const Component* collectible = FindComponent(entity, "CollectibleRuntime")) {
            collectiblePointTotal += std::max(0, ComponentIntProperty(*collectible, "points", 0));
        }
        if (HasPhysicsSettings(entity)) {
            ++physicsSettingsCount;
        }
        if (HasPhysicsBodyOrCollider(entity)) {
            ++physicsBodyOrColliderCount;
        }
        if (HasUiCanvas(entity)) {
            ++uiCanvasCount;
            if (entity.parentId != 0) {
                result.ok = false;
                result.diagnostics.push_back(entity.name + " has UICanvasRuntime but is not a root entity.");
            }
        }
        const bool entityHasUiElement = HasUiElement(entity);
        const bool entityHasUiBinding = std::any_of(entity.components.begin(), entity.components.end(), [](const Component& component) {
            return IsUiBindingComponent(component.type);
        });
        if (entityHasUiElement) {
            ++uiElementCount;
            if (entity.parentId == 0 || !HasUiCanvasAncestor(entities, entity)) {
                result.ok = false;
                result.diagnostics.push_back(entity.name + " has a UI element component but is not parented under a UICanvasRuntime.");
            } else {
                const Entity* parent = FindEntityById(entities, entity.parentId);
                if (parent == nullptr || !HasUiComponent(*parent)) {
                    result.ok = false;
                    result.diagnostics.push_back(entity.name +
                                                 " has a UI element component but its direct parent is not a UI canvas or UI element.");
                }
            }
        }
        if (entityHasUiBinding) {
            ++uiBindingCount;
            if (!entityHasUiElement && !HasUiCanvas(entity)) {
                result.ok = false;
                result.diagnostics.push_back(entity.name + " has UIStateBindingRuntime but no UI canvas or element component.");
            }
        }
        for (const Component& uiComponent : entity.components) {
            if (uiComponent.type == "UIButtonRuntime" && ComponentStringProperty(uiComponent, "action").empty()) {
                result.ok = false;
                result.diagnostics.push_back(entity.name + ".UIButtonRuntime has an empty action.");
            }
            if (uiComponent.type == "UIStateBindingRuntime") {
                const std::string stateKey = ComponentStringProperty(uiComponent, "stateKey");
                if (!IsSupportedUIStateBindingKey(stateKey)) {
                    result.ok = false;
                    result.diagnostics.push_back(entity.name + ".UIStateBindingRuntime has unsupported stateKey '" +
                                                 stateKey + "'.");
                }
            }
            if (uiComponent.type == "UIScriptCallbackRuntime" &&
                (ComponentStringProperty(uiComponent, "event").empty() ||
                 ComponentStringProperty(uiComponent, "scriptMethod").empty())) {
                result.ok = false;
                result.diagnostics.push_back(entity.name +
                                             ".UIScriptCallbackRuntime requires event and scriptMethod.");
            }
        }

        for (const Component& component : entity.components) {
            if (component.type == "Fog") {
                ValidateFogComponent(entity, component, &result);
            }
            if (component.type == "EnvironmentLighting") {
                ValidateEnvironmentLightingComponent(entity, component, &result);
            }

            const ComponentSchema* schema = FindSchema(component.type);
            if (schema == nullptr) {
                continue;
            }

            for (const ComponentSchemaProperty& propertySchema : schema->properties) {
                if (!HasProperty(component, propertySchema.name)) {
                    if (propertySchema.required) {
                        result.ok = false;
                        result.diagnostics.push_back(entity.name + "." + component.type + " is missing required property " +
                                                     propertySchema.name + ".");
                    }
                    continue;
                }

                std::string diagnostic;
                if (!ValidatePropertyKind(component, propertySchema, &diagnostic)) {
                    result.ok = false;
                    result.diagnostics.push_back(entity.name + "." + component.type + ": " + diagnostic);
                }
            }
        }
    }

    if (physicsBodyOrColliderCount > 0 && physicsSettingsCount == 0) {
        result.diagnostics.push_back(
            "Physics scene has no PhysicsSettings entity; runtime will use default gravity 0, -9.81, 0.");
    }

    if (uiElementCount > 0 && uiCanvasCount == 0) {
        result.ok = false;
        result.diagnostics.push_back("Scene has UI elements but no UICanvasRuntime.");
    }
    if (uiBindingCount > 0 && uiCanvasCount == 0) {
        result.ok = false;
        result.diagnostics.push_back("Scene has UI state bindings but no UICanvasRuntime.");
    }
    if (uiCanvasCount > 1) {
        for (const Entity& entity : entities) {
            const Component* uiComponent = FindFirstUiComponent(entity);
            if (uiComponent == nullptr || !IsUiElementComponent(uiComponent->type) || entity.parentId != 0 ||
                !ComponentStringProperty(*uiComponent, "canvasName").empty()) {
                continue;
            }
            result.ok = false;
            result.diagnostics.push_back(entity.name + " has " + uiComponent->type +
                                         " but multiple canvases exist and canvasName is empty.");
        }
    }

    if (gameRulesCount > 0) {
        if (inputMapCount == 0) {
            result.ok = false;
            result.diagnostics.push_back("Playable scene has GameRulesRuntime but no InputActionMapRuntime.");
        }
        if (playerControllerCount == 0) {
            result.ok = false;
            result.diagnostics.push_back("Playable scene has no CharacterControllerRuntime player.");
        }
        if (cameraCount == 0) {
            result.ok = false;
            result.diagnostics.push_back("Playable scene has no Camera.");
        }
        if (goalCount == 0) {
            result.ok = false;
            result.diagnostics.push_back("Playable scene has no GoalRuntime objective.");
        }
        if (collectiblePointTotal < requiredScore) {
            result.ok = false;
            result.diagnostics.push_back("Playable scene collectible points cannot satisfy required score " +
                                         std::to_string(requiredScore) + ".");
        }
    }

    if (result.diagnostics.empty()) {
        result.diagnostics.push_back("validate.scene passed: " + std::to_string(entities.size()) +
                                     " entities checked.");
    } else if (result.ok) {
        result.diagnostics.insert(result.diagnostics.begin(),
                                  "validate.scene passed with notes: " + std::to_string(entities.size()) +
                                      " entities checked.");
    }
    return result;
}

} // namespace aine

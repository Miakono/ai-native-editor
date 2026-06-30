#include "engine/assets/Sprite.h"
#include "engine/runtime/ComponentRegistry.h"
#include "engine/runtime/Runtime.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool NearlyEqual(float left, float right, float epsilon = 0.0001f) {
    return std::fabs(left - right) <= epsilon;
}

const aine::ComponentProperty* FindProperty(const aine::Component& component, const std::string& name) {
    for (const aine::ComponentProperty& property : component.properties) {
        if (property.name == name) {
            return &property;
        }
    }
    return nullptr;
}

void SetProperty(aine::Component& component, std::string name, std::string value) {
    for (aine::ComponentProperty& property : component.properties) {
        if (property.name == name) {
            property.value = std::move(value);
            return;
        }
    }
    component.properties.push_back({std::move(name), std::move(value)});
}

bool Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "sprite_tests failed: " << message << "\n";
        return false;
    }
    return true;
}

bool SpriteRectMathTest() {
    aine::SpriteRectPixels rect;
    if (!Expect(aine::ParseSpriteRectPixels("16, 32, 48, 64", &rect), "could not parse sprite rect")) {
        return false;
    }
    if (!Expect(NearlyEqual(rect.x, 16.0f) && NearlyEqual(rect.y, 32.0f) &&
                    NearlyEqual(rect.width, 48.0f) && NearlyEqual(rect.height, 64.0f),
                "parsed sprite rect values were wrong")) {
        return false;
    }

    const aine::SpriteUvRect uv = aine::SpriteRectToUv(rect, 128, 128);
    if (!Expect(NearlyEqual(uv.uMin, 0.125f) && NearlyEqual(uv.uMax, 0.5f) &&
                    NearlyEqual(uv.vMin, 0.25f) && NearlyEqual(uv.vMax, 0.75f),
                "sprite rect did not map to expected OpenGL UVs")) {
        return false;
    }

    const aine::SpriteWorldBounds2D centered =
        aine::SpriteWorldBoundsFromPixels(rect, 32.0f, {0.5f, 0.5f});
    if (!Expect(NearlyEqual(centered.size[0], 1.5f) && NearlyEqual(centered.size[1], 2.0f) &&
                    NearlyEqual(centered.offset[0], 0.0f) && NearlyEqual(centered.offset[1], 0.0f),
                "center pivot sprite bounds were wrong")) {
        return false;
    }

    const aine::SpriteWorldBounds2D bottomLeft =
        aine::SpriteWorldBoundsFromPixels(rect, 32.0f, {0.0f, 0.0f});
    if (!Expect(NearlyEqual(bottomLeft.offset[0], 0.75f) && NearlyEqual(bottomLeft.offset[1], 1.0f),
                "bottom-left pivot offset was wrong")) {
        return false;
    }

    return true;
}

bool SpriteComponentRegistryTest() {
    const aine::Component sprite = aine::MakeSpriteRendererComponent("asset_0001");
    const aine::Component animation = aine::MakeSpriteAnimationRuntimeComponent();
    if (!Expect(sprite.type == "SpriteRenderer" && FindProperty(sprite, "pixelsPerUnit") != nullptr &&
                    FindProperty(sprite, "sortingLayer") != nullptr,
                "SpriteRenderer defaults were incomplete")) {
        return false;
    }
    if (!Expect(animation.type == "SpriteAnimationRuntime" && FindProperty(animation, "frameRate") != nullptr,
                "SpriteAnimationRuntime defaults were incomplete")) {
        return false;
    }
    if (!Expect(aine::FindComponentDefinition("SpriteRenderer") != nullptr &&
                    aine::FindComponentDefinition("SpriteAnimationRuntime") != nullptr,
                "sprite components were not registered")) {
        return false;
    }
    return true;
}

bool SpriteAnimationRuntimeTest() {
    aine::EngineRuntime runtime;
    std::vector<aine::Entity> entities;
    aine::Entity sprite;
    sprite.id = 1;
    sprite.name = "Animated Sprite";
    sprite.activeSelf = true;
    sprite.components.push_back(aine::MakeTransformComponent());
    sprite.components.push_back(aine::MakeSpriteRendererComponent("sheet.png"));
    aine::Component animation = aine::MakeSpriteAnimationRuntimeComponent();
    SetProperty(animation, "frames", "0, 0, 16, 16;16, 0, 16, 16;32, 0, 16, 16");
    SetProperty(animation, "frameRate", "10");
    sprite.components.push_back(animation);
    entities.push_back(sprite);

    if (!Expect(runtime.Begin(entities), "runtime did not begin")) {
        return false;
    }
    runtime.Update(entities, 0.16f);

    const aine::Component* renderer = nullptr;
    const aine::Component* anim = nullptr;
    for (const aine::Component& component : entities.front().components) {
        if (component.type == "SpriteRenderer") {
            renderer = &component;
        }
        if (component.type == "SpriteAnimationRuntime") {
            anim = &component;
        }
    }
    if (!Expect(renderer != nullptr && anim != nullptr, "runtime entity lost sprite components")) {
        return false;
    }
    const aine::ComponentProperty* rect = FindProperty(*renderer, "rect");
    const aine::ComponentProperty* frame = FindProperty(*anim, "currentFrame");
    if (!Expect(rect != nullptr && rect->value == "16, 0, 16, 16", "sprite animation did not select frame rect 1")) {
        return false;
    }
    if (!Expect(frame != nullptr && frame->value == "1", "sprite animation currentFrame was not updated")) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (!SpriteRectMathTest() || !SpriteComponentRegistryTest() || !SpriteAnimationRuntimeTest()) {
        return 1;
    }
    return 0;
}

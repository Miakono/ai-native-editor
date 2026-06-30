#pragma once

#include <array>
#include <string>

namespace aine {

struct SpriteRectPixels {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct SpriteMetadata {
    std::string id;
    std::string name;
    std::string texture;
    SpriteRectPixels rect;
    float pixelsPerUnit = 100.0f;
    std::array<float, 2> pivot{0.5f, 0.5f};
    std::string packingTag;
    std::string atlas;
};

struct SpriteUvRect {
    float uMin = 0.0f;
    float vMin = 0.0f;
    float uMax = 1.0f;
    float vMax = 1.0f;
};

struct SpriteWorldBounds2D {
    std::array<float, 2> size{1.0f, 1.0f};
    std::array<float, 2> offset{0.0f, 0.0f};
};

bool ParseSpriteRectPixels(const std::string& value, SpriteRectPixels* outRect);
std::string FormatSpriteRectPixels(const SpriteRectPixels& rect);
SpriteRectPixels FullTextureSpriteRect(int textureWidth, int textureHeight);
SpriteUvRect SpriteRectToUv(const SpriteRectPixels& rect, int textureWidth, int textureHeight);
SpriteWorldBounds2D SpriteWorldBoundsFromPixels(const SpriteRectPixels& rect,
                                                float pixelsPerUnit,
                                                std::array<float, 2> pivot);

} // namespace aine

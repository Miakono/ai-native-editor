#include "engine/assets/Sprite.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace aine {
namespace {

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

std::string TrimTrailingZeros(std::string value) {
    while (value.size() > 1 && value.back() == '0') {
        value.pop_back();
    }
    if (!value.empty() && value.back() == '.') {
        value.pop_back();
    }
    return value.empty() ? "0" : value;
}

std::string FormatFloat(float value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return TrimTrailingZeros(stream.str());
}

} // namespace

bool ParseSpriteRectPixels(const std::string& value, SpriteRectPixels* outRect) {
    if (outRect == nullptr || value.empty()) {
        return false;
    }

    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::replace(normalized.begin(), normalized.end(), ';', ' ');
    std::istringstream stream(normalized);
    SpriteRectPixels parsed;
    if (!(stream >> parsed.x >> parsed.y >> parsed.width >> parsed.height)) {
        return false;
    }
    if (!std::isfinite(parsed.x) || !std::isfinite(parsed.y) || !std::isfinite(parsed.width) ||
        !std::isfinite(parsed.height) || parsed.width <= 0.0f || parsed.height <= 0.0f) {
        return false;
    }

    *outRect = parsed;
    return true;
}

std::string FormatSpriteRectPixels(const SpriteRectPixels& rect) {
    return FormatFloat(rect.x) + ", " + FormatFloat(rect.y) + ", " + FormatFloat(rect.width) + ", " +
           FormatFloat(rect.height);
}

SpriteRectPixels FullTextureSpriteRect(int textureWidth, int textureHeight) {
    return {0.0f, 0.0f, std::max(1.0f, static_cast<float>(textureWidth)),
            std::max(1.0f, static_cast<float>(textureHeight))};
}

SpriteUvRect SpriteRectToUv(const SpriteRectPixels& rect, int textureWidth, int textureHeight) {
    const float width = std::max(1.0f, static_cast<float>(textureWidth));
    const float height = std::max(1.0f, static_cast<float>(textureHeight));
    const float left = std::clamp(FiniteOr(rect.x, 0.0f), 0.0f, width);
    const float top = std::clamp(FiniteOr(rect.y, 0.0f), 0.0f, height);
    const float right = std::clamp(left + std::max(0.0f, FiniteOr(rect.width, width)), 0.0f, width);
    const float bottom = std::clamp(top + std::max(0.0f, FiniteOr(rect.height, height)), 0.0f, height);

    SpriteUvRect uv;
    uv.uMin = left / width;
    uv.uMax = right / width;
    uv.vMin = 1.0f - bottom / height;
    uv.vMax = 1.0f - top / height;
    return uv;
}

SpriteWorldBounds2D SpriteWorldBoundsFromPixels(const SpriteRectPixels& rect,
                                                float pixelsPerUnit,
                                                std::array<float, 2> pivot) {
    const float ppu = std::max(0.001f, FiniteOr(pixelsPerUnit, 100.0f));
    pivot[0] = std::clamp(FiniteOr(pivot[0], 0.5f), 0.0f, 1.0f);
    pivot[1] = std::clamp(FiniteOr(pivot[1], 0.5f), 0.0f, 1.0f);

    SpriteWorldBounds2D bounds;
    bounds.size = {std::max(0.001f, FiniteOr(rect.width, 1.0f)) / ppu,
                   std::max(0.001f, FiniteOr(rect.height, 1.0f)) / ppu};
    bounds.offset = {bounds.size[0] * (0.5f - pivot[0]), bounds.size[1] * (0.5f - pivot[1])};
    return bounds;
}

} // namespace aine

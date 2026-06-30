#pragma once

#include "engine/scene/Component.h"

#include <array>
#include <string>
#include <vector>

namespace aine {

struct Entity {
    int id = 0;
    int parentId = 0;
    std::string name;
    bool activeSelf = true;
    bool isStatic = false;
    std::string tag = "Untagged";
    std::string layer = "Default";
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    std::vector<Component> components;
};

} // namespace aine

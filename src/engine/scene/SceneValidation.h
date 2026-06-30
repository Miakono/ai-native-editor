#pragma once

#include "engine/scene/Entity.h"

#include <string>
#include <vector>

namespace aine {

struct SceneValidationResult {
    bool ok = true;
    std::vector<std::string> diagnostics;
};

SceneValidationResult ValidateEngineScene(const std::vector<Entity>& entities);

} // namespace aine

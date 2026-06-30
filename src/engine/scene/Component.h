#pragma once

#include <string>
#include <vector>

namespace aine {

struct ComponentProperty {
    std::string name;
    std::string value;
};

struct Component {
    std::string type;
    std::vector<ComponentProperty> properties;
};

struct ComponentSchemaProperty {
    std::string name;
    std::string kind;
    bool required = true;
};

struct ComponentSchema {
    std::string type;
    std::string category;
    std::vector<ComponentSchemaProperty> properties;
};

struct ComponentDefinition {
    ComponentSchema schema;
    Component defaultComponent;
    std::string displayName;
    bool allowMultiple = false;
    bool editorVisible = true;
};

} // namespace aine

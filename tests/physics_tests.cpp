#include "Physics.h"

#include <iostream>
#include <string>
#include <vector>

int main() {
    std::vector<std::string> diagnostics;
    if (!aine::RunPhysicsSelfTests(&diagnostics)) {
        for (const std::string& diagnostic : diagnostics) {
            std::cerr << diagnostic << '\n';
        }
        return 1;
    }

    for (const std::string& diagnostic : diagnostics) {
        std::cout << diagnostic << '\n';
    }
    return 0;
}

cmake_minimum_required(VERSION 3.25)

project(AINativeEditor
    VERSION 0.1.0
    DESCRIPTION "Native AI-first editor prototype"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw imgui nlohmann_json)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(imgui
    PUBLIC
        GLFW_INCLUDE_NONE
        IMGUI_ENABLE_DOCKING
)

target_link_libraries(imgui
    PUBLIC
        glfw
)

find_package(OpenGL REQUIRED)

add_library(ai_native_engine STATIC
    src/engine/core/Log.h
    src/engine/core/PerformanceProfiler.cpp
    src/engine/core/PerformanceProfiler.h
    src/engine/assets/Sprite.cpp
    src/engine/assets/Sprite.h
    src/engine/cave/CaveVolume.cpp
    src/engine/cave/CaveVolume.h
    src/engine/physics/Physics.cpp
    src/engine/physics/Physics.h
    src/engine/runtime/Camera.cpp
    src/engine/runtime/Camera.h
    src/engine/runtime/ComponentRegistry.cpp
    src/engine/runtime/ComponentRegistry.h
    src/engine/runtime/Runtime.cpp
    src/engine/runtime/Runtime.h
    src/engine/scene/Component.h
    src/engine/scene/Entity.h
    src/engine/scene/SceneSerialization.cpp
    src/engine/scene/SceneSerialization.h
    src/engine/scene/SceneValidation.cpp
    src/engine/scene/SceneValidation.h
    src/engine/scripting/ScriptCompiler.cpp
    src/engine/scripting/ScriptCompiler.h
    src/engine/scripting/ScriptRuntimeHost.cpp
    src/engine/scripting/ScriptRuntimeHost.h
    src/engine/terrain/Terrain.cpp
    src/engine/terrain/Terrain.h
)

target_include_directories(ai_native_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ai_native_engine
    PUBLIC
        nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(ai_native_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(ai_native_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ai_native_editor
    src/main.cpp
    src/AgentOrchestrator.cpp
    src/AgentOrchestrator.h
    src/ComponentRegistry.h
    src/EditorApp.cpp
    src/EditorApp.h
    src/EditorState.cpp
    src/EditorState.h
    src/Physics.h
    src/ProviderResult.cpp
    src/ProviderResult.h
    src/ToolGateway.cpp
    src/ToolGateway.h
)

target_compile_definitions(ai_native_editor
    PRIVATE
        GLFW_INCLUDE_NONE
        AINE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(ai_native_editor
    PRIVATE
        ai_native_engine
        imgui
        glfw
        OpenGL::GL
)

if(WIN32)
    target_link_libraries(ai_native_editor
        PRIVATE
            ole32
            shell32
            windowscodecs
    )
endif()

if(MSVC)
    target_compile_options(ai_native_editor PRIVATE /W4 /permissive-)
else()
    target_compile_options(ai_native_editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
    target_link_options(ai_native_editor PRIVATE -static-libgcc -static-libstdc++)
endif()

add_executable(aine_physics_tests
    tests/physics_tests.cpp
    src/ComponentRegistry.h
    src/Physics.h
)

target_include_directories(aine_physics_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(aine_physics_tests
    PRIVATE
        ai_native_engine
)

if(MSVC)
    target_compile_options(aine_physics_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(aine_physics_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME aine_physics_tests COMMAND aine_physics_tests)

add_executable(aine_runtime_tests
    tests/runtime_tests.cpp
    src/ComponentRegistry.h
    src/EditorState.cpp
    src/EditorState.h
    src/Physics.h
)

target_include_directories(aine_runtime_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(aine_runtime_tests
    PRIVATE
        ai_native_engine
)

if(MSVC)
    target_compile_options(aine_runtime_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(aine_runtime_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME aine_runtime_tests COMMAND aine_runtime_tests)

add_executable(aine_sprite_tests
    tests/sprite_tests.cpp
)

target_include_directories(aine_sprite_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(aine_sprite_tests
    PRIVATE
        ai_native_engine
)

if(MSVC)
    target_compile_options(aine_sprite_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(aine_sprite_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME aine_sprite_tests COMMAND aine_sprite_tests)

add_executable(aine_cave_tests
    tests/cave_tests.cpp
)

target_include_directories(aine_cave_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(aine_cave_tests
    PRIVATE
        ai_native_engine
)

if(MSVC)
    target_compile_options(aine_cave_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(aine_cave_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME aine_cave_tests COMMAND aine_cave_tests)

add_executable(aine_terrain_tests
    tests/terrain_tests.cpp
)

target_include_directories(aine_terrain_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(aine_terrain_tests
    PRIVATE
        ai_native_engine
)

if(MSVC)
    target_compile_options(aine_terrain_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(aine_terrain_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME aine_terrain_tests COMMAND aine_terrain_tests)

add_executable(ai_native_player
    src/player/main.cpp
)

target_include_directories(ai_native_player
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ai_native_player
    PRIVATE
        ai_native_engine
)

if(MSVC)
    target_compile_options(ai_native_player PRIVATE /W4 /permissive-)
else()
    target_compile_options(ai_native_player PRIVATE -Wall -Wextra -Wpedantic)
endif()

# System Architecture

## Overview

The editor should be built as a native application with a small stable core and extensible modules. The AI system should be part of that core from the first prototype.

High-level components:

- Editor Shell
- Scene System
- Asset Database
- Renderer
- Runtime Preview
- Scripting Layer
- Plugin System
- AI Chat Dock
- Agent Runtime
- Tool Command Gateway
- Validation and Build Runner

## Proposed Process Model

### Editor Process

The editor process owns UI, viewport presentation, undo/redo, asset browser, inspector, selected entity, project state, and editor session state. Runtime data and behavior come from `ai_native_engine`.

It exposes a typed command gateway that the AI agent can call.

### Engine Library

`ai_native_engine` owns engine data and runtime behavior:

- `Entity` and `Component`
- component schemas and default factories
- scene JSON serialization
- scene validation
- `EngineRuntime` and Play Mode runtime state
- physics simulation, queries, and physics events

Dependency rule:

- engine does not depend on editor
- player depends on engine
- editor depends on engine
- future game scripts depend on the engine scripting API
- engine does not depend on a specific game project

### Player Process

`ai_native_player` is the standalone runtime foundation. It loads a project or scene path, deserializes the same scene data as editor Play Mode, starts `EngineRuntime`, and ticks frames without ImGui or editor UI dependencies.

### Agent Worker Process

The agent worker can run out of process for safety and responsiveness. It handles LLM calls, long-running analysis, file indexing, code generation, and tool orchestration.

The user experience is still fully integrated: the editor is the only app the user needs to open.

### Build Worker Process

The build worker runs compilation, asset cooking, tests, packaging, and smoke checks. It should stream structured progress back to the editor and agent.

## Core Data Model

### Project

Project metadata:

- project name
- engine version
- enabled plugins
- asset root
- scenes
- script assemblies
- build targets
- AI project memory

### Scene

Scene data should be structured and diffable.

Initial scene concepts:

- entity id
- name
- parent id
- transform
- components
- asset references
- tags

### Component

Initial component types:

- Transform
- Camera
- MeshRenderer
- Light
- RigidBody
- Collider
- Script
- AudioSource

The prototype now includes a small engine-owned typed runtime-component schema registry for the first playable game. Schema-backed runtime components include `InputActionMapRuntime`, `CharacterControllerRuntime`, `CameraFollowRuntime`, `GameRulesRuntime`, `CollectibleRuntime`, `GoalRuntime`, `HazardRuntime`, `SpinRuntime`, runtime UI components, `AudioSourceRuntime`, `AnimatorRuntime`, `NavigationAgentRuntime`, `NetworkIdentityRuntime`, and `NetworkSessionRuntime`. Engine scene validation uses these schemas to catch missing required properties and incomplete playable scenes.

## Runtime Preview Systems

The current Play Mode runtime is intentionally small and lives in `EngineRuntime`, not editor UI code. It runs named system passes:

- `InputSystem`: captures named actions from the Game viewport.
- `SpinRuntimeSystem`: updates simple animated placeholders.
- `CharacterControllerSystem`: moves the currently controlled entity from action input.
- `PhysicsSystem`: steps component-driven 2D/3D physics.
- `TriggerRuntimeSystem`: handles collectible, hazard, and goal checks.
- `FallDeathRuntimeSystem`: evaluates simple game-rule failure state.
- `UIStateBindingRuntimeSystem`: drives runtime UI visibility from runtime state.
- `UIFocusRuntimeSystem` and `UIAnimationRuntimeSystem`: maintain focus and animation state for scene-authored runtime UI.
- `AudioRuntimeSystem`: captures first-pass audio playback events from scene components.
- `AnimationRuntimeSystem`: advances simple animator state time.
- `NavigationRuntimeSystem`: moves agents toward named targets.
- `NetworkingRuntimeSystem`: simulates local sessions and replicated identities for editor/runtime proof.
- `CameraFollowRuntimeSystem`: drives the built-in `Camera` component through follow, orbit, chase, top-down, side-scroll, fixed, cinematic, and free/fly modes while preserving `Camera` as the non-removable render identity on camera GameObjects.

`TriggerRuntimeSystem` can consume physics-trigger events and still falls back to temporary distance checks where a scene lacks trigger colliders.

## Scripting Path

Project scripts live under project `Assets/Scripts`. Runtime `.cs` files compile outside editor UI code through the engine-owned compiler, which writes generated API/build files and `compile_manifest.json` under `Library/Scripts`, builds `AINative.GameScripts.dll`, and reports diagnostics to the editor/tool gateway.

Play Mode loads the compiled script assembly through the engine scripting host, binds attached script components to runtime entities, invokes lifecycle callbacks, synchronizes exposed `[ScriptField]` values back to component data, and surfaces runtime errors without crashing the editor. The current host is out-of-process and optimized for correctness and diagnostics before a final embedded scripting runtime.

### Asset

The asset database should track:

- source path
- imported asset id
- type
- dependencies
- license/source metadata
- import settings
- cooked output

Current imports copy neutral source files into the project, discover sidecar dependencies for glTF/OBJ/MTL and common model sidecars, write resource/metadata/thumbnail JSON under `Library/ImportedAssets`, and expose those records through validation and `asset.getMetadata`. Scene and Game views now build a component/resource render queue before the OpenGL backend draws, so imported model references are tracked as renderer-facing resource items instead of only hardcoded placeholders.

## Editor UI Areas

- Viewport
- Scene hierarchy
- Inspector
- Asset browser
- Console
- AI chat dock
- Agent activity panel
- Validation/results panel
- Play/build controls

## AI Integration Path

The AI should not directly mutate live scene state. It should request changes through editor commands:

1. Agent proposes command batch.
2. Editor validates command schema.
3. Editor previews affected objects/files.
4. User approves when required.
5. Editor applies changes as one undoable transaction.
6. Validator runs.
7. Agent receives structured results.

## Self Development Path

Self-development should happen through plugin/module generation first.

Example:

1. User asks: "Add a dialogue tree editor."
2. Agent reads plugin API docs and current project structure.
3. Agent creates a new plugin in a sandbox branch/workspace.
4. Agent validates the manifest and installs the plugin into `Library/Plugins`.
5. Agent opens the editor with the plugin enabled.
6. Agent runs UI and data validation.
7. User sees a summary and can install, revise, or discard.

## Suggested Initial Technical Shape

This is not final, but it is a practical starting point:

- Native editor: C++ app
- UI: Dear ImGui docking or a native retained UI layer
- Window/input: SDL or GLFW
- Renderer bootstrap: DirectX 11/12 or Vulkan abstraction
- Asset format: internal JSON/YAML scene files plus binary cooked assets
- Scripting: C# scripts compile to `AINative.GameScripts.dll` and execute through the engine-owned runtime scripting host during Play Mode
- Build scripts: PowerShell plus CMake or another native build system
- Agent protocol: JSON-RPC or equivalent typed local protocol

The first prototype should bias toward speed and debuggability, not final renderer architecture.

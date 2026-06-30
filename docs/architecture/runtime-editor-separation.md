# Runtime / Editor Separation

## Dependency Rule

- `ai_native_engine` owns engine data and runtime behavior.
- `ai_native_editor` depends on `ai_native_engine`.
- `ai_native_player` depends on `ai_native_engine`.
- Future game scripts depend on the engine scripting API.
- `ai_native_engine` does not depend on the editor, player, ImGui, or a specific game project.

## What Moved To Engine

The first separation slice keeps the editor surface stable while moving shared runtime ownership into `src/engine`:

- `engine/scene`: `Component`, `Entity`, scene JSON serialization, and scene validation.
- `engine/runtime`: component schemas/default factories and `EngineRuntime`.
- `engine/physics`: `PhysicsWorld`, physics components, physics events, and physics self-tests.
- `engine/core`: shared log severity types.

`EngineRuntime` owns Play Mode runtime state: elapsed time, score, goal/failure flags, controlled entity, runtime status, transient runtime logs, runtime input, physics world, physics event dedupe, and the named runtime system order.

## What Stays In Editor

`EditorState` remains editor/project/session state:

- open project paths and project metadata
- selected entity
- edit-mode scene snapshot for Play Mode restore
- undo/redo stacks
- editor console logs, chat history, and agent activity
- project tree, asset import records, script authoring files, and plugin records
- editor-only project folder and save-location behavior

The editor now calls engine APIs for scene validation, scene JSON serialization, component defaults/schemas, physics, and runtime ticks. It still owns UI and session workflow around those APIs.

## Standalone Player

`ai_native_player` is a minimal foundation executable. It loads a project or scene path, deserializes the same scene JSON as the editor, starts `EngineRuntime`, ticks a fixed number of frames, and prints a runtime summary. It has no editor or ImGui dependency.

Example:

```powershell
.\build\windows-ninja-debug\ai_native_player.exe --project="projects\FirstGame\AI Native Project.aineproject.json" --smoke --frames=240
```

## Script Truth

Project scripts live under project `Assets/Scripts`. Runtime scripts now compile outside editor UI code through the engine-owned script compiler. The compiler writes generated API/build files and `compile_manifest.json` under `Library/Scripts`, builds `AINative.GameScripts.dll` with the local .NET SDK, and returns diagnostics to the editor/tool gateway.

The current path is:

- project scripts stay in project-owned `Assets/Scripts`
- scripts compile outside editor UI code through `engine/scripting`
- the engine exposes a first-pass generated scripting API stub for compilation
- the editor creates, attaches, and reports diagnostics for scripts
- Play Mode blocks when attached runtime scripts fail to compile
- Play Mode loads `AINative.GameScripts.dll` through the engine-owned runtime scripting host
- attached script components bind to runtime entities, invoke lifecycle callbacks, synchronize exposed fields, and report runtime errors through editor logs/activity

## Remaining Work

- Move more scene mutation helpers from `EditorState` into an `EngineScene` API.
- Replace compatibility include shims once source files include engine headers directly.
- Replace the current out-of-process .NET script host with an embedded or persistent host when performance and reload isolation become the bottleneck.
- Expand `ai_native_player` from smoke runner to packaged game host.

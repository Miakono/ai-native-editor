# Building

The first prototype is a native C++ desktop app using GLFW, OpenGL, and Dear ImGui docking.

Dependencies are fetched by CMake:

- GLFW 3.4
- Dear ImGui docking branch

## Windows

From the repository root:

```powershell
cmake --preset windows-ninja-debug
cmake --build --preset windows-ninja-debug
.\build\windows-ninja-debug\ai_native_editor.exe
```

For a launch smoke test that opens the native window briefly and exits:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test
```

For a local command gateway smoke test that submits the starter-scene prompt, saves JSON, reloads the project, opens the native window briefly, and exits:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --project-root="build\command-smoke-project" --smoke-test-prompt="create a starter scene" --smoke-test-reload
```

For the scene integration smoke test that selects a renderable hierarchy entity, edits its transform through the same state path as the inspector, renders the selected highlight, and verifies the viewport path:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-scene-integration --project-root="build\scene-integration-smoke"
```

For the editor layout smoke test that saves a named custom layout, proves built-in presets apply only when explicitly selected, reapplies the custom layout, and reloads the profile:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-editor-layouts --project-root="build\editor-layouts-smoke-project" --editor-profile-path="build\editor-layouts-smoke\editor-profile.json"
```

For the first game fixture smoke test that opens `projects\FirstGame`, enters Play Mode, drives the player through the action-input path, verifies camera follow plus score/goal runtime state, and confirms Stop restores edit state:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-first-game
```

For the runtime log capture smoke test that opens `projects\FirstGame`, captures play/input/collectible/goal events, and confirms transient runtime events clear after Stop:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-runtime-logs --smoke-test-frames=120
```

For the runtime scripting smoke test that compiles a C# script, loads `AINative.GameScripts.dll`, invokes lifecycle hooks, synchronizes exposed fields, and verifies script runtime events:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-runtime-scripting --project-root="build\runtime-scripting-smoke" --smoke-test-frames=120
```

For the runtime UI smoke test that authors a death-screen panel, saves/reloads it, validates it, triggers a fall-death state, renders the Game View UI overlay path, and executes the restart action:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-runtime-ui --project-root="build\runtime-ui-smoke" --smoke-test-frames=120
```

For the provider file-change smoke test that parses a provider `proposed_file_change`, previews/applies full replacement content, records session continuity under `docs/agent-memory/provider_file_changes.jsonl`, reverts it, and fails safely for diff-only proposals:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-provider-file-change --project-root="build\provider-file-change-smoke"
```

For the asset pipeline smoke test that imports a neutral glTF fixture, copies discovered buffer/texture dependencies, writes resource/metadata/thumbnail artifacts with license/source metadata, validates the asset database, and verifies the Scene renderer sees the imported model as resource-backed:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-asset-pipeline --project-root="build\asset-pipeline-smoke"
```

For the project builder smoke test that saves build settings, honors a selected client output folder, packages a Windows player `.exe`, writes build/multiplayer/installer/updater manifests, records executable hash/signing metadata, and runs the packaged player smoke path:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-project-builder --project-root="build\project-builder-smoke"
```

To run the maintained smoke matrix:

```powershell
.\scripts\run-smoke-tests.ps1
```

If the default debug executable is open and locked, run the matrix against a fresh already-built folder:

```powershell
.\scripts\run-smoke-tests.ps1 -SkipBuild -BuildDir build\runtime-logs-verify
```

The smoke runner covers:

- launch and framebuffer-backed viewport rendering
- scene hierarchy selection, transform sync, and selected renderable highlight
- first game fixture load, action input, camera follow, coin collection, goal completion, and play-mode restore
- runtime event capture for play lifecycle, input capture, pickups, goal completion, and stop cleanup
- runtime UI save/reload, validation, Game View overlay layout, fall-death binding, and restart action
- provider file-change preview/apply/revert/session logging plus diff-only failure recovery
- asset import records, neutral-format dependency copy, generated resource/metadata/thumbnail artifacts, license metadata, and renderer resource lookup
- project builder Windows `.exe` packaging, selected client output folders, copied project data, build/installer/updater manifests, signing metadata, multiplayer settings, and packaged player smoke
- Local Tool Gateway chat prompt, save, and reload
- Local Tool Gateway broad-gameplay fail-closed routing so broad feature requests do not add canned starter-scene placeholders
- provider output rejection for invalid JSON
- provider Proposed Actions approval gating
- Codex CLI health cases: not found, launch failed, cmd shim ready, fake run through Codex backend, WindowsApps skip when present, fail-closed provider errors without scene fallback, and real unavailable status when applicable

## Current Status

As of this integration pass, the prototype is stable for the core local loop: AI Chat can submit prompts, Local Tool Gateway can apply explicit deterministic scene commands, provider command batches and file changes are reviewed before approval, project/scene JSON saves and reloads, the viewport builds a component/resource render queue before drawing scene entities with selection highlights, Game View can render scene-serializable runtime UI, compiled C# scripts execute in Play Mode, and build packaging writes release metadata.

Known limitations:

- The built-in viewport now uses a component/resource render queue consumed by the OpenGL view backend. It is still not a full production renderer with decoded mesh/material buffers, shaders, or GPU resource lifetime management.
- Runtime scripting currently uses an engine-owned out-of-process .NET host for correctness, diagnostics, and reload isolation. It is not yet an embedded/persistent high-performance scripting VM.
- Runtime UI is screen-space only and supports canvas/panel/text/button/box/image rendering, basic anchors, visibility/order, layout/focus/animation component state, simple state bindings, restart/reset button actions, and `script:` callback events. It does not yet support real font asset loading, rich text, or textured image sampling in the OpenGL path.
- Local Tool Gateway prompts are deterministic and apply immediately only for explicit supported asks. Broad gameplay/editor feature requests route to a diagnostic or development backend instead of mutating the scene with canned objects. External provider batches require approval.
- Provider file changes can be previewed, applied when full content is supplied, reverted from captured previous content, and logged as JSONL sessions. Diff-only proposals remain preview-only and fail safely if applied directly.
- The asset pipeline imports neutral glTF/FBX/OBJ/image/audio/material files into project assets and generated `Library/ImportedAssets` metadata. The renderer queue sees imported model resources, but full mesh/material decoding remains future renderer work.
- Plugin support loads generated manifests, validates/copies plugin source into `Library/Plugins`, and persists install/enable state. It does not yet dynamically link third-party native plugin code into the running editor.
- Codex CLI availability depends on how Codex is installed. Package-private WindowsApps paths may be visible to shell discovery but fail with access denied; the editor reports that exact status without mutating the scene through Local Tool Gateway.
- On Windows, Codex CLI runs from the editor workspace root with `danger-full-access` by default because the Codex read-only sandbox helper can fail with access denied. Embedded editor calls also use lean Codex exec defaults (`--ignore-user-config`, `--ephemeral`, and `--color never`) so short prompts do not inherit desktop MCP/plugin startup cost or high reasoning settings. Set `AI_NATIVE_CODEX_USE_USER_CONFIG=1` to restore normal Codex config inheritance. Set `AI_NATIVE_CODEX_SANDBOX`, `AI_NATIVE_CODEX_TIMEOUT_MS`, `AI_NATIVE_CODEX_MODEL`, `AI_NATIVE_CODEX_REASONING_EFFORT`, or `AI_NATIVE_CODEX_EPHEMERAL` to override launch behavior.
- Claude remains a provider-health placeholder until a real adapter is implemented.

## Scene View Controls

The Scene view follows Unity-style navigation:

- `F` frames the selected GameObject.
- Right mouse drag looks around. While holding right mouse, use `W/A/S/D` to fly, `Q` to move down, and `E` to move up.
- Middle mouse drag pans. `Alt` + left mouse drag orbits. `Alt` + right mouse drag zooms.
- Mouse wheel zooms, and mouse wheel while right mouse is held changes fly speed.

The app writes the ImGui docking layout to `ai_native_editor.ini` in the working directory. The default project lives under `projects/DefaultProject` unless `--project-root=...` is supplied. Use `--open-project="projects\FirstGame\AI Native Project.aineproject.json"` to open the first game fixture without creating or replacing a temporary project.

## Release Build

```powershell
cmake --preset windows-ninja-release
cmake --build --preset windows-ninja-release
.\build\windows-ninja-release\ai_native_editor.exe
```

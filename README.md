# AI Native Editor

Planning project for a native, original, AI-integrated game engine editor.

The core product idea is a game editor where the agent lives inside the editor as a first-class development surface. The user should be able to keep the editor open, talk to the built-in AI chat, and have the agent inspect the project, edit scenes, generate code, run validation, compile, test, and package builds through controlled editor tools.

## Current Direction

- Build our own original editor and runtime.
- Do not fork Godot.
- Do not use Electron or Tauri for the main editor shell.
- Do not copy Unity or Unreal source, editor internals, or proprietary asset formats.
- Support user-owned assets through neutral import paths and license metadata.
- Make AI integration one of the first foundations, because it will accelerate development of the editor itself.

## First Planning Files

- [Product Brief](docs/planning/product-brief.md)
- [MVP Roadmap](docs/planning/mvp-roadmap.md)
- [Implementation Backlog](docs/planning/implementation-backlog.md)
- [Risks and License Notes](docs/planning/risks-and-license-notes.md)
- [System Architecture](docs/architecture/system-architecture.md)
- [Engine Substrate Roadmap](docs/architecture/engine-substrate-roadmap.md)
- [Render Pipeline Model](docs/architecture/render-pipeline-model.md)
- [Runtime / Editor Separation](docs/architecture/runtime-editor-separation.md)
- [AI Editor Agent](docs/ai/ai-editor-agent.md)
- [Editor Tool API](docs/ai/editor-tool-api.md)
- [Agent Backends And Local Assist](docs/ai/agent-backends-and-local-assist.md)
- [Decision 0001: Native Original Editor](docs/decisions/0001-native-original-editor.md)
- [Decision 0002: AI Chat Is A Core Feature](docs/decisions/0002-ai-chat-is-core.md)
- [Decision 0003: Multi Agent Backends And Local Prompt Assist](docs/decisions/0003-multi-agent-backends-and-local-prompt-assist.md)

## Prototype Goal

The first functional prototype should prove this loop:

1. Open the editor.
2. Use the built-in AI chat.
3. Ask the agent to create or modify a scene.
4. The agent calls typed editor tools, not raw uncontrolled file edits.
5. The editor applies changes through undoable transactions.
6. The agent runs validation.
7. The user can press play and see the result.

That loop matters more than advanced graphics in the first milestone. Once the AI loop works, it can help build the rest of the editor.

## Agent Strategy

The editor should not be locked to one model or one CLI. It should have an agent backend layer:

- Claude backend for high-quality long-horizon coding and game-development planning.
- Codex CLI backend for local repo development, command execution, build/test loops, and OpenAI coding workflows.
- Local lightweight assistant for instant prompt suggestions, prompt improvements, scene-aware hints, and game-design guidance while the user types.

The editor UI stays the same regardless of backend. The agent worker decides which backend handles the request and routes all changes back through the editor tool gateway.

## Current Prototype

The repository now contains a narrow native C++ editor shell:

- GLFW desktop window
- Dear ImGui docking layout
- `ai_native_engine` static library for engine-owned scene data, component schemas, serialization, validation, runtime systems, and physics
- minimal `ai_native_player` executable that loads a project/scene and ticks `EngineRuntime` without editor UI
- project-level renderer profiles for `basic-built-in`, `2d`, `lightweight-3d`, and future `high-fidelity-3d`, with graphics backend metadata kept separate from user-facing profile selection
- framebuffer-backed viewport that builds a component/resource render queue, then draws scene entities, grid, camera/light gizmos, and selection outlines through the current OpenGL backend
- MVP 2D/3D physics runtime with fixed timestep rigid bodies, colliders, triggers, raycasts, overlap queries, and collider/contact debug overlays
- Play Mode runtime log capture for play lifecycle, input capture, physics contacts, pickups, hazards, and goal events
- scene-serializable runtime UI components rendered in Game View: `UICanvasRuntime`, `UIPanelRuntime`, `UITextRuntime`, `UIButtonRuntime`, `UIBoxRuntime`, `UIImageRuntime`, `UIStateBindingRuntime`, `UILayoutGroupRuntime`, `UIFocusRuntime`, `UIAnimationRuntime`, and `UIScriptCallbackRuntime`
- scene hierarchy
- inspector
- console/log panel
- Codex-first AI chat dock with conversation rail, pinned chats, search, and profile-persisted chat state
- agent activity/status panel
- advanced provider diagnostics for Codex CLI, OpenAI API, Claude, and internal Tool Gateway status
- deterministic local tool command gateway
- project/source tree panel
- Build Settings / Project Builder window for target-aware Windows, Linux, and macOS player packaging, project-data staging, build/installer/updater manifests, signing metadata, and multiplayer configuration metadata

The AI chat now routes through an `AgentOrchestrator` abstraction with Codex CLI as the normal in-editor chat backend. Local Prompt Assistant is temporarily disabled while Codex responses are hardened. Local Tool Gateway remains available as the deterministic command applicator for approved Codex command batches and explicit internal smoke coverage, but broad gameplay or editor feature requests should go to Codex CLI or return a diagnostic instead of silently adding canned placeholder scene objects. External providers such as Codex CLI, OpenAI API, and Claude fail closed when unavailable or timed out instead of mutating the live scene through local fallback. Codex CLI is optional, detected on `PATH` or `AI_NATIVE_CODEX_PATH`, and invoked as a subprocess from the editor workspace root with compact project context when available. OpenAI API is a separate backend that reads `OPENAI_API_KEY` from the process environment only; API keys are never written to project files. Claude is registered as a health/status placeholder until an SDK or CLI integration is implemented.

Provider executable paths can be overridden with environment variables:

- `AI_NATIVE_CODEX_PATH`
- `AI_NATIVE_CLAUDE_PATH`

OpenAI API uses:

- `OPENAI_API_KEY`

Provider health reports `Connected`, `Missing API key`, `Not found`, `Login required`, `WindowsApps alias inaccessible`, `Launch failed`, or `Timed out` with the exact candidate path and source when applicable.

Current integration status:

- Local Tool Gateway can submit chat prompts, create/save/reload scene JSON, and update the visible viewport.
- Codex CLI health is probed through environment override, known installs, cmd shim, PowerShell shim, and direct PATH candidates. If Codex is installed but not authenticated, the Agent panel shows a setup card with Log In To Codex, Refresh Codex, path override, and install/docs actions.
- Codex CLI runs use a lean embedded exec mode by default: `--ignore-user-config`, `--ephemeral`, `--color never`, the editor sandbox setting, and a 180 second timeout. This avoids inheriting desktop Codex MCP/plugin config and high reasoning settings that can make short editor prompts feel slow. Set `AI_NATIVE_CODEX_USE_USER_CONFIG=1` to use the normal Codex desktop config, `AI_NATIVE_CODEX_MODEL` to select a model, `AI_NATIVE_CODEX_REASONING_EFFORT` to set reasoning effort, `AI_NATIVE_CODEX_EPHEMERAL=0` to keep sessions, or `AI_NATIVE_CODEX_TIMEOUT_MS` to override timeout.
- OpenAI API health reports whether `OPENAI_API_KEY` is present and uses the Responses API backend without storing the key.
- The Agent panel shows the current run id, backend, elapsed time, stage, recent activity, command/output summaries, changed entities/files, and failure reason without requiring the raw-log view.
- Provider command batches are normalized, displayed under Proposed Actions in the main Agent workflow with diagnostics and changed entities/files, and must be approved before applying.
- Provider file changes are normalized into Proposed Actions with diff/content preview, full-content apply, captured revert, JSONL session logging, and diff-only failure recovery.
- Agent visual context captures real Scene and Game framebuffer PNGs under `docs/agent-memory/visuals`, references them in generated prompts and `run_log.jsonl`, and supports lightweight frame-sequence recording without requiring ffmpeg.
- Hierarchy selection drives the inspector and viewport highlight. Inspector transform edits update the rendered viewport immediately.

Scene view navigation follows Unity-style defaults:

- `F` frames the selected GameObject from the hierarchy or scene view.
- Right mouse drag looks around; hold right mouse and use `W/A/S/D` plus `Q/E` for flythrough movement.
- Middle mouse drag pans. `Alt` + left mouse drag orbits. `Alt` + right mouse drag zooms.
- Mouse wheel zooms, or changes fly speed while right mouse flythrough is active.

Scene/project persistence is now JSON-backed. The editor creates a default project structure at `projects/DefaultProject`:

- `Assets/Materials`
- `Assets/Meshes`
- `Assets/Prefabs`
- `Assets/Scenes`
- `Assets/Scripts/Gameplay`
- `Assets/Scripts/Editor`
- `Assets/Textures`
- `Plugins/GeneratedTools`
- `ProjectSettings`
- `Library`
- `Temp`
- `Logs`
- `Saved`

Project metadata now records the asset root, scene root, and game save-data root. New projects save the active scene under `Assets/Scenes/Prototype.scene.json` and default runtime save data to `Saved`, while older projects with root-level `Scenes` continue to open. The File menu owns project folder, Ctrl+S quick save, Explorer-style project Save As, scene Save As, and game-save-folder actions. The Project panel stays focused on the game work tree and exposes folder/file actions through right-click context menus, plus direct project Save As from the toolbar. Scene-changing chat requests still apply through structured command batches in the editor command gateway rather than letting external providers mutate live editor state directly.

Runtime/editor boundary status:

- Engine-owned: `Entity`, `Component`, component schemas/defaults, scene JSON serialization, scene validation, `EngineRuntime`, runtime logs/state, and physics.
- Editor-owned: UI, project/session paths, selected entity, undo/redo, edit-mode Play snapshot, editor logs/chat/activity, asset import records, script authoring files, plugins, and project tree.
- Dependency rule: engine does not depend on editor; editor and player depend on engine; future game scripts should depend on the engine scripting API.

Script status: `.cs` files under `Assets/Scripts` now compile through the project script compiler. The compiler discovers runtime scripts, writes generated API/build files and `compile_manifest.json` under `Library/Scripts`, builds `AINative.GameScripts.dll` with the local .NET SDK, and feeds diagnostics back through `script.compile`, `script.getDiagnostics`, `validate.scripts`, and Play Mode startup. Play Mode loads the compiled assembly through the engine-owned out-of-process scripting host, invokes lifecycle hooks, synchronizes exposed fields, and surfaces runtime errors.

Manual test prompt:

```text
create a starter scene
```

That prompt proposes and applies `project.getState`, `scene.createEntity`, `scene.setTransform`, `scene.addComponent`, `scene.renameEntity`, `validate.scene`, and `project.save`, then writes the active scene to disk.

## First Game Fixture

The first editor-native game is a separate project fixture at `projects/FirstGame`. It is intentionally not mixed into `projects/DefaultProject` so gameplay checks can open it, mutate play-mode state, and restore cleanly without polluting the editor's default prototype scene.

`First Game - Coin Dash` uses the current runtime primitives: a blue player cube with `CharacterControllerRuntime`, an `InputActionMapRuntime`, a camera with `CameraFollowRuntime`, three gold collectibles with `CollectibleRuntime`, side hazards with `HazardRuntime`, a green `GoalRuntime` gate, and a `GameRulesRuntime` entity that defines the required score.

The prototype runtime now runs named system passes in Play Mode:

- `InputSystem`
- `SpinRuntimeSystem`
- `CharacterControllerSystem`
- `PhysicsSystem`
- `TriggerRuntimeSystem`
- `FallDeathRuntimeSystem`
- `UIStateBindingRuntimeSystem`
- `UIFocusRuntimeSystem`
- `UIAnimationRuntimeSystem`
- `AudioRuntimeSystem`
- `AnimationRuntimeSystem`
- `NavigationRuntimeSystem`
- `NetworkingRuntimeSystem`
- `CameraFollowRuntimeSystem`

Runtime events are captured in a bounded transient stream during Play Mode and surfaced in the Game View overlay. The stream is intentionally separate from generic editor console logs and clears when Stop restores the edit-mode scene.

Agent visual capture is separate from runtime logs. The Agent panel can manually capture Scene/Game screenshots, auto-captures before worker-backed agent runs, and can record periodic Scene/Game PNG frame sets for visual debugging. The editor exposes `runtime.captureScreenshot`, `runtime.startRecording`, and `runtime.stopRecording` as editor-owned visual commands in provider command batches.

`TriggerRuntimeSystem` still uses simple distance checks as a temporary gameplay bridge. It is isolated so a physics/collider system can replace those checks with real trigger events without rewriting the gameplay components.

The editor also has a first pass at reusable gameplay templates under `GameObject > Gameplay Templates` and runtime UI templates under `GameObject > UI Templates`: Canvas, Panel, Text, Button, HUD Label, and Death Screen Panel. The Death Screen Panel template is a hidden panel with a `playFailed` state binding and a `restart` button action.

Runtime UI is intentionally basic in this pass. It is screen-space Game View UI, not editor ImGui. It supports hierarchy parenting under a canvas, anchors, pixel size scaled against the canvas reference size, color, text, image/box rectangles, visibility, order, simple layout/focus/animation component state, state bindings, restart/reset button actions, and `script:` callback events. It does not yet include real font assets, rich text, or textured image sampling.

Open it directly:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --open-project="projects\FirstGame\AI Native Project.aineproject.json"
```

Run the standalone player smoke path:

```powershell
.\build\windows-ninja-debug\ai_native_player.exe --project="projects\FirstGame\AI Native Project.aineproject.json" --smoke --frames=240
```

Build a packaged player from the editor through `Build > Build Settings` or `Build > Package Selected Target`. Build Settings supports `Windows`, `Linux`, and `macOS` targets with platform defaults of `Builds/Windows`, `Builds/Linux`, and `Builds/macOS`. The packager copies a matching target-specific `ai_native_player` runtime, stages project data under `Data` or inside the macOS `.app` bundle resources, writes build/installer/updater manifests with executable hashes and signing metadata, writes platform launchers, and persists multiplayer settings under `ProjectSettings/MultiplayerSettings.json`. Windows packages can be launch-smoked on Windows; Linux/macOS packages require a matching target runtime and otherwise report `Unavailable` instead of pretending cross-compilation happened. Networking support is local runtime component simulation plus project/build metadata; internet transport is still future work.

Run its dedicated smoke test:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-first-game
```

Run the performance profiler smoke test against the same gameplay fixture:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-profiler --open-project="projects\FirstGame\AI Native Project.aineproject.json" --smoke-test-frames=120
```

Run the runtime log capture smoke test:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-runtime-logs --smoke-test-frames=120
```

Run the runtime UI death-screen smoke test:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-runtime-ui --project-root="build\runtime-ui-smoke" --smoke-test-frames=120
```

Run the project builder smoke test:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-project-builder --project-root="build\project-builder-smoke"
```

Compile project scripts through the Tool Gateway smoke path:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-expanded-tools --project-root="build\expanded-tool-api-smoke"
```

## Physics Demos

The `projects/PhysicsDemos` fixture contains separate 2D and 3D scenes for the built-in physics runtime. Both scenes include dynamic falling bodies, static floor/wall colliders, trigger zones, and debug raycast probes. Enable the Scene viewport `Physics` toggle to see collider, contact/trigger, and raycast overlays.

Open the active 3D demo:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --open-project="projects\PhysicsDemos\AI Native Project.aineproject.json"
```

Run the standalone physics tests:

```powershell
.\build\windows-ninja-debug\aine_physics_tests.exe
```

Run the editor physics smoke test:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-physics
```

## Build And Run

Windows with CMake and Ninja:

```powershell
cmake --preset windows-ninja-debug
cmake --build --preset windows-ninja-debug
.\build\windows-ninja-debug\ai_native_editor.exe
```

Launch smoke test:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test
```

Structured command/save/reload smoke test:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --project-root="build\command-smoke-project" --smoke-test-prompt="create a starter scene" --smoke-test-reload
```

Project folder/save-location smoke test:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-project-folder --project-root="build\project-folder-smoke"
```

Project tree action smoke test:

```powershell
.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-project-tree-actions --project-root="build\project-tree-actions-smoke"
```

Full smoke suite:

```powershell
.\scripts\run-smoke-tests.ps1
```

More detail: [Building](docs/building.md).

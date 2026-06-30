# Engine Substrate Roadmap

This project is an original native editor and runtime. Godot is useful here only as a category reference for what mature engines usually separate into clear layers: core values and object metadata, scene/runtime lifecycle, servers for rendering/physics/audio/navigation, editor tooling, optional modules, and import/export surfaces. This document does not copy Godot code, names, APIs, or file layout.

## Current Audit

The current prototype proves the AI-driven editor loop, scene JSON persistence, an inspector, a structured command gateway, a physics/runtime loop, script compile/host execution, provider file-change approval, neutral asset import artifacts, plugin manifest install state, Windows packaging metadata, Play Mode, and the FirstGame fixture. It also has useful early boundaries:

- `EditorState.*` owns project metadata, selected entity, undoable mutations, editor session state, edit-mode Play snapshots, asset/script/plugin authoring records, and project tree state.
- `src/engine/scene` owns `Entity`, `Component`, scene JSON serialization, and scene validation.
- `src/engine/runtime` owns component schemas/default factories and `EngineRuntime`.
- `EditorApp.*` owns desktop UI, Game/Scene panels, inspector controls, menus, smoke entry points, and backend status surfaces.
- `ToolGateway.*` owns deterministic local command batches and provider command validation.
- `AgentOrchestrator.*` and `ProviderResult.*` own provider selection, run status, command/result normalization, and activity summaries.
- `src/engine/physics` owns the current standalone physics primitives and runtime contact data.
- `src/player` owns the minimal standalone player foundation.
- `tests`, `projects/FirstGame`, `projects/PhysicsDemos`, and `scripts/run-smoke-tests.ps1` are the current proof harness.

The main substrate gap was that component metadata lived in scattered factory functions, validation branches, inspector rules, and command normalization logic. That made it easy for runtime components to serialize but hard for the editor, validation, templates, and agents to agree on what a component is. The in-game UI layer was also missing a serializable control model that belongs to the game scene instead of editor ImGui.

Recent run history in `docs/agent-memory/run_log.jsonl` showed another gap: broad game-design prompts could fall into deterministic local scene creation and add canned placeholder objects. Local tools need to mutate only for explicit supported intents; broad requests should route to a development backend or return a diagnostic.

## First Foundation Slice

The first practical slice is a centralized component/type registry:

- Registry-owned component definitions provide type, category, display name, schema, and default properties.
- Component defaults are merged from the registry for provider commands.
- Scene validation and inspector rendering use schema kinds from the registry instead of duplicated type tables.
- Local Tool Gateway rejects unknown component types unless they are explicit script-backed components.
- Runtime UI components are normal scene components, not hardcoded editor-only widgets.

This is intentionally a substrate step, not a full engine rewrite. Existing scenes, fixtures, project metadata, physics, and AI memory behavior remain compatible.

## Ownership Boundaries

Target boundaries should become:

- `ai_native_engine`: entity/component data, scene serialization, scene validation, component definitions, runtime lifecycle, physics simulation, and query authority behind component-driven scene data.
- `EditorState`: durable project/session state, edit-mode scene snapshots, selected entity, undoable editor mutations, asset/script/plugin records, and project tree state.
- `ComponentRegistry`: engine component definitions, schemas, defaults, categories, and registry self-tests.
- `Physics`: engine physics simulation and query authority behind component-driven scene data.
- `EngineRuntime`: Play Mode runtime state and named runtime systems.
- `ai_native_player`: standalone runtime host foundation.
- `EditorApp`: editor presentation only: panels, menus, inspector UI, viewports, smoke wiring, and user approval flows.
- `ToolGateway`: structured command schema validation and deterministic local intent support only.
- `AgentOrchestrator`: backend routing, run lifecycle, stages, event summaries, proposed commands, changed files/entities, and failures.
- `ProviderResult`: provider output normalization, diagnostics, and safe conversion into command batches.

`EditorState` and `EditorApp` are still overloaded. Future work should move more scene mutation helpers into an engine scene API and split editor panels, component inspectors, and templates into smaller modules.

## Phases

### Foundation

- Expand the component registry into the single source for built-in components, schema kinds, default values, editor-visible categories, multiplicity rules, and documentation export.
- Add a value/property layer with typed storage rather than string-only component properties.
- Introduce resource IDs and stable references for assets, scenes, prefabs, scripts, materials, and UI assets.
- Keep validation centralized and executable in tests, smokes, and agent preflight.

### Scene/Runtime

- Grow the new `EngineRuntime` split while moving more scene operations out of `EditorState`.
- Add a lifecycle model for scene load, play enter, fixed update, frame update, pause, stop, and teardown.
- Add prefab/resource instancing with clear edit-mode and play-mode behavior.
- Replace temporary gameplay distance checks with component-driven physics trigger events where possible.

### Assets

- Continue expanding the asset database beyond current import records, source paths, generated artifacts, hashes, dependency copies, thumbnails, metadata, and license notes.
- Track dependencies between scenes, prefabs, scripts, meshes, textures, materials, audio, and UI images.
- Add import workers, reimport, cache invalidation, and production mesh/material decoding under the project `Library` directory.

### UI

- Grow the current Game View UI from screen-space canvas, panel, text, button, box/image, layout/focus/animation state, state binding, restart action, and `script:` callback events into a real control system.
- Add font assets, textured image rendering, richer focus/navigation, disabled/hover/pressed states, and deeper script callback binding.
- Keep game UI rendering separate from editor ImGui UI.

### Scripting

- Continue hardening script component identity, lifecycle hooks, exposed properties, serialization, and out-of-process host diagnostics.
- Keep script compile/load diagnostics visible in the Agent panel and console.
- Route script creation through the tool gateway with explicit file/entity changes.
- Current `.cs` files compile into `AINative.GameScripts.dll` and execute in Play Mode through the engine-owned scripting host; future work should optimize host lifetime and embedding.

### Rendering

- Current Scene and Game views build a component/resource render queue for terrain, mesh resources, cameras, and lights before the OpenGL backend draws them.
- Add material and texture asset resources.
- Decode imported mesh/material buffers into backend resources instead of drawing resource-backed model placeholders.
- Keep Scene View editor gizmos separate from Game View runtime rendering.

### Audio

- Expand current `AudioSourceRuntime` event playback into audio resources, listeners, emitters, buses, mixer settings, and actual device output.
- Add runtime events for audio failures and missing resources.

### Animation

- Expand current `AnimatorRuntime` time advancement into clips, timelines/state machines, animated properties, skeletal placeholders, and runtime sampling.
- Make animation validation check missing targets and incompatible property types.

### Navigation

- Expand current `NavigationAgentRuntime` target movement into navigation surfaces, obstacle components, debug overlays, and validation for missing nav data.
- Keep navigation as its own runtime system rather than hiding it inside gameplay components.

### Build/Export

- Continue expanding current build targets, platform settings, content/installer/updater manifests, export validation, signing metadata, and reproducible output folders.
- Separate editor smoke success from packaged build success.

### Debugging

- Expand runtime logs into filterable event streams with system, entity, frame, and severity metadata.
- Add live inspection of play-mode component state, physics events, UI hit regions, agent commands, and resource load failures.

### Plugins

- Expand current plugin manifests, install manifests, enable/disable state, and source validation into editor tool registration, component registration hooks, compile/test execution, and uninstall/revert flows.
- Keep plugin-provided components discoverable through the same registry contract as built-ins.

### AI Workflow

- Keep Local Tool Gateway deterministic and narrow.
- Route broad gameplay, editor feature, scripting, asset pipeline, or architecture requests to a development backend such as Codex.
- Agent run cards should continue to expose current stage, recent events, proposed/applied commands, changed files/entities, changed repository files, diagnostics, and failure reasons.
- Provider output must fail closed when it cannot be normalized into explicit tool commands or safe diagnostics.

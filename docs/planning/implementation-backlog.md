# Implementation Backlog

## P0: Foundation

- Choose initial native app stack.
- Create repository layout.
- Add build scripts.
- Create blank editor window.
- Add docking editor layout.
- Add logging system.
- Add project file format.
- Add scene file format.
- Add entity ids.
- Add component serialization.

## P0: AI Integration

- Add AI chat dock UI.
- Add agent worker process contract.
- Add agent backend adapter interface.
- Add backend selector UI.
- Add local prompt assistant panel/chips.
- Add tool command gateway.
- Add command schema validation.
- Add command transaction logging.
- Add undo/redo integration for command batches.
- Add changed-file and changed-entity summaries.
- Add project diagnostics API.
- Add project context snapshot API for agents.
- Add stub backends for local assistant, Claude, and Codex CLI.

## P0: Project Awareness

- Add hot context snapshot: current scene, selection, logs, diagnostics.
- Add warm context snapshot: recently edited files, recent commands, current task.
- Add cold context index: project files, assets, scripts, docs, settings.
- Add context bundle builder for agent requests.
- Add project memory file for durable agent context.
- Add prompt-suggestion source that uses local context only.

## P0: First Editor Tools

- `project.create`
- `project.open`
- `project.save`
- `scene.create`
- `scene.createEntity`
- `scene.setTransform`
- `scene.addComponent`
- `scene.setComponentProperty`
- `validate.scene`

## P1: Viewport

- Add renderer bootstrap.
- Add viewport panel.
- Add editor camera.
- Add grid rendering.
- Add primitive mesh rendering.
- Add object picking.
- Add selection outline.
- Add transform gizmo.

## P1: Assets

- Add asset database.
- Add asset ids.
- Add texture import.
- Add material asset.
- Add mesh import.
- Add import settings.
- Add source/license metadata fields.
- Add drag/drop import.
- Add AI asset search and placement.

## P1: Runtime Preview

- Add play/stop mode.
- Add fixed update loop.
- Add input mapping.
- Add script/component execution path.
- Add runtime log capture.
- Add runtime error return to AI agent.

## P2: Self Development

- Add plugin format.
- Add plugin loader.
- Add plugin generator.
- Add plugin compile command.
- Add plugin test command.
- Add plugin install/revert flow.
- Add generated editor panel template.

## P2: Real Agent Backends

- Implement Claude Agent SDK backend.
- Implement Codex CLI non-interactive backend.
- Support session continuity for provider backends.
- Stream provider output into the Agent Activity panel.
- Normalize provider tool proposals into Editor Tool API command batches.
- Add credential configuration outside project files.
- Add provider health checks.
- Add cost/latency/status display.

## P2: Build

- Add asset cooking command.
- Add Windows package command.
- Add build log parser.
- Add smoke test runner.
- Add build artifact index.

## P3: Advanced Graphics

- PBR materials.
- HDR and tone mapping.
- shadow maps.
- image-based lighting.
- clustered lights.
- post-processing.
- volumetrics.
- terrain.
- streaming.

## Open Technical Decisions

- C++ UI layer: Dear ImGui docking vs custom retained UI vs Qt.
- Renderer start: simple DirectX 11 bootstrap vs Vulkan/DX12 abstraction.
- Scripting: C# vs Lua vs custom ECS systems first.
- Scene file format: JSON first vs binary-plus-text manifest.
- Agent transport: JSON-RPC over stdio, named pipes, or local socket.
- LLM providers: Claude Agent SDK, Codex CLI, cloud API adapters, plus local prompt assistant.
- Local model runtime: llama.cpp, ONNX Runtime, or another lightweight local inference path.

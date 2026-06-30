# MVP Roadmap

## Milestone 0: Planning And Repository Setup

Goal: define the first buildable shape.

Deliverables:

- project docs
- repo layout
- build system decision
- native app shell decision
- first command schema

Exit criteria:

- a developer can clone/open the repo and build a blank editor window

## Milestone 1: Native Editor Shell

Goal: launch a native desktop editor.

Deliverables:

- window
- docking layout
- viewport panel
- hierarchy panel
- inspector panel
- console panel
- AI chat dock placeholder

Exit criteria:

- the editor opens and saves a basic layout

## Milestone 2: Scene Model And Save/Load

Goal: make the editor state durable.

Deliverables:

- project file
- scene file
- entity/component model
- transform component
- camera component
- light component
- mesh placeholder component
- scene save/load

Exit criteria:

- create a scene, add entities, save, close, reopen, and see the same hierarchy

## Milestone 3: AI Chat Dock And Tool Gateway

Goal: make AI a real editor operator.

Deliverables:

- AI chat dock
- agent worker process
- agent orchestrator interface
- backend selector
- stub backends for local assistant, Claude, and Codex CLI
- prompt suggestion chips
- local tool protocol
- command batch preview
- undoable command transaction
- first scene commands

Exit criteria:

- user can type "create a starter scene" and the request flows through the orchestrator into a schema-validated command batch
- the local assistant can suggest prompt improvements while staying read-only
- the UI shows which backend handled the request

## Milestone 4: Viewport Rendering

Goal: show actual scene objects.

Deliverables:

- render loop
- camera controls
- basic grid
- primitives
- directional light visualization
- object selection
- transform gizmo placeholder

Exit criteria:

- AI-created scene objects are visible in the viewport

## Milestone 5: Asset Import

Goal: import and place user assets.

Deliverables:

- asset database
- texture import
- mesh import
- material asset
- drag/drop placement
- AI asset placement command

Exit criteria:

- import a model, place it in scene, save/reload, and render it

## Milestone 6: Play Mode

Goal: prove the editor can run a game loop.

Deliverables:

- runtime preview
- input system bootstrap
- simple script/component execution
- play/stop
- runtime logs returned to editor

Exit criteria:

- press play and move a simple object or character

## Milestone 7: AI Development Loop

Goal: let the editor help build itself.

Deliverables:

- real Claude Agent SDK backend
- real Codex CLI backend
- plugin skeleton generator
- compile/test command integration
- diff preview
- install/revert workflow
- first generated editor tool plugin

Exit criteria:

- user asks for a simple editor tool, agent creates it as a plugin, compiles it, and editor loads it

## Aggressive Prototype Timeline

If we keep scope tight:

- Day 1: repo, build, native shell, panels
- Day 2: scene model, save/load, command gateway
- Day 3: AI chat dock, first tool calls, generated starter scene
- Day 4-5: viewport primitives, selection, validation
- Week 2: asset import and simple play mode

This timeline proves the concept. It does not imply production stability or high-end graphics.

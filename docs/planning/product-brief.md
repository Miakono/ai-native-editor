# Product Brief

## Working Name

AI Native Editor

This is a placeholder name for planning. Branding can come later.

## Product Definition

AI Native Editor is a native game development editor with a built-in AI development agent. The editor should allow a developer to create games, build editor tools, repair project issues, and eventually extend the engine from inside the editor itself.

The agent should not feel like a detached chatbot. It should understand the currently open project, selected scene nodes, scripts, assets, errors, build settings, and runtime logs. It should act through editor commands that are visible, undoable, logged, and testable.

## Primary User Experience

The user opens the editor and works through a combination of normal editor controls and AI chat.

Example requests:

- Create a small playable level from these imported assets.
- Add a controllable third-person character.
- Generate a basic enemy that patrols, chases, and attacks.
- Fix the broken script errors.
- Create a main menu and pause menu.
- Add a dialogue editor to this editor.
- Build a Windows test client.
- Explain why this scene is slow.

## Product Pillars

### 1. AI Integrated From The Start

The editor ships with an AI chat dock and a local agent runtime. The agent can inspect editor state and call official editor tools.

The chat surface should support multiple backends instead of being locked to one model. Claude and Codex CLI should be treated as high-capability development backends, while a small local assistant handles prompt suggestions and fast project-aware hints.

### 2. Controlled Self Development

The engine can help develop itself through plugins, modules, code generation, tests, and build validation. Self-development should be controlled through source control, review, and test gates.

### 3. Native Desktop Editor

The main editor should be a native app, not Electron or Tauri. The editor can still use web views for optional docs, accounts, or marketplace surfaces later, but not as the core shell.

### 4. Original Engine Code

The editor and runtime should be original code using permissively licensed dependencies where useful. Unity, Unreal, and store assets may be supported through legal user-owned import/export workflows, not by copying proprietary engine code.

### 5. Asset Interoperability

The engine should import common formats first:

- glTF
- FBX
- OBJ
- PNG, JPG, TGA, EXR, HDR
- WAV, OGG

Later, add Unity and Unreal bridge plugins that export user-owned content through official editor APIs into neutral packages.

## Non Goals For The First Prototype

- Unreal-level rendering quality.
- Native `.uasset` parsing.
- Native Unity project import.
- Multiplayer.
- Console builds.
- Full terrain system.
- Full material graph.
- Asset marketplace.
- Live self-modification of engine source without review.

## First Success Criteria

The first prototype is successful when:

1. The editor launches as a native desktop app.
2. It has a 3D viewport.
3. It has an AI chat dock.
4. The agent can create and edit scene objects through tool calls.
5. The project can save and reload a scene.
6. A simple play mode can run the scene.
7. The agent can read errors and attempt a fix.
8. The AI dock shows the selected backend and can route through stub Claude, Codex CLI, and local assistant adapters.
9. The user gets prompt suggestions while typing without sending every keystroke to a cloud model.

## Longer Term Success Criteria

- The agent can generate full gameplay systems.
- The agent can create editor plugins.
- The agent can run validation and test loops without leaving the editor.
- The engine can import user-owned assets through neutral formats.
- The editor can package a playable Steam build.
- The engine can improve itself through controlled plugin and module development.

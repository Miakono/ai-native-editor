# AI Editor Agent

## Purpose

The AI editor agent is a built-in development assistant that can operate the editor through controlled tools. It should help build games and help build the editor itself.

The agent lives inside the editor UX through the AI chat dock, activity panel, tool previews, and validation results.

The visible chat surface should be provider-neutral. The editor can use Claude, Codex CLI, OpenAI API models, local models, or future providers behind the same agent orchestrator interface.

## Core Capabilities

### Project Awareness

The agent should know:

- current project files
- open scene
- selected entities
- available assets
- scripts and compile errors
- recent console logs
- build settings
- enabled plugins
- validation results

The project-awareness system should be shared by all agent backends. It should feed the high-capability development agents and the fast local prompt assistant.

### Editor Control

The agent should be able to:

- create entities
- edit transforms
- assign meshes/materials
- create lights/cameras
- create scripts
- attach scripts
- import assets
- build prefabs
- run scene validation
- start play mode
- package builds

### Code Development

The agent should be able to:

- create components
- modify scripts
- create editor plugins
- add tests
- run compile checks
- inspect logs
- retry fixes

### Self Development

The agent should be able to create new editor features through isolated plugins first.

Examples:

- terrain brush tool
- dialogue graph editor
- quest editor
- material inspector
- animation retarget helper
- asset validator

### Prompt Assistance

The editor should include a lightweight local assistant that works while the user types.

It should suggest:

- clearer prompts
- missing gameplay details
- likely editor actions
- project-aware completions
- game-design improvements
- reusable prompt templates

The local assistant is read-only. It must not directly edit scenes, source files, or assets.

## Safety Model

The agent should have capability levels.

### Level 0: Read Only

Can inspect project state, logs, assets, and docs.

### Level 1: Scene Editing

Can modify scene objects through undoable editor commands.

### Level 2: Project Editing

Can create scripts, prefabs, import settings, and project assets.

### Level 3: Build And Test

Can run compile, validation, play-mode checks, and package builds.

### Level 4: Plugin Development

Can create or modify editor plugins with user approval.

### Level 5: Engine Source Development

Can modify engine/editor source only through branch, diff, compile, tests, and explicit approval.

## Agent UX Requirements

- Show the plan before large changes.
- Show tool calls as they happen.
- Show which backend is being used.
- Let the user choose or override the backend when useful.
- Show affected files and scene nodes.
- Let the user approve risky actions.
- Make every editor-state change undoable.
- Keep a persistent project chat history.
- Keep a short project memory file for durable context.
- Report proof clearly: created, compiled, tested, failed, blocked.

## First Prototype Agent Loop

The first version should support:

1. User enters a request in chat.
2. Agent reads open project state.
3. Agent proposes a scene command batch.
4. Editor applies the batch.
5. Agent runs validation.
6. Agent summarizes what changed.

Before adding real cloud model calls, route this through a local orchestrator abstraction with three stub backends:

- local prompt assistant stub
- Claude backend stub
- Codex CLI backend stub

This keeps the UI and tool gateway stable while backend integrations mature.

Example command batch:

```json
{
  "transactionName": "Create starter scene",
  "commands": [
    {
      "type": "scene.createEntity",
      "name": "Main Camera",
      "components": ["Transform", "Camera"]
    },
    {
      "type": "scene.createEntity",
      "name": "Sun",
      "components": ["Transform", "DirectionalLight"]
    }
  ]
}
```

## Non Negotiables

- The agent must not silently rewrite engine source.
- The agent must not apply destructive project changes without approval.
- The agent must not bypass undo/redo for editor state.
- The agent must not store API keys in project files.
- The agent must not import or redistribute restricted assets automatically.
- The local prompt assistant must stay read-only.

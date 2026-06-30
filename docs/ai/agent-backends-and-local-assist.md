# Agent Backends And Local Assist

## Purpose

The editor should provide a built-in AI chat surface that can drive real game development from inside the editor. That does not mean one hardcoded model. The product should use an agent backend layer so different AI systems can be used for different jobs.

The user-facing experience is one integrated chat dock, but internally there should be multiple agent roles.

## Backend Roles

### 1. Development Agent

Handles large project changes:

- engine/editor source changes
- gameplay system implementation
- script generation and repair
- build/test loops
- plugin generation
- asset pipeline work
- validation and debugging

Preferred backends:

- Claude Agent SDK backend
- Codex CLI backend

### 2. Editor Control Agent

Handles structured scene and project edits:

- create entities
- update transforms
- assign components
- validate scene
- save project
- run play mode

This agent should always operate through the Editor Tool API. It can use Claude, Codex, OpenAI API models, or another backend, but it must return schema-validated command batches.

### 3. Local Prompt Assistant

Runs locally and responds quickly while the user types.

Responsibilities:

- suggest stronger prompts
- complete common editor intents
- detect vague requests
- suggest missing details
- offer game-design improvements
- surface relevant project context
- suggest next likely actions

This assistant should not make project edits directly. It can only suggest text, show chips/buttons, and help the user write better requests.

### 4. Game Design Advisor

Uses project context plus a durable knowledge base about game design:

- player motivation
- feedback loops
- challenge pacing
- combat feel
- reward cadence
- level readability
- onboarding
- moment-to-moment fun
- retention hooks
- genre expectations
- accessibility

This should start as retrieval and prompt engineering, not a claim that the local model magically understands all games. The editor should build a game-development knowledge base from curated design notes, engine docs, project files, templates, and postmortems.

## Recommended Integration Strategy

### Claude

For the production integration, prefer the Claude Agent SDK over raw CLI subprocess wrapping. The SDK is intended for custom applications and exposes Claude Code-style agent behavior, tools, sessions, permissions, hooks, subagents, and MCP.

Raw `claude` CLI support can still be useful for local developer workflows, debugging, and early prototypes.

### Codex

For early integration, support Codex CLI as a backend worker:

- run in the project root
- use non-interactive execution for discrete tasks
- stream JSON output where available
- keep transcripts/session ids for continuity
- route proposed file/scene changes through the editor review flow

Longer term, also support Codex through MCP/server-style integration where appropriate, so the editor can treat Codex as one agent backend behind the same interface.

### Local Model

Use a small local model only for low-latency assistive behavior:

- prompt autocomplete
- rewrite suggestions
- command intent classification
- simple retrieval over project metadata
- offline help

It should not be trusted for large code edits or final game-design decisions. It is a fast assistant, not the main autonomous developer.

## Agent Orchestrator

The editor should launch an Agent Orchestrator worker process.

The orchestrator owns:

- provider selection
- session management
- credentials lookup
- prompt assembly
- context retrieval
- tool-call normalization
- structured output validation
- command-batch generation
- cost and latency tracking
- transcript storage

The editor owns:

- UI
- project state
- scene state
- undo/redo
- tool command application
- approval prompts
- validation results

## Provider Adapter Interface

Each backend should implement the same internal interface:

```json
{
  "providerId": "claude-agent-sdk",
  "capabilities": [
    "code_edit",
    "shell",
    "project_index",
    "structured_output",
    "long_context",
    "tool_use"
  ],
  "request": {
    "sessionId": "optional-session-id",
    "projectRoot": "C:/Path/To/Project",
    "userPrompt": "Create a starter scene",
    "contextBundle": {},
    "allowedActions": []
  }
}
```

Provider result:

```json
{
  "ok": true,
  "providerId": "claude-agent-sdk",
  "sessionId": "provider-session-id",
  "messages": [],
  "proposedCommands": [],
  "proposedFileChanges": [],
  "diagnostics": [],
  "cost": null,
  "latencyMs": 0
}
```

## Context System

Both cloud agents and local assist need project awareness.

The editor should maintain indexes for:

- project settings
- scene graph
- selected objects
- assets and dependencies
- scripts and diagnostics
- console logs
- build results
- package targets
- input actions
- gameplay systems
- design docs
- TODOs
- recent agent actions

Context should be summarized into tiers:

- hot context: current scene, selection, visible errors
- warm context: recently edited files, recent logs, current task
- cold context: full project index, docs, assets, history

## Suggested Prompt UX

While the user types, show lightweight suggestions:

- intent chips: "Create scene", "Fix error", "Add gameplay", "Improve fun"
- prompt improvement: "Add camera, player controller, win condition, and validation"
- missing detail prompts: "Which genre?", "Single-player or co-op?", "Target camera?"
- project-aware suggestions: "Use the existing PlayerController component"
- design suggestions: "Add a clear objective and short feedback loop"

The user should be able to accept a suggestion with one click or ignore it.

## Permissions

Default permissions:

- Local prompt assistant: read-only, no file edits.
- Editor control agent: scene/project commands only.
- Development agent: file edits and shell commands only after approval mode allows it.
- Engine source development: explicit approval required.

## Current Prototype Implementation

The editor now has a provider-neutral `AgentOrchestrator` behind the AI Chat dock. The visible backend selector includes:

- Local Tool Gateway
- Codex CLI
- OpenAI API
- Claude
- Local Prompt Assistant

Local Tool Gateway remains the deterministic offline scene-command backend and is still the only path that mutates live scene state automatically. It is intentionally narrow: explicit supported asks such as adding a cube, light, camera, starter scene, running-simulator template, save, project-state inspection, or validation can apply locally. Broad gameplay/editor feature requests should route to Codex or another development backend, or return a diagnostic command batch, instead of creating unrelated starter-scene placeholders.

Codex CLI is optional: the editor detects `codex` through `AI_NATIVE_CODEX_PATH`, cmd/PowerShell shims, and direct PATH candidates; reports missing, authentication, WindowsApps alias, launch, timeout, and ready states; and fails closed without applying local scene commands when unavailable. Provider health refreshes and provider execution are kept off the UI thread after startup. OpenAI API is a separate backend that reads `OPENAI_API_KEY` from the process environment only and sends compact project context through the Responses API without storing keys in project files. Claude is a placeholder provider with health/status until the SDK or CLI adapter is added. Local Prompt Assistant is read-only and surfaces project-aware prompt chips while the user types.

The Agent workflow now surfaces the current run card directly in the main panel: run id, backend, elapsed time, current stage, recent activity, command/output summaries, changed entities/files, result text, and failure reason. Proposed command batches are also shown in the main workflow with diagnostics, changed entities/files, and Approve/Reject controls; Advanced remains available for full raw logs.

Agent visual context is editor-owned. The Agent panel can capture Scene and Game view framebuffer PNGs, auto-capture before worker-backed runs, and record lightweight frame sequences. Generated prompts and `docs/agent-memory/run_log.jsonl` include the latest visual artifact paths so backend agents can inspect the editor/game state from current screenshots.

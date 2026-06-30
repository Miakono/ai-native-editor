# Decision 0002: AI Chat Is A Core Feature

## Status

Accepted for initial planning.

## Context

The editor should be self-developing over time. For that to work, the AI agent must be present from the start instead of being added after the editor is mostly built.

The user wants to only need the editor open for the agent to work inside the editor.

## Decision

The AI chat dock, agent worker, and editor tool command gateway are part of the first editor milestone.

The first AI feature is not general code completion. The first AI feature is editor operation through typed tools.

## Consequences

Benefits:

- the agent can help build the editor earlier
- project operations become automatable
- tests and validation can reuse the same command API
- the product identity is clear from day one

Costs:

- the editor needs a command protocol early
- undo/redo and validation need to be designed earlier
- safety permissions need to exist before powerful actions are added

## First Implementation Target

The first agent loop:

1. User types into the built-in chat dock.
2. Agent receives project and scene context.
3. Agent returns a command batch.
4. Editor previews and applies command batch.
5. Editor validates the scene.
6. Agent reports changed objects and validation results.

This proves the editor can be operated by AI without uncontrolled file mutation.

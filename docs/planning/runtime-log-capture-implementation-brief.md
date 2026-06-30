# Runtime Log Capture Implementation Brief

You are working in the AI Native Editor repository. Read the runtime and smoke-test owner paths first, preserve the existing C++/ImGui architecture, and implement runtime log capture as a real editor feature rather than a static note.

## User Goal

Look at editor features that are not implemented yet, choose one that can be properly implemented and smoke tested, create the implementation prompt first, then implement it.

## Chosen Feature

Implement the roadmap item "runtime log capture" from the Runtime Preview milestone.

## Inferred Intent

The editor already has Play Mode, fixed runtime systems, input state, physics, game-object triggers, and console logs. What is missing is a first-class captured runtime event stream that the editor UI and automated tests can inspect separately from generic editor logs.

## Owner Paths

- `src/EditorState.h` / `src/EditorState.cpp`: persistent scene/runtime state, play-mode transitions, runtime systems, validation, logs, and test-facing accessors.
- `src/EditorApp.h` / `src/EditorApp.cpp`: Game View UI, smoke-test flags, CLI smoke orchestration, and runtime overlays.
- `src/main.cpp`: CLI option parsing for a focused runtime-log smoke flag.
- `tests/runtime_tests.cpp`: non-UI runtime regression coverage.
- `scripts/run-smoke-tests.ps1`: broad smoke matrix entry if the focused smoke is stable and cheap.

## Implementation Requirements

- Add a bounded runtime log/event stream to `EditorState`, distinct from generic editor console logs.
- Capture play lifecycle, runtime input capture, physics trigger/collision summaries, collectible pickups, hazard failure, goal completion, and stop/restore events where applicable.
- Keep the event stream bounded so long play sessions do not grow memory indefinitely.
- Expose the runtime log through a const accessor for UI, smoke tests, and future agent context.
- Show the latest runtime events in the Game View without overlapping the existing overlays.
- Add a focused `--smoke-test-runtime-logs` path that opens the FirstGame fixture, runs a deterministic play route, verifies captured events, stops play mode, and verifies edit-state restoration.
- Add or update unit-style runtime test coverage for the new event stream.
- Preserve existing play-mode, first-game, physics, provider, profile, and task-composer smoke behavior.

## Constraints

- Keep changes narrow and additive.
- Do not replace the existing console log or agent activity systems.
- Do not introduce a new dependency or large logging framework.
- Do not persist transient runtime logs into project or scene files.
- Treat build success, focused runtime-log smoke success, and full smoke-matrix success as separate proof boundaries.

## Acceptance Criteria

- Runtime events are captured in `EditorState` during Play Mode.
- The Game View shows recent runtime events while playing.
- The focused smoke fails if collectible or goal events are not captured.
- Runtime logs clear on a fresh play session and do not survive as stale events after returning to edit mode.
- `aine_runtime_tests` covers the new runtime log behavior.
- `cmake --build --preset windows-ninja-debug`, `aine_runtime_tests`, and the focused runtime-log smoke pass.

## Required Verification

1. `cmake --build --preset windows-ninja-debug`
2. `.\build\windows-ninja-debug\aine_runtime_tests.exe`
3. `.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-runtime-logs --smoke-test-frames=120`
4. Run the full smoke matrix or explain clearly if only the focused proof was run.

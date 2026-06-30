# Project Folder And Save Location Implementation Brief

You are working in the AI Native Editor repository. Read the project/state and UI code before editing, preserve the existing native C++/ImGui architecture, and implement durable project-folder behavior instead of a cosmetic path field.

## User Goal

The editor needs a real project folder workflow. Users must be able to create or open a project by folder, see the actual project file and active scene paths, choose where game/runtime save data is written, and have those paths persist in project metadata.

## Inferred Intent

Repair the project persistence model so the engine treats a project as a folder with standard subdirectories, not only as an active scene JSON file. Keep existing sample projects opening, but make new projects use the documented `Assets/Scenes` layout.

## Implementation Requirements

- Put persistent project path ownership in `EditorState`, not just `EditorApp`.
- Default new projects to `Assets/Scenes/Prototype.scene.json`.
- Store project directories in the `.aineproject.json` file, including asset root, scene root, and game save data root.
- Preserve backward compatibility for existing projects whose scenes live under root-level `Scenes`.
- Create required project directories when a project is created, opened, saved, or when the save data location changes.
- Expose UI controls for Project Folder, Project File, Scene File, and Game Save Folder.
- Add folder-picker buttons on Windows for project folder and game save folder selection, with text input fallback.
- Persist the selected game save folder through project save/reload.
- Extend ToolGateway so AI/provider command batches can create/open projects and set the game save folder through typed commands.
- Update docs and smoke coverage so regressions are caught.

## Constraints

- Keep the change narrow and additive.
- Do not break existing scene, physics, first-game, provider, or task-composer smoke tests.
- Do not store credentials or machine-specific API keys in project files.
- Treat build success, smoke success, and manual UI path picker availability as separate proof boundaries.

## Acceptance Criteria

- A new project created from the editor or CLI project-root override writes `AI Native Project.aineproject.json` under the selected folder.
- The active scene for a new project is saved under `Assets/Scenes/Prototype.scene.json`.
- The project file records a save data location, and the folder exists on disk.
- Changing the save data folder from UI or ToolGateway persists after reopening the project.
- Opening legacy project files with root-level `Scenes/*.scene.json` still works.
- The project/source panel clearly distinguishes project folder, project file, scene file, and game save folder.

## Required Verification

1. `cmake --preset windows-ninja-debug`
2. `cmake --build --preset windows-ninja-debug`
3. `.\build\windows-ninja-debug\aine_runtime_tests.exe`
4. `.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-project-folder --project-root=build\project-folder-smoke`
5. `powershell -ExecutionPolicy Bypass -File scripts/run-smoke-tests.ps1`

# Focused Project Tree Implementation Brief

You are working in AI Native Editor. The Project panel should feel like a game-engine asset/work tree, not a path settings form.

## User Goal

Simplify the Project area so users primarily see and work with the project tree. Project folder, project file, scene file, and game save folder management should live under File/project actions rather than occupying the tree panel. Add typical engine-style right-click actions on tree folders/files, including showing items in Explorer and deleting with a confirmation warning.

## Inferred Intent

Make the editor feel closer to a production game engine workflow: asset browser first, project paths managed from menus, and filesystem actions available from context menus where the user expects them.

## Implementation Requirements

- Keep the Project panel focused on the work tree and a small action toolbar.
- Move detailed project path controls to File menu/project-management menu items.
- Add filesystem paths to project tree nodes so UI actions operate on actual folders/files.
- Add right-click context menus for tree folders and files.
- Include at least: Show In Explorer, Copy Path, Refresh, and Delete.
- Delete must require a modal confirmation and must block deleting the active project root.
- Deleting folders/files must update the project tree afterward and log enough detail to diagnose failures.
- Keep existing project save/open/new behavior intact.
- Preserve existing smoke-test semantics and add coverage for the new project tree workflow.

## Constraints

- Keep changes narrow and additive.
- Do not add a full asset database yet.
- Avoid destructive actions without an explicit confirmation modal.
- Do not remove project metadata controls entirely; move them to less intrusive File menu actions.

## Acceptance Criteria

- The Project panel no longer shows the full Project Folder, Project File, Scene File, and Game Save Folder form by default.
- The Project panel shows a concise game-project tree with a small toolbar.
- Right-clicking a real folder/file exposes common filesystem actions.
- Show In Explorer launches Explorer on Windows.
- Delete requires confirmation, refuses the project root, removes the selected item, refreshes the tree, and logs the result.
- Project save-folder management remains available from File.

## Required Verification

1. `cmake --preset windows-ninja-debug`
2. `cmake --build --preset windows-ninja-debug`
3. `.\build\windows-ninja-debug\ai_native_editor.exe --smoke-test --smoke-test-project-tree-actions --project-root=build\project-tree-actions-smoke`
4. `powershell -ExecutionPolicy Bypass -File scripts/run-smoke-tests.ps1`

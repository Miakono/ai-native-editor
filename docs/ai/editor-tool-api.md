# Editor Tool API

## Purpose

The Editor Tool API is the command surface used by the AI agent. It should also be useful for tests, automation, editor plugins, and batch operations.

The agent should call this API instead of directly manipulating live editor state.

## Design Rules

- Commands are typed and schema-validated.
- Commands return structured results.
- Commands can be grouped into transactions.
- Commands affecting editor state support undo/redo.
- Dangerous commands require approval.
- Long-running commands stream progress.
- All commands are logged.

## Command Categories

### Project

- `project.create`
- `project.open`
- `project.setSaveLocation`
- `project.save`
- `project.getState`
- `project.getDiagnostics`
- `project.setSetting`

### Scene

- `scene.create`
- `scene.open`
- `scene.save`
- `scene.createEntity`
- `scene.deleteEntity`
- `scene.renameEntity`
- `scene.setParent`
- `scene.setTransform`
- `scene.addComponent`
- `scene.removeComponent`
- `scene.setComponentProperty`
- `scene.createPrefab`

### Assets

- `asset.import`
- `asset.reimport`
- `asset.find`
- `asset.getMetadata`
- `asset.setImportSettings`
- `asset.assignMaterial`
- `asset.createMaterial`

`asset.import` currently accepts neutral source files such as glTF, FBX, OBJ, images, audio, and generated materials. It records source labels, hashes, license metadata, dependencies, imported resource JSON, metadata JSON, and thumbnail JSON under the project asset database and `Library/ImportedAssets`.

### Scripts

- `script.create`
- `script.modify`
- `script.format`
- `script.compile`
- `script.getDiagnostics`
- `script.attachToEntity`

`script.compile` discovers runtime `.cs` files under `Assets/Scripts`, writes generated compiler inputs under `Library/Scripts`, builds `AINative.GameScripts.dll`, and returns compiler diagnostics. `validate.scripts` uses the same compiler result and also checks attached script components against declarations in the referenced file.

### Runtime

- `runtime.play`
- `runtime.stop`
- `runtime.pause`
- `runtime.captureScreenshot`
- `runtime.startRecording`
- `runtime.stopRecording`
- `runtime.getLogs`

`runtime.captureScreenshot`, `runtime.startRecording`, and `runtime.stopRecording` are editor-owned visual commands. The ToolGateway validates and acknowledges them in command batches, while `EditorApp` performs the Scene/Game framebuffer capture on the UI thread. Captures are written as PNG artifacts under `docs/agent-memory/visuals` and referenced from generated agent prompts and `docs/agent-memory/run_log.jsonl`.

### Validation

- `validate.scene`
- `validate.project`
- `validate.assets`
- `validate.scripts`
- `validate.buildTarget`

### Build

- `build.cookAssets`
- `build.package`
- `build.runSmokeTest`
- `build.openOutputFolder`

### Plugin Development

- `plugin.create`
- `plugin.install`
- `plugin.enable`
- `plugin.disable`
- `plugin.compile`
- `plugin.test`
- `plugin.remove`

`plugin.create` writes a generated plugin manifest and source scaffold under `Plugins/GeneratedTools`. `plugin.install` validates that manifest and source list, copies the plugin into `Library/Plugins/<name>`, writes an install manifest, and persists installed state. `plugin.enable` requires an installed plugin record.

## Example Transaction

```json
{
  "transactionName": "Create playable prototype room",
  "approval": "required",
  "commands": [
    {
      "type": "scene.createEntity",
      "name": "Player",
      "components": ["Transform", "CharacterController", "Script"]
    },
    {
      "type": "script.create",
      "path": "Assets/Scripts/Gameplay/PlayerController.cs",
      "content": "using AINative.Runtime;\n\npublic class PlayerController : ScriptBehaviour\n{\n    public override void OnUpdate(float deltaSeconds) {}\n}\n"
    },
    {
      "type": "script.attachToEntity",
      "targetName": "Player",
      "path": "Assets/Scripts/Gameplay/PlayerController.cs",
      "componentType": "PlayerController"
    },
    {
      "type": "script.compile"
    },
    {
      "type": "validate.scene"
    }
  ]
}
```

## Result Shape

```json
{
  "ok": true,
  "transactionId": "txn_0001",
  "changedFiles": [
    "Assets/Scenes/Prototype.scene.json",
    "Assets/Scripts/Gameplay/PlayerController.ai.cs"
  ],
  "changedEntities": [
    "Player"
  ],
  "diagnostics": [],
  "undoAvailable": true
}
```

## First Commands To Implement

The first prototype only needs:

- `project.create`
- `project.open`
- `project.setSaveLocation`
- `build.packageWindows`
- `scene.create`
- `scene.createEntity`
- `scene.setTransform`
- `scene.addComponent`
- `scene.setComponentProperty`
- `project.save`
- `validate.scene`

Once those work, the AI can create a visible scene and prove the command loop.

## Project Folder Commands

Project commands use folder paths rather than direct scene mutations:

```json
{"type": "project.create", "path": "C:/Games/MyAINativeProject"}
```

```json
{"type": "project.open", "path": "C:/Games/MyAINativeProject"}
```

```json
{"type": "project.setSaveLocation", "path": "SavedGames/ProfileA"}
```

New projects use `Assets/Scenes` for scene JSON and `Saved` for game save data by default. `project.setSaveLocation` accepts either a project-relative folder or an absolute folder and persists the result in the project manifest.

## Build Commands

```json
{"type": "build.packageWindows"}
```

`build.packageWindows` saves the current project, uses the project manifest's `buildSettings`, copies the current `ai_native_player.exe` runtime as the game executable, stages project data under `Data`, writes `build-manifest.ainebuild.json`, records executable hash/signing metadata, writes installer/update manifests when enabled, and preserves multiplayer metadata in the package. Current networking support is local runtime component simulation plus project/package metadata; internet transport remains future work.

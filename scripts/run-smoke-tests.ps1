param(
    [switch]$SkipBuild,
    [string]$BuildDir = "build\windows-ninja-debug"
)

$ErrorActionPreference = "Stop"

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptRoot
Set-Location $RepoRoot

if (-not $SkipBuild) {
    cmake --preset windows-ninja-debug
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }

    cmake --build --preset windows-ninja-debug
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with exit code $LASTEXITCODE."
    }
}

$EditorExe = Join-Path $RepoRoot (Join-Path $BuildDir "ai_native_editor.exe")
if (-not (Test-Path $EditorExe)) {
    throw "Editor executable was not found: $EditorExe"
}

Write-Host "==> ctest runtime and physics units"
ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) {
    throw "CTest failed with exit code $LASTEXITCODE."
}

$FixtureRoot = Join-Path $RepoRoot "build\smoke-fixtures"
$CmdShimRoot = Join-Path $FixtureRoot "cmd-shim"
New-Item -ItemType Directory -Force -Path $CmdShimRoot | Out-Null
Set-Content -Encoding ASCII -Path (Join-Path $FixtureRoot "fake-codex.exe") -Value "not a real executable"
Set-Content -Encoding ASCII -Path (Join-Path $CmdShimRoot "codex.cmd") -Value @"
@echo off
if "%~1"=="login" if "%~2"=="status" (
  echo Logged in using ChatGPT
  exit /b 0
)
if "%~1"=="--version" (
  echo codex 0.0.0-smoke
  exit /b 0
)
echo {"result_type":"chat_response","message":"Codex smoke response."}
"@

function Invoke-Smoke {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [string]$PathOverride,
        [string]$CodexOverride,
        [string]$LocalAppDataOverride
    )

    Write-Host "==> $Name"
    $oldPath = $env:PATH
    $hadCodexOverride = Test-Path Env:\AI_NATIVE_CODEX_PATH
    $oldCodexOverride = $env:AI_NATIVE_CODEX_PATH
    $hadLocalAppDataOverride = Test-Path Env:\LOCALAPPDATA
    $oldLocalAppDataOverride = $env:LOCALAPPDATA
    try {
        if ($PathOverride) {
            $env:PATH = $PathOverride
        }
        if ($CodexOverride) {
            $env:AI_NATIVE_CODEX_PATH = $CodexOverride
        } else {
            Remove-Item Env:\AI_NATIVE_CODEX_PATH -ErrorAction SilentlyContinue
        }
        if ($LocalAppDataOverride) {
            New-Item -ItemType Directory -Force -Path $LocalAppDataOverride | Out-Null
            $env:LOCALAPPDATA = $LocalAppDataOverride
        }

        & $EditorExe @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$Name failed with exit code $LASTEXITCODE."
        }
    } finally {
        $env:PATH = $oldPath
        if ($hadCodexOverride) {
            $env:AI_NATIVE_CODEX_PATH = $oldCodexOverride
        } else {
            Remove-Item Env:\AI_NATIVE_CODEX_PATH -ErrorAction SilentlyContinue
        }
        if ($hadLocalAppDataOverride) {
            $env:LOCALAPPDATA = $oldLocalAppDataOverride
        } else {
            Remove-Item Env:\LOCALAPPDATA -ErrorAction SilentlyContinue
        }
    }
}

$SystemPath = "C:\Windows\System32;C:\Windows"
$FakeCodex = Join-Path $FixtureRoot "fake-codex.exe"
$CmdShimPath = "$CmdShimRoot;$SystemPath"
$NoCodexLocalAppData = Join-Path $FixtureRoot "no-codex-localappdata"

Invoke-Smoke "launch viewport" @(
    "--smoke-test",
    "--project-root=build\smoke-launch-project"
)

Invoke-Smoke "scene integration" @(
    "--smoke-test",
    "--smoke-test-scene-integration",
    "--project-root=build\scene-integration-smoke"
)

Invoke-Smoke "play mode lifecycle" @(
    "--smoke-test",
    "--smoke-test-play-mode",
    "--project-root=build\play-mode-smoke"
)

Invoke-Smoke "physics runtime" @(
    "--smoke-test",
    "--smoke-test-physics",
    "--project-root=build\physics-smoke"
)

Invoke-Smoke "terrain editor runtime" @(
    "--smoke-test",
    "--smoke-test-terrain",
    "--smoke-test-frames=120"
)

Invoke-Smoke "unified volumetric terrain proof" @(
    "--smoke-test",
    "--smoke-test-unified-terrain-proof",
    "--smoke-test-frames=120"
)

Invoke-Smoke "unified volumetric terrain performance" @(
    "--smoke-test",
    "--smoke-test-terrain-performance",
    "--smoke-test-frames=120"
)

Invoke-Smoke "fog rendering" @(
    "--smoke-test",
    "--smoke-test-fog",
    "--smoke-test-frames=90"
)

Invoke-Smoke "sprite 2d workflow" @(
    "--smoke-test",
    "--smoke-test-sprites",
    "--project-root=build\sprite-smoke-entry",
    "--smoke-test-frames=90"
)

Invoke-Smoke "first game fixture" @(
    "--smoke-test",
    "--smoke-test-first-game",
    "--smoke-test-frames=120"
)

Invoke-Smoke "performance profiler" @(
    "--smoke-test",
    "--smoke-test-profiler",
    "--open-project=projects\FirstGame\AI Native Project.aineproject.json",
    "--smoke-test-frames=120"
)

Invoke-Smoke "runtime log capture" @(
    "--smoke-test",
    "--smoke-test-runtime-logs",
    "--smoke-test-frames=120"
)

Invoke-Smoke "runtime scripting host" @(
    "--smoke-test",
    "--smoke-test-runtime-scripting",
    "--project-root=build\runtime-scripting-smoke",
    "--smoke-test-frames=120"
)

Invoke-Smoke "runtime ui death screen" @(
    "--smoke-test",
    "--smoke-test-runtime-ui",
    "--project-root=build\runtime-ui-smoke",
    "--smoke-test-frames=120"
)

Invoke-Smoke "local gateway prompt save reload" @(
    "--smoke-test",
    "--project-root=build\command-smoke-project",
    "--smoke-test-prompt=create a starter scene",
    "--smoke-test-reload"
)

Invoke-Smoke "local gateway physics gravity prompt" @(
    "--smoke-test",
    "--project-root=build\physics-gravity-prompt-smoke",
    "--smoke-test-prompt=create a physics cube that falls at 9.81",
    "--smoke-test-reload"
)

Invoke-Smoke "project folder and save location" @(
    "--smoke-test",
    "--smoke-test-project-folder",
    "--project-root=build\project-folder-smoke"
)

Invoke-Smoke "project tree actions" @(
    "--smoke-test",
    "--smoke-test-project-tree-actions",
    "--project-root=build\project-tree-actions-smoke"
)

Invoke-Smoke "console filters copy collapse" @(
    "--smoke-test",
    "--smoke-test-console",
    "--project-root=build\console-smoke"
)

Invoke-Smoke "expanded tool api asset script plugin undo" @(
    "--smoke-test",
    "--smoke-test-expanded-tools",
    "--project-root=build\expanded-tool-api-smoke"
)

Invoke-Smoke "provider file change apply revert" @(
    "--smoke-test",
    "--smoke-test-provider-file-change",
    "--project-root=build\provider-file-change-smoke"
)

Invoke-Smoke "asset pipeline import artifacts renderer" @(
    "--smoke-test",
    "--smoke-test-asset-pipeline",
    "--project-root=build\asset-pipeline-smoke"
)

Invoke-Smoke "project builder windows package" @(
    "--smoke-test",
    "--smoke-test-project-builder",
    "--project-root=build\project-builder-smoke"
)

Invoke-Smoke "render profile project settings" @(
    "--smoke-test",
    "--smoke-test-render-profiles",
    "--project-root=build\render-profile-entry-smoke"
)

Invoke-Smoke "provider invalid json rejected" @(
    "--smoke-test",
    "--project-root=build\provider-invalid-json-smoke",
    "--smoke-test-provider-normalizer=invalid-json"
)

Invoke-Smoke "provider approval gate" @(
    "--smoke-test",
    "--project-root=build\provider-valid-approve-smoke",
    "--smoke-test-provider-normalizer=valid-command-batch-approve"
)

Invoke-Smoke "appearance settings persist" @(
    "--smoke-test",
    "--project-root=build\appearance-smoke-project",
    "--editor-profile-path=build\appearance-smoke\editor-profile.json",
    "--smoke-test-appearance"
)

Invoke-Smoke "editor profile persist" @(
    "--smoke-test",
    "--project-root=build\editor-profile-smoke-project",
    "--editor-profile-path=build\editor-profile-smoke\editor-profile.json",
    "--smoke-test-editor-profile"
)

Invoke-Smoke "editor layout presets persist" @(
    "--smoke-test",
    "--project-root=build\editor-layouts-smoke-project",
    "--editor-profile-path=build\editor-layouts-smoke\editor-profile.json",
    "--smoke-test-editor-layouts"
)

Invoke-Smoke "panel tab duplicate close persist" @(
    "--smoke-test",
    "--project-root=build\panel-tabs-smoke-project",
    "--editor-profile-path=build\panel-tabs-smoke\editor-profile.json",
    "--smoke-test-panel-tabs"
)

Invoke-Smoke "codex not found" @(
    "--smoke-test",
    "--project-root=build\provider-not-found-smoke",
    "--smoke-test-provider-health=codex-not-found"
) -PathOverride $SystemPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "codex launch failed" @(
    "--smoke-test",
    "--project-root=build\provider-launch-failed-smoke",
    "--smoke-test-provider-health=codex-launch-failed"
) -PathOverride $SystemPath -CodexOverride $FakeCodex -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "codex cmd shim ready" @(
    "--smoke-test",
    "--project-root=build\provider-health-smoke",
    "--smoke-test-provider-health=codex-cmd-shim-ready"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "codex scene request routes local" @(
    "--smoke-test",
    "--project-root=build\codex-scene-routes-local-smoke",
    "--smoke-test-provider-health=codex-scene-request-routes-local"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "local broad gameplay no mutation" @(
    "--smoke-test",
    "--project-root=build\local-broad-gameplay-no-mutation-smoke",
    "--smoke-test-provider-health=local-broad-gameplay-no-mutation"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "codex diagnostic request stays chat" @(
    "--smoke-test",
    "--project-root=build\codex-diagnostic-chat-smoke",
    "--smoke-test-provider-health=codex-diagnostic-request-stays-chat"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "codex command batch auto applies" @(
    "--smoke-test",
    "--project-root=build\codex-direct-apply-smoke",
    "--smoke-test-provider-health=codex-command-batch-auto-applies"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "codex rigidbody defaults auto apply" @(
    "--smoke-test",
    "--project-root=build\codex-rigidbody-defaults-smoke",
    "--smoke-test-provider-health=codex-rigidbody-defaults-auto-apply"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "codex command batch review when auto apply disabled" @(
    "--smoke-test",
    "--project-root=build\codex-review-mode-smoke",
    "--smoke-test-provider-health=codex-command-batch-review-when-auto-apply-disabled"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "agent task composer codex routing" @(
    "--smoke-test",
    "--smoke-test-task-composer",
    "--smoke-test-frames=160",
    "--project-root=build\task-composer-smoke",
    "--smoke-test-task-composer-request=add an agent task composer smoke verification path"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "agent visual capture" @(
    "--smoke-test",
    "--smoke-test-agent-visuals",
    "--smoke-test-frames=45",
    "--project-root=build\agent-visuals-smoke"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "codex run chat response" @(
    "--smoke-test",
    "--project-root=build\provider-codex-run-smoke",
    "--smoke-test-provider-health=codex-run-chat-response"
) -PathOverride $CmdShimPath -LocalAppDataOverride $NoCodexLocalAppData

Invoke-Smoke "codex provider failure no scene fallback" @(
    "--smoke-test",
    "--project-root=build\provider-fallback-smoke",
    "--smoke-test-provider-health=codex-gateway-fallback"
) -PathOverride $SystemPath -CodexOverride $FakeCodex -LocalAppDataOverride $NoCodexLocalAppData

$codexVersionOk = $false
$previousErrorActionPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = "Continue"
    cmd /d /s /c "codex --version" 1>$null 2>$null
    if ($LASTEXITCODE -eq 0) {
        $codexVersionOk = $true
    }
} finally {
    $ErrorActionPreference = $previousErrorActionPreference
}

$codexWhere = where.exe codex 2>$null
if ($codexWhere -match "\\WindowsApps\\" -and -not $codexVersionOk) {
    Invoke-Smoke "codex windowsapps skipped" @(
        "--smoke-test",
        "--project-root=build\provider-windowsapps-skipped-smoke",
        "--smoke-test-provider-health=codex-windowsapps-skipped"
    ) -LocalAppDataOverride $NoCodexLocalAppData
} else {
    Write-Host "==> codex windowsapps skipped"
    if ($codexVersionOk) {
        Write-Host "Skipped: codex --version succeeds, so the WindowsApps unavailable-path scenario does not apply."
    } else {
        Write-Host "Skipped: where.exe codex did not report a WindowsApps path on this machine."
    }
}

if (-not $codexVersionOk) {
    Invoke-Smoke "real unavailable codex status" @(
        "--smoke-test",
        "--project-root=build\provider-health-real-smoke",
        "--smoke-test-provider-health=codex-all-probes-fail"
    ) -LocalAppDataOverride $NoCodexLocalAppData
} else {
    Write-Host "==> real unavailable codex status"
    Write-Host "Skipped: real codex --version succeeds on this machine."
}

Write-Host "All smoke tests passed."

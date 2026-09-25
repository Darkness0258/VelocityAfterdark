[CmdletBinding()]
param([switch]$Editor, [string]$EngineRoot = $env:UE_ROOT, [string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $projectRoot $(if ($Editor) { 'Artifacts\GarageSmokeEditor' } else { 'Artifacts\GarageSmoke' })
$profileDirectory = Join-Path $outputDirectory ('Profiles\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $profileDirectory -Force | Out-Null
$profilePath = Join-Path $profileDirectory 'profile.json'
if ($Editor) {
    if (-not $EngineRoot) { $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8' }
    $runtime = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $prefixArguments = @(('"{0}"' -f (Join-Path $projectRoot 'VelocityAfterdark.uproject')), '-game')
    $workingDirectory = $projectRoot
    $savedCandidates = @((Join-Path $projectRoot 'Saved'))
} else {
    if (-not $PackageDirectory) { $PackageDirectory = Join-Path $projectRoot 'Artifacts\Package\Windows' }
    $PackageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).Path
    $runtime = Join-Path $PackageDirectory 'VelocityAfterdark\Binaries\Win64\VelocityAfterdark.exe'
    $prefixArguments = @()
    $workingDirectory = $PackageDirectory
    $savedCandidates = @((Join-Path $PackageDirectory 'VelocityAfterdark\Saved'), (Join-Path $env:LOCALAPPDATA 'VelocityAfterdark\Saved'))
}
if (-not (Test-Path -LiteralPath $runtime)) { throw "Runtime missing: $runtime. Build the requested target first." }
$environmentRuns = @()
$reports = @{}
foreach ($mode in @('Save', 'Reload')) {
    $modeDirectory = Join-Path $outputDirectory $mode
    New-Item -ItemType Directory -Path $modeDirectory -Force | Out-Null
    $logFile = Join-Path $modeDirectory 'runtime.log'
    $runtimeArguments = $prefixArguments + @('-windowed', '-ResX=1280', '-ResY=720', '-d3d11', '-RenderOffscreen',
        '-unattended', '-nosplash', '-AfterdarkGarageSmokeExit', '-csvCompression=0',
        $(if ($mode -eq 'Save') { '-AfterdarkGarageSmoke' } else { '-AfterdarkGarageVerify' }),
        ('"-AfterdarkProfile={0}"' -f $profilePath), ('"-abslog={0}"' -f $logFile),
        '"-ExecCmds=t.MaxFPS 0,r.VSync 0,r.ScreenPercentage 85,DisableAllScreenMessages"')
    $started = Get-Date
    $runtimeProcess = Start-Process -FilePath $runtime -ArgumentList $runtimeArguments -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru
    $deadline = $started.AddMinutes(3)
    $peakWorkingSetBytes = 0L
    while (-not $runtimeProcess.WaitForExit(1000)) {
        $runtimeProcess.Refresh()
        if (-not $runtimeProcess.HasExited) { $peakWorkingSetBytes = [Math]::Max($peakWorkingSetBytes, $runtimeProcess.PeakWorkingSet64) }
        if ((Get-Date) -gt $deadline) {
            $runtimeProcess.Kill()
            throw "Garage $mode smoke exceeded three minutes. Inspect $logFile"
        }
    }
    $environmentRuns += [ordered]@{ mode = $mode; startedUtc = $started.ToUniversalTime().ToString('o');
        processPeakWorkingSetMB = [Math]::Round($peakWorkingSetBytes / 1MB, 2); exitCode = $runtimeProcess.ExitCode }
    $captureRoot = $null
    foreach ($candidate in $savedCandidates) {
        $reportPath = Join-Path $candidate "GarageSmoke\$mode\report.json"
        if ((Test-Path -LiteralPath $reportPath) -and (Get-Item -LiteralPath $reportPath).LastWriteTime -ge $started) {
            $captureRoot = $candidate
            Copy-Item -LiteralPath $reportPath -Destination $modeDirectory -Force
            break
        }
    }
    if ($runtimeProcess.ExitCode -ne 0) { throw "Garage $mode exited with $($runtimeProcess.ExitCode). Inspect $logFile" }
    foreach ($marker in @('Dockside ready:', 'AFTERDARK_GARAGE_SMOKE_START', 'AFTERDARK_GARAGE_SMOKE_OK')) {
        if (-not (Select-String -LiteralPath $logFile -SimpleMatch $marker -Quiet)) { throw "Garage runtime marker missing: $marker" }
    }
    if (Select-String -LiteralPath $logFile -Pattern 'Fatal error:|LogADVehiclePhysics: Error:|Dockside load failed|AFTERDARK_GARAGE_SMOKE_FAILED' -Quiet) {
        throw "Garage smoke reported a runtime failure. Inspect $logFile"
    }
    if (-not $captureRoot) { throw "Fresh $mode report missing; earlier reports are not evidence." }
    $report = Get-Content -LiteralPath (Join-Path $modeDirectory 'report.json') -Raw | ConvertFrom-Json
    if (-not $report.success -or -not $report.returnedToDriving -or @($report.ownedUpgrades).Count -ne 1 -or
        @($report.equippedUpgrades).Count -ne 1 -or $report.torque4000Nm -le 0 -or $report.finalDrive -le 0) {
        throw "Garage $mode report failed ownership/physical/return checks: $($report.reason)"
    }
    $reports[$mode] = $report
    $images = if ($mode -eq 'Save') { @('AfterdarkGarageStock.png', 'AfterdarkGarageCustom.png') } else { @('AfterdarkGarageReload.png') }
    foreach ($name in $images) {
        $capture = Join-Path $captureRoot "GarageSmoke\$mode\$name"
        if (-not (Test-Path -LiteralPath $capture) -or (Get-Item -LiteralPath $capture).LastWriteTime -lt $started) {
            throw "Fresh garage capture missing: $name"
        }
        Copy-Item -LiteralPath $capture -Destination $modeDirectory -Force
    }
    if ($mode -eq 'Save') {
        if ($report.captureWallSeconds -lt 20 -or $report.capturedFrames -lt 20) { throw 'Garage profiling window is incomplete.' }
        if (-not (Test-Path -LiteralPath $profilePath)) { throw 'Garage commit did not create the isolated save file.' }
        $captureCsv = Get-ChildItem -LiteralPath (Join-Path $captureRoot 'Profiling\CSV') -Filter '*.csv' |
            Where-Object LastWriteTime -ge $started | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $captureCsv) { throw 'Garage smoke did not produce a fresh profiling CSV.' }
        Copy-Item -LiteralPath $captureCsv.FullName -Destination (Join-Path $outputDirectory 'frames.csv') -Force
        & python (Join-Path $PSScriptRoot 'measure_frame_capture.py') (Join-Path $outputDirectory 'frames.csv') --warmup-seconds 0 --output (Join-Path $outputDirectory 'frame-summary.json')
        if ($LASTEXITCODE -ne 0) { throw 'Garage frame-time summary failed.' }
        $frames = Get-Content -LiteralPath (Join-Path $outputDirectory 'frame-summary.json') -Raw | ConvertFrom-Json
        if ([Math]::Abs($frames.measuredSeconds - $report.captureWallSeconds) -gt 1 -or
            [Math]::Abs($frames.measuredFrames - $report.capturedFrames) -gt 3) { throw 'CSV does not match the measured garage window.' }
    }
}
foreach ($property in @('credits', 'paintId', 'tuneId', 'torque4000Nm', 'finalDrive', 'massKg')) {
    if ($reports.Save.$property -ne $reports.Reload.$property) { throw "Process relaunch changed saved $property." }
}
foreach ($property in @('ownedUpgrades', 'equippedUpgrades')) {
    if (($reports.Save.$property -join ',') -ne ($reports.Reload.$property -join ',')) { throw "Process relaunch changed saved $property." }
}
$environment = [ordered]@{
    mode = $(if ($Editor) { 'Editor' } else { 'Package' }); runtime = $runtime;
    runtimeSha256 = (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash;
    profile = $profilePath; profileSha256 = (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash;
    renderer = 'DX11/SM5, RenderOffscreen, 1280x720, 85% screen percentage, uncapped, VSync off';
    memoryScope = 'Process working-set high-water including startup; not whole-system RAM or VRAM'; runs = $environmentRuns
}
if ($Editor) { $environment.editorModuleSha256 = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-VelocityAfterdark.dll')).Hash }
$environment | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputDirectory 'run-environment.json') -Encoding utf8
Write-Host "AFTERDARK_GARAGE_SMOKE_OK: purchase, physical configuration, paint, two-process persistence, three captures and bounded CSV. Inspect $outputDirectory"

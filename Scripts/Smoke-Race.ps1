[CmdletBinding()]
param([switch]$Editor, [string]$EngineRoot = $env:UE_ROOT, [string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $projectRoot $(if ($Editor) { 'Artifacts\RaceSmokeEditor' } else { 'Artifacts\RaceSmoke' })
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$logFile = Join-Path $outputDirectory 'runtime.log'
$runtimeArguments = @('-windowed','-ResX=1280','-ResY=720','-d3d11','-RenderOffscreen',
    '-unattended','-nosplash','-AfterdarkRaceSmoke','-AfterdarkRaceSmokeExit','-csvCompression=0','"-LogCmds=LogADRace Verbose"',
    '"-ExecCmds=t.MaxFPS 0,r.VSync 0,r.ScreenPercentage 85,DisableAllScreenMessages"',('"-abslog={0}"' -f $logFile))
if ($Editor) {
    if (-not $EngineRoot) { $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8' }
    $runtime = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $runtimeArguments = @(('"{0}"' -f (Join-Path $projectRoot 'VelocityAfterdark.uproject')), '-game') + $runtimeArguments
    $workingDirectory = $projectRoot
    $savedCandidates = @((Join-Path $projectRoot 'Saved'))
} else {
    if (-not $PackageDirectory) { $PackageDirectory = Join-Path $projectRoot 'Artifacts\Package\Windows' }
    $PackageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).Path
    $runtime = Join-Path $PackageDirectory 'VelocityAfterdark\Binaries\Win64\VelocityAfterdark.exe'
    $workingDirectory = $PackageDirectory
    $savedCandidates = @((Join-Path $PackageDirectory 'VelocityAfterdark\Saved'),
        (Join-Path $env:LOCALAPPDATA 'VelocityAfterdark\Saved'))
}
if (-not (Test-Path -LiteralPath $runtime)) { throw "Runtime missing: $runtime. Build the requested target first." }
$started = Get-Date
$runtimeProcess = Start-Process -FilePath $runtime -ArgumentList $runtimeArguments -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru
$deadline = $started.AddMinutes(10)
$peakWorkingSetBytes = 0L
while (-not $runtimeProcess.WaitForExit(1000)) {
    $runtimeProcess.Refresh()
    if (-not $runtimeProcess.HasExited) { $peakWorkingSetBytes = [Math]::Max($peakWorkingSetBytes, $runtimeProcess.PeakWorkingSet64) }
    if ((Get-Date) -gt $deadline) {
        $runtimeProcess.Kill()
        throw "Packaged race smoke exceeded ten minutes. Inspect $logFile"
    }
}
$runInfo = [ordered]@{
    mode = $(if ($Editor) { 'Editor' } else { 'Package' })
    startedUtc = $started.ToUniversalTime().ToString('o')
    runtime = $runtime
    runtimeSha256 = (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash
    renderer = 'DX11/SM5, RenderOffscreen, 1280x720 output, 85% screen percentage, uncapped, VSync off'
    processPeakWorkingSetMB = [Math]::Round($peakWorkingSetBytes / 1MB, 2)
    memoryScope = 'Process working-set high-water including startup; not whole-system RAM or VRAM'
}
if ($Editor) { $runInfo.editorModuleSha256 = (Get-FileHash -LiteralPath (Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-VelocityAfterdark.dll')).Hash }
$runInfo | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputDirectory 'run-environment.json') -Encoding utf8
# Copy a fresh failure report too, so a failed physical run stays diagnosable.
$captureRoot = $null
foreach ($candidate in $savedCandidates) {
    $reportPath = Join-Path $candidate 'RaceSmoke\report.json'
    if ((Test-Path -LiteralPath $reportPath) -and (Get-Item -LiteralPath $reportPath).LastWriteTime -ge $started) {
        $captureRoot = $candidate
        Copy-Item -LiteralPath $reportPath -Destination $outputDirectory -Force
        break
    }
}
if ($runtimeProcess.ExitCode -ne 0) { throw "Packaged race exited with $($runtimeProcess.ExitCode). Inspect $logFile" }
foreach ($marker in @('Dockside ready:','AFTERDARK_RACE_CAPTURE_STARTED','AFTERDARK_RACE_CAPTURE_STOPPED','AFTERDARK_RACE_SMOKE_OK')) {
    if (-not (Select-String -LiteralPath $logFile -SimpleMatch $marker -Quiet)) { throw "Runtime marker missing: $marker" }
}
if (Select-String -LiteralPath $logFile -Pattern 'Fatal error:|LogADVehiclePhysics: Error:|Dockside load failed|AFTERDARK_RACE_SMOKE_FAILED' -Quiet) {
    throw "Race smoke reported a runtime failure. Inspect $logFile"
}
if (-not $captureRoot) { throw 'Fresh race report missing; previous runs are not evidence.' }
$reportPath = Join-Path $captureRoot 'RaceSmoke\report.json'
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if (-not $report.success -or -not $report.classificationFinal -or $report.captureWallSeconds -lt 120 -or $report.capturedFrames -lt 120) {
    throw "Race report failed completion or capture requirements: $($report.reason)"
}
if (@($report.racers).Count -ne 4) { throw 'Race report does not contain four racers.' }
$places = @()
foreach ($racer in $report.racers) {
    if (-not $racer.finished -or $racer.dnf -or $racer.recoveries -ne 0 -or $racer.completedLaps -ne 2 -or
        $racer.finishSeconds -le 0 -or $racer.bestLapSeconds -le 0 -or $racer.distanceMeters -lt 3500) {
        throw "Racer $($racer.name) failed physical completion, lap timing or distance requirements."
    }
    $places += $racer.place
}
if ((@($places | Sort-Object) -join ',') -ne '1,2,3,4') { throw 'Final places are not a complete unique classification.' }
Copy-Item -LiteralPath $reportPath -Destination $outputDirectory -Force
foreach ($name in @('AfterdarkRaceGrid.png','AfterdarkRaceResults.png')) {
    $capture = Join-Path $captureRoot "RaceSmoke\$name"
    if (-not (Test-Path -LiteralPath $capture) -or (Get-Item -LiteralPath $capture).LastWriteTime -lt $started) {
        throw "Fresh race capture missing: $name"
    }
    Copy-Item -LiteralPath $capture -Destination $outputDirectory -Force
}
$csvDirectory = Join-Path $captureRoot 'Profiling\CSV'
$captureCsv = Get-ChildItem -LiteralPath $csvDirectory -Filter '*.csv' |
    Where-Object LastWriteTime -ge $started | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $captureCsv) { throw 'Race smoke did not produce a fresh profiling CSV.' }
Copy-Item -LiteralPath $captureCsv.FullName -Destination (Join-Path $outputDirectory 'frames.csv') -Force
& python (Join-Path $PSScriptRoot 'measure_frame_capture.py') (Join-Path $outputDirectory 'frames.csv') --warmup-seconds 0 --output (Join-Path $outputDirectory 'frame-summary.json')
if ($LASTEXITCODE -ne 0) { throw 'Frame-time summary failed.' }
$frames = Get-Content -LiteralPath (Join-Path $outputDirectory 'frame-summary.json') -Raw | ConvertFrom-Json
if ([Math]::Abs($frames.measuredSeconds - $report.captureWallSeconds) -gt 1 -or
    [Math]::Abs($frames.measuredFrames - $report.capturedFrames) -gt 3) {
    throw 'CSV duration/frame count does not match the measured race window.'
}
Write-Host "AFTERDARK_RACE_SMOKE_OK: four physical finishes, lap times, results, two captures and fresh CSV. Inspect $outputDirectory"

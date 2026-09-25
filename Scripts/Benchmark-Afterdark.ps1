[CmdletBinding()]
param(
    [switch]$Editor,
    [switch]$IncludeEnvironment,
    [string]$EngineRoot = $env:UE_ROOT,
    [string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$mode = if ($Editor) { 'Editor' } else { 'Package' }
$outputDirectory = Join-Path $projectRoot "Artifacts\DrivingBenchmark$mode"
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$profileDirectory = Join-Path $outputDirectory ('Profiles\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $profileDirectory -Force | Out-Null
$profilePath = Join-Path $profileDirectory 'profile.json'
$logFile = Join-Path $outputDirectory 'runtime.log'
$arguments = @('-windowed','-ResX=1280','-ResY=720','-d3d11','-RenderOffscreen',
    '-unattended','-nosplash','-AfterdarkDriveBenchmark','-AfterdarkBenchmarkExit',
    ('"-AfterdarkProfile={0}"' -f $profilePath),
    '-csvCompression=0','"-ExecCmds=t.MaxFPS 0,r.VSync 0,r.ScreenPercentage 85,DisableAllScreenMessages"',('"-abslog={0}"' -f $logFile))
if ($IncludeEnvironment) { $arguments += '-AfterdarkEnvironment' }
if ($Editor) {
    if (-not $EngineRoot) { $EngineRoot = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8' }
    $runtime = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $arguments = @(('"{0}"' -f (Join-Path $projectRoot 'VelocityAfterdark.uproject')), '-game') + $arguments
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
$process = Start-Process -FilePath $runtime -ArgumentList $arguments -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru
$deadline = $started.AddMinutes(5)
$peakWorkingSetBytes = 0L
while (-not $process.WaitForExit(1000)) {
    $process.Refresh()
    if (-not $process.HasExited) {
        $peakWorkingSetBytes = [Math]::Max($peakWorkingSetBytes, $process.PeakWorkingSet64)
    }
    if ((Get-Date) -gt $deadline) {
        $process.Kill()
        throw "Driving benchmark exceeded five minutes. Inspect $logFile"
    }
}
$runInfo = [ordered]@{
    mode = $mode
    includeEnvironment = [bool]$IncludeEnvironment
    startedUtc = $started.ToUniversalTime().ToString('o')
    runtime = $runtime
    runtimeSha256 = (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash
    renderer = 'DX11/SM5, RenderOffscreen, 1280x720 output, 85% screen percentage, frame rate uncapped, VSync off'
    presetSource = 'Existing project and persisted GameUserSettings; inspect runtime.log for applied CVars'
    processPeakWorkingSetMB = [Math]::Round($peakWorkingSetBytes / 1MB, 2)
    memoryScope = 'Process working-set high-water sampled once per second, including startup; not total system RAM or VRAM'
}
if ($Editor) {
    $module = Join-Path $projectRoot 'Binaries\Win64\UnrealEditor-VelocityAfterdark.dll'
    $runInfo.editorModuleSha256 = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash
}
$runInfo | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputDirectory 'run-environment.json') -Encoding utf8
# Preserve a fresh failure report as well, to make route/handling failures diagnosable.
$captureRoot = $null
foreach ($candidate in $savedCandidates) {
    $reportFile = Join-Path $candidate 'Benchmark\drive-report.json'
    if ((Test-Path -LiteralPath $reportFile) -and (Get-Item -LiteralPath $reportFile).LastWriteTime -ge $started) {
        $captureRoot = $candidate
        Copy-Item -LiteralPath $reportFile -Destination $outputDirectory -Force
        break
    }
}
if (-not $captureRoot) { throw "No fresh driving report was produced. Inspect $logFile" }
$report = Get-Content -Raw -LiteralPath (Join-Path $outputDirectory 'drive-report.json') | ConvertFrom-Json
if ($process.ExitCode -ne 0 -or -not $report.success -or $report.captureWallSeconds -lt 120 -or $report.distanceMeters -lt 1200) {
    throw "Driving benchmark failed: $($report.reason). Inspect $outputDirectory"
}
foreach ($marker in @('Dockside ready:', 'Loaded Aster S6,', 'AFTERDARK_BENCHMARK_OK')) {
    if (-not (Select-String -LiteralPath $logFile -SimpleMatch $marker -Quiet)) { throw "Runtime marker missing: $marker" }
}
if ($IncludeEnvironment -and -not (Select-String -LiteralPath $logFile -SimpleMatch 'Rain streak emitter active:' -Quiet)) {
    throw 'Environment benchmark did not activate the rain emitter; previous performance captures are not evidence.'
}
if (Select-String -LiteralPath $logFile -Pattern 'Fatal error:|LogADVehiclePhysics: Error:|Dockside load failed' -Quiet) {
    throw "Runtime failure during benchmark. Inspect $logFile"
}
$csvDirectory = Join-Path $captureRoot 'Profiling\CSV'
$captureCsv = Get-ChildItem -LiteralPath $csvDirectory -Filter '*.csv' |
    Where-Object LastWriteTime -ge $started | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $captureCsv) { throw 'Fresh driving CSV missing; previous samples cannot certify this run.' }
$destination = Join-Path $outputDirectory 'frames.csv'
Copy-Item -LiteralPath $captureCsv.FullName -Destination $destination -Force
$summary = Join-Path $outputDirectory 'frame-summary.json'
& python (Join-Path $PSScriptRoot 'measure_frame_capture.py') $destination --warmup-seconds 0 --output $summary
if ($LASTEXITCODE -ne 0) { throw 'Frame-time summary failed.' }
$frames = Get-Content -Raw -LiteralPath $summary | ConvertFrom-Json
if ($frames.measuredSeconds -lt 119 -or [Math]::Abs($frames.measuredFrames - $report.measuredFrames) -gt 10) {
    throw 'CSV duration/frame count does not match the driving report.'
}
Write-Host "AFTERDARK_DRIVING_CAPTURE_OK: $mode, uncapped 1280x720 DX11, $($report.distanceMeters) m. This records performance; it does not certify a 60 FPS target."

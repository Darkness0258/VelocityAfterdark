[CmdletBinding()]
param(
    [string]$PackageDirectory,
    [ValidateRange(640,5120)][int]$ResX=1280,
    [ValidateRange(480,2880)][int]$ResY=720,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$OutputName='PackageSmoke'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $projectRoot 'Artifacts\Package\Windows' }
$PackageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).Path
$runtime = Join-Path $PackageDirectory 'VelocityAfterdark\Binaries\Win64\VelocityAfterdark.exe'
if (-not (Test-Path -LiteralPath $runtime)) { throw 'Cooked game executable missing. Run Invoke-Afterdark.ps1 -Action Package first.' }
$outputDirectory = Join-Path (Join-Path $projectRoot 'Artifacts') $OutputName
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$profileDirectory = Join-Path $outputDirectory ('Profiles\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $profileDirectory -Force | Out-Null
$profilePath = Join-Path $profileDirectory 'profile.json'
$logFile = Join-Path $outputDirectory 'runtime.log'
$started = Get-Date
$runtimeArguments = @('-windowed',("-ResX={0}" -f $ResX),("-ResY={0}" -f $ResY),'-d3d11','-RenderOffscreen',
    '-unattended','-nosplash','-AfterdarkEnvironment','-AfterdarkRenderSmoke','-AfterdarkSmokeExit','-csvCompression=0',
    ('"-AfterdarkProfile={0}"' -f $profilePath),
    '"-ExecCmds=DisableAllScreenMessages"',('"-abslog={0}"' -f $logFile))
$runtimeProcess = Start-Process -FilePath $runtime -ArgumentList $runtimeArguments -WorkingDirectory $PackageDirectory -WindowStyle Hidden -PassThru
$deadline = $started.AddMinutes(5)
while (-not $runtimeProcess.WaitForExit(1000)) {
    if ((Get-Date) -gt $deadline) {
        $runtimeProcess.Kill()
        throw "Packaged smoke exceeded five minutes. Inspect $logFile"
    }
}
if ($runtimeProcess.ExitCode -ne 0) { throw "Packaged runtime exited with $($runtimeProcess.ExitCode). Inspect $logFile" }
foreach ($marker in @('Dockside ready:','AFTERDARK_PARKED_ART_READY: 3 static cars','Loaded Aster S6,','Rain streak emitter active:',
        'AFTERDARK_GARAGE_SMOKE_COMPLETE','AFTERDARK_CUTSCENE_SMOKE_COMPLETE','AFTERDARK_RENDER_SMOKE_COMPLETE')) {
    if (-not (Select-String -LiteralPath $logFile -SimpleMatch $marker -Quiet)) { throw "Runtime marker missing: $marker" }
}
if (Select-String -LiteralPath $logFile -Pattern 'Fatal error:|LogADVehiclePhysics: Error:|Dockside load failed' -Quiet) {
    throw "Runtime reported a startup failure. Inspect $logFile"
}
$savedCandidates = @((Join-Path $PackageDirectory 'VelocityAfterdark\Saved'),
    (Join-Path $env:LOCALAPPDATA 'VelocityAfterdark\Saved'))
$captureRoot = $null
foreach ($candidate in $savedCandidates) {
    $title = Join-Path $candidate 'Smoke\AfterdarkTitle.png'
    if ((Test-Path -LiteralPath $title) -and (Get-Item -LiteralPath $title).LastWriteTime -ge $started) {
        $captureRoot = $candidate
        break
    }
}
if (-not $captureRoot) { throw 'Packaged startup finished but fresh camera captures were not found.' }
foreach ($camera in @('Title','Chase','Hood','Cockpit','Garage','Arrival01','Arrival02','Arrival03','Arrival04')) {
    $capture = Join-Path $captureRoot "Smoke\Afterdark$camera.png"
    if (-not (Test-Path -LiteralPath $capture) -or (Get-Item -LiteralPath $capture).LastWriteTime -lt $started) {
        throw "Fresh packaged $camera capture missing."
    }
    Copy-Item -LiteralPath $capture -Destination $outputDirectory -Force
}
$csvDirectory = Join-Path $captureRoot 'Profiling\CSV'
$captureCsv = Get-ChildItem -LiteralPath $csvDirectory -Filter '*.csv' |
    Where-Object LastWriteTime -ge $started | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $captureCsv) { throw 'Packaged smoke did not produce a fresh profiling CSV; previous captures are not evidence for this run.' }
Copy-Item -LiteralPath $captureCsv.FullName -Destination (Join-Path $outputDirectory 'frames.csv') -Force
Write-Host "AFTERDARK_PACKAGE_SMOKE_OK: cooked data, runtime, four driving views, garage studio, four animated cutscene shots and clean exit. Inspect $outputDirectory"

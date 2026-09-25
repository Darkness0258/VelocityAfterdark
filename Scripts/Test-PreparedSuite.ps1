[CmdletBinding()]
param([string]$EngineRoot = $env:UE_ROOT, [switch]$IncludeDrivingBenchmark)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$artifactRoot = Join-Path $projectRoot 'Artifacts'
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$outputDirectory = Join-Path $artifactRoot ('Consolidated\' + $runId)
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$proofPath = Join-Path $artifactRoot 'build-proof.json'
$sourceManifestPath = Join-Path $artifactRoot 'integrated-source-manifest.json'
if (-not (Test-Path -LiteralPath $proofPath) -or -not (Test-Path -LiteralPath $sourceManifestPath)) {
    throw 'Build a fresh integrated package first. A Phase 2 package/report cannot validate this source.'
}
$proof = Get-Content -LiteralPath $proofPath -Raw | ConvertFrom-Json
$manifest = Get-Content -LiteralPath $sourceManifestPath -Raw | ConvertFrom-Json
function Confirm-EvidenceFiles($Records) {
    foreach ($record in $Records) {
        $path = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $record.path))
        if (-not $path.StartsWith($projectRoot + [System.IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Build evidence contains a path outside the project.'
        }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $record.sha256) {
            throw "Build evidence no longer matches the current file: $($record.path). Repackage before running this suite."
        }
    }
}
Confirm-EvidenceFiles $proof.files
Confirm-EvidenceFiles $manifest.files
Copy-Item -LiteralPath $proofPath -Destination (Join-Path $outputDirectory 'build-proof.json')
Copy-Item -LiteralPath $sourceManifestPath -Destination (Join-Path $outputDirectory 'source-manifest.json')
$steps = @(
    @{ name = 'Source contracts'; action = { & python (Join-Path $PSScriptRoot 'validate_source.py'); if ($LASTEXITCODE -ne 0) { throw 'Source contract validation failed.' } } }
    @{ name = 'Portable vehicle math'; action = { & (Join-Path $PSScriptRoot 'Test-Core.ps1') } }
    @{ name = 'Portable race rules'; action = { & (Join-Path $PSScriptRoot 'Test-RaceRules.ps1') } }
    @{ name = 'Unreal required cases'; action = { & (Join-Path $PSScriptRoot 'Invoke-Afterdark.ps1') -Action Test -EngineRoot $EngineRoot } }
    @{ name = 'Cooked camera smoke'; action = { & (Join-Path $PSScriptRoot 'Smoke-Package.ps1') } }
    @{ name = 'Packaged display/viewport matrix'; action = { & (Join-Path $PSScriptRoot 'Smoke-DisplayMatrix.ps1') } }
    @{ name = 'Cooked race smoke'; action = { & (Join-Path $PSScriptRoot 'Smoke-Race.ps1') } }
    @{ name = 'Cooked garage save and relaunch'; action = { & (Join-Path $PSScriptRoot 'Smoke-Garage.ps1') } }
    @{ name = 'Two-process authoritative multiplayer smoke'; action = { & (Join-Path $PSScriptRoot 'Smoke-Multiplayer.ps1') } }
)
if ($IncludeDrivingBenchmark) {
    $steps += @{ name = 'Cooked driving benchmark'; action = { & (Join-Path $PSScriptRoot 'Benchmark-Afterdark.ps1') } }
}
$results = @($steps | ForEach-Object { [ordered]@{ name = $_.name; state = 'NotRun'; durationSeconds = 0; error = '' } })
$started = [DateTime]::UtcNow
$failure = $null
Push-Location -LiteralPath $projectRoot
try {
    for ($index = 0; $index -lt $steps.Count; $index++) {
        Write-Host "RUNNING: $($steps[$index].name)"
        $timer = [System.Diagnostics.Stopwatch]::StartNew()
        try {
            & $steps[$index].action 2>&1 | Tee-Object -FilePath (Join-Path $outputDirectory ("{0:00}.log" -f $index))
            $results[$index].state = 'Passed'
        } catch {
            $results[$index].state = 'Failed'
            $results[$index].error = $_.Exception.Message
            throw
        } finally {
            $timer.Stop()
            $results[$index].durationSeconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
        }
    }
    # Refuse acceptance against a moving source/package even if every fixture passed.
    Confirm-EvidenceFiles $manifest.files
    Confirm-EvidenceFiles $proof.files
} catch {
    $failure = $_
} finally {
    Pop-Location
    $report = [ordered]@{
        startedAtUtc = $started.ToString('o')
        endedAtUtc = [DateTime]::UtcNow.ToString('o')
        preparedCasesPassed = ($null -eq $failure)
        fullRoadmapAccepted = $false
        scope = 'Prepared automation and isolated package fixtures only. Living-world, multiplayer, device, asset and full performance acceptance remain separate.'
        remainingPlan = 'Docs/CONSOLIDATED_TEST_PLAN.md'
        failure = $(if ($failure) { $failure.Exception.Message } else { '' })
        stages = $results
    }
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outputDirectory 'report.json') -Encoding utf8
}
if ($failure) { throw $failure }
Write-Host "AFTERDARK_PREPARED_SUITE_PASSED: $outputDirectory. This does not accept the full roadmap."

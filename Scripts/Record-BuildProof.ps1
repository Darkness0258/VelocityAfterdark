[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$artifactRoot = Join-Path $projectRoot 'Artifacts'
$packageRoot = Join-Path $artifactRoot 'Package\Windows'
$packageLog = Join-Path $artifactRoot 'package.log'
$executable = Join-Path $packageRoot 'VelocityAfterdark\Binaries\Win64\VelocityAfterdark.exe'
$ufsManifest = Join-Path $packageRoot 'Manifest_UFSFiles_Win64.txt'
foreach ($required in @($packageLog, $executable, $ufsManifest)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing build evidence: $required" }
}
$logText = Get-Content -LiteralPath $packageLog -Raw
if ($logText -notmatch 'BUILD SUCCESSFUL' -or $logText -notmatch 'AutomationTool exiting with ExitCode=0') {
    throw 'The package log does not record a successful completed build/cook/archive.'
}
$completedAt = (Get-Item -LiteralPath $packageLog).LastWriteTimeUtc
$runtimeFiles = @(
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source') -Recurse -File
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Config') -Recurse -File
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Content') -Recurse -File |
        Where-Object Extension -in @('.json', '.uasset', '.umap', '.glb', '.obj', '.mtl')
    Get-Item -LiteralPath (Join-Path $projectRoot 'VelocityAfterdark.uproject')
)
foreach ($file in $runtimeFiles) {
    if ($file.LastWriteTimeUtc -gt $completedAt) { throw "Runtime source changed after packaging: $($file.FullName)" }
}
function Get-EvidenceRecord([System.IO.FileInfo]$File) {
    [ordered]@{
        path = $File.FullName.Substring($projectRoot.Length + 1).Replace('\', '/')
        sha256 = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash
        bytes = $File.Length
    }
}
$sourceManifest = [ordered]@{
    project = 'VelocityAfterdark'
    packagedAtUtc = $completedAt.ToString('o')
    purpose = 'Runtime source and authored/generated content identity for the compiled package. This is not a test report.'
    files = @($runtimeFiles | Sort-Object FullName | ForEach-Object { Get-EvidenceRecord $_ })
}
$sourceManifestPath = Join-Path $artifactRoot 'integrated-source-manifest.json'
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($sourceManifestPath, ($sourceManifest | ConvertTo-Json -Depth 6), $utf8)

$proofPath = Join-Path $artifactRoot 'build-proof.json'
$phase2Proof = Join-Path $artifactRoot 'phase2-build-proof.json'
if ((Test-Path -LiteralPath $proofPath) -and -not (Test-Path -LiteralPath $phase2Proof)) {
    $previous = Get-Content -LiteralPath $proofPath -Raw | ConvertFrom-Json
    if ($previous.phase -eq 2) { Copy-Item -LiteralPath $proofPath -Destination $phase2Proof }
}
$artifacts = @(
    Get-Item -LiteralPath $executable
    Get-Item -LiteralPath $ufsManifest
    Get-Item -LiteralPath $packageLog
    Get-Item -LiteralPath $sourceManifestPath
    Get-ChildItem -LiteralPath (Join-Path $packageRoot 'VelocityAfterdark\Content\Paks') -File |
        Where-Object Extension -in @('.pak', '.utoc', '.ucas')
)
$proof = [ordered]@{
    project = 'VelocityAfterdark'
    phase = 'Integrated development build, Phases 3-9 source scope'
    packagedAtUtc = $completedAt.ToString('o')
    recordedAtUtc = [DateTime]::UtcNow.ToString('o')
    acceptedScope = 'C++/UHT compilation, Windows cook/stage/archive and runtime data inclusion only.'
    runtimeValidation = 'Separate test reports are required. This build record alone does not certify gameplay, multiplayer, device or performance acceptance.'
    previousPhase2ReportsApplyToThisExecutable = $false
    fullRoadmapComplete = $false
    files = @($artifacts | ForEach-Object { Get-EvidenceRecord $_ })
}
[System.IO.File]::WriteAllText($proofPath, ($proof | ConvertTo-Json -Depth 6), $utf8)
Write-Host "AFTERDARK_BUILD_PROOF_RECORDED: $proofPath (build evidence only; inspect separate runtime reports)."

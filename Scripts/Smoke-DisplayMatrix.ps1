[CmdletBinding()]
param([string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $projectRoot 'Artifacts\Package\Windows' }
$PackageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).Path
$outputDirectory = Join-Path $projectRoot 'Artifacts\DisplayMatrix'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$smoke = Join-Path $PSScriptRoot 'Smoke-Package.ps1'
$resolutions = @(
    [pscustomobject]@{ width=800; height=600 },
    [pscustomobject]@{ width=1280; height=720 },
    [pscustomobject]@{ width=1920; height=1080 },
    [pscustomobject]@{ width=2560; height=1440 }
)
$records = [System.Collections.Generic.List[object]]::new()
$started = [DateTime]::UtcNow
$failure = $null
function Read-PngDimensions([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = [System.IO.BinaryReader]::new($stream)
        $signature = $reader.ReadBytes(8)
        if (([BitConverter]::ToString($signature).Replace('-','')) -ne '89504E470D0A1A0A') { throw "Invalid PNG: $Path" }
        [void]$reader.ReadBytes(4)
        if ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(4)) -ne 'IHDR') { throw "PNG IHDR is missing: $Path" }
        [byte[]]$widthBytes = $reader.ReadBytes(4)
        [byte[]]$heightBytes = $reader.ReadBytes(4)
        [Array]::Reverse($widthBytes); [Array]::Reverse($heightBytes)
        return [pscustomobject]@{ width=[BitConverter]::ToInt32($widthBytes,0); height=[BitConverter]::ToInt32($heightBytes,0) }
    } finally { $stream.Dispose() }
}
try {
    foreach ($resolution in $resolutions) {
        $key = '{0}x{1}' -f $resolution.width,$resolution.height
        $name = 'DisplayMatrix_' + $key
        Write-Host "RUNNING_PACKAGED_VIEWPORT: $key"
        & $smoke -PackageDirectory $PackageDirectory -ResX $resolution.width -ResY $resolution.height -OutputName $name
        $captureSet = @()
        $captureDimensions = @()
        foreach ($camera in @('Title','Chase','Hood','Cockpit')) {
            $capture = Join-Path (Join-Path $projectRoot 'Artifacts') "$name\Afterdark$camera.png"
            if (-not (Test-Path -LiteralPath $capture -PathType Leaf)) { throw "Missing $camera capture at $key." }
            $dimensions = Read-PngDimensions $capture
            if ($dimensions.width -lt 640 -or $dimensions.height -lt 480) { throw "$camera capture at $key is too small: $($dimensions.width)x$($dimensions.height)." }
            $captureSet += $capture
            $captureDimensions += $dimensions
        }
        $actualWidth = $captureDimensions[0].width
        $actualHeight = $captureDimensions[0].height
        foreach ($dimensions in $captureDimensions) {
            if ($dimensions.width -ne $actualWidth -or $dimensions.height -ne $actualHeight) {
                throw "Camera captures at $key use inconsistent render sizes."
            }
        }
        $requestedAspect = $resolution.width / [double]$resolution.height
        $captureAspect = $actualWidth / [double]$actualHeight
        if ([Math]::Abs($captureAspect - $requestedAspect) -gt 0.02) {
            throw "Capture aspect ratio at $key changed from $requestedAspect to $captureAspect."
        }
        $runtimeLog = Get-Content -LiteralPath (Join-Path (Join-Path $projectRoot 'Artifacts') "$name\runtime.log") -Raw
        $reportedWidth = [regex]::Matches($runtimeLog, 'systemresolution\.resx="(\d+)"')
        $reportedHeight = [regex]::Matches($runtimeLog, 'systemresolution\.resy="(\d+)"')
        if ($reportedWidth.Count -eq 0 -or $reportedHeight.Count -eq 0) { throw "Runtime render dimensions were not logged for $key." }
        $renderWidth = [int]$reportedWidth[$reportedWidth.Count - 1].Groups[1].Value
        $renderHeight = [int]$reportedHeight[$reportedHeight.Count - 1].Groups[1].Value
        if ($renderWidth -ne $actualWidth -or $renderHeight -ne $actualHeight) {
            throw "PNG capture at $key ($actualWidth x $actualHeight) differs from the runtime render size ($renderWidth x $renderHeight)."
        }
        $records.Add([ordered]@{
            requestedResolution=$key
            captureResolution="${actualWidth}x${actualHeight}"
            runtimeRenderResolution="${renderWidth}x${renderHeight}"
            cameraCaptureCount=$captureSet.Count
            state='Passed'
        })
    }
} catch { $failure = $_ }
finally {
    $report = [ordered]@{
        startedAtUtc=$started.ToString('o')
        endedAtUtc=[DateTime]::UtcNow.ToString('o')
        runtimeSha256=(Get-FileHash -LiteralPath (Join-Path $PackageDirectory 'VelocityAfterdark\Binaries\Win64\VelocityAfterdark.exe') -Algorithm SHA256).Hash
        passed=($null -eq $failure -and $records.Count -eq $resolutions.Count)
        scope='Packaged render/capture validation at four requested window sizes. Captures must be large enough, preserve the requested aspect ratio, match each other and match Unreal runtime render-resolution metadata. RenderOffscreen may scale below the requested physical size and does not certify physical displays, fullscreen modes, refresh rates, HDR or another GPU.'
        failure=$(if ($failure) { $failure.Exception.Message } else { '' })
        results=@($records)
    }
    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outputDirectory 'report.json') -Encoding utf8
}
if ($failure) { throw $failure }
Write-Host "AFTERDARK_DISPLAY_MATRIX_OK: $($records.Count) requested sizes with four consistent, aspect-preserving packaged captures each. Inspect $outputDirectory"

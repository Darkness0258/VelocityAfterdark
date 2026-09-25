[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$artifactRoot = Join-Path $projectRoot 'Artifacts\CoreTests'
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
$sourceFile = Join-Path $projectRoot 'Tests\VehicleMathTests.cpp'
$includeDir = Join-Path $projectRoot 'Source\VelocityAfterdark\Public'
$executable = Join-Path $artifactRoot 'VehicleMathTests.exe'
$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compiler) { throw 'Portable tests require g++ on PATH. Unreal tests can instead run through Invoke-Afterdark.ps1 -Action Test.' }
& $compiler.Source '-std=c++14' '-Wall' '-Wextra' '-Werror' '-pedantic' '-O2' "-I$includeDir" $sourceFile '-o' $executable
if ($LASTEXITCODE -ne 0) { throw "Portable test compilation failed ($LASTEXITCODE)." }
& $executable | Tee-Object -FilePath (Join-Path $artifactRoot 'results.txt')
if ($LASTEXITCODE -ne 0) { throw "Portable vehicle math tests failed ($LASTEXITCODE)." }

[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$artifactRoot = Join-Path $projectRoot 'Artifacts\RaceRuleTests'
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
$sourceFile = Join-Path $projectRoot 'Tests\RaceRulesTests.cpp'
$includeDir = Join-Path $projectRoot 'Source\VelocityAfterdark\Public'
$executable = Join-Path $artifactRoot 'RaceRulesTests.exe'
$compiler = Get-Command g++ -ErrorAction SilentlyContinue
if (-not $compiler) { throw 'Portable race rules tests require g++ on PATH.' }
& $compiler.Source '-std=c++14' '-Wall' '-Wextra' '-Werror' '-pedantic' '-O2' "-I$includeDir" $sourceFile '-o' $executable
if ($LASTEXITCODE -ne 0) { throw "Race rules test compilation failed ($LASTEXITCODE)." }
& $executable | Tee-Object -FilePath (Join-Path $artifactRoot 'results.txt')
if ($LASTEXITCODE -ne 0) { throw "Race rules tests failed ($LASTEXITCODE)." }

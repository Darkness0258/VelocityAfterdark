[CmdletBinding()]
param(
    [ValidateSet('Check', 'Build', 'Bootstrap', 'UpgradeVisuals', 'Open', 'Test', 'Render', 'Package')]
    [string]$Action = 'Check',
    [string]$EngineRoot = $env:UE_ROOT,
    [string]$TestFilter
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'VelocityAfterdark.uproject'
$artifactRoot = Join-Path $projectRoot 'Artifacts'
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null

if (-not $EngineRoot) {
    $candidate = Join-Path $env:ProgramFiles 'Epic Games\UE_5.8'
    if (Test-Path -LiteralPath $candidate) { $EngineRoot = $candidate }
}
if (-not $EngineRoot) { throw 'Unreal Engine was not found. Pass -EngineRoot with the UE 5.8 installation directory.' }
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$buildScript = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$editorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$versionFile = Join-Path $EngineRoot 'Engine\Build\Build.version'
foreach ($required in @($buildScript, $editorCmd, $editor, $versionFile)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Engine component missing: $required" }
}
$version = Get-Content -Raw -LiteralPath $versionFile | ConvertFrom-Json
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8) {
    throw "This project targets UE 5.8; found $($version.MajorVersion).$($version.MinorVersion). Select UE 5.8 or explicitly port the target first."
}
Write-Host "Engine: $EngineRoot ($($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion))"
Write-Host "Project: $projectFile"

function Invoke-Logged([string]$Executable, [string[]]$Arguments, [string]$LogName) {
    $logFile = Join-Path $artifactRoot $LogName
    & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logFile | ForEach-Object {
        if ($_ -match '(?i)(error|failed|succeeded|AFTERDARK_|LogADVehicle|Dockside ready|^\[\d+/\d+\]|^Result:|Test Completed|Smoke acceleration|Smoke braking)') { Write-Host $_ }
    }
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit $LASTEXITCODE. See $logFile" }
}
function Build-Editor {
    & python (Join-Path $PSScriptRoot 'validate_source.py')
    if ($LASTEXITCODE -ne 0) { throw 'Project source/data validation failed; build was not started.' }
    # Gathering includes newly added source files; a cached UBT makefile can omit
    # new .cpp tests until project-file regeneration otherwise.
    Invoke-Logged $buildScript @('VelocityAfterdarkEditor','Win64','Development',"-Project=$projectFile",'-NoUBTMakefiles','-WaitMutex','-NoHotReloadFromIDE','-NoUBA','-MaxParallelActions=1') 'build-editor.log'
}
switch ($Action) {
    'Check' {
        Write-Host 'Engine entry points found. This check does not compile the project.'
    }
    'Build' { Build-Editor }
    'Bootstrap' {
        Build-Editor
        $bootstrap = Join-Path $PSScriptRoot 'Editor\bootstrap_content.py'
        Invoke-Logged $editorCmd @($projectFile,'-run=pythonscript',"-script=$bootstrap",'-unattended','-nosplash','-NullRHI','-UTF8Output') 'bootstrap.log'
        $mapFile = Join-Path $projectRoot 'Content\Velocity\Maps\L_Dockside.umap'
        if (-not (Test-Path -LiteralPath $mapFile)) { throw 'Bootstrap returned without creating the required Dockside map.' }
    }
    'UpgradeVisuals' {
        Build-Editor
        $textureGenerator = Join-Path $PSScriptRoot 'generate_surface_textures.py'
        & python $textureGenerator
        if ($LASTEXITCODE -ne 0) { throw 'Procedural surface maps could not be generated.' }
        $upgrade = Join-Path $PSScriptRoot 'Editor\upgrade_visual_materials.py'
        Invoke-Logged $editorCmd @($projectFile,'-run=pythonscript',"-script=$upgrade",'-unattended','-nosplash','-NullRHI','-UTF8Output') 'visual-materials.log'
        $visualLog = Join-Path $artifactRoot 'visual-materials.log'
        if (-not (Select-String -LiteralPath $visualLog -SimpleMatch 'Python script executed successfully' -Quiet)) {
            throw 'UE did not report a successful PBR material import.'
        }
        $polyHavenUpgrade = Join-Path $PSScriptRoot 'Editor\import_polyhaven_asphalt.py'
        Invoke-Logged $editorCmd @($projectFile,'-run=pythonscript',"-script=$polyHavenUpgrade",'-unattended','-nosplash','-NullRHI','-UTF8Output') 'polyhaven-asphalt.log'
        $polyHavenLog = Join-Path $artifactRoot 'polyhaven-asphalt.log'
        if (-not (Select-String -LiteralPath $polyHavenLog -SimpleMatch 'Python script executed successfully' -Quiet)) {
            throw 'UE did not report a successful Poly Haven asphalt import.'
        }
        $generatedContent = Join-Path $projectRoot 'Content\Velocity\Materials'
        foreach ($asset in @('M_Asphalt_Smooth_V3.uasset','M_Concrete_PBR_V3.uasset','M_Building_PBR_V3.uasset',
                'M_IndustrialMetal_PBR.uasset','M_Rubber_PBR.uasset',
                'Generated\T_Asphalt_Surface_V2.uasset','Generated\T_Asphalt_Normal_V2.uasset',
                'Generated\T_Concrete_Surface_V2.uasset','Generated\T_Concrete_Normal_V2.uasset',
                'Generated\T_Facade_Surface_V2.uasset','Generated\T_Facade_Normal_V2.uasset',
                'Generated\T_Asphalt_Surface_V3.uasset','Generated\T_Asphalt_Normal_V3.uasset',
                'Generated\T_Concrete_Surface_V3.uasset','Generated\T_Concrete_Normal_V3.uasset',
                'Generated\T_Facade_Surface_V3.uasset','Generated\T_Facade_Normal_V3.uasset',
                'Generated\T_IndustrialMetal_Surface_V3.uasset','Generated\T_IndustrialMetal_Normal_V3.uasset',
                'Generated\T_Rubber_Surface_V3.uasset','Generated\T_Rubber_Normal_V3.uasset')) {
            if (-not (Test-Path -LiteralPath (Join-Path $generatedContent $asset))) {
                throw "PBR material build did not create $asset"
            }
        }
        $asphaltContent = Join-Path $projectRoot 'Content\Velocity'
        foreach ($asset in @('External\AsphaltTrack\T_AsphaltTrack_Diffuse_2K.uasset',
                'External\AsphaltTrack\T_AsphaltTrack_NormalDX_2K.uasset',
                'External\AsphaltTrack\T_AsphaltTrack_Roughness_2K.uasset',
                'Materials\M_Asphalt_PolyHaven.uasset')) {
            if (-not (Test-Path -LiteralPath (Join-Path $asphaltContent $asset))) {
                throw "Poly Haven asphalt import did not create $asset"
            }
        }
        Write-Host 'AFTERDARK_VISUAL_ASSETS_OK: wettable Poly Haven asphalt, five updated PBR materials and sixteen generated surface maps.'
    }
    'Open' {
        $mapFile = Join-Path $projectRoot 'Content\Velocity\Maps\L_Dockside.umap'
        if (-not (Test-Path -LiteralPath $mapFile)) { throw 'Run -Action Bootstrap first to generate the map and materials.' }
        & $editor $projectFile
    }
    'Test' {
        Build-Editor
        $testStarted = Get-Date
        $automationQuery=if ($TestFilter) { $TestFilter } else { 'Afterdark' }
        Invoke-Logged $editorCmd @($projectFile,'-unattended','-NullRHI','-nosound','-nosplash','-LogCmds=LogADRace Verbose',"-ExecCmds=Automation RunTests $automationQuery",'-TestExit=Automation Test Queue Empty',"-ReportExportPath=$artifactRoot\UnrealTests", "-abslog=$artifactRoot\automation-engine.log",'-UTF8Output') 'automation.log'
        $reportPath = Join-Path $artifactRoot 'UnrealTests\index.json'
        if (-not (Test-Path -LiteralPath $reportPath) -or (Get-Item -LiteralPath $reportPath).LastWriteTime -lt $testStarted) {
            throw 'Unreal did not produce a fresh automation report.'
        }
        $report = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
        $expectedTests = @('Afterdark.Data.VehicleDefinition', 'Afterdark.Data.RaceDefinition', 'Afterdark.Runtime.InputAndCamera')
        foreach ($rate in @(30, 60, 120)) {
            $expectedTests += "Afterdark.Runtime.DriveAndBrake.${rate}Hz"
            foreach ($scenario in @('HighSpeed', 'Steering', 'Barrier', 'StrandedRecovery', 'Curb', 'Wheelspin', 'TractionControl')) {
                $expectedTests += "Afterdark.Runtime.Handling.$scenario.${rate}Hz"
            }
        }
        $expectedTests += @('Afterdark.Runtime.Race.Easy', 'Afterdark.Runtime.Race.Normal', 'Afterdark.Runtime.Race.Hard')
        $expectedTests += @('Afterdark.Runtime.GarageFlow', 'Afterdark.Runtime.GarageAcceleration',
            'Afterdark.Ownership.Transactions', 'Afterdark.Ownership.CorruptionRecovery', 'Afterdark.Ownership.MigrationAndRollback')
        $expectedTests += @('Afterdark.Data.RoadNetwork', 'Afterdark.Ownership.WorldProgress')
        $expectedTests += @('Afterdark.Data.RaceCatalog', 'Afterdark.Career.RouteProgression',
            'Afterdark.Career.CommitPersistence',
            'Afterdark.Runtime.CareerRaceRewardRetry',
            'Afterdark.Map.ZoomPanProjection', 'Afterdark.Ownership.MapWaypointPersistence',
            'Afterdark.Ownership.VehicleCollection', 'Afterdark.Ownership.VehicleMigrationAndRollback',
            'Afterdark.Runtime.GarageVehicleSwitch', 'Afterdark.Input.Rebinding',
            'Afterdark.Settings.DraftApplyCancel', 'Afterdark.Settings.CanvasScale',
            'Afterdark.Runtime.LivingWorld', 'Afterdark.Runtime.PhotoReplay')
        foreach ($route in @('Foundry', 'IronQuay', 'NightSurvey', 'Northfield', 'GlassCoast', 'Sable', 'AfterdarkFinal', 'AfterdarkSprint')) {
            $expectedTests += "Afterdark.Runtime.RegionalRace.$route"
        }
        if ($TestFilter) {
            $expectedRun = @($expectedTests | Where-Object { $_.StartsWith($TestFilter,[StringComparison]::Ordinal) })
        } else {
            $expectedRun = @($expectedTests)
        }
        if ($expectedRun.Count -eq 0) { throw "No prepared automation cases match $TestFilter." }
        foreach ($testName in $expectedRun) {
            $entry = @($report.tests | Where-Object fullTestPath -eq $testName)
            if ($entry.Count -ne 1 -or $entry[0].state -ne 'Success') {
                throw "Expected test did not pass: $testName. Inspect $reportPath"
            }
        }
        if ($report.failed -gt 0 -or $report.notRun -gt 0) {
            throw "Unreal automation has failed or unrun tests. Inspect $reportPath"
        }
        # Complex latent automation cases are reported per-test with state=Success,
        # while Unreal's aggregate succeeded counter can remain zero for PIE tests.
        Write-Host "AFTERDARK_TESTS_OK: $($expectedRun.Count) passed, 0 failed. Report: $reportPath"
    }
    'Render' {
        Build-Editor
        $renderLog = Join-Path $artifactRoot 'render-smoke.log'
        $renderStarted = Get-Date
        $renderArguments = @(('"{0}"' -f $projectFile),'/Game/Velocity/Maps/L_Dockside',
            '-game','-windowed','-ResX=1280','-ResY=720','-d3d11','-RenderOffscreen',
            '-unattended','-nosplash','-AfterdarkEnvironment','-AfterdarkRenderSmoke','-AfterdarkSmokeExit',
            '-csvCompression=0','"-ExecCmds=DisableAllScreenMessages"',('"-abslog={0}"' -f $renderLog))
        $renderProcess = Start-Process -FilePath $editor -ArgumentList $renderArguments -WindowStyle Hidden -PassThru
        if (-not $renderProcess.WaitForExit(300000)) {
            $renderProcess.Kill()
            throw "The render smoke helper exceeded five minutes. Inspect $renderLog"
        }
        if ($renderProcess.ExitCode -ne 0 -or -not (Select-String -LiteralPath $renderLog -SimpleMatch 'AFTERDARK_RENDER_SMOKE_COMPLETE' -Quiet)) {
            throw "Rendered startup/capture did not finish. Inspect $renderLog"
        }
        $renderOutput = Join-Path $artifactRoot 'Render'
        New-Item -ItemType Directory -Path $renderOutput -Force | Out-Null
        foreach ($camera in @('Title','Chase','Hood','Cockpit','Arrival01','Arrival02','Arrival03','Arrival04')) {
            $capture = Join-Path $projectRoot "Saved\Smoke\Afterdark$camera.png"
            if (-not (Test-Path -LiteralPath $capture) -or (Get-Item -LiteralPath $capture).LastWriteTime -lt $renderStarted) {
                throw "A fresh $camera capture was not produced. Inspect $renderLog"
            }
            Copy-Item -LiteralPath $capture -Destination $renderOutput -Force
        }
        $garageCapture = Join-Path $projectRoot 'Saved\Smoke\AfterdarkGarage.png'
        if (-not (Test-Path -LiteralPath $garageCapture) -or (Get-Item -LiteralPath $garageCapture).LastWriteTime -lt $renderStarted) {
            throw "A fresh garage lighting capture was not produced. Inspect $renderLog"
        }
        Copy-Item -LiteralPath $garageCapture -Destination $renderOutput -Force
        if (-not (Select-String -LiteralPath $renderLog -SimpleMatch 'AFTERDARK_CUTSCENE_SMOKE_COMPLETE' -Quiet)) {
            throw 'The packaged-style arrival cutscene did not complete with a live, driveable vehicle.'
        }
        if (-not (Select-String -LiteralPath $renderLog -SimpleMatch 'AFTERDARK_GARAGE_SMOKE_COMPLETE' -Quiet)) {
            throw 'The garage rendering smoke did not complete with its new PBR surfaces and exposure.'
        }
        Write-Host "AFTERDARK_RENDER_CAPTURE_OK: four driving views, garage studio and four moving cutscene shots in $renderOutput."
    }
    'Package' {
        $mapFile = Join-Path $projectRoot 'Content\Velocity\Maps\L_Dockside.umap'
        if (-not (Test-Path -LiteralPath $mapFile)) { throw 'Run -Action Bootstrap before packaging.' }
        $uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
        Invoke-Logged $uat @('BuildCookRun',"-project=$projectFile",'-noP4','-platform=Win64','-clientconfig=Development','-build','-cook','-stage','-pak','-archive',"-archivedirectory=$artifactRoot\Package",'-UbtArgs=-NoUBTMakefiles -NoUBA -MaxParallelActions=1','-utf8output') 'package.log'
        $manifest = Join-Path $artifactRoot 'Package\Windows\Manifest_UFSFiles_Win64.txt'
        if (-not (Test-Path -LiteralPath $manifest)) { throw 'Package has no staged UFS manifest.' }
        $dataRoot = Join-Path $projectRoot 'Content\Data'
        foreach ($dataFile in (Get-ChildItem -LiteralPath $dataRoot -Recurse -File -Filter '*.json')) {
            $relativeData = $dataFile.FullName.Substring($dataRoot.Length + 1).Replace('\', '/')
            $requiredData = 'VelocityAfterdark/Content/Data/' + $relativeData
            if (-not (Select-String -LiteralPath $manifest -SimpleMatch $requiredData -Quiet)) {
                throw "Cooked package omitted required runtime data: $requiredData"
            }
        }
        $requiredCityCar = 'SM_CC0_CityCar.uasset'
        if (-not (Select-String -LiteralPath $manifest -SimpleMatch $requiredCityCar -Quiet)) {
            throw "Cooked package omitted the imported CC0 background vehicle: $requiredCityCar"
        }
        $requiredVisualAssets = @(
            'VelocityAfterdark/Content/Velocity/Materials/M_Asphalt_PolyHaven.uasset',
            'VelocityAfterdark/Content/Velocity/External/AsphaltTrack/T_AsphaltTrack_Diffuse_2K.uasset',
            'VelocityAfterdark/Content/Velocity/External/AsphaltTrack/T_AsphaltTrack_NormalDX_2K.uasset',
            'VelocityAfterdark/Content/Velocity/External/AsphaltTrack/T_AsphaltTrack_Roughness_2K.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/M_Asphalt_Smooth.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/M_Building_PBR.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/M_Concrete_PBR.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/M_Asphalt_Smooth_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/M_Building_PBR_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/M_Concrete_PBR_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/M_IndustrialMetal_PBR.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/M_Rubber_PBR.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Asphalt_Surface_V2.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Concrete_Surface_V2.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Facade_Surface_V2.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Asphalt_Normal_V2.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Concrete_Normal_V2.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Facade_Normal_V2.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Asphalt_Surface_V2.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Concrete_Surface_V2.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Facade_Surface_V2.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Asphalt_Surface_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Concrete_Surface_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Facade_Surface_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_IndustrialMetal_Surface_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Rubber_Surface_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Asphalt_Normal_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Concrete_Normal_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Facade_Normal_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_IndustrialMetal_Normal_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Rubber_Normal_V3.uasset',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Asphalt_Normal_V2.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Concrete_Normal_V2.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Facade_Normal_V2.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Asphalt_Surface_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Concrete_Surface_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Facade_Surface_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_IndustrialMetal_Surface_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Rubber_Surface_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Asphalt_Normal_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Concrete_Normal_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Facade_Normal_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_IndustrialMetal_Normal_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/Materials/Generated/T_Rubber_Normal_V3.ubulk',
            'VelocityAfterdark/Content/Velocity/External/AsphaltTrack/T_AsphaltTrack_Diffuse_2K.ubulk',
            'VelocityAfterdark/Content/Velocity/External/AsphaltTrack/T_AsphaltTrack_NormalDX_2K.ubulk',
            'VelocityAfterdark/Content/Velocity/External/AsphaltTrack/T_AsphaltTrack_Roughness_2K.ubulk',
            'VelocityAfterdark/Content/Velocity/External/KenneyCityKitCommercial/SM_Kenney_Commercial_SkyscraperA.uasset',
            'VelocityAfterdark/Content/Velocity/External/KenneyCityKitCommercial/SM_Kenney_Commercial_BuildingA.uasset',
            'VelocityAfterdark/Content/Velocity/External/KenneyCityKitIndustrial/SM_Kenney_Industrial_BuildingA.uasset',
            'VelocityAfterdark/Content/Velocity/External/KenneyCityKitIndustrial/SM_Kenney_Industrial_ContainerA.uasset',
            'VelocityAfterdark/Content/Velocity/External/KenneyCityKitRoads/SM_Kenney_Roads_LampCurved.uasset',
            'VelocityAfterdark/Content/Velocity/External/KenneyCityKitRoads/SM_Kenney_Roads_TrafficLight.uasset'
        )
        foreach ($visualAsset in $requiredVisualAssets) {
            if (-not (Select-String -LiteralPath $manifest -SimpleMatch $visualAsset -Quiet)) {
                throw "Cooked package omitted required visual asset: $visualAsset"
            }
        }
        & (Join-Path $PSScriptRoot 'Record-BuildProof.ps1')
        Write-Host 'AFTERDARK_PACKAGE_OK: Windows archive, runtime JSON, road PBR, district materials, CC0 city car and Kenney district meshes staged. Runtime acceptance is separate from packaging.'
    }
}

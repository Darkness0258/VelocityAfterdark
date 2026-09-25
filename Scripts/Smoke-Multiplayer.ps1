[CmdletBinding()]
param([string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $PackageDirectory) { $PackageDirectory = Join-Path $projectRoot 'Artifacts\Package\Windows' }
$PackageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).Path
$runtime = Join-Path $PackageDirectory 'VelocityAfterdark\Binaries\Win64\VelocityAfterdark.exe'
if (-not (Test-Path -LiteralPath $runtime)) { throw 'Cooked game executable missing. Run Invoke-Afterdark.ps1 -Action Package first.' }
$outputDirectory = Join-Path $projectRoot 'Artifacts\NetworkSmoke'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$profileDirectory = Join-Path $outputDirectory ('Profiles\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $profileDirectory -Force | Out-Null
$hostProfile = Join-Path $profileDirectory 'host.json'
$clientProfile = Join-Path $profileDirectory 'client.json'
$hostLog = Join-Path $outputDirectory 'host.log'
$clientLog = Join-Path $outputDirectory 'client.log'
$baseArguments = @('-windowed','-ResX=1280','-ResY=720','-d3d11','-RenderOffscreen','-unattended','-nosplash')
$hostArguments = $baseArguments + @('-AfterdarkNetHostProbe','-ExecCmds=ADHost',('"-AfterdarkProfile={0}"' -f $hostProfile),('"-abslog={0}"' -f $hostLog))
$clientArguments = $baseArguments + @('"-ExecCmds=ADJoin 127.0.0.1:7777"',
    '-AfterdarkNetDriveProbe',('"-AfterdarkProfile={0}"' -f $clientProfile),('"-abslog={0}"' -f $clientLog))
$hostProcess = $null
$clientProcess = $null
$started = Get-Date
$result = [ordered]@{
    mode = 'Packaged local listen server and one remote client'
    startedUtc = $started.ToUniversalTime().ToString('o')
    runtimeSha256 = (Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash
    hostListened = $false
    clientWelcomed = $false
    clientLoadedNetworkMap = $false
    clientSentDrivingInput = $false
    serverSimulatedVehicleMovement = $false
    clientReceivedReplicatedTelemetry = $false
    serverObservedJoin = $false
    serverObservedDisconnect = $false
    passed = $false
    scope = 'Two packaged processes on loopback; validates listen/join/map load, one client drive RPC causing authoritative server movement, returned speed telemetry, and server-observed disconnect. Four-player capacity, external networks, NAT, latency/loss and host migration are not covered.'
    endedUtc = ''
    clientExitCode = $null
    hostExitCode = $null
}
function Wait-ForLog([string]$Path,[string]$Pattern,[int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if (Test-Path -LiteralPath $Path) {
            if (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet) { return $true }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    return $false
}
function Stop-SmokeProcess($Process) {
    if ($null -eq $Process) { return }
    $Process.Refresh()
    if ($Process.HasExited) { return }
    [void]$Process.CloseMainWindow()
    $stopBy = (Get-Date).AddSeconds(8)
    do {
        Start-Sleep -Milliseconds 250
        $Process.Refresh()
    } while (-not $Process.HasExited -and (Get-Date) -lt $stopBy)
    if (-not $Process.HasExited) { Stop-Process -Id $Process.Id -Force }
}
try {
    $hostProcess = Start-Process -FilePath $runtime -ArgumentList $hostArguments -WorkingDirectory $PackageDirectory -WindowStyle Hidden -PassThru
    $result.hostListened = Wait-ForLog $hostLog 'IpNetDriver listening on port 7777' 45
    if (-not $result.hostListened) { throw "Listen server failed to bind UDP 7777. Inspect $hostLog" }
    $clientProcess = Start-Process -FilePath $runtime -ArgumentList $clientArguments -WorkingDirectory $PackageDirectory -WindowStyle Hidden -PassThru
    $result.serverObservedJoin = Wait-ForLog $hostLog 'LogNet: Join succeeded:' 35
    $result.clientWelcomed = Wait-ForLog $clientLog 'LogNet: Welcomed by server' 35
    if (-not $result.serverObservedJoin -or -not $result.clientWelcomed) {
        throw "Packaged client did not complete direct-connect login. Inspect $hostLog and $clientLog"
    }
    $result.clientLoadedNetworkMap = Wait-ForLog $clientLog 'LogLoad: LoadMap: 127.0.0.1/Game/Velocity/Maps/L_Dockside' 15
    if (-not $result.clientLoadedNetworkMap) { throw "Client was welcomed but did not load the networked Dockside world. Inspect $clientLog" }
    $result.clientSentDrivingInput = Wait-ForLog $clientLog 'AFTERDARK_NET_DRIVE_STARTED' 12
    if (-not $result.clientSentDrivingInput) { throw "Client network probe did not start driving. Inspect $clientLog" }
    $result.serverSimulatedVehicleMovement = Wait-ForLog $hostLog 'AFTERDARK_NET_MOVEMENT_CONFIRMED' 18
    if (-not $result.serverSimulatedVehicleMovement) { throw "The server did not observe authoritative movement from client input. Inspect $hostLog" }
    $result.clientReceivedReplicatedTelemetry = Wait-ForLog $clientLog 'AFTERDARK_NET_TELEMETRY_CONFIRMED' 8
    if (-not $result.clientReceivedReplicatedTelemetry) { throw "Client did not receive a moving-vehicle telemetry update from the server. Inspect $clientLog" }
    $hostProcess.Refresh()
    if ($hostProcess.HasExited) { throw "Listen server exited while the client was connected. Inspect $hostLog" }
    Stop-SmokeProcess $clientProcess
    if ($clientProcess) {
        $clientProcess.Refresh()
        if ($clientProcess.HasExited) { $result.clientExitCode = $clientProcess.ExitCode }
    }
    $result.serverObservedDisconnect = Wait-ForLog $hostLog 'UNetConnection::Cleanup: Closing open connection' 15
    if (-not $result.serverObservedDisconnect) { throw "Listen server did not observe client disconnect. Inspect $hostLog" }
    $result.passed = $true
} finally {
    Stop-SmokeProcess $clientProcess
    Stop-SmokeProcess $hostProcess
    $result.endedUtc = [DateTime]::UtcNow.ToString('o')
    if ($clientProcess) {
        $clientProcess.Refresh()
        if ($clientProcess.HasExited) { $result.clientExitCode = $clientProcess.ExitCode }
    }
    if ($hostProcess) {
        $hostProcess.Refresh()
        if ($hostProcess.HasExited) { $result.hostExitCode = $hostProcess.ExitCode }
    }
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputDirectory 'report.json') -Encoding utf8
}
if (Select-String -LiteralPath $clientLog -Pattern 'Fatal error:|LogNet: Error:|NetworkFailure|TravelFailure' -Quiet) {
    throw "Client log contains a transport/runtime failure. Inspect $clientLog"
}
Write-Host "AFTERDARK_MULTIPLAYER_SMOKE_OK: packaged host, loopback join, network-map load and server-observed disconnect. Inspect $outputDirectory"

[CmdletBinding()]
param(
    [ValidatePattern('^(?i:AUTO|COM\d+)$')][string]$Port='AUTO',
    [ValidateRange(1024,65535)][int]$UdpPort=9011,
    [Alias('VerticalScale')][ValidateRange(0.1,2.0)][double]$CenterResponse=1.0,
    [ValidatePattern('^(?i:PRIMARY|DISPLAY\d+)$')][string]$Display='DISPLAY2',
    [string]$Python='',
    [string]$TestProject='',
    [string]$Editor='D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
)
$ErrorActionPreference='Stop'
# Keep redirected Python diagnostics readable by Windows PowerShell 5.1 too.
$env:PYTHONUTF8='1'
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not $TestProject) {
    $isolated=Join-Path $projectRoot 'Build/Testing/ControllerAimFire/Project/OperationBreakpoint.uproject'
    $isolatedModule=Join-Path (Split-Path $isolated) 'Binaries/Win64/UnrealEditor-OperationBreakpoint.dll'
    $TestProject=if (Test-Path -LiteralPath $isolatedModule) { $isolated } else { Join-Path $projectRoot 'OperationBreakpoint.uproject' }
}
if (-not $Python) { $Python=Join-Path $projectRoot 'Support/Testing/Packaging/.venv/Scripts/python.exe' }
foreach ($file in @($Editor,$Python,$TestProject,(Join-Path (Split-Path $TestProject) 'Content/Tests/ControllerAim/Maps/L_ControllerAimTest.umap'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required test file missing: $file" }
}
$tag=(Get-Date -Format 'yyyyMMdd_HHmmss')+'_'+[guid]::NewGuid().ToString('N').Substring(0,8)
$logDir=Join-Path $projectRoot "Saved/Tests/ControllerAim/IR/$tag"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$portJson=& $Python (Join-Path $PSScriptRoot 'device_port.py') --port $Port --wait-seconds 5
$portExitCode=$LASTEXITCODE
$portSelection=$portJson | ConvertFrom-Json
$portSelection | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDir 'port-selection.json') -Encoding UTF8
if ($portExitCode -ne 0 -or $portSelection.error) { throw "IR board not ready: $($portSelection.error) See $logDir" }
$Port=[string]$portSelection.port
Write-Output "IR_TEST_PORT=$Port"
$displayJson=& $Python (Join-Path $PSScriptRoot 'display_target.py') --device $Display
if ($LASTEXITCODE -ne 0) { throw "Could not identify display: $Display" }
$displayTarget=$displayJson | ConvertFrom-Json
$displayTarget | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDir 'display-target.json') -Encoding UTF8
$stopFile=Join-Path $logDir 'stop.request'
$bridgeArgs=@('-u',('"'+(Join-Path $PSScriptRoot 'ir_test_bridge.py')+'"'),'--port',$Port,
    '--display',$Display,
    '--udp-port',"$UdpPort",'--log-dir',('"'+$logDir+'"'),'--stop-file',('"'+$stopFile+'"'))
$gameArgs=@(('"'+$TestProject+'"'),
    '/Game/Tests/ControllerAim/Maps/L_ControllerAimTest','-game','-fullscreen','-nosplash','-nop4','-NoHMD','-nosound','-DisablePython',
    '-OBOpenFire',"-OBOpenFirePort=$UdpPort",'-unattended',
    '-ForceRes',"-ResX=$($displayTarget.width)","-ResY=$($displayTarget.height)",
    "-WinX=$($displayTarget.x)","-WinY=$($displayTarget.y)",'-SAVEWINPOS=0',
    ('-ini:GameUserSettings:[/Script/Engine.GameUserSettings]:DisplayID='+$displayTarget.device_id),
    '-ini:GameUserSettings:[/Script/Engine.GameUserSettings]:DisplayIndex=-1',
    ('-ini:GameUserSettings:[/Script/Engine.GameUserSettings]:LastUserConfirmedDisplayID='+$displayTarget.device_id),
    '-ini:GameUserSettings:[/Script/Engine.GameUserSettings]:LastUserConfirmedDisplayIndex=-1',
    ('-ControllerAimIRCenterResponse='+$CenterResponse.ToString([Globalization.CultureInfo]::InvariantCulture)),
    '-ini:Engine:[/Script/EngineSettings.GameMapsSettings]:GameInstanceClass=/Script/Engine.GameInstance',
    ('-AbsLog="'+(Join-Path $logDir 'unreal.log')+'"'))
function Get-IRBridgeFailure {
    param([string]$Folder)
    $statusFile=Join-Path $Folder 'bridge-status.json'
    if (Test-Path -LiteralPath $statusFile) {
        try {
            $result=Get-Content -LiteralPath $statusFile -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($result.error) { return [string]$result.error }
        } catch { }
    }
    $stderrFile=Join-Path $Folder 'bridge.stderr.log'
    if (Test-Path -LiteralPath $stderrFile) {
        $lastError=Get-Content -LiteralPath $stderrFile -Encoding UTF8 -Tail 12 | Where-Object { $_.Trim() } | Select-Object -Last 1
        if ($lastError) { return [string]$lastError }
    }
    return $null
}
$bridge=$null
try {
    $bridge=Start-Process -FilePath $Python -ArgumentList $bridgeArgs -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logDir 'bridge.stdout.log') -RedirectStandardError (Join-Path $logDir 'bridge.stderr.log')
    $ready=Join-Path $logDir 'bridge-status.json'
    $deadline=(Get-Date).AddSeconds(5)
    $identityReady=$false
    while (-not $bridge.HasExited -and (Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $ready) {
            $status=Get-Content -LiteralPath $ready -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($status.error) { throw "IR bridge failed: $($status.error). See $logDir" }
            if ($status.identity) { $identityReady=$true; break }
        }
        Start-Sleep -Milliseconds 100
    }
    # The worker can exit before the while loop reads its final status file.
    # Always read it again; a camera error is different from a busy COM port.
    $failure=Get-IRBridgeFailure $logDir
    if ($failure) { throw "IR bridge failed: $failure. See $logDir" }
    if ($bridge.HasExited) { throw "IR bridge exited before initialization. See $logDir" }
    if (-not $identityReady) { throw "IR board did not initialize. See $logDir" }
    # Visible interactive test explicitly requested by the user.
    $game=Start-Process -FilePath $Editor -ArgumentList $gameArgs -WindowStyle Normal -PassThru
    Write-Output "IR_TEST_LOGS=$logDir"
    Write-Output "IR_TEST_PROJECT=$TestProject"
    Write-Output "IR_TEST_DISPLAY=$($displayTarget.device) primary=$($displayTarget.primary) $($displayTarget.width)x$($displayTarget.height) at $($displayTarget.x),$($displayTarget.y)"
    Write-Output "Aim with four LEDs visible; trigger or Space leaves a wall mark. Esc closes this test."
    $game.WaitForExit()
    if ($game.ExitCode -ne 0) { throw "IR test game exited with $($game.ExitCode). See $logDir" }
}
finally {
    if ($bridge -and -not $bridge.HasExited) {
        New-Item -ItemType File -Path $stopFile -Force | Out-Null
        if (-not $bridge.WaitForExit(4000)) { Stop-Process -Id $bridge.Id -ErrorAction SilentlyContinue }
    }
}

[CmdletBinding()]
param([string]$PackageDirectory='', [string]$Python='')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not $PackageDirectory) { $PackageDirectory=Join-Path $root 'Build/Submission/Windows' }
if (-not $Python) { $Python=Join-Path $root 'Support/Testing/Packaging/.venv/Scripts/python.exe' }
if (-not (Test-Path -LiteralPath (Join-Path $PackageDirectory 'OperationBreakpoint.exe'))) { throw 'Build the game package first.' }
$work=Join-Path $root 'Intermediate/Packaging/OBLauncher'
$output=Join-Path $root 'Support/Dist/Submission/OpenFire/Launcher'
$verify=Join-Path $root 'Saved/Tests/Packaging/Final'
New-Item -ItemType Directory -Force -Path $work,$output,$verify | Out-Null
& $Python -X utf8 -m unittest discover -s (Join-Path $PSScriptRoot 'tests') -q *> (Join-Path $verify 'launcher-tests.log')
if ($LASTEXITCODE -ne 0) { throw "IR launcher tests failed: $verify/launcher-tests.log" }
$icon=Join-Path $root 'Support/Testing/Packaging/Assets/OperationBreakpointLauncher.ico'
& $Python -m PyInstaller --noconfirm --onefile --windowed --noupx --name OB --icon $icon `
    --paths $PSScriptRoot --paths (Join-Path $root 'Support/Submission/DeviceSettings') --paths (Join-Path $root 'Support/Submission/MediaPipeBridge') --paths (Join-Path $root 'Support/Testing/Packaging') `
    --distpath $output --workpath $work --specpath $work (Join-Path $PSScriptRoot 'package_launcher.py') `
    *> (Join-Path $verify 'launcher-build.log')
if ($LASTEXITCODE -ne 0) { throw "OB launcher build failed: $verify/launcher-build.log" }
Copy-Item -LiteralPath (Join-Path $output 'OB.exe') -Destination (Join-Path $PackageDirectory 'OB.exe') -Force
$motion=Join-Path $PackageDirectory 'MediaPipe'
New-Item -ItemType Directory -Force -Path $motion | Out-Null
$motionSource=Join-Path $root 'Support/Dist/Submission/MediaPipe/MediaPipe.exe'
$motionTarget=Join-Path $motion 'MediaPipe.exe'
if (-not (Test-Path -LiteralPath $motionTarget) -or
    (Get-FileHash -LiteralPath $motionSource).Hash -ne (Get-FileHash -LiteralPath $motionTarget).Hash) {
    Copy-Item -LiteralPath $motionSource -Destination $motionTarget -Force
}
if (-not (Test-Path -LiteralPath (Join-Path $motion 'config.json'))) {
    Copy-Item -LiteralPath (Join-Path $root 'Support/Dist/Submission/MediaPipe/config.json') -Destination $motion
}
$offsetFolder=Join-Path $PackageDirectory 'Saved/ControllerInput/OpenFire'
New-Item -ItemType Directory -Force -Path $offsetFolder | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $offsetFolder 'aim-adjustment.json'))) {
    $offsetSource=Join-Path $root 'Saved/ControllerInput/OpenFire/aim-adjustment.json'
    if (-not (Test-Path -LiteralPath $offsetSource)) {
        $offsetSource=Join-Path $root 'Build/Submission/Windows/Saved/ControllerInput/OpenFire/aim-adjustment.json'
    }
    if (-not (Test-Path -LiteralPath $offsetSource)) { throw 'No saved OpenFire aim adjustment is available for this package.' }
    Copy-Item -LiteralPath $offsetSource -Destination $offsetFolder
}
$config=Join-Path $PackageDirectory 'OB.json'
if (-not (Test-Path -LiteralPath $config)) {
    @{display='DISPLAY1';port='AUTO';udp_port=9011;mediapipe=$true} | ConvertTo-Json | Set-Content -LiteralPath $config -Encoding UTF8
}
$smoke=Join-Path $verify 'launcher-ui.json'
$process=Start-Process -FilePath (Join-Path $PackageDirectory 'OB.exe') `
    -ArgumentList @('--smoke-test',('"'+$smoke+'"')) -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw 'OB launcher GUI smoke failed.' }
$gui=Get-Content -LiteralPath $smoke -Raw | ConvertFrom-Json
if (-not $gui.selectors.port -or -not $gui.selectors.camera -or $gui.required[0] -gt $gui.size[0] -or $gui.required[1] -gt $gui.size[1]) { throw 'OB launcher device selectors are missing or the GUI layout is clipped.' }
Write-Output "Built IR package launcher: $PackageDirectory/OB.exe"

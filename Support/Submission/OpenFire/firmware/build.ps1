[CmdletBinding()]
param([string]$ArduinoCLI = '', [string]$Python = '')
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$research = Join-Path $project 'Build/Testing/OpenFireResearch'
$source = Join-Path $research 'Firmware-obir'
$output = Join-Path $project 'Build/Testing/OpenFireFirmware/obir3'
$distribution = Join-Path $project 'Support/Dist/Submission/OpenFire/Firmware'
$base = 'ca30987ab07dfdf980f258683648469a023de0b0'
if (-not $Python) { $Python = Join-Path $project 'Support/Testing/Packaging/.venv/Scripts/python.exe' }
if (-not $ArduinoCLI) { $ArduinoCLI = Join-Path $env:LOCALAPPDATA 'Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' }
if (-not (Test-Path -LiteralPath $ArduinoCLI)) { throw 'Supply -ArduinoCLI with arduino-cli.exe.' }
New-Item -ItemType Directory -Force -Path $research,$output,$distribution | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $source '.git'))) {
    & git clone --no-checkout https://github.com/TeamOpenFIRE/OpenFIRE-Firmware $source
    if ($LASTEXITCODE -ne 0) { throw 'Firmware clone failed.' }
    & git -C $source checkout --detach $base
    if ($LASTEXITCODE -ne 0) { throw 'Pinned firmware checkout failed.' }
}
& git -C $source submodule update --init --depth 1
if ($LASTEXITCODE -ne 0) { throw 'Firmware dependencies failed.' }
$config = Join-Path $research 'arduino-cli.yaml'
@{ directories = @{ data = (Join-Path $research 'arduino-data'); downloads = (Join-Path $research 'arduino-downloads'); user = (Join-Path $research 'arduino-user') } } |
    ConvertTo-Json | Set-Content -LiteralPath $config -Encoding utf8
if (-not (Test-Path -LiteralPath (Join-Path $research 'arduino-data/packages/rp2040/hardware/rp2040/4.5.4-tusb'))) {
    & $ArduinoCLI --config-file $config core install rp2040:rp2040@4.5.4-tusb --additional-urls https://github.com/TeamOpenFIRE/arduino-pico/releases/download/global/package_rp2040_index.json
    if ($LASTEXITCODE -ne 0) { throw 'Pinned RP2040 core install failed.' }
}
& $Python (Join-Path $PSScriptRoot 'patch_firmware.py') $source
if ($LASTEXITCODE -ne 0) { throw 'Firmware patch failed.' }
& $ArduinoCLI --config-file $config compile --fqbn rp2040:rp2040:rpipico:freq=133,usbstack=tinyusb,flash=2097152_65536,opt=Optimize3 `
    (Join-Path $source 'OpenFIREmain') --libraries (Join-Path $source 'libraries') --output-dir $output *> (Join-Path $research 'firmware-build.log')
if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed; see Build/Testing/OpenFireResearch/firmware-build.log.' }
& $Python (Join-Path $PSScriptRoot 'package_firmware.py') $source $output $distribution
if ($LASTEXITCODE -ne 0) { throw 'Firmware packaging failed.' }
Write-Output "Built diagnostic extension (not flashed): $distribution"

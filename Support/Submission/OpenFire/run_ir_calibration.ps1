[CmdletBinding()]
param(
    [ValidatePattern('^(?i:AUTO|COM\d+)$')][string]$Port='AUTO',
    [ValidatePattern('^(?i:PRIMARY|DISPLAY\d+)$')][string]$Display='DISPLAY2',
    [string]$Python=''
)
$ErrorActionPreference='Stop'
$env:PYTHONUTF8='1'
$projectRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not $Python) { $Python=Join-Path $projectRoot 'Support/Testing/Packaging/.venv/Scripts/python.exe' }
$tag=(Get-Date -Format 'yyyyMMdd_HHmmss')+'_'+[guid]::NewGuid().ToString('N').Substring(0,8)
$output=Join-Path $projectRoot "Saved/Tests/ControllerAim/IR/recalibration_$tag"
Write-Output "IR_CALIBRATION_LOGS=$output"
& $Python -X utf8 (Join-Path $PSScriptRoot 'firmware/recalibrate.py') --port $Port --display $Display --output $output
if ($LASTEXITCODE -ne 0) { throw "IR calibration failed. See $output" }
$progress=Join-Path $output 'calibration-progress.json'
if (Test-Path -LiteralPath $progress) {
    $state=Get-Content -LiteralPath $progress -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Output "IR_CALIBRATION_SAVED=$($state.result.saved) VERIFIED=$($state.result.validated)"
}

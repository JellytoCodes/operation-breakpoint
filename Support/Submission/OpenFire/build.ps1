[CmdletBinding()]
param([string]$Python = '', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not $Python) { $Python = Join-Path $projectRoot 'Support/Testing/Packaging/.venv/Scripts/python.exe' }
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) { throw "Python environment not found: $Python" }
$output = if ($OutputDirectory) { [System.IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $projectRoot 'Support/Dist/Submission/OpenFire' }
if (-not $output.StartsWith($projectRoot + '\',[System.StringComparison]::OrdinalIgnoreCase)) { throw 'Build output must remain within the project directory.' }
$work = Join-Path $projectRoot 'Intermediate/Packaging/OpenFireTool'
$verification = Join-Path $projectRoot 'Saved/Tests/Packaging/OpenFireTool'
New-Item -ItemType Directory -Force -Path $output,$work,$verification | Out-Null
& $Python -m unittest discover -s (Join-Path $PSScriptRoot 'tests') -v
if ($LASTEXITCODE -ne 0) { throw 'OpenFire tool tests failed.' }
& $Python -m PyInstaller --noconfirm --onefile --windowed --noupx --name OBIRController `
    --paths $PSScriptRoot --paths (Join-Path $projectRoot 'Support/Submission/DeviceSettings') --distpath $output --workpath $work --specpath $work `
    (Join-Path $PSScriptRoot 'app.py') *> (Join-Path $verification 'tool-build.log')
if ($LASTEXITCODE -ne 0) { throw "Tool build failed. See $verification/tool-build.log." }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $output '사용법.md') -Force
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'Support/Dist/Archive/OpenFire') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'Support/Archive/OpenFire/RESEARCH.md') -Destination (Join-Path $projectRoot 'Support/Dist/Archive/OpenFire/RESEARCH.md') -Force
$exe = Join-Path $output 'OBIRController.exe'
$smokeFile = Join-Path $verification 'frozen-ui-smoke.json'
$settings = Join-Path $verification 'frozen-smoke-settings.json'
$process = Start-Process -FilePath $exe -ArgumentList @('--settings',('"'+$settings+'"'),'--smoke-test',('"'+$smokeFile+'"')) -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Frozen UI smoke test failed with exit code $($process.ExitCode)." }
$result = Get-Content -LiteralPath $smokeFile -Raw | ConvertFrom-Json
if (-not $result.raw_window -or $result.raw_error -or $result.sending) { throw 'Frozen tool input initialization failed.' }
if ($result.camera_points -ne 6 -or $result.camera_edges -ne 4 -or $result.stale_camera_points -ne 0) { throw 'Frozen IR camera scene validation failed.' }
if ($result.detected_points -ne 4 -or $result.led_indicators -ne 4 -or $result.stale_detected -ne 0) { throw 'Frozen measured LED view validation failed.' }
if (-not $result.diagnostic_metrics_checked -or $result.callback_errors.Count -gt 0) { throw 'Frozen diagnostic update loop failed.' }
if ($result.guide_targets -ne 4) { throw 'Frozen Square installation guide failed.' }
if ($result.live_aim_points -ne 1 -or $result.aim_points -ne 1 -or $result.stale_aim_points -ne 0) { throw 'Frozen live aim view failed.' }
if ($result.offscreen_arrows -ne 1 -or $result.false_inside_aim -ne 0) { throw 'Frozen off-screen direction view failed.' }
Get-FileHash -LiteralPath $exe -Algorithm SHA256 | Format-List
Write-Output "Built and opened successfully: $exe"

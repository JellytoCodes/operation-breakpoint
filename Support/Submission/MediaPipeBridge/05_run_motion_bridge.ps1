$ErrorActionPreference = "Stop"
$Python = Join-Path $PSScriptRoot ".venv\Scripts\python.exe"
$Bridge = Join-Path $PSScriptRoot "mediapipe_motion_bridge.py"
& $Python $Bridge

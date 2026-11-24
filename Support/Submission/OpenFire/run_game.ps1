[CmdletBinding()]
param(
    [ValidateSet('Training','Stage01')][string]$Map = 'Training',
    [switch]$Demo,
    [ValidateRange(1024,65535)][int]$Port = 9011,
    [string]$Editor = 'D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if (-not (Test-Path -LiteralPath $Editor -PathType Leaf)) { throw "Editor not found: $Editor" }
$level = if ($Map -eq 'Training') { '/Game/Maps/TutorialMap' } else { '/Game/Maps/Stage_01' }
$contentMap = Join-Path $projectRoot ($level.Replace('/Game/','Content/') + '.umap')
if (-not (Test-Path -LiteralPath $contentMap -PathType Leaf)) { throw "Map not found: $contentMap" }
$arguments = @(('"'+(Join-Path $projectRoot 'OperationBreakpoint.uproject')+'"'),$level,'-game','-fullscreen',
    '-OBOpenFire',"-OBOpenFirePort=$Port",'-nosplash','-nop4')
if ($Demo) { $arguments += '-OBOpenFireDemo' }
# This script intentionally starts the interactive game selected by the user.
Start-Process -FilePath $Editor -ArgumentList $arguments -WindowStyle Normal

param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$toolDirectory = $PSScriptRoot
$projectDirectory = (Resolve-Path (Join-Path $toolDirectory '../../..')).Path
$venvPython = Join-Path $toolDirectory '.venv/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $venvPython)) {
    & $Python -m venv (Join-Path $toolDirectory '.venv')
    if ($LASTEXITCODE -ne 0) { throw 'Unable to create report-generator venv' }
}
& $venvPython -m pip install -r (Join-Path $toolDirectory 'requirements.txt')
if ($LASTEXITCODE -ne 0) { throw 'Unable to install report-generator dependencies' }
$fontFile = Join-Path $toolDirectory 'fonts/TrainingKorean.ttf'
$fontArguments = @()
if (Test-Path -LiteralPath $fontFile) { $fontArguments = @('--add-data', "$fontFile;fonts") }
& $venvPython -m PyInstaller --noconfirm --clean --onefile --name TrainingReportGenerator --distpath (Join-Path $projectDirectory 'Binaries/Win64') --workpath (Join-Path $toolDirectory 'build') --specpath (Join-Path $toolDirectory 'build') @fontArguments (Join-Path $toolDirectory 'training_report.py')
if ($LASTEXITCODE -ne 0) { throw 'Unable to package PDF generator' }

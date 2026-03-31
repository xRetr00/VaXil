$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$runtimeRoot = Join-Path $repoRoot "python_runtime"
$venvPath = Join-Path $runtimeRoot ".venv"
$requirementsPath = Join-Path $runtimeRoot "requirements.lock"
$playwrightBrowsersPath = Join-Path $runtimeRoot "ms-playwright"

if (!(Test-Path $requirementsPath)) {
    $requirementsPath = Join-Path $runtimeRoot "requirements.txt"
}
if (!(Test-Path $requirementsPath)) {
    throw "Missing requirements file at $requirementsPath"
}

$pythonExe = $env:VAXIL_PYTHON_EXECUTABLE
if ([string]::IsNullOrWhiteSpace($pythonExe)) {
    $pythonExe = (Get-Command python -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
}
if ([string]::IsNullOrWhiteSpace($pythonExe)) {
    throw "Python was not found. Set VAXIL_PYTHON_EXECUTABLE or install Python first."
}

if (!(Test-Path $venvPath)) {
    & $pythonExe -m venv $venvPath
}

$venvPython = Join-Path $venvPath "Scripts\python.exe"
if (!(Test-Path $venvPython)) {
    throw "Virtualenv python not found at $venvPython"
}

& $venvPython -m pip install --upgrade pip
& $venvPython -m pip install -r $requirementsPath
$env:PLAYWRIGHT_BROWSERS_PATH = $playwrightBrowsersPath
& $venvPython -m playwright install chromium

Write-Host "Python runtime bootstrapped at $venvPath"

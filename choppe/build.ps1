param(
    [string]$Environment = "esplolin_c3_mini32dev",
    [switch]$Clean,
    [switch]$Upload,
    [switch]$ListEnvironments,
    [switch]$Monitor,
    [string]$Port
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$coreDir = Join-Path $projectRoot ".platformio-core"
$availableEnvironments = @(
    "esplolin_c3_mini32dev",
    "esp32-s3-devkitc-1",
    "esp32dev"
)

New-Item -ItemType Directory -Force -Path $coreDir | Out-Null

$env:PLATFORMIO_CORE_DIR = $coreDir
$env:PLATFORMIO_HOME_DIR = $coreDir

if ($ListEnvironments) {
    Write-Host "Ambientes disponiveis:"
    $availableEnvironments | ForEach-Object { Write-Host " - $_" }
    exit 0
}

if ($Environment -notin $availableEnvironments) {
    Write-Error "Ambiente invalido: $Environment. Use -ListEnvironments para ver as opcoes."
    exit 1
}

$platformioArgs = @()

if ($Monitor) {
    $platformioArgs += @("device", "monitor", "-e", $Environment)
} else {
    $platformioArgs += @("run", "-e", $Environment)
}

if ($Clean) {
    $platformioArgs += @("-t", "clean")
}

if ($Upload) {
    $platformioArgs += @("-t", "upload")
}

if ($Port) {
    if ($Monitor) {
        $platformioArgs += @("--port", $Port)
    } else {
        $platformioArgs += @("--upload-port", $Port)
    }
}

Write-Host "PlatformIO core local:" $coreDir
Write-Host "Ambiente:" $Environment

& python -m platformio @platformioArgs
exit $LASTEXITCODE

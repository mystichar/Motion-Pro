param(
    [switch]$Monitor,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host ""
Write-Host "Upload needs exclusive access to COM3."
Write-Host "If a serial monitor is open (PlatformIO, Cursor Serial, PuTTY), stop it first: Ctrl+C in that terminal."
Write-Host ""

$args = @("run", "-t", "upload")
if ($NoBuild) {
    $args = @("run", "-t", "nobuild", "-t", "upload")
}

python -m platformio @args
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Upload failed. Close the serial monitor on COM3 and run this script again."
    exit $LASTEXITCODE
}

if ($Monitor) {
    Write-Host ""
    Write-Host "Starting serial monitor at 115200 (Ctrl+C to exit)..."
    python -m platformio device monitor
}

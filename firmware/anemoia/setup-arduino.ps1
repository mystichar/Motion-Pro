# Copy Motion Pro TFT_eSPI User_Setup.h into the Arduino libraries folder.
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$source = Join-Path $repoRoot "third_party\anemoia-esp32\User_Setups\Motion-Pro-ESP32-S3\User_Setup.h"
$destDir = Join-Path $env:USERPROFILE "Documents\Arduino\libraries\TFT_eSPI"
$dest = Join-Path $destDir "User_Setup.h"

if (-not (Test-Path $source)) {
    Write-Error "Source not found: $source`nRun: git submodule update --init third_party/anemoia-esp32"
}

if (-not (Test-Path $destDir)) {
    Write-Error "TFT_eSPI library not found at $destDir`nInstall TFT_eSPI from Arduino Library Manager first."
}

Copy-Item -Path $source -Destination $dest -Force
Write-Host "Copied Motion Pro User_Setup.h -> $dest"
Write-Host "Open third_party/anemoia-esp32/Anemoia-ESP32.ino in Arduino IDE and select ESP32-S3 board."

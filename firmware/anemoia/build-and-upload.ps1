# Build and upload Motion Pro Anemoia-ESP32 (ESP32-S3 + 280x240 ST7789)
$ErrorActionPreference = "Stop"

$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
            [System.Environment]::GetEnvironmentVariable("Path", "User")

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sketch = Join-Path $PSScriptRoot "Anemoia-ESP32"
$anemoia = Join-Path $repoRoot "third_party\anemoia-esp32"
$buildOut = Join-Path $sketch "build\motion-pro"
$port = $env:MOTION_PRO_PORT
if (-not $port) { $port = "COM3" }

if (-not (Test-Path $sketch)) {
    cmd /c mklink /J "$sketch" "$anemoia" | Out-Null
}

$pkg = Join-Path $env:LOCALAPPDATA "Arduino15\packages\esp32\hardware\esp32\3.2.1"
Copy-Item (Join-Path $anemoia "platform.txt") (Join-Path $pkg "platform.txt") -Force
Copy-Item (Join-Path $anemoia "User_Setups\Motion-Pro-ESP32-S3\User_Setup.h") `
    (Join-Path $env:USERPROFILE "Documents\Arduino\libraries\TFT_eSPI\User_Setup.h") -Force

& (Join-Path $PSScriptRoot "embed-rom.ps1")

$flags = "-DOPTIMIZATION_FLAGS -Ofast -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti " +
         "-funroll-loops -fno-tree-vectorize -frename-registers -fno-plt -flto -DSCREEN_SWAP_BYTES"
$fqbn = "esp32:esp32:esp32s3:PartitionScheme=custom,FlashSize=4M,CDCOnBoot=cdc,USBMode=hwcdc,UploadSpeed=921600,EventsCore=0"

Write-Host "Building Anemoia-ESP32 for Motion Pro..."
arduino-cli compile -b $fqbn `
    --build-property "compiler.cpp.extra_flags=$flags" `
    --build-property "compiler.c.extra_flags=$flags" `
    --output-dir $buildOut $sketch

Write-Host "Uploading to $port (close Serial Monitor / other COM users first)..."
arduino-cli upload -b $fqbn -p $port --input-dir $buildOut $sketch

Write-Host "Done. Open WebSerialController.html and connect to $port for controls."

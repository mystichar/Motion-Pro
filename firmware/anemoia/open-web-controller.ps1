# Serve WebSerialController.html on localhost (required — file:// blocks Web Serial in Chrome/Edge).
$ErrorActionPreference = "Stop"

$dir = $PSScriptRoot
$html = Join-Path $dir "WebSerialController.html"
$url = "https://raw.githubusercontent.com/jethomson/SerialGameControllerAdapter/main/WebSerialController.html"
$port = 8765

if (-not (Test-Path $html)) {
    Write-Host "Downloading WebSerialController.html..."
    Invoke-WebRequest -Uri $url -OutFile $html -UseBasicParsing
}

Write-Host ""
Write-Host "Motion Pro — browser NES controller" -ForegroundColor Cyan
Write-Host "  1. Keep the ESP32 plugged in via USB"
Write-Host "  2. Close Arduino Serial Monitor / any tool using the COM port"
Write-Host "  3. Open this URL in Chrome or Edge (not Firefox):"
Write-Host "     http://localhost:$port/WebSerialController.html" -ForegroundColor Green
Write-Host "  4. Click 'Connect Serial' and pick your ESP32 (e.g. COM3)"
Write-Host "  5. Keys: WASD move, K=A, J=B, Enter=Start, Right Shift=Select"
Write-Host ""
Write-Host "Press Ctrl+C to stop the server."
Write-Host ""

Start-Process "http://localhost:$port/WebSerialController.html"
Set-Location $dir
python -m http.server $port

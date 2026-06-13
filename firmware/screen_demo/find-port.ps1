# Scan for ESP32 USB/serial devices — run while the board is plugged in.
Write-Host "ESP32 port / driver check" -ForegroundColor Cyan
Write-Host ""

$comPorts = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue
if ($comPorts) {
    Write-Host "COM ports:" -ForegroundColor Green
    $comPorts | ForEach-Object {
        Write-Host ("  {0,-8} {1}" -f $_.DeviceID, $_.Name)
    }
} else {
    Write-Host "COM ports: none found" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Espressif / JTAG / UART USB devices:" -ForegroundColor Cyan
$espDevices = Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.Caption -match 'JTAG|Espressif|CP210|CH340|CH910|FTDI|USB Serial|USB-Enhanced-SERIAL'
}
if ($espDevices) {
    $espDevices | ForEach-Object {
        $color = if ($_.Status -eq 'OK') { 'Green' } else { 'Red' }
        Write-Host ("  [{0}] {1}" -f $_.Status, $_.Caption) -ForegroundColor $color
    }
} else {
    Write-Host "  none found" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Unknown USB devices (often = missing driver):" -ForegroundColor Cyan
$unknown = Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.Caption -match 'Unknown|Other device' -or
    ($_.ConfigManagerErrorCode -ne $null -and $_.ConfigManagerErrorCode -ne 0)
} | Where-Object {
    $_.PNPDeviceID -match 'USB\\VID_'
}
if ($unknown) {
    $unknown | ForEach-Object {
        Write-Host ("  {0} (error code {1})" -f $_.Caption, $_.ConfigManagerErrorCode) -ForegroundColor Red
        Write-Host ("    {0}" -f $_.PNPDeviceID)
    }
} else {
    Write-Host "  none" -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "PlatformIO:" -ForegroundColor Cyan
python -m platformio device list

Write-Host ""
if (-not $comPorts -and -not $espDevices) {
    Write-Host "Your PC does not see the ESP32 over USB yet." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Try in order:"
    Write-Host "  1. Different USB cable (must support DATA, not charge-only)"
    Write-Host "  2. Direct laptop USB port (avoid hubs/docks if possible)"
    Write-Host "  3. Hold B (Boot), tap R (Reset), release both"
    Write-Host "  4. Open Device Manager (Win+X) while board is plugged in"
    Write-Host "     - Ports (COM & LPT) -> look for COM number"
    Write-Host "     - Universal Serial Bus devices -> 'USB JTAG/serial debug unit'"
    Write-Host "     - Other devices -> yellow 'Unknown device' = driver needed"
    Write-Host ""
    Write-Host "Driver fix for ESP32-S3 native USB (if you see Unknown device):"
    Write-Host "  https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/windows-setup.html"
    Write-Host "  Or install 'Espressif-IDE' / 'CP210x' driver if your board uses a UART chip."
    Write-Host ""
    Write-Host "Once a COM port appears, set firmware/screen_demo/platformio.local.ini:"
    Write-Host "  upload_port = COMx"
    Write-Host "  monitor_port = COMx"
} elseif ($comPorts) {
    $first = ($comPorts | Select-Object -First 1).DeviceID
    Write-Host "Use this in platformio.local.ini:" -ForegroundColor Green
    Write-Host "  upload_port = $first"
    Write-Host "  monitor_port = $first"
}

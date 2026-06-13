# Convert firmware/anemoia/rom/game.nes into third_party/anemoia-esp32/embedded_rom.h
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$romPath = Join-Path $PSScriptRoot "rom\game.nes"
$outPath = Join-Path $repoRoot "third_party\anemoia-esp32\embedded_rom.h"

if (-not (Test-Path $romPath)) {
    Write-Host "No ROM at $romPath - building without embedded game."
    Set-Content -Path $outPath -Encoding ASCII -Value @(
        "#pragma once",
        "",
        "#define MOTION_PRO_HAVE_EMBEDDED_ROM 0"
    )
    exit 0
}

$bytes = [System.IO.File]::ReadAllBytes($romPath)
Write-Host ("Embedding {0} bytes from {1}" -f $bytes.Length, $romPath)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#pragma once")
$lines.Add("")
$lines.Add("// Auto-generated from firmware/anemoia/rom/game.nes")
$lines.Add("#define MOTION_PRO_HAVE_EMBEDDED_ROM 1")
$lines.Add("")
$lines.Add("#include <cstddef>")
$lines.Add("#include <cstdint>")
$lines.Add("")
$lines.Add("alignas(4) const uint8_t kEmbeddedRom[] = {")

$row = "  "
for ($i = 0; $i -lt $bytes.Length; $i++) {
    $row += ("0x{0:X2}," -f $bytes[$i])
    if (($i + 1) % 16 -eq 0) {
        $lines.Add($row)
        $row = "  "
    }
}
if ($row.Trim().Length -gt 0) {
    $lines.Add($row.TrimEnd(","))
}

$lines.Add("};")
$lines.Add("")
$lines.Add("const size_t kEmbeddedRomSize = sizeof(kEmbeddedRom);")

[System.IO.File]::WriteAllLines($outPath, $lines)
Write-Host ("Wrote {0}" -f $outPath)

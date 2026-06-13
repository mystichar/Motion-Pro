# Embedded test ROM (no SD card)

Drop **one** legally owned `.nes` file here as `game.nes`, then rebuild:

```powershell
cd firmware\anemoia
.\build-and-upload.ps1
```

The build script converts it into `third_party/anemoia-esp32/embedded_rom.h` and flashes it with the firmware. A typical NROM game (Mapper 0) is ~24–48 KB — well within the ESP32-S3 4 MB flash budget (~1.1 MB firmware + ~1.1 MB `nesrom` cache partition).

**Supported mappers in Anemoia:** 0, 1, 2, 3, 4, 69. Super Mario Bros (Mapper 0) works well for a first test.

Save states still require SD and are disabled in embedded mode.

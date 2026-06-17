# Motion-Pro
An upgrade intended to replace the Nintendo Wii Motion Plus 

## Firmware

| Path | Purpose |
|------|---------|
| [`firmware/screen_demo/`](firmware/screen_demo/) | LCD pin-validation demo (ST7789 bring-up) |
| [`firmware/imu_demo/`](firmware/imu_demo/) | Live ISM330DHCX accel/gyro readout on the LCD |
| [`firmware/anemoia/`](firmware/anemoia/) | [Anemoia-ESP32](https://github.com/Shim06/Anemoia-ESP32) NES emulator — submodule + build notes |
| [`third_party/anemoia-esp32/`](third_party/anemoia-esp32/) | Forked Anemoia source (`motion-pro` branch) |

Hardware pin plan: [`motion_pro_pinout.md`](motion_pro_pinout.md) · [`wii_motion_pro_prototype_wiring.md`](wii_motion_pro_prototype_wiring.md)

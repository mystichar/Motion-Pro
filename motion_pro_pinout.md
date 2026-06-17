# Motion Pro — GPIO pinout



Quick reference for the **Waveshare ESP32-S3 Mini** + **1.69" ST7789V2 (240×280)** + **SparkFun Micro ISM330DHCX (Qwiic)** bench wiring.



Full bring-up notes: [`wii_motion_pro_prototype_wiring.md`](wii_motion_pro_prototype_wiring.md)



---



## LCD (SPI — ST7789V2)



| LCD module | ESP32 GPIO | Signal |

|------------|------------|--------|

| DIN | **GPIO 7** | MOSI |

| CLK | **GPIO 6** | SCK |

| CS | **GPIO 5** | Chip select |

| DC | **GPIO 4** | Data / command |

| RST | **GPIO 8** | Reset |

| BL | **GPIO 9** | Backlight (PWM) |

| VCC | 3.3 V | |

| GND | GND | |



Firmware constants: [`firmware/screen_demo/include/display_pins.h`](firmware/screen_demo/include/display_pins.h)



---



## IMU (I2C — SparkFun ISM330DHCX Qwiic)



Dedicated internal I2C bus. **Do not** share with the Wii extension connector.



| Qwiic wire | ESP32 GPIO | Signal |

|------------|------------|--------|

| **Blue** | **GPIO 11** | **SDA** |

| **Yellow** | **GPIO 10** | **SCL** |

| Red | 3.3 V | Power (1.71–3.6 V) |

| Black | GND | Ground |



```cpp

Wire.begin(11, 10);  // SDA, SCL

```



Firmware constants: [`firmware/imu_demo/include/imu_pins.h`](firmware/imu_demo/include/imu_pins.h)



---



## Wii extension harness (reserved — not used in firmware yet)



Separate `Wire1` bus from the IMU. Left header pads.



| Harness wire | ESP32 GPIO | Signal |

|--------------|------------|--------|

| Gray | **GPIO 1** | SDA (`WII_SDA`) |

| White | **GPIO 2** | SCL_S (`WII_SCL`) |

| Black | **GPIO 3** | TRIG_S / Sense (`WII_SENSE`) |

| Red | GND | |

| Blue | +3.3 V ref | Do not use as main supply |



Add **100–330 Ω** series resistors on SDA/SCL; **1–10 kΩ** on Sense while experimenting.



```cpp

// Future — not called in current builds:

Wire1.begin(WII_SDA, WII_SCL);

pinMode(WII_SENSE, INPUT_PULLUP);

```



Firmware constants: [`firmware/imu_demo/include/wii_pins.h`](firmware/imu_demo/include/wii_pins.h)



**Note:** GPIO 2/3 overlap the Anemoia microSD map (SCLK/CS). Use embedded ROM or remap SD if both Wii harness and SD card are needed.



---



## User button



| Signal | ESP32 GPIO | Wiring |

|--------|------------|--------|

| Screen select | **GPIO 12** | Momentary switch: GPIO 12 ↔ GND, `INPUT_PULLUP` (active LOW) |



Toggles between the IMU dashboard and a 3D prism view in `imu_demo`. Right header pad **12**. Conflicts with Anemoia microSD MOSI.



Firmware constants: [`firmware/imu_demo/include/button_pins.h`](firmware/imu_demo/include/button_pins.h)



---



## Optional microSD (FSPI — Anemoia builds)



| SD signal | ESP32 GPIO |

|-----------|------------|

| MOSI | GPIO 12 ⚠ conflicts with `BTN_GPIO` |

| MISO | GPIO 13 |

| SCLK | GPIO 2 ⚠ conflicts with `WII_SCL` |

| CS | GPIO 3 ⚠ conflicts with `WII_SENSE` |



Skipped when using embedded ROM (`MOTION_PRO_SKIP_SD`).



---



## GPIO summary (header 0–13 + button)



| GPIO | Assignment |

|------|------------|

| 1 | Wii SDA (reserved) |

| 2 | Wii SCL (reserved) |

| 3 | Wii Sense (reserved) |

| 4 | LCD DC |

| 5 | LCD CS |

| 6 | LCD SCK |

| 7 | LCD MOSI |

| 8 | LCD RST |

| 9 | LCD BL |

| 10 | IMU SCL (Qwiic yellow) |

| 11 | IMU SDA (Qwiic blue) |

| 12 | **Screen-select button** |

| 13 | microSD MISO (optional) |



---



## Bottom pads (reference)



| GPIO | Default function | Motion Pro use |

|------|------------------|----------------|

| 17 | `U1TXD`, ADC2 | Free |

| 18 | `U1RXD`, ADC2 | Free |

| 38–40 | FSPI / JTAG | I2S audio (Anemoia) |

| 41–43 | JTAG / UART0 | Free (avoid 43 if using hardware UART0 TX) |


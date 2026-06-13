#pragma once

// ---------------------------------------------------------------------------
// LCD pin map — edit these to match your bench wiring.
//
// Defaults follow the Waveshare ESP32-S3 + 1.69" ST7789V2 SPI layout, with BL
// on GPIO9 because many ESP32-S3 Mini breakouts only expose GPIOs up to ~13.
//   DIN/MOSI = GPIO7, CLK/SCK = GPIO6, CS = GPIO5, DC = GPIO4,
//   RST = GPIO8, BL = GPIO9
//
// Free GPIOs on a 0-13 header (after LCD): 2, 3, 9, 12, 13
// (GPIO10/11 reserved for future IMU I2C in the Motion Pro pin plan)
//
// Mapping from LCD module silkscreen → ESP32 GPIO:
//   VCC → 3.3 V    GND → GND
//   DIN → LCD_MOSI   CLK → LCD_SCK   CS → LCD_CS
//   DC  → LCD_DC     RST → LCD_RST   BL → LCD_BL (PWM)
// ---------------------------------------------------------------------------

#define LCD_MOSI 7
#define LCD_SCK  6
#define LCD_CS   5
#define LCD_DC   4
#define LCD_RST  8
#define LCD_BL   9

// ST7789V2 240 x 280 panel geometry (Waveshare 1.69" class)
#define LCD_WIDTH   240
#define LCD_HEIGHT  280

// Many 240x280 ST7789V2 modules offset the active area in the 240x320 RAM.
#define LCD_COL_START 0
#define LCD_ROW_START 20

// Backlight: set true to drive BL from GPIO; false if BL is tied to 3.3 V
#define LCD_BL_USE_PWM true

// SPI clock for bring-up (Hz). Lower if you see garbled pixels on long wires.
#define LCD_SPI_HZ 40000000

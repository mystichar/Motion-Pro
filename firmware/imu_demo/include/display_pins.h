#pragma once

// Same LCD map as firmware/screen_demo — keep both copies in sync.
// See motion_pro_pinout.md for the full table.

#define LCD_MOSI 7
#define LCD_SCK  6
#define LCD_CS   5
#define LCD_DC   4
#define LCD_RST  8
#define LCD_BL   9

#define LCD_WIDTH   240
#define LCD_HEIGHT  280
#define LCD_COL_START 0
#define LCD_ROW_START 20
#define LCD_BL_USE_PWM true
#define LCD_SPI_HZ 40000000

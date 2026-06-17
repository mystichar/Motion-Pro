#pragma once

// Wii extension harness — dedicated bus, separate from IMU (Wire on GPIO 10/11).
// Reserved for future Wii-side I2C / Sense; not initialized in current firmware.
//
// Harness wire colors (Motion Pro prototype):
//   Gray  -> GPIO 1 (SDA)
//   White -> GPIO 2 (SCL_S)
//   Black -> GPIO 3 (TRIG_S / Sense)
//   Red   -> GND
//   Blue  -> +3.3 V ref (do not use as main supply)
//
// When enabled:
//   Wire1.begin(WII_SDA, WII_SCL);
//   pinMode(WII_SENSE, INPUT_PULLUP);

#define WII_SDA   1
#define WII_SCL   2
#define WII_SENSE 3

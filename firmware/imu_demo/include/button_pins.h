#pragma once

// Screen-select button — right header GPIO 12.
// Active LOW: one leg to GPIO 12, other leg to GND; enable INPUT_PULLUP in setup.
//
// Conflicts with Anemoia microSD MOSI — use embedded ROM or remap SD if needed.

#define BTN_GPIO 12

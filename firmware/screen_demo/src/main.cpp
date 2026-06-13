#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "display_pins.h"

namespace {

constexpr uint32_t kDemoHoldMs = 2500;
constexpr uint8_t kBacklightPwmChannel = 0;
constexpr uint8_t kBacklightPwmResolution = 8;
constexpr uint32_t kBacklightPwmFrequency = 5000;

Adafruit_ST7789 tft(LCD_CS, LCD_DC, LCD_RST);

constexpr uint16_t kHeaderColor = 0x0010;   // dark blue
constexpr uint16_t kGridColor = 0x4208;     // dark grey

struct Rgb565 {
  uint16_t color;
  const char *name;
};

constexpr Rgb565 kSolidColors[] = {
    {ST77XX_RED, "RED"},
    {ST77XX_GREEN, "GREEN"},
    {ST77XX_BLUE, "BLUE"},
    {ST77XX_WHITE, "WHITE"},
    {ST77XX_BLACK, "BLACK"},
};

constexpr uint16_t kBarColors[] = {
    ST77XX_RED,
    ST77XX_YELLOW,
    ST77XX_GREEN,
    ST77XX_CYAN,
    ST77XX_BLUE,
    ST77XX_MAGENTA,
};

void logPinMap() {
  Serial.println();
  Serial.println(F("=== Motion Pro LCD pin validation demo ==="));
  Serial.println(F("Edit include/display_pins.h if the on-screen map does not match your wiring."));
  Serial.printf("  DIN/MOSI -> GPIO%d\n", LCD_MOSI);
  Serial.printf("  CLK/SCK  -> GPIO%d\n", LCD_SCK);
  Serial.printf("  CS       -> GPIO%d\n", LCD_CS);
  Serial.printf("  DC       -> GPIO%d\n", LCD_DC);
  Serial.printf("  RST      -> GPIO%d\n", LCD_RST);
  Serial.printf("  BL       -> GPIO%d\n", LCD_BL);
  Serial.printf("  Panel    -> %dx%d (row start %d)\n", LCD_WIDTH, LCD_HEIGHT, LCD_ROW_START);
  Serial.println();
}

void writeBacklightDuty(uint32_t duty) {
#if LCD_BL_USE_PWM
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(LCD_BL, duty);
#else
  ledcWrite(kBacklightPwmChannel, duty);
#endif
#else
  digitalWrite(LCD_BL, duty > 0 ? HIGH : LOW);
#endif
}

void setupBacklight() {
#if LCD_BL_USE_PWM
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(LCD_BL, kBacklightPwmFrequency, kBacklightPwmResolution);
#else
  ledcSetup(kBacklightPwmChannel, kBacklightPwmFrequency, kBacklightPwmResolution);
  ledcAttachPin(LCD_BL, kBacklightPwmChannel);
#endif
  writeBacklightDuty(255);
#else
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
#endif
}

void setBacklightPercent(uint8_t percent) {
  const uint32_t duty = (255UL * percent) / 100UL;
  writeBacklightDuty(duty);
}

void drawHeader(const __FlashStringHelper *title) {
  tft.fillRect(0, 0, LCD_WIDTH, 28, kHeaderColor);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(4, 10);
  tft.print(title);
}

void drawPinMapScreen() {
  tft.fillScreen(ST77XX_BLACK);
  drawHeader(F("PIN MAP — verify wiring"));

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(8, 36);
  tft.println(F("LCD PINS"));

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  int y = 68;
  auto row = [&](const char *label, int gpio) {
    tft.setCursor(12, y);
    tft.printf("%-8s GPIO%-2d", label, gpio);
    y += 16;
  };

  row("DIN", LCD_MOSI);
  row("CLK", LCD_SCK);
  row("CS", LCD_CS);
  row("DC", LCD_DC);
  row("RST", LCD_RST);
  row("BL", LCD_BL);

  tft.setCursor(12, y + 8);
  tft.setTextColor(ST77XX_YELLOW);
  tft.printf("Panel %dx%d", LCD_WIDTH, LCD_HEIGHT);

  tft.setCursor(12, y + 24);
  tft.setTextColor(ST77XX_GREEN);
  tft.println(F("If text is readable,"));
  tft.setCursor(12, y + 36);
  tft.println(F("SPI + DC/RST/CS OK"));

  tft.drawRect(0, 0, LCD_WIDTH, LCD_HEIGHT, ST77XX_WHITE);
  tft.drawPixel(0, 0, ST77XX_RED);
  tft.drawPixel(LCD_WIDTH - 1, 0, ST77XX_GREEN);
  tft.drawPixel(0, LCD_HEIGHT - 1, ST77XX_BLUE);
  tft.drawPixel(LCD_WIDTH - 1, LCD_HEIGHT - 1, ST77XX_YELLOW);
}

void drawSolidColorScreen(const Rgb565 &swatch) {
  tft.fillScreen(swatch.color);
  drawHeader(F("SOLID COLOR"));

  tft.setTextSize(3);
  tft.setTextColor(ST77XX_BLACK);
  if (swatch.color == ST77XX_BLACK) {
    tft.setTextColor(ST77XX_WHITE);
  } else if (swatch.color == ST77XX_BLUE) {
    tft.setTextColor(ST77XX_YELLOW);
  }

  tft.setCursor(16, 120);
  tft.println(swatch.name);

  tft.setTextSize(1);
  tft.setCursor(16, 170);
  tft.println(F("Wrong color -> recheck DIN/CLK/DC"));
}

void drawColorBars() {
  tft.fillScreen(ST77XX_BLACK);
  drawHeader(F("COLOR BARS"));

  const int barTop = 32;
  const int barHeight = LCD_HEIGHT - barTop - 8;
  const int barWidth = LCD_WIDTH / static_cast<int>(sizeof(kBarColors) / sizeof(kBarColors[0]));

  for (size_t i = 0; i < sizeof(kBarColors) / sizeof(kBarColors[0]); ++i) {
    tft.fillRect(static_cast<int>(i) * barWidth, barTop, barWidth, barHeight, kBarColors[i]);
  }

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(8, LCD_HEIGHT - 18);
  tft.println(F("Rainbow order left->right"));
}

void drawGridScreen() {
  tft.fillScreen(ST77XX_BLACK);
  drawHeader(F("GRID + BORDERS"));

  for (int x = 0; x < LCD_WIDTH; x += 20) {
    tft.drawFastVLine(x, 28, LCD_HEIGHT - 28, kGridColor);
  }
  for (int y = 28; y < LCD_HEIGHT; y += 20) {
    tft.drawFastHLine(0, y, LCD_WIDTH, kGridColor);
  }

  tft.drawRect(0, 0, LCD_WIDTH, LCD_HEIGHT, ST77XX_WHITE);
  tft.drawRect(2, 2, LCD_WIDTH - 4, LCD_HEIGHT - 4, ST77XX_CYAN);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(8, 40);
  tft.println(F("Check corners + edges"));
  tft.setCursor(8, 56);
  tft.printf("Active area %dx%d", LCD_WIDTH, LCD_HEIGHT);
}

void drawBacklightScreen(uint8_t percent) {
  tft.fillScreen(ST77XX_BLACK);
  drawHeader(F("BACKLIGHT PWM"));

  const int barX = 20;
  const int barY = 80;
  const int barW = LCD_WIDTH - 40;
  const int barH = 24;

  tft.drawRect(barX, barY, barW, barH, ST77XX_WHITE);
  const int fillW = (barW - 2) * percent / 100;
  tft.fillRect(barX + 1, barY + 1, fillW, barH - 2, ST77XX_WHITE);

  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(20, 120);
  tft.printf("%u%%", percent);

  tft.setTextSize(1);
  tft.setCursor(20, 150);
  tft.println(F("No change -> check BL pin"));
  tft.setCursor(20, 166);
  tft.printf("BL on GPIO%d", LCD_BL);
}

void runDemoStep(const __FlashStringHelper *stepName, void (*drawFn)()) {
  Serial.print(F("Demo step: "));
  Serial.println(stepName);
  drawFn();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);
  logPinMap();

  setupBacklight();
  setBacklightPercent(80);

  SPI.begin(LCD_SCK, -1, LCD_MOSI, LCD_CS);
  tft.init(LCD_WIDTH, LCD_HEIGHT, SPI_MODE0);
  tft.setSPISpeed(LCD_SPI_HZ);
  tft.setRotation(0);

  tft.fillScreen(ST77XX_BLACK);
  drawHeader(F("BOOT OK"));
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(24, 120);
  tft.println(F("MOTION PRO"));
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(24, 150);
  tft.println(F("LCD demo starting..."));
  delay(1200);
}

void loop() {
  runDemoStep(F("pin map"), drawPinMapScreen);
  delay(kDemoHoldMs);

  for (const Rgb565 &swatch : kSolidColors) {
    Serial.printf("Demo step: solid %s\n", swatch.name);
    drawSolidColorScreen(swatch);
    delay(kDemoHoldMs);
  }

  runDemoStep(F("color bars"), drawColorBars);
  delay(kDemoHoldMs);

  runDemoStep(F("grid"), drawGridScreen);
  delay(kDemoHoldMs);

  for (uint8_t level = 10; level <= 100; level += 30) {
    Serial.printf("Demo step: backlight %u%%\n", level);
    setBacklightPercent(level);
    drawBacklightScreen(level);
    delay(kDemoHoldMs);
  }

  setBacklightPercent(80);
}

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SparkFun_ISM330DHCX.h>

#include "display_pins.h"
#include "imu_pins.h"
#include "button_pins.h"
#include "orientation.h"
#include "prism_view.h"
#include "wii_i2c_view.h"

namespace {

constexpr uint32_t kRefreshMs = 50;
constexpr uint32_t kBtnDebounceMs = 50;
constexpr uint32_t kBtnHoldCalMs = 800;
constexpr uint32_t kDoubleTapWindowMs = 400;
constexpr uint32_t kPrismRefreshMs = 33;
constexpr uint32_t kCalFlashMs = 400;
constexpr uint16_t kHeaderColor = 0x0010;
constexpr uint16_t kGridColor = 0x4208;

enum class Screen : uint8_t { ImuDashboard, Prism3D, WiiI2c };

constexpr int kGraphX = 4;
constexpr int kGraphY = 108;
constexpr int kGraphW = 232;
constexpr int kGraphH = 118;
constexpr float kGyroScale = 80000.0f;  // ±80 dps (milli-dps units)

Adafruit_ST7789 tft(LCD_CS, LCD_DC, LCD_RST);
SparkFun_ISM330DHCX imu;

sfe_ism_data_t accel{};
sfe_ism_data_t gyro{};

float gyroXHist[kGraphW]{};
float gyroYHist[kGraphW]{};
float gyroZHist[kGraphW]{};
int graphHead = 0;
int graphSampleCount = 0;

// Off-screen buffer for smooth left-scrolling graph (interior only; border stays on TFT).
constexpr int kCanvasW = kGraphW - 2;
constexpr int kCanvasH = kGraphH - 2;
GFXcanvas16 graphCanvas(kCanvasW, kCanvasH);

bool imuOk = false;
Screen activeScreen = Screen::ImuDashboard;
orient::Tracker orientationTracker;
orient::GravityReference gravityReference;
bool btnIsPressed = false;
bool btnHoldHandled = false;
bool prismShowAxes = false;
prism::SceneViewMode prismViewMode = prism::SceneViewMode::GroundFixed;
bool pendingSingleTap = false;
uint32_t btnPressStartMs = 0;
uint32_t btnLastEdgeMs = 0;
uint32_t lastTapReleaseMs = 0;
uint32_t pendingSingleTapAtMs = 0;
uint32_t calFlashUntilMs = 0;

void drawStaticChrome();
void showImuDashboard();
void executeSingleTapToggle();
void executeDoubleTap();

void setupButton() {
  pinMode(BTN_GPIO, INPUT_PULLUP);
}

void showPrismView() {
  const orient::Quat qWorld =
      orient::wiimoteWorldOrientation(orientationTracker.orientation(), gravityReference.up());
  prism::drawPrismWithOverlay(tft, LCD_WIDTH, LCD_HEIGHT, prismViewMode, qWorld, gravityReference.up(),
                              prismShowAxes, orientationTracker.axisMapIndex(),
                              orientationTracker.axisMapLabel());
}

void showWiiI2cView() {
  wii_i2c::enter(tft);
}

void executeDoubleTap() {
  if (activeScreen == Screen::WiiI2c) {
    return;
  }
  if (activeScreen == Screen::ImuDashboard) {
    activeScreen = Screen::Prism3D;
  }
  if (activeScreen == Screen::Prism3D) {
    if (prismShowAxes) {
      orientationTracker.nextAxisMap();
    } else {
      prismShowAxes = true;
    }
    showPrismView();
    Serial.printf("Axis map %u: %s (R/P/Y = red/green/blue)\n", orientationTracker.axisMapIndex(),
                  orientationTracker.axisMapLabel());
  }
}

void executeSingleTapToggle() {
  if (activeScreen == Screen::ImuDashboard) {
    activeScreen = Screen::Prism3D;
    showPrismView();
    Serial.println(F("Screen: 3D prism"));
  } else if (activeScreen == Screen::Prism3D) {
    activeScreen = Screen::WiiI2c;
    showWiiI2cView();
    Serial.println(F("Screen: Wii I2C"));
  } else {
    activeScreen = Screen::ImuDashboard;
    showImuDashboard();
    Serial.println(F("Screen: IMU dashboard"));
  }
}

void togglePrismViewMode() {
  prismViewMode = (prismViewMode == prism::SceneViewMode::GroundFixed)
                      ? prism::SceneViewMode::WiimoteFixed
                      : prism::SceneViewMode::GroundFixed;
  calFlashUntilMs = millis() + kCalFlashMs;
  if (prismViewMode == prism::SceneViewMode::GroundFixed) {
    Serial.println(F("View: ground fixed (wiimote moves)"));
  } else {
    Serial.println(F("View: wiimote fixed (ground moves)"));
  }
}

void resetGraphHistory() {
  graphHead = 0;
  graphSampleCount = 0;
  memset(gyroXHist, 0, sizeof(gyroXHist));
  memset(gyroYHist, 0, sizeof(gyroYHist));
  memset(gyroZHist, 0, sizeof(gyroZHist));
  graphCanvas.fillScreen(ST77XX_BLACK);
}

void showImuDashboard() {
  drawStaticChrome();
  resetGraphHistory();
}

void pollScreenButton() {
  const bool pressed = digitalRead(BTN_GPIO) == LOW;
  const uint32_t now = millis();

  if (pressed != btnIsPressed && (now - btnLastEdgeMs) >= kBtnDebounceMs) {
    btnLastEdgeMs = now;
    btnIsPressed = pressed;
    if (pressed) {
      btnPressStartMs = now;
      btnHoldHandled = false;
    } else if (!btnHoldHandled && (now - btnPressStartMs) < kBtnHoldCalMs) {
      if (lastTapReleaseMs != 0 && (now - lastTapReleaseMs) <= kDoubleTapWindowMs) {
        pendingSingleTap = false;
        lastTapReleaseMs = 0;
        executeDoubleTap();
      } else {
        pendingSingleTap = true;
        pendingSingleTapAtMs = now + kDoubleTapWindowMs;
        lastTapReleaseMs = now;
      }
    }
  }

  if (pendingSingleTap && now >= pendingSingleTapAtMs) {
    pendingSingleTap = false;
    lastTapReleaseMs = 0;
    executeSingleTapToggle();
  }

  if (pressed && !btnHoldHandled && (now - btnPressStartMs) >= kBtnHoldCalMs) {
    btnHoldHandled = true;
    pendingSingleTap = false;
    togglePrismViewMode();
    if (activeScreen == Screen::Prism3D) {
      showPrismView();
    }
  }
}

void setupBacklight() {
#if LCD_BL_USE_PWM
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(LCD_BL, 5000, 8);
  ledcWrite(LCD_BL, 204);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(LCD_BL, 0);
  ledcWrite(0, 204);
#endif
#else
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
#endif
}

void drawStaticChrome() {
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, LCD_WIDTH, 24, kHeaderColor);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(4, 8);
  tft.print(F("ISM330DHCX"));

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(4, 30);
  tft.println(F("Accel (milli-g)"));

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(4, 88);
  tft.print(F("Gyro (milli-dps)  "));
  tft.setTextColor(ST77XX_RED);
  tft.print('X');
  tft.setTextColor(ST77XX_GREEN);
  tft.print('Y');
  tft.setTextColor(ST77XX_BLUE);
  tft.print('Z');

  tft.drawRect(kGraphX, kGraphY, kGraphW, kGraphH, kGridColor);
  const int midY = kGraphY + kGraphH / 2;
  tft.drawFastHLine(kGraphX + 1, midY, kGraphW - 2, kGridColor);

  tft.setTextColor(kGridColor);
  tft.setCursor(4, 232);
  tft.print(F("SDA GPIO"));
  tft.print(IMU_SDA);
  tft.print(F("  SCL GPIO"));
  tft.println(IMU_SCL);
}

void drawValueBlock(int y, char axis, float value) {
  tft.fillRect(40, y, LCD_WIDTH - 44, 14, ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(4, y);
  tft.print(axis);
  tft.print(F(": "));
  tft.setTextColor(ST77XX_WHITE);
  tft.print(value, 1);
}

void drawErrorScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(2);
  tft.setCursor(12, 100);
  tft.println(F("IMU ERROR"));
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 140);
  tft.println(F("Check Qwiic wiring:"));
  tft.setCursor(4, 156);
  tft.print(F("Blue  -> GPIO"));
  tft.println(IMU_SDA);
  tft.setCursor(4, 172);
  tft.print(F("Yellow-> GPIO"));
  tft.println(IMU_SCL);
  tft.setCursor(4, 196);
  tft.println(F("Red=3.3V Black=GND"));
}

bool initImu() {
  Wire.begin(IMU_SDA, IMU_SCL);
  Wire.setClock(400000);

  if (!imu.begin()) {
    return false;
  }

  imu.deviceReset();
  while (!imu.getDeviceReset()) {
    delay(1);
  }

  imu.setDeviceConfig();
  imu.setBlockDataUpdate();
  imu.setAccelDataRate(ISM_XL_ODR_104Hz);
  imu.setAccelFullScale(ISM_4g);
  imu.setGyroDataRate(ISM_GY_ODR_104Hz);
  imu.setGyroFullScale(ISM_500dps);
  return true;
}

void readImu() {
  imu.getAccel(&accel);
  imu.getGyro(&gyro);
}

void pushGyroSample() {
  gyroXHist[graphHead] = gyro.xData;
  gyroYHist[graphHead] = gyro.yData;
  gyroZHist[graphHead] = gyro.zData;
  graphHead = (graphHead + 1) % kGraphW;
  if (graphSampleCount < kGraphW) {
    ++graphSampleCount;
  }
}

int gyroToCanvasY(float value) {
  const int midY = kCanvasH / 2;
  const float clipped = constrain(value, -kGyroScale, kGyroScale);
  return midY - (int)((clipped / kGyroScale) * (midY - 2));
}

void scrollGraphCanvasLeft() {
  uint16_t* buf = graphCanvas.getBuffer();
  for (int y = 0; y < kCanvasH; ++y) {
    uint16_t* row = buf + y * kCanvasW;
    memmove(row, row + 1, (kCanvasW - 1) * sizeof(uint16_t));
    row[kCanvasW - 1] = ST77XX_BLACK;
  }
}

void drawGyroGraph() {
  scrollGraphCanvasLeft();

  const int midY = kCanvasH / 2;
  graphCanvas.drawFastHLine(0, midY, kCanvasW, kGridColor);

  const int idxNew = (graphHead + kGraphW - 1) % kGraphW;
  const int xNew = kCanvasW - 1;

  if (graphSampleCount >= 2) {
    const int idxPrev = (graphHead + kGraphW - 2) % kGraphW;
    const int xPrev = kCanvasW - 2;
    graphCanvas.drawLine(xPrev, gyroToCanvasY(gyroXHist[idxPrev]), xNew,
                         gyroToCanvasY(gyroXHist[idxNew]), ST77XX_RED);
    graphCanvas.drawLine(xPrev, gyroToCanvasY(gyroYHist[idxPrev]), xNew,
                         gyroToCanvasY(gyroYHist[idxNew]), ST77XX_GREEN);
    graphCanvas.drawLine(xPrev, gyroToCanvasY(gyroZHist[idxPrev]), xNew,
                         gyroToCanvasY(gyroZHist[idxNew]), ST77XX_BLUE);
  } else {
    graphCanvas.drawPixel(xNew, gyroToCanvasY(gyroXHist[idxNew]), ST77XX_RED);
    graphCanvas.drawPixel(xNew, gyroToCanvasY(gyroYHist[idxNew]), ST77XX_GREEN);
    graphCanvas.drawPixel(xNew, gyroToCanvasY(gyroZHist[idxNew]), ST77XX_BLUE);
  }

  tft.drawRGBBitmap(kGraphX + 1, kGraphY + 1, graphCanvas.getBuffer(), kCanvasW, kCanvasH);
}

void drawCurrentGyro() {
  tft.fillRect(4, 248, LCD_WIDTH - 8, 10, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(4, 248);
  tft.setTextColor(ST77XX_RED);
  tft.print(gyro.xData, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(F("  "));
  tft.setTextColor(ST77XX_GREEN);
  tft.print(gyro.yData, 0);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(F("  "));
  tft.setTextColor(ST77XX_BLUE);
  tft.print(gyro.zData, 0);
}

void drawReadings() {
  drawValueBlock(44, 'X', accel.xData);
  drawValueBlock(58, 'Y', accel.yData);
  drawValueBlock(72, 'Z', accel.zData);
  drawCurrentGyro();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  setupBacklight();
  setupButton();
  SPI.begin(LCD_SCK, -1, LCD_MOSI, LCD_CS);
  tft.init(LCD_WIDTH, LCD_HEIGHT, SPI_MODE0);
  tft.setSPISpeed(LCD_SPI_HZ);
  tft.setRotation(0);

  imuOk = initImu();
  wii_i2c::begin();
  if (imuOk) {
    orientationTracker.reset();
    gravityReference.reset();
    showImuDashboard();
    Serial.println(F("IMU OK — tap: cycle screens | double-tap: axis map | hold: 3D view"));
  } else {
    drawErrorScreen();
    Serial.println(F("IMU begin() failed — check wiring"));
  }
}

void loop() {
  if (!imuOk) {
    delay(1000);
    return;
  }

  readImu();
  orientationTracker.updateGyro(gyro.xData, gyro.yData, gyro.zData);
  gravityReference.updateAccel(accel.xData, accel.yData, accel.zData);
  pollScreenButton();

  if (activeScreen == Screen::ImuDashboard) {
    pushGyroSample();
    drawGyroGraph();
    drawReadings();

    Serial.printf("A  X:%8.1f  Y:%8.1f  Z:%8.1f  |  G  X:%8.1f  Y:%8.1f  Z:%8.1f\n",
                  accel.xData, accel.yData, accel.zData, gyro.xData, gyro.yData, gyro.zData);
    delay(kRefreshMs);
  } else if (activeScreen == Screen::Prism3D) {
    showPrismView();
    if (millis() < calFlashUntilMs) {
      tft.fillCircle(LCD_WIDTH - 10, 10, 4, ST77XX_GREEN);
    }
    delay(kPrismRefreshMs);
  } else {
    wii_i2c::refresh(tft);
    wii_i2c::logPeriodicSerial();
    delay(kRefreshMs);
  }
}

#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Wire.h>

#include "wii_extension_regs.h"
#include "wii_i2c_protocol.h"
#include "wii_pins.h"

namespace wii_i2c {

constexpr uint8_t kMaxEventBytes = 16;
constexpr uint8_t kEventRingSize = 12;

struct Event {
  char kind;
  char op[48];
  uint8_t len;
  uint8_t data[kMaxEventBytes];
};

Event gEvents[kEventRingSize];
volatile uint8_t gEventHead = 0;
bool gInitialized = false;

inline void pushEvent(char kind, const char* op, const uint8_t* data, uint8_t len) {
  const uint8_t slot = gEventHead % kEventRingSize;
  gEvents[slot].kind = kind;
  strncpy(gEvents[slot].op, op ? op : "", sizeof(gEvents[slot].op) - 1);
  gEvents[slot].op[sizeof(gEvents[slot].op) - 1] = '\0';
  gEvents[slot].len = len > kMaxEventBytes ? kMaxEventBytes : len;
  for (uint8_t i = 0; i < gEvents[slot].len; ++i) {
    gEvents[slot].data[i] = data[i];
  }
  gEventHead++;
}

inline void onReceive(int numBytes) {
  uint8_t buf[kMaxEventBytes]{};
  uint8_t n = 0;
  while (Wire1.available() && n < kMaxEventBytes) {
    buf[n++] = (uint8_t)Wire1.read();
  }
  while (Wire1.available()) {
    (void)Wire1.read();
  }
  DecodeInfo info{};
  decodeLastWrite(buf, n, info);
  onMasterWrite(buf, n);
  pushEvent('W', info.op, buf, n);
}

inline void onRequest() {
  uint8_t reply[kLiveReportLen]{};
  uint8_t reply_len = 0;
  const uint8_t ptr = readPointer();
  onMasterReadRequest(reply, kLiveReportLen, reply_len);
  Wire1.write(reply, reply_len);

  char op[48];
  if (reply_len >= kIdLen && ptr == kRegIdOff) {
    snprintf(op, sizeof(op), "ID %s", idLabelFor(reply));
  } else if (reply_len >= kLiveReportLen && ptr == kRegLiveDataOff) {
    snprintf(op, sizeof(op), "live@08");
  } else {
    snprintf(op, sizeof(op), "read %uB", reply_len);
  }
  pushEvent('R', op, reply, reply_len);
}

inline void begin() {
  if (gInitialized) {
    return;
  }
  pinMode(WII_SENSE, INPUT_PULLUP);
  protocolBegin();
  Wire1.begin(kExtSlaveAddr7bit, WII_SDA, WII_SCL, kExtI2cSpeedHz);
  Wire1.onReceive(onReceive);
  Wire1.onRequest(onRequest);
  gInitialized = true;
}

inline void formatHexLine(const uint8_t* data, uint8_t len, char* out, size_t outSize) {
  size_t pos = 0;
  for (uint8_t i = 0; i < len && pos + 3 < outSize; ++i) {
    pos += (size_t)snprintf(out + pos, outSize - pos, "%02X ", data[i]);
  }
  if (pos == 0 && outSize > 0) {
    out[0] = '-';
    out[1] = '\0';
  }
}

inline void drawStaticChrome(Adafruit_ST7789& tft) {
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 240, 22, 0x0010);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(4, 7);
  tft.print(F("Wii I2C stream"));

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(4, 26);
  tft.print(F("Slave 0x"));
  tft.print(kExtSlaveAddr7bit, HEX);
  tft.print(F("  bank A4/A6 per wiimote_i2c.json"));

  tft.setCursor(4, 38);
  tft.print(F("Gray=SDA G"));
  tft.print(WII_SDA);
  tft.print(F("  White=SCL G"));
  tft.println(WII_SCL);
  tft.setCursor(4, 50);
  tft.print(F("Black=Sense G"));
  tft.print(WII_SENSE);
  tft.print(F("  Red=GND  Blue=3V3 ref"));

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(4, 68);
  tft.println(F("Bus / decode"));
  tft.setCursor(4, 104);
  tft.println(F("Last write"));
  tft.setCursor(4, 130);
  tft.println(F("Last read"));
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(4, 156);
  tft.println(F("Recent events"));

  tft.setTextColor(0x4208);
  tft.setCursor(4, 268);
  tft.print(F("Tap: cycle screens"));
}

inline void drawDashboard(Adafruit_ST7789& tft) {
  uint32_t writes = 0;
  uint32_t reads = 0;
  getCounts(writes, reads);

  uint8_t lastWriteLen = 0;
  uint8_t lastReadLen = 0;
  uint8_t lastWrite[kMaxEventBytes]{};
  uint8_t lastRead[kLiveReportLen]{};
  getLastWrite(lastWrite, lastWriteLen);
  getLastRead(lastRead, lastReadLen);

  DecodeInfo writeInfo{};
  decodeLastWrite(lastWrite, lastWriteLen, writeInfo);

  DecodeInfo readInfo{};
  if (lastReadLen >= kLiveReportLen) {
    decodeLiveReport(lastRead, readInfo);
    readInfo.has_mp = true;
    if (readPointer() >= kRegIdOff) {
      snprintf(readInfo.op, sizeof(readInfo.op), "ID");
      snprintf(readInfo.detail, sizeof(readInfo.detail), "%s", idLabelFor(lastRead));
    } else {
      snprintf(readInfo.op, sizeof(readInfo.op), "MP gyro");
    }
  }

  const bool senseHigh = digitalRead(WII_SENSE) == HIGH;
  const char* bankLabel = activeBank() == RegBank::A60000 ? "A60000" : "A40000";

  tft.fillRect(4, 80, 232, 18, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 80);
  tft.print(F("Sense "));
  tft.print(senseHigh ? F("HI") : F("LO"));
  tft.print(F("  "));
  tft.print(bankLabel);
  tft.print(F(" ptr="));
  tft.print(readPointer(), HEX);
  tft.print(F(" W:"));
  tft.print(writes);
  tft.print(F(" R:"));
  tft.print(reads);

  tft.fillRect(4, 96, 232, 12, ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(4, 96);
  tft.print(writeInfo.op);
  if (writeInfo.detail[0]) {
    tft.print(F(" "));
    tft.print(writeInfo.detail);
  }

  char line[64];
  formatHexLine(lastWrite, lastWriteLen, line, sizeof(line));
  tft.fillRect(4, 116, 232, 12, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 116);
  tft.print(line);

  tft.fillRect(4, 142, 232, 12, ST77XX_BLACK);
  if (readInfo.has_mp) {
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(4, 142);
    tft.print(readInfo.op);
    tft.print(F(" "));
    tft.print(readInfo.detail);
  } else {
    formatHexLine(lastRead, lastReadLen, line, sizeof(line));
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(4, 142);
    tft.print(line);
  }

  tft.fillRect(4, 168, 232, 96, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  int y = 168;
  const uint8_t head = gEventHead;
  const uint8_t shown = head < 6 ? head : 6;
  const uint8_t start = head >= shown ? head - shown : 0;
  for (uint8_t i = 0; i < shown; ++i) {
    const Event& ev = gEvents[(start + i) % kEventRingSize];
    formatHexLine(ev.data, ev.len, line, sizeof(line));
    tft.setCursor(4, y);
    tft.print(ev.kind == 'W' ? F("W ") : F("R "));
    tft.print(ev.op);
    tft.print(F(" "));
    tft.print(line);
    y += 14;
  }

  if (head == 0) {
    tft.setCursor(4, 168);
    tft.setTextColor(0x4208);
    tft.println(F("Waiting for Wiimote I2C..."));
    tft.setCursor(4, 182);
    tft.println(F("Expect init F0=55, ID @FA"));
  }
}

inline void enter(Adafruit_ST7789& tft) {
  drawStaticChrome(tft);
  drawDashboard(tft);
}

inline void refresh(Adafruit_ST7789& tft) {
  drawDashboard(tft);
}

}  // namespace wii_i2c

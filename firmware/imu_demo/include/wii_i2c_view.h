#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Wire.h>

#include "wii_extension_regs.h"
#include "wii_i2c_protocol.h"
#include "wii_pins.h"

namespace wii_i2c {

constexpr uint8_t kMaxEventBytes = kMaxRawBytes;
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
uint32_t gLastInitAttemptMs = 0;

// Address-sweep debug (disable now that 0x58 is known-good on this hardware).
constexpr bool kEnableAddrScan = false;

// Address-sweep debug state.
constexpr uint8_t kScanLo = 0x08;
constexpr uint8_t kScanHi = 0x77;
constexpr uint32_t kScanDwellMs = 500;
bool gScanActive = false;
uint8_t gScanAddr = kExtSlaveAddr7bit;
uint8_t gCurrentAddr = kExtSlaveAddr7bit;
uint8_t gHitAddr = 0;  // 0 = no address has received traffic yet
uint32_t gScanStepStartMs = 0;
bool gScanArmed = false;

inline void formatHexLine(const uint8_t* data, uint8_t len, char* out, size_t outSize);

inline void prepareBusPins() {
  pinMode(WII_SENSE, OUTPUT);
  digitalWrite(WII_SENSE, HIGH);
  pinMode(WII_SDA, INPUT_PULLUP);
  pinMode(WII_SCL, INPUT_PULLUP);
}

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

// Callbacks run in the I2C slave task and must stay lean — especially onRequest(),
// which executes while the Wiimote is clocking out the reply. We only capture data
// into the ring here and let the main loop drain it to Serial (drainEventLog()).
inline void onReceive(int numBytes) {
  uint8_t buf[kMaxEventBytes]{};
  uint8_t n = 0;
  const int limit = numBytes > (int)sizeof(buf) ? (int)sizeof(buf) : numBytes;
  while (n < (uint8_t)limit && Wire1.available()) {
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
  uint8_t reply[kMaxEventBytes]{};
  uint8_t reply_len = 0;
  const uint8_t ptr = readPointer();
  onMasterReadRequest(reply, kLiveReportLen, reply_len);
  Wire1.write(reply, reply_len);

  char op[48];
  if (reply_len >= kIdLen && ptr == kRegIdOff) {
    snprintf(op, sizeof(op), "ID %s", idLabelFor(reply));
  } else if (reply_len >= kLiveReportLen && ptr == kRegLiveDataOff) {
    snprintf(op, sizeof(op), "live@08 ptr=%02X", ptr);
  } else {
    snprintf(op, sizeof(op), "read ptr=%02X %uB", ptr, reply_len);
  }
  pushEvent('R', op, reply, reply_len);
}

inline void drainEventLog() {
  static uint8_t tail = 0;
  const uint8_t head = gEventHead;
  if ((uint8_t)(head - tail) > kEventRingSize) {
    tail = head - kEventRingSize;
  }
  char line[128];
  while (tail != head) {
    const Event& ev = gEvents[tail % kEventRingSize];
    formatHexLine(ev.data, ev.len, line, sizeof(line));
    Serial.printf("I2C %c %-20s %s\n", ev.kind, ev.op[0] ? ev.op : "-", line);
    tail++;
  }
}

// Poll SDA/SCL as plain GPIO and count transitions. Zero edges while the Wiimote is
// powered and seated means the master is not clocking the bus (wiring / sync issue),
// not a firmware decode problem.
inline void probeBusActivity(uint16_t windowMs, uint32_t& sclEdges, uint32_t& sdaEdges) {
  pinMode(WII_SCL, INPUT_PULLUP);
  pinMode(WII_SDA, INPUT_PULLUP);
  int lastScl = digitalRead(WII_SCL);
  int lastSda = digitalRead(WII_SDA);
  sclEdges = 0;
  sdaEdges = 0;
  const uint32_t start = millis();
  while ((millis() - start) < windowMs) {
    const int s = digitalRead(WII_SCL);
    const int d = digitalRead(WII_SDA);
    if (s != lastScl) {
      sclEdges++;
      lastScl = s;
    }
    if (d != lastSda) {
      sdaEdges++;
      lastSda = d;
    }
  }
}

inline bool beginSlaveAt(uint8_t addr7, bool verbose) {
  prepareBusPins();
  delay(5);
  const int sda = digitalRead(WII_SDA);
  const int scl = digitalRead(WII_SCL);
  if (sda == LOW || scl == LOW) {
    if (verbose) {
      Serial.printf("I2C lines SDA=%d SCL=%d — bus stuck low, power ON Wiimote\n", sda, scl);
    }
    return false;
  }

  protocolBegin();
  Wire1.end();
  if (!Wire1.begin(addr7, WII_SDA, WII_SCL, kExtI2cSpeedHz)) {
    if (verbose) {
      Serial.println(F("Wire1.begin failed"));
    }
    return false;
  }
  Wire1.onReceive(onReceive);
  Wire1.onRequest(onRequest);
  gCurrentAddr = addr7;
  if (verbose) {
    Serial.printf("I2C slave ready addr=0x%02X SDA=GPIO%d SCL=GPIO%d SENSE=GPIO%d\n",
                  addr7, WII_SDA, WII_SCL, WII_SENSE);
  }
  return true;
}

inline void startAddressScan() {
  gScanActive = true;
  gScanArmed = false;
  gScanAddr = kScanLo;
  gHitAddr = 0;
  Serial.printf("Address scan started (0x%02X..0x%02X, %lums each)\n", kScanLo, kScanHi,
                (unsigned long)kScanDwellMs);
}

// Advance the slave through candidate addresses, locking on the first that sees traffic.
inline void scanTick() {
  const uint32_t now = millis();
  if (!gScanArmed) {
    gInitialized = beginSlaveAt(gScanAddr, false);
    gScanStepStartMs = now;
    gScanArmed = true;
    return;
  }

  uint32_t writes = 0;
  uint32_t reads = 0;
  getCounts(writes, reads);
  if (writes > 0 || reads > 0) {
    gHitAddr = gCurrentAddr;
    gScanActive = false;
    Serial.printf("*** SCAN HIT: address 0x%02X  W=%lu R=%lu — locking here ***\n", gCurrentAddr,
                  (unsigned long)writes, (unsigned long)reads);
    return;
  }

  if (now - gScanStepStartMs >= kScanDwellMs) {
    if (gScanAddr >= kScanHi) {
      Serial.println(F("Scan complete: NO address received traffic despite bus activity."));
      Serial.println(F("  -> ESP32 slave is not ACKing. Likely repeated-start/timing or"));
      Serial.println(F("     the master expects clock stretching. Re-arming at 0x52."));
      gScanActive = false;
      gHitAddr = 0;
      gInitialized = beginSlaveAt(kExtSlaveAddr7bit, true);
      return;
    }
    gScanAddr++;
    gScanArmed = false;  // re-begin at next address on the following tick
  }
}

inline void begin() {
  if (gInitialized) {
    return;
  }
  gLastInitAttemptMs = millis();
  gInitialized = beginSlaveAt(kExtSlaveAddr7bit, true);
}

inline void ensureSlave() {
  if (gScanActive) {
    scanTick();
    return;
  }
  if (gInitialized) {
    return;
  }
  const uint32_t now = millis();
  if (now - gLastInitAttemptMs < 2000) {
    return;
  }
  gLastInitAttemptMs = now;
  const uint8_t addr = gHitAddr != 0 ? gHitAddr : kExtSlaveAddr7bit;
  gInitialized = beginSlaveAt(addr, true);
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
  if (kExtSlaveAddr7bit != kExtSlaveAddrCanonical7bit) {
    tft.print(F(" (spec 0x"));
    tft.print(kExtSlaveAddrCanonical7bit, HEX);
    tft.print(')');
  }
  tft.print(F("  bank A4/A6"));

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
  tft.println(F("Last write (raw hex)"));
  tft.setCursor(4, 142);
  tft.println(F("Last read (raw hex)"));
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
  uint8_t lastRead[kMaxEventBytes]{};
  getLastWrite(lastWrite, lastWriteLen);
  getLastRead(lastRead, lastReadLen);

  DecodeInfo writeInfo{};
  decodeLastWrite(lastWrite, lastWriteLen, writeInfo);

  DecodeInfo readInfo{};
  if (lastReadLen >= kLiveReportLen) {
    decodeLiveReport(lastRead, readInfo);
    readInfo.has_mp = true;
    snprintf(readInfo.op, sizeof(readInfo.op), "MP gyro");
  }

  const bool senseHigh = digitalRead(WII_SENSE) == HIGH;
  const bool sdaHigh = digitalRead(WII_SDA) == HIGH;
  const bool sclHigh = digitalRead(WII_SCL) == HIGH;
  const char* bankLabel = activeBank() == RegBank::A60000 ? "A60000" : "A40000";
  const uint32_t lastAct = lastActivityMs();
  const uint32_t idleMs = lastAct == 0 ? 0 : millis() - lastAct;

  tft.fillRect(4, 80, 232, 18, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 80);
  if (gScanActive) {
    tft.setTextColor(ST77XX_YELLOW);
    tft.print(F("SCAN 0x"));
    tft.print(gCurrentAddr, HEX);
  } else if (gHitAddr != 0) {
    tft.setTextColor(ST77XX_GREEN);
    tft.print(F("HIT 0x"));
    tft.print(gHitAddr, HEX);
  } else {
    tft.print(gInitialized ? F("Slave OK") : F("Slave WAIT"));
  }
  tft.setTextColor(ST77XX_WHITE);
  tft.print(F(" S:"));
  tft.print(sdaHigh ? '1' : '0');
  tft.print(sclHigh ? '1' : '0');
  tft.print(F(" Sn"));
  tft.print(senseHigh ? F("H") : F("L"));
  tft.print(F("  "));
  tft.print(bankLabel);
  tft.print(F(" W:"));
  tft.print(writes);
  tft.print(F(" R:"));
  tft.print(reads);
  if (lastAct != 0) {
    tft.print(F(" "));
    tft.print(idleMs);
    tft.print(F("ms"));
  }

  tft.fillRect(4, 96, 232, 12, ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(4, 96);
  if (writeInfo.op[0]) {
    tft.print(writeInfo.op);
    if (writeInfo.detail[0]) {
      tft.print(F(" "));
      tft.print(writeInfo.detail);
    }
  } else {
    tft.print(F("no writes yet"));
  }

  char line[96];
  char line2[96];
  formatHexLine(lastWrite, lastWriteLen, line, sizeof(line));
  if (lastWriteLen > 10) {
    formatHexLine(lastWrite + 10, lastWriteLen - 10, line2, sizeof(line2));
  } else {
    line2[0] = '\0';
  }
  tft.fillRect(4, 116, 232, 24, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 116);
  tft.print(line);
  if (line2[0]) {
    tft.setCursor(4, 128);
    tft.print(line2);
  }

  formatHexLine(lastRead, lastReadLen, line, sizeof(line));
  if (readInfo.has_mp) {
    tft.fillRect(4, 154, 232, 12, ST77XX_BLACK);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(4, 154);
    tft.print(readInfo.op);
    tft.print(F(" "));
    tft.print(readInfo.detail);
  } else {
    tft.fillRect(4, 154, 232, 12, ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(4, 154);
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
    if (!gInitialized) {
      tft.println(F("Slave not started:"));
      tft.setCursor(4, 182);
      tft.println(F("SDA/SCL must be HIGH"));
      tft.setCursor(4, 196);
      tft.println(F("Power ON Wiimote first"));
    } else {
      tft.println(F("No I2C yet — check wiring:"));
      tft.setCursor(4, 182);
      tft.println(F("Gray=SDA White=SCL Black=Sense"));
      tft.setCursor(4, 196);
      tft.println(F("Serial 115200 logs raw W/R"));
    }
  }
}

inline void logPeriodicSerial() {
  drainEventLog();

  static uint32_t lastLogMs = 0;
  static uint8_t idleSeconds = 0;
  const uint32_t now = millis();
  if (now - lastLogMs < 1000) {
    return;
  }
  lastLogMs = now;
  logStatusSerial();

  if (gScanActive) {
    Serial.printf("Scan: listening at 0x%02X\n", gCurrentAddr);
    return;
  }

  uint32_t writes = 0;
  uint32_t reads = 0;
  getCounts(writes, reads);
  if (gHitAddr != 0 || writes > 0 || reads > 0) {
    idleSeconds = 0;
    return;
  }

  // No traffic at the current address. Confirm whether the master is clocking, then
  // (if it is) sweep addresses to find where the Wiimote is actually talking.
  idleSeconds++;
  if (kEnableAddrScan && idleSeconds >= 3) {
    uint32_t sclEdges = 0;
    uint32_t sdaEdges = 0;
    Wire1.end();
    probeBusActivity(30, sclEdges, sdaEdges);
    Serial.printf("Bus probe: SCL edges=%lu SDA edges=%lu in 30ms (0 = master not clocking)\n",
                  (unsigned long)sclEdges, (unsigned long)sdaEdges);
    idleSeconds = 0;
    if (sclEdges > 0) {
      startAddressScan();
    } else {
      gInitialized = beginSlaveAt(kExtSlaveAddr7bit, true);
    }
  }
}

inline void enter(Adafruit_ST7789& tft) {
  drawStaticChrome(tft);
  drawDashboard(tft);
}

inline void refresh(Adafruit_ST7789& tft) {
  ensureSlave();
  drawDashboard(tft);
}

}  // namespace wii_i2c

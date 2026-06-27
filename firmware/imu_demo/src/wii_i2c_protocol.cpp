#include "wii_i2c_protocol.h"

#include <Arduino.h>
#include <string.h>

#include "wii_pins.h"

namespace wii_i2c {

namespace {

uint8_t gRegA6[kRegBlockSize]{};
uint8_t gRegA4[kRegBlockSize]{};
RegBank gBank = RegBank::A60000;
uint16_t gReadPtr = kRegLiveDataOff;
volatile uint32_t gWriteCount = 0;
volatile uint32_t gReadCount = 0;
volatile uint32_t gLastActivityMs = 0;
uint8_t gLastWrite[kMaxRawBytes]{};
uint8_t gLastWriteLen = 0;
uint8_t gLastRead[kMaxRawBytes]{};
uint8_t gLastReadLen = 0;
uint8_t gLastWriteReported = 0;
uint8_t gLastReadReported = 0;

InitProgress gInitProgress{};
uint8_t gLastMpReport[kLiveReportLen]{};

void markInitF0() {
  gInitProgress.init_f0 = true;
  gInitProgress.init_f0_count++;
}

void markInitFb() {
  gInitProgress.init_fb = true;
}

void markIdRead(const uint8_t* id6) {
  gInitProgress.id_read = true;
  gInitProgress.id_read_count++;
  strncpy(gInitProgress.last_id_label, idLabelFor(id6), sizeof(gInitProgress.last_id_label) - 1);
  gInitProgress.last_id_label[sizeof(gInitProgress.last_id_label) - 1] = '\0';
}

void markActivated(uint8_t fmt) {
  gInitProgress.activated = true;
  gInitProgress.last_fmt_byte = fmt;
}

void markBankA4() {
  gInitProgress.bank_a4 = true;
}

void markLivePolling() {
  gInitProgress.live_polling = true;
}

// Wiimote writes are not always parsed as offset+payload; scan raw bytes too.
void scanWriteMilestones(const uint8_t* data, uint8_t len) {
  for (uint8_t i = 0; i + 1 < len; ++i) {
    if (data[i] == kRegInitOff && data[i + 1] == kInitDisableEncryption) {
      markInitF0();
    }
    if (data[i] == kRegInitDoneOff && data[i + 1] == kInitCompleteByte) {
      markInitFb();
    }
    if (data[i] == kRegFormatOff) {
      const uint8_t mode = data[i + 1];
      if (mode == kMpActivateOnly || mode == kMpActivateNunchukPassthrough ||
          mode == kMpActivateClassicPassthrough) {
        markActivated(mode);
      }
    }
  }
  if (len == 1 && data[0] == 0x37) {
    markLivePolling();
  }
}

int16_t clampMp14(int32_t v) {
  if (v > 8191) {
    return 8191;
  }
  if (v < -8192) {
    return -8192;
  }
  return (int16_t)v;
}

int16_t gyroToMpRaw(float mdps) {
  return clampMp14((int32_t)(mdps / 50.0f));
}

void packMotionPlusBytes(int16_t yaw, int16_t roll, int16_t pitch, bool slow, uint8_t* b) {
  const uint16_t uy = (uint16_t)yaw;
  const uint16_t ur = (uint16_t)roll;
  const uint16_t up = (uint16_t)pitch;
  b[0] = (uint8_t)(uy & 0xFF);
  b[1] = (uint8_t)(ur & 0xFF);
  b[2] = (uint8_t)(up & 0xFF);
  b[3] = (uint8_t)(((uy >> 6) & 0xFC) | (slow ? 0x03 : 0x00));
  b[4] = (uint8_t)(((ur >> 6) & 0xFC) | (slow ? 0x02 : 0x00));
  b[5] = (uint8_t)(((up >> 6) & 0xFC) | 0x02);
}

void writeLiveReportEverywhere(const uint8_t report[kLiveReportLen]) {
  memcpy(&gRegA6[kRegLiveDataOff], report, kLiveReportLen);
  memcpy(&gRegA4[kRegLiveDataOff], report, kLiveReportLen);
  // Wiimote on this prototype polls 0x37/0x3D pre-activation (A60000).
  memcpy(&gRegA6[0x37], report, kLiveReportLen);
  memcpy(&gRegA6[0x3D], report, kLiveReportLen);
  memcpy(gLastMpReport, report, kLiveReportLen);
}

uint8_t* bankRegs(RegBank bank) {
  return bank == RegBank::A60000 ? gRegA6 : gRegA4;
}

void seedRegisters() {
  memset(gRegA6, 0, sizeof(gRegA6));
  memset(gRegA4, 0, sizeof(gRegA4));
  memcpy(&gRegA6[kRegIdOff], kIdMpInactiveA6, kIdLen);
  memcpy(&gRegA4[kRegIdOff], kIdMpInactiveA4, kIdLen);
}

void activateMotionPlus(uint8_t mode) {
  markActivated(mode);
  markBankA4();
  memcpy(&gRegA4[kRegIdOff], kIdMpActive, kIdLen);
  gBank = RegBank::A40000;
  gReadPtr = kRegLiveDataOff;
}

void handleRegisterWrite(RegBank bank, uint16_t offset, const uint8_t* data, uint8_t len) {
  uint8_t* regs = bankRegs(bank);
  for (uint8_t i = 0; i < len && (offset + i) < kRegBlockSize; ++i) {
    regs[offset + i] = data[i];
  }

  if (bank == RegBank::A60000 && offset == kRegFormatOff && len >= 1) {
    const uint8_t mode = data[0];
    if (mode == kMpActivateOnly || mode == kMpActivateNunchukPassthrough ||
        mode == kMpActivateClassicPassthrough) {
      activateMotionPlus(mode);
      if (mode == kMpActivateNunchukPassthrough) {
        memcpy(&gRegA4[kRegIdOff], kIdMpNunchukPass, kIdLen);
      } else if (mode == kMpActivateClassicPassthrough) {
        memcpy(&gRegA4[kRegIdOff], kIdMpClassicPass, kIdLen);
      }
    }
  }

  if (offset == kRegInitOff && len >= 1 && data[0] == kInitDisableEncryption) {
    markInitF0();
    if (bank == RegBank::A40000) {
      memcpy(&gRegA4[kRegIdOff], kIdMpInactiveA4, kIdLen);
    }
  }
  if (offset == kRegInitDoneOff && len >= 1) {
    markInitFb();
  }
}

// Wii extension transactions to 0x52 use a single-byte register offset within the
// 0x100-byte block (e.g. 0xA400F0 -> 0xF0 on the wire). The first payload byte is
// the offset; any remaining bytes are the value(s) written.
uint16_t parseOffset(const uint8_t* data, uint8_t len, uint8_t& data_start) {
  if (len == 0) {
    data_start = 0;
    return 0;
  }
  data_start = 1;
  return data[0];
}

}  // namespace

void protocolBegin() {
  seedRegisters();
  gBank = RegBank::A60000;
  gReadPtr = kRegLiveDataOff;
  gWriteCount = 0;
  gReadCount = 0;
  gLastWriteLen = 0;
  gLastReadLen = 0;
  // Init milestones are sticky for the power-on session — do not clear here.
  memset(gLastMpReport, 0, sizeof(gLastMpReport));
}

void protocolEnd() {}

const char* idLabelFor(const uint8_t* id6) {
  for (uint8_t i = 0; i < kKnownIdCount; ++i) {
    if (memcmp(id6, kKnownIds[i].bytes, kIdLen) == 0) {
      return kKnownIds[i].label;
    }
  }
  return "unknown";
}

void decodeLiveReport(const uint8_t* b, DecodeInfo& info) {
  info.has_mp = true;
  info.mp.yaw_raw = (int16_t)(b[0] | ((b[3] & 0xFC) << 6));
  info.mp.roll_raw = (int16_t)(b[1] | ((b[4] & 0xFC) << 6));
  info.mp.pitch_raw = (int16_t)(b[2] | ((b[5] & 0xFC) << 6));
  info.mp.yaw_slow = (b[3] & 0x02) != 0;
  info.mp.pitch_slow = (b[3] & 0x01) != 0;
  info.mp.roll_slow = (b[4] & 0x02) != 0;
  info.mp.extension_connected = (b[4] & 0x01) != 0;
  info.mp.is_motionplus_report = (b[5] & 0x02) != 0;
  snprintf(info.detail, sizeof(info.detail), "Y%04X R%04X P%04X", (uint16_t)info.mp.yaw_raw,
           (uint16_t)info.mp.roll_raw, (uint16_t)info.mp.pitch_raw);
}

void decodeLastWrite(const uint8_t* data, uint8_t len, DecodeInfo& info) {
  info.op[0] = '\0';
  info.detail[0] = '\0';
  info.has_mp = false;
  if (len == 0) {
    snprintf(info.op, sizeof(info.op), "empty");
    return;
  }

  uint8_t payload_start = len;
  const uint16_t offset = parseOffset(data, len, payload_start);
  const uint8_t payload_len = (payload_start < len) ? len - payload_start : 0;

  if (payload_len == 0 && len <= 2) {
    snprintf(info.op, sizeof(info.op), "read ptr %02X", (unsigned)offset);
    return;
  }

  if (offset == kRegInitOff && payload_len >= 1 && data[payload_start] == kInitDisableEncryption) {
    snprintf(info.op, sizeof(info.op), "init 0xF0=55");
    snprintf(info.detail, sizeof(info.detail), "disable encryption");
    return;
  }
  if (offset == kRegInitDoneOff && payload_len >= 1) {
    snprintf(info.op, sizeof(info.op), "init 0xFB=%02X", data[payload_start]);
    return;
  }
  if (offset == kRegFormatOff && payload_len >= 1) {
    snprintf(info.op, sizeof(info.op), "fmt 0xFE=%02X", data[payload_start]);
    if (data[payload_start] == kMpActivateOnly) {
      snprintf(info.detail, sizeof(info.detail), "activate MotionPlus");
    }
    return;
  }

  snprintf(info.op, sizeof(info.op), "wr @%02X", (unsigned)offset);
  if (payload_len > 0) {
    char* p = info.detail;
    size_t remain = sizeof(info.detail);
    for (uint8_t i = 0; i < payload_len && i < 4 && remain > 3; ++i) {
      const int n = snprintf(p, remain, "%02X ", data[payload_start + i]);
      if (n <= 0) break;
      p += n;
      remain -= (size_t)n;
    }
  }
}

void onMasterWrite(const uint8_t* data, uint8_t len) {
  gWriteCount++;
  gLastActivityMs = millis();
  gLastWriteReported = len;
  gLastWriteLen = len > sizeof(gLastWrite) ? sizeof(gLastWrite) : len;
  memcpy(gLastWrite, data, gLastWriteLen);

  scanWriteMilestones(data, len);

  if (len == 0) {
    return;
  }

  uint8_t payload_start = len;
  const uint16_t offset = parseOffset(data, len, payload_start);
  if (payload_start >= len) {
    gReadPtr = offset;
    if (offset == 0x37 || offset == kRegLiveDataOff) {
      markLivePolling();
    }
    return;
  }

  const uint8_t* payload = data + payload_start;
  const uint8_t payload_len = len - payload_start;
  handleRegisterWrite(gBank, offset, payload, payload_len);
  gReadPtr = offset;
}

void onMasterReadRequest(uint8_t* out, uint8_t max_len, uint8_t& out_len) {
  gReadCount++;
  gLastActivityMs = millis();
  const uint16_t ptr_at_start = gReadPtr;
  uint8_t* regs = bankRegs(gBank);
  out_len = 0;
  while (out_len < max_len && out_len < kLiveReportLen && (gReadPtr + out_len) < kRegBlockSize) {
    out[out_len++] = regs[gReadPtr + out_len];
  }
  if (out_len == 0 && max_len >= kLiveReportLen) {
    memcpy(out, &regs[kRegLiveDataOff], kLiveReportLen);
    out_len = kLiveReportLen;
  }
  if (ptr_at_start == kRegIdOff && out_len >= kIdLen) {
    markIdRead(out);
  }
  if (ptr_at_start == 0x37 || ptr_at_start == kRegLiveDataOff) {
    if (out_len >= kLiveReportLen && (out[5] & 0x02)) {
      markLivePolling();
    }
  }
  gLastReadLen = out_len > sizeof(gLastRead) ? sizeof(gLastRead) : out_len;
  gLastReadReported = out_len;
  memcpy(gLastRead, out, gLastReadLen);
  gReadPtr += out_len;
}

void updateMotionPlusFromGyro(float yaw_mdps, float roll_mdps, float pitch_mdps) {
  const bool slow =
      (fabsf(yaw_mdps) + fabsf(roll_mdps) + fabsf(pitch_mdps)) < 800.0f;
  const int16_t yaw = gyroToMpRaw(yaw_mdps);
  const int16_t roll = gyroToMpRaw(roll_mdps);
  const int16_t pitch = gyroToMpRaw(pitch_mdps);
  uint8_t report[kLiveReportLen]{};
  packMotionPlusBytes(yaw, roll, pitch, slow, report);
  writeLiveReportEverywhere(report);
}

void getInitProgress(InitProgress& out) {
  out = gInitProgress;
  if (gBank == RegBank::A40000) {
    out.bank_a4 = true;
  }
}

void getLastMotionPlusReport(uint8_t out[kLiveReportLen]) {
  memcpy(out, gLastMpReport, kLiveReportLen);
}

void logStatusSerial() {
  const bool senseHigh = digitalRead(WII_SENSE) == HIGH;
  const bool sdaHigh = digitalRead(WII_SDA) == HIGH;
  const bool sclHigh = digitalRead(WII_SCL) == HIGH;
  Serial.printf("I2C status: sda=%d scl=%d sense=%s bank=%s ptr=%02X W=%lu R=%lu last=%lums ago\n",
                sdaHigh ? 1 : 0, sclHigh ? 1 : 0, senseHigh ? "HI" : "LO",
                gBank == RegBank::A60000 ? "A60000" : "A40000", (unsigned)gReadPtr,
                (unsigned long)gWriteCount, (unsigned long)gReadCount,
                gLastActivityMs == 0 ? 0UL : (unsigned long)(millis() - gLastActivityMs));
}

uint32_t lastActivityMs() {
  return gLastActivityMs;
}

void getCounts(uint32_t& writes, uint32_t& reads) {
  writes = gWriteCount;
  reads = gReadCount;
}

RegBank activeBank() {
  return gBank;
}

uint8_t readPointer() {
  return (uint8_t)gReadPtr;
}

void getLastWrite(uint8_t* out, uint8_t& len) {
  len = gLastWriteLen;
  memcpy(out, gLastWrite, len);
}

void getLastRead(uint8_t* out, uint8_t& len) {
  len = gLastReadLen;
  memcpy(out, gLastRead, len);
}

}  // namespace wii_i2c

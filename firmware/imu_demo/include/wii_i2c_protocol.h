#pragma once

#include <stdint.h>

#include "wii_extension_regs.h"

namespace wii_i2c {

enum class RegBank : uint8_t { A60000, A40000 };

struct MotionPlusSample {
  int16_t yaw_raw;
  int16_t roll_raw;
  int16_t pitch_raw;
  bool yaw_slow;
  bool roll_slow;
  bool pitch_slow;
  bool extension_connected;
  bool is_motionplus_report;
};

struct DecodeInfo {
  char op[48];
  char detail[64];
  MotionPlusSample mp{};
  bool has_mp = false;
};

// Sticky init milestones — latched once seen (survives fast serial scroll).
struct InitProgress {
  bool init_f0;
  bool init_fb;
  bool id_read;
  bool activated;
  bool bank_a4;
  bool live_polling;
  uint32_t init_f0_count;
  uint32_t id_read_count;
  uint8_t last_fmt_byte;
  char last_id_label[20];
};

void protocolBegin();
void protocolEnd();

void onMasterWrite(const uint8_t* data, uint8_t len);
void onMasterReadRequest(uint8_t* out, uint8_t max_len, uint8_t& out_len);

void updateMotionPlusFromGyro(float yaw_mdps, float roll_mdps, float pitch_mdps);
void getInitProgress(InitProgress& out);
void getLastMotionPlusReport(uint8_t out[6]);

void getCounts(uint32_t& writes, uint32_t& reads);
RegBank activeBank();
uint8_t readPointer();
const char* idLabelFor(const uint8_t* id6);

void getLastWrite(uint8_t* out, uint8_t& len);
void getLastRead(uint8_t* out, uint8_t& len);
uint32_t lastActivityMs();
void logStatusSerial();

constexpr uint8_t kMaxRawBytes = 32;

void decodeLastWrite(const uint8_t* data, uint8_t len, DecodeInfo& info);
void decodeLiveReport(const uint8_t* b, DecodeInfo& info);

}  // namespace wii_i2c

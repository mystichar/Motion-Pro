#pragma once

#include <Arduino.h>
#include <math.h>

// Gyro integration + face-down calibration for the Wiimote prism view.

namespace orient {

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Quat {
  float w;
  float x;
  float y;
  float z;
};

struct AxisMap {
  uint8_t rollIn;
  uint8_t pitchIn;
  uint8_t yawIn;
  int8_t rollSign;
  int8_t pitchSign;
  int8_t yawSign;
  const char* label;
};

// Default mount: IMU -X → controller bottom (-Y), IMU +Y → controller left (-X),
// IMU +Z → controller front (+Z).  Model vector = (-imu_y, imu_x, imu_z).
constexpr uint8_t kDefaultAxisMapIndex = 0;

constexpr AxisMap kAxisMaps[] = {
    {1, 0, 2, -1, 1, 1, "-Y X Z"},
    {0, 1, 2, 1, 1, 1, "XYZ"},
    {0, 1, 2, -1, 1, 1, "-X Y Z"},
    {0, 1, 2, 1, -1, 1, "X -Y Z"},
    {0, 1, 2, 1, 1, -1, "X Y -Z"},
    {2, 0, 1, 1, 1, 1, "ZXY"},
    {2, 0, 1, -1, 1, 1, "-Z X Y"},
    {1, 2, 0, 1, 1, 1, "YZX"},
    {1, 2, 0, 1, -1, 1, "Y -Z X"},
    {0, 2, 1, 1, 1, 1, "XZY"},
    {1, 0, 2, 1, 1, 1, "YXZ"},
    {2, 1, 0, 1, 1, 1, "ZYX"},
    {2, 1, 0, 1, 1, -1, "Z Y -X"},
};

constexpr uint8_t kAxisMapCount = sizeof(kAxisMaps) / sizeof(kAxisMaps[0]);

inline Vec3 vec3Normalize(Vec3 v) {
  const float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
  if (len < 0.0001f) {
    return {0.0f, 0.0f, 1.0f};
  }
  return {v.x / len, v.y / len, v.z / len};
}

inline Quat quatIdentity() {
  return {1.0f, 0.0f, 0.0f, 0.0f};
}

inline Quat quatNormalize(Quat q) {
  const float len = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  if (len < 0.0001f) {
    return quatIdentity();
  }
  return {q.w / len, q.x / len, q.y / len, q.z / len};
}

inline Quat quatConjugate(Quat q) {
  return {q.w, -q.x, -q.y, -q.z};
}

inline Quat quatMultiply(Quat a, Quat b) {
  return {
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
  };
}

inline Vec3 quatRotate(Quat q, Vec3 v) {
  const Quat p = {0.0f, v.x, v.y, v.z};
  const Quat r = quatMultiply(quatMultiply(q, p), quatConjugate(q));
  return {r.x, r.y, r.z};
}

inline Quat quatFromAxisAngle(Vec3 axis, float angleRad) {
  axis = vec3Normalize(axis);
  const float half = angleRad * 0.5f;
  const float s = sinf(half);
  return quatNormalize({cosf(half), axis.x * s, axis.y * s, axis.z * s});
}

inline Quat quatFromRotMatrix(float m00, float m01, float m02, float m10, float m11, float m12,
                              float m20, float m21, float m22) {
  const float trace = m00 + m11 + m22;
  Quat q{};
  if (trace > 0.0f) {
    const float s = sqrtf(trace + 1.0f) * 2.0f;
    q.w = 0.25f * s;
    q.x = (m21 - m12) / s;
    q.y = (m02 - m20) / s;
    q.z = (m10 - m01) / s;
  } else if (m00 > m11 && m00 > m22) {
    const float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
    q.w = (m21 - m12) / s;
    q.x = 0.25f * s;
    q.y = (m01 + m10) / s;
    q.z = (m02 + m20) / s;
  } else if (m11 > m22) {
    const float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
    q.w = (m02 - m20) / s;
    q.x = (m01 + m10) / s;
    q.y = 0.25f * s;
    q.z = (m12 + m21) / s;
  } else {
    const float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
    q.w = (m10 - m01) / s;
    q.x = (m02 + m20) / s;
    q.y = (m12 + m21) / s;
    q.z = 0.25f * s;
  }
  return quatNormalize(q);
}

inline Quat quatFrontUprightOnScreen(Vec3 upHint) {
  // Lock model +Z (front texture) toward the viewer (+Z), twist so model +Y follows upHint.
  constexpr Vec3 kForward = {0.0f, 0.0f, 1.0f};
  upHint = vec3Normalize(upHint);

  Vec3 right = {
      upHint.y * kForward.z - upHint.z * kForward.y,
      upHint.z * kForward.x - upHint.x * kForward.z,
      upHint.x * kForward.y - upHint.y * kForward.x,
  };
  const float rightLen = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
  if (rightLen < 0.05f) {
    right = {1.0f, 0.0f, 0.0f};
  } else {
    right.x /= rightLen;
    right.y /= rightLen;
    right.z /= rightLen;
  }

  upHint = {
      right.y * kForward.z - right.z * kForward.y,
      right.z * kForward.x - right.x * kForward.z,
      right.x * kForward.y - right.y * kForward.x,
  };

  return quatFromRotMatrix(right.x, upHint.x, kForward.x, right.y, upHint.y, kForward.y, right.z,
                           upHint.z, kForward.z);
}

inline Vec3 remapSensorAxes(float x, float y, float z, const AxisMap& map) {
  const float in[3] = {x, y, z};
  const uint8_t rollIn = map.rollIn % 3;
  const uint8_t pitchIn = map.pitchIn % 3;
  const uint8_t yawIn = map.yawIn % 3;
  return {
      in[rollIn] * map.rollSign,
      in[pitchIn] * map.pitchSign,
      in[yawIn] * map.yawSign,
  };
}

class Tracker {
 public:
  void reset() {
    qIntegrated_ = quatIdentity();
    qOffset_ = quatIdentity();
    mapIndex_ = kDefaultAxisMapIndex;
    lastUs_ = micros();
  }

  void nextAxisMap() {
    mapIndex_ = (mapIndex_ + 1) % kAxisMapCount;
  }

  uint8_t axisMapIndex() const {
    return mapIndex_;
  }

  const char* axisMapLabel() const {
    return kAxisMaps[mapIndex_].label;
  }

  void updateGyro(float gxMilliDps, float gyMilliDps, float gzMilliDps) {
    const uint32_t nowUs = micros();
    float dt = (nowUs - lastUs_) * 1e-6f;
    lastUs_ = nowUs;
    if (dt <= 0.0f || dt > 0.25f) {
      return;
    }

    constexpr float kMilliDpsToRad = PI / (180.0f * 1000.0f);
    const Vec3 raw = {
        gxMilliDps * kMilliDpsToRad,
        gyMilliDps * kMilliDpsToRad,
        gzMilliDps * kMilliDpsToRad,
    };
    const Vec3 omega = remapSensorAxes(raw.x, raw.y, raw.z, kAxisMaps[mapIndex_]);

    const Quat omegaQuat = {0.0f, omega.x, omega.y, omega.z};
    const Quat qDot = quatMultiply(qIntegrated_, omegaQuat);
    qIntegrated_.w += 0.5f * dt * qDot.w;
    qIntegrated_.x += 0.5f * dt * qDot.x;
    qIntegrated_.y += 0.5f * dt * qDot.y;
    qIntegrated_.z += 0.5f * dt * qDot.z;
    qIntegrated_ = quatNormalize(qIntegrated_);
  }

  void recenterViewUpright(float axMilliG, float ayMilliG, float azMilliG) {
    const Vec3 upHint = remapSensorAxes(axMilliG, ayMilliG, azMilliG, kAxisMaps[mapIndex_]);
    const Quat qTarget = quatFrontUprightOnScreen(upHint);
    qOffset_ = quatMultiply(qTarget, quatConjugate(qIntegrated_));
  }

  Quat orientation() const {
    return quatMultiply(qOffset_, qIntegrated_);
  }

  const AxisMap& axisMap() const {
    return kAxisMaps[mapIndex_];
  }

 private:
  Quat qIntegrated_ = quatIdentity();
  Quat qOffset_ = quatIdentity();
  uint8_t mapIndex_ = kDefaultAxisMapIndex;
  uint32_t lastUs_ = 0;
};

// Low-pass accelerometer "up" for a gravity-referenced ground plane (independent of gyro).
class GravityReference {
 public:
  void reset() {
    filteredUp_ = {0.0f, 1.0f, 0.0f};
    hasSample_ = false;
    lastUs_ = micros();
  }

  void updateAccel(float axMilliG, float ayMilliG, float azMilliG, const AxisMap& map) {
    const uint32_t nowUs = micros();
    float dt = (nowUs - lastUs_) * 1e-6f;
    lastUs_ = nowUs;
    if (dt <= 0.0f || dt > 0.25f) {
      dt = 0.033f;
    }

    const Vec3 raw = remapSensorAxes(axMilliG, ayMilliG, azMilliG, map);
    const float len = sqrtf(raw.x * raw.x + raw.y * raw.y + raw.z * raw.z);
    if (len < 50.0f) {
      return;
    }

    const Vec3 up = {raw.x / len, raw.y / len, raw.z / len};
    if (!hasSample_) {
      filteredUp_ = up;
      hasSample_ = true;
      return;
    }

    // Time-based smoothing (~60 ms when still, slower when |a| != g during motion).
    constexpr float kTauStillSec = 0.06f;
    constexpr float kTauMotionSec = 0.18f;
    const float tau =
        (len > 900.0f && len < 1100.0f) ? kTauStillSec : kTauMotionSec;
    const float alpha = 1.0f - expf(-dt / tau);

    filteredUp_.x += alpha * (up.x - filteredUp_.x);
    filteredUp_.y += alpha * (up.y - filteredUp_.y);
    filteredUp_.z += alpha * (up.z - filteredUp_.z);
    filteredUp_ = vec3Normalize(filteredUp_);
  }

  Vec3 up() const {
    return filteredUp_;
  }

 private:
  Vec3 filteredUp_{0.0f, 1.0f, 0.0f};
  bool hasSample_ = false;
  uint32_t lastUs_ = 0;
};

}  // namespace orient

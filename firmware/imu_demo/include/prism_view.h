#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

// White rectangular prism: width 5 (X), height 20 (Y), depth 4 (Z), centered at origin.
// Renders to an off-screen buffer each frame, then blits once (no partial-frame flicker).

namespace prism {

struct Vec3 {
  float x;
  float y;
  float z;
};

struct Projected {
  int16_t x;
  int16_t y;
  float z;
};

constexpr float kHalfWidth = 2.5f;
constexpr float kHalfHeight = 10.0f;
constexpr float kHalfDepth = 2.0f;

constexpr float kTiltX = 0.55f;
constexpr float kScale = 5.5f;

constexpr Vec3 kBaseVerts[8] = {
    {-kHalfWidth, -kHalfHeight, -kHalfDepth},
    {kHalfWidth, -kHalfHeight, -kHalfDepth},
    {kHalfWidth, kHalfHeight, -kHalfDepth},
    {-kHalfWidth, kHalfHeight, -kHalfDepth},
    {-kHalfWidth, -kHalfHeight, kHalfDepth},
    {kHalfWidth, -kHalfHeight, kHalfDepth},
    {kHalfWidth, kHalfHeight, kHalfDepth},
    {-kHalfWidth, kHalfHeight, kHalfDepth},
};

struct Face {
  uint8_t i0;
  uint8_t i1;
  uint8_t i2;
  uint8_t i3;
};

constexpr Face kFaces[6] = {
    {4, 5, 6, 7},  // +Z front
    {1, 0, 3, 2},  // -Z back
    {5, 1, 2, 6},  // +X right
    {0, 4, 7, 3},  // -X left
    {7, 6, 2, 3},  // +Y top (outward normal +Y)
    {0, 1, 5, 4},  // -Y bottom (outward normal -Y)
};

constexpr float kLightX = 0.35f;
constexpr float kLightY = 0.65f;
constexpr float kLightZ = 0.45f;

inline Vec3 rotateX(Vec3 v, float cosA, float sinA) {
  const float y = v.y * cosA - v.z * sinA;
  const float z = v.y * sinA + v.z * cosA;
  return {v.x, y, z};
}

inline Vec3 rotateY(Vec3 v, float cosA, float sinA) {
  const float x = v.x * cosA + v.z * sinA;
  const float z = -v.x * sinA + v.z * cosA;
  return {x, v.y, z};
}

inline Vec3 faceNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
  const float ux = b.x - a.x;
  const float uy = b.y - a.y;
  const float uz = b.z - a.z;
  const float vx = c.x - a.x;
  const float vy = c.y - a.y;
  const float vz = c.z - a.z;
  Vec3 n{
      uy * vz - uz * vy,
      uz * vx - ux * vz,
      ux * vy - uy * vx,
  };
  const float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
  if (len > 0.0001f) {
    n.x /= len;
    n.y /= len;
    n.z /= len;
  }
  return n;
}

inline uint16_t shadedWhite(float brightness) {
  brightness = constrain(brightness, 0.22f, 1.0f);
  const uint8_t level = (uint8_t)(brightness * 255.0f);
  const uint8_t r = level >> 3;
  const uint8_t g = level >> 2;
  const uint8_t b = level >> 3;
  return (r << 11) | (g << 5) | b;
}

inline float minFaceZ(const Projected& p0, const Projected& p1, const Projected& p2,
                      const Projected& p3) {
  float z = p0.z;
  if (p1.z < z) z = p1.z;
  if (p2.z < z) z = p2.z;
  if (p3.z < z) z = p3.z;
  return z;
}

inline void drawQuad(GFXcanvas16& canvas, const Projected& p0, const Projected& p1,
                     const Projected& p2, const Projected& p3, uint16_t color) {
  canvas.fillTriangle(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, color);
  canvas.fillTriangle(p0.x, p0.y, p2.x, p2.y, p3.x, p3.y, color);
}

struct FaceDraw {
  uint8_t index;
  float sortZ;
};

inline GFXcanvas16& frameBuffer() {
  static GFXcanvas16 canvas(240, 280);
  return canvas;
}

inline void drawRotatingPrism(Adafruit_ST7789& tft, int screenW, int screenH, float angleY) {
  GFXcanvas16& canvas = frameBuffer();
  if (screenW != canvas.width() || screenH != canvas.height()) {
    return;
  }

  const float cosY = cosf(angleY);
  const float sinY = sinf(angleY);
  const float cosX = cosf(kTiltX);
  const float sinX = sinf(kTiltX);

  Vec3 world[8];
  Projected projected[8];
  for (uint8_t i = 0; i < 8; ++i) {
    const Vec3 v = rotateY(rotateX(kBaseVerts[i], cosX, sinX), cosY, sinY);
    world[i] = v;
    projected[i].x = (int16_t)(screenW / 2 + v.x * kScale);
    projected[i].y = (int16_t)(screenH / 2 - v.y * kScale);
    projected[i].z = v.z;
  }

  FaceDraw order[6];
  uint8_t visibleCount = 0;
  for (uint8_t f = 0; f < 6; ++f) {
    const Face& face = kFaces[f];
    const Vec3 normal = faceNormal(world[face.i0], world[face.i1], world[face.i2]);
    if (normal.z <= 0.0f) {
      continue;
    }

    order[visibleCount].index = f;
    order[visibleCount].sortZ =
        minFaceZ(projected[face.i0], projected[face.i1], projected[face.i2], projected[face.i3]);
    ++visibleCount;
  }

  for (uint8_t a = 0; a + 1 < visibleCount; ++a) {
    for (uint8_t b = a + 1; b < visibleCount; ++b) {
      if (order[a].sortZ > order[b].sortZ ||
          (order[a].sortZ == order[b].sortZ && order[a].index > order[b].index)) {
        const FaceDraw tmp = order[a];
        order[a] = order[b];
        order[b] = tmp;
      }
    }
  }

  canvas.fillScreen(ST77XX_BLACK);

  for (uint8_t o = 0; o < visibleCount; ++o) {
    const Face& face = kFaces[order[o].index];
    const Vec3 normal = faceNormal(world[face.i0], world[face.i1], world[face.i2]);
    const float lighting =
        normal.x * kLightX + normal.y * kLightY + normal.z * kLightZ;
    const uint16_t color = shadedWhite(lighting);

    drawQuad(canvas, projected[face.i0], projected[face.i1], projected[face.i2],
             projected[face.i3], color);
  }

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), screenW, screenH);
}

}  // namespace prism

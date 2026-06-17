#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

#include "orientation.h"
#include "wiimote_textures.h"

// Wiimote-shaped box: 9 (X) x 8 (Y) x 36 (Z) — matches assets/*.png aspect ratios.
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

constexpr float kHalfWidth = 4.5f;
constexpr float kHalfHeight = 4.0f;
constexpr float kHalfDepth = 18.0f;

constexpr float kScale = 3.0f;

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
  WiimoteFaceTex texture;
  float u0;
  float v0;
  float u1;
  float v1;
  float u2;
  float v2;
  float u3;
  float v3;
};

constexpr Face kFaces[6] = {
    // +Z front — u left→right, v bottom→top (matches PNG orientation)
    {4, 5, 6, 7, WiimoteFaceTex::Front, 0, 1, 1, 1, 1, 0, 0, 0},
    // -Z back — rotated 180° so D-pad / labels match front
    {1, 0, 3, 2, WiimoteFaceTex::Back, 1, 0, 0, 0, 0, 1, 1, 1},
    {5, 1, 2, 6, WiimoteFaceTex::None, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 4, 7, 3, WiimoteFaceTex::None, 0, 0, 0, 0, 0, 0, 0, 0},
    // +Y top — long edge runs front (+Z) to back (-Z)
    {7, 6, 2, 3, WiimoteFaceTex::Top, 0, 0, 1, 0, 1, 1, 0, 1},
    // -Y bottom — flipped v so underside reads correctly vs front
    {0, 1, 5, 4, WiimoteFaceTex::Bottom, 0, 1, 1, 1, 1, 0, 0, 0},
};

constexpr float kLightX = 0.35f;
constexpr float kLightY = 0.65f;
constexpr float kLightZ = 0.45f;

constexpr float kGroundHalfExtent = 36.0f;
constexpr float kGroundDrop = 14.0f;
constexpr uint8_t kGroundDivisions = 8;
constexpr uint16_t kGroundLineColor = 0x0320;
constexpr uint16_t kGroundEdgeColor = 0x04E0;

inline Projected projectPoint(int screenW, int screenH, const Vec3& v) {
  return {
      (int16_t)(screenW / 2 + v.x * kScale),
      (int16_t)(screenH / 2 - v.y * kScale),
      v.z,
  };
}

inline void groundTangentAxes(const orient::Vec3& upIn, orient::Vec3& outRight,
                              orient::Vec3& outForward) {
  orient::Vec3 up = orient::vec3Normalize(upIn);
  orient::Vec3 ref = {0.0f, 0.0f, 1.0f};
  orient::Vec3 right = {
      up.y * ref.z - up.z * ref.y,
      up.z * ref.x - up.x * ref.z,
      up.x * ref.y - up.y * ref.x,
  };
  float rightLen = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
  if (rightLen < 0.05f) {
    ref = {1.0f, 0.0f, 0.0f};
    right = {
        up.y * ref.z - up.z * ref.y,
        up.z * ref.x - up.x * ref.z,
        up.x * ref.y - up.y * ref.x,
    };
    rightLen = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
  }
  if (rightLen > 0.0001f) {
    right.x /= rightLen;
    right.y /= rightLen;
    right.z /= rightLen;
  } else {
    right = {1.0f, 0.0f, 0.0f};
  }

  orient::Vec3 forward = {
      right.y * up.z - right.z * up.y,
      right.z * up.x - right.x * up.z,
      right.x * up.y - right.y * up.x,
  };
  outRight = {right.x, right.y, right.z};
  outForward = {forward.x, forward.y, forward.z};
}

inline void drawGroundPlane(GFXcanvas16& canvas, int screenW, int screenH,
                            const orient::Vec3& gravityUp) {
  orient::Vec3 right{};
  orient::Vec3 forward{};
  groundTangentAxes(gravityUp, right, forward);

  const orient::Vec3 up = orient::vec3Normalize(gravityUp);
  const orient::Vec3 center = {
      -up.x * kGroundDrop,
      -up.y * kGroundDrop,
      -up.z * kGroundDrop,
  };

  Projected grid[kGroundDivisions + 1][kGroundDivisions + 1];
  for (uint8_t i = 0; i <= kGroundDivisions; ++i) {
    const float u = ((float)i / (float)kGroundDivisions) * 2.0f - 1.0f;
    for (uint8_t j = 0; j <= kGroundDivisions; ++j) {
      const float v = ((float)j / (float)kGroundDivisions) * 2.0f - 1.0f;
      const Vec3 p = {
          center.x + (right.x * u + forward.x * v) * kGroundHalfExtent,
          center.y + (right.y * u + forward.y * v) * kGroundHalfExtent,
          center.z + (right.z * u + forward.z * v) * kGroundHalfExtent,
      };
      grid[i][j] = projectPoint(screenW, screenH, p);
    }
  }

  for (uint8_t i = 0; i <= kGroundDivisions; ++i) {
    for (uint8_t j = 0; j < kGroundDivisions; ++j) {
      const uint16_t color =
          (i == 0 || i == kGroundDivisions || j == 0 || j == kGroundDivisions - 1)
              ? kGroundEdgeColor
              : kGroundLineColor;
      canvas.drawLine(grid[i][j].x, grid[i][j].y, grid[i][j + 1].x, grid[i][j + 1].y, color);
    }
  }
  for (uint8_t j = 0; j <= kGroundDivisions; ++j) {
    for (uint8_t i = 0; i < kGroundDivisions; ++i) {
      const uint16_t color =
          (i == 0 || i == kGroundDivisions - 1 || j == 0 || j == kGroundDivisions)
              ? kGroundEdgeColor
              : kGroundLineColor;
      canvas.drawLine(grid[i][j].x, grid[i][j].y, grid[i + 1][j].x, grid[i + 1][j].y, color);
    }
  }
}

inline Vec3 rotateByQuat(const orient::Quat& q, const Vec3& v) {
  const orient::Vec3 in = {v.x, v.y, v.z};
  const orient::Vec3 out = orient::quatRotate(q, in);
  return {out.x, out.y, out.z};
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

inline uint16_t shadeRgb565(uint16_t color, float brightness) {
  brightness = constrain(brightness, 0.22f, 1.0f);
  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5) & 0x3F;
  uint8_t b = color & 0x1F;
  r = (uint8_t)(r * brightness);
  g = (uint8_t)(g * brightness);
  b = (uint8_t)(b * brightness);
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

inline uint16_t sampleTexture(const WiimoteTexture& tex, float u, float v) {
  if (!tex.data || tex.width == 0 || tex.height == 0) {
    return ST77XX_WHITE;
  }
  int tx = (int)(u * (tex.width - 1) + 0.5f);
  int ty = (int)(v * (tex.height - 1) + 0.5f);
  tx = constrain(tx, 0, tex.width - 1);
  ty = constrain(ty, 0, tex.height - 1);
  return pgm_read_word(&tex.data[ty * tex.width + tx]);
}

inline void drawSolidTriangle(GFXcanvas16& canvas, int x0, int y0, int x1, int y1, int x2,
                              int y2, uint16_t color) {
  canvas.fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

inline void drawTexturedTriangle(GFXcanvas16& canvas, int x0, int y0, float u0, float v0, int x1,
                                 int y1, float u1, float v1, int x2, int y2, float u2, float v2,
                                 const WiimoteTexture& tex, float lighting) {
  if (!tex.data) {
    return;
  }

  const float denom = (float)(y1 - y2) * (float)(x0 - x2) + (float)(x2 - x1) * (float)(y0 - y2);
  if (fabsf(denom) < 0.001f) {
    return;
  }

  int minX = x0;
  if (x1 < minX) minX = x1;
  if (x2 < minX) minX = x2;
  int maxX = x0;
  if (x1 > maxX) maxX = x1;
  if (x2 > maxX) maxX = x2;
  int minY = y0;
  if (y1 < minY) minY = y1;
  if (y2 < minY) minY = y2;
  int maxY = y0;
  if (y1 > maxY) maxY = y1;
  if (y2 > maxY) maxY = y2;

  if (minX < 0) minX = 0;
  if (minY < 0) minY = 0;
  if (maxX >= canvas.width()) maxX = canvas.width() - 1;
  if (maxY >= canvas.height()) maxY = canvas.height() - 1;

  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const float w0 =
          ((float)(y1 - y2) * (float)(x - x2) + (float)(x2 - x1) * (float)(y - y2)) / denom;
      const float w1 =
          ((float)(y2 - y0) * (float)(x - x2) + (float)(x0 - x2) * (float)(y - y2)) / denom;
      const float w2 = 1.0f - w0 - w1;
      if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
        continue;
      }
      const float u = w0 * u0 + w1 * u1 + w2 * u2;
      const float v = w0 * v0 + w1 * v1 + w2 * v2;
      const uint16_t color = shadeRgb565(sampleTexture(tex, u, v), lighting);
      canvas.drawPixel(x, y, color);
    }
  }
}

inline void drawFace(GFXcanvas16& canvas, const Face& face, const Projected& p0,
                     const Projected& p1, const Projected& p2, const Projected& p3,
                     float lighting) {
  if (face.texture == WiimoteFaceTex::None) {
    const uint16_t color = shadedWhite(lighting);
    drawSolidTriangle(canvas, p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, color);
    drawSolidTriangle(canvas, p0.x, p0.y, p2.x, p2.y, p3.x, p3.y, color);
    return;
  }

  const WiimoteTexture tex = wiimoteTextureFor(face.texture);
  drawTexturedTriangle(canvas, p0.x, p0.y, face.u0, face.v0, p1.x, p1.y, face.u1, face.v1, p2.x,
                       p2.y, face.u2, face.v2, tex, lighting);
  drawTexturedTriangle(canvas, p0.x, p0.y, face.u0, face.v0, p2.x, p2.y, face.u2, face.v2, p3.x,
                       p3.y, face.u3, face.v3, tex, lighting);
}

struct FaceDraw {
  uint8_t index;
  float sortZ;
};

inline GFXcanvas16& frameBuffer() {
  static GFXcanvas16 canvas(240, 280);
  return canvas;
}

inline void drawPrism(Adafruit_ST7789& tft, int screenW, int screenH, const orient::Quat& orientation,
                      const orient::Vec3& gravityUp) {
  GFXcanvas16& canvas = frameBuffer();
  if (screenW != canvas.width() || screenH != canvas.height()) {
    return;
  }

  Vec3 world[8];
  Projected projected[8];
  for (uint8_t i = 0; i < 8; ++i) {
    const Vec3 v = rotateByQuat(orientation, kBaseVerts[i]);
    world[i] = v;
    projected[i] = projectPoint(screenW, screenH, v);
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
  drawGroundPlane(canvas, screenW, screenH, gravityUp);

  for (uint8_t o = 0; o < visibleCount; ++o) {
    const Face& face = kFaces[order[o].index];
    const Vec3 normal = faceNormal(world[face.i0], world[face.i1], world[face.i2]);
    const float lighting = normal.x * kLightX + normal.y * kLightY + normal.z * kLightZ;
    drawFace(canvas, face, projected[face.i0], projected[face.i1], projected[face.i2],
             projected[face.i3], lighting);
  }

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), screenW, screenH);
}

inline void drawAxisGizmo(GFXcanvas16& canvas, int screenW, int screenH, const orient::Quat& orientation) {
  const int cx = screenW / 2;
  const int cy = screenH / 2;
  constexpr float kAxisLen = 4.0f;
  const orient::Vec3 modelAxes[3] = {
      {kAxisLen, 0.0f, 0.0f},
      {0.0f, kAxisLen, 0.0f},
      {0.0f, 0.0f, kAxisLen},
  };
  const uint16_t colors[3] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE};
  const char labels[3] = {'R', 'P', 'Y'};

  for (uint8_t i = 0; i < 3; ++i) {
    const orient::Vec3 in = modelAxes[i];
    const orient::Vec3 out = orient::quatRotate(orientation, in);
    const int x1 = cx + (int)(out.x * kScale);
    const int y1 = cy - (int)(out.y * kScale);
    canvas.drawLine(cx, cy, x1, y1, colors[i]);
    canvas.setTextColor(colors[i]);
    canvas.setTextSize(1);
    canvas.setCursor(x1 + 2, y1 - 4);
    canvas.print(labels[i]);
  }
}

inline void drawPrismWithOverlay(Adafruit_ST7789& tft, int screenW, int screenH,
                                 const orient::Quat& orientation, const orient::Vec3& gravityUp,
                                 bool showAxes, uint8_t axisMapIndex, const char* axisMapLabel) {
  drawPrism(tft, screenW, screenH, orientation, gravityUp);
  if (!showAxes) {
    return;
  }

  GFXcanvas16& canvas = frameBuffer();
  drawAxisGizmo(canvas, screenW, screenH, orientation);

  canvas.setTextColor(ST77XX_WHITE);
  canvas.setTextSize(1);
  canvas.fillRect(0, 0, screenW, 10, ST77XX_BLACK);
  canvas.setCursor(4, 2);
  canvas.print(F("Map "));
  canvas.print(axisMapIndex);
  canvas.print(F(": "));
  canvas.print(axisMapLabel);
  canvas.print(F("  R/P/Y=RGB"));

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), screenW, screenH);
}

}  // namespace prism

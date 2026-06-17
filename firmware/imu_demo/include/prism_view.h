#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

#include "orientation.h"
#include "wiimote_textures.h"

// Wiimote-shaped box: 9 (X) x 8 (Y) x 36 (Z) — matches assets/*.png aspect ratios.
// Renders to an off-screen buffer each frame, then blits once (no partial-frame flicker).

namespace prism {

enum class SceneViewMode : uint8_t { GroundFixed, WiimoteFixed };

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

// Ground view: isometric camera (equal offset on X, Y, Z). Wiimote view: top head-on.
constexpr float kCamDistance = 95.0f;
constexpr float kIsoInvSqrt3 = 0.57735026919f;
constexpr float kTopCamDistance = 72.0f;
constexpr float kLookAtY = 5.0f;
constexpr float kWiimoteHoverY = 14.0f;

struct ViewFrame {
  orient::Vec3 right;
  orient::Vec3 up;
  orient::Vec3 forward;
  orient::Vec3 lookAt;
};

inline float vec3Dot(orient::Vec3 a, orient::Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline orient::Vec3 vec3Sub(orient::Vec3 a, orient::Vec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline orient::Vec3 vec3Cross(orient::Vec3 a, orient::Vec3 b) {
  return {
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
  };
}

inline orient::Vec3 vec3Scale(orient::Vec3 v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

inline orient::Vec3 vec3Add(orient::Vec3 a, orient::Vec3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline ViewFrame buildViewFrame(orient::Vec3 camPos, orient::Vec3 lookAt,
                                orient::Vec3 worldUpHint) {
  ViewFrame frame{};
  frame.lookAt = lookAt;

  orient::Vec3 toCam = vec3Sub(camPos, lookAt);
  const float toCamLen = sqrtf(vec3Dot(toCam, toCam));
  if (toCamLen > 0.0001f) {
    frame.forward = vec3Scale(toCam, 1.0f / toCamLen);
  } else {
    frame.forward = {0.0f, 0.0f, 1.0f};
  }

  orient::Vec3 right = vec3Cross(frame.forward, worldUpHint);
  float rightLen = sqrtf(vec3Dot(right, right));
  if (rightLen < 0.05f) {
    right = {1.0f, 0.0f, 0.0f};
    rightLen = 1.0f;
  }
  frame.right = vec3Scale(right, 1.0f / rightLen);
  frame.up = vec3Cross(frame.right, frame.forward);
  return frame;
}

inline ViewFrame makeIsometricCamera() {
  const orient::Vec3 lookAt = {0.0f, kLookAtY, 0.0f};
  const float d = kCamDistance * kIsoInvSqrt3;
  const orient::Vec3 camPos = {d, kLookAtY + d, d};
  constexpr orient::Vec3 kWorldUp = {0.0f, 1.0f, 0.0f};
  return buildViewFrame(camPos, lookAt, kWorldUp);
}

// Top texture head-on: +Y toward viewer, IMU +Y (left) toward screen left, IMU +X (front) up.
inline ViewFrame makeTopOnCamera() {
  const orient::Vec3 lookAt = {0.0f, kWiimoteHoverY, 0.0f};
  const orient::Vec3 camPos = {0.0f, kWiimoteHoverY + kTopCamDistance, 0.0f};
  constexpr orient::Vec3 kFrontUpHint = {0.0f, 0.0f, 1.0f};
  return buildViewFrame(camPos, lookAt, kFrontUpHint);
}

inline ViewFrame makeCameraForMode(SceneViewMode mode) {
  return (mode == SceneViewMode::WiimoteFixed) ? makeTopOnCamera() : makeIsometricCamera();
}

inline Vec3 worldToView(const ViewFrame& frame, orient::Vec3 world) {
  const orient::Vec3 rel = vec3Sub(world, frame.lookAt);
  return {
      vec3Dot(rel, frame.right),
      vec3Dot(rel, frame.up),
      vec3Dot(rel, frame.forward),
  };
}

inline Projected projectViewPoint(int screenW, int screenH, const Vec3& view) {
  return {
      (int16_t)(screenW / 2 + view.x * kScale),
      (int16_t)(screenH / 2 - view.y * kScale),
      view.z,
  };
}

inline orient::Vec3 modelPointToWorld(const orient::Quat& qWorld, orient::Vec3 modelPoint) {
  const orient::Vec3 rotated = orient::quatRotate(qWorld, modelPoint);
  return {rotated.x, kWiimoteHoverY + rotated.y, rotated.z};
}

inline orient::Vec3 normalModelToWorld(const orient::Quat& qWorld, const Vec3& modelNormal) {
  const orient::Vec3 n = orient::quatRotate(qWorld, {modelNormal.x, modelNormal.y, modelNormal.z});
  return n;
}

inline orient::Vec3 normalWorldToView(const ViewFrame& frame, orient::Vec3 worldNormal) {
  return {
      vec3Dot(worldNormal, frame.right),
      vec3Dot(worldNormal, frame.up),
      vec3Dot(worldNormal, frame.forward),
  };
}

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
    {4, 5, 6, 7, WiimoteFaceTex::Front, 0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 3, 2, WiimoteFaceTex::Back, 1, 0, 0, 0, 0, 1, 1, 1},
    {5, 1, 2, 6, WiimoteFaceTex::None, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 4, 7, 3, WiimoteFaceTex::None, 0, 0, 0, 0, 0, 0, 0, 0},
    {7, 6, 2, 3, WiimoteFaceTex::Top, 0, 0, 1, 0, 1, 1, 0, 1},
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

inline void groundTangentAxes(const orient::Vec3& upIn, orient::Vec3& outRight,
                              orient::Vec3& outForward) {
  orient::Vec3 up = orient::vec3Normalize(upIn);
  orient::Vec3 ref = {0.0f, 0.0f, 1.0f};
  orient::Vec3 right = vec3Cross(up, ref);
  float rightLen = sqrtf(vec3Dot(right, right));
  if (rightLen < 0.05f) {
    ref = {1.0f, 0.0f, 0.0f};
    right = vec3Cross(up, ref);
    rightLen = sqrtf(vec3Dot(right, right));
  }
  if (rightLen > 0.0001f) {
    outRight = vec3Scale(right, 1.0f / rightLen);
  } else {
    outRight = {1.0f, 0.0f, 0.0f};
  }

  const orient::Vec3 forward = vec3Cross(outRight, up);
  outForward = forward;
}

inline void drawGroundPlaneFlat(GFXcanvas16& canvas, int screenW, int screenH, const ViewFrame& camera) {
  Projected grid[kGroundDivisions + 1][kGroundDivisions + 1];
  for (uint8_t i = 0; i <= kGroundDivisions; ++i) {
    const float u = ((float)i / (float)kGroundDivisions) * 2.0f - 1.0f;
    for (uint8_t j = 0; j <= kGroundDivisions; ++j) {
      const float v = ((float)j / (float)kGroundDivisions) * 2.0f - 1.0f;
      const orient::Vec3 world = {u * kGroundHalfExtent, 0.0f, v * kGroundHalfExtent};
      grid[i][j] = projectViewPoint(screenW, screenH, worldToView(camera, world));
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

inline void drawGroundPlaneGravity(GFXcanvas16& canvas, int screenW, int screenH,
                                   const ViewFrame& camera, const orient::Vec3& gravityUp) {
  orient::Vec3 right{};
  orient::Vec3 forward{};
  groundTangentAxes(gravityUp, right, forward);
  const orient::Vec3 up = orient::vec3Normalize(gravityUp);
  const orient::Vec3 wiimotePos = {0.0f, kWiimoteHoverY, 0.0f};
  const orient::Vec3 center = vec3Sub(wiimotePos, vec3Scale(up, kGroundDrop));

  Projected grid[kGroundDivisions + 1][kGroundDivisions + 1];
  for (uint8_t i = 0; i <= kGroundDivisions; ++i) {
    const float u = ((float)i / (float)kGroundDivisions) * 2.0f - 1.0f;
    for (uint8_t j = 0; j <= kGroundDivisions; ++j) {
      const float v = ((float)j / (float)kGroundDivisions) * 2.0f - 1.0f;
      const orient::Vec3 world = vec3Add(
          center, vec3Add(vec3Scale(right, u * kGroundHalfExtent),
                          vec3Scale(forward, v * kGroundHalfExtent)));
      grid[i][j] = projectViewPoint(screenW, screenH, worldToView(camera, world));
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

inline void drawModeLabel(GFXcanvas16& canvas, SceneViewMode mode) {
  canvas.setTextColor(ST77XX_WHITE);
  canvas.setTextSize(1);
  canvas.fillRect(0, 0, 72, 10, ST77XX_BLACK);
  canvas.setCursor(4, 2);
  if (mode == SceneViewMode::GroundFixed) {
    canvas.print(F("Ground view"));
  } else {
    canvas.print(F("Wiimote view"));
  }
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

inline void drawPrism(Adafruit_ST7789& tft, int screenW, int screenH, SceneViewMode mode,
                      const orient::Quat& qWorldWiimote, const orient::Vec3& gravityUp) {
  GFXcanvas16& canvas = frameBuffer();
  if (screenW != canvas.width() || screenH != canvas.height()) {
    return;
  }

  const ViewFrame camera = makeCameraForMode(mode);
  const orient::Quat qDraw =
      (mode == SceneViewMode::WiimoteFixed) ? orient::quatIdentity() : qWorldWiimote;

  Projected projected[8];
  for (uint8_t i = 0; i < 8; ++i) {
    const orient::Vec3 model = {kBaseVerts[i].x, kBaseVerts[i].y, kBaseVerts[i].z};
    const orient::Vec3 w = modelPointToWorld(qDraw, model);
    projected[i] = projectViewPoint(screenW, screenH, worldToView(camera, w));
  }

  FaceDraw order[6];
  uint8_t visibleCount = 0;
  for (uint8_t f = 0; f < 6; ++f) {
    const Face& face = kFaces[f];
    const Vec3 modelNormal =
        faceNormal(kBaseVerts[face.i0], kBaseVerts[face.i1], kBaseVerts[face.i2]);
    const orient::Vec3 worldNormal =
        normalModelToWorld(qDraw, {modelNormal.x, modelNormal.y, modelNormal.z});
    const orient::Vec3 viewNormal = normalWorldToView(camera, worldNormal);
    if (viewNormal.z <= 0.0f) {
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
  if (mode == SceneViewMode::GroundFixed) {
    drawGroundPlaneFlat(canvas, screenW, screenH, camera);
  } else {
    drawGroundPlaneGravity(canvas, screenW, screenH, camera, gravityUp);
  }

  for (uint8_t o = 0; o < visibleCount; ++o) {
    const Face& face = kFaces[order[o].index];
    const Vec3 modelNormal =
        faceNormal(kBaseVerts[face.i0], kBaseVerts[face.i1], kBaseVerts[face.i2]);
    const orient::Vec3 worldNormal =
        normalModelToWorld(qDraw, {modelNormal.x, modelNormal.y, modelNormal.z});
    const float lighting = worldNormal.x * kLightX + worldNormal.y * kLightY + worldNormal.z * kLightZ;
    drawFace(canvas, face, projected[face.i0], projected[face.i1], projected[face.i2],
             projected[face.i3], lighting);
  }

  drawModeLabel(canvas, mode);
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), screenW, screenH);
}

inline void drawAxisGizmo(GFXcanvas16& canvas, int screenW, int screenH, const ViewFrame& camera,
                          const orient::Quat& qWorldWiimote) {
  constexpr float kAxisLen = 4.0f;
  const orient::Vec3 modelCenter = {0.0f, 0.0f, 0.0f};
  const orient::Vec3 worldCenter = modelPointToWorld(qWorldWiimote, modelCenter);
  const Vec3 viewCenter = worldToView(camera, worldCenter);
  const Projected center = projectViewPoint(screenW, screenH, viewCenter);

  const orient::Vec3 modelAxes[3] = {
      {kAxisLen, 0.0f, 0.0f},
      {0.0f, kAxisLen, 0.0f},
      {0.0f, 0.0f, kAxisLen},
  };
  const uint16_t colors[3] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE};
  const char labels[3] = {'R', 'P', 'Y'};

  for (uint8_t i = 0; i < 3; ++i) {
    const orient::Vec3 worldTip = modelPointToWorld(qWorldWiimote, modelAxes[i]);
    const Projected tip = projectViewPoint(screenW, screenH, worldToView(camera, worldTip));
    canvas.drawLine(center.x, center.y, tip.x, tip.y, colors[i]);
    canvas.setTextColor(colors[i]);
    canvas.setTextSize(1);
    canvas.setCursor(tip.x + 2, tip.y - 4);
    canvas.print(labels[i]);
  }
}

inline void drawPrismWithOverlay(Adafruit_ST7789& tft, int screenW, int screenH, SceneViewMode mode,
                                 const orient::Quat& qWorldWiimote, const orient::Vec3& gravityUp,
                                 bool showAxes, uint8_t axisMapIndex, const char* axisMapLabel) {
  drawPrism(tft, screenW, screenH, mode, qWorldWiimote, gravityUp);
  if (!showAxes) {
    return;
  }

  GFXcanvas16& canvas = frameBuffer();
  const ViewFrame camera = makeCameraForMode(mode);
  const orient::Quat qDraw =
      (mode == SceneViewMode::WiimoteFixed) ? orient::quatIdentity() : qWorldWiimote;
  drawAxisGizmo(canvas, screenW, screenH, camera, qDraw);

  canvas.setTextColor(ST77XX_WHITE);
  canvas.setTextSize(1);
  canvas.fillRect(0, 12, screenW, 10, ST77XX_BLACK);
  canvas.setCursor(4, 14);
  canvas.print(F("Map "));
  canvas.print(axisMapIndex);
  canvas.print(F(": "));
  canvas.print(axisMapLabel);
  canvas.print(F("  R/P/Y=RGB"));

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), screenW, screenH);
}

}  // namespace prism

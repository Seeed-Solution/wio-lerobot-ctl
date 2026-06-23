#pragma once

#include <TFT_eSPI.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Planar side-view kinematics for SO-101 (scaled from so101.urdf link spans).
// Servo neutral (bambot home) = 180 deg on all joints.

// Home pose: all servos at 180 deg (tick ~2048). Side view extends up and slightly right.
static constexpr float ARM_HOME_TILT = 0.55f;

struct ArmVizLayout {
  int originX = 44;
  int originY = 163;
  int linkUpper = 58;
  int linkLower = 68;
  int linkWrist = 46;
  int jawLen = 22;
  int baseRadius = 12;
  int jointRadius = 7;
  int yawRadius = 28;
  int lineUpper = 7;
  int lineLower = 6;
  int lineWrist = 5;
  bool mirrorX = false;
};

static constexpr int ARM_HOME_TICK = 2048;

// SO-101 at servo home (180 deg) is not a flat line in 3D. Side-view 2D needs a
// fixed silhouette offset so init/home still reads as a bent arm, not one segment.
static constexpr float ARM_SIDE_VIEW_OFFSET_DEG[6] = {
    0.0f,   // Rotation
    14.0f,  // Pitch
    34.0f,  // Elbow
   -24.0f,  // Wrist_Pitch
    0.0f,   // Wrist_Roll
   -10.0f,  // Jaw
};

inline float degreesToRad(float degrees) {
  return degrees * (PI / 180.0f);
}

inline float servoTicksToRad(int ticks) {
  const float deg = (static_cast<float>(ticks) * 360.0f) / 4096.0f;
  return (deg - 180.0f) * (PI / 180.0f);
}

struct ArmPoint {
  int x;
  int y;
};

struct ArmSkeleton {
  ArmPoint base;
  ArmPoint shoulder;
  ArmPoint elbow;
  ArmPoint wrist;
  ArmPoint tip;
  ArmPoint jawA;
  ArmPoint jawB;
  int yawHeadX;
  int yawHeadY;
  float wristAngle;
  float jawAngle;
  float yawAngle;
};

inline ArmSkeleton computeSkeleton(const int jointTicks[6], const ArmVizLayout& layout) {
  const float pitch = servoTicksToRad(jointTicks[1]) + degreesToRad(ARM_SIDE_VIEW_OFFSET_DEG[1]);
  const float elbow = servoTicksToRad(jointTicks[2]) + degreesToRad(ARM_SIDE_VIEW_OFFSET_DEG[2]);
  const float wristPitch = servoTicksToRad(jointTicks[3]) + degreesToRad(ARM_SIDE_VIEW_OFFSET_DEG[3]);
  const float wristRoll = servoTicksToRad(jointTicks[4]) + degreesToRad(ARM_SIDE_VIEW_OFFSET_DEG[4]);
  const float jaw = servoTicksToRad(jointTicks[5]) + degreesToRad(ARM_SIDE_VIEW_OFFSET_DEG[5]);
  const float yaw = servoTicksToRad(jointTicks[0]);

  // Screen coords: y grows downward. Neutral pose points up-right into the canvas.
  const float shoulderAngle = (-PI / 2.0f) + ARM_HOME_TILT + pitch;
  const float elbowAngle = shoulderAngle + elbow;
  const float wristAngle = elbowAngle + wristPitch;

  ArmSkeleton sk;
  sk.base.x = layout.originX;
  sk.base.y = layout.originY;
  sk.wristAngle = wristAngle + wristRoll;
  sk.jawAngle = jaw;
  sk.yawAngle = yaw;

  auto polar = [](int x0, int y0, int length, float angle, ArmPoint& out) {
    out.x = x0 + static_cast<int>(lroundf(cosf(angle) * length));
    out.y = y0 + static_cast<int>(lroundf(sinf(angle) * length));
  };

  sk.shoulder = sk.base;
  polar(sk.shoulder.x, sk.shoulder.y, layout.linkUpper, shoulderAngle, sk.elbow);
  polar(sk.elbow.x, sk.elbow.y, layout.linkLower, elbowAngle, sk.wrist);
  polar(sk.wrist.x, sk.wrist.y, layout.linkWrist, wristAngle, sk.tip);

  if (fabsf(yaw) > 0.01f) {
    const float scale = 0.55f + 0.45f * cosf(yaw);
    sk.elbow.x = sk.base.x + static_cast<int>((sk.elbow.x - sk.base.x) * scale);
    sk.wrist.x = sk.base.x + static_cast<int>((sk.wrist.x - sk.base.x) * scale);
    sk.tip.x = sk.base.x + static_cast<int>((sk.tip.x - sk.base.x) * scale);
  }

  const float open = sk.jawAngle * 0.9f;
  const float spread = 0.35f + fabsf(open) * 0.8f;
  polar(sk.tip.x, sk.tip.y, layout.jawLen, sk.wristAngle + spread, sk.jawA);
  polar(sk.tip.x, sk.tip.y, layout.jawLen, sk.wristAngle - spread, sk.jawB);

  sk.yawHeadX = sk.base.x + static_cast<int>(sinf(sk.yawAngle) * layout.yawRadius);
  sk.yawHeadY = sk.base.y + static_cast<int>(cosf(sk.yawAngle) * layout.yawRadius);

  if (layout.mirrorX) {
    auto mirrorX = [&](int x) { return 2 * layout.originX - x; };
    sk.elbow.x = mirrorX(sk.elbow.x);
    sk.wrist.x = mirrorX(sk.wrist.x);
    sk.tip.x = mirrorX(sk.tip.x);
    sk.jawA.x = mirrorX(sk.jawA.x);
    sk.jawB.x = mirrorX(sk.jawB.x);
    sk.yawHeadX = mirrorX(sk.yawHeadX);
  }

  return sk;
}

inline void expandBounds(int x, int y, int pad, int& x0, int& y0, int& x1, int& y1) {
  const int px = x - pad;
  const int py = y - pad;
  if (x0 > px) {
    x0 = px;
  }
  if (y0 > py) {
    y0 = py;
  }
  if (x1 < x + pad) {
    x1 = x + pad;
  }
  if (y1 < y + pad) {
    y1 = y + pad;
  }
}

inline void skeletonBounds(const ArmSkeleton& sk, const ArmVizLayout& layout, int pad, int& x0, int& y0, int& x1, int& y1) {
  x0 = sk.base.x;
  y0 = sk.base.y;
  x1 = sk.base.x;
  y1 = sk.base.y;

  const ArmPoint pts[] = {sk.elbow, sk.wrist, sk.tip, sk.jawA, sk.jawB};
  for (const ArmPoint& p : pts) {
    expandBounds(p.x, p.y, pad, x0, y0, x1, y1);
  }

  expandBounds(sk.yawHeadX, sk.yawHeadY, pad + 2, x0, y0, x1, y1);
  expandBounds(sk.base.x, sk.base.y, layout.yawRadius + pad, x0, y0, x1, y1);
  expandBounds(sk.base.x, sk.base.y, layout.baseRadius + pad + 6, x0, y0, x1, y1);
}

inline void clampBounds(int canvasX, int canvasY, int canvasW, int canvasH, int& x0, int& y0, int& x1, int& y1) {
  if (x0 < canvasX) {
    x0 = canvasX;
  }
  if (y0 < canvasY) {
    y0 = canvasY;
  }
  if (x1 >= canvasX + canvasW) {
    x1 = canvasX + canvasW - 1;
  }
  if (y1 >= canvasY + canvasH) {
    y1 = canvasY + canvasH - 1;
  }
}

inline void drawWideLine(TFT_eSPI& tft, int x0, int y0, int x1, int y1, uint16_t color, uint8_t thickness) {
  if (thickness <= 1) {
    tft.drawLine(x0, y0, x1, y1, color);
    return;
  }
  const int half = thickness / 2;
  for (int dx = -half; dx <= half; dx++) {
    for (int dy = -half; dy <= half; dy++) {
      tft.drawLine(x0 + dx, y0 + dy, x1 + dx, y1 + dy, color);
    }
  }
}

inline void drawYawIndicator(TFT_eSPI& tft, const ArmSkeleton& sk, const ArmVizLayout& layout, uint16_t color) {
  tft.drawCircle(sk.base.x, sk.base.y, layout.yawRadius, tft.color565(40, 40, 40));
  drawWideLine(tft, sk.base.x, sk.base.y, sk.yawHeadX, sk.yawHeadY, color, 2);
  tft.fillCircle(sk.yawHeadX, sk.yawHeadY, 4, color);
}

inline void drawJaw(TFT_eSPI& tft, const ArmSkeleton& sk, uint16_t color) {
  drawWideLine(tft, sk.tip.x, sk.tip.y, sk.jawA.x, sk.jawA.y, color, 3);
  drawWideLine(tft, sk.tip.x, sk.tip.y, sk.jawB.x, sk.jawB.y, color, 3);
}

inline void drawArmSkeleton(
    TFT_eSPI& tft,
    const ArmSkeleton& sk,
    int selectedServoId,
    const ArmVizLayout& layout) {
  const uint16_t linkColor = tft.color565(210, 210, 220);
  const uint16_t baseColor = tft.color565(30, 130, 45);
  const uint16_t jointColor = tft.color565(255, 210, 60);
  const uint16_t selectedColor = tft.color565(255, 70, 70);
  const uint16_t jawColor = tft.color565(80, 200, 255);
  const uint16_t yawColor = tft.color565(120, 160, 255);

  drawYawIndicator(tft, sk, layout, yawColor);

  drawWideLine(tft, sk.shoulder.x, sk.shoulder.y, sk.elbow.x, sk.elbow.y, linkColor, layout.lineUpper);
  drawWideLine(tft, sk.elbow.x, sk.elbow.y, sk.wrist.x, sk.wrist.y, linkColor, layout.lineLower);
  drawWideLine(tft, sk.wrist.x, sk.wrist.y, sk.tip.x, sk.tip.y, linkColor, layout.lineWrist);

  tft.fillCircle(sk.base.x, sk.base.y, layout.baseRadius, baseColor);
  drawJaw(tft, sk, jawColor);

  const ArmPoint joints[4] = {sk.shoulder, sk.elbow, sk.wrist, sk.tip};
  const int jointIds[4] = {2, 3, 4, 5};
  for (int i = 0; i < 4; i++) {
    const bool selected = jointIds[i] == selectedServoId;
    if (selected) {
      tft.drawCircle(joints[i].x, joints[i].y, layout.jointRadius + 4, selectedColor);
    }
    tft.fillCircle(joints[i].x, joints[i].y, layout.jointRadius, selected ? selectedColor : jointColor);
  }

  if (selectedServoId == 1 || selectedServoId == 2) {
    tft.drawCircle(sk.base.x, sk.base.y, layout.baseRadius + 5, selectedColor);
  }
  if (selectedServoId == 6) {
    tft.drawCircle(sk.tip.x, sk.tip.y, layout.jointRadius + 7, selectedColor);
  }
}

inline void drawArmSkeleton(
    TFT_eSPI& tft,
    const int jointTicks[6],
    int selectedServoId,
    const ArmVizLayout& layout) {
  drawArmSkeleton(tft, computeSkeleton(jointTicks, layout), selectedServoId, layout);
}

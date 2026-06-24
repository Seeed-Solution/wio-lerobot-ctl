// Wio Terminal SO-101 controller - PC hub mode.
//
// Wio USB -> Mac/PC bridge -> arm USB (feetech.js / 1Mbps)
//
// Controls:
//   L/R move joint   A cycle joint   Press fine/coarse   B E-stop   C rescan

#include <TFT_eSPI.h>
#include "arm_viz.h"

static constexpr uint16_t HOST_BAUD = 115200;
static constexpr uint16_t UPDATE_INTERVAL_MS = 8;
static constexpr float FINE_STEP_DEGREES = 3.0f;
static constexpr float COARSE_STEP_DEGREES = 12.0f;
static constexpr unsigned long PC_LINK_TIMEOUT_MS = 3000;

struct JointInfo {
  uint8_t id;
  const char* name;
};

static const JointInfo JOINTS[] = {
    {1, "Rotation"},
    {2, "Pitch"},
    {3, "Elbow"},
    {4, "Wrist_P"},
    {5, "Wrist_R"},
    {6, "Jaw"},
};
static constexpr int NUM_JOINTS = sizeof(JOINTS) / sizeof(JOINTS[0]);

static constexpr int VIZ_TOP = 20;
static constexpr int VIZ_HEIGHT = 166;
static constexpr int PANEL_TOP = 188;

static constexpr int ARM_CANVAS_X = 2;
static constexpr int ARM_CANVAS_Y = VIZ_TOP + 1;
static constexpr int ARM_CANVAS_W = 316;
static constexpr int ARM_CANVAS_H = VIZ_HEIGHT - 2;
static constexpr int ARM_BOUNDS_PAD = 10;

static constexpr int BANNER_Y = PANEL_TOP;
static constexpr int BANNER_H = 18;
static constexpr int INFO_Y = PANEL_TOP + 20;
static constexpr int INFO_H = 14;
static constexpr int STATUS_Y = PANEL_TOP + 36;
static constexpr int STATUS_H = 14;

TFT_eSPI tft;
ArmVizLayout armLayout;

int jointPositions[NUM_JOINTS] = {0};
bool jointOnline[NUM_JOINTS] = {false};
int selectedJoint = 0;
bool torqueEnabled = false;
bool fineStep = true;
bool pcLinked = false;

char statusLine[48] = "Waiting for PC bridge";
char hostLineBuffer[96];
uint8_t hostLineLength = 0;

unsigned long lastControlMs = 0;
unsigned long lastDrawMs = 0;
unsigned long lastHostRxMs = 0;

bool uiStaticDrawn = false;
ArmSkeleton lastArmSk;
bool hasLastArmSk = false;
int lastDrawnPositions[NUM_JOINTS] = {0};
int lastDrawnSelected = -1;
int lastDrawnSelectedTick = -1;
bool lastDrawnTorque = false;
bool lastDrawnFineStep = true;
bool lastDrawnPcLinked = false;
char lastDrawnStatus[48] = "";
bool forceFullArmRedraw = true;
bool vizDirty = false;

bool lastKeyA = false;
bool lastKeyB = false;
bool lastKeyC = false;
bool lastJoyPress = false;

int clampTicks(int ticks) {
  if (ticks < 0) return 0;
  if (ticks > 4095) return 4095;
  return ticks;
}

float ticksToDegrees(int ticks) {
  return (static_cast<float>(ticks) * 360.0f) / 4096.0f;
}

float currentStepDegrees() {
  return fineStep ? FINE_STEP_DEGREES : COARSE_STEP_DEGREES;
}

void setStatus(const char* message) {
  strncpy(statusLine, message, sizeof(statusLine) - 1);
  statusLine[sizeof(statusLine) - 1] = '\0';
}

void markVizDirty() { vizDirty = true; }

void requestArmRedraw() {
  forceFullArmRedraw = true;
  markVizDirty();
}

int degreesToTickDelta(float degrees) {
  return static_cast<int>(lroundf((degrees * 4096.0f) / 360.0f));
}

void applyHomePoseForViz() {
  for (int i = 0; i < NUM_JOINTS; i++) {
    jointPositions[i] = ARM_HOME_TICK;
    jointOnline[i] = false;
  }
  requestArmRedraw();
}

void initArmLayout() {
  armLayout.originX = ARM_CANVAS_X + ARM_CANVAS_W / 2;
  armLayout.originY = ARM_CANVAS_Y + ARM_CANVAS_H - 22;
  armLayout.mirrorX = false;
  armLayout.linkUpper = 58;
  armLayout.linkLower = 68;
  armLayout.linkWrist = 46;
  armLayout.jawLen = 22;
  armLayout.baseRadius = 12;
  armLayout.jointRadius = 7;
  armLayout.yawRadius = 28;
}

void sendHostLine(const char* line) {
  Serial.println(line);
}

void sendMoveCommand(int jointIndex, float deltaDegrees) {
  char buf[32];
  snprintf(buf, sizeof(buf), "M %u %+.2f", JOINTS[jointIndex].id, deltaDegrees);
  sendHostLine(buf);

  const int deltaTicks = degreesToTickDelta(deltaDegrees);
  if (deltaTicks != 0) {
    jointPositions[jointIndex] = clampTicks(jointPositions[jointIndex] + deltaTicks);
    jointOnline[jointIndex] = true;
    markVizDirty();
  }
}

void updatePcLinkState() {
  const bool wasLinked = pcLinked;
  pcLinked = (millis() - lastHostRxMs) < PC_LINK_TIMEOUT_MS;
  if (wasLinked && !pcLinked) {
    setStatus("PC bridge offline");
    markVizDirty();
  }
}

void handleHostPositions(const char* args) {
  int values[NUM_JOINTS];
  int count = 0;
  bool changed = false;
  const char* cursor = args;

  while (*cursor && count < NUM_JOINTS) {
    while (*cursor == ' ') cursor++;
    if (!*cursor) break;
    values[count++] = atoi(cursor);
    while (*cursor && *cursor != ' ') cursor++;
  }

  for (int i = 0; i < NUM_JOINTS; i++) {
    if (i < count) {
      const int next = clampTicks(values[i]);
      if (jointPositions[i] != next) {
        jointPositions[i] = next;
        changed = true;
      }
      jointOnline[i] = true;
    } else if (jointOnline[i]) {
      jointOnline[i] = false;
      changed = true;
    }
  }
  if (changed) {
    markVizDirty();
  }
}

void handleHostLine(const char* line) {
  lastHostRxMs = millis();
  pcLinked = true;

  if (line[0] == 'P' && line[1] == ' ') {
    handleHostPositions(line + 2);
    return;
  }
  if (line[0] == 'O' && line[1] == ' ') {
    return;
  }
  if (line[0] == 'T' && line[1] == ' ') {
    const bool nextTorque = (line[2] == '1');
    if (torqueEnabled != nextTorque) {
      torqueEnabled = nextTorque;
      markVizDirty();
    }
    return;
  }
  if (line[0] == 'S' && line[1] == 'T' && line[2] == ' ') {
    setStatus(line + 3);
    markVizDirty();
  }
}

void pollHostSerial() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      hostLineBuffer[hostLineLength] = '\0';
      if (hostLineLength > 0) {
        handleHostLine(hostLineBuffer);
      }
      hostLineLength = 0;
      continue;
    }
    if (hostLineLength + 1 < sizeof(hostLineBuffer)) {
      hostLineBuffer[hostLineLength++] = ch;
    }
  }
}

bool jointPositionsChanged() {
  for (int i = 0; i < NUM_JOINTS; i++) {
    if (jointPositions[i] != lastDrawnPositions[i]) {
      return true;
    }
  }
  return false;
}

void snapshotDrawnState() {
  memcpy(lastDrawnPositions, jointPositions, sizeof(lastDrawnPositions));
  lastDrawnSelected = selectedJoint;
  lastDrawnSelectedTick = jointOnline[selectedJoint] ? jointPositions[selectedJoint] : -1;
  lastDrawnTorque = torqueEnabled;
  lastDrawnFineStep = fineStep;
  lastDrawnPcLinked = pcLinked;
  strncpy(lastDrawnStatus, statusLine, sizeof(lastDrawnStatus));
  lastDrawnStatus[sizeof(lastDrawnStatus) - 1] = '\0';
}

void drawStaticChrome() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(4, 2);
  tft.print("SO-101");

  tft.setTextSize(1);
  tft.setTextColor(tft.color565(120, 120, 120), TFT_BLACK);
  tft.setCursor(78, 10);
  tft.print("PC hub");

  tft.drawRect(0, VIZ_TOP - 1, 320, VIZ_HEIGHT + 2, tft.color565(45, 45, 45));
  tft.drawFastHLine(0, PANEL_TOP - 1, 320, tft.color565(45, 45, 45));
  tft.fillRect(ARM_CANVAS_X, ARM_CANVAS_Y, ARM_CANVAS_W, ARM_CANVAS_H, TFT_BLACK);

  tft.setCursor(4, PANEL_TOP + 52);
  tft.setTextColor(tft.color565(110, 110, 110), TFT_BLACK);
  tft.print("L/R  A  Press | B stop C sync");

  uiStaticDrawn = true;
  forceFullArmRedraw = true;
}

void mergeBounds(int ax0, int ay0, int ax1, int ay1, int& x0, int& y0, int& x1, int& y1) {
  if (ax0 < x0) x0 = ax0;
  if (ay0 < y0) y0 = ay0;
  if (ax1 > x1) x1 = ax1;
  if (ay1 > y1) y1 = ay1;
}

void redrawArmPartial() {
  const ArmSkeleton sk = computeSkeleton(jointPositions, armLayout);
  const int selectedServoId = JOINTS[selectedJoint].id;

  if (forceFullArmRedraw || !hasLastArmSk) {
    tft.fillRect(ARM_CANVAS_X, ARM_CANVAS_Y, ARM_CANVAS_W, ARM_CANVAS_H, TFT_BLACK);
  } else {
    int x0 = ARM_CANVAS_X + ARM_CANVAS_W;
    int y0 = ARM_CANVAS_Y + ARM_CANVAS_H;
    int x1 = ARM_CANVAS_X;
    int y1 = ARM_CANVAS_Y;

    int nx0, ny0, nx1, ny1;
    skeletonBounds(sk, armLayout, ARM_BOUNDS_PAD, nx0, ny0, nx1, ny1);
    clampBounds(ARM_CANVAS_X, ARM_CANVAS_Y, ARM_CANVAS_W, ARM_CANVAS_H, nx0, ny0, nx1, ny1);
    mergeBounds(nx0, ny0, nx1, ny1, x0, y0, x1, y1);

    int lx0, ly0, lx1, ly1;
    skeletonBounds(lastArmSk, armLayout, ARM_BOUNDS_PAD, lx0, ly0, lx1, ly1);
    clampBounds(ARM_CANVAS_X, ARM_CANVAS_Y, ARM_CANVAS_W, ARM_CANVAS_H, lx0, ly0, lx1, ly1);
    mergeBounds(lx0, ly0, lx1, ly1, x0, y0, x1, y1);

    tft.fillRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, TFT_BLACK);
  }

  drawArmSkeleton(tft, sk, selectedServoId, armLayout);
  lastArmSk = sk;
  hasLastArmSk = true;
  forceFullArmRedraw = false;
}

void redrawBannerPartial() {
  const uint16_t bannerColor = torqueEnabled ? tft.color565(20, 90, 35) : tft.color565(120, 20, 20);
  tft.fillRect(0, BANNER_Y, 320, BANNER_H, bannerColor);
  tft.setTextColor(TFT_WHITE, bannerColor);
  tft.setTextSize(1);
  tft.setCursor(4, BANNER_Y + 5);
  tft.print(torqueEnabled ? "READY" : "E-STOP");
  tft.print("  ");
  tft.print(JOINTS[selectedJoint].name);
  tft.print("  ");
  if (jointOnline[selectedJoint]) {
    tft.print(ticksToDegrees(jointPositions[selectedJoint]), 1);
    tft.print(" deg   ");
  } else {
    tft.print("offline     ");
  }
}

void redrawInfoPartial() {
  tft.fillRect(0, INFO_Y, 320, INFO_H, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(4, INFO_Y + 2);
  tft.print("Step ");
  tft.print(currentStepDegrees(), 2);
  tft.print(fineStep ? " fine" : " coarse");
  tft.print(pcLinked ? "  PC ok" : "  PC --");
}

void redrawStatusPartial() {
  tft.fillRect(0, STATUS_Y, 320, STATUS_H, TFT_BLACK);
  tft.setTextColor(tft.color565(180, 180, 180), TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(4, STATUS_Y + 2);
  tft.print(statusLine);
}

void updateDisplay() {
  if (!uiStaticDrawn) {
    drawStaticChrome();
  }

  const bool armChanged =
      forceFullArmRedraw || !hasLastArmSk || jointPositionsChanged() ||
      selectedJoint != lastDrawnSelected;
  const bool bannerChanged =
      torqueEnabled != lastDrawnTorque || selectedJoint != lastDrawnSelected ||
      (jointOnline[selectedJoint] ? jointPositions[selectedJoint] : -1) != lastDrawnSelectedTick;
  const bool infoChanged =
      fineStep != lastDrawnFineStep || selectedJoint != lastDrawnSelected ||
      pcLinked != lastDrawnPcLinked ||
      (jointOnline[selectedJoint] ? jointPositions[selectedJoint] : -1) != lastDrawnSelectedTick;
  const bool statusChanged = strcmp(statusLine, lastDrawnStatus) != 0;

  if (armChanged) redrawArmPartial();
  if (bannerChanged) redrawBannerPartial();
  if (infoChanged) redrawInfoPartial();
  if (statusChanged) redrawStatusPartial();

  if (armChanged || bannerChanged || infoChanged || statusChanged) {
    snapshotDrawnState();
  }
}

bool readSwitchPressed(int pin, bool& wasPressed) {
  const bool isPressed = digitalRead(pin) == LOW;
  const bool edge = isPressed && !wasPressed;
  wasPressed = isPressed;
  return edge;
}

void handleButtons() {
  if (readSwitchPressed(WIO_KEY_A, lastKeyA)) {
    selectedJoint = (selectedJoint + 1) % NUM_JOINTS;
    markVizDirty();
  }

  if (readSwitchPressed(WIO_KEY_B, lastKeyB)) {
    sendHostLine("E");
  }

  if (readSwitchPressed(WIO_KEY_C, lastKeyC)) {
    sendHostLine("R");
  }

  if (readSwitchPressed(WIO_5S_PRESS, lastJoyPress)) {
    fineStep = !fineStep;
    setStatus(fineStep ? "Fine step" : "Coarse step");
    markVizDirty();
  }
}

void handleJoystickContinuous() {
  if (!pcLinked || !torqueEnabled) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastControlMs < UPDATE_INTERVAL_MS) {
    return;
  }

  const bool left = digitalRead(WIO_5S_LEFT) == LOW;
  const bool right = digitalRead(WIO_5S_RIGHT) == LOW;
  if (!left && !right) {
    return;
  }

  lastControlMs = now;
  const float step = currentStepDegrees();
  if (left) {
    sendMoveCommand(selectedJoint, -step);
  } else if (right) {
    sendMoveCommand(selectedJoint, step);
  }
}

void setupUi() {
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);

  pinMode(WIO_KEY_A, INPUT_PULLUP);
  pinMode(WIO_KEY_B, INPUT_PULLUP);
  pinMode(WIO_KEY_C, INPUT_PULLUP);
  pinMode(WIO_5S_UP, INPUT_PULLUP);
  pinMode(WIO_5S_DOWN, INPUT_PULLUP);
  pinMode(WIO_5S_LEFT, INPUT_PULLUP);
  pinMode(WIO_5S_RIGHT, INPUT_PULLUP);
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);

  tft.begin();
  tft.setRotation(3);
  initArmLayout();
  applyHomePoseForViz();
  updateDisplay();
}

void setup() {
  Serial.begin(HOST_BAUD);
  while (!Serial && millis() < 3000) {
    delay(10);
  }

  setupUi();
  sendHostLine("! WIO READY");
}

void loop() {
  pollHostSerial();
  updatePcLinkState();
  handleButtons();
  handleJoystickContinuous();

  if (vizDirty) {
    vizDirty = false;
    updateDisplay();
    return;
  }

  const unsigned long now = millis();
  if (now - lastDrawMs >= 250) {
    lastDrawMs = now;
    updateDisplay();
  }
}

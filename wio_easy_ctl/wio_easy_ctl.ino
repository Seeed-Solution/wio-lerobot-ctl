// Wio Terminal controller for lerobot-easy.
//
// Wio owns only its USB serial link. lerobot-easy owns business state and
// robot serial ownership. Wio sends input events and renders screen payloads.

#include <TFT_eSPI.h>

static constexpr uint32_t HOST_BAUD = 115200;
static constexpr unsigned long DRAW_INTERVAL_MS = 80;
static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 3000;
static constexpr unsigned long LINK_TIMEOUT_MS = 5000;
static constexpr unsigned long BUTTON_REPEAT_MS = 220;

TFT_eSPI tft;

char titleText[24] = "MODEL";
char modeText[16] = "model";
char primaryText[80] = "No model";
char indexText[16] = "0/0";
char statusText[80] = "Connect from easy";
char footerText[48] = "A Mode  B Start  C Stop";
char rxBuffer[512];
uint16_t rxLength = 0;
unsigned long seq = 1;
unsigned long lastDrawMs = 0;
unsigned long lastHostRxMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastRepeatMs = 0;
bool hostLinked = false;
bool needsFullRedraw = true;
bool dirtyHeader = true;
bool dirtyMain = true;
bool dirtyFooter = true;
bool dirtyStatus = true;
bool lastKeyA = false;
bool lastKeyB = false;
bool lastKeyC = false;
bool lastJoyPress = false;
bool lastJoyUp = false;
bool lastJoyDown = false;

void copyText(char* dest, size_t destSize, const char* src) {
  if (destSize == 0) return;
  strncpy(dest, src ? src : "", destSize - 1);
  dest[destSize - 1] = '\0';
}

void markAllDirty() {
  dirtyHeader = true;
  dirtyMain = true;
  dirtyFooter = true;
  dirtyStatus = true;
}

void sendHello() {
  Serial.print("{\"type\":\"hello\",\"seq\":");
  Serial.print(seq++);
  Serial.println("}");
}

void sendStatusRequest() {
  Serial.print("{\"type\":\"status\",\"seq\":");
  Serial.print(seq++);
  Serial.println("}");
}

void sendInput(const char* key, const char* eventType) {
  Serial.print("{\"type\":\"input\",\"key\":\"");
  Serial.print(key);
  Serial.print("\",\"event\":\"");
  Serial.print(eventType);
  Serial.print("\",\"seq\":");
  Serial.print(seq++);
  Serial.println("}");
}

bool readSwitchPressed(int pin, bool& wasPressed) {
  const bool isPressed = digitalRead(pin) == LOW;
  const bool edge = isPressed && !wasPressed;
  wasPressed = isPressed;
  return edge;
}

bool readRepeatPressed(int pin) {
  if (digitalRead(pin) != LOW) return false;
  const unsigned long now = millis();
  if (now - lastRepeatMs < BUTTON_REPEAT_MS) return false;
  lastRepeatMs = now;
  return true;
}

bool readJsonStringValue(const char* json, const char* key, char* dest, size_t destSize) {
  if (destSize == 0) return false;
  dest[0] = '\0';
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char* start = strstr(json, pattern);
  if (!start) return false;
  start += strlen(pattern);
  size_t i = 0;
  while (*start && *start != '"' && i + 1 < destSize) {
    if (*start == '\\' && start[1]) start++;
    dest[i++] = *start++;
  }
  dest[i] = '\0';
  return true;
}

void applyScreenMessage(const char* line) {
  readJsonStringValue(line, "mode", modeText, sizeof(modeText));
  readJsonStringValue(line, "title", titleText, sizeof(titleText));
  readJsonStringValue(line, "primary", primaryText, sizeof(primaryText));
  readJsonStringValue(line, "index", indexText, sizeof(indexText));
  readJsonStringValue(line, "status", statusText, sizeof(statusText));
  readJsonStringValue(line, "footer", footerText, sizeof(footerText));
  markAllDirty();
}

void handleHostLine(const char* line) {
  const bool wasLinked = hostLinked;
  lastHostRxMs = millis();
  hostLinked = true;
  if (!wasLinked) dirtyHeader = true;

  char type[16];
  readJsonStringValue(line, "type", type, sizeof(type));
  if (strcmp(type, "screen") == 0) {
    applyScreenMessage(line);
  } else if (strcmp(type, "ready") == 0) {
    readJsonStringValue(line, "text", statusText, sizeof(statusText));
    dirtyStatus = true;
  } else if (strcmp(type, "error") == 0) {
    if (!readJsonStringValue(line, "text", statusText, sizeof(statusText)) || !statusText[0]) {
      copyText(statusText, sizeof(statusText), "Error");
    }
    dirtyStatus = true;
  }
}

void pollHostSerial() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      rxBuffer[rxLength] = '\0';
      if (rxLength > 0) handleHostLine(rxBuffer);
      rxLength = 0;
      continue;
    }
    if (rxLength + 1 < sizeof(rxBuffer)) {
      rxBuffer[rxLength++] = ch;
    } else {
      rxLength = 0;
      copyText(statusText, sizeof(statusText), "Serial line too long");
      dirtyStatus = true;
    }
  }
}

void updateLinkState() {
  const bool wasLinked = hostLinked;
  hostLinked = (millis() - lastHostRxMs) < LINK_TIMEOUT_MS;
  if (wasLinked != hostLinked) {
    dirtyHeader = true;
    if (!hostLinked) {
      copyText(statusText, sizeof(statusText), "easy offline");
      dirtyStatus = true;
    }
  }
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 28, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setCursor(8, 9);
  tft.print("easy ");
  tft.print(hostLinked ? "OK" : "--");
  tft.setCursor(226, 9);
  tft.print(modeText);
}

void drawMain() {
  tft.fillRect(0, 28, 320, 154, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(14, 44);
  tft.print(titleText);

  tft.setTextSize(3);
  tft.setCursor(14, 82);
  tft.print(primaryText);

  tft.setTextColor(tft.color565(160, 160, 160), TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(14, 132);
  tft.print(indexText);
}

void drawStatus() {
  tft.fillRect(0, 182, 320, 34, TFT_BLACK);
  tft.setTextColor(tft.color565(160, 160, 160), TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(8, 194);
  tft.print(statusText);
}

void drawFooter() {
  tft.fillRect(0, 216, 320, 24, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setCursor(8, 225);
  tft.print(footerText);
}

void drawScreen() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  drawMain();
  drawStatus();
  drawFooter();
  needsFullRedraw = false;
  dirtyHeader = false;
  dirtyMain = false;
  dirtyFooter = false;
  dirtyStatus = false;
}

void updateDisplay() {
  const unsigned long now = millis();
  const bool dirty = needsFullRedraw || dirtyHeader || dirtyMain || dirtyStatus || dirtyFooter;
  if (!dirty) return;
  if (now - lastDrawMs < DRAW_INTERVAL_MS) return;
  lastDrawMs = now;
  if (needsFullRedraw) {
    drawScreen();
    return;
  }
  if (dirtyHeader) {
    drawHeader();
    dirtyHeader = false;
  }
  if (dirtyMain) {
    drawMain();
    dirtyMain = false;
  }
  if (dirtyStatus) {
    drawStatus();
    dirtyStatus = false;
  }
  if (dirtyFooter) {
    drawFooter();
    dirtyFooter = false;
  }
}

void handleButtons() {
  if (readSwitchPressed(WIO_KEY_A, lastKeyA)) sendInput("c", "press");
  if (readSwitchPressed(WIO_KEY_B, lastKeyB)) sendInput("b", "press");
  if (readSwitchPressed(WIO_KEY_C, lastKeyC)) sendInput("a", "press");
  if (readSwitchPressed(WIO_5S_PRESS, lastJoyPress)) sendInput("press", "press");
  if (readSwitchPressed(WIO_5S_UP, lastJoyUp)) sendInput("up", "press");
  if (readSwitchPressed(WIO_5S_DOWN, lastJoyDown)) sendInput("down", "press");
  if (readRepeatPressed(WIO_5S_LEFT)) sendInput("left", "repeat");
  if (readRepeatPressed(WIO_5S_RIGHT)) sendInput("right", "repeat");
}

void maybeSendHeartbeat() {
  const unsigned long now = millis();
  if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return;
  lastHeartbeatMs = now;
  if (hostLinked) {
    sendStatusRequest();
  } else {
    sendHello();
  }
}

void setup() {
  pinMode(WIO_KEY_A, INPUT_PULLUP);
  pinMode(WIO_KEY_B, INPUT_PULLUP);
  pinMode(WIO_KEY_C, INPUT_PULLUP);
  pinMode(WIO_5S_PRESS, INPUT_PULLUP);
  pinMode(WIO_5S_UP, INPUT_PULLUP);
  pinMode(WIO_5S_DOWN, INPUT_PULLUP);
  pinMode(WIO_5S_LEFT, INPUT_PULLUP);
  pinMode(WIO_5S_RIGHT, INPUT_PULLUP);

  Serial.begin(HOST_BAUD);
  tft.begin();
  tft.setRotation(3);
  drawScreen();
  delay(300);
  sendHello();
}

void loop() {
  pollHostSerial();
  updateLinkState();
  handleButtons();
  maybeSendHeartbeat();
  updateDisplay();
}

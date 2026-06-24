// Wio Terminal B601 run/replay console for bambot + lerobot-easy.
//
// Wio USB Serial -> bambot HTTPS Web Serial bridge -> local lerobot-easy API.
//
// Scope:
//   - List/select local models
//   - Start/stop inference runs
//   - List/select datasets
//   - Start/stop dataset replay
//   - Show short English status text
//
// Not included:
//   - Wi-Fi
//   - direct lerobot-easy HTTP calls
//
// Board package: Seeed SAMD Boards -> Wio Terminal
// Libraries: Seeed_Arduino_LCD / TFT_eSPI from the Wio board package

#include <TFT_eSPI.h>

static constexpr uint32_t HOST_BAUD = 115200;
static constexpr unsigned long DRAW_INTERVAL_MS = 80;
static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 3000;
static constexpr unsigned long LINK_TIMEOUT_MS = 5000;
static constexpr unsigned long BUTTON_REPEAT_MS = 220;

enum Screen : uint8_t {
  SCREEN_MODEL = 0,
  SCREEN_REPLAY = 1,
};

TFT_eSPI tft;

Screen screen = SCREEN_MODEL;
bool bridgeLinked = false;
bool needsFullRedraw = true;
bool dirtyHeader = true;
bool dirtyFooter = true;
bool dirtyMain = true;
bool dirtySelection = true;
bool dirtyActionStatus = true;
bool dirtyStatusBar = true;

char currentModel[48] = "No Model";
char currentDataset[48] = "No Dataset";
char runStatus[24] = "Ready";
char runStage[24] = "unknown";
char replayStatus[24] = "Ready";
char errorText[24] = "None";
char statusLine[56] = "Open bambot and connect Wio";
int selectedIndex = 0;
int modelCount = 0;
int selectedDatasetIndex = 0;
int datasetCount = 0;
int replayEpisode = 0;
unsigned long seq = 1;

char rxBuffer[384];
uint16_t rxLength = 0;

unsigned long lastDrawMs = 0;
unsigned long lastHostRxMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastRepeatMs = 0;

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

void setStatus(const char* value) {
  copyText(statusLine, sizeof(statusLine), value);
  dirtyStatusBar = true;
}

void markSelectionDirty() {
  dirtySelection = true;
}

void markActionStatusDirty() {
  dirtyActionStatus = true;
}

void markModeDirty() {
  dirtyHeader = true;
  dirtyFooter = true;
  dirtyMain = true;
  dirtySelection = true;
  dirtyActionStatus = true;
  dirtyStatusBar = true;
}

void sendCommand(const char* type) {
  Serial.print("{\"type\":\"");
  Serial.print(type);
  Serial.print("\",\"seq\":");
  Serial.print(seq++);
  Serial.println("}");
}

void sendRunStart() {
  Serial.print("{\"type\":\"run_start\",\"seq\":");
  Serial.print(seq++);
  Serial.println(",\"mode\":\"local\"}");
}

void sendSetMode(const char* mode) {
  Serial.print("{\"type\":\"set_mode\",\"seq\":");
  Serial.print(seq++);
  Serial.print(",\"mode\":\"");
  Serial.print(mode);
  Serial.println("}");
}

void sendReplayStart() {
  Serial.print("{\"type\":\"replay_start\",\"seq\":");
  Serial.print(seq++);
  Serial.print(",\"episodeIndex\":");
  Serial.print(replayEpisode);
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

void switchScreen() {
  screen = screen == SCREEN_MODEL ? SCREEN_REPLAY : SCREEN_MODEL;
  sendSetMode(screen == SCREEN_MODEL ? "model" : "replay");
  setStatus(screen == SCREEN_MODEL ? "Model mode" : "Dataset mode");
  markModeDirty();
}

void requestModels() {
  sendCommand("models");
  setStatus("Loading models");
}

void selectPreviousModel() {
  sendCommand("prev_model");
  setStatus("Prev model");
}

void selectNextModel() {
  sendCommand("next_model");
  setStatus("Next model");
}

void requestDatasets() {
  sendCommand("datasets");
  setStatus("Loading datasets");
}

void selectPreviousDataset() {
  sendCommand("prev_dataset");
  setStatus("Prev dataset");
}

void selectNextDataset() {
  sendCommand("next_dataset");
  setStatus("Next dataset");
}

void startRun() {
  sendRunStart();
  copyText(runStatus, sizeof(runStatus), "Preparing");
  copyText(errorText, sizeof(errorText), "None");
  markActionStatusDirty();
  setStatus("Start run");
}

void stopRun() {
  sendCommand("run_stop");
  copyText(runStatus, sizeof(runStatus), "Stopping");
  markActionStatusDirty();
  setStatus("Stop run");
}

void startReplay() {
  sendReplayStart();
  copyText(replayStatus, sizeof(replayStatus), "Running");
  copyText(errorText, sizeof(errorText), "None");
  markActionStatusDirty();
  setStatus("Start replay");
}

void stopReplay() {
  sendCommand("replay_stop");
  copyText(replayStatus, sizeof(replayStatus), "Stopping");
  markActionStatusDirty();
  setStatus("Stop replay");
}

const char* findJsonStringValue(const char* json, const char* key) {
  static char value[96];
  value[0] = '\0';

  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char* start = strstr(json, pattern);
  if (!start) return value;
  start += strlen(pattern);

  size_t i = 0;
  while (*start && *start != '"' && i + 1 < sizeof(value)) {
    if (*start == '\\' && start[1]) start++;
    value[i++] = *start++;
  }
  value[i] = '\0';
  return value;
}

const char* findNthJsonStringValue(const char* json, const char* key, int index) {
  static char value[96];
  value[0] = '\0';

  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char* cursor = json;
  int seen = 0;
  while ((cursor = strstr(cursor, pattern)) != nullptr) {
    cursor += strlen(pattern);
    if (seen == index) {
      size_t i = 0;
      while (*cursor && *cursor != '"' && i + 1 < sizeof(value)) {
        if (*cursor == '\\' && cursor[1]) cursor++;
        value[i++] = *cursor++;
      }
      value[i] = '\0';
      return value;
    }
    seen++;
  }
  return value;
}

int findJsonIntValue(const char* json, const char* key, int fallback) {
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char* start = strstr(json, pattern);
  if (!start) return fallback;
  start += strlen(pattern);
  return atoi(start);
}

int countJsonKey(const char* json, const char* key) {
  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char* cursor = json;
  int count = 0;
  while ((cursor = strstr(cursor, pattern)) != nullptr) {
    count++;
    cursor += strlen(pattern);
  }
  return count;
}

void handleModelsMessage(const char* line) {
  selectedIndex = findJsonIntValue(line, "selected", 0);
  modelCount = countJsonKey(line, "id");
  if (selectedIndex < 0) selectedIndex = 0;
  if (modelCount > 0 && selectedIndex >= modelCount) selectedIndex = modelCount - 1;

  const char* selectedName = findNthJsonStringValue(line, "name", selectedIndex);
  if (selectedName[0]) {
    copyText(currentModel, sizeof(currentModel), selectedName);
  } else if (modelCount <= 0) {
    copyText(currentModel, sizeof(currentModel), "No Model");
  }
  copyText(errorText, sizeof(errorText), "None");
  markSelectionDirty();
  setStatus(modelCount > 0 ? "Models loaded" : "No models");
}

void handleSelectedMessage(const char* line) {
  selectedIndex = findJsonIntValue(line, "index", selectedIndex);
  const char* name = findJsonStringValue(line, "name");
  if (name[0]) copyText(currentModel, sizeof(currentModel), name);
  copyText(errorText, sizeof(errorText), "None");
  markSelectionDirty();
  setStatus("Model selected");
}

void handleDatasetsMessage(const char* line) {
  selectedDatasetIndex = findJsonIntValue(line, "selected", 0);
  datasetCount = countJsonKey(line, "id");
  if (selectedDatasetIndex < 0) selectedDatasetIndex = 0;
  if (datasetCount > 0 && selectedDatasetIndex >= datasetCount) selectedDatasetIndex = datasetCount - 1;

  const char* selectedName = findNthJsonStringValue(line, "name", selectedDatasetIndex);
  if (selectedName[0]) {
    copyText(currentDataset, sizeof(currentDataset), selectedName);
  } else if (datasetCount <= 0) {
    copyText(currentDataset, sizeof(currentDataset), "No Dataset");
  }
  copyText(errorText, sizeof(errorText), "None");
  markSelectionDirty();
  setStatus(datasetCount > 0 ? "Datasets loaded" : "No datasets");
}

void handleDatasetSelectedMessage(const char* line) {
  selectedDatasetIndex = findJsonIntValue(line, "index", selectedDatasetIndex);
  const char* name = findJsonStringValue(line, "name");
  if (name[0]) copyText(currentDataset, sizeof(currentDataset), name);
  copyText(errorText, sizeof(errorText), "None");
  markSelectionDirty();
  setStatus("Dataset selected");
}

void handleRunMessage(const char* line) {
  char status[24];
  char stage[24];
  char err[24];
  copyText(status, sizeof(status), findJsonStringValue(line, "status"));
  copyText(stage, sizeof(stage), findJsonStringValue(line, "stage"));
  copyText(err, sizeof(err), findJsonStringValue(line, "error"));

  if (status[0]) copyText(runStatus, sizeof(runStatus), status);
  if (stage[0]) copyText(runStage, sizeof(runStage), stage);
  copyText(errorText, sizeof(errorText), err[0] ? err : "None");
  markActionStatusDirty();
  setStatus("Run update");
}

void handleReplayMessage(const char* line) {
  char status[24];
  char err[24];
  copyText(status, sizeof(status), findJsonStringValue(line, "status"));
  copyText(err, sizeof(err), findJsonStringValue(line, "error"));

  if (status[0]) copyText(replayStatus, sizeof(replayStatus), status);
  replayEpisode = findJsonIntValue(line, "episode", replayEpisode);
  copyText(errorText, sizeof(errorText), err[0] ? err : "None");
  markActionStatusDirty();
  setStatus("Replay update");
}

void handleErrorMessage(const char* line) {
  const char* text = findJsonStringValue(line, "text");
  copyText(errorText, sizeof(errorText), text[0] ? text : "Error");
  markActionStatusDirty();
  setStatus(errorText);
}

void handleReadyMessage(const char* line) {
  const char* text = findJsonStringValue(line, "text");
  copyText(errorText, sizeof(errorText), "None");
  setStatus(text[0] ? text : "Ready");
  requestModels();
  requestDatasets();
}

void handleHostLine(const char* line) {
  const bool wasLinked = bridgeLinked;
  lastHostRxMs = millis();
  bridgeLinked = true;
  if (!wasLinked) {
    dirtyHeader = true;
  }

  const char* type = findJsonStringValue(line, "type");
  if (strcmp(type, "ready") == 0) {
    handleReadyMessage(line);
  } else if (strcmp(type, "models") == 0) {
    handleModelsMessage(line);
  } else if (strcmp(type, "selected") == 0) {
    handleSelectedMessage(line);
  } else if (strcmp(type, "run") == 0) {
    handleRunMessage(line);
  } else if (strcmp(type, "datasets") == 0) {
    handleDatasetsMessage(line);
  } else if (strcmp(type, "dataset_selected") == 0) {
    handleDatasetSelectedMessage(line);
  } else if (strcmp(type, "replay") == 0) {
    handleReplayMessage(line);
  } else if (strcmp(type, "error") == 0) {
    handleErrorMessage(line);
  }
}

void pollHostSerial() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      rxBuffer[rxLength] = '\0';
      if (rxLength > 0) {
        handleHostLine(rxBuffer);
      }
      rxLength = 0;
      continue;
    }
    if (rxLength + 1 < sizeof(rxBuffer)) {
      rxBuffer[rxLength++] = ch;
    } else {
      rxLength = 0;
      setStatus("RX overflow");
    }
  }
}

void updateLinkState() {
  const bool wasLinked = bridgeLinked;
  bridgeLinked = (millis() - lastHostRxMs) < LINK_TIMEOUT_MS;
  if (wasLinked != bridgeLinked) {
    dirtyHeader = true;
  }
  if (wasLinked && !bridgeLinked) {
    setStatus("Bridge offline");
  }
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 24, tft.color565(20, 24, 32));
  tft.setTextColor(TFT_WHITE, tft.color565(20, 24, 32));
  tft.setTextSize(1);
  tft.setCursor(8, 8);
  tft.print(screen == SCREEN_MODEL ? "Wio Model Mode" : "Wio Replay Mode");
  tft.setCursor(238, 8);
  tft.print(bridgeLinked ? "USB OK" : "USB --");
}

void drawFooter() {
  tft.fillRect(0, 214, 320, 26, tft.color565(18, 18, 18));
  tft.setTextColor(tft.color565(190, 190, 190), tft.color565(18, 18, 18));
  tft.setTextSize(1);
  tft.setCursor(8, 222);
  if (screen == SCREEN_MODEL) {
    tft.print("A Stop  B Start  C Dataset  Joy Select");
  } else {
    tft.print("A Stop  B Replay  C Models  Joy Select");
  }
}

void drawStatusBar() {
  tft.fillRect(0, 190, 320, 24, TFT_BLACK);
  tft.setTextColor(tft.color565(180, 180, 180), TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(8, 198);
  tft.print(statusLine);
}

void drawModelScreen() {
  tft.setTextColor(tft.color565(120, 190, 255), TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(14, 44);
  tft.print("MODEL");
}

void drawModelSelection() {
  tft.fillRect(0, 72, 320, 84, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(14, 82);
  tft.print(currentModel);

  tft.setTextColor(tft.color565(160, 160, 160), TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(14, 136);
  tft.print(modelCount > 0 ? selectedIndex + 1 : 0);
  tft.print("/");
  tft.print(modelCount);
}

void drawModelActionStatus() {
  tft.fillRect(0, 156, 320, 30, TFT_BLACK);
  tft.setTextColor(tft.color565(160, 160, 160), TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(14, 164);
  tft.print("Run: ");
  tft.print(runStatus);
  tft.print("  ");
  tft.print(runStage);
}

void drawReplayScreen() {
  tft.setTextColor(tft.color565(255, 210, 90), TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(14, 44);
  tft.print("REPLAY");
}

void drawReplaySelection() {
  tft.fillRect(0, 72, 320, 84, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(14, 78);
  tft.print(currentDataset);

  tft.setTextSize(2);
  tft.setCursor(14, 132);
  tft.print(datasetCount > 0 ? selectedDatasetIndex + 1 : 0);
  tft.print("/");
  tft.print(datasetCount);
}

void drawReplayActionStatus() {
  tft.fillRect(0, 156, 320, 30, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(tft.color565(160, 160, 160), TFT_BLACK);
  tft.setCursor(14, 164);
  tft.print("Status: ");
  tft.print(replayStatus);
  tft.print("  Ep ");
  tft.print(replayEpisode);
}

void drawMainArea() {
  tft.fillRect(0, 24, 320, 166, TFT_BLACK);
  if (screen == SCREEN_MODEL) {
    drawModelScreen();
    drawModelSelection();
    drawModelActionStatus();
  } else {
    drawReplayScreen();
    drawReplaySelection();
    drawReplayActionStatus();
  }
}

void drawSelectionArea() {
  if (screen == SCREEN_MODEL) {
    drawModelSelection();
  } else {
    drawReplaySelection();
  }
}

void drawActionStatusArea() {
  if (screen == SCREEN_MODEL) {
    drawModelActionStatus();
  } else {
    drawReplayActionStatus();
  }
}

void drawScreen() {
  drawHeader();
  drawMainArea();
  drawStatusBar();
  drawFooter();
  needsFullRedraw = false;
  dirtyHeader = false;
  dirtyFooter = false;
  dirtyMain = false;
  dirtySelection = false;
  dirtyActionStatus = false;
  dirtyStatusBar = false;
}

void updateDisplay() {
  const unsigned long now = millis();
  const bool hasDirty =
      needsFullRedraw || dirtyHeader || dirtyFooter || dirtyMain || dirtySelection || dirtyActionStatus || dirtyStatusBar;
  if (!hasDirty && now - lastDrawMs < DRAW_INTERVAL_MS) {
    return;
  }
  if (!hasDirty) return;
  lastDrawMs = now;
  if (needsFullRedraw) {
    drawScreen();
    return;
  }
  if (dirtyHeader) {
    drawHeader();
    dirtyHeader = false;
  }
  if (dirtyFooter) {
    drawFooter();
    dirtyFooter = false;
  }
  if (dirtyMain) {
    drawMainArea();
    dirtyMain = false;
    dirtySelection = false;
    dirtyActionStatus = false;
  } else {
    if (dirtySelection) {
      drawSelectionArea();
      dirtySelection = false;
    }
    if (dirtyActionStatus) {
      drawActionStatusArea();
      dirtyActionStatus = false;
    }
  }
  if (dirtyStatusBar) {
    drawStatusBar();
    dirtyStatusBar = false;
  }
}

void handleButtons() {
  if (screen == SCREEN_MODEL) {
    if (readSwitchPressed(WIO_KEY_A, lastKeyA)) {
      stopRun();
    }
    if (readSwitchPressed(WIO_KEY_B, lastKeyB)) {
      startRun();
    }
    if (readSwitchPressed(WIO_KEY_C, lastKeyC)) {
      switchScreen();
    }
    if (readSwitchPressed(WIO_5S_UP, lastJoyUp)) selectPreviousModel();
    if (readSwitchPressed(WIO_5S_DOWN, lastJoyDown)) selectNextModel();
    if (readRepeatPressed(WIO_5S_LEFT)) {
      selectPreviousModel();
    }
    if (readRepeatPressed(WIO_5S_RIGHT)) {
      selectNextModel();
    }
  } else {
    if (readSwitchPressed(WIO_KEY_A, lastKeyA)) {
      stopReplay();
    }
    if (readSwitchPressed(WIO_KEY_B, lastKeyB)) {
      startReplay();
    }
    if (readSwitchPressed(WIO_KEY_C, lastKeyC)) {
      switchScreen();
    }
    if (readSwitchPressed(WIO_5S_UP, lastJoyUp)) selectPreviousDataset();
    if (readSwitchPressed(WIO_5S_DOWN, lastJoyDown)) selectNextDataset();
    if (readRepeatPressed(WIO_5S_LEFT)) {
      selectPreviousDataset();
    }
    if (readRepeatPressed(WIO_5S_RIGHT)) {
      selectNextDataset();
    }
  }
  readSwitchPressed(WIO_5S_PRESS, lastJoyPress);
}

void maybeSendHeartbeat() {
  const unsigned long now = millis();
  if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return;
  lastHeartbeatMs = now;
  if (!bridgeLinked) {
    sendCommand("hello");
  } else {
    sendCommand("status");
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
  tft.fillScreen(TFT_BLACK);
}

void setup() {
  Serial.begin(HOST_BAUD);
  while (!Serial && millis() < 3000) {
    delay(10);
  }

  setupUi();
  drawScreen();
  sendCommand("hello");
}

void loop() {
  pollHostSerial();
  updateLinkState();
  handleButtons();
  maybeSendHeartbeat();
  updateDisplay();
}

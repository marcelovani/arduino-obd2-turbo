// OBD2.h — BLE OBD2 transport and application state machine.
//
// Handles: BLE scanning → connecting → ELM327 initialisation → live polling.
// Compiled for production builds only (not SIMULATION, not DEMO).
//
// BLE device: "OBD BLE", service FFF0, notify char FFF1, write char FFF2.
// UUIDs confirmed via nRF Connect on the target dongle (PHY LE 1M).
//
// Polling strategy:
//   Engine off  (RPM < 200)  — battery, coolant, RPM every 3 s
//   Engine idle (RPM 200–999) — RPM only every 500 ms
//   Driving     (RPM ≥ 1000) — TPS + speed + RPM every 100 ms, then checkTurbo
//
// Encoder activity pauses OBD2 polling for ENCODER_PRIORITY_MS to keep the
// menu responsive even during heavy serial traffic.

#if !defined(SIMULATION) && !defined(DEMO)

#define ENCODER_PRIORITY_MS  500    // pause OBD2 polling after encoder activity
#define SCAN_TIMEOUT_MS      30000  // give up scanning after 30 s

enum AppState { SCANNING, CONNECTING, INIT_ELM, RUNNING, NO_OBD };
AppState appState       = SCANNING;
bool     connectFailed  = false;
String   targetName     = "";
uint32_t lastPollMs     = 0;
uint32_t lastIdlePollMs = 0;
uint32_t scanStartMs    = 0;
int      scanFrame      = 0;

static BLEUUID BLE_SVC_UUID   ("0000fff0-0000-1000-8000-00805f9b34fb");
static BLEUUID BLE_NOTIFY_UUID("0000fff1-0000-1000-8000-00805f9b34fb");  // RX — has CCCD
static BLEUUID BLE_WRITE_UUID ("0000fff2-0000-1000-8000-00805f9b34fb");  // TX — write

static BLEClient*               bleClient   = nullptr;
static BLERemoteCharacteristic* bleNotifyCh = nullptr;
static BLERemoteCharacteristic* bleWriteCh  = nullptr;
static BLEAdvertisedDevice*     bleDevice   = nullptr;
static bool                     bleFound     = false;
static bool                     bleConnected = false;
static String                   bleRxBuf     = "";

static void bleNotifyCB(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  for (size_t i = 0; i < len; i++) bleRxBuf += (char)data[i];
}

class OBDScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (bleFound) return;
    String name = dev.getName().c_str();
    String upper = name; upper.toUpperCase();
    bool nameMatch = upper.indexOf("OBD") >= 0 || upper.indexOf("ELM") >= 0 || upper.indexOf("LINK") >= 0;
    bool svcMatch  = dev.haveServiceUUID() && dev.isAdvertisingService(BLE_SVC_UUID);
    if (nameMatch || svcMatch) {
      BLEDevice::getScan()->stop();
      delete bleDevice;
      bleDevice = new BLEAdvertisedDevice(dev);
      bleFound  = true;
    }
  }
};

static float parseVoltage(const String& resp) {
  float v = strtof(resp.c_str(), nullptr);
  return (v > 5.0f && v < 20.0f) ? v : 0;
}

static String obdSend(const char* cmd, uint16_t timeout = 300) {
  if (!bleConnected || !bleWriteCh) return "";
  bleRxBuf = "";
  String s = String(cmd) + "\r";
  bleWriteCh->writeValue((uint8_t*)s.c_str(), s.length(), true);
  uint32_t t0 = millis();
  while (millis() - t0 < timeout) {
    if (bleRxBuf.indexOf('>') >= 0) return bleRxBuf;
    // Only collect data when the menu is closed or we are on the recording screen.
    if (menuState != MENU_CLOSED && menuState != MENU_RECORDING) return "";
    delay(10);
  }
  return bleRxBuf;
}

static float parsePID(const String& resp, int bytes, float multiplier) {
  if (resp.length() < (unsigned)(4 + bytes * 2)) return -1;
  String hex = resp.substring(4);
  if (bytes == 1)
    return strtol(hex.substring(0, 2).c_str(), nullptr, 16) * multiplier;
  int hi = strtol(hex.substring(0, 2).c_str(), nullptr, 16);
  int lo = strtol(hex.substring(2, 4).c_str(), nullptr, 16);
  return (hi * 256.0f + lo) * multiplier;
}

void doScanning() {
  readEncoder();
  if (menuState != MENU_CLOSED) return;

  if (scanStartMs == 0) {
    scanStartMs = millis();
    scanFrame   = 0;
    bleFound    = false;
    Serial.println("[scan] playing pairing voice (3 s)...");
    dfplayerVoice(TRACK_PAIRING);
    Serial.println("[scan] voice done — starting BLE scan bursts");
  }

  if (bleFound) {
    targetName    = bleDevice->getName().c_str();
    Serial.print("[scan] found device: "); Serial.println(targetName);
    connectFailed = false;
    appState      = CONNECTING;
    return;
  }

  if (millis() - scanStartMs >= SCAN_TIMEOUT_MS) {
    Serial.println("[scan] timeout — no OBD2 found");
    showMessage("No OBD2 found", "Display only", "Click for menu");
    dfplayerVoice(TRACK_NO_OBD2);
    appState = NO_OBD;
    return;
  }

  static const char SPIN[] = {'-', '/', '|', '\\'};
  char line1[22];
  uint32_t remaining = (SCAN_TIMEOUT_MS - (millis() - scanStartMs)) / 1000;
  snprintf(line1, sizeof(line1), "Scanning... %c %lus", SPIN[scanFrame % 4], remaining);
  scanFrame++;
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 28, line1);
  display.drawStr(0, 44, "Plug OBD BLE in");
  display.drawStr(0, 59, "OBD2 port & wait");
  display.sendBuffer();

  // 3-second BLE scan burst; stop() called from callback if device found early
  BLEDevice::getScan()->start(3, false);
}

void doNoObd() {
  uint32_t now = millis();
  if (now - lastDrawMs >= 200 && menuState == MENU_CLOSED) {
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB10_tr);
    display.drawStr(0, 28, "No OBD2");
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 44, "Display OK");
    display.drawStr(0, 59, "Click for menu");
    display.sendBuffer();
    lastDrawMs = now;
  }
}

void doConnecting() {
  char nameBuf[18];
  strncpy(nameBuf, targetName.c_str(), 17); nameBuf[17] = '\0';
  Serial.print("[connect] connecting to: "); Serial.println(nameBuf);
  showMessage("Found:", nameBuf, "Connecting BLE...");

  if (bleClient) { bleClient->disconnect(); bleClient = nullptr; }
  bleClient = BLEDevice::createClient();

  if (!bleClient->connect(bleDevice)) {
    Serial.println("[connect] FAILED — rescanning");
    connectFailed = true;
    showMessage("Connect failed", "Scanning again...");
    dfplayerVoice(TRACK_NO_OBD2);
    appState    = SCANNING;
    scanStartMs = 0;
    bleFound    = false;
    return;
  }
  Serial.println("[connect] BLE connected");

  BLERemoteService* svc = bleClient->getService(BLE_SVC_UUID);
  if (!svc) {
    Serial.println("[connect] service FFF0 missing — rescanning");
    bleClient->disconnect();
    connectFailed = true;
    showMessage("Service missing", "Scanning again...");
    dfplayerVoice(TRACK_NO_OBD2);
    appState    = SCANNING;
    scanStartMs = 0;
    bleFound    = false;
    return;
  }

  bleWriteCh  = svc->getCharacteristic(BLE_WRITE_UUID);
  bleNotifyCh = svc->getCharacteristic(BLE_NOTIFY_UUID);

  if (!bleWriteCh || !bleNotifyCh) {
    Serial.println("[connect] chars FFF1/FFF2 missing — rescanning");
    bleClient->disconnect();
    connectFailed = true;
    showMessage("Chars missing", "Scanning again...");
    dfplayerVoice(TRACK_NO_OBD2);
    appState    = SCANNING;
    scanStartMs = 0;
    bleFound    = false;
    return;
  }

  if (bleNotifyCh->canNotify())
    bleNotifyCh->registerForNotify(bleNotifyCB);

  Serial.println("[connect] all chars OK — moving to INIT_ELM");
  bleConnected  = true;
  connectFailed = false;
  appState      = INIT_ELM;
}

void doInitElm() {
  char nameBuf[18];
  strncpy(nameBuf, targetName.c_str(), 17); nameBuf[17] = '\0';
  Serial.println("[init] sending ATZ + init commands...");
  showMessage("Initialising...", nameBuf);

  obdSend("ATZ", 3000); delay(500);
  const char* cmds[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* cmd : cmds) { obdSend(cmd, 1500); delay(100); }

  String vResp = obdSend("ATRV", 1500);
  metricVoltage = parseVoltage(vResp);

  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(0, 28, "OBD2 connected!");
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 44, nameBuf);
  if (metricVoltage > 0) {
    char vbuf[20];
    snprintf(vbuf, sizeof(vbuf), "Battery: %.1f V", metricVoltage);
    display.drawStr(0, 59, vbuf);
  }
  display.sendBuffer();
  Serial.println("[init] done — entering RUNNING");
  delay(1500);
  appState = RUNNING;
}

void doRunning() {
  if (!bleClient || !bleClient->isConnected()) {
    bleConnected = false;
    bleWriteCh   = nullptr;
    bleNotifyCh  = nullptr;
    appState     = SCANNING;
    scanStartMs  = 0;
    bleFound     = false;
    return;
  }

  uint32_t now = millis();
  bool encActive = (now - lastEncActiveMs < ENCODER_PRIORITY_MS);

  if (metricRPM < ENGINE_IDLE_RPM) {
    // Standby screen only — battery voltage and coolant are NEVER polled elsewhere.
    if (!encActive && now - lastIdlePollMs >= 3000) {
      String r; float v;
      r = obdSend("ATRV", 1500); v = parseVoltage(r);       if (v > 0)  metricVoltage = v;
      r = obdSend("0105");       v = parsePID(r, 1, 1.0f);  if (v >= 0) metricCoolant = v - 40.0f;
      r = obdSend("010C");       v = parsePID(r, 2, 0.25f); if (v >= 0) metricRPM     = v;
      lastIdlePollMs = now;
    }
    if (now - lastDrawMs >= 200 && menuState == MENU_CLOSED) { drawParked(targetName.c_str()); lastDrawMs = now; }

  } else if (metricRPM < ENGINE_DRIVING_RPM) {
    // Idle — RPM only, every 500 ms
    if (!encActive && now - lastPollMs >= 500) {
      String r; float v;
      r = obdSend("010C"); v = parsePID(r, 2, 0.25f); if (v >= 0) metricRPM = v;
      lastPollMs = now;
    }
    if (now - lastDrawMs >= 50 && menuState == MENU_CLOSED) { drawDisplay(); lastDrawMs = now; }

  } else {
    // Driving — TPS + speed + RPM every 100 ms, then check for Turbo
    if (!encActive && now - lastPollMs >= 100) {
      String r; float v;
      r = obdSend("0145"); v = parsePID(r, 1, 100.0f / 255.0f); if (v >= 0) metricTPS   = v;
      r = obdSend("010D"); v = parsePID(r, 1, 1.0f);             if (v >= 0) metricSpeed = v;
      r = obdSend("010C"); v = parsePID(r, 2, 0.25f);            if (v >= 0) metricRPM   = v;
      checkTurbo(now);
      lastPollMs = now;
    }
    if (now - lastDrawMs >= 50 && menuState == MENU_CLOSED) { drawDisplay(); lastDrawMs = now; }
  }
}

#endif // !SIMULATION && !DEMO

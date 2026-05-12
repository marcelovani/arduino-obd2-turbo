// Phase 3 — BOV trigger (core feature)
// Connects to ELM327, reads throttle + RPM, and fires the BOV sound via
// DFPlayer Mini whenever the driver lifts off the throttle during a gear change.
//
// Tune the BOV_* constants after your first real-car test.
//
// Libraries needed: U8g2, DFRobotDFPlayerMini
// Built-in: BluetoothSerial

#include <BluetoothSerial.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DFRobotDFPlayerMini.h>

// ── Pins ──────────────────────────────────────────────────────────────────
#define PIN_DFP_RX 16   // ESP32 RX2 ← DFPlayer TX
#define PIN_DFP_TX 17   // ESP32 TX2 → DFPlayer RX (via 1kΩ resistor)

// ── BOV thresholds — tune after first car test ────────────────────────────
#define BOV_THROTTLE_HIGH  40.0f   // TPS must have been above this (accelerating)
#define BOV_THROTTLE_LOW   10.0f   // TPS must now be below this (lifted off)
#define BOV_RPM_MIN        1500.0f // must be in boost range
#define BOV_MAX_GEAR       2       // only trigger in 1st and 2nd gear
#define BOV_COOLDOWN_MS    2000    // min ms between BOV sounds

// ── Objects ───────────────────────────────────────────────────────────────
BluetoothSerial BT;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
DFRobotDFPlayerMini dfplayer;

// ── State ─────────────────────────────────────────────────────────────────
String   targetName  = "";
float    prevTPS     = 0;
uint32_t lastBovMs   = 0;
uint32_t bovCount    = 0;

// ── OBD2 helpers ─────────────────────────────────────────────────────────

String obdSend(const char* cmd, uint16_t timeout = 1000) {
  while (BT.available()) BT.read();
  BT.print(cmd);
  BT.print('\r');

  String resp = "";
  uint32_t t0 = millis();
  while (millis() - t0 < timeout) {
    while (BT.available()) {
      char c = BT.read();
      if (c == '>') return resp;
      if (c != '\r' && c != '\n') resp += c;
    }
  }
  return "";
}

float parsePID(const String& resp, int bytes, float multiplier) {
  if (resp.length() < (unsigned)(4 + bytes * 2)) return -1;
  String hex = resp.substring(4);
  if (bytes == 1) {
    return strtol(hex.substring(0, 2).c_str(), nullptr, 16) * multiplier;
  }
  int hi = strtol(hex.substring(0, 2).c_str(), nullptr, 16);
  int lo = strtol(hex.substring(2, 4).c_str(), nullptr, 16);
  return (hi * 256.0f + lo) * multiplier;
}

// ── Gear estimate (raw ratio — calibrate per-car later) ───────────────────
int estimateGear(float rpm, float speedKmh) {
  if (speedKmh < 2 || rpm < 500) return 0;
  float ratio = rpm / speedKmh;
  // Rough thresholds — refine after logging real values in each gear
  if (ratio > 120) return 1;
  if (ratio > 70)  return 2;
  if (ratio > 50)  return 3;
  if (ratio > 35)  return 4;
  if (ratio > 25)  return 5;
  return 6;
}

// ── BOV trigger ───────────────────────────────────────────────────────────
void checkBov(float tps, float rpm, float speed) {
  uint32_t now = millis();
  int gear = estimateGear(rpm, speed);

  if (now - lastBovMs >= BOV_COOLDOWN_MS &&
      prevTPS   > BOV_THROTTLE_HIGH &&
      tps       < BOV_THROTTLE_LOW  &&
      rpm       > BOV_RPM_MIN       &&
      gear      > 0                 &&
      gear     <= BOV_MAX_GEAR) {

    dfplayer.play(1);
    lastBovMs = now;
    bovCount++;
    Serial.printf("BOV! TPS %.0f→%.0f%% RPM %.0f Gear %d (total: %lu)\n",
                  prevTPS, tps, rpm, gear, bovCount);
  }
  prevTPS = tps;
}

// ── Display ───────────────────────────────────────────────────────────────
void showMessage(const char* line1, const char* line2 = nullptr) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(0, 18, line1);
  if (line2) {
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 36, line2);
  }
  display.sendBuffer();
}

void showRunning(float tps, float speed, float rpm) {
  int gear = estimateGear(rpm, speed);
  bool bovRecent = (millis() - lastBovMs < 1000);

  char buf[24];
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);

  snprintf(buf, sizeof(buf), "TPS: %.0f%%  G:%d", tps >= 0 ? tps : 0.0f, gear);
  display.drawStr(0, 14, buf);

  snprintf(buf, sizeof(buf), "SPD: %.0f km/h", speed >= 0 ? speed : 0.0f);
  display.drawStr(0, 28, buf);

  snprintf(buf, sizeof(buf), "RPM: %.0f", rpm >= 0 ? rpm : 0.0f);
  display.drawStr(0, 42, buf);

  if (bovRecent) {
    display.setFont(u8g2_font_ncenB10_tr);
    snprintf(buf, sizeof(buf), "PSSSSH! #%lu", bovCount);
    display.drawStr(0, 60, buf);
  } else {
    display.setFont(u8g2_font_ncenB06_tr);
    snprintf(buf, sizeof(buf), "BOV count: %lu", bovCount);
    display.drawStr(0, 60, buf);
  }

  display.sendBuffer();
}

// ── Bluetooth scan + connect ──────────────────────────────────────────────
bool connectWithPin(const String& name) {
  const char* pins[] = {"1234", "0000"};
  for (const char* pin : pins) {
    char msg[24];
    snprintf(msg, sizeof(msg), "PIN: %s", pin);
    showMessage("Connecting...", msg);
    BT.setPin(pin);
    if (BT.connect(name)) return true;
    delay(500);
  }
  return false;
}

bool scanAndConnect() {
  showMessage("Scanning BT...", "Looking for ELM327");
  BTScanResults* results = BT.discover(8000);

  if (!results || results->getCount() == 0) {
    showMessage("No devices found", "Retrying...");
    delay(3000);
    return false;
  }

  for (int i = 0; i < results->getCount(); i++) {
    BTAdvertisedDevice* dev = results->getDevice(i);
    String name = dev->getName().c_str();
    String upper = name;
    upper.toUpperCase();

    if (upper.indexOf("ELM") >= 0 || upper.indexOf("OBD") >= 0 || upper.indexOf("LINK") >= 0) {
      targetName = name;
      if (connectWithPin(targetName)) return true;
    }
  }
  showMessage("ELM327 not found", "Retrying...");
  delay(3000);
  return false;
}

bool initElm() {
  showMessage("ELM327", "Initialising...");
  obdSend("ATZ", 3000); delay(500);
  const char* cmds[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* cmd : cmds) { obdSend(cmd, 1500); delay(100); }
  return true;
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
  Wire.begin();
  display.begin();
  BT.begin("ESP32-OBD", true);

  showMessage("OBD2 Turbo", "BOV Mode");
  delay(1000);

  if (dfplayer.begin(Serial2)) {
    dfplayer.volume(25);
    Serial.println("DFPlayer ready");
  } else {
    Serial.println("DFPlayer failed — check SD card and wiring");
  }

  while (!scanAndConnect()) {}
  initElm();

  showMessage("Ready!", "Waiting for boost...");
  delay(1000);
}

// ── Loop ──────────────────────────────────────────────────────────────────
uint32_t lastPollMs = 0, lastDrawMs = 0;
float tps = 0, speed = 0, rpm = 0;

void loop() {
  uint32_t now = millis();

  if (now - lastPollMs >= 100) {
    String r;
    r = obdSend("0111"); tps   = parsePID(r, 1, 100.0f / 255.0f);
    r = obdSend("010D"); speed = parsePID(r, 1, 1.0f);
    r = obdSend("010C"); rpm   = parsePID(r, 2, 0.25f);
    if (tps < 0) tps = 0;
    if (speed < 0) speed = 0;
    if (rpm < 0) rpm = 0;

    checkBov(tps, rpm, speed);
    lastPollMs = now;
  }

  if (now - lastDrawMs >= 50) {
    showRunning(tps, speed, rpm);
    lastDrawMs = now;
  }
}

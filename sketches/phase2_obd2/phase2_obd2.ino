// Phase 2 — OBD2 Bluetooth connection test
//
// Auto-scans for ELM327, tries PIN 1234 then 0000, and shows live data.
//
// Parameters that work with ignition ON, engine OFF:
//   Battery voltage, fuel level, coolant temperature, throttle position
// Parameters that need engine RUNNING:
//   Speed, RPM
//
// OLED top-right corner shows connection dot: filled = connected
//
// Libraries needed: U8g2
// Built-in: BluetoothSerial

#include <BluetoothSerial.h>
#include <Wire.h>
#include <U8g2lib.h>

BluetoothSerial BT;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ── OBD2 helpers ──────────────────────────────────────────────────────────

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
  if (bytes == 1)
    return strtol(hex.substring(0, 2).c_str(), nullptr, 16) * multiplier;
  int hi = strtol(hex.substring(0, 2).c_str(), nullptr, 16);
  int lo = strtol(hex.substring(2, 4).c_str(), nullptr, 16);
  return (hi * 256.0f + lo) * multiplier;
}

// ── Display helpers ───────────────────────────────────────────────────────

// Small filled dot in top-right = BT connected
void drawConnectionDot(bool connected) {
  if (connected)
    display.drawDisc(124, 4, 3);   // filled circle
  else
    display.drawCircle(124, 4, 3); // hollow circle
}

void showMessage(const char* line1, const char* line2 = nullptr, bool connected = false) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(0, 18, line1);
  if (line2) {
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 36, line2);
  }
  drawConnectionDot(connected);
  display.sendBuffer();
}

// ── Bluetooth scan + connect ──────────────────────────────────────────────

String targetName = "";

// Try to connect with PIN 1234, then 0000
bool connectWithPin(const String& name) {
  const char* pins[] = {"1234", "0000"};
  for (const char* pin : pins) {
    char msg[24];
    snprintf(msg, sizeof(msg), "PIN: %s", pin);
    showMessage("Connecting...", msg);
    Serial.printf("Trying PIN %s for %s\n", pin, name.c_str());

    BT.setPin(pin);
    if (BT.connect(name)) {
      Serial.printf("Connected with PIN %s\n", pin);
      return true;
    }
    delay(500);
  }
  return false;
}

bool scanAndConnect() {
  showMessage("Scanning BT...", "Looking for ELM327");
  Serial.println("Scanning...");

  BTScanResults* results = BT.discover(8000);
  if (!results || results->getCount() == 0) {
    showMessage("No devices found", "Retrying in 3s...");
    Serial.println("No BT devices found");
    delay(3000);
    return false;
  }

  Serial.printf("Found %d device(s):\n", results->getCount());
  for (int i = 0; i < results->getCount(); i++) {
    BTAdvertisedDevice* dev = results->getDevice(i);
    String name = dev->getName().c_str();
    Serial.printf("  [%d] %s\n", i, name.c_str());

    String upper = name;
    upper.toUpperCase();
    if (upper.indexOf("ELM") >= 0 || upper.indexOf("OBD") >= 0 || upper.indexOf("LINK") >= 0) {
      targetName = name;
      showMessage("Found!", targetName.c_str());
      delay(400);

      if (connectWithPin(targetName)) return true;

      showMessage("Connect failed", "Scanning again...");
      delay(2000);
    }
  }
  showMessage("ELM327 not found", "Retrying...");
  delay(3000);
  return false;
}

bool initElm() {
  showMessage("ELM327", "Initialising...", true);
  Serial.println("Sending AT init sequence...");

  String r = obdSend("ATZ", 3000);
  Serial.printf("ATZ   → [%s]\n", r.c_str());
  delay(500);

  const char* cmds[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* cmd : cmds) {
    r = obdSend(cmd, 1500);
    Serial.printf("%-6s→ [%s]\n", cmd, r.c_str());
    delay(100);
  }

  showMessage("Connected!", targetName.c_str(), true);
  delay(800);
  return true;
}

// ── Setup ─────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Wire.begin();
  display.begin();
  BT.begin("ESP32-OBD", true);

  showMessage("OBD2 Test", "Booting...");
  delay(1000);

  while (!scanAndConnect()) {}
  initElm();
}

// ── Loop — poll all parameters ────────────────────────────────────────────

void loop() {
  // ── Parameters that work with ignition ON, engine OFF ──
  String r = obdSend("ATRV", 1000);                          // battery voltage
  float batt    = atof(r.c_str());                           // "12.4V" → 12.4

  r = obdSend("012F");
  float fuel    = parsePID(r, 1, 100.0f / 255.0f);          // fuel level %

  r = obdSend("0105");
  float coolant = parsePID(r, 1, 1.0f) - 40.0f;             // coolant °C (A-40)

  r = obdSend("0111");
  float tps     = parsePID(r, 1, 100.0f / 255.0f);          // throttle %

  // ── Parameters that need engine running ──
  r = obdSend("010D");
  float speed   = parsePID(r, 1, 1.0f);                     // km/h

  r = obdSend("010C");
  float rpm     = parsePID(r, 2, 0.25f);                    // RPM

  Serial.printf("BATT: %.1fV  FUEL: %.0f%%  COOL: %.0f°C  TPS: %.0f%%  SPD: %.0f  RPM: %.0f\n",
                batt, fuel >= 0 ? fuel : 0.0f, coolant >= 0 ? coolant : 0.0f,
                tps >= 0 ? tps : 0.0f, speed >= 0 ? speed : 0.0f, rpm >= 0 ? rpm : 0.0f);

  // ── Draw ──
  char buf[28];
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);

  snprintf(buf, sizeof(buf), "BATT: %.1fV", batt);
  display.drawStr(0, 10, buf);

  snprintf(buf, sizeof(buf), "FUEL:%.0f%%  COOL:%.0fC", fuel >= 0 ? fuel : 0.0f, coolant >= 0 ? coolant : 0.0f);
  display.drawStr(0, 22, buf);

  snprintf(buf, sizeof(buf), "TPS: %.0f%%", tps >= 0 ? tps : 0.0f);
  display.drawStr(0, 34, buf);

  snprintf(buf, sizeof(buf), "SPD: %.0f km/h", speed >= 0 ? speed : 0.0f);
  display.drawStr(0, 46, buf);

  snprintf(buf, sizeof(buf), "RPM: %.0f", rpm >= 0 ? rpm : 0.0f);
  display.drawStr(0, 58, buf);

  drawConnectionDot(true);
  display.sendBuffer();
}

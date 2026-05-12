// Phase 2 — OBD2 Bluetooth connection test
// Expected: auto-scans for ELM327, connects, then shows live
// throttle / speed / RPM on the OLED and serial monitor.
// Engine must be running.
//
// Libraries needed: U8g2
// Built-in: BluetoothSerial

#include <BluetoothSerial.h>
#include <Wire.h>
#include <U8g2lib.h>

BluetoothSerial BT;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

#define OBD_TIMEOUT_MS 1000

// ── OBD2 helpers ─────────────────────────────────────────────────────────

String obdSend(const char* cmd, uint16_t timeout = OBD_TIMEOUT_MS) {
  while (BT.available()) BT.read();  // flush stale bytes
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
  return "";  // timeout
}

float parsePID(const String& resp, int bytes, float multiplier) {
  if (resp.length() < (unsigned)(4 + bytes * 2)) return -1;
  String hex = resp.substring(4);  // skip "41XX" header
  if (bytes == 1) {
    return strtol(hex.substring(0, 2).c_str(), nullptr, 16) * multiplier;
  }
  int hi = strtol(hex.substring(0, 2).c_str(), nullptr, 16);
  int lo = strtol(hex.substring(2, 4).c_str(), nullptr, 16);
  return (hi * 256.0f + lo) * multiplier;
}

// ── Display helper ────────────────────────────────────────────────────────

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

// ── Bluetooth scan + connect ──────────────────────────────────────────────

String targetName = "";

bool scanAndConnect() {
  showMessage("Scanning BT...", "Looking for ELM327");
  Serial.println("Starting BT scan...");

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
      delay(500);

      showMessage("Connecting...", targetName.c_str());
      Serial.printf("Connecting to: %s\n", targetName.c_str());

      if (BT.connect(targetName)) {
        Serial.println("Connected!");
        return true;
      } else {
        Serial.println("Connection failed");
        showMessage("Connect failed", "Retrying...");
        delay(2000);
      }
    }
  }
  return false;
}

bool initElm() {
  showMessage("ELM327", "Initialising...");
  Serial.println("Sending AT init sequence...");

  String r = obdSend("ATZ", 3000);
  Serial.printf("ATZ  → [%s]\n", r.c_str());
  delay(500);

  const char* cmds[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* cmd : cmds) {
    r = obdSend(cmd, 1500);
    Serial.printf("%-6s→ [%s]\n", cmd, r.c_str());
    delay(100);
  }

  showMessage("ELM327 ready!", targetName.c_str());
  delay(1000);
  return true;
}

// ── Setup ─────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Wire.begin();
  display.begin();
  BT.begin("ESP32-OBD", true);  // Bluetooth Classic master

  showMessage("OBD2 Test", "Booting...");
  delay(1000);

  while (!scanAndConnect()) {}
  initElm();
}

// ── Loop — poll and display ───────────────────────────────────────────────

void loop() {
  String r;

  r = obdSend("0111");
  float tps   = parsePID(r, 1, 100.0f / 255.0f);

  r = obdSend("010D");
  float speed = parsePID(r, 1, 1.0f);

  r = obdSend("010C");
  float rpm   = parsePID(r, 2, 0.25f);

  Serial.printf("TPS: %.1f%%  Speed: %.0f km/h  RPM: %.0f\n",
                tps >= 0 ? tps : 0,
                speed >= 0 ? speed : 0,
                rpm >= 0 ? rpm : 0);

  char buf[24];
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);

  snprintf(buf, sizeof(buf), "TPS: %.1f%%", tps >= 0 ? tps : 0.0f);
  display.drawStr(0, 16, buf);

  snprintf(buf, sizeof(buf), "SPD: %.0f km/h", speed >= 0 ? speed : 0.0f);
  display.drawStr(0, 32, buf);

  snprintf(buf, sizeof(buf), "RPM: %.0f", rpm >= 0 ? rpm : 0.0f);
  display.drawStr(0, 48, buf);

  display.setFont(u8g2_font_ncenB06_tr);
  display.drawStr(0, 62, targetName.c_str());

  display.sendBuffer();
}

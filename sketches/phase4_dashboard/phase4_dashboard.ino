// Phase 4 — Full dashboard
// State machine: SCANNING → CONNECTING → INIT_ELM → RUNNING
// Auto-connects to ELM327. OLED shows live gauges. Turbo sound fires on gear change.
// Rotate encoder to cycle views. Click encoder to disconnect and rescan.
//
// Libraries needed: U8g2, DFRobotDFPlayerMini, Bounce2, Adafruit MPU6050, Adafruit Unified Sensor
// Built-in: BluetoothSerial

#include <BluetoothSerial.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Bounce2.h>

// ── Pins ──────────────────────────────────────────────────────────────────
#define PIN_ENC_CLK 25
#define PIN_ENC_DT  26
#define PIN_ENC_SW  27
#define PIN_DFP_RX  16
#define PIN_DFP_TX  17

// ── Turbo thresholds ────────────────────────────────────────────────────────
#define TURBO_THROTTLE_HIGH  40.0f
#define TURBO_THROTTLE_LOW   10.0f
#define TURBO_RPM_MIN        1500.0f
#define TURBO_MAX_GEAR       2
#define TURBO_COOLDOWN_MS    2000

// ── Objects ───────────────────────────────────────────────────────────────
BluetoothSerial    BT;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
DFRobotDFPlayerMini dfplayer;
Adafruit_MPU6050   mpu;
Bounce             encBtn;

// ── App state ─────────────────────────────────────────────────────────────
enum AppState { SCANNING, CONNECTING, INIT_ELM, RUNNING };
AppState appState = SCANNING;

String   targetName  = "";
int      currentView = 0;    // 0=throttle, 1=speed, 2=all, 3=gforce
int      lastClk     = HIGH;

// ── Metrics ───────────────────────────────────────────────────────────────
float metricTPS    = 0;
float metricSpeed  = 0;
float metricRPM    = 0;
float metricGforce = 0;
float prevTPS      = 0;
uint32_t lastTurboMs = 0;
uint32_t turboCount  = 0;

#define NUM_VIEWS 4

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

// ── Gear estimate ─────────────────────────────────────────────────────────
int estimateGear(float rpm, float speedKmh) {
  if (speedKmh < 2 || rpm < 500) return 0;
  float ratio = rpm / speedKmh;
  if (ratio > 120) return 1;
  if (ratio > 70)  return 2;
  if (ratio > 50)  return 3;
  if (ratio > 35)  return 4;
  if (ratio > 25)  return 5;
  return 6;
}

// ── Turbo trigger ───────────────────────────────────────────────────────────
void checkTurbo() {
  uint32_t now = millis();
  int gear = estimateGear(metricRPM, metricSpeed);

  if (now - lastTurboMs >= TURBO_COOLDOWN_MS &&
      prevTPS        > TURBO_THROTTLE_HIGH &&
      metricTPS      < TURBO_THROTTLE_LOW  &&
      metricRPM      > TURBO_RPM_MIN       &&
      gear           > 0                 &&
      gear          <= TURBO_MAX_GEAR) {
    dfplayer.play(1);
    lastTurboMs = now;
    turboCount++;
    Serial.printf("Turbo #%lu  TPS %.0f→%.0f%%  RPM %.0f  Gear %d\n",
                  turboCount, prevTPS, metricTPS, metricRPM, gear);
  }
  prevTPS = metricTPS;
}

// ── Display helpers ───────────────────────────────────────────────────────
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

void drawRunning() {
  int gear = estimateGear(metricRPM, metricSpeed);
  bool turboRecent = (millis() - lastTurboMs < 800);
  char buf[24];

  display.clearBuffer();

  if (currentView == 0) {
    // ── Throttle (large) + speed ──
    display.setFont(u8g2_font_ncenB08_tr);
    snprintf(buf, sizeof(buf), "TPS  G:%d", gear);
    display.drawStr(0, 10, buf);
    display.setFont(u8g2_font_logisoso24_tr);
    snprintf(buf, sizeof(buf), "%.0f%%", metricTPS);
    display.drawStr(0, 44, buf);
    display.setFont(u8g2_font_ncenB08_tr);
    snprintf(buf, sizeof(buf), "%.0f km/h", metricSpeed);
    display.drawStr(0, 60, buf);

  } else if (currentView == 1) {
    // ── Speed (large) + RPM ──
    display.setFont(u8g2_font_ncenB08_tr);
    snprintf(buf, sizeof(buf), "SPEED  G:%d", gear);
    display.drawStr(0, 10, buf);
    display.setFont(u8g2_font_logisoso24_tr);
    snprintf(buf, sizeof(buf), "%.0f", metricSpeed);
    display.drawStr(0, 44, buf);
    display.setFont(u8g2_font_ncenB08_tr);
    snprintf(buf, sizeof(buf), "RPM: %.0f", metricRPM);
    display.drawStr(0, 60, buf);

  } else if (currentView == 2) {
    // ── All metrics as text ──
    display.setFont(u8g2_font_ncenB08_tr);
    snprintf(buf, sizeof(buf), "TPS: %.0f%%  G:%d", metricTPS, gear);
    display.drawStr(0, 14, buf);
    snprintf(buf, sizeof(buf), "SPD: %.0f km/h", metricSpeed);
    display.drawStr(0, 30, buf);
    snprintf(buf, sizeof(buf), "RPM: %.0f", metricRPM);
    display.drawStr(0, 46, buf);
    snprintf(buf, sizeof(buf), turboRecent ? "PSSSSH! #%lu" : "Turbo: %lu", turboCount);
    display.drawStr(0, 62, buf);

  } else {
    // ── G-force ──
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 10, "G-FORCE");
    display.setFont(u8g2_font_logisoso24_tr);
    snprintf(buf, sizeof(buf), "%+.1f G", metricGforce);
    display.drawStr(0, 44, buf);
    display.setFont(u8g2_font_ncenB08_tr);
    snprintf(buf, sizeof(buf), "SPD: %.0f  RPM: %.0f", metricSpeed, metricRPM);
    display.drawStr(0, 60, buf);
  }

  display.sendBuffer();
}

// ── Encoder ───────────────────────────────────────────────────────────────
void handleEncoder() {
  int clk = digitalRead(PIN_ENC_CLK);
  if (clk != lastClk) {
    lastClk = clk;
    if (clk == LOW) {
      int delta = (digitalRead(PIN_ENC_DT) == HIGH) ? -1 : 1;
      currentView = (currentView + delta + NUM_VIEWS) % NUM_VIEWS;
    }
  }

  encBtn.update();
  if (encBtn.fell()) {
    BT.disconnect();
    appState    = SCANNING;
    currentView = 0;
    Serial.println("Disconnected — returning to scan");
  }
}

// ── State: SCANNING ───────────────────────────────────────────────────────
void doScanning() {
  showMessage("Scanning BT...", "Looking for ELM327");
  Serial.println("Scanning for ELM327...");

  BTScanResults* results = BT.discover(8000);
  if (!results || results->getCount() == 0) {
    showMessage("No devices found", "Retrying...");
    delay(3000);
    return;
  }

  for (int i = 0; i < results->getCount(); i++) {
    BTAdvertisedDevice* dev = results->getDevice(i);
    String name = dev->getName().c_str();
    String upper = name; upper.toUpperCase();
    Serial.printf("  [%d] %s\n", i, name.c_str());

    if (upper.indexOf("ELM") >= 0 || upper.indexOf("OBD") >= 0 || upper.indexOf("LINK") >= 0) {
      targetName = name;
      appState   = CONNECTING;
      return;
    }
  }
  showMessage("ELM327 not found", "Retrying...");
  delay(3000);
}

// ── State: CONNECTING ─────────────────────────────────────────────────────
void doConnecting() {
  Serial.printf("Connecting to: %s\n", targetName.c_str());

  const char* pins[] = {"1234", "0000"};
  for (const char* pin : pins) {
    char msg[24];
    snprintf(msg, sizeof(msg), "PIN: %s", pin);
    showMessage("Connecting...", msg);
    BT.setPin(pin);
    if (BT.connect(targetName)) {
      appState = INIT_ELM;
      return;
    }
    delay(500);
  }

  showMessage("Connect failed", "Scanning again...");
  delay(2000);
  appState = SCANNING;
}

// ── State: INIT_ELM ───────────────────────────────────────────────────────
void doInitElm() {
  showMessage("ELM327", "Initialising...");
  obdSend("ATZ", 3000); delay(500);
  const char* cmds[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* cmd : cmds) { obdSend(cmd, 1500); delay(100); }
  showMessage("Connected!", targetName.c_str());
  delay(800);
  appState = RUNNING;
}

// ── State: RUNNING ────────────────────────────────────────────────────────
uint32_t lastPollMs = 0, lastImuMs = 0, lastDrawMs = 0;

void doRunning() {
  uint32_t now = millis();

  if (now - lastImuMs >= 20) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    metricGforce = (a.acceleration.z - 9.81f) / 9.81f;
    lastImuMs = now;
  }

  if (now - lastPollMs >= 100) {
    String r;
    r = obdSend("0111"); metricTPS   = max(0.0f, parsePID(r, 1, 100.0f / 255.0f));
    r = obdSend("010D"); metricSpeed = max(0.0f, parsePID(r, 1, 1.0f));
    r = obdSend("010C"); metricRPM   = max(0.0f, parsePID(r, 2, 0.25f));
    checkTurbo();
    lastPollMs = now;
  }

  if (now - lastDrawMs >= 50) {
    drawRunning();
    lastDrawMs = now;
  }

  handleEncoder();
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
  Wire.begin();

  display.begin();
  mpu.begin();
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  pinMode(PIN_ENC_CLK, INPUT);
  pinMode(PIN_ENC_DT,  INPUT);
  encBtn.attach(PIN_ENC_SW, INPUT_PULLUP);
  encBtn.interval(25);
  lastClk = digitalRead(PIN_ENC_CLK);

  BT.begin("ESP32-OBD", true);

  if (dfplayer.begin(Serial2)) {
    dfplayer.volume(25);
    Serial.println("DFPlayer ready");
  } else {
    Serial.println("DFPlayer failed — Turbo sounds disabled");
  }

  showMessage("OBD2 Turbo", "v1.0");
  delay(1500);
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
  switch (appState) {
    case SCANNING:   doScanning();   break;
    case CONNECTING: doConnecting(); break;
    case INIT_ELM:   doInitElm();    break;
    case RUNNING:    doRunning();    break;
  }
}

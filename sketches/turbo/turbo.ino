// turbo.ino — OBD2 Turbo Sound Emulator
//
// Single sketch for both real device and Wokwi simulation.
// Build targets:
//   Real device : arduino-cli compile --fqbn esp32:esp32:esp32doit-devkit-v1 sketches/turbo
//   Wokwi sim   : same + --build-property "compiler.cpp.extra_flags=-DSIMULATION"
//
// Hardware (real device):
//   SSD1306 OLED   — I2C (SDA=GPIO21, SCL=GPIO22)
//   KY-040 encoder — CLK=GPIO25, DT=GPIO26, SW=GPIO27
//   DFPlayer Mini  — Serial2 (RX=GPIO16, TX=GPIO17), microSD with /mp3/0001.mp3
//   ELM327 dongle  — Bluetooth Classic, OBD2 port
//
// Hardware (Wokwi simulation):
//   SSD1306 OLED, KY-040 encoder, LED on GPIO17
//   OBD2 data replayed from a built-in 9-second driving scenario.
//
// Encoder: rotate = cycle views, click = reset counter (sim) / disconnect BT (device)

#include <Wire.h>
#include <U8g2lib.h>
#include <Bounce2.h>

#ifndef SIMULATION
  #include <BluetoothSerial.h>
  #include <DFRobotDFPlayerMini.h>
#endif

// ── Pins ──────────────────────────────────────────────────────────────────
#define PIN_ENC_CLK  25
#define PIN_ENC_DT   26
#define PIN_ENC_SW   27

#ifdef SIMULATION
  #define PIN_LED     17   // blinks while MP3 would play
#else
  #define PIN_DFP_RX  16
  #define PIN_DFP_TX  17
#endif

// ── Turbo thresholds ──────────────────────────────────────────────────────
#define TURBO_THROTTLE_HIGH  40.0f
#define TURBO_THROTTLE_LOW   10.0f
#define TURBO_RPM_MIN        1500.0f
#define TURBO_MAX_GEAR       2
#define TURBO_COOLDOWN_MS    2000

// ── Encoder sensitivity ───────────────────────────────────────────────────
#ifdef SIMULATION
  #define STEPS_PER_ZONE  1   // Wokwi encoder sends 1 pulse per detent
#else
  #define STEPS_PER_ZONE  5   // Real KY-040: ~20 detents/rev → 5 per 90°
#endif

// ── Objects ───────────────────────────────────────────────────────────────
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
Bounce encBtn;

#ifndef SIMULATION
  BluetoothSerial     BT;
  DFRobotDFPlayerMini dfplayer;
#endif

// ── Shared state ──────────────────────────────────────────────────────────
int      currentView  = 0;     // 0=throttle, 1=speed, 2=all, 3=bars
int      encoderPos   = 0;     // 0..(4*STEPS_PER_ZONE-1)
int      lastClk      = HIGH;

float    metricTPS    = 0;
float    metricSpeed  = 0;
float    metricRPM    = 0;
float    prevTPS      = 0;
uint32_t lastTurboMs  = 0;
uint32_t turboCount   = 0;
uint32_t turboUntilMs = 0;    // show "PSSSSH!" banner until this time
uint32_t lastDrawMs   = 0;

#ifdef SIMULATION
  uint32_t turboSoundUntilMs = 0;   // LED blinks while this is in the future
  int      scenIdx            = 0;
  uint32_t scenStart          = 0;
#else
  enum AppState { SCANNING, CONNECTING, INIT_ELM, RUNNING };
  AppState appState   = SCANNING;
  String   targetName = "";
  uint32_t lastPollMs = 0;
#endif

// ── Gear estimation ───────────────────────────────────────────────────────
// RPM/speed ratio thresholds tuned for a small European petrol car.
int estimateGear(float rpm, float speed) {
  if (speed < 2.0f || rpm < 100.0f) return 0;
  float ratio = rpm / speed;
  if      (ratio > 110.0f) return 1;
  else if (ratio >  65.0f) return 2;
  else if (ratio >  43.0f) return 3;
  else if (ratio >  30.0f) return 4;
  else if (ratio >  22.0f) return 5;
  else                     return 6;
}

// ── Turbo trigger ─────────────────────────────────────────────────────────
void checkTurbo(uint32_t now) {
  int gear = estimateGear(metricRPM, metricSpeed);
  if (prevTPS       > TURBO_THROTTLE_HIGH &&
      metricTPS     < TURBO_THROTTLE_LOW  &&
      metricRPM     > TURBO_RPM_MIN       &&
      gear         <= TURBO_MAX_GEAR      &&
      now - lastTurboMs > TURBO_COOLDOWN_MS) {
    turboCount++;
    lastTurboMs  = now;
    turboUntilMs = now + 800;
#ifdef SIMULATION
    turboSoundUntilMs = now + 1000;
#else
    dfplayer.play(1);
#endif
    Serial.printf("[Turbo] #%lu  gear=%d  TPS %.0f→%.0f  RPM %.0f\n",
                  turboCount, gear, prevTPS, metricTPS, metricRPM);
  }
  prevTPS = metricTPS;
}

// ── Encoder ───────────────────────────────────────────────────────────────
void readEncoder() {
  encBtn.update();
  if (encBtn.fell()) {
#ifdef SIMULATION
    turboCount  = 0;
    lastTurboMs = 0;
    Serial.println("[ENC] Turbo counter reset");
#else
    BT.disconnect();
    appState    = SCANNING;
    currentView = 0;
    Serial.println("Disconnected — returning to scan");
#endif
  }
  int clk = digitalRead(PIN_ENC_CLK);
  if (clk != lastClk && clk == LOW) {
    int delta = (digitalRead(PIN_ENC_DT) != clk) ? -1 : 1;
    int total = 4 * STEPS_PER_ZONE;
    encoderPos  = ((encoderPos + delta) % total + total) % total;
    currentView = encoderPos / STEPS_PER_ZONE;
  }
  lastClk = clk;
}

// ── Display ───────────────────────────────────────────────────────────────
void drawDisplay() {
  int  gear        = estimateGear(metricRPM, metricSpeed);
  bool turboRecent = (millis() < turboUntilMs);

  display.clearBuffer();

  // ── Header ──
  if (turboRecent) {
    display.setFont(u8g2_font_ncenB10_tr);
    char buf[24];
    snprintf(buf, sizeof(buf), "** PSSSSH! #%lu **", turboCount);
    display.drawStr(0, 12, buf);
    display.setFont(u8g2_font_5x7_tr);
  } else if (currentView == 3) {
    display.setFont(u8g2_font_5x7_tr);
    char gbuf[10];
    snprintf(gbuf, sizeof(gbuf), "Gear: %d", gear);
    display.drawStr(0, 8, gbuf);
  } else {
    display.setFont(u8g2_font_5x7_tr);
    char hdr[24];
    snprintf(hdr, sizeof(hdr), "Turbo:%lu", turboCount);
    display.drawStr(0, 8, hdr);
  }

  // ── View content ──
  if (currentView == 0) {
    display.setFont(u8g2_font_ncenB24_tr);
    char buf[8];
    snprintf(buf, sizeof(buf), "%3.0f%%", metricTPS);
    display.drawStr(14, 58, buf);
    display.setFont(u8g2_font_5x7_tr);
    display.drawStr(0, 20, "THROTTLE");
    int barW = (int)(metricTPS);
    display.drawFrame(0, 24, 102, 8);
    if (barW > 0) display.drawBox(1, 25, barW, 6);

  } else if (currentView == 1) {
    display.setFont(u8g2_font_ncenB24_tr);
    char buf[8];
    snprintf(buf, sizeof(buf), "%3.0f", metricSpeed);
    display.drawStr(14, 58, buf);
    display.setFont(u8g2_font_5x7_tr);
    display.drawStr(0, 20, "SPEED km/h");
    char grpbuf[12];
    snprintf(grpbuf, sizeof(grpbuf), "Gear: %d", gear);
    display.drawStr(80, 20, grpbuf);

  } else if (currentView == 2) {
    display.setFont(u8g2_font_5x7_tr);
    char line[32];
    snprintf(line, sizeof(line), "TPS:  %5.1f %%",   metricTPS);   display.drawStr(0, 20, line);
    snprintf(line, sizeof(line), "SPD:  %5.1f km/h", metricSpeed); display.drawStr(0, 30, line);
    snprintf(line, sizeof(line), "RPM:  %5.0f",      metricRPM);   display.drawStr(0, 40, line);
    snprintf(line, sizeof(line), "Gear: %d",          gear);        display.drawStr(0, 50, line);

  } else {
    // Dual bar — throttle + speed
    display.setFont(u8g2_font_5x7_tr);

    display.drawStr(0, 22, "Throttle");
    char tpsbuf[8];
    snprintf(tpsbuf, sizeof(tpsbuf), "%3.0f%%", metricTPS);
    display.drawStr(90, 22, tpsbuf);
    int tpsW = (int)metricTPS;
    display.drawFrame(0, 24, 102, 10);
    if (tpsW > 0) display.drawBox(1, 25, tpsW, 8);

    display.drawStr(0, 44, "Speed");
    char spdbuf[10];
    snprintf(spdbuf, sizeof(spdbuf), "%3.0f", metricSpeed);
    display.drawStr(90, 44, spdbuf);
    int spdW = min(100, (int)(metricSpeed / 2.0f));
    display.drawFrame(0, 46, 102, 10);
    if (spdW > 0) display.drawBox(1, 47, spdW, 8);
  }

  // ── View indicator: 4 circles on right edge, filled = current ──
  for (int i = 0; i < 4; i++) {
    if (i == currentView) display.drawDisc(124,   6 + i * 12, 3);
    else                  display.drawCircle(124, 6 + i * 12, 3);
  }

  display.sendBuffer();
}

// ═══════════════════════════════════════════════════════════════════════════
// SIMULATION build: scenario playback, LED blink
// ═══════════════════════════════════════════════════════════════════════════
#ifdef SIMULATION

// {time_ms, tps%, rpm, speed_kmh}
// Two Turbo triggers expected: at ~2800ms (1st→2nd) and ~5000ms (2nd→3rd).
struct DataPoint { uint32_t t; float tps; float rpm; float speed; };
static const DataPoint SCENARIO[] = {
  {    0,  0,   800,  0  },
  {  800, 30,  1500, 10  },
  { 1500, 75,  2800, 20  },
  { 2200, 85,  3300, 26  },
  { 2800,  4,  3200, 28  },  // *** Turbo #1 ***
  { 3000, 55,  2400, 33  },
  { 3600, 80,  3100, 42  },
  { 4300, 85,  3500, 46  },
  { 5000,  4,  3300, 48  },  // *** Turbo #2 ***
  { 5200, 45,  2200, 55  },
  { 6000, 60,  2700, 62  },
  { 7000,  3,  2500, 65  },
  { 8000,  0,  1000, 30  },
  { 9000,  0,   800,  0  },
};
static const int SCENARIO_LEN = sizeof(SCENARIO) / sizeof(SCENARIO[0]);

void advanceScenario() {
  uint32_t elapsed = millis() - scenStart;
  while (scenIdx + 1 < SCENARIO_LEN - 1 && SCENARIO[scenIdx + 1].t <= elapsed)
    scenIdx++;
  if (elapsed >= SCENARIO[SCENARIO_LEN - 1].t) {
    scenIdx   = 0;
    scenStart = millis();
    prevTPS   = 0;
    return;
  }
  uint32_t tA = SCENARIO[scenIdx].t;
  uint32_t tB = SCENARIO[scenIdx + 1].t;
  float frac  = (float)(elapsed - tA) / (float)(tB - tA);
  metricTPS   = SCENARIO[scenIdx].tps   + frac * (SCENARIO[scenIdx+1].tps   - SCENARIO[scenIdx].tps);
  metricRPM   = SCENARIO[scenIdx].rpm   + frac * (SCENARIO[scenIdx+1].rpm   - SCENARIO[scenIdx].rpm);
  metricSpeed = SCENARIO[scenIdx].speed + frac * (SCENARIO[scenIdx+1].speed - SCENARIO[scenIdx].speed);
}

#endif // SIMULATION

// ═══════════════════════════════════════════════════════════════════════════
// REAL DEVICE build: Bluetooth OBD2, DFPlayer, state machine
// ═══════════════════════════════════════════════════════════════════════════
#ifndef SIMULATION

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
    String name  = dev->getName().c_str();
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

void doInitElm() {
  showMessage("ELM327", "Initialising...");
  obdSend("ATZ", 3000); delay(500);
  const char* cmds[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* cmd : cmds) { obdSend(cmd, 1500); delay(100); }
  showMessage("Connected!", targetName.c_str());
  delay(800);
  appState = RUNNING;
}

void doRunning() {
  uint32_t now = millis();
  if (now - lastPollMs >= 100) {
    String r;
    r = obdSend("0111"); metricTPS   = max(0.0f, parsePID(r, 1, 100.0f / 255.0f));
    r = obdSend("010D"); metricSpeed = max(0.0f, parsePID(r, 1, 1.0f));
    r = obdSend("010C"); metricRPM   = max(0.0f, parsePID(r, 2, 0.25f));
    checkTurbo(now);
    lastPollMs = now;
  }
  if (now - lastDrawMs >= 50) {
    drawDisplay();
    lastDrawMs = now;
  }
  readEncoder();
}

#endif // !SIMULATION

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Turbo Sound Emulator ===");
#ifdef SIMULATION
  Serial.println("Mode: Wokwi simulation");
  Serial.println("Rotate encoder to cycle views.");
  Serial.println("Push encoder button to reset Turbo counter.");
#else
  Serial.println("Mode: Real device");
#endif

  Wire.begin();
  display.begin();
  display.setFont(u8g2_font_5x7_tr);
  display.clearBuffer();
  display.drawStr(0, 20, "Turbo Emulator");
#ifdef SIMULATION
  display.drawStr(0, 32, "Wokwi simulation");
#else
  display.drawStr(0, 32, "OBD2 v1.0");
#endif
  display.drawStr(0, 44, "Starting...");
  display.sendBuffer();

  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  encBtn.attach(PIN_ENC_SW, INPUT_PULLUP);
  encBtn.interval(10);
  lastClk = digitalRead(PIN_ENC_CLK);

#ifdef SIMULATION
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  delay(800);
  scenStart = millis();
#else
  Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
  BT.begin("ESP32-OBD", true);
  if (dfplayer.begin(Serial2)) {
    dfplayer.volume(25);
    Serial.println("DFPlayer ready");
  } else {
    Serial.println("DFPlayer failed — Turbo sounds disabled");
  }
  delay(1500);
#endif
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
#ifdef SIMULATION
  uint32_t now = millis();
  readEncoder();  // polled every iteration — no delay, so no pulses are missed
  if (now - lastDrawMs >= 50) {
    advanceScenario();
    checkTurbo(now);
    digitalWrite(PIN_LED, (now < turboSoundUntilMs && (now / 100) % 2 == 0) ? HIGH : LOW);
    drawDisplay();
    lastDrawMs = now;
  }
#else
  switch (appState) {
    case SCANNING:   doScanning();   break;
    case CONNECTING: doConnecting(); break;
    case INIT_ELM:   doInitElm();    break;
    case RUNNING:    doRunning();    break;
  }
#endif
}

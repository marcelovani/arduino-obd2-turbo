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
//   OBD2 data replayed from a built-in scenario.
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
#define TURBO_VOLUME_GEAR1   30    // 100% — DFPlayer max is 30
#define TURBO_VOLUME_GEAR2   21    // 70%

// ── Engine state threshold ────────────────────────────────────────────────
#define ENGINE_IDLE_RPM  200.0f   // below this = engine off → parked screen

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

float    metricTPS      = 0;
float    metricSpeed    = 0;
float    metricRPM      = 0;
float    metricVoltage  = 0;
float    metricCoolant  = -999;  // °C, -999 = not yet read
float    prevTPS        = 0;
uint32_t lastTurboMs    = 0;
uint32_t turboCount     = 0;
uint32_t turboUntilMs   = 0;
uint32_t lastDrawMs     = 0;

#ifdef SIMULATION
  uint32_t turboSoundUntilMs = 0;
  int      scenIdx            = 0;
  uint32_t scenStart          = 0;
  enum SimPhase { SIM_SCANNING, SIM_CONNECTING, SIM_INIT, SIM_RUNNING };
  SimPhase simPhase      = SIM_SCANNING;
  uint32_t simPhaseStart = 0;
#else
  enum AppState { SCANNING, CONNECTING, INIT_ELM, RUNNING };
  AppState appState      = SCANNING;
  String   targetName    = "";
  uint32_t lastPollMs    = 0;
  uint32_t lastIdlePollMs = 0;
  int      scanFrame     = 0;
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
    dfplayer.volume(gear == 1 ? TURBO_VOLUME_GEAR1 : TURBO_VOLUME_GEAR2);
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

// ── Shared display helpers ────────────────────────────────────────────────
void showMessage(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(0, 18, line1);
  display.setFont(u8g2_font_ncenB08_tr);
  if (line2) display.drawStr(0, 34, line2);
  if (line3) display.drawStr(0, 50, line3);
  display.sendBuffer();
}

void drawParked(const char* deviceName) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);

  char nameBuf[18];
  strncpy(nameBuf, deviceName, 17); nameBuf[17] = '\0';
  display.drawStr(0, 10, nameBuf);
  display.drawHLine(0, 13, 128);

  char buf[24];
  if (metricVoltage > 0) {
    snprintf(buf, sizeof(buf), "Battery:  %.1f V", metricVoltage);
    display.drawStr(0, 27, buf);
  }
  if (metricCoolant > -999) {
    snprintf(buf, sizeof(buf), "Coolant:  %.0f C", metricCoolant);
    display.drawStr(0, 40, buf);
  }

  if ((millis() / 600) % 2 == 0)
    display.drawStr(0, 56, "Start engine...");

  display.sendBuffer();
}

// ── Gauge display ─────────────────────────────────────────────────────────
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
// SIMULATION build: scenario playback, startup phases, LED blink
// ═══════════════════════════════════════════════════════════════════════════
#ifdef SIMULATION

// {time_ms, tps%, rpm, speed_kmh}
// Starts with 2s engine-off (parked screen), then engine starts and drives.
// Two Turbo triggers expected: at ~4800ms (1st→2nd) and ~7000ms (2nd→3rd).
struct DataPoint { uint32_t t; float tps; float rpm; float speed; };
static const DataPoint SCENARIO[] = {
  {    0,  0,    0,  0  },  // engine off → parked screen
  { 2000,  0,  800,  0  },  // engine starts → gauge screen
  { 2800, 30, 1500, 10  },  // pulling away in 1st
  { 3500, 75, 2800, 20  },  // accelerating hard
  { 4200, 85, 3300, 26  },  // near red-line 1st gear
  { 4800,  4, 3200, 28  },  // *** Turbo #1 *** (1st→2nd)
  { 5000, 55, 2400, 33  },  // back on throttle in 2nd
  { 5600, 80, 3100, 42  },  // hard acceleration in 2nd
  { 6300, 85, 3500, 46  },  // near red-line 2nd gear
  { 7000,  4, 3300, 48  },  // *** Turbo #2 *** (2nd→3rd)
  { 7200, 45, 2200, 55  },  // into 3rd, steady throttle
  { 8000, 60, 2700, 62  },  // cruising 3rd — no Turbo
  { 9000,  3, 2500, 65  },  // lift in 3rd — no Turbo (gear > max_gear)
  {10000,  0, 1000, 30  },  // braking
  {11000,  0,  800,  0  },  // back to idle — loop restarts
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

float parseVoltage(const String& resp) {
  float v = strtof(resp.c_str(), nullptr);
  return (v > 5.0f && v < 20.0f) ? v : 0;
}

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

void doScanning() {
  static const char SPIN[] = {'-', '/', '|', '\\'};
  char line1[22];
  snprintf(line1, sizeof(line1), "Scanning OBD2... %c", SPIN[scanFrame % 4]);
  scanFrame++;
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(0, 20, line1);
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 38, "Plug ELM327 into");
  display.drawStr(0, 52, "OBD2 port & wait");
  display.sendBuffer();

  Serial.println("Scanning for ELM327...");
  BTScanResults* results = BT.discover(8000);
  if (!results || results->getCount() == 0) {
    showMessage("No device found", "Retrying...");
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
  char nameBuf[18];
  strncpy(nameBuf, targetName.c_str(), 17); nameBuf[17] = '\0';
  showMessage("Found:", nameBuf, "Connecting...");

  const char* pins[] = {"1234", "0000"};
  for (const char* pin : pins) {
    Serial.printf("  Trying PIN %s\n", pin);
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
  char nameBuf[18];
  strncpy(nameBuf, targetName.c_str(), 17); nameBuf[17] = '\0';
  showMessage("Initialising...", nameBuf);

  obdSend("ATZ", 3000); delay(500);
  const char* cmds[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* cmd : cmds) { obdSend(cmd, 1500); delay(100); }

  String vResp = obdSend("ATRV", 1500);
  metricVoltage = parseVoltage(vResp);

  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(0, 18, "OBD2 connected!");
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 32, nameBuf);
  if (metricVoltage > 0) {
    char vbuf[20];
    snprintf(vbuf, sizeof(vbuf), "Battery: %.1f V", metricVoltage);
    display.drawStr(0, 48, vbuf);
  }
  display.sendBuffer();
  delay(1500);
  appState = RUNNING;
}

void doRunning() {
  uint32_t now = millis();
  readEncoder();

  if (metricRPM < ENGINE_IDLE_RPM) {
    // Engine off — slow-poll voltage/coolant, show parked screen
    if (now - lastIdlePollMs >= 3000) {
      String r; float v;
      r = obdSend("ATRV", 1500); v = parseVoltage(r);          if (v > 0)   metricVoltage = v;
      r = obdSend("0105", 1000); v = parsePID(r, 1, 1.0f);     if (v >= 0)  metricCoolant = v - 40.0f;
      r = obdSend("010C", 1000); v = parsePID(r, 2, 0.25f);    if (v >= 0)  metricRPM     = v;
      lastIdlePollMs = now;
    }
    if (now - lastDrawMs >= 100) {
      drawParked(targetName.c_str());
      lastDrawMs = now;
    }
  } else {
    // Engine running — fast-poll drive metrics, show gauges
    if (now - lastPollMs >= 100) {
      String r; float v;
      r = obdSend("0111"); v = parsePID(r, 1, 100.0f / 255.0f); if (v >= 0) metricTPS   = v;
      r = obdSend("010D"); v = parsePID(r, 1, 1.0f);             if (v >= 0) metricSpeed = v;
      r = obdSend("010C"); v = parsePID(r, 2, 0.25f);             if (v >= 0) metricRPM   = v;
      checkTurbo(now);
      lastPollMs = now;
    }
    if (now - lastDrawMs >= 50) {
      drawDisplay();
      lastDrawMs = now;
    }
  }
}

#endif // !SIMULATION

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Turbo Sound Emulator ===");

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
  simPhaseStart = millis();
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
  readEncoder();

  switch (simPhase) {

    case SIM_SCANNING: {
      // Spinner animates in real time using millis()
      if (now - lastDrawMs >= 150) {
        static const char SPIN[] = {'-', '/', '|', '\\'};
        char line1[22];
        snprintf(line1, sizeof(line1), "Scanning OBD2... %c", SPIN[(now / 150) % 4]);
        display.clearBuffer();
        display.setFont(u8g2_font_ncenB10_tr);
        display.drawStr(0, 20, line1);
        display.setFont(u8g2_font_ncenB08_tr);
        display.drawStr(0, 38, "Looking for ELM327");
        display.sendBuffer();
        lastDrawMs = now;
      }
      if (now - simPhaseStart >= 3000) {
        simPhase = SIM_CONNECTING;
        simPhaseStart = now;
      }
      break;
    }

    case SIM_CONNECTING: {
      if (now - lastDrawMs >= 50) {
        showMessage("Found:", "ELM327-SIM", "Connecting...");
        lastDrawMs = now;
      }
      if (now - simPhaseStart >= 2000) {
        simPhase = SIM_INIT;
        simPhaseStart = now;
        // Set simulated values shown on the confirmation screen
        metricVoltage = 12.4f;
        metricCoolant = 18.0f;  // cold engine
      }
      break;
    }

    case SIM_INIT: {
      bool confirmed = (now - simPhaseStart >= 1500);
      if (now - lastDrawMs >= 50) {
        if (!confirmed) {
          showMessage("Initialising...", "ELM327-SIM");
        } else {
          display.clearBuffer();
          display.setFont(u8g2_font_ncenB10_tr);
          display.drawStr(0, 18, "OBD2 connected!");
          display.setFont(u8g2_font_ncenB08_tr);
          display.drawStr(0, 32, "ELM327-SIM");
          char vbuf[20];
          snprintf(vbuf, sizeof(vbuf), "Battery: %.1f V", metricVoltage);
          display.drawStr(0, 48, vbuf);
          display.sendBuffer();
        }
        lastDrawMs = now;
      }
      if (now - simPhaseStart >= 3000) {
        simPhase  = SIM_RUNNING;
        scenStart = millis();
        Serial.println("[SIM] Starting driving scenario");
      }
      break;
    }

    case SIM_RUNNING: {
      if (now - lastDrawMs >= 50) {
        advanceScenario();
        checkTurbo(now);
        digitalWrite(PIN_LED, (now < turboSoundUntilMs && (now / 100) % 2 == 0) ? HIGH : LOW);
        if (metricRPM < ENGINE_IDLE_RPM) {
          drawParked("ELM327-SIM");
        } else {
          drawDisplay();
        }
        lastDrawMs = now;
      }
      break;
    }
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

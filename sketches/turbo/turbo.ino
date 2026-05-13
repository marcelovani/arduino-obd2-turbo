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

// ── Engine state thresholds ───────────────────────────────────────────────
#define ENGINE_IDLE_RPM    200.0f   // below = engine off → parked screen, poll battery/coolant/RPM
#define ENGINE_DRIVING_RPM 1000.0f  // above = driving → poll TPS + speed + RPM

// ── Encoder sensitivity ───────────────────────────────────────────────────
#define STEPS_PER_ZONE  1   // 1 detent = 1 view change
#define NUM_VIEWS       2   // total display screens

// ── Objects ───────────────────────────────────────────────────────────────
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
Bounce encBtn;

#ifndef SIMULATION
  BluetoothSerial     BT;
  DFRobotDFPlayerMini dfplayer;
#endif

// ── Encoder ISRs (real device only) ──────────────────────────────────────
// Whichever pin falls FIRST determines direction; the second pin's fall is
// ignored because the other ISR already updated its timestamp recently.
// This works for all KY-040 variants regardless of which pin leads for CW.
//   CW:  DT falls first  → encISR_DT  fires, encDelta++
//         CLK falls after → encISR_CLK  sees DtUs was recent, skips
//   CCW: CLK falls first → encISR_CLK fires, encDelta--
//         DT falls after  → encISR_DT  sees ClkUs was recent, skips
// 500 µs cross-pin window filters second edges; 500 µs self-debounce
// filters contact bounce (KY-040 bounce well under 500 µs).
#ifndef SIMULATION
  volatile int           encDelta = 0;
  volatile unsigned long encClkUs = 0;
  volatile unsigned long encDtUs  = 0;

  void IRAM_ATTR encISR_CLK() {
    unsigned long now = micros();
    if (now - encClkUs < 500) return;      // self-debounce
    encClkUs = now;
    if (now - encDtUs > 500) encDelta--;   // CLK led → CCW
  }

  void IRAM_ATTR encISR_DT() {
    unsigned long now = micros();
    if (now - encDtUs < 500) return;       // self-debounce
    encDtUs = now;
    if (now - encClkUs > 500) encDelta++;  // DT led → CW
  }
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
  AppState appState        = SCANNING;
  String   targetName      = "";
  uint32_t lastPollMs      = 0;
  uint32_t lastIdlePollMs  = 0;
  uint32_t lastEncActiveMs = 0;   // millis() of last encoder turn
  int      scanFrame       = 0;
  #define  ENCODER_PRIORITY_MS  500  // pause OBD2 for 500ms after encoder activity
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
#ifdef SIMULATION
  // Rate-limit to 100ms to match real device OBD2 poll rate.
  // Without this, prevTPS updates every frame and the high→low condition
  // is never simultaneously true across a single "tick".
  static uint32_t lastCheckMs = 0;
  if (now - lastCheckMs < 100) return;
  lastCheckMs = now;
#endif
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

#ifdef SIMULATION
  // Simulation: poll CLK directly (no blocking calls to miss pulses)
  int clk = digitalRead(PIN_ENC_CLK);
  if (clk != lastClk && clk == LOW) {
    int delta = (digitalRead(PIN_ENC_DT) != clk) ? -1 : 1;
    int total = NUM_VIEWS * STEPS_PER_ZONE;
    encoderPos  = ((encoderPos + delta) % total + total) % total;
    currentView = encoderPos / STEPS_PER_ZONE;
  }
  lastClk = clk;
#else
  // Real device: consume delta accumulated by the ISR
  if (encDelta != 0) {
    noInterrupts();
    int delta = encDelta;
    encDelta  = 0;
    interrupts();
    int total = NUM_VIEWS * STEPS_PER_ZONE;
    encoderPos    = ((encoderPos + delta) % total + total) % total;
    currentView   = encoderPos / STEPS_PER_ZONE;
    lastEncActiveMs = millis();
  }
#endif
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

// ── Bar helper ────────────────────────────────────────────────────────────
// Draws a framed bar (full display width) filled proportionally.
// Yellow OLED top 16 rows / blue bottom 48 rows — bars live in blue zone.
void drawBar(int x, int y, int w, int h, float value, float maxVal) {
  int fill = (int)(value / maxVal * (w - 2));
  fill = constrain(fill, 0, w - 2);
  display.drawFrame(x, y, w, h);
  if (fill > 0) display.drawBox(x + 1, y + 1, fill, h - 2);
}

// ── Gauge display ─────────────────────────────────────────────────────────
// OLED: top 16 rows = yellow (header), bottom 48 rows = blue (data).
//
// Screen 0 — bars:      Screen 1 — text only:
//  [yellow] Turbo:N Gear:N   [yellow] Turbo:N Gear:N
//  Throttle  N%              Throttle: N%
//  [========  ]              RPM: N
//  RPM  N                    Speed: N mph
//  [====      ]
//  Speed  N mph
//  [===       ]
void drawDisplay() {
  int   gear        = estimateGear(metricRPM, metricSpeed);
  bool  turboRecent = (millis() < turboUntilMs);
  float speedMph    = metricSpeed * 0.621371f;

  display.clearBuffer();

  // ── Yellow zone header (y=0–15) ──────────────────────────────────────────
  display.setFont(u8g2_font_ncenB08_tr);
  char hdr[28];
  if (turboRecent)
    snprintf(hdr, sizeof(hdr), "PSSSSH! #%lu", turboCount);
  else
    snprintf(hdr, sizeof(hdr), "Turbo:%-2lu  Gear:%d", turboCount, gear);
  display.drawStr(0, 11, hdr);

  // ── Blue zone (y=16–63) ──────────────────────────────────────────────────
  display.setFont(u8g2_font_5x7_tr);
  char buf[24];

  if (currentView == 0) {
    // Screen 0: label + value + bar for each metric
    snprintf(buf, sizeof(buf), "Throttle  %.0f%%", metricTPS);
    display.drawStr(0, 23, buf);
    drawBar(0, 25, 128, 5, metricTPS, 100.0f);

    snprintf(buf, sizeof(buf), "RPM  %.0f", metricRPM);
    display.drawStr(0, 38, buf);
    drawBar(0, 40, 128, 5, metricRPM, 8000.0f);

    snprintf(buf, sizeof(buf), "Speed  %.0f mph", speedMph);
    display.drawStr(0, 53, buf);
    drawBar(0, 55, 128, 5, speedMph, 140.0f);

  } else {
    // Screen 1: text only
    snprintf(buf, sizeof(buf), "Throttle: %.0f%%", metricTPS);
    display.drawStr(0, 26, buf);
    snprintf(buf, sizeof(buf), "RPM: %.0f", metricRPM);
    display.drawStr(0, 40, buf);
    snprintf(buf, sizeof(buf), "Speed: %.0f mph", speedMph);
    display.drawStr(0, 55, buf);
  }

  display.sendBuffer();
}

// ═══════════════════════════════════════════════════════════════════════════
// SIMULATION build: scenario playback, startup phases, LED blink
// ═══════════════════════════════════════════════════════════════════════════
#ifdef SIMULATION

// {time_ms, tps%, rpm, speed_kmh}
// Full cycle (~20s):
//   0–3s   ignition on, engine off  → parked screen
//   3–8s   engine idling, not moving → idle screen (RPM 200–999)
//   8–15s  driving 1st → 2nd → 3rd  → two Turbo triggers
//   15–20s stopped, engine idle      → idle screen
// Turbo #1 at ~10300ms (1st→2nd), Turbo #2 at ~12500ms (2nd→3rd)
struct DataPoint { uint32_t t; float tps; float rpm; float speed; };
static const DataPoint SCENARIO[] = {
  {     0,  0,    0,   0 },  // ignition on, engine off → parked screen
  {  3000,  0,    0,   0 },  // end of ignition-on window
  {  3100,  0,  750,   0 },  // engine cranks → idle screen appears (RPM 200-999)
  {  8000,  0,  850,   0 },  // 5s idling — rotary fully responsive
  {  8500, 40, 1600,   8 },  // pull away in 1st — driving mode starts
  {  9200, 80, 3000,  22 },  // hard acceleration 1st
  {  9800, 85, 3400,  27 },  // near red-line 1st — prevTPS will be 85
  {  9801,  4, 3200,  30 },  // *** Turbo #1 *** instant drop (1ms gap)
  { 10000, 55, 2500,  35 },  // back on throttle 2nd
  { 11200, 80, 3100,  44 },  // hard acceleration 2nd
  { 11900, 85, 3500,  50 },  // near red-line 2nd — prevTPS will be 85
  { 11901,  4, 3300,  52 },  // *** Turbo #2 *** instant drop (1ms gap)
  { 12100, 45, 2200,  58 },  // into 3rd
  { 13000, 40, 2600,  66 },  // cruising 3rd
  { 13800,  0, 1800,  55 },  // lift in 3rd — no Turbo (gear 3 > max)
  { 15000,  0,  850,   0 },  // braked to stop → idle screen
  { 20000,  0,  800,   0 },  // 5s idle then loop
};
static const int SCENARIO_LEN = sizeof(SCENARIO) / sizeof(SCENARIO[0]);

void advanceScenario() {
  uint32_t elapsed = millis() - scenStart;
  while (scenIdx + 1 < SCENARIO_LEN - 1 && SCENARIO[scenIdx + 1].t <= elapsed)
    scenIdx++;
  if (elapsed >= SCENARIO[SCENARIO_LEN - 1].t) {
    // Restart full cycle from the beginning
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

String obdSend(const char* cmd, uint16_t timeout = 300) {
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
  bool encActive = (now - lastEncActiveMs < ENCODER_PRIORITY_MS);

  if (metricRPM < ENGINE_IDLE_RPM) {
    // ── PARKED: engine off — poll battery, coolant, RPM every 3s ──────────
    if (!encActive && now - lastIdlePollMs >= 3000) {
      String r; float v;
      r = obdSend("ATRV", 1500); v = parseVoltage(r);       if (v > 0)  metricVoltage = v;
      r = obdSend("0105");       v = parsePID(r, 1, 1.0f);  if (v >= 0) metricCoolant = v - 40.0f;
      r = obdSend("010C");       v = parsePID(r, 2, 0.25f); if (v >= 0) metricRPM     = v;
      lastIdlePollMs = now;
    }
    if (now - lastDrawMs >= 200) {
      drawParked(targetName.c_str());
      lastDrawMs = now;
    }

  } else if (metricRPM < ENGINE_DRIVING_RPM) {
    // ── IDLE: engine running, not moving — poll RPM only every 500ms ──────
    // Rotary is fully unblocked; TPS/speed not needed, no Turbo possible.
    if (!encActive && now - lastPollMs >= 500) {
      String r; float v;
      r = obdSend("010C"); v = parsePID(r, 2, 0.25f); if (v >= 0) metricRPM = v;
      lastPollMs = now;
    }
    if (now - lastDrawMs >= 50) {
      drawDisplay();
      lastDrawMs = now;
    }

  } else {
    // ── DRIVING: RPM ≥ 1000 — poll TPS + speed + RPM every 100ms ─────────
    if (!encActive && now - lastPollMs >= 100) {
      String r; float v;
      r = obdSend("0111"); v = parsePID(r, 1, 100.0f / 255.0f); if (v >= 0) metricTPS   = v;
      r = obdSend("010D"); v = parsePID(r, 1, 1.0f);             if (v >= 0) metricSpeed = v;
      r = obdSend("010C"); v = parsePID(r, 2, 0.25f);            if (v >= 0) metricRPM   = v;
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
#ifndef SIMULATION
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encISR_CLK, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT),  encISR_DT,  FALLING);
#endif

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

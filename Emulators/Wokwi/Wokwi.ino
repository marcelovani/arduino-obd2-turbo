// Wokwi simulation — OBD2 Turbo Sound Emulator
//
// No Bluetooth / ELM327 in this simulation. OBD2 data is replayed from a
// built-in driving scenario that loops automatically and demonstrates two
// Turbo triggers (1st→2nd and 2nd→3rd gear changes) followed by cruising.
//
// Hardware wired in the diagram:
//   SSD1306 OLED  — I2C (SDA=GPIO21, SCL=GPIO22)
//   KY-040 encoder — CLK=GPIO25, DT=GPIO26, SW=GPIO27
//   LED (DFPlayer placeholder) — GPIO17 via 1 kΩ resistor
//
// Rotate encoder → cycle display views.
// Push encoder button → reset Turbo counter.
//
// Libraries: U8g2, Bounce2

#include <Wire.h>
#include <U8g2lib.h>
#include <Bounce2.h>

// ── Pins ──────────────────────────────────────────────────────────────────
#define PIN_ENC_CLK  25
#define PIN_ENC_DT   26
#define PIN_ENC_SW   27
#define PIN_LED      17   // DFPlayer TX placeholder — LED lights while MP3 would play

// ── Turbo thresholds (mirror constants in phase3_turbo.ino / obd_logic.py) ───
#define TURBO_THROTTLE_HIGH  40.0f
#define TURBO_THROTTLE_LOW   10.0f
#define TURBO_RPM_MIN        1500.0f
#define TURBO_MAX_GEAR       2
#define TURBO_COOLDOWN_MS    2000

// ── Objects ───────────────────────────────────────────────────────────────
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
Bounce encBtn;

// ── State ─────────────────────────────────────────────────────────────────
#define STEPS_PER_ZONE 1          // encoder detents per zone; 4 zones = 1 full turn
int      currentView  = 0;
int      encoderPos   = 0;        // 0..(4*STEPS_PER_ZONE - 1)
int      lastClk      = HIGH;

float    metricTPS    = 0;
float    metricSpeed  = 0;
float    metricRPM    = 0;
float    prevTPS      = 0;
uint32_t lastTurboMs    = 0;
uint32_t turboCount     = 0;
uint32_t turboUntilMs   = 0;    // show "PSSSSH!" on OLED until this time
uint32_t turboSoundUntilMs = 0; // filled circle indicator while sound plays
uint32_t lastDrawMs        = 0;

// ── Gear estimation ───────────────────────────────────────────────────────
// RPM / speed ratio thresholds tuned for a typical small European petrol car.
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

// ── Driving scenario ──────────────────────────────────────────────────────
// {time_ms, tps%, rpm, speed_kmh}
// Two Turbo triggers expected: at ~2800ms and ~5000ms.
struct DataPoint { uint32_t t; float tps; float rpm; float speed; };

static const DataPoint SCENARIO[] = {
  {    0,  0,   800,  0  },  // idle
  {  800, 30,  1500, 10  },  // pulling away in 1st
  { 1500, 75,  2800, 20  },  // accelerating hard
  { 2200, 85,  3300, 26  },  // near red-line 1st gear
  { 2800,  4,  3200, 28  },  // *** Turbo #1 *** (1st→2nd, TPS drops, RPM 3200)
  { 3000, 55,  2400, 33  },  // back on throttle in 2nd
  { 3600, 80,  3100, 42  },  // hard acceleration in 2nd
  { 4300, 85,  3500, 46  },  // near red-line 2nd gear
  { 5000,  4,  3300, 48  },  // *** Turbo #2 *** (2nd→3rd, 2200ms after Turbo#1)
  { 5200, 45,  2200, 55  },  // into 3rd, steady throttle
  { 6000, 60,  2700, 62  },  // cruising 3rd — ratio ~44, gear 3: no Turbo
  { 7000,  3,  2500, 65  },  // lift in 3rd — no Turbo (gear > max_gear)
  { 8000,  0,  1000, 30  },  // braking
  { 9000,  0,   800,  0  },  // back to idle — loop restarts
};
static const int SCENARIO_LEN = sizeof(SCENARIO) / sizeof(SCENARIO[0]);

int      scenIdx   = 0;
uint32_t scenStart = 0;

// ── Scenario playback ─────────────────────────────────────────────────────
void advanceScenario() {
  uint32_t elapsed = millis() - scenStart;

  // Advance index until we reach the current time
  while (scenIdx + 1 < SCENARIO_LEN - 1 &&
         SCENARIO[scenIdx + 1].t <= elapsed) {
    scenIdx++;
  }

  if (elapsed >= SCENARIO[SCENARIO_LEN - 1].t) {
    // Restart loop
    scenIdx   = 0;
    scenStart = millis();
    prevTPS   = 0;
    return;
  }

  // Linear interpolation between adjacent data points
  uint32_t tA = SCENARIO[scenIdx].t;
  uint32_t tB = SCENARIO[scenIdx + 1].t;
  float frac  = (float)(elapsed - tA) / (float)(tB - tA);

  metricTPS   = SCENARIO[scenIdx].tps   + frac * (SCENARIO[scenIdx+1].tps   - SCENARIO[scenIdx].tps);
  metricRPM   = SCENARIO[scenIdx].rpm   + frac * (SCENARIO[scenIdx+1].rpm   - SCENARIO[scenIdx].rpm);
  metricSpeed = SCENARIO[scenIdx].speed + frac * (SCENARIO[scenIdx+1].speed - SCENARIO[scenIdx].speed);
}

// ── Turbo trigger check ────────────────────────────────────────────────────
void checkTurbo(uint32_t now) {
  int gear = estimateGear(metricRPM, metricSpeed);
  if (prevTPS          > TURBO_THROTTLE_HIGH  &&
      metricTPS        < TURBO_THROTTLE_LOW   &&
      metricRPM        > TURBO_RPM_MIN        &&
      gear            <= TURBO_MAX_GEAR       &&
      now - lastTurboMs  > TURBO_COOLDOWN_MS) {
    turboCount++;
    lastTurboMs       = now;
    turboUntilMs      = now + 800;
    turboSoundUntilMs = now + 1000; // blink for 1 s; real sketch uses DFPlayer BUSY pin
    Serial.printf("[Turbo] #%lu  gear=%d  TPS %.0f→%.0f  RPM %.0f\n",
                  turboCount, gear, prevTPS, metricTPS, metricRPM);
  }
  prevTPS = metricTPS;
}

// ── Encoder ───────────────────────────────────────────────────────────────
void readEncoder() {
  encBtn.update();
  if (encBtn.fell()) {
    turboCount  = 0;
    lastTurboMs = 0;
    Serial.println("[ENC] Turbo counter reset");
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

// ── OLED rendering ───────────────────────────────────────────────────────
void drawDisplay() {
  int gear = estimateGear(metricRPM, metricSpeed);
  bool turboRecent = (millis() < turboUntilMs);

  display.clearBuffer();

  if (turboRecent) {
    // Big Turbo flash — override top line
    display.setFont(u8g2_font_ncenB10_tr);
    char turbo[20];
    snprintf(turbo, sizeof(turbo), "** PSSSSH! #%lu **", turboCount);
    display.drawStr(0, 12, turbo);
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

  if (currentView == 0) {
    // Large throttle display
    display.setFont(u8g2_font_ncenB24_tr);
    char buf[8];
    snprintf(buf, sizeof(buf), "%3.0f%%", metricTPS);
    display.drawStr(14, 58, buf);
    display.setFont(u8g2_font_5x7_tr);
    display.drawStr(0, 20, "THROTTLE");

    // Throttle bar (100 px wide, 8 px tall)
    int barW = (int)(metricTPS * 1.0f);   // 100% = 100 px
    display.drawFrame(0, 24, 102, 8);
    if (barW > 0) display.drawBox(1, 25, barW, 6);

  } else if (currentView == 1) {
    // Large speed display
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
    // All metrics summary
    display.setFont(u8g2_font_5x7_tr);
    char line[32];
    snprintf(line, sizeof(line), "TPS:  %5.1f %%", metricTPS);
    display.drawStr(0, 20, line);
    snprintf(line, sizeof(line), "SPD:  %5.1f km/h", metricSpeed);
    display.drawStr(0, 30, line);
    snprintf(line, sizeof(line), "RPM:  %5.0f", metricRPM);
    display.drawStr(0, 40, line);
    snprintf(line, sizeof(line), "Gear: %d", gear);
    display.drawStr(0, 50, line);

  } else {
    // Dual bar — throttle + speed, gear shown in header
    display.setFont(u8g2_font_5x7_tr);

    // TPS bar
    display.drawStr(0, 22, "Throttle");
    char tpsbuf[8];
    snprintf(tpsbuf, sizeof(tpsbuf), "%3.0f%%", metricTPS);
    display.drawStr(90, 22, tpsbuf);
    int tpsW = (int)metricTPS;
    display.drawFrame(0, 24, 102, 10);
    if (tpsW > 0) display.drawBox(1, 25, tpsW, 8);

    // Speed bar (0–200 km/h scale)
    display.drawStr(0, 44, "Speed");
    char spdbuf[10];
    snprintf(spdbuf, sizeof(spdbuf), "%3.0f", metricSpeed);
    display.drawStr(90, 44, spdbuf);
    int spdW = min(100, (int)(metricSpeed / 2.0f));
    display.drawFrame(0, 46, 102, 10);
    if (spdW > 0) display.drawBox(1, 47, spdW, 8);
  }

  // View indicator: 4 circles on right edge, filled = current view
  for (int i = 0; i < 4; i++) {
    if (i == currentView) display.drawDisc(124, 6 + i * 12, 3);
    else                  display.drawCircle(124, 6 + i * 12, 3);
  }

  display.sendBuffer();
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Turbo Emulator (Wokwi simulation) ===");
  Serial.println("Rotate encoder to cycle views.");
  Serial.println("Push encoder button to reset Turbo counter.\n");

  display.begin();
  display.setFont(u8g2_font_5x7_tr);
  display.clearBuffer();
  display.drawStr(0, 20, "Turbo Emulator");
  display.drawStr(0, 32, "Wokwi simulation");
  display.drawStr(0, 44, "Starting...");
  display.sendBuffer();

  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  encBtn.attach(PIN_ENC_SW, INPUT_PULLUP);
  encBtn.interval(10);
  lastClk = digitalRead(PIN_ENC_CLK);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  delay(800);
  scenStart = millis();
}

// ── Loop ─────────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  readEncoder();  // polled every iteration — no delay, so no pulses are missed

  if (now - lastDrawMs >= 50) {
    advanceScenario();
    checkTurbo(now);
    if (now < turboSoundUntilMs) {
      digitalWrite(PIN_LED, (now / 100) % 2 == 0 ? HIGH : LOW); // blink ~5 Hz
    } else {
      digitalWrite(PIN_LED, LOW);
    }
    drawDisplay();
    lastDrawMs = now;
  }
}

// Wokwi simulation — OBD2 Turbo BOV Emulator
//
// No Bluetooth / ELM327 in this simulation. OBD2 data is replayed from a
// built-in driving scenario that loops automatically and demonstrates two
// BOV triggers (1st→2nd and 2nd→3rd gear changes) followed by cruising.
//
// Hardware wired in the diagram:
//   SSD1306 OLED  — I2C (SDA=GPIO21, SCL=GPIO22)
//   MPU6050 IMU   — I2C (SDA=GPIO21, SCL=GPIO22)
//   KY-040 encoder — CLK=GPIO25, DT=GPIO26, SW=GPIO27
//   Buzzer (DFPlayer placeholder) — GPIO17 via 1 kΩ resistor
//
// Rotate encoder → cycle display views.
// Push encoder button → reset BOV counter.
//
// Libraries: U8g2, Adafruit MPU6050, Adafruit Unified Sensor, Bounce2

#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Bounce2.h>

// ── Pins ──────────────────────────────────────────────────────────────────
#define PIN_ENC_CLK  25
#define PIN_ENC_DT   26
#define PIN_ENC_SW   27
#define PIN_BUZZER   17   // DFPlayer TX placeholder — buzzer gives audible feedback

// ── BOV thresholds (mirror constants in phase3_bov.ino / obd_logic.py) ───
#define BOV_THROTTLE_HIGH  40.0f
#define BOV_THROTTLE_LOW   10.0f
#define BOV_RPM_MIN        1500.0f
#define BOV_MAX_GEAR       2
#define BOV_COOLDOWN_MS    2000

// ── Objects ───────────────────────────────────────────────────────────────
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
Adafruit_MPU6050 mpu;
Bounce encBtn;

// ── State ─────────────────────────────────────────────────────────────────
int      currentView  = 0;    // 0=throttle, 1=speed, 2=all metrics
int      lastClk      = HIGH;
float    gforce       = 0.0f; // longitudinal G from MPU6050

float    metricTPS    = 0;
float    metricSpeed  = 0;
float    metricRPM    = 0;
float    prevTPS      = 0;
uint32_t lastBovMs    = 0;
uint32_t bovCount     = 0;
uint32_t bovUntilMs   = 0;    // show "PSSSSH!" on OLED until this time

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
// Two BOV triggers expected: at ~2800ms and ~5000ms.
struct DataPoint { uint32_t t; float tps; float rpm; float speed; };

static const DataPoint SCENARIO[] = {
  {    0,  0,   800,  0  },  // idle
  {  800, 30,  1500, 10  },  // pulling away in 1st
  { 1500, 75,  2800, 20  },  // accelerating hard
  { 2200, 85,  3300, 26  },  // near red-line 1st gear
  { 2800,  4,  3200, 28  },  // *** BOV #1 *** (1st→2nd, TPS drops, RPM 3200)
  { 3000, 55,  2400, 33  },  // back on throttle in 2nd
  { 3600, 80,  3100, 42  },  // hard acceleration in 2nd
  { 4300, 85,  3500, 46  },  // near red-line 2nd gear
  { 5000,  4,  3300, 48  },  // *** BOV #2 *** (2nd→3rd, 2200ms after BOV#1)
  { 5200, 45,  2200, 55  },  // into 3rd, steady throttle
  { 6000, 60,  2700, 62  },  // cruising 3rd — ratio ~44, gear 3: no BOV
  { 7000,  3,  2500, 65  },  // lift in 3rd — no BOV (gear > max_gear)
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

// ── BOV trigger check ────────────────────────────────────────────────────
void checkBov(uint32_t now) {
  int gear = estimateGear(metricRPM, metricSpeed);
  if (prevTPS          > BOV_THROTTLE_HIGH  &&
      metricTPS        < BOV_THROTTLE_LOW   &&
      metricRPM        > BOV_RPM_MIN        &&
      gear            <= BOV_MAX_GEAR       &&
      now - lastBovMs  > BOV_COOLDOWN_MS) {
    bovCount++;
    lastBovMs  = now;
    bovUntilMs = now + 800;
    tone(PIN_BUZZER, 900, 350);
    Serial.printf("[BOV] #%lu  gear=%d  TPS %.0f→%.0f  RPM %.0f\n",
                  bovCount, gear, prevTPS, metricTPS, metricRPM);
  }
  prevTPS = metricTPS;
}

// ── Encoder ───────────────────────────────────────────────────────────────
void readEncoder() {
  encBtn.update();
  if (encBtn.fell()) {
    bovCount  = 0;
    lastBovMs = 0;
    Serial.println("[ENC] BOV counter reset");
  }
  int clk = digitalRead(PIN_ENC_CLK);
  if (clk != lastClk && clk == LOW) {
    if (digitalRead(PIN_ENC_DT) != clk) {
      currentView = (currentView + 1) % 3;
    } else {
      currentView = (currentView + 2) % 3;
    }
  }
  lastClk = clk;
}

// ── IMU ───────────────────────────────────────────────────────────────────
void readIMU() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  // Longitudinal G: forward acceleration minus gravity component on Z axis
  gforce = (a.acceleration.z / 9.81f) - 1.0f;
}

// ── OLED rendering ───────────────────────────────────────────────────────
void drawDisplay() {
  int gear = estimateGear(metricRPM, metricSpeed);
  bool bovRecent = (millis() < bovUntilMs);

  display.clearBuffer();

  if (bovRecent) {
    // Big BOV flash — override top line
    display.setFont(u8g2_font_ncenB10_tr);
    char bov[20];
    snprintf(bov, sizeof(bov), "** PSSSSH! #%lu **", bovCount);
    display.drawStr(0, 12, bov);
    display.setFont(u8g2_font_5x7_tr);
  } else {
    display.setFont(u8g2_font_5x7_tr);
    char hdr[24];
    snprintf(hdr, sizeof(hdr), "BOV:%lu  G:%+.1f  [%d]", bovCount, gforce, currentView + 1);
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

  } else {
    // All metrics summary
    display.setFont(u8g2_font_5x7_tr);
    char line[32];
    snprintf(line, sizeof(line), "TPS:  %5.1f %%", metricTPS);
    display.drawStr(0, 20, line);
    snprintf(line, sizeof(line), "SPD:  %5.1f km/h", metricSpeed);
    display.drawStr(0, 30, line);
    snprintf(line, sizeof(line), "RPM:  %5.0f", metricRPM);
    display.drawStr(0, 40, line);
    snprintf(line, sizeof(line), "Gear: %d    G:%+.2f", gear, gforce);
    display.drawStr(0, 50, line);
    snprintf(line, sizeof(line), "BOV:  %lu", bovCount);
    display.drawStr(0, 60, line);
  }

  display.sendBuffer();
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== BOV Emulator (Wokwi simulation) ===");
  Serial.println("Rotate encoder to cycle views.");
  Serial.println("Push encoder button to reset BOV counter.\n");

  display.begin();
  display.setFont(u8g2_font_5x7_tr);
  display.clearBuffer();
  display.drawStr(0, 20, "BOV Emulator");
  display.drawStr(0, 32, "Wokwi simulation");
  display.drawStr(0, 44, "Starting...");
  display.sendBuffer();

  if (!mpu.begin()) {
    display.clearBuffer();
    display.drawStr(0, 24, "MPU6050 error!");
    display.sendBuffer();
    while (true) delay(1000);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);

  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  encBtn.attach(PIN_ENC_SW, INPUT_PULLUP);
  encBtn.interval(10);
  lastClk = digitalRead(PIN_ENC_CLK);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  delay(800);
  scenStart = millis();
}

// ── Loop ─────────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  readEncoder();
  readIMU();
  advanceScenario();
  checkBov(now);
  drawDisplay();

  delay(50);  // ~20 Hz
}

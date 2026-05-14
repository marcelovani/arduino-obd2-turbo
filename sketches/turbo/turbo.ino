// turbo.ino — OBD2 Turbo Sound Emulator
//
// Single sketch for both real device and Wokwi simulation.
// Build targets:
//   Real device : arduino-cli compile --fqbn esp32:esp32:esp32doit-devkit-v1 sketches/turbo
//   Wokwi sim   : same + --build-property "compiler.cpp.extra_flags=-DSIMULATION"
//
// Hardware (real device):
//   SSD1306 OLED   — SPI: SCK=GPIO18, MOSI=GPIO23, RES=GPIO15, DC=GPIO32, CS=GPIO5
//                    Breadboard: OLED pins plug into col J rows 3-9 alongside ESP32
//   KY-040 encoder — CLK=GPIO25, DT=GPIO26, SW=GPIO27
//                    Breadboard: col D rows 30-34
//   DFPlayer Mini  — Serial2 (RX2=GPIO16, TX2=GPIO17), VCC=3.3V, microSD root: 01.mp3 (FAT order)
//                    1kΩ resistor on RX line (breadboard E21–F21)
//                    Breadboard: cols D-G rows 20-27
//   ELM327 dongle  — Bluetooth Classic, OBD2 port
//
// Hardware (Wokwi simulation):
//   SSD1306 OLED (I2C, GPIO21/22 — Wokwi SSD1306 is I2C only), KY-040 encoder, buzzer on GPIO17, LED on GPIO4
//   OBD2 data replayed from a built-in scenario.
//
// Encoder: rotate = cycle views / navigate menu, click = open menu / confirm

#include <U8g2lib.h>
#include <Bounce2.h>

#ifdef SIMULATION
  #include <Wire.h>
#else
  #include <SPI.h>
  #ifndef DEMO
    #include <BluetoothSerial.h>
  #endif
  #include <DFRobotDFPlayerMini.h>
#endif

// ── Pins ──────────────────────────────────────────────────────────────────
#define PIN_ENC_CLK  25
#define PIN_ENC_DT   26
#define PIN_ENC_SW   27

#ifdef SIMULATION
  #define PIN_BUZZER    17   // passive buzzer — beeps when Turbo fires (TX2)
  #define PIN_LED        4   // LED — blinks while Turbo sound plays
#else
  #define PIN_OLED_CS    5   // SPI chip select
  #define PIN_OLED_DC   32   // SPI data/command
  #define PIN_OLED_RES  15   // SPI reset
  // SCK=GPIO18, MOSI=GPIO23 — VSPI hardware defaults, no define needed
  #define PIN_DFP_RX    16
  #define PIN_DFP_TX    17
  #define PIN_LED        2   // built-in blue LED — blinks when Turbo fires
#endif

// ── Audio track numbers (/mp3/00NN.mp3 on SD card) ───────────────────────
#define TRACK_PAIRING      1   // "Pairing"            — scanning for ELM327
#define TRACK_NO_OBD2      4   // "OBD2 not connected" — scan timeout or connect fail
#define TRACK_DEMO_MODE    8   // "Demo mode"          — demo mode activated
#define TRACK_GOODBYE      9   // "Goodbye"            — power off
#define TRACK_SPRAY_GEAR1 10   // long spray           — 1st→2nd gear change
#define TRACK_SPRAY_GEAR2 11   // faster spray         — 2nd→3rd gear change

// ── Turbo thresholds (compile-time defaults — overridden at runtime via cfg*) ──
#define TURBO_THROTTLE_HIGH  60.0f
#define TURBO_THROTTLE_LOW   10.0f
#define TURBO_RPM_MIN        3000.0f
#define TURBO_MIN_GEAR       1
#define TURBO_MAX_GEAR       2
#define TURBO_COOLDOWN_MS    2000
#define TURBO_VOLUME_GEAR1   30    // 100% — DFPlayer max is 30
#define TURBO_VOLUME_GEAR2   27    // 90%
#define TURBO_VOLUME_VOICE   13    // 50% — spoken announcements

// ── Engine state thresholds ───────────────────────────────────────────────
#define ENGINE_IDLE_RPM    200.0f   // below = engine off → parked screen, poll battery/coolant/RPM
#define ENGINE_DRIVING_RPM 1000.0f  // above = driving → poll TPS + speed + RPM

// ── Encoder sensitivity ───────────────────────────────────────────────────
#define STEPS_PER_ZONE  1   // 1 detent = 1 view change
#define NUM_VIEWS       2   // total display screens

// ── Objects ───────────────────────────────────────────────────────────────
#ifdef SIMULATION
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
#else
  U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display(U8G2_R0, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RES);
#endif
Bounce encBtn;

#ifndef SIMULATION
  #ifndef DEMO
    BluetoothSerial     BT;
  #endif
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
int      currentView  = 0;     // 0=throttle+bars, 1=text only
int      encoderPos   = 0;     // 0..(NUM_VIEWS*STEPS_PER_ZONE-1)
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

// ── System power + demo ───────────────────────────────────────────────────
bool systemOn = true;   // false = screen blank, polling stopped
#ifdef SIMULATION
  bool demoMode = true;   // ON by default in Wokwi (no real OBD2 available)
#else
  bool demoMode = false;  // OFF on real hardware (DEMO and production builds)
#endif

// ── Runtime-adjustable trigger parameters ─────────────────────────────────
// Initialized from compile-time defaults; changed via the Settings menu.
float cfgThrottleHigh = TURBO_THROTTLE_HIGH;
float cfgThrottleLow  = TURBO_THROTTLE_LOW;
float cfgRpmMin       = TURBO_RPM_MIN;
float cfgMinGear      = (float)TURBO_MIN_GEAR;
float cfgMaxGear      = (float)TURBO_MAX_GEAR;
float cfgCooldownMs   = (float)TURBO_COOLDOWN_MS;
float cfgVolGear1     = (float)TURBO_VOLUME_GEAR1;
float cfgVolGear2     = (float)TURBO_VOLUME_GEAR2;

struct SettingDef {
  const char* label;
  float*      val;
  float       step;
  float       vmin;
  float       vmax;
  bool        isInt;
};
static SettingDef CFG_DEFS[] = {
  {"TPS High %",  &cfgThrottleHigh,   5.0f,  10.0f, 100.0f, false},
  {"TPS Low  %",  &cfgThrottleLow,    1.0f,   0.0f,  50.0f, false},
  {"RPM Min",     &cfgRpmMin,       100.0f, 500.0f, 6000.0f, true },
  {"Min Gear",    &cfgMinGear,        1.0f,   1.0f,    6.0f, true },
  {"Max Gear",    &cfgMaxGear,        1.0f,   1.0f,    6.0f, true },
  {"Cooldown ms", &cfgCooldownMs,   100.0f, 500.0f,10000.0f, true },
  {"Vol Gear 1",  &cfgVolGear1,       1.0f,   0.0f,   30.0f, true },
  {"Vol Gear 2",  &cfgVolGear2,       1.0f,   0.0f,   30.0f, true },
};
#define NUM_CFG_DEFS  (int)(sizeof(CFG_DEFS) / sizeof(CFG_DEFS[0]))

// ── Menu state machine ─────────────────────────────────────────────────────
// CLOSED   : normal operation, encoder rotates views
// MAIN     : top-level menu (Power / Demo / Settings / Exit)
// SETTINGS : settings list (scroll through CFG_DEFS + Back)
// EDIT     : editing one setting value with the encoder
enum MenuState { MENU_CLOSED, MENU_MAIN, MENU_SETTINGS, MENU_EDIT };
MenuState menuState  = MENU_CLOSED;
int       mainSel    = 0;   // selected item in main menu (0-3)
int       settSel    = 0;   // selected item in settings list (0..NUM_CFG_DEFS)
#define   NUM_MAIN_ITEMS  4  // Power, Demo mode, Settings, Exit

// ── Scenario playback state (all builds — needed for runtime demo mode) ──
int      scenIdx   = 0;
uint32_t scenStart = 0;

#if defined(SIMULATION) || defined(DEMO)
  #ifdef SIMULATION
    uint32_t turboSoundUntilMs = 0;
  #endif
  enum SimPhase { SIM_SCANNING, SIM_CONNECTING, SIM_INIT, SIM_RUNNING };
  SimPhase simPhase      = SIM_SCANNING;
  uint32_t simPhaseStart = 0;
#else
  enum AppState { SCANNING, CONNECTING, INIT_ELM, RUNNING, NO_OBD };
  AppState appState        = SCANNING;
  bool     connectFailed   = false;  // true after a failed connect attempt — LED stays solid
  String   targetName      = "";
  uint32_t lastPollMs      = 0;
  uint32_t lastIdlePollMs  = 0;
  uint32_t lastEncActiveMs = 0;   // millis() of last encoder turn
  uint32_t scanStartMs     = 0;   // when SCANNING state was entered (for 30s bypass)
  int      scanFrame       = 0;
  #define  ENCODER_PRIORITY_MS  500   // pause OBD2 for 500ms after encoder activity
  #define  SCAN_TIMEOUT_MS      60000 // give up scanning after 60s if no ELM327 found
#endif

// ── Gear estimation ───────────────────────────────────────────────────────
// RPM/speed ratio thresholds. These are rough defaults; calibrate for your
// specific car by driving steadily in each gear and logging RPM/speed.
// Target speeds: gear 1 ≈ 0-40 mph, gear 2 ≈ 40-60 mph.
int estimateGear(float rpm, float speed) {
  if (speed < 2.0f || rpm < 100.0f) return 0;
  float ratio = rpm / speed;
  if      (ratio > 50.0f) return 1;
  else if (ratio > 33.0f) return 2;
  else if (ratio > 19.0f) return 3;
  else if (ratio > 12.0f) return 4;
  else if (ratio >  8.0f) return 5;
  else                    return 6;
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
  if (prevTPS       > cfgThrottleHigh &&
      metricTPS     < cfgThrottleLow  &&
      metricRPM     > cfgRpmMin       &&
      gear         >= (int)cfgMinGear &&
      gear         <= (int)cfgMaxGear &&
      now - lastTurboMs > (uint32_t)cfgCooldownMs) {
    turboCount++;
    lastTurboMs  = now;
    turboUntilMs = now + 2000;
#ifdef SIMULATION
    turboSoundUntilMs = now + 1000;
    tone(PIN_BUZZER, 900, 350);
#else
    dfplayer.volume(gear == 1 ? (int)cfgVolGear1 : (int)cfgVolGear2);
    dfplayer.playMp3Folder(gear == 1 ? TRACK_SPRAY_GEAR1 : TRACK_SPRAY_GEAR2);
#endif
  }
  prevTPS = metricTPS;
}

// ── Menu execution helpers ─────────────────────────────────────────────────
void execMainMenu() {
  switch (mainSel) {
    case 0:  // Power toggle
      systemOn = !systemOn;
      if (!systemOn) {
        menuState = MENU_CLOSED;
#ifndef SIMULATION
        dfplayer.volume(TURBO_VOLUME_VOICE);
        dfplayer.playMp3Folder(TRACK_GOODBYE);
#endif
        display.clearBuffer();
        display.sendBuffer();
      }
      break;
    case 1:  // Demo mode toggle
      demoMode = !demoMode;
      menuState = MENU_CLOSED;
      if (demoMode) {
        scenIdx   = 0;
        scenStart = millis();
        prevTPS   = 0;
#if defined(SIMULATION) || defined(DEMO)
        simPhase      = SIM_SCANNING;
        simPhaseStart = millis();
#endif
#ifndef SIMULATION
        dfplayer.volume(TURBO_VOLUME_VOICE);
        dfplayer.playMp3Folder(TRACK_DEMO_MODE);
#endif
      }
      break;
    case 2:  // Settings submenu
      menuState = MENU_SETTINGS;
      settSel   = 0;
      break;
    case 3:  // Exit
      menuState = MENU_CLOSED;
      break;
  }
}

void execSettingsMenu() {
  if (settSel == NUM_CFG_DEFS) {
    menuState = MENU_MAIN;  // Back
  } else {
    menuState = MENU_EDIT;
  }
}

// ── Encoder ───────────────────────────────────────────────────────────────
void readEncoder() {
  encBtn.update();
  if (encBtn.fell()) {
    if (!systemOn) {
      systemOn  = true;
      menuState = MENU_MAIN;
      mainSel   = 0;
    } else if (menuState == MENU_CLOSED) {
      menuState = MENU_MAIN;
      mainSel   = 0;
    } else if (menuState == MENU_MAIN) {
      execMainMenu();
    } else if (menuState == MENU_SETTINGS) {
      execSettingsMenu();
    } else if (menuState == MENU_EDIT) {
      menuState = MENU_SETTINGS;
    }
  }

#ifdef SIMULATION
  // Simulation: poll CLK directly (no blocking calls to miss pulses)
  int clk = digitalRead(PIN_ENC_CLK);
  if (clk != lastClk && clk == LOW) {
    int delta = (digitalRead(PIN_ENC_DT) != clk) ? -1 : 1;
    applyDelta(delta);
  }
  lastClk = clk;
#else
  // Real device: consume delta accumulated by the ISR
  if (encDelta != 0) {
    noInterrupts();
    int delta = encDelta;
    encDelta  = 0;
    interrupts();
    applyDelta(delta);
#if !defined(SIMULATION) && !defined(DEMO)
    if (menuState == MENU_CLOSED) lastEncActiveMs = millis();
#endif
  }
#endif
}

void applyDelta(int delta) {
  // Rate-limit menu navigation: one step per 150 ms prevents encoder bounce
  // from skipping multiple items per physical detent.
  if (menuState != MENU_CLOSED) {
    static uint32_t lastMenuStepMs = 0;
    uint32_t now = millis();
    if (now - lastMenuStepMs < 150) return;
    lastMenuStepMs = now;
    delta = (delta > 0) ? 1 : -1;
  }
  if (menuState == MENU_MAIN) {
    mainSel = (mainSel + delta % NUM_MAIN_ITEMS + NUM_MAIN_ITEMS) % NUM_MAIN_ITEMS;
  } else if (menuState == MENU_SETTINGS) {
    int total = NUM_CFG_DEFS + 1;  // +1 for Back
    settSel = (settSel + delta % total + total) % total;
  } else if (menuState == MENU_EDIT) {
    float* v = CFG_DEFS[settSel].val;
    *v = constrain(*v + delta * CFG_DEFS[settSel].step,
                   CFG_DEFS[settSel].vmin, CFG_DEFS[settSel].vmax);
  } else {
    int total = NUM_VIEWS * STEPS_PER_ZONE;
    encoderPos  = ((encoderPos + delta) % total + total) % total;
    currentView = encoderPos / STEPS_PER_ZONE;
  }
}

// ── Menu display functions ────────────────────────────────────────────────
void drawMainMenu() {
  char items[NUM_MAIN_ITEMS][24];
  snprintf(items[0], sizeof(items[0]), "Power %s",    systemOn ? "OFF" : "ON");
  snprintf(items[1], sizeof(items[1]), "Demo  %s",    demoMode ? "OFF" : "ON");
  strncpy (items[2], "Settings >",  sizeof(items[2]));
  strncpy (items[3], "Exit",        sizeof(items[3]));

  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(34, 11, "SETTINGS");
  display.drawHLine(0, 13, 128);

  for (int i = 0; i < NUM_MAIN_ITEMS; i++) {
    int y = 26 + i * 12;
    if (i == mainSel) {
      display.drawBox(0, y - 9, 128, 11);
      display.setDrawColor(0);
    }
    display.drawStr(4, y, items[i]);
    if (i == mainSel) display.setDrawColor(1);
  }
  display.sendBuffer();
}

void drawSettingsMenu() {
  int total  = NUM_CFG_DEFS + 1;  // +1 for Back
  int start  = constrain(settSel - 1, 0, total - 3);

  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(20, 11, "SETTINGS");
  display.drawHLine(0, 13, 128);

  for (int i = 0; i < 3 && (start + i) < total; i++) {
    int  idx = start + i;
    int  y   = 27 + i * 16;
    char buf[24];
    if (idx == NUM_CFG_DEFS) {
      strncpy(buf, "< Back", sizeof(buf));
    } else {
      if (CFG_DEFS[idx].isInt)
        snprintf(buf, sizeof(buf), "%-11s %4d", CFG_DEFS[idx].label, (int)*CFG_DEFS[idx].val);
      else
        snprintf(buf, sizeof(buf), "%-11s %4.0f", CFG_DEFS[idx].label, *CFG_DEFS[idx].val);
    }
    if (idx == settSel) {
      display.drawBox(0, y - 10, 128, 12);
      display.setDrawColor(0);
    }
    display.drawStr(4, y, buf);
    if (idx == settSel) display.setDrawColor(1);
  }
  display.sendBuffer();
}

void drawSettingsEdit() {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(4, 11, CFG_DEFS[settSel].label);
  display.drawHLine(0, 13, 128);

  char valBuf[16];
  if (CFG_DEFS[settSel].isInt)
    snprintf(valBuf, sizeof(valBuf), "%d", (int)*CFG_DEFS[settSel].val);
  else
    snprintf(valBuf, sizeof(valBuf), "%.0f", *CFG_DEFS[settSel].val);

  display.setFont(u8g2_font_ncenB10_tr);
  int w = display.getStrWidth(valBuf);
  display.drawStr((128 - w) / 2, 42, valBuf);

  display.setFont(u8g2_font_5x7_tr);
  display.drawStr(8, 58, "< rotate >  click OK");
  display.sendBuffer();
}

// ── Shared display helpers ────────────────────────────────────────────────
void showMessage(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(0, 28, line1);
  display.setFont(u8g2_font_ncenB08_tr);
  if (line2) display.drawStr(0, 44, line2);
  if (line3) display.drawStr(0, 59, line3);
  display.sendBuffer();
}

void drawParked(const char* deviceName) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);

  char nameBuf[18];
  strncpy(nameBuf, deviceName, 17); nameBuf[17] = '\0';
  display.drawStr(0, 25, nameBuf);

  char buf[24];
  if (metricVoltage > 0) {
    snprintf(buf, sizeof(buf), "Battery:  %.1f V", metricVoltage);
    display.drawStr(0, 37, buf);
  }
  if (metricCoolant > -999) {
    snprintf(buf, sizeof(buf), "Coolant:  %.0f C", metricCoolant);
    display.drawStr(0, 49, buf);
  }

  if ((millis() / 600) % 2 == 0)
    display.drawStr(0, 61, "Start engine...");

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
  if (turboRecent) {
    char buf[20];
    snprintf(buf, sizeof(buf), "PSSSSH! #%lu", turboCount);
    display.drawStr(0, 11, buf);
  } else {
    char tStr[16], gStr[12];
    snprintf(tStr, sizeof(tStr), "Turbo: %lu", turboCount);
    snprintf(gStr, sizeof(gStr), "Gear: %d", gear);
    display.drawStr(0, 11, tStr);
    display.drawStr(128 - display.getStrWidth(gStr), 11, gStr);
  }

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
// Scenario data — all builds (runtime demo mode needs it in production too)
// ═══════════════════════════════════════════════════════════════════════════

// {time_ms, tps%, rpm, speed_kmh}
// Full cycle (~24s):
//   0–3s    ignition on, engine off   → parked screen
//   3–8s    engine idle, not moving   → idle screen
//   8–19s   driving: 1st→2nd→3rd→4th → two Turbo triggers
//   19–24s  stopped, engine idle      → idle screen → loop
//
// Turbo #1 at ~9.5s (1st→2nd), Turbo #2 at ~12.5s (2nd→3rd)
// TPS hold for 200ms after each lift-off guarantees a 100ms checkpoint lands
// while prevTPS is high and currentTPS is low.
//
// Gear thresholds (ratio = RPM / km/h):  >50=1st  >33=2nd  >19=3rd  >12=4th
struct DataPoint { uint32_t t; float tps; float rpm; float speed; };
static const DataPoint SCENARIO[] = {
  // ── Parked ────────────────────────────────────────────────────────────────
  {     0,  0,    0,    0 },  // ignition on, engine off → parked screen
  {  3000,  0,    0,    0 },  // end of ignition-on window
  {  3100,  0,  750,    0 },  // engine cranks → idle screen (RPM 200–999)
  {  8000,  0,  850,    0 },  // 5s idle — rotary fully responsive

  // ── 1st gear: quick throttle, 0→64 km/h (40 mph) in ~1.5s ───────────────
  {  8200, 85, 1200,    2 },  // snap to throttle
  {  9500, 90, 3400,   60 },  // near redline  (ratio=57 → gear 1)
  {  9501,  4, 3300,   60 },  // *** Turbo #1 *** instant lift-off
  {  9700,  4, 3200,   62 },  // hold TPS low 200ms (ensures trigger fires)

  // ── 2nd gear: quick throttle, 64→97 km/h (40→60 mph) in ~2.8s ───────────
  {  9900, 85, 2600,   65 },  // back on throttle  (ratio=40 → gear 2)
  { 12500, 88, 3300,   90 },  // near redline  (ratio=37 → gear 2)
  { 12501,  4, 3200,   90 },  // *** Turbo #2 *** instant lift-off
  { 12700,  4, 3100,   92 },  // hold TPS low 200ms

  // ── 3rd gear: moderate throttle, 97→145 km/h (60→90 mph) in 3s ──────────
  { 12900, 50, 2200,   96 },  // back on throttle  (ratio=23 → gear 3)
  { 15900, 45, 2800,  140 },  // cruising  (ratio=20 → gear 3)

  // ── 4th gear: light throttle, 145→161 km/h (90→100 mph) in ~2s ──────────
  { 16000, 28, 2200,  145 },  // shift to 4th  (ratio=15 → gear 4)
  { 17900, 25, 2400,  161 },  // 100 mph  (ratio=15 → gear 4)

  // ── Stop ──────────────────────────────────────────────────────────────────
  { 18500,  0, 1500,  100 },  // lift off and brake
  { 19500,  0,  850,    0 },  // stopped → idle screen
  { 24500,  0,  800,    0 },  // 5s idle then loop
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

// ═══════════════════════════════════════════════════════════════════════════
// REAL DEVICE build: Bluetooth OBD2, DFPlayer, state machine
// ═══════════════════════════════════════════════════════════════════════════
#if !defined(SIMULATION) && !defined(DEMO)

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
      if (c == '>') {
        return resp;
      }
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

// Non-blocking delay that keeps polling the encoder so the menu stays
// accessible even during the long blocking BT.discover call.
static void scanDelay(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    readEncoder();
    if (menuState != MENU_CLOSED) return;
    delay(30);
  }
}

void doScanning() {
  // Check encoder at entry so a button press during the previous BT.discover
  // is acted on before we block again.
  readEncoder();
  if (menuState != MENU_CLOSED) return;

  if (scanStartMs == 0) {
    scanStartMs = millis();
    dfplayer.volume(TURBO_VOLUME_VOICE);
    dfplayer.playMp3Folder(TRACK_PAIRING);  // "Pairing"
  }

  if (millis() - scanStartMs >= SCAN_TIMEOUT_MS) {
    dfplayer.volume(TURBO_VOLUME_VOICE);
    dfplayer.playMp3Folder(TRACK_NO_OBD2);  // "OBD2 not connected"
    showMessage("No OBD2 found", "Display only", "Click for menu");
    scanDelay(1500);
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
  display.drawStr(0, 44, "Plug ELM327 into");
  display.drawStr(0, 59, "OBD2 port & wait");
  display.sendBuffer();

  // BT.discover blocks for the full 8 s — poll encoder immediately after
  BTScanResults* results = BT.discover(8000);
  readEncoder();
  if (menuState != MENU_CLOSED) return;

  if (!results || results->getCount() == 0) {
    showMessage("No device found", "Retrying...");
    scanDelay(2000);
    return;
  }
  for (int i = 0; i < results->getCount(); i++) {
    BTAdvertisedDevice* dev = results->getDevice(i);
    String name  = dev->getName().c_str();
    String upper = name; upper.toUpperCase();
    if (upper.indexOf("ELM") >= 0 || upper.indexOf("OBD") >= 0 || upper.indexOf("LINK") >= 0) {
      targetName    = name;
      connectFailed = false;
      appState      = CONNECTING;
      return;
    }
  }
  showMessage("ELM327 not found", "Retrying...");
  scanDelay(2000);
}

void doNoObd() {
  uint32_t now = millis();
  if (now - lastDrawMs >= 200) {
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
  showMessage("Found:", nameBuf, "Connecting...");

  const char* pins[] = {"1234", "0000"};
  for (const char* pin : pins) {
    BT.setPin(pin, strlen(pin));
    if (BT.connect(targetName)) {
      connectFailed = false;
      appState = INIT_ELM;
      return;
    }
    delay(500);
  }
  connectFailed = true;
  dfplayer.volume(TURBO_VOLUME_VOICE);
  dfplayer.playMp3Folder(TRACK_NO_OBD2);  // "OBD2 not connected"
  showMessage("Connect failed", "Scanning again...");
  delay(2000);
  appState    = SCANNING;
  scanStartMs = 0;  // reset so "Pairing" plays again on re-entry
}

void doInitElm() {
  char nameBuf[18];
  strncpy(nameBuf, targetName.c_str(), 17); nameBuf[17] = '\0';
  showMessage("Initialising...", nameBuf);

  obdSend("ATZ", 3000); delay(500);
  const char* cmds[] = {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"};
  for (const char* cmd : cmds) {
    obdSend(cmd, 1500); delay(100);
  }

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
  delay(1500);
  appState = RUNNING;
}

void doRunning() {
  uint32_t now = millis();
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

#endif // !SIMULATION && !DEMO

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  // ── 1. Screen first — visible immediately, before anything else ───────────
#ifdef SIMULATION
  Wire.begin();
#endif
  display.begin();
  display.setFont(u8g2_font_ncenB10_tr);
  display.clearBuffer();
  display.drawStr(0, 28, "Turbo Emulator");
  display.setFont(u8g2_font_ncenB08_tr);
#ifdef SIMULATION
  display.drawStr(0, 44, "Wokwi simulation");
#else
  display.drawStr(0, 44, "OBD2 v1.0");
#endif
  display.drawStr(0, 59, "Starting...");
  display.sendBuffer();

  // ── 2. Encoder ───────────────────────────────────────────────────────────
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
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  delay(800);
  simPhaseStart = millis();
#elif defined(DEMO)
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
  delay(500);
  if (dfplayer.begin(Serial2)) dfplayer.volume(TURBO_VOLUME_VOICE);
  delay(800);
  simPhaseStart = millis();
#else
  // ── 3. LED ────────────────────────────────────────────────────────────────
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // ── 4. DFPlayer — play startup sound so we know it works ─────────────────
  Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
  delay(500);  // DFPlayer needs ~500ms after power-on before it responds
  if (dfplayer.begin(Serial2)) dfplayer.volume(TURBO_VOLUME_VOICE);

  // ── 5. Bluetooth — last, after display and audio are confirmed working ────
  BT.begin("ESP32-OBD", true);

#endif
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
#if defined(SIMULATION) || defined(DEMO)
  uint32_t now = millis();
  readEncoder();

  // ── Menu overlay ──────────────────────────────────────────────────────────
  if (menuState == MENU_MAIN)     { drawMainMenu();     return; }
  if (menuState == MENU_SETTINGS) { drawSettingsMenu(); return; }
  if (menuState == MENU_EDIT)     { drawSettingsEdit(); return; }

  if (!systemOn) {
    display.clearBuffer();
    display.sendBuffer();
    return;
  }

  switch (simPhase) {

    case SIM_SCANNING: {
      // Spinner animates in real time using millis()
      if (now - lastDrawMs >= 150) {
        static const char SPIN[] = {'-', '/', '|', '\\'};
        char line1[22];
        snprintf(line1, sizeof(line1), "Scanning OBD2... %c", SPIN[(now / 150) % 4]);
        display.clearBuffer();
        display.setFont(u8g2_font_ncenB10_tr);
        display.drawStr(0, 28, line1);
        display.setFont(u8g2_font_ncenB08_tr);
        display.drawStr(0, 44, "Looking for ELM327");
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
          display.drawStr(0, 28, "OBD2 connected!");
          display.setFont(u8g2_font_ncenB08_tr);
          display.drawStr(0, 44, "ELM327-SIM");
          char vbuf[20];
          snprintf(vbuf, sizeof(vbuf), "Battery: %.1f V", metricVoltage);
          display.drawStr(0, 59, vbuf);
          display.sendBuffer();
        }
        lastDrawMs = now;
      }
      if (confirmed && now - simPhaseStart >= 3000) {
        simPhase  = SIM_RUNNING;
        scenStart = millis();
        scenIdx   = 0;
      }
      break;
    }

    case SIM_RUNNING: {
      if (demoMode) {
        advanceScenario();
        checkTurbo(now);
      }
      if (now - lastDrawMs >= 50) {
        drawDisplay();
        lastDrawMs = now;
      }
#ifdef SIMULATION
      // LED behaviour: blink fast while turbo sound plays, off otherwise
      if (now < turboSoundUntilMs) {
        digitalWrite(PIN_LED, (now / 100) % 2 == 0 ? HIGH : LOW);
      } else {
        digitalWrite(PIN_LED, LOW);
      }
#endif
      break;
    }
  }

#else
  // ── Production build ──────────────────────────────────────────────────────
  uint32_t now = millis();
  readEncoder();

  if (menuState == MENU_MAIN)     { drawMainMenu();     return; }
  if (menuState == MENU_SETTINGS) { drawSettingsMenu(); return; }
  if (menuState == MENU_EDIT)     { drawSettingsEdit(); return; }

  if (!systemOn) {
    display.clearBuffer();
    display.sendBuffer();
    return;
  }

  if (demoMode) {
    advanceScenario();
    checkTurbo(now);
    if (now - lastDrawMs >= 50) {
      drawDisplay();
      lastDrawMs = now;
    }
    return;
  }

  if (now < turboUntilMs) {
    digitalWrite(PIN_LED, (now / 100) % 2 == 0 ? HIGH : LOW);
  } else if (appState == RUNNING) {
    digitalWrite(PIN_LED, LOW);
  } else if (connectFailed) {
    digitalWrite(PIN_LED, HIGH);
  } else {
    digitalWrite(PIN_LED, (now / 500) % 2 == 0 ? HIGH : LOW);
  }
  switch (appState) {
    case SCANNING:   doScanning();   break;
    case CONNECTING: doConnecting(); break;
    case INIT_ELM:   doInitElm();    break;
    case RUNNING:    doRunning();    break;
    case NO_OBD:     doNoObd();      break;
  }
#endif
}

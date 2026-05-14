// turbo.ino — OBD2 Turbo Sound Emulator  (entry point)
//
// All logic lives in dedicated .h modules included below in dependency order.
// This file owns only the shared mutable state and the Arduino entry points.
//
// Build targets:
//   Wokwi sim   : make wokwi-build  (-DSIMULATION)
//   Demo/dev    : make demo-upload  (-DDEMO)
//   Production  : make deploy
//
// Hardware (real device):
//   SSD1306 OLED   — SPI: SCK=GPIO18, MOSI=GPIO23, RES=GPIO15, DC=GPIO32, CS=GPIO5
//                    Breadboard: OLED pins plug into col J rows 3-9 alongside ESP32
//   KY-040 encoder — CLK=GPIO25, DT=GPIO26, SW=GPIO27
//                    Breadboard: col D rows 30-34
//   DFPlayer Mini  — Serial2 (RX2=GPIO16, TX2=GPIO17), VCC=3.3V, 1kΩ on RX line
//                    Breadboard: cols D-G rows 20-27
//   ELM327 dongle  — BLE OBD2 port
//
// Hardware (Wokwi simulation):
//   SSD1306 I2C (GPIO21/22), KY-040 encoder, passive buzzer GPIO17, LED GPIO4

// ── Library includes ──────────────────────────────────────────────────────
#include <U8g2lib.h>
#include <Bounce2.h>

#ifdef SIMULATION
  #include <Wire.h>
#else
  #include <SPI.h>
  #include <Preferences.h>
  #ifndef DEMO
    #include <BLEDevice.h>
    #include <BLEClient.h>
    #include <BLEScan.h>
    #include <BLEAdvertisedDevice.h>
  #endif
  #include <DFRobotDFPlayerMini.h>
#endif

// ── Configuration constants ───────────────────────────────────────────────
#include "Config.h"

// ── Shared mutable state ──────────────────────────────────────────────────
// All included modules share one compilation unit and access these directly.

// OBD2 metrics — written by Scenario (demo) or OBD2 (real); read by Display + TurboTrigger
float    metricTPS     = 0;
float    metricRPM     = 0;
float    metricSpeed   = 0;
float    metricVoltage = 0;
float    metricCoolant = -999;  // °C, -999 = not yet read

// Turbo trigger state — written by TurboTrigger; read by Display
float    prevTPS      = 0;
uint32_t lastTurboMs  = 0;
uint32_t turboCount   = 0;
uint32_t turboUntilMs = 0;

// View state — written by Encoder; read by Display
int currentView = 0;
int encoderPos  = 0;

// System
bool systemOn = true;
#ifdef SIMULATION
  bool demoMode = true;   // demo always on in Wokwi — no real OBD2 available
#else
  bool demoMode = false;
#endif
uint32_t lastDrawMs = 0;

// Scenario playback — reset by Menu on demo toggle; advanced by Scenario
int      scenIdx   = 0;
uint32_t scenStart = 0;

// Simulation/Demo phase state — defined here so Menu.h can reference SimPhase
#if defined(SIMULATION) || defined(DEMO)
  enum SimPhase { SIM_SCANNING, SIM_CONNECTING, SIM_INIT, SIM_RUNNING };
  SimPhase simPhase      = SIM_SCANNING;
  uint32_t simPhaseStart = 0;
  #ifdef SIMULATION
    uint32_t turboSoundUntilMs = 0;
  #endif
#endif

// Encoder activity timestamp — written by Encoder, read by OBD2 to pause polling
#if !defined(SIMULATION) && !defined(DEMO)
  uint32_t lastEncActiveMs = 0;
#endif

// ── Module includes (in dependency order) ─────────────────────────────────
#include "Settings.h"       // cfg* vars, SettingDef, CFG_DEFS[], NVS functions
#include "GearEstimator.h"  // estimateGear()
#include "Audio.h"          // dfplayer object, dfplayerVoice() — not SIMULATION
#include "Display.h"        // display object, showMessage, drawBar, drawParked, drawDisplay
#include "Menu.h"           // MenuState enum, menu state, drawMenu*, execMenu*
#include "Encoder.h"        // ISR, encBtn, readEncoder, applyDelta
#include "TurboTrigger.h"   // checkTurbo
#include "Scenario.h"       // SCENARIO[], advanceScenario
#include "SimLoop.h"        // doSimLoop — SIMULATION and DEMO builds only
#include "OBD2.h"           // BLE OBD2, AppState, doScanning/doRunning — production only

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  // 1. Screen — splash visible immediately before any other init
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

  // 2. Encoder — ISR registered for all builds including Wokwi
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  encBtn.attach(PIN_ENC_SW, INPUT_PULLUP);
  encBtn.interval(10);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encISR_CLK, FALLING);
  // PIN_ENC_DT needs no interrupt — read directly inside encISR_CLK

#ifdef SIMULATION
  // 3-SIM. Buzzer + LED, brief delay, start simulated scan sequence
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  delay(800);
  simPhaseStart = millis();

#elif defined(DEMO)
  // 3-DEMO. Load NVS settings, init DFPlayer, start simulated scan sequence
  loadSettings();
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
  delay(500);
  if (dfplayer.begin(Serial2)) dfplayerVoice(TRACK_PAIRING);
  simPhaseStart = millis();

#else
  // 3-PROD. Load NVS settings, LED, DFPlayer startup tone, BLE scan
  loadSettings();
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
  delay(500);  // DFPlayer needs ~500 ms after power-on before it responds
  if (dfplayer.begin(Serial2)) dfplayer.volume((int)cfgVolVoice);
  BLEDevice::init("");
  BLEScan* bleScan = BLEDevice::getScan();
  bleScan->setAdvertisedDeviceCallbacks(new OBDScanCallbacks(), false);
  bleScan->setActiveScan(true);
  bleScan->setInterval(100);
  bleScan->setWindow(99);
#endif
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();
  readEncoder();

  // Menu overlay — takes priority over everything else
  if (menuState == MENU_MAIN)     { drawMainMenu();     return; }
  if (menuState == MENU_SETTINGS) { drawSettingsMenu(); return; }
  if (menuState == MENU_EDIT)     { drawSettingsEdit(); return; }

  if (!systemOn) {
    display.clearBuffer();
    display.sendBuffer();
    return;
  }

#if defined(SIMULATION) || defined(DEMO)
  doSimLoop(now);

#else
  // Production: demo mode uses the built-in scenario without OBD2
  if (demoMode) {
    advanceScenario();
    checkTurbo(now);
    if (now - lastDrawMs >= 50) { drawDisplay(); lastDrawMs = now; }
    return;
  }

  // LED: fast blink during Turbo, solid on connect fail, slow blink while scanning
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

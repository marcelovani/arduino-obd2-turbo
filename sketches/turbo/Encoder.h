// Encoder.h — KY-040 rotary encoder: ISR, button debounce, and input dispatch.
//
// Rotation — single ISR on CLK FALLING, DT read immediately:
//   CLK fell while DT LOW  → CCW → encDelta--
//   CLK fell while DT HIGH → CW  → encDelta++
// 3000 µs self-debounce on CLK prevents double-counting contact bounce.
// Wokwi supports attachInterrupt on ESP32 — every edge captured instantly,
// fixing the "must click 10 times" lag seen with the previous polling approach.
//
// Button — Bounce2 library (10 ms debounce).
// applyDelta() must be declared before readEncoder() because readEncoder calls it.

// Forward declarations for functions defined in later-included modules.
#if !defined(SIMULATION)
void stopRecording();
void stopWifiExport();
#endif

Bounce encBtn;

volatile int           encDelta = 0;
volatile unsigned long encClkUs = 0;

void IRAM_ATTR encISR_CLK() {
  unsigned long now = micros();
  if (now - encClkUs < 3000) return;   // self-debounce
  encClkUs = now;
  if (digitalRead(PIN_ENC_DT) == LOW)
    encDelta--;   // CLK fell, DT already LOW → CCW
  else
    encDelta++;   // CLK fell, DT still HIGH → CW
}

void applyDelta(int delta) {
  // Rate-limit menu navigation on real hardware — the ISR can accumulate a
  // large delta before applyDelta() runs; cap at one step per 50 ms.
  // Not applied in simulation: millis() runs at simulation speed.
#ifndef SIMULATION
  if (menuState != MENU_CLOSED) {
    static uint32_t lastMenuStepMs = 0;
    uint32_t now = millis();
    if (now - lastMenuStepMs < 50) return;
    lastMenuStepMs = now;
    delta = (delta > 0) ? 1 : -1;
  }
#endif
  if (menuState == MENU_MAIN) {
    mainSel = (mainSel + delta % NUM_MAIN_ITEMS + NUM_MAIN_ITEMS) % NUM_MAIN_ITEMS;
  } else if (menuState == MENU_SETTINGS) {
    int total = NUM_CFG_DEFS + 2;
    settSel = (settSel + delta % total + total) % total;
  } else if (menuState == MENU_EDIT) {
    float* v = CFG_DEFS[settSel].val;
    *v = constrain(*v + delta * CFG_DEFS[settSel].step,
                   CFG_DEFS[settSel].vmin, CFG_DEFS[settSel].vmax);
  } else if (menuState == MENU_RECORDING || menuState == MENU_EXPORT) {
    // no rotation in recording or export modes
  } else {
    int total = NUM_VIEWS * STEPS_PER_ZONE;
    encoderPos  = ((encoderPos + delta) % total + total) % total;
    currentView = encoderPos / STEPS_PER_ZONE;
  }
}

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
    } else if (menuState == MENU_RECORDING) {
#if !defined(SIMULATION)
      stopRecording();
#endif
      menuState = MENU_MAIN;
    } else if (menuState == MENU_EXPORT) {
#if !defined(SIMULATION)
      stopWifiExport();
#endif
      menuState = MENU_MAIN;
    }
  }

  // Consume delta accumulated by the ISR (all builds, including Wokwi).
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
}

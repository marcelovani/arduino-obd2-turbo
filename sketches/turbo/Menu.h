// Menu.h — Menu state machine: state variables, rendering, and execution.
//
// States:
//   CLOSED    — normal operation, encoder rotates display views
//   MAIN      — top-level: Power, Demo, Record*, Export*, Settings, Exit
//   SETTINGS  — settings list: CFG_DEFS entries + Back + Factory Reset
//   EDIT      — adjusting one value with the encoder (confirm with click)
//   RECORDING — full-screen recording view; button stops and saves
//   EXPORT    — full-screen WiFi export; button stops server
//
// * Record and Export are hidden in SIMULATION builds (no LittleFS / WiFi).

enum MenuState { MENU_CLOSED, MENU_MAIN, MENU_SETTINGS, MENU_EDIT,
                 MENU_RECORDING, MENU_EXPORT };
MenuState menuState = MENU_CLOSED;
int       mainSel   = 0;
int       settSel   = 0;

#ifdef SIMULATION
  #define NUM_MAIN_ITEMS 4   // Power, Demo, Settings, Exit
#else
  #define NUM_MAIN_ITEMS 6   // + Record, Export
#endif

// Forward declarations for functions defined in later-included modules.
#if !defined(SIMULATION)
void startRecording();
void startWifiExport();
void wipeAllLogs();
#endif

// ── Rendering ─────────────────────────────────────────────────────────────

void drawMainMenu() {
  char items[NUM_MAIN_ITEMS][24];
  snprintf(items[0], sizeof(items[0]), "Power %s", systemOn ? "OFF" : "ON");
  snprintf(items[1], sizeof(items[1]), "Demo  %s", demoMode ? "OFF" : "ON");
#ifdef SIMULATION
  strncpy(items[2], "Settings >", sizeof(items[2]));
  strncpy(items[3], "Exit",       sizeof(items[3]));
#else
  strncpy(items[2], "Record",     sizeof(items[2]));
  strncpy(items[3], "Export",     sizeof(items[3]));
  strncpy(items[4], "Settings >", sizeof(items[4]));
  strncpy(items[5], "Exit",       sizeof(items[5]));
#endif

  // Scrolling window: show 4 items at a time, track selected item.
  int start = constrain(mainSel - 1, 0, NUM_MAIN_ITEMS - 4);

  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(40, 11, "MENU");
  display.drawHLine(0, 13, 128);

  for (int i = 0; i < 4 && (start + i) < NUM_MAIN_ITEMS; i++) {
    int idx = start + i;
    int y   = 26 + i * 12;
    if (idx == mainSel) { display.drawBox(0, y - 9, 128, 11); display.setDrawColor(0); }
    display.drawStr(4, y, items[idx]);
    if (idx == mainSel) display.setDrawColor(1);
  }
  display.sendBuffer();
}

void drawSettingsMenu() {
  int total = NUM_CFG_DEFS + 2;  // +1 Back, +1 Factory Reset
  int start = constrain(settSel - 1, 0, total - 3);

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
    } else if (idx == NUM_CFG_DEFS + 1) {
      strncpy(buf, "Factory Reset", sizeof(buf));
    } else {
      switch (CFG_DEFS[idx].type) {
        case SETTING_BOOL:
          snprintf(buf, sizeof(buf), "%-11s  %s", CFG_DEFS[idx].label, *CFG_DEFS[idx].val > 0 ? "On" : "Off");
          break;
        case SETTING_INT:
          snprintf(buf, sizeof(buf), "%-11s %4d", CFG_DEFS[idx].label, (int)*CFG_DEFS[idx].val);
          break;
        default:
          snprintf(buf, sizeof(buf), "%-11s %4.0f", CFG_DEFS[idx].label, *CFG_DEFS[idx].val);
      }
    }
    if (idx == settSel) { display.drawBox(0, y - 10, 128, 12); display.setDrawColor(0); }
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
  switch (CFG_DEFS[settSel].type) {
    case SETTING_BOOL:
      strncpy(valBuf, *CFG_DEFS[settSel].val > 0 ? "On" : "Off", sizeof(valBuf));
      break;
    case SETTING_INT:
      snprintf(valBuf, sizeof(valBuf), "%d", (int)*CFG_DEFS[settSel].val);
      break;
    default:
      snprintf(valBuf, sizeof(valBuf), "%.0f", *CFG_DEFS[settSel].val);
  }

  display.setFont(u8g2_font_ncenB10_tr);
  int w = display.getStrWidth(valBuf);
  display.drawStr((128 - w) / 2, 42, valBuf);
  display.setFont(u8g2_font_5x7_tr);
  display.drawStr(8, 58, "< rotate >  click OK");
  display.sendBuffer();
}

// ── Execution ─────────────────────────────────────────────────────────────

void execMainMenu() {
  switch (mainSel) {
    case 0:  // Power toggle
      systemOn = !systemOn;
      if (!systemOn) {
        menuState = MENU_CLOSED;
#ifndef SIMULATION
        dfplayerVoice(TRACK_GOODBYE);
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
        dfplayer.volume((int)cfgVolVoice);
        dfplayer.playMp3Folder(TRACK_DEMO_MODE);
#endif
      }
      break;
#ifdef SIMULATION
    case 2:  // Settings (SIMULATION — no Record/Export)
      menuState = MENU_SETTINGS;
      settSel   = 0;
      break;
    case 3:  // Exit
      menuState = MENU_CLOSED;
      break;
#else
    case 2:  // Record
      startRecording();
      menuState = MENU_RECORDING;
      break;
    case 3:  // Export
      startWifiExport();
      menuState = MENU_EXPORT;
      break;
    case 4:  // Settings
      menuState = MENU_SETTINGS;
      settSel   = 0;
      break;
    case 5:  // Exit
      menuState = MENU_CLOSED;
      break;
#endif
  }
}

void execSettingsMenu() {
  if (settSel == NUM_CFG_DEFS) {
#ifndef SIMULATION
    saveSettings();
#endif
    menuState = MENU_MAIN;
  } else if (settSel == NUM_CFG_DEFS + 1) {
#ifndef SIMULATION
    resetSettings();
    wipeAllLogs();
#endif
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB10_tr);
    display.drawStr(0, 28, "Factory reset");
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 44, "Settings + logs");
    display.drawStr(0, 57, "cleared.");
    display.sendBuffer();
    delay(1500);
    menuState = MENU_MAIN;
  } else {
    menuState = MENU_EDIT;
  }
}

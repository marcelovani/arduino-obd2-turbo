// Recorder.h — OBD2 data recorder to LittleFS.
//
// Writes TPS, RPM, speed, voltage, coolant to a CSV every 100 ms.
// File names are sequential (log_001.csv…) with a counter in NVS namespace "rec".
// Active in real and DEMO builds — not SIMULATION (no LittleFS in Wokwi).

#if !defined(SIMULATION)

#include <LittleFS.h>

static File     recFile;
static bool     recFsReady     = false;
static bool     recActive      = false;
static uint32_t recSampleCount = 0;
static uint32_t lastRecMs      = 0;

static bool mountFs() {
  if (recFsReady) return true;
  recFsReady = LittleFS.begin(true);  // format on first use
  return recFsReady;
}

static String nextLogFilename() {
  Preferences prefs;
  prefs.begin("rec", false);
  int n = prefs.getInt("count", 0) + 1;
  prefs.putInt("count", n);
  prefs.end();
  char buf[20];
  snprintf(buf, sizeof(buf), "/log_%03d.csv", n);
  return String(buf);
}

void wipeAllLogs() {
  if (!mountFs()) return;
  File root = LittleFS.open("/");
  File f;
  while ((f = root.openNextFile())) {
    if (!f.isDirectory()) {
      String path = f.name();
      if (!path.startsWith("/")) path = "/" + path;
      f.close();
      LittleFS.remove(path);
    }
  }
  // Reset the sequential filename counter
  Preferences prefs;
  prefs.begin("rec", false);
  prefs.putInt("count", 0);
  prefs.end();
}

void startRecording() {
  if (!mountFs()) return;
  String fname = nextLogFilename();
  recFile = LittleFS.open(fname, "w");
  if (!recFile) return;
  // Embed active runtime settings so the viewer can replay trigger detection accurately.
  recFile.printf("# throttle_high=%.1f,throttle_low=%.1f,rpm_min=%.1f,min_gear=%d,max_gear=%d,cooldown_ms=%d,speed12=%.1f,speed23=%.1f\n",
    cfgThrottleHigh, cfgThrottleLow, cfgRpmMin,
    (int)cfgMinGear, (int)cfgMaxGear, (int)cfgCooldownMs,
    cfgSpeed12, cfgSpeed23);
  recFile.println("ms,tps,rpm,speed");
  recActive      = true;
  recSampleCount = 0;
  lastRecMs      = millis();
}

void stopRecording() {
  if (recActive && recFile) { recFile.flush(); recFile.close(); }
  recActive = false;
}

void tickRecording(uint32_t now) {
  if (!recActive) return;
  if (now - lastRecMs < 100) return;
  lastRecMs = now;
  // Only write rows when in driving mode — TPS and speed are not polled below
  // ENGINE_DRIVING_RPM, so recording idle rows produces stale zeros.
  if (metricRPM < ENGINE_DRIVING_RPM) return;
  char buf[48];
  snprintf(buf, sizeof(buf), "%lu,%.1f,%.1f,%.1f",
    (unsigned long)now, metricTPS, metricRPM, metricSpeed);
  recFile.println(buf);
  recSampleCount++;
}

void drawRecordingScreen(uint32_t now) {
  static uint32_t lastRecDrawMs = 0;
  if (now - lastRecDrawMs < 200) return;
  lastRecDrawMs = now;

  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  if ((now / 500) % 2 == 0) display.drawStr(0, 16, "* REC");
  else                       display.drawStr(0, 16, "  REC");

  display.setFont(u8g2_font_ncenB08_tr);
  char buf[24];
  snprintf(buf, sizeof(buf), "%lu samples", (unsigned long)recSampleCount);
  display.drawStr(0, 33, buf);

  snprintf(buf, sizeof(buf), "RPM:%.0f  TPS:%.0f%%", metricRPM, metricTPS);
  display.drawStr(0, 48, buf);

  display.drawStr(0, 63, "[Click] Stop & Save");
  display.sendBuffer();
}

#endif // !SIMULATION

// Settings.h — Runtime-adjustable parameters and NVS persistence.
//
// cfg* variables are the live trigger/audio values used throughout the sketch.
// CFG_DEFS[] maps each variable to its menu label, edit step, and limits.
// Changes take effect immediately when edited in the menu.
// Saved to NVS on "< Back"; wiped and restored to defaults on "Factory Reset".

struct SettingDef {
  const char* label;
  float*      val;
  float       step;
  float       vmin;
  float       vmax;
  bool        isInt;
};

// Initialised from compile-time defaults; changed at runtime via the Settings menu.
float cfgThrottleHigh = TURBO_THROTTLE_HIGH;
float cfgThrottleLow  = TURBO_THROTTLE_LOW;
float cfgRpmMin       = TURBO_RPM_MIN;
float cfgMinGear      = (float)TURBO_MIN_GEAR;
float cfgMaxGear      = (float)TURBO_MAX_GEAR;
float cfgCooldownMs   = (float)TURBO_COOLDOWN_MS;
float cfgVolGear1     = (float)TURBO_VOLUME_GEAR1;
float cfgVolGear2     = (float)TURBO_VOLUME_GEAR2;
float cfgRatio12      = TURBO_RATIO_GEAR12;
float cfgRatio23      = TURBO_RATIO_GEAR23;
float cfgVolVoice     = (float)TURBO_VOLUME_VOICE;

static SettingDef CFG_DEFS[] = {
  // Turbo trigger conditions (all must be true simultaneously):
  {"TPS High %",  &cfgThrottleHigh,   5.0f,  10.0f, 100.0f, false}, // TPS must have been above this (hard push)
  {"TPS Low  %",  &cfgThrottleLow,    1.0f,   0.0f,  50.0f, false}, // TPS must now be below this (lifted off)
  {"RPM Min",     &cfgRpmMin,       100.0f, 500.0f, 6000.0f, true }, // engine must be spinning above this RPM
  {"Min Gear",    &cfgMinGear,        1.0f,   1.0f,    6.0f, true }, // only trigger in this gear or higher
  {"Max Gear",    &cfgMaxGear,        1.0f,   1.0f,    6.0f, true }, // only trigger in this gear or lower
  {"Cooldown ms", &cfgCooldownMs,   100.0f, 500.0f,10000.0f, true }, // min time between two triggers (ms)
  // Audio volumes (DFPlayer scale 0–30):
  {"Vol Gear 1",  &cfgVolGear1,       1.0f,   0.0f,   30.0f, true }, // spray volume for 1st gear change
  {"Vol Gear 2",  &cfgVolGear2,       1.0f,   0.0f,   30.0f, true }, // spray volume for 2nd gear change
  {"Vol Voice",   &cfgVolVoice,       1.0f,   0.0f,   30.0f, true }, // voice announcements volume
  // Gear estimation (ratio = RPM ÷ speed_km/h — OBD2 speed is always km/h):
  {"Ratio G1/G2", &cfgRatio12,        5.0f,  30.0f,  200.0f, true }, // ratio above this = 1st gear
  {"Ratio G2/G3", &cfgRatio23,        5.0f,  20.0f,  150.0f, true }, // ratio above this (and ≤ G1/G2) = 2nd gear
};
#define NUM_CFG_DEFS  (int)(sizeof(CFG_DEFS) / sizeof(CFG_DEFS[0]))

// ── NVS persistence (real device and DEMO builds only) ────────────────────
#ifndef SIMULATION
static void loadSettings() {
  Preferences prefs;
  prefs.begin("turbo", true);
  cfgThrottleHigh = prefs.getFloat("tpsHigh",  cfgThrottleHigh);
  cfgThrottleLow  = prefs.getFloat("tpsLow",   cfgThrottleLow);
  cfgRpmMin       = prefs.getFloat("rpmMin",   cfgRpmMin);
  cfgMinGear      = prefs.getFloat("minGear",  cfgMinGear);
  cfgMaxGear      = prefs.getFloat("maxGear",  cfgMaxGear);
  cfgCooldownMs   = prefs.getFloat("cooldown", cfgCooldownMs);
  cfgVolGear1     = prefs.getFloat("volG1",    cfgVolGear1);
  cfgVolGear2     = prefs.getFloat("volG2",    cfgVolGear2);
  cfgRatio12      = prefs.getFloat("ratio12",  cfgRatio12);
  cfgRatio23      = prefs.getFloat("ratio23",  cfgRatio23);
  cfgVolVoice     = prefs.getFloat("volVoice", cfgVolVoice);
  prefs.end();
}

static void saveSettings() {
  Preferences prefs;
  prefs.begin("turbo", false);
  prefs.putFloat("tpsHigh",  cfgThrottleHigh);
  prefs.putFloat("tpsLow",   cfgThrottleLow);
  prefs.putFloat("rpmMin",   cfgRpmMin);
  prefs.putFloat("minGear",  cfgMinGear);
  prefs.putFloat("maxGear",  cfgMaxGear);
  prefs.putFloat("cooldown", cfgCooldownMs);
  prefs.putFloat("volG1",    cfgVolGear1);
  prefs.putFloat("volG2",    cfgVolGear2);
  prefs.putFloat("ratio12",  cfgRatio12);
  prefs.putFloat("ratio23",  cfgRatio23);
  prefs.putFloat("volVoice", cfgVolVoice);
  prefs.end();
}

static void resetSettings() {
  Preferences prefs;
  prefs.begin("turbo", false);
  prefs.clear();   // wipe the entire "turbo" NVS namespace
  prefs.end();
  cfgThrottleHigh = TURBO_THROTTLE_HIGH;
  cfgThrottleLow  = TURBO_THROTTLE_LOW;
  cfgRpmMin       = TURBO_RPM_MIN;
  cfgMinGear      = (float)TURBO_MIN_GEAR;
  cfgMaxGear      = (float)TURBO_MAX_GEAR;
  cfgCooldownMs   = (float)TURBO_COOLDOWN_MS;
  cfgVolGear1     = (float)TURBO_VOLUME_GEAR1;
  cfgVolGear2     = (float)TURBO_VOLUME_GEAR2;
  cfgRatio12      = TURBO_RATIO_GEAR12;
  cfgRatio23      = TURBO_RATIO_GEAR23;
  cfgVolVoice     = (float)TURBO_VOLUME_VOICE;
}
#endif

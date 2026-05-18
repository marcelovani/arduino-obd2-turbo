// Config.h — Pin assignments, compile-time thresholds, and audio track numbers.
// Pure #defines — no state, no objects.

// ── Encoder ───────────────────────────────────────────────────────────────
#define PIN_ENC_CLK  25
#define PIN_ENC_DT   26
#define PIN_ENC_SW   27

// ── Build-specific pins ───────────────────────────────────────────────────
#ifdef SIMULATION
  #define PIN_BUZZER    17   // passive buzzer — tone() on Turbo fire (TX2 pin, free in sim)
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
#define TRACK_SPRAY_DEFAULT 12 // default spray (0012.wav) — all gear changes
#define TRACK_SPRAY_GEAR1   12 // alternative: longer      — 1st→2nd gear change
#define TRACK_SPRAY_GEAR2   12 // alternative: shorter     — 2nd→3rd gear change
#define TRACK_SPRAY_GEAR3   12 // (same as default)        — 3rd→4th gear change

// ── Turbo thresholds (compile-time defaults — overridden at runtime via cfg*) ──
#define TURBO_THROTTLE_HIGH  60.0f   // % — TPS must have been above this (hard acceleration)
#define TURBO_THROTTLE_LOW   10.0f   // % — TPS must now be below this (lifted off)
#define TURBO_RPM_MIN        3000.0f // RPM — must be spinning hard when you lift off
#define TURBO_MIN_GEAR       1
#define TURBO_MAX_GEAR       2
#define TURBO_COOLDOWN_MS    2000
#define TURBO_VOLUME_GEAR1   30      // 100% — DFPlayer max is 30
#define TURBO_VOLUME_GEAR2   27      // 90%
#define TURBO_VOLUME_VOICE   13      // 50% — spoken announcements
#define VOICE_PLAY_MS        3000    // ms to wait for voice clip to finish before muting amp
// Speed-band gear estimation — OBD2 PID 010D returns speed in km/h (SAE J1979).
// Tune via Settings menu. CLA180: shifts 1→2 at ~30 mph (48 km/h), 2→3 at ~40 mph (64 km/h).
#define TURBO_SPEED_GEAR12   50.0f   // km/h (31 mph) — below this → gear 1
#define TURBO_SPEED_GEAR23   65.0f   // km/h (40 mph) — below this (and ≥ GEAR12) → gear 2

// Maximum RPM rise per 100 ms poll while throttle is high before a trigger is
// suppressed. A fast rise means the clutch is in and the engine is free-revving
// (no load), not a real gear change. Under load, RPM rises ~50–300 RPM/poll;
// free-revving can exceed 500–1000 RPM/poll.
// Set to 0 to disable the check entirely (default — off).
#define TURBO_RPM_RISE_MAX     0.0f  // RPM per poll — 0 = disabled; e.g. 500 to enable

// ── Engine state thresholds ───────────────────────────────────────────────
#define ENGINE_IDLE_RPM    200.0f    // below = engine off → parked screen
#define ENGINE_DRIVING_RPM 1000.0f   // above = driving → poll TPS + speed + RPM

// ── Display views ─────────────────────────────────────────────────────────
#define STEPS_PER_ZONE  1   // 1 encoder detent = 1 view change
#define NUM_VIEWS       2   // total display screens (bars, text)

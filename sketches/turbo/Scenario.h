// Scenario.h — Built-in demo drive cycle replayed in all builds.
//
// Used when demoMode is true. Interpolates linearly between data points so the
// display animates smoothly. Loops every ~24 s with two Turbo triggers per cycle.
//
// Full cycle:
//   0–3s    ignition on, engine off   → parked screen
//   3–8s    engine idle, not moving   → idle screen
//   8–19s   driving: 1st→2nd→3rd→4th → two Turbo triggers
//   19–24s  stopped, engine idle      → idle then loop
//
// Gear thresholds (ratio = RPM / km/h): >85=1st  >45=2nd  >19=3rd  >12=4th
// CLA180: shift 1→2 at ~30 mph (48 km/h), shift 2→3 at ~40 mph (64 km/h).
// TPS held low for 200 ms after each lift-off ensures the 100 ms check window
// captures the high→low transition.

struct DataPoint { uint32_t t; float tps; float rpm; float speed; };

static const DataPoint SCENARIO[] = {
  // ── Parked ────────────────────────────────────────────────────────────────
  {     0,  0,    0,    0 },   // ignition on, engine off → parked screen
  {  3000,  0,    0,    0 },   // end of ignition-on window
  {  3100,  0,  750,    0 },   // engine cranks → idle screen (RPM 200–999)
  {  8000,  0,  850,    0 },   // 5 s idle — rotary fully responsive

  // ── 1st gear: 0→48 km/h (30 mph) in ~1.5 s ───────────────────────────────
  {  8200, 85, 1500,    2 },   // snap to throttle
  {  9500, 90, 4000,   46 },   // near redline  (ratio=87 → gear 1)
  {  9501,  4, 3900,   46 },   // *** Turbo #1 *** instant lift-off
  {  9700,  4, 3800,   47 },   // hold TPS low 200 ms

  // ── 2nd gear: 48→70 km/h (30→43 mph) in ~2.8 s ──────────────────────────
  {  9900, 85, 2600,   54 },   // back on throttle  (ratio=48 → gear 2)
  { 12500, 88, 3500,   70 },   // near redline  (ratio=50 → gear 2)
  { 12501,  4, 3400,   70 },   // *** Turbo #2 *** instant lift-off
  { 12700,  4, 3300,   71 },   // hold TPS low 200 ms

  // ── 3rd gear: 70→140 km/h (43→87 mph) in 3 s ────────────────────────────
  { 12900, 50, 2200,   74 },   // back on throttle  (ratio=30 → gear 3)
  { 15900, 45, 2800,  140 },   // cruising  (ratio=20 → gear 3)

  // ── 4th gear: 140→161 km/h (87→100 mph) in ~2 s ─────────────────────────
  { 16000, 28, 2200,  145 },   // shift to 4th  (ratio=15 → gear 4)
  { 17900, 25, 2400,  161 },   // 100 mph  (ratio=15 → gear 4)

  // ── Stop ──────────────────────────────────────────────────────────────────
  { 18500,  0, 1500,  100 },   // lift off and brake
  { 19500,  0,  850,    0 },   // stopped → idle screen
  { 24500,  0,  800,    0 },   // 5 s idle then loop
};
static const int SCENARIO_LEN = sizeof(SCENARIO) / sizeof(SCENARIO[0]);

void advanceScenario() {
  uint32_t elapsed = millis() - scenStart;
  while (scenIdx + 1 < SCENARIO_LEN - 1 && SCENARIO[scenIdx + 1].t <= elapsed)
    scenIdx++;
  if (elapsed >= SCENARIO[SCENARIO_LEN - 1].t) {
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

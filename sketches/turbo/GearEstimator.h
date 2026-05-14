// GearEstimator.h — Estimates current gear from speed only (speed-band approach).
//
// OBD2 PID 010D always reports km/h (SAE J1979) regardless of country.
// Speed bands are monotonic so gear naturally drops as speed drops — no stickiness
// from clutch-depressed states where RPM is at idle but speed still has momentum.
//
// Gear 1/2 and 2/3 boundaries are runtime-tunable via cfgSpeed12 / cfgSpeed23.
// Gears 3–6 use fixed bands (calibrated for a typical small-engine manual car).

int estimateGear(float rpm, float speed) {
  if (speed < 3.0f || rpm < 200.0f) return 0;
  if (speed < cfgSpeed12) return 1;   // < 50 km/h (30 mph)
  if (speed < cfgSpeed23) return 2;   // < 65 km/h (40 mph)
  if (speed < 145.0f)     return 3;
  if (speed < 165.0f)     return 4;
  if (speed < 200.0f)     return 5;
  return 6;
}

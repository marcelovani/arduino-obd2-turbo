// GearEstimator.h — Estimates current gear from RPM and speed.
//
// Uses ratio = RPM ÷ speed_km/h. OBD2 PID 010D always reports km/h (SAE J1979)
// regardless of country, so the ratio is correct globally.
//
// Gear 1/2 thresholds are runtime-tunable via cfgRatio12 / cfgRatio23.
// Gears 3–6 use fixed ratios (calibrated for a typical small-engine manual).
// Tune gear 1/2 via the Settings menu after driving steadily in each gear.

int estimateGear(float rpm, float speed) {
  if (speed < 2.0f || rpm < 100.0f) return 0;
  float ratio = rpm / speed;
  if      (ratio > cfgRatio12) return 1;
  else if (ratio > cfgRatio23) return 2;
  else if (ratio > 19.0f)      return 3;
  else if (ratio > 12.0f)      return 4;
  else if (ratio >  8.0f)      return 5;
  else                         return 6;
}

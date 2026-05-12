"""
Python mirror of the C++ OBD logic in the Arduino sketch.

These functions are kept in sync with the constants and algorithms in:
  sketches/turbo/turbo.ino

They are used by:
  - Unit tests (tests/unit/)
  - Scenario simulation (tests/visual_monitor.py)
  - Integration tests (tests/integration/)

When you change a threshold or algorithm in the sketch, update it here too.
"""

# ── Turbo trigger thresholds ────────────────────────────────────────────────
# Keep in sync with #define TURBO_* in the sketches

TURBO_THROTTLE_HIGH = 40.0   # TPS must have been aturboe this (accelerating)
TURBO_THROTTLE_LOW  = 10.0   # TPS must now be below this (lifted off)
TURBO_RPM_MIN       = 1500.0 # must be in boost range
TURBO_MAX_GEAR      = 2      # only trigger in 1st and 2nd gear
TURBO_COOLDOWN_MS   = 2000   # min ms between Turbo sounds


def parse_pid(resp: str, byte_count: int, multiplier: float) -> float:
    """
    Mirror of C++ parsePID(resp, bytes, multiplier).

    Parses a compact ELM327 response (ATS0 mode, no spaces).
    Example responses:
      "410D3C"     → speed 60 km/h  (byte_count=1, multiplier=1.0)
      "410CXXYY"   → RPM            (byte_count=2, multiplier=0.25)
      "41113F"     → throttle ~25%  (byte_count=1, multiplier=100/255)

    Returns -1.0 on bad/short/empty response.
    """
    resp = resp.strip().upper()
    if not resp or len(resp) < 4 + byte_count * 2:
        return -1.0
    hex_part = resp[4:]  # skip "41XX" header
    try:
        if byte_count == 1:
            return int(hex_part[:2], 16) * multiplier
        hi = int(hex_part[:2], 16)
        lo = int(hex_part[2:4], 16)
        return (hi * 256 + lo) * multiplier
    except ValueError:
        return -1.0


def estimate_gear(rpm: float, speed_kmh: float) -> int:
    """
    Mirror of C++ estimateGear(rpm, speedKmh).

    Estimates current gear from the RPM/speed ratio.
    Returns 0 if gear cannot be determined (stopped or engine off).

    Thresholds are approximate for a typical small gasoline car.
    Calibrate for your specific car by driving in each gear at steady
    speed and noting the RPM/speed ratio (RPM ÷ km/h).

    Mercedes CLA180 calibration:
      Gear 1: ratio ~130–180
      Gear 2: ratio ~75–100
      Gear 3: ratio ~50–65
      Gear 4: ratio ~37–48
      Gear 5: ratio ~28–36
      Gear 6: ratio ~22–28
    These are rough — update from serial monitor logs in Phase 5.
    """
    if speed_kmh < 2 or rpm < 500:
        return 0
    ratio = rpm / speed_kmh
    if ratio > 120: return 1
    if ratio > 70:  return 2
    if ratio > 50:  return 3
    if ratio > 35:  return 4
    if ratio > 25:  return 5
    return 6


class TurboTrigger:
    """
    Mirror of C++ checkTurbo() — stateful Turbo trigger logic.

    Tracks previous TPS and cooldown timer. Call update() on every
    OBD poll cycle (every 100 ms). Returns True when Turbo should fire.

    Thresholds can be overridden for scenario tuning:
        trigger = TurboTrigger(throttle_high=35, rpm_min=1200)
    """

    def __init__(
        self,
        throttle_high: float = TURBO_THROTTLE_HIGH,
        throttle_low:  float = TURBO_THROTTLE_LOW,
        rpm_min:       float = TURBO_RPM_MIN,
        max_gear:      int   = TURBO_MAX_GEAR,
        cooldown_ms:   float = TURBO_COOLDOWN_MS,
    ):
        self.throttle_high = throttle_high
        self.throttle_low  = throttle_low
        self.rpm_min       = rpm_min
        self.max_gear      = max_gear
        self.cooldown_ms   = cooldown_ms

        self._prev_tps    = 0.0
        self._last_turbo_ms = -(cooldown_ms + 1)  # allow immediate first trigger
        self.count        = 0
        self.events: list[dict] = []             # log of every trigger

    def update(self, tps: float, rpm: float, speed: float, now_ms: float) -> bool:
        """
        Process one poll cycle.
        Returns True if Turbo triggered this cycle.
        """
        gear = estimate_gear(rpm, speed)
        triggered = (
            now_ms - self._last_turbo_ms >= self.cooldown_ms
            and self._prev_tps > self.throttle_high
            and tps            < self.throttle_low
            and rpm            > self.rpm_min
            and 0 < gear      <= self.max_gear
        )
        if triggered:
            self._last_turbo_ms = now_ms
            self.count += 1
            self.events.append({
                "time_ms":  now_ms,
                "prev_tps": self._prev_tps,
                "tps":      tps,
                "rpm":      rpm,
                "speed":    speed,
                "gear":     gear,
            })
        self._prev_tps = tps
        return triggered

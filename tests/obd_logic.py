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

TURBO_THROTTLE_HIGH = 60.0   # TPS must have been above this (hard acceleration)
TURBO_THROTTLE_LOW  = 10.0   # TPS must now be below this (lifted off)
TURBO_RPM_MIN       = 3000.0 # must be near peak RPM (about to shift)
TURBO_MIN_GEAR      = 1      # trigger from 1st gear upward
TURBO_MAX_GEAR      = 2      # only trigger in 2nd gear or lower
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
    if speed_kmh < 2 or rpm < 100:
        return 0
    ratio = rpm / speed_kmh
    if ratio > 50: return 1
    if ratio > 33: return 2
    if ratio > 19: return 3
    if ratio > 12: return 4
    if ratio >  8: return 5
    return 6


class MenuController:
    """
    Mirror of the C++ menu state machine in turbo.ino.

    States: closed → main → settings → edit (and back).
    Call button_press() for encoder-button clicks, rotate(delta) for turns.
    """

    # Main menu item indices
    MAIN_POWER    = 0
    MAIN_DEMO     = 1
    MAIN_SETTINGS = 2
    MAIN_EXIT     = 3
    NUM_MAIN      = 4

    # Settings: (label, attr_name, step, vmin, vmax)
    CFG_DEFS = [
        ("TPS High %",  "tps_high",    5.0,  10.0, 100.0),
        ("TPS Low  %",  "tps_low",     1.0,   0.0,  50.0),
        ("RPM Min",     "rpm_min",   100.0, 500.0, 6000.0),
        ("Min Gear",    "min_gear",    1.0,   1.0,    6.0),
        ("Max Gear",    "max_gear",    1.0,   1.0,    6.0),
        ("Cooldown ms", "cooldown_ms",100.0, 500.0,10000.0),
        ("Vol Gear 1",  "vol_gear1",   1.0,   0.0,   30.0),
        ("Vol Gear 2",  "vol_gear2",   1.0,   0.0,   30.0),
    ]
    NUM_SETTINGS = len(CFG_DEFS)   # Back is index NUM_SETTINGS

    def __init__(self):
        self.menu_state   = "closed"   # closed | main | settings | edit
        self.main_sel     = 0
        self.settings_sel = 0
        self.system_on    = True
        self.demo_mode    = False
        # Settings values — defaults mirror C++ cfg* initial values
        self.tps_high    = 60.0
        self.tps_low     = 10.0
        self.rpm_min     = 3000.0
        self.min_gear    = 1.0
        self.max_gear    = 2.0
        self.cooldown_ms = 2000.0
        self.vol_gear1   = 30.0
        self.vol_gear2   = 27.0

    def button_press(self):
        """Simulate one encoder button press."""
        if not self.system_on:
            self.system_on  = True
            self.menu_state = "main"
            self.main_sel   = 0
        elif self.menu_state == "closed":
            self.menu_state = "main"
            self.main_sel   = 0
        elif self.menu_state == "main":
            self._execute_main()
        elif self.menu_state == "settings":
            self._execute_settings()
        elif self.menu_state == "edit":
            self.menu_state = "settings"   # confirm value

    def _execute_main(self):
        sel = self.main_sel
        if sel == self.MAIN_POWER:
            self.system_on = not self.system_on
            if not self.system_on:
                self.menu_state = "closed"
        elif sel == self.MAIN_DEMO:
            self.demo_mode = not self.demo_mode
            self.menu_state = "closed"
        elif sel == self.MAIN_SETTINGS:
            self.menu_state   = "settings"
            self.settings_sel = 0
        elif sel == self.MAIN_EXIT:
            self.menu_state = "closed"

    def _execute_settings(self):
        if self.settings_sel == self.NUM_SETTINGS:   # Back
            self.menu_state = "main"
        else:
            self.menu_state = "edit"

    def rotate(self, delta: int):
        """Simulate encoder rotation."""
        if self.menu_state == "main":
            self.main_sel = (self.main_sel + delta) % self.NUM_MAIN
        elif self.menu_state == "settings":
            total = self.NUM_SETTINGS + 1   # +1 for Back
            self.settings_sel = (self.settings_sel + delta) % total
        elif self.menu_state == "edit":
            _, attr, step, vmin, vmax = self.CFG_DEFS[self.settings_sel]
            new_val = getattr(self, attr) + delta * step
            setattr(self, attr, max(vmin, min(vmax, new_val)))


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
        min_gear:      int   = TURBO_MIN_GEAR,
        max_gear:      int   = TURBO_MAX_GEAR,
        cooldown_ms:   float = TURBO_COOLDOWN_MS,
    ):
        self.throttle_high = throttle_high
        self.throttle_low  = throttle_low
        self.rpm_min       = rpm_min
        self.min_gear      = min_gear
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
            and self.min_gear <= gear <= self.max_gear
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

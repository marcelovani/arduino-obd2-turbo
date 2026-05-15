"""
Shared Python mirror of the C++ OBD logic in the Arduino sketch.

Kept in sync with:
  sketches/turbo/Config.h        — constants (#define TURBO_*)
  sketches/turbo/GearEstimator.h — estimate_gear()
  sketches/turbo/TurboTrigger.h  — TurboTrigger.update()
  sketches/turbo/OBD2.h          — parse_pid()

Used by:
  tests/   — unit, integration and scenario tests
  viewer/  — recording viewer trigger detection

When you change an algorithm in the sketch, update it here too.
Constants are loaded from Config.h automatically — no manual sync needed.
"""

import os
import re

# ── Load constants from Config.h ─────────────────────────────────────────────

_CONFIG_H = os.path.join(os.path.dirname(__file__), '..', 'sketches', 'turbo', 'Config.h')

_CONFIG_MAP = {
    'TURBO_THROTTLE_HIGH': ('TURBO_THROTTLE_HIGH', float),
    'TURBO_THROTTLE_LOW':  ('TURBO_THROTTLE_LOW',  float),
    'TURBO_RPM_MIN':       ('TURBO_RPM_MIN',        float),
    'TURBO_MIN_GEAR':      ('TURBO_MIN_GEAR',       int),
    'TURBO_MAX_GEAR':      ('TURBO_MAX_GEAR',       int),
    'TURBO_COOLDOWN_MS':   ('TURBO_COOLDOWN_MS',    int),
    'TURBO_SPEED_GEAR12':  ('TURBO_SPEED_GEAR12',   float),
    'TURBO_SPEED_GEAR23':  ('TURBO_SPEED_GEAR23',   float),
}

# Hard fallback — used only if Config.h cannot be read.
_FALLBACK = {
    'TURBO_THROTTLE_HIGH': 60.0,
    'TURBO_THROTTLE_LOW':  10.0,
    'TURBO_RPM_MIN':       3000.0,
    'TURBO_MIN_GEAR':      1,
    'TURBO_MAX_GEAR':      2,
    'TURBO_COOLDOWN_MS':   2000,
    'TURBO_SPEED_GEAR12':  50.0,
    'TURBO_SPEED_GEAR23':  65.0,
}


def _load_config_h():
    values = dict(_FALLBACK)
    try:
        with open(_CONFIG_H) as f:
            for line in f:
                m = re.match(r'#define\s+(\w+)\s+([\d.]+)f?', line)
                if m and m.group(1) in _CONFIG_MAP:
                    name, cast = _CONFIG_MAP[m.group(1)]
                    values[name] = cast(m.group(2))
    except OSError:
        pass
    return values


_cfg = _load_config_h()

TURBO_THROTTLE_HIGH = _cfg['TURBO_THROTTLE_HIGH']
TURBO_THROTTLE_LOW  = _cfg['TURBO_THROTTLE_LOW']
TURBO_RPM_MIN       = _cfg['TURBO_RPM_MIN']
TURBO_MIN_GEAR      = _cfg['TURBO_MIN_GEAR']
TURBO_MAX_GEAR      = _cfg['TURBO_MAX_GEAR']
TURBO_COOLDOWN_MS   = _cfg['TURBO_COOLDOWN_MS']
TURBO_SPEED_GEAR12  = _cfg['TURBO_SPEED_GEAR12']
TURBO_SPEED_GEAR23  = _cfg['TURBO_SPEED_GEAR23']


def load_config_h_as_settings():
    """Return constants from Config.h as a viewer-compatible settings dict."""
    return {
        'throttle_high': TURBO_THROTTLE_HIGH,
        'throttle_low':  TURBO_THROTTLE_LOW,
        'rpm_min':       TURBO_RPM_MIN,
        'min_gear':      TURBO_MIN_GEAR,
        'max_gear':      TURBO_MAX_GEAR,
        'cooldown_ms':   TURBO_COOLDOWN_MS,
        'speed12':       TURBO_SPEED_GEAR12,
        'speed23':       TURBO_SPEED_GEAR23,
    }


# ── Mirror of OBD2.h parsePID() ──────────────────────────────────────────────

def parse_pid(resp: str, byte_count: int, multiplier: float) -> float:
    """
    Mirror of C++ parsePID(resp, bytes, multiplier).

    Parses a compact ELM327 response (ATS0 mode, no spaces).
    Returns -1.0 on bad/short/empty response.
    """
    resp = resp.strip().upper()
    if not resp or len(resp) < 4 + byte_count * 2:
        return -1.0
    hex_part = resp[4:]
    try:
        if byte_count == 1:
            return int(hex_part[:2], 16) * multiplier
        hi = int(hex_part[:2], 16)
        lo = int(hex_part[2:4], 16)
        return (hi * 256 + lo) * multiplier
    except ValueError:
        return -1.0


# ── Mirror of GearEstimator.h estimateGear() ─────────────────────────────────

def estimate_gear(
    rpm: float,
    speed_kmh: float,
    s12: float = TURBO_SPEED_GEAR12,
    s23: float = TURBO_SPEED_GEAR23,
) -> int:
    """
    Mirror of C++ estimateGear(rpm, speedKmh) — speed-band approach.

    s12/s23 can be overridden to match runtime NVS settings from a recording.
    Returns 0 if gear cannot be determined (stopped or engine off).
    """
    if speed_kmh < 3 or rpm < 200:
        return 0
    if speed_kmh < s12:  return 1
    if speed_kmh < s23:  return 2
    if speed_kmh < 145:  return 3
    if speed_kmh < 165:  return 4
    if speed_kmh < 200:  return 5
    return 6


# ── Mirror of TurboTrigger.h checkTurbo() ────────────────────────────────────

class TurboTrigger:
    """
    Mirror of C++ checkTurbo() — stateful Turbo trigger logic.

    Tracks previous TPS and cooldown timer. Call update() on every
    OBD poll cycle (every 100 ms). Returns True when Turbo should fire.

    All thresholds can be overridden to match runtime NVS settings.
    """

    def __init__(
        self,
        throttle_high: float = TURBO_THROTTLE_HIGH,
        throttle_low:  float = TURBO_THROTTLE_LOW,
        rpm_min:       float = TURBO_RPM_MIN,
        min_gear:      int   = TURBO_MIN_GEAR,
        max_gear:      int   = TURBO_MAX_GEAR,
        cooldown_ms:   float = TURBO_COOLDOWN_MS,
        speed12:       float = TURBO_SPEED_GEAR12,
        speed23:       float = TURBO_SPEED_GEAR23,
    ):
        self.throttle_high = throttle_high
        self.throttle_low  = throttle_low
        self.rpm_min       = rpm_min
        self.min_gear      = min_gear
        self.max_gear      = max_gear
        self.cooldown_ms   = cooldown_ms
        self.speed12       = speed12
        self.speed23       = speed23

        self._prev_tps      = 0.0
        self._last_turbo_ms = -(cooldown_ms + 1)
        self.count          = 0
        self.events: list[dict] = []

    def update(self, tps: float, rpm: float, speed: float, now_ms: float) -> bool:
        """Process one poll cycle. Returns True if Turbo triggered."""
        gear = estimate_gear(rpm, speed, self.speed12, self.speed23)
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
                'time_ms':  now_ms,
                'prev_tps': self._prev_tps,
                'tps':      tps,
                'rpm':      rpm,
                'speed':    speed,
                'gear':     gear,
            })
        self._prev_tps = tps
        return triggered

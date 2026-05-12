"""
Minimal ELM327-compatible TCP server for automated integration tests.

Responds to AT commands and OBD2 PIDs with realistic values.
PID values can be updated at runtime to simulate driving scenarios.

Usage:
    server = MockElm327Server(port=35000)
    server.start()
    # ... run tests ...
    server.stop()

Or as a context manager:
    with MockElm327Server(port=35000) as server:
        server.set_pid("010D", 60)   # speed = 60 km/h
        server.set_pid("010C", 3000) # RPM = 3000
"""

import socket
import threading
import time
import logging

logger = logging.getLogger(__name__)


def _encode_pid_response(pid_cmd: str, value: float) -> str:
    """Build an ELM327 compact response string for a given PID and value."""
    pid_cmd = pid_cmd.strip().upper()

    if pid_cmd == "0111":   # throttle %  → 1 byte, A * 100/255
        raw = min(255, max(0, int(value * 255 / 100)))
        return f"4111{raw:02X}"

    if pid_cmd == "010D":   # speed km/h  → 1 byte
        raw = min(255, max(0, int(value)))
        return f"410D{raw:02X}"

    if pid_cmd == "010C":   # RPM         → 2 bytes, (A*256+B)/4
        raw = min(65535, max(0, int(value * 4)))
        hi  = (raw >> 8) & 0xFF
        lo  = raw & 0xFF
        return f"410C{hi:02X}{lo:02X}"

    if pid_cmd == "0105":   # coolant °C  → 1 byte, A - 40
        raw = min(255, max(0, int(value + 40)))
        return f"4105{raw:02X}"

    if pid_cmd == "012F":   # fuel %      → 1 byte, A * 100/255
        raw = min(255, max(0, int(value * 255 / 100)))
        return f"412F{raw:02X}"

    return "NODATA"


class MockElm327Server:
    """
    Lightweight TCP server that speaks ELM327 protocol.
    Serves one client at a time (sufficient for test use).
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 35000):
        self.host = host
        self.port = port
        self._pid_values: dict[str, float] = {
            "0111": 0.0,     # throttle %
            "010D": 0.0,     # speed km/h
            "010C": 800.0,   # RPM (idle)
            "0105": 20.0,    # coolant °C (cold)
            "012F": 75.0,    # fuel %
        }
        self._batt_voltage = 12.4
        self._running  = False
        self._server   = None
        self._thread   = None

    # ── Public API ────────────────────────────────────────────────────────

    def set_pid(self, pid: str, value: float):
        """Set the value returned for a PID command (e.g. set_pid('010D', 60))."""
        self._pid_values[pid.upper()] = value

    def set_battery(self, volts: float):
        self._batt_voltage = volts

    def apply_data_point(self, tps: float, rpm: float, speed: float):
        """Convenience: set throttle, RPM, and speed in one call."""
        self.set_pid("0111", tps)
        self.set_pid("010C", rpm)
        self.set_pid("010D", speed)

    def start(self):
        self._running = True
        self._server  = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server.bind((self.host, self.port))
        self._server.listen(1)
        self._thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._thread.start()
        logger.info("MockElm327Server listening on %s:%d", self.host, self.port)

    def stop(self):
        self._running = False
        if self._server:
            try:
                self._server.close()
            except OSError:
                pass

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *_):
        self.stop()

    # ── Internal ──────────────────────────────────────────────────────────

    def _accept_loop(self):
        while self._running:
            try:
                self._server.settimeout(1.0)
                conn, addr = self._server.accept()
                logger.info("Client connected from %s", addr)
                self._handle_client(conn)
            except (socket.timeout, OSError):
                continue

    def _handle_client(self, conn: socket.socket):
        buf = ""
        conn.settimeout(0.5)
        try:
            while self._running:
                try:
                    chunk = conn.recv(64).decode(errors="replace")
                except socket.timeout:
                    continue
                if not chunk:
                    break
                buf += chunk
                while '\r' in buf:
                    cmd, buf = buf.split('\r', 1)
                    response = self._respond(cmd.strip().upper())
                    conn.sendall((response + '\r\n>').encode())
        except (OSError, ConnectionResetError):
            pass
        finally:
            conn.close()

    def _respond(self, cmd: str) -> str:
        logger.debug("CMD: %r", cmd)

        if cmd == "ATZ":
            time.sleep(0.1)
            return "ELM327 v1.5"

        if cmd in ("ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"):
            return "OK"

        if cmd == "ATRV":
            return f"{self._batt_voltage:.1f}V"

        if cmd in self._pid_values:
            return _encode_pid_response(cmd, self._pid_values[cmd])

        return "NODATA"

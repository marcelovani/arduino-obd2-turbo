"""
Integration tests using MockElm327Server.

These tests verify the full OBD2 communication flow:
  AT init sequence → PID polling → value parsing

For testing against the real ELM327-emulator (optional), start it first:
    make emulator
Then run with:
    make test-emulator-real

Run automated tests with: make test-emulator
"""

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

import pytest
import time
import threading
from mock_elm327 import MockElm327Server
from elm_client   import ElmClient
from obd_logic    import parse_pid, BovTrigger


PORT = 35001  # different port from the default to avoid conflicts


@pytest.fixture(scope="module")
def elm_server():
    """Start the mock server once for all tests in this module."""
    server = MockElm327Server(port=PORT)
    server.start()
    time.sleep(0.1)  # give thread time to bind
    yield server
    server.stop()


@pytest.fixture
def client(elm_server):
    """Fresh client connection for each test."""
    with ElmClient(port=PORT) as c:
        yield c


# ── AT init sequence ──────────────────────────────────────────────────────

class TestAtCommands:

    def test_atz_returns_version(self, client):
        resp = client.query("ATZ", timeout=2.0)
        assert "ELM327" in resp

    def test_ate0_returns_ok(self, client):
        assert client.query("ATE0") == "OK"

    def test_atl0_returns_ok(self, client):
        assert client.query("ATL0") == "OK"

    def test_ats0_returns_ok(self, client):
        assert client.query("ATS0") == "OK"

    def test_ath0_returns_ok(self, client):
        assert client.query("ATH0") == "OK"

    def test_atsp0_returns_ok(self, client):
        assert client.query("ATSP0") == "OK"

    def test_atrv_returns_voltage(self, client):
        resp = client.query("ATRV")
        assert "V" in resp
        assert float(resp.replace("V", "")) > 10.0

    def test_unknown_command_returns_nodata(self, client):
        assert client.query("ATXYZ") == "NODATA"


# ── PID polling ───────────────────────────────────────────────────────────

class TestPidPolling:

    def test_speed_at_zero(self, elm_server, client):
        elm_server.set_pid("010D", 0)
        resp = client.query("010D")
        assert parse_pid(resp, 1, 1.0) == pytest.approx(0.0)

    def test_speed_at_60kmh(self, elm_server, client):
        elm_server.set_pid("010D", 60)
        resp = client.query("010D")
        assert parse_pid(resp, 1, 1.0) == pytest.approx(60.0, abs=1.0)

    def test_rpm_at_idle(self, elm_server, client):
        elm_server.set_pid("010C", 800)
        resp = client.query("010C")
        assert parse_pid(resp, 2, 0.25) == pytest.approx(800.0, abs=2.0)

    def test_rpm_at_3000(self, elm_server, client):
        elm_server.set_pid("010C", 3000)
        resp = client.query("010C")
        assert parse_pid(resp, 2, 0.25) == pytest.approx(3000.0, abs=2.0)

    def test_throttle_zero(self, elm_server, client):
        elm_server.set_pid("0111", 0)
        resp = client.query("0111")
        assert parse_pid(resp, 1, 100.0 / 255.0) == pytest.approx(0.0, abs=0.5)

    def test_throttle_full(self, elm_server, client):
        elm_server.set_pid("0111", 100)
        resp = client.query("0111")
        assert parse_pid(resp, 1, 100.0 / 255.0) == pytest.approx(100.0, abs=0.5)

    def test_throttle_50_percent(self, elm_server, client):
        elm_server.set_pid("0111", 50)
        resp = client.query("0111")
        result = parse_pid(resp, 1, 100.0 / 255.0)
        assert 48.0 < result < 52.0

    def test_coolant_temperature(self, elm_server, client):
        elm_server.set_pid("0105", 90)
        resp = client.query("0105")
        # formula: A - 40
        assert parse_pid(resp, 1, 1.0) - 40 == pytest.approx(90.0, abs=1.0)

    def test_fuel_level(self, elm_server, client):
        elm_server.set_pid("012F", 75)
        resp = client.query("012F")
        result = parse_pid(resp, 1, 100.0 / 255.0)
        assert 73.0 < result < 77.0


# ── End-to-end: BOV trigger via emulator ──────────────────────────────────

class TestBovViaEmulator:
    """
    Simulate a gear change over the mock server and verify the BOV
    trigger logic fires correctly at the right moment.
    """

    def test_bov_fires_on_gear_change(self, elm_server, client):
        bov = BovTrigger()

        # Step through a first-gear acceleration + lift-off
        steps = [
            (0,   0,   800,  0),    # idle
            (100, 70, 3000, 20),    # hard acceleration in 1st
            (200, 80, 3500, 25),    # still pressing
            (300,  4, 3300, 28),    # lift off → BOV expected
        ]

        for (t, tps, rpm, speed) in steps:
            elm_server.apply_data_point(tps, rpm, speed)

            r_tps   = client.query("0111")
            r_speed = client.query("010D")
            r_rpm   = client.query("010C")

            parsed_tps   = parse_pid(r_tps,   1, 100.0 / 255.0)
            parsed_speed = parse_pid(r_speed, 1, 1.0)
            parsed_rpm   = parse_pid(r_rpm,   2, 0.25)

            bov.update(parsed_tps, parsed_rpm, parsed_speed, t)

        assert bov.count == 1, f"Expected 1 BOV trigger, got {bov.count}"

    def test_bov_does_not_fire_at_idle(self, elm_server, client):
        bov = BovTrigger()

        elm_server.apply_data_point(0, 800, 0)
        for t in range(0, 1000, 100):
            r_tps   = client.query("0111")
            r_speed = client.query("010D")
            r_rpm   = client.query("010C")
            bov.update(
                parse_pid(r_tps,   1, 100.0 / 255.0),
                parse_pid(r_rpm,   2, 0.25),
                parse_pid(r_speed, 1, 1.0),
                t,
            )

        assert bov.count == 0

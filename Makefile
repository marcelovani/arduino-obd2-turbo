# ─────────────────────────────────────────────────────────────────────────────
# Arduino OBD2 Turbo BOV Emulator — Test Makefile
#
# Targets:
#   make build            Install Python deps into a virtual environment
#   make test             Run all tests (unit + emulator integration)
#   make test-unit        Run unit tests only (no emulator needed)
#   make test-emulator    Run integration tests using the mock ELM327 server
#   make scenario         Run visual scenario monitor (all scenarios)
#   make scenario SCENARIO=first_gear_change   Run one scenario
#   make scenario THROTTLE_HIGH=35 RPM_MIN=1200  Tune BOV thresholds
#   make emulator         Start the real ELM327-emulator for manual testing
#   make wokwi-setup      Install arduino-cli + ESP32 board + libraries (once)
#   make wokwi-build      Compile Wokwi simulation sketch → firmware .bin/.elf
#   make deploy           Compile + flash production firmware to real ESP32
#   make viewer           Start recording viewer in background (http://localhost:8080)
#   make stop-viewer      Stop the recording viewer
#   make clean            Remove virtual environment and caches
#
# Quick start:
#   make build && make test
#
# Wokwi VS Code extension quick start:
#   brew install arduino-cli   (Mac — one time)
#   make wokwi-setup           (one time)
#   make wokwi-build           (after any sketch change)
# ─────────────────────────────────────────────────────────────────────────────

PYTHON      := python3
VENV        := .venv
VENV_PY     := $(VENV)/bin/python
VENV_PIP    := $(VENV)/bin/pip
VENV_PYTEST := $(VENV)/bin/pytest

# Visual monitor tuning (override on command line)
SCENARIO       ?=
THROTTLE_HIGH  ?=
THROTTLE_LOW   ?=
RPM_MIN        ?=
MAX_GEAR       ?=
COOLDOWN       ?=
SPEED          ?= 1.0

# ELM327 emulator settings
EMULATOR_PORT  := 35000
EMULATOR_PID   := /tmp/elm327_emulator.pid

# Wokwi / Arduino CLI settings
ARDUINO_CLI    := arduino-cli
SKETCH         := sketches/turbo
WOKWI_BUILD    := Emulators/Wokwi/build
FQBN           := esp32:esp32:esp32doit-devkit-v1

PORT ?= $(shell ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -1)

.PHONY: build test test-unit test-emulator scenario emulator \
        wokwi-setup wokwi-build deploy demo-upload clean help viewer stop-viewer

# ─── help ────────────────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "  make build            Install Python dependencies + git hooks"
	@echo "  make test             Run all tests"
	@echo "  make test-unit        Unit tests only (no emulator)"
	@echo "  make test-emulator    Integration tests via mock ELM327 server"
	@echo "  make scenario         Visual scenario monitor (all scenarios)"
	@echo "  make scenario SCENARIO=first_gear_change"
	@echo "  make scenario THROTTLE_HIGH=35 RPM_MIN=1200  (tune thresholds)"
	@echo "  make emulator         Start real ELM327-emulator (manual use)"
	@echo "  make wokwi-setup      Install arduino-cli board + libraries (once)"
	@echo "  make wokwi-build      Compile Wokwi sketch for VS Code extension"
	@echo "  make deploy           Compile + flash production firmware (real OBD2)"
	@echo "  make deploy PORT=/dev/cu.usbserial-XXXX   (specify port manually)"
	@echo "  make demo-upload      Compile + flash DEMO firmware (no OBD2 needed)"
	@echo "  make viewer           Start recording viewer in background (http://localhost:8080)"
	@echo "  make stop-viewer      Stop the recording viewer"
	@echo "  make clean            Remove venv and caches"
	@echo ""

# ─── build ───────────────────────────────────────────────────────────────────
build: $(VENV)
	@echo "→ Installing Python dependencies..."
	$(VENV_PIP) install --quiet --upgrade pip
	$(VENV_PIP) install --quiet -r requirements.txt
	@if [ -f .github/scripts/setup_hooks.sh ]; then .github/scripts/setup_hooks.sh; fi
	@echo "✓ Build complete. Run: make test"

$(VENV):
	@echo "→ Creating virtual environment in $(VENV)/..."
	$(PYTHON) -m venv $(VENV)

# ─── test (all) ──────────────────────────────────────────────────────────────
test: test-unit test-emulator
	@echo ""
	@echo "✓ All tests passed."

# ─── unit tests ──────────────────────────────────────────────────────────────
test-unit: $(VENV)
	@echo ""
	@echo "─── Unit Tests ────────────────────────────────────────────────────"
	$(VENV_PYTEST) tests/unit/ -v --tb=short
	@echo "─── Unit Tests complete ───────────────────────────────────────────"

# ─── integration tests (mock ELM327 server) ──────────────────────────────────
test-emulator: $(VENV)
	@echo ""
	@echo "─── Integration Tests (Mock ELM327 Server) ────────────────────────"
	$(VENV_PYTEST) tests/integration/ -v --tb=short
	@echo "─── Integration Tests complete ────────────────────────────────────"

# ─── integration tests (real ELM327-emulator, optional) ─────────────────────
# Requires the ELM327-emulator to be running: make emulator
# Then in a second terminal: make test-emulator-real
test-emulator-real: $(VENV)
	@echo ""
	@echo "─── Integration Tests (Real ELM327-Emulator on port $(EMULATOR_PORT)) ─"
	@echo "    (make sure 'make emulator' is running in another terminal)"
	EMULATOR_PORT=$(EMULATOR_PORT) $(VENV_PYTEST) tests/integration/ -v --tb=short \
	    -k "not MockElm327Server"
	@echo "─── Done ──────────────────────────────────────────────────────────"

# ─── visual scenario monitor ─────────────────────────────────────────────────
scenario: $(VENV)
	$(VENV_PY) tests/visual_monitor.py \
	    $(if $(SCENARIO),$(SCENARIO),) \
	    $(if $(THROTTLE_HIGH),--throttle-high $(THROTTLE_HIGH),) \
	    $(if $(THROTTLE_LOW),--throttle-low  $(THROTTLE_LOW),) \
	    $(if $(RPM_MIN),--rpm-min       $(RPM_MIN),) \
	    $(if $(MAX_GEAR),--max-gear      $(MAX_GEAR),) \
	    $(if $(COOLDOWN),--cooldown      $(COOLDOWN),) \
	    --speed $(SPEED)

# list available scenarios
scenarios: $(VENV)
	$(VENV_PY) tests/visual_monitor.py --list

# ─── real ELM327-emulator ────────────────────────────────────────────────────
# Starts the ELM327-emulator in the foreground on a TCP socket.
# Use this when you want to test the actual ESP32 sketch against the emulator
# instead of a real car.
#
# The emulator creates a virtual serial device. To expose it over TCP so the
# ESP32 can connect via WiFi (or for local testing), we use socat:
#
#   Terminal 1:  make emulator          ← starts the emulator on /tmp/elm_pty
#   Terminal 2:  make emulator-bridge   ← bridges PTY to TCP port 35000
#   Then connect your test client to localhost:35000
#
# For direct Python testing, the mock server (make test-emulator) is faster.
emulator: $(VENV)
	@echo "→ Installing ELM327-emulator (required for this target only)..."
	$(VENV_PIP) install --quiet --no-build-isolation setuptools ELM327-emulator pyserial
	@echo "Starting ELM327-emulator..."
	@echo "Press Ctrl+C to stop."
	@echo ""
	@echo "Tip: In a second terminal run 'make emulator-bridge' to expose it on TCP port $(EMULATOR_PORT)"
	@echo ""
	$(VENV_PY) -m elm -s 'car: BT4'

# Bridge the ELM327-emulator PTY to a TCP port using socat.
# Requires socat: brew install socat (Mac) or apt install socat (Linux)
emulator-bridge:
	@echo "Bridging ELM327-emulator PTY to TCP port $(EMULATOR_PORT)..."
	@echo "Requires socat. Install with: brew install socat"
	socat TCP-LISTEN:$(EMULATOR_PORT),reuseaddr,fork FILE:/tmp/elm_pty,nonblock,raw,echo=0

# ─── Wokwi firmware build ────────────────────────────────────────────────────
# Compiles sketches/turbo/turbo.ino with -DSIMULATION into Emulators/Wokwi/build/.
# The same sketch without -DSIMULATION is the real-device firmware.
#
# Install arduino-cli first:
#   Mac:   brew install arduino-cli
#   Linux: curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

wokwi-setup:
	@which $(ARDUINO_CLI) > /dev/null 2>&1 || \
	    (echo "arduino-cli not found — install with: brew install arduino-cli" && exit 1)
	@echo "→ Updating board index..."
	$(ARDUINO_CLI) core update-index
	@echo "→ Installing ESP32 board package..."
	$(ARDUINO_CLI) core install esp32:esp32
	@echo "→ Installing Arduino libraries..."
	$(ARDUINO_CLI) lib install "U8g2"
	$(ARDUINO_CLI) lib install "Bounce2"
	@echo "✓ Wokwi setup complete. Run: make wokwi-build"

wokwi-build:
	@which $(ARDUINO_CLI) > /dev/null 2>&1 || \
	    (echo "arduino-cli not found — run: make wokwi-setup" && exit 1)
	@echo "→ Compiling $(SKETCH)/turbo.ino (SIMULATION mode)..."
	$(ARDUINO_CLI) compile \
	    --fqbn $(FQBN) \
	    --build-property "compiler.cpp.extra_flags=-DSIMULATION" \
	    --output-dir $(WOKWI_BUILD) \
	    $(SKETCH)
	@echo "✓ Firmware ready: $(WOKWI_BUILD)/turbo.ino.bin"

# ─── Production deploy: compile + flash to real ESP32 ────────────────────────
# Compiles sketches/turbo/turbo.ino in production mode (BLE OBD2 + DFPlayer +
# LittleFS recorder + WiFi export) and uploads to the connected ESP32.
#
# Uses the "huge_app" partition scheme (3 MB app + 896 KB LittleFS) because
# BLE + WiFi libraries together exceed the 1.25 MB default app partition.
#
# Usage:
#   make deploy                              auto-detect port
#   make deploy PORT=/dev/cu.usbserial-XXXX  specify port manually
PROD_FLAGS := --build-property "build.partitions=huge_app" \
              --build-property "upload.maximum_size=3145728"

deploy:
	@which $(ARDUINO_CLI) > /dev/null 2>&1 || \
	    (echo "arduino-cli not found — run: make wokwi-setup" && exit 1)
	@if [ -z "$(PORT)" ]; then \
	    echo "ERROR: no ESP32 port found. Plug in USB and retry, or run:"; \
	    echo "  make deploy PORT=/dev/cu.usbserial-XXXX"; \
	    exit 1; \
	fi
	@echo "→ Compiling + uploading $(SKETCH)/turbo.ino (production mode, huge_app partition)..."
	$(ARDUINO_CLI) compile \
	    --fqbn $(FQBN) \
	    $(PROD_FLAGS) \
	    --upload \
	    --port $(PORT) \
	    $(SKETCH)
	@echo "✓ Deployed. Open Serial Monitor at 115200 baud."

# ─── Demo upload: compile + flash with -DDEMO ────────────────────────────────
# Runs the built-in 24 s driving scenario on real hardware (SPI OLED, DFPlayer,
# speaker) without needing an OBD2 dongle. Useful for bench testing and for
# replaying a captured log as a custom scenario.
#
# Usage:
#   make demo-upload                              auto-detect port
#   make demo-upload PORT=/dev/cu.usbserial-XXXX  specify port manually
demo-upload:
	@which $(ARDUINO_CLI) > /dev/null 2>&1 || \
	    (echo "arduino-cli not found — run: make wokwi-setup" && exit 1)
	@if [ -z "$(PORT)" ]; then \
	    echo "ERROR: no ESP32 port found. Plug in USB and retry, or run:"; \
	    echo "  make demo-upload PORT=/dev/cu.usbserial-XXXX"; \
	    exit 1; \
	fi
	@echo "→ Compiling + uploading $(SKETCH)/turbo.ino (DEMO mode, huge_app partition)..."
	$(ARDUINO_CLI) compile \
	    --fqbn $(FQBN) \
	    --build-property "compiler.cpp.extra_flags=-DDEMO" \
	    $(PROD_FLAGS) \
	    --upload \
	    --port $(PORT) \
	    $(SKETCH)
	@echo "✓ Demo firmware deployed."

# ─── recording viewer ────────────────────────────────────────────────────────
VIEWER_PID := /tmp/turbo_viewer.pid

viewer:
	@if [ -f $(VIEWER_PID) ] && kill -0 $$(cat $(VIEWER_PID)) 2>/dev/null; then \
	    echo "Viewer already running (PID $$(cat $(VIEWER_PID))) — open http://localhost:8080"; \
	else \
	    $(PYTHON) viewer/server.py & echo $$! > $(VIEWER_PID); \
	    echo "→ Viewer started at http://localhost:8080 (PID $$(cat $(VIEWER_PID)))"; \
	    echo "  Run 'make stop-viewer' to stop."; \
	fi

stop-viewer:
	@if [ -f $(VIEWER_PID) ] && kill -0 $$(cat $(VIEWER_PID)) 2>/dev/null; then \
	    kill $$(cat $(VIEWER_PID)) && rm -f $(VIEWER_PID); \
	    echo "→ Viewer stopped."; \
	else \
	    echo "Viewer is not running."; \
	    rm -f $(VIEWER_PID); \
	fi

# ─── clean ───────────────────────────────────────────────────────────────────
clean:
	rm -rf $(VENV)
	rm -rf __pycache__ tests/__pycache__ tests/unit/__pycache__
	rm -rf tests/integration/__pycache__ tests/scenarios/__pycache__
	rm -rf .pytest_cache tests/.pytest_cache
	rm -f $(EMULATOR_PID)
	@echo "✓ Cleaned."

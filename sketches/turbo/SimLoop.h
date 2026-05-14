// SimLoop.h — Simulation/Demo phase state machine.
//
// Replays the fake BLE scanning → connecting → OBD2 initialisation sequence
// before handing off to the scenario player. Active in SIMULATION and DEMO builds.
//
// SimPhase enum and state variables (simPhase, simPhaseStart, turboSoundUntilMs)
// are defined in turbo.ino before this file is included.

#if defined(SIMULATION) || defined(DEMO)

void doSimLoop(uint32_t now) {
  switch (simPhase) {

    case SIM_SCANNING: {
      if (now - lastDrawMs >= 150) {
        static const char SPIN[] = {'-', '/', '|', '\\'};
        char line1[22];
        snprintf(line1, sizeof(line1), "Scanning OBD2... %c", SPIN[(now / 150) % 4]);
        display.clearBuffer();
        display.setFont(u8g2_font_ncenB10_tr);
        display.drawStr(0, 28, line1);
        display.setFont(u8g2_font_ncenB08_tr);
        display.drawStr(0, 44, "Looking for ELM327");
        display.sendBuffer();
        lastDrawMs = now;
      }
      if (now - simPhaseStart >= 3000) {
        simPhase      = SIM_CONNECTING;
        simPhaseStart = now;
      }
      break;
    }

    case SIM_CONNECTING: {
      if (now - lastDrawMs >= 50) {
        showMessage("Found:", "ELM327-SIM", "Connecting...");
        lastDrawMs = now;
      }
      if (now - simPhaseStart >= 2000) {
        simPhase      = SIM_INIT;
        simPhaseStart = now;
        metricVoltage = 12.4f;
        metricCoolant = 18.0f;   // cold engine
      }
      break;
    }

    case SIM_INIT: {
      bool confirmed = (now - simPhaseStart >= 1500);
      if (now - lastDrawMs >= 50) {
        if (!confirmed) {
          showMessage("Initialising...", "ELM327-SIM");
        } else {
          display.clearBuffer();
          display.setFont(u8g2_font_ncenB10_tr);
          display.drawStr(0, 28, "OBD2 connected!");
          display.setFont(u8g2_font_ncenB08_tr);
          display.drawStr(0, 44, "ELM327-SIM");
          char vbuf[20];
          snprintf(vbuf, sizeof(vbuf), "Battery: %.1f V", metricVoltage);
          display.drawStr(0, 59, vbuf);
          display.sendBuffer();
        }
        lastDrawMs = now;
      }
      if (confirmed && now - simPhaseStart >= 3000) {
        simPhase  = SIM_RUNNING;
        scenStart = millis();
        scenIdx   = 0;
      }
      break;
    }

    case SIM_RUNNING: {
      if (demoMode) {
        advanceScenario();
        checkTurbo(now);
      }
      if (now - lastDrawMs >= 50) {
        drawDisplay();
        lastDrawMs = now;
      }
#ifdef SIMULATION
      // Blink LED fast while buzzer plays, off otherwise
      if (now < turboSoundUntilMs) {
        digitalWrite(PIN_LED, (now / 100) % 2 == 0 ? HIGH : LOW);
      } else {
        digitalWrite(PIN_LED, LOW);
      }
#endif
      break;
    }
  }
}

#endif // SIMULATION || DEMO

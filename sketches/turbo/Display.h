// Display.h — U8g2 OLED object and all screen rendering functions.
//
// Real device : SSD1306 128×64 SPI (4-wire hardware SPI)
// Wokwi sim   : SSD1306 128×64 I2C (Wokwi's SSD1306 component is I2C-only)
//
// Shared metrics (metricTPS, metricRPM, etc.), turboCount, turboUntilMs, and
// currentView are globals defined in turbo.ino, accessible throughout the sketch.

#ifdef SIMULATION
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
#else
  U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI display(U8G2_R0, PIN_OLED_CS, PIN_OLED_DC, PIN_OLED_RES);
#endif

// ── Generic helpers ───────────────────────────────────────────────────────

void showMessage(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(0, 28, line1);
  display.setFont(u8g2_font_ncenB08_tr);
  if (line2) display.drawStr(0, 44, line2);
  if (line3) display.drawStr(0, 59, line3);
  display.sendBuffer();
}

// Draws a framed progress bar filled proportionally.
// OLED yellow zone: top 16 rows. Blue zone: bottom 48 rows. Bars live in blue zone.
void drawBar(int x, int y, int w, int h, float value, float maxVal) {
  int fill = (int)(value / maxVal * (w - 2));
  fill = constrain(fill, 0, w - 2);
  display.drawFrame(x, y, w, h);
  if (fill > 0) display.drawBox(x + 1, y + 1, fill, h - 2);
}

// ── Parked screen (engine off) ────────────────────────────────────────────

void drawParked(const char* deviceName) {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);

  char nameBuf[18];
  strncpy(nameBuf, deviceName, 17); nameBuf[17] = '\0';
  display.drawStr(0, 25, nameBuf);

  char buf[24];
  if (metricVoltage > 0) {
    snprintf(buf, sizeof(buf), "Battery:  %.1f V", metricVoltage);
    display.drawStr(0, 37, buf);
  }
  if (metricCoolant > -999) {
    snprintf(buf, sizeof(buf), "Coolant:  %.0f C", metricCoolant);
    display.drawStr(0, 49, buf);
  }
  if ((millis() / 600) % 2 == 0)
    display.drawStr(0, 61, "Start engine...");
  display.sendBuffer();
}

// ── Live gauge (driving / idle) ───────────────────────────────────────────
// Screen 0 — bars:         Screen 1 — text only:
//  [yellow] Turbo:N Gear:N   [yellow] Turbo:N Gear:N
//  Throttle  N%              Throttle: N%
//  [========  ]              RPM: N
//  RPM  N                    Speed: N mph
//  [====      ]
//  Speed  N mph
//  [===       ]

void drawDisplay() {
  int   gear        = estimateGear(metricRPM, metricSpeed);
  bool  turboRecent = (millis() < turboUntilMs);
  float speedMph    = metricSpeed * 0.621371f;

  display.clearBuffer();

  // Yellow zone header (y = 0–15)
  display.setFont(u8g2_font_ncenB08_tr);
  if (turboRecent) {
    char buf[20];
    snprintf(buf, sizeof(buf), "PSSSSH! #%lu", turboCount);
    display.drawStr(0, 11, buf);
  } else {
    char tStr[16], gStr[12];
    snprintf(tStr, sizeof(tStr), "Turbo: %lu", turboCount);
    snprintf(gStr, sizeof(gStr), "Gear: %d", gear);
    display.drawStr(0, 11, tStr);
    display.drawStr(128 - display.getStrWidth(gStr), 11, gStr);
  }

  // Blue zone (y = 16–63)
  display.setFont(u8g2_font_5x7_tr);
  char buf[24];
  if (currentView == 0) {
    snprintf(buf, sizeof(buf), "Throttle  %.0f%%", metricTPS);
    display.drawStr(0, 23, buf);
    drawBar(0, 25, 128, 5, metricTPS, 100.0f);

    snprintf(buf, sizeof(buf), "RPM  %.0f", metricRPM);
    display.drawStr(0, 38, buf);
    drawBar(0, 40, 128, 5, metricRPM, 8000.0f);

    snprintf(buf, sizeof(buf), "Speed  %.0f mph", speedMph);
    display.drawStr(0, 53, buf);
    drawBar(0, 55, 128, 5, speedMph, 140.0f);
  } else {
    snprintf(buf, sizeof(buf), "Throttle: %.0f%%", metricTPS);
    display.drawStr(0, 26, buf);
    snprintf(buf, sizeof(buf), "RPM: %.0f", metricRPM);
    display.drawStr(0, 40, buf);
    snprintf(buf, sizeof(buf), "Speed: %.0f mph", speedMph);
    display.drawStr(0, 55, buf);
  }
  display.sendBuffer();
}

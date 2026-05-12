// Phase 1 — OLED display test
// Expected: title on line 1, incrementing counter updates every 500ms.
// If display shows garbage or nothing, change SSD1306 to SH1106 in the constructor.
//
// Libraries needed: U8g2

#include <Wire.h>
#include <U8g2lib.h>

// If display shows garbage, replace with:
// U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

int counter = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!display.begin()) {
    Serial.println("Display init failed — check SDA/SCL wiring");
    while (1) delay(100);
  }

  Serial.println("Display OK");
}

void loop() {
  char buf[20];
  snprintf(buf, sizeof(buf), "Count: %d", counter++);

  display.clearBuffer();

  display.setFont(u8g2_font_ncenB10_tr);
  display.drawStr(4, 18, "OBD2 Turbo");

  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(4, 34, "Display OK!");
  display.drawStr(4, 50, buf);

  display.drawFrame(0, 0, 128, 64);

  display.sendBuffer();

  Serial.printf("Frame %d\n", counter);
  delay(500);
}

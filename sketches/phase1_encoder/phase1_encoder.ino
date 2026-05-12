// Phase 1 — KY-040 rotary encoder test
// Expected: rotating the encoder changes the position number on screen.
//   CW  = position increments
//   CCW = position decrements
//   Click = event shows "CLICK" and resets position to 0
//
// Libraries needed: U8g2, Bounce2

#include <Wire.h>
#include <U8g2lib.h>
#include <Bounce2.h>

#define PIN_CLK 25
#define PIN_DT  26
#define PIN_SW  27

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
Bounce btn;

int  position  = 0;
int  lastClk   = HIGH;
const char* lastEvent = "---";

void draw() {
  char buf[24];
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);

  display.drawStr(0, 12, "Encoder Test");

  snprintf(buf, sizeof(buf), "Position: %d", position);
  display.drawStr(0, 30, buf);

  snprintf(buf, sizeof(buf), "Event: %s", lastEvent);
  display.drawStr(0, 46, buf);

  display.setFont(u8g2_font_ncenB06_tr);
  display.drawStr(0, 62, "Rotate or click...");

  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  display.begin();

  pinMode(PIN_CLK, INPUT);
  pinMode(PIN_DT,  INPUT);
  btn.attach(PIN_SW, INPUT_PULLUP);
  btn.interval(25);

  lastClk = digitalRead(PIN_CLK);
  Serial.println("Encoder test ready");
  draw();
}

void loop() {
  bool changed = false;

  // Rotation
  int clk = digitalRead(PIN_CLK);
  if (clk != lastClk) {
    lastClk = clk;
    if (clk == LOW) {
      if (digitalRead(PIN_DT) == HIGH) {
        position--;
        lastEvent = "CCW";
      } else {
        position++;
        lastEvent = "CW";
      }
      Serial.printf("Position: %d (%s)\n", position, lastEvent);
      changed = true;
    }
  }

  // Button click
  btn.update();
  if (btn.fell()) {
    position  = 0;
    lastEvent = "CLICK — reset";
    Serial.println("Click — position reset to 0");
    changed = true;
  }

  if (changed) draw();
}

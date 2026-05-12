// Phase 1 — DFPlayer Mini test
// Expected: press the encoder button to play /mp3/0001.mp3 from the SD card.
// SD card must be FAT32 with a /mp3/ folder containing 0001.mp3.
//
// Libraries needed: U8g2, Bounce2, DFRobotDFPlayerMini

#include <Wire.h>
#include <U8g2lib.h>
#include <Bounce2.h>
#include <DFRobotDFPlayerMini.h>

#define PIN_SW     27
#define PIN_DFP_RX 16   // ESP32 RX2 ← DFPlayer TX
#define PIN_DFP_TX 17   // ESP32 TX2 → DFPlayer RX (via 1kΩ resistor)

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
DFRobotDFPlayerMini dfplayer;
Bounce btn;

int  volume       = 20;   // 0–30
bool dfplayerOk   = false;

void showStatus(const char* status) {
  char volBuf[16];
  snprintf(volBuf, sizeof(volBuf), "Volume: %d/30", volume);

  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 12, "DFPlayer Test");
  display.drawStr(0, 28, volBuf);
  display.drawStr(0, 44, status);
  display.setFont(u8g2_font_ncenB06_tr);
  display.drawStr(0, 60, "Click = play Turbo sound");
  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);

  Wire.begin();
  display.begin();

  btn.attach(PIN_SW, INPUT_PULLUP);
  btn.interval(25);

  showStatus("Initialising...");
  delay(1000);

  if (dfplayer.begin(Serial2)) {
    dfplayer.volume(volume);
    dfplayerOk = true;
    showStatus("Ready!");
    Serial.println("DFPlayer ready");
  } else {
    showStatus("FAILED — check wiring");
    Serial.println("DFPlayer init failed");
    Serial.println("Check: SD card inserted? FAT32? /mp3/0001.mp3 exists?");
    Serial.println("Check: DFPlayer TX→GPIO16, RX→GPIO17 (via 1k resistor)");
  }
}

void loop() {
  btn.update();

  if (btn.fell()) {
    if (dfplayerOk) {
      dfplayer.play(1);
      showStatus("Playing 0001.mp3...");
      Serial.println("Playing track 1");
    } else {
      showStatus("DFPlayer not ready!");
    }
  } else {
    showStatus(dfplayerOk ? "Ready — click to play" : "FAILED — check wiring");
  }
}

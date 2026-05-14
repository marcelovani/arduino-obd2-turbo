// dfplayer_test.ino — standalone DFPlayer audio diagnostic
//
// Plays all six audio tracks in sequence on boot and reports each step
// over Serial so you can confirm exactly what the DFPlayer is doing.
//
// Wiring (same as turbo.ino):
//   ESP32 GPIO16 (RX2) ← 1kΩ → DFPlayer TX
//   ESP32 GPIO17 (TX2)        → DFPlayer RX
//   ESP32 3V3              → DFPlayer VCC
//   ESP32 GND              → DFPlayer GND
//   Speaker (4–8 Ω)        → DFPlayer SPK1 / SPK2
//
// SD card: copy the /mp3/ folder from this repo to the root of a FAT32 card.
// Files are looked up by name — no specific copy order needed.
//   /mp3/0001.mp3  Pairing
//   /mp3/0004.mp3  OBD2 not connected
//   /mp3/0008.mp3  Demo mode
//   /mp3/0009.mp3  Goodbye
//   /mp3/0010.mp3  Long spray (gear 1)
//   /mp3/0011.mp3  Faster spray (gear 2)
//
// Open Serial Monitor at 115200 baud after flashing.

#include <DFRobotDFPlayerMini.h>

#define PIN_DFP_RX  16
#define PIN_DFP_TX  17
#define VOLUME      15    // reduced — 30 overdrives small speakers
#define GAP_MS      4000  // pause after each track — 0004.mp3 is ~2.7s, longest clip

DFRobotDFPlayerMini dfplayer;

static const int TRACK_NUMS[]  = {1, 4, 8, 9, 10, 11};
static const char* TRACK_NAMES[] = {
  "0001.mp3 — Pairing",
  "0004.mp3 — OBD2 not connected",
  "0008.mp3 — Demo mode",
  "0009.mp3 — Goodbye",
  "0010.mp3 — Long spray (gear 1)",
  "0011.mp3 — Faster spray (gear 2)",
};
static const int TRACK_COUNT = sizeof(TRACK_NUMS) / sizeof(TRACK_NUMS[0]);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== DFPlayer test ===");
  Serial.printf("RX=GPIO%d  TX=GPIO%d  volume=%d\n", PIN_DFP_RX, PIN_DFP_TX, VOLUME);

  Serial2.begin(9600, SERIAL_8N1, PIN_DFP_RX, PIN_DFP_TX);
  delay(500);  // DFPlayer needs ~500ms after power-on

  if (!dfplayer.begin(Serial2)) {
    Serial.println("FAIL: DFPlayer did not respond.");
    Serial.println("  Check: VCC=3.3V, GND, 1kΩ on RX line, SD card inserted.");
    return;
  }

  Serial.println("OK: DFPlayer initialised.");
  dfplayer.EQ(DFPLAYER_EQ_NORMAL);
  dfplayer.volume(VOLUME);
  delay(200);

  for (int i = 0; i < TRACK_COUNT; i++) {
    Serial.printf("[%d/%d] Playing: %s\n", i + 1, TRACK_COUNT, TRACK_NAMES[i]);
    dfplayer.playMp3Folder(TRACK_NUMS[i]);
    delay(GAP_MS);
  }

  dfplayer.stop();  // release amp — reduces idle hiss on some clones
  Serial.println("\nDone.");
}

void loop() {}

// Phase 1 — MPU6050 IMU test
// Expected: X/Y/Z acceleration in G shown on OLED and serial monitor.
// Lay the board flat — Z should read ~0 G (gravity subtracted), X and Y ~0 G.
// Tilt the board to see values change.
//
// Libraries needed: U8g2, Adafruit MPU6050, Adafruit Unified Sensor

#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
Adafruit_MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  display.begin();

  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(0, 20, "IMU Test");
  display.drawStr(0, 36, "Initialising...");
  display.sendBuffer();

  if (!mpu.begin()) {
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 20, "MPU6050 not found!");
    display.drawStr(0, 36, "Check SDA/SCL wiring");
    display.sendBuffer();
    Serial.println("MPU6050 not found — check wiring");
    while (1) delay(100);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("MPU6050 ready");
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Convert to G, subtract gravity from Z axis
  float gX = a.acceleration.x / 9.81f;
  float gY = a.acceleration.y / 9.81f;
  float gZ = (a.acceleration.z - 9.81f) / 9.81f;

  Serial.printf("X: %+.2f G  Y: %+.2f G  Z: %+.2f G\n", gX, gY, gZ);

  char buf[24];
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB08_tr);

  display.drawStr(0, 12, "IMU Test (G-force)");

  snprintf(buf, sizeof(buf), "X: %+.2f G", gX);
  display.drawStr(0, 28, buf);

  snprintf(buf, sizeof(buf), "Y: %+.2f G", gY);
  display.drawStr(0, 42, buf);

  snprintf(buf, sizeof(buf), "Z: %+.2f G", gZ);
  display.drawStr(0, 56, buf);

  display.sendBuffer();
  delay(100);
}

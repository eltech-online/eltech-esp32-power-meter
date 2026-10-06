// ElTech-Online ESP32 Power Meter — ADS1115 test
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// The smallest useful sketch for the ADS1115 converter: once a second it
// prints the voltage on inputs A0 and A1 to Serial Monitor. No display, no
// WiFi, and no library for the chip: the sketch talks to it directly over I2C.
//
// Use it to check the level converter and ADS1115 are wired correctly, and to
// watch the knob's voltage change as you turn it.
//
// Wiring: as in the full project (see the wiring diagram in the README).
//   A0 = current sensor OUT (about 2500 mV with no current flowing)
//   A1 = potentiometer middle pin (0 to about 5000 mV as you turn it)

#include <Wire.h>

#define I2C_SDA 8
#define I2C_SCL 9
#define ADS1115_ADDR 0x48   // the ADS1115's I2C address with its ADDR pin on GND

// Reads one input (0-3) and returns its voltage in millivolts, or -1 if the
// chip didn't answer. The full project's sketch explains every step.
float readAdsMillivolts(int channel) {
  uint16_t config = 0xC183 + (channel << 12);   // "measure this input once, range +/-6.144 V"

  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(1);                // register 1 = settings
  Wire.write(config >> 8);
  Wire.write(config & 0xFF);
  if (Wire.endTransmission() != 0) return -1;

  delay(9);                     // wait for the measurement

  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(0);                // register 0 = the result
  if (Wire.endTransmission() != 0) return -1;
  if (Wire.requestFrom(ADS1115_ADDR, 2) != 2) return -1;
  int16_t steps = (Wire.read() << 8) | Wire.read();

  return steps * 0.1875;        // each step is 0.1875 mV at this range
}

// setup() runs once, when the board is powered on or reset.
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("ADS1115 test - readings in millivolts (mV)");
}

// loop() runs over and over, forever.
void loop() {
  float a0 = readAdsMillivolts(0);
  float a1 = readAdsMillivolts(1);

  if (a0 < 0 || a1 < 0) {
    Serial.println("ADS1115 NOT FOUND - check the wiring and the level converter");
  } else {
    Serial.print("A0 (current sensor): ");
    Serial.print(a0, 1);        // 1 = one decimal place
    Serial.print(" mV    A1 (knob): ");
    Serial.print(a1, 1);
    Serial.println(" mV");
  }
  delay(1000);                  // wait 1 second, then loop() runs again
}

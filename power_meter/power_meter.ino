// ElTech-Online ESP32 Power Meter — measures the current a low-voltage DC
// device draws, and shows amps, watts and battery use (mAh) on an OLED and a
// web page.
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// Three techniques in one kit:
//   - ACS712 current sensor  -> CURRENT SENSING: current becomes a voltage
//   - ADS1115 converter      -> an EXTERNAL ADC, far more precise than the
//                               ESP32's own, talked to over I2C register by register
//   - Logic level converter  -> LEVEL SHIFTING: letting a 5 V part and a 3.3 V
//                               board share the same I2C wires safely
// The potentiometer (the knob) sets an over-current warning limit.
//
// SAFETY: LOW-VOLTAGE DC ONLY (batteries, USB, bench supplies up to 24 V).
// Never connect mains electricity to this kit.
//
// Libraries needed (Arduino IDE Library Manager):
//   Adafruit SH110X
//   Adafruit GFX Library
// (click "Install all" if it offers Adafruit BusIO. WiFi and WebServer are
// built into the ESP32 board package. The ADS1115 needs NO library: this
// sketch talks to it directly, which is the lesson.)
// Board package: esp32 by Espressif Systems
//
// How to use:
//   1. Flash this sketch with nothing connected to the sensor's screw terminals.
//   2. Open the web page (WiFi name and password are on the OLED) and press
//      "Set zero". This teaches the meter what "no current" looks like.
//   3. Wire the sensor's two screw terminals IN SERIES with your load: cut
//      into ONE wire going to the load, and put the sensor in the gap.
//   4. Type your supply voltage on the page so the watts are right.
// Full source, wiring diagram and setup guide: github.com/eltech-online/eltech-esp32-power-meter
//
// ---------------------------------------------------------------------------
// New to Arduino code? How to read this file
// ---------------------------------------------------------------------------
// Lines starting with // are comments: notes for people, ignored by the board.
// The file is in this order, and you can read it top to bottom:
//   1. Settings       - pin numbers and sensor values you can safely change
//   2. ADS1115        - reading a voltage from the converter chip over I2C
//   3. Measuring      - turning that voltage into amps, watts and mAh
//   4. Web server     - what the board sends to your browser
//   5. setup()        - runs ONCE when the board is powered on
//   6. loop()         - then runs over and over, forever
//   7. OLED screen    - drawing the readings on the display
//   8. Self-test      - checking every part works at power-on
// A good first experiment: change LIMIT_FULL_SCALE_AMPS below and upload.

#include <WebServer.h>
#include <Preferences.h>   // saves the zero point and supply voltage in flash
#include <Wire.h>          // the I2C bus
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ---- WiFi Access Point settings ----
// Leave both empty ("") and every board gets its OWN network name (e.g.
// "ElTech-PW-A3F2") and its OWN random 8-character password, saved in flash.
// Or type your own: name up to 32 characters, password 8-63 characters.
const char* AP_SSID     = "";
const char* AP_PASSWORD = "";
const bool  AP_OPEN_NETWORK = false;  // true = no password at all
#define KIT_SSID_PREFIX "ElTech-PW-"
#define KIT_PREFS       "power"       // name of this kit's flash "notebook"
#include "eltech_wifi.h"     // starts the WiFi network (second tab in the IDE)
#include "logo_bitmap.h"     // shop logo bitmap for the OLED splash screen
#include "page_template.h"   // the web page's HTML

// ---- I2C ----
#define I2C_SDA 8
#define I2C_SCL 9
#define OLED_ADDR    0x3C   // try 0x3D if the screen stays blank
#define ADS1115_ADDR 0x48   // the ADS1115's address with its ADDR pin on GND

// ---- Which ADS1115 input is which ----
#define CHANNEL_CURRENT 0   // A0 = ACS712 OUT
#define CHANNEL_KNOB    1   // A1 = potentiometer middle pin

// ---- The current sensor ----
// The ACS712 turns current into voltage. With no current its output sits at
// half its supply (about 2.5 V), and it moves 100 mV for every amp: up for
// current one way, down for the other. This number is for the 20 A version.
// (The 5 A version is 185 mV per amp and the 30 A version is 66.)
const float SENSOR_MV_PER_AMP = 100.0;
// Readings closer to zero than this are shown as 0. The sensor's output is a
// little noisy, and a meter that flickers between 0.02 and -0.03 A with
// nothing connected just looks broken.
const float NOISE_FLOOR_AMPS = 0.06;

// Turning the knob from one end to the other sets the warning limit from 0 A
// up to this many amps.
const float LIMIT_FULL_SCALE_AMPS = 5.0;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);

bool oledOK = false, adsOK = false, sensorOK = false;
bool selfTestPassed = false;

// Saved in flash:
float zeroMv = 2500.0;      // the sensor's output with no current flowing
bool zeroSaved = false;
float supplyVolts = 5.0;    // your load's supply voltage, typed on the web page

// The latest measurements. "float" is a number with a decimal point.
float sensorMv = 0;         // raw sensor output, in millivolts
float amps = 0;
float watts = 0;
float limitAmps = 0;        // set by the knob
double mAh = 0;             // charge used so far (what battery capacity is measured in)
double wattHours = 0;       // energy used so far
bool overLimit = false;

void centerText(const String& text, int y, int textSize);
void drawDataScreen();
bool runSelfTest();

// ---------------------------------------------------------------------------
// ADS1115 — reading a voltage over I2C, without a library
// ---------------------------------------------------------------------------
// Why not just use one of the ESP32's own analog pins? Two reasons:
//   - the ESP32's ADC has 12 bits (4096 steps) and is not very straight;
//     the ADS1115 has 16 bits (65536 steps) and is accurate
//   - the ESP32 can only measure up to about 2.5 V, and this sensor's output
//     goes up to nearly 5 V
//
// An I2C chip is a set of numbered "registers", like pigeonholes. You write a
// number into one to tell the chip what to do, and read a number out of
// another to get the answer. The ADS1115 has two that matter here:
//   register 1 = CONFIG:     what to measure and how
//   register 0 = CONVERSION: the result
//
// The 16 bits written to CONFIG mean (see the ADS1115 datasheet, table 8):
//   bit 15      1    = start one measurement now
//   bits 14-12  100  = measure input A0 against ground (101 = A1, 110 = A2, 111 = A3)
//   bits 11-9   000  = range +/-6.144 V  (each step is then 0.1875 mV)
//   bit 8       1    = measure once, then sleep
//   bits 7-5    100  = 128 measurements per second
//   bits 4-0    00011 = alert pin not used
// Put together for A0 that is 1100 0001 1000 0011 in binary = 0xC183 in hex.
const float ADS_MV_PER_STEP = 0.1875;

// Reads one input (0-3) and returns its voltage in millivolts, or -1 if the
// chip didn't answer.
float readAdsMillivolts(int channel) {
  uint16_t config = 0xC183 + (channel << 12);   // put the channel into bits 14-12

  // Write the two bytes of config into register 1.
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(1);                // which register
  Wire.write(config >> 8);      // the top 8 bits...
  Wire.write(config & 0xFF);    // ...then the bottom 8 bits
  if (Wire.endTransmission() != 0) return -1;   // not 0 = the chip didn't answer

  delay(9);                     // one measurement takes 1/128 s = about 8 ms

  // Point at register 0, then read its two bytes.
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(0);
  if (Wire.endTransmission() != 0) return -1;
  if (Wire.requestFrom(ADS1115_ADDR, 2) != 2) return -1;
  int16_t steps = (Wire.read() << 8) | Wire.read();   // glue the two bytes back together

  return steps * ADS_MV_PER_STEP;
}

// The same, averaged over several readings for a steadier number.
float readAdsAveraged(int channel, int samples) {
  float total = 0;
  for (int i = 0; i < samples; i++) {
    float mv = readAdsMillivolts(channel);
    if (mv < 0) return -1;
    total += mv;
  }
  return total / samples;
}

// ---------------------------------------------------------------------------
// Measuring
// ---------------------------------------------------------------------------

// Takes the readings and works out amps, watts and the running totals.
// Called every half second from loop().
void measure() {
  static unsigned long lastMs = 0;

  float mv = readAdsAveraged(CHANNEL_CURRENT, 8);
  float knobMv = readAdsMillivolts(CHANNEL_KNOB);
  if (mv < 0 || knobMv < 0) {       // the chip stopped answering
    adsOK = false;
    return;
  }
  adsOK = true;
  sensorMv = mv;

  // Current: how far the output has moved from its no-current level, divided
  // by how far it moves per amp. Example: 2650 mV with zero at 2500 mV is
  // 150 mV away, and 150 / 100 = 1.5 A.
  amps = (sensorMv - zeroMv) / SENSOR_MV_PER_AMP;
  if (fabs(amps) < NOISE_FLOOR_AMPS) amps = 0;
  amps = fabs(amps);                // show it positive whichever way round it is wired

  // Power in watts = volts x amps.
  watts = supplyVolts * amps;

  // The knob: 0-5 V on its middle pin becomes 0 to LIMIT_FULL_SCALE_AMPS.
  limitAmps = constrain(knobMv / 5000.0, 0.0, 1.0) * LIMIT_FULL_SCALE_AMPS;
  overLimit = limitAmps > 0.05 && amps > limitAmps;

  // Running totals. Current x time = charge. We know the current now and how
  // long it is since the last measurement, so each time round we add that
  // small slice on. 3600000 is the number of milliseconds in an hour.
  unsigned long now = millis();
  if (lastMs != 0) {
    double hours = (now - lastMs) / 3600000.0;
    mAh += amps * 1000.0 * hours;
    wattHours += watts * hours;
  }
  lastMs = now;
}

// Saves the sensor's present output as "no current". Returns a message.
String setZero() {
  float mv = readAdsAveraged(CHANNEL_CURRENT, 32);
  if (mv < 0) return "The ADS1115 is not answering. Check the wiring.";
  if (mv < 2000 || mv > 3000) {
    return "Reading of " + String(mv, 0) + " mV is not near 2500. Is current flowing, or the sensor on 3.3V?";
  }
  zeroMv = mv;
  zeroSaved = true;
  Preferences prefs;
  prefs.begin(KIT_PREFS, false);
  prefs.putFloat("zero_mv", zeroMv);
  prefs.end();
  return "Zero saved: " + String(zeroMv, 1) + " mV.";
}

// ---------------------------------------------------------------------------
// Web server
// ---------------------------------------------------------------------------
// A browser asks the board for an address, and the matching function below
// sends the answer. setup() connects each address to its function.

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "text/html", PAGE_TEMPLATE);
}

void handleStyle() {
  server.send(200, "text/css", STYLE_CSS);
}

// "/data" -> the measurements as JSON, a simple text format programs can read.
// The page asks for it once a second.
String dataJson(const String& message) {
  String json = "{";
  json += "\"amps\":\"" + String(amps, 2) + " A\",";
  json += "\"watts\":\"" + String(watts, 2) + " W\",";
  json += "\"mah\":\"" + String(mAh, 1) + " mAh\",";
  json += "\"wh\":\"" + String(wattHours, 3) + " Wh\",";
  json += "\"limit\":\"" + String(limitAmps, 2) + " A\",";
  json += "\"over\":" + String(overLimit ? "true" : "false") + ",";
  json += "\"sensor_mv\":\"" + String(sensorMv, 1) + " mV\",";
  json += "\"zero_mv\":\"" + String(zeroMv, 1) + " mV\",";
  json += "\"zero_saved\":" + String(zeroSaved ? "true" : "false") + ",";
  json += "\"volts\":" + String(supplyVolts, 1) + ",";
  json += "\"ads\":" + String(adsOK ? "true" : "false") + ",";
  json += "\"msg\":\"" + message + "\",";
  json += "\"selftest\":\"" + String(selfTestPassed ? "PASS" : "FAIL") + "\"}";
  return json;
}

void sendData(const String& message) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json", dataJson(message));
}

void handleData()  { sendData(""); }
void handleZero()  { sendData(setZero()); }

// "/reset" -> the mAh and Wh totals go back to nothing.
void handleReset() {
  mAh = 0;
  wattHours = 0;
  sendData("Totals reset.");
}

// "/volts?v=12" -> your load's supply voltage, saved in flash.
void handleVolts() {
  supplyVolts = constrain(server.arg("v").toFloat(), 0.0, 60.0);
  Preferences prefs;
  prefs.begin(KIT_PREFS, false);
  prefs.putFloat("volts", supplyVolts);
  prefs.end();
  sendData("Supply voltage saved.");
}

// ---------------------------------------------------------------------------
// setup() runs once at power-on, loop() then runs forever
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  oledOK = display.begin(OLED_ADDR, true);
  if (oledOK) {
    display.setTextColor(SH110X_WHITE);   // required, or no text is drawn
    display.setTextWrap(false);
  }

  // -1 is what getFloat() hands back if nothing was ever saved.
  Preferences prefs;
  prefs.begin(KIT_PREFS, true);           // true = read only
  float savedZero = prefs.getFloat("zero_mv", -1);
  supplyVolts = prefs.getFloat("volts", 5.0);
  prefs.end();
  if (savedZero > 0) { zeroMv = savedZero; zeroSaved = true; }

  Serial.println("========================================");
  Serial.println("           ElTech-Online");
  Serial.println("      ESP32 Power Meter (BETA)");
  Serial.println("========================================");

  wifiOK = startAccessPoint();
  selfTestPassed = runSelfTest();
  printWifiDetails();
  Serial.printf("Zero point: %.1f mV (%s). Type z = set zero, r = reset totals\n",
                zeroMv, zeroSaved ? "saved" : "default, press Set zero");

  server.on("/", handleRoot);
  server.on("/style.css", handleStyle);
  server.on("/data", handleData);
  server.on("/zero", HTTP_POST, handleZero);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/volts", HTTP_POST, handleVolts);
  server.begin();

  if (oledOK) {
    display.clearDisplay();
    display.drawBitmap((SCREEN_WIDTH - LOGO_WIDTH) / 2, 0, logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, SH110X_WHITE);
    centerText("ElTech-Online", 36, 1);
    centerText("Power Meter", 48, 1);
    display.display();
    delay(2000);
    if (wifiOK) {
      display.clearDisplay();
      centerText("Connect to WiFi:", 0, 1);
      centerText(apSsid, 12, 1);
      centerText(AP_OPEN_NETWORK ? String("(open network)") : "Pass: " + apPassword, 24, 1);
      centerText("then open:", 36, 1);
      centerText(apUrl, 48, 1);
      display.display();
      delay(6000);
    }
  }
}

void loop() {
  server.handleClient();   // answer any browser that's waiting

  // Serial Monitor shortcuts: type z or r and press Enter.
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'z') Serial.println(setZero());
    if (c == 'r') { mAh = 0; wattHours = 0; Serial.println("Totals reset."); }
  }

  // Measure every half second WITHOUT stopping the web server. millis() is the
  // number of milliseconds since the board started; we note when we last
  // measured and only measure again once 500 ms have gone by. ("static" makes
  // lastMeasure keep its value between runs of loop().)
  static unsigned long lastMeasure = 0;
  if (millis() - lastMeasure >= 500) {
    lastMeasure = millis();
    measure();
    Serial.printf("%.2f A  %.2f W  %.1f mAh  (sensor %.1f mV, limit %.2f A)%s\n",
                  amps, watts, mAh, sensorMv, limitAmps, overLimit ? "  OVER LIMIT" : "");
    if (oledOK) drawDataScreen();
  }
}

// The live data screen:
//
//   y=0   WiFi: ElTech-PW-A3F2      <- network name
//   y=9   Pass: abcd2345            <- password
//   y=21      1.52A                 <- big current reading (text size 2)
//   y=40  7.60W           84mAh     <- power, and charge used so far
//   y=50  Limit 2.50A               <- the knob's warning limit (or OVER LIMIT)
void drawDataScreen() {
  display.clearDisplay();

  if (wifiOK) {
    String nameLine = "WiFi: " + apSsid;
    centerText(nameLine.length() <= 21 ? nameLine : apSsid, 0, 1);
    String passLine = AP_OPEN_NETWORK ? String("Open network") : "Pass: " + apPassword;
    centerText(passLine.length() <= 21 ? passLine : apPassword, 9, 1);
  } else {
    centerText("WiFi FAILED", 0, 1);
    centerText(wifiError, 9, 1);
  }
  display.drawLine(0, 18, SCREEN_WIDTH, 18, SH110X_WHITE);

  if (!adsOK) {
    centerText("ADS1115", 22, 2);
    centerText("not answering", 44, 1);
    display.display();
    return;
  }

  centerText(String(amps, 2) + "A", 21, 2);

  display.setTextSize(1);
  display.setCursor(0, 40);
  display.print(String(watts, 2) + "W");
  String charge = String(mAh, 0) + "mAh";
  display.setCursor(SCREEN_WIDTH - charge.length() * 6, 40);   // 6 pixels per character
  display.print(charge);

  // Flash "OVER LIMIT" on and off twice a second when the current is too high.
  if (overLimit && (millis() / 500) % 2 == 0) {
    display.fillRect(0, 49, SCREEN_WIDTH, 11, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    centerText("OVER LIMIT", 51, 1);
    display.setTextColor(SH110X_WHITE);
  } else {
    centerText("Limit " + String(limitAmps, 2) + "A", 51, 1);
  }
  display.display();
}

// Draws `text` horizontally centered at the given y, for the given text size.
// Each character of the default font is 6 pixels wide at size 1.
void centerText(const String& text, int y, int textSize) {
  display.setTextSize(textSize);
  int x = (SCREEN_WIDTH - (int)text.length() * 6 * textSize) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

// Checks each part is connected AND gives a believable first reading, prints
// the result to Serial, and returns true only if everything passed.
bool runSelfTest() {
  Serial.println("--- Self-test ---");
  Serial.print("OLED (SH1106): "); Serial.println(oledOK ? "OK" : "NOT FOUND");

  // An I2C chip that is there "acknowledges" its address. This also proves the
  // level converter is passing the signals both ways.
  Wire.beginTransmission(ADS1115_ADDR);
  adsOK = Wire.endTransmission() == 0;
  Serial.print("ADS1115:       "); Serial.println(adsOK ? "OK" : "NOT FOUND");

  // The current sensor can't be "found" like an I2C chip. With no current
  // flowing, a working one powered from 5 V sits near 2500 mV.
  Serial.print("ACS712:        ");
  if (!adsOK) {
    Serial.println("NOT TESTED (needs the ADS1115)");
  } else {
    float mv = readAdsAveraged(CHANNEL_CURRENT, 8);
    sensorOK = mv > 2000 && mv < 3000;
    Serial.printf("%s (%.0f mV at rest)\n", sensorOK ? "OK" : "BAD READING", mv);
  }

  Serial.print("Knob:          ");
  if (!adsOK) {
    Serial.println("NOT TESTED (needs the ADS1115)");
  } else {
    Serial.printf("%.0f mV (turn it and watch the limit change)\n", readAdsMillivolts(CHANNEL_KNOB));
  }

  Serial.print("WiFi AP:       ");
  if (wifiOK) { Serial.println("OK"); } else { Serial.print("FAILED ("); Serial.print(wifiError); Serial.println(")"); }

  bool passed = oledOK && adsOK && sensorOK && wifiOK;
  Serial.print("RESULT:        "); Serial.println(passed ? "PASS" : "FAIL");
  return passed;
}

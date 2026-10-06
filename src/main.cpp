/*
  BÓBR AIR starter kit: hardware check

  Checks the screen, the sensor and the touch panel and shows the first numbers.
  After that it's your turn: replace this file with your own program.
  Everything about the hardware is in CLAUDE.md.

  Build and flash:   pio run -e cyd -t upload        (board with one USB port)
                     pio run -e cyd2usb -t upload    (board with two USB ports)
*/
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <bme68xLibrary.h>

// Pins of the Cheap Yellow Display (ESP32-2432S028R)
#define PIN_SDA        27   // sensor, connector CN1
#define PIN_SCL        22
#define PIN_BACKLIGHT  21   // HIGH = light on
#define PIN_TOUCH_IRQ  36   // LOW while the screen is touched
#define PIN_LED_R       4   // RGB LED on the back, LOW = on
#define PIN_LED_G      16
#define PIN_LED_B      17

TFT_eSPI tft;
Bme68x bme;

uint16_t BG, FG, DIM, AMBER, GREEN, RED;
uint8_t sensorAddr = 0;   // I2C address that answered (0x77 or 0x76), 0 = nothing
uint8_t chipId = 0;       // a BME680 says 0x61
bool sensorOk = false;
bool touchOk = false;
uint32_t lastTry = 0;

// ------------------------------------------------------------------ screen helpers
void row(int y, const char *name, const String &value, uint16_t colour, const String &detail = "") {
  tft.fillRect(0, y, 320, 28, BG);
  tft.setTextFont(2);
  tft.setTextColor(DIM, BG);
  tft.drawString(name, 16, y + 6);
  tft.setTextFont(4);
  tft.setTextColor(colour, BG);
  tft.drawString(value, 90, y);
  if (detail.length()) {
    int x = 90 + tft.textWidth(value) + 10;
    tft.setTextFont(2);
    tft.setTextColor(DIM, BG);
    tft.drawString(detail, x, y + 6);
  }
}

void message(const String &text, uint16_t colour) {
  tft.fillRect(0, 210, 320, 30, BG);
  tft.setTextFont(2);
  tft.setTextColor(colour, BG);
  tft.drawString(text, 16, 214);
}

void values(float t, float h, float p) {
  tft.fillRect(0, 168, 320, 32, BG);
  tft.setTextFont(4);
  tft.setTextColor(FG, BG);
  int x = 16;
  String s = String(t, 1);
  tft.drawString(s, x, 172);
  x += tft.textWidth(s) + 6;
  tft.drawCircle(x, 176, 3, FG);              // the built-in fonts have no degree sign
  tft.drawString("C", x + 6, 172);
  x += 6 + tft.textWidth("C") + 22;
  s = String((int)lroundf(h)) + " %";
  tft.drawString(s, x, 172);
  x += tft.textWidth(s) + 22;
  tft.drawString(String((int)lroundf(p)) + " hPa", x, 172);
}

// ------------------------------------------------------------------ sensor
// Ask 0x77 and 0x76 for the chip ID register (0xD0).
void findSensor() {
  sensorAddr = 0;
  chipId = 0;
  const uint8_t addrs[2] = {0x77, 0x76};
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    Wire.write(0xD0);
    if (Wire.endTransmission(false) != 0) continue;
    if (Wire.requestFrom(a, (uint8_t)1) != 1) continue;
    sensorAddr = a;
    chipId = Wire.read();
    return;
  }
}

bool startSensor() {
  findSensor();
  char addr[8];
  snprintf(addr, sizeof(addr), "0x%02X", sensorAddr);
  if (!sensorAddr) {
    Serial.println("sensor: nothing answers at 0x77 or 0x76");
    row(98, "Sensor", "Not found", AMBER);
    message("Fine before soldering. After: check the joints.", AMBER);
    return false;
  }
  if (chipId != 0x61) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Chip ID 0x%02X at %s, not a BME680.", chipId, addr);
    Serial.printf("sensor: %s A BME680 says 0x61.\n", msg);
    row(98, "Sensor", "Wrong chip", RED);
    message(msg, RED);
    return false;
  }
  bme.begin(sensorAddr, Wire);
  if (bme.checkStatus() == BME68X_ERROR) {
    Serial.println("sensor: " + bme.statusString());
    row(98, "Sensor", "Error", RED);
    message(bme.statusString(), RED);
    return false;
  }
  bme.setTPH();   // temperature, pressure and humidity with the default oversampling. No gas heater.
  Serial.printf("sensor: BME680 at %s\n", addr);
  row(98, "Sensor", "OK", GREEN, String("BME680 at ") + addr);
  message("Your turn. Tell Claude what to build.", AMBER);
  return true;
}

bool readSensor(float &t, float &h, float &p) {
  bme.setOpMode(BME68X_FORCED_MODE);
  delayMicroseconds(bme.getMeasDur() + 2000);
  if (!bme.fetchData()) return false;
  bme68xData d;
  bme.getData(d);
  t = d.temperature;
  h = d.humidity;
  p = d.pressure / 100.0f;   // Pa -> hPa
  return true;
}

// ------------------------------------------------------------------ setup / loop
void setup() {
  Serial.begin(115200);
  for (int pin : {PIN_LED_R, PIN_LED_G, PIN_LED_B}) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); }   // LED off
  pinMode(PIN_TOUCH_IRQ, INPUT);

  tft.init();
  tft.setRotation(1);   // landscape. If the picture is upside down in the case, use 3.
#ifdef CYD2USB
  // the two-port board looks washed out with the default gamma curve
  tft.writecommand(0x26); tft.writedata(2); delay(120);
  tft.writecommand(0x26); tft.writedata(1);
#endif
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, HIGH);

  BG = tft.color565(14, 17, 20);
  FG = tft.color565(240, 238, 232);
  DIM = tft.color565(128, 134, 142);
  AMBER = tft.color565(242, 169, 59);
  GREEN = tft.color565(98, 196, 120);
  RED = tft.color565(226, 92, 72);

  tft.fillScreen(BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(FG, BG);
  tft.drawString("Hej Szymon!", 16, 12);
  tft.setTextFont(2);
  tft.setTextColor(DIM, BG);
  tft.drawString("Starter kit: hardware check", 16, 42);

  row(66, "Screen", "OK", GREEN, "if you can read this");
  row(130, "Touch", "Tap the screen", DIM);
  Serial.println("starter: screen on");

  Wire.begin(PIN_SDA, PIN_SCL);
  sensorOk = startSensor();
  lastTry = millis();
}

void loop() {
  if (!touchOk && digitalRead(PIN_TOUCH_IRQ) == LOW) {
    touchOk = true;
    row(130, "Touch", "OK", GREEN);
    Serial.println("touch: ok");
  }

  if (millis() - lastTry >= 2000) {   // every 2 seconds
    lastTry = millis();
    if (!sensorOk) {
      sensorOk = startSensor();       // maybe the plug was loose
    } else {
      float t, h, p;
      if (readSensor(t, h, p)) {
        values(t, h, p);
        Serial.printf("T %.1f C  H %.0f %%  P %.0f hPa\n", t, h, p);
      } else {
        Serial.println("sensor: no new data");
      }
    }
  }
  delay(10);
}

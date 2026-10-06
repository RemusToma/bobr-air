/*
  BÓBR AIR  -  desk climate station
  Board : ESP32-2432S028R "Cheap Yellow Display" (CYD, 1 USB port, ILI9341)
          or the CYD2USB (2 USB ports, ST7789). The display driver is chosen at compile time.
  Sensor: Bosch BME680 on I2C (SDA = IO27, SCL = IO22, connector CN1), read with Bosch BSEC2.

  Screen: temperature, humidity, pressure and an air quality index (IAQ, 0..500).
  The bóbr speaks up when the air gets stuffy.
  Hold a finger on the screen for 3 seconds to turn the picture by 180 degrees (saved).

  Made by Remus for Szymon, October 2026.  Sto lat!
*/
#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <bsec2.h>
#include "fonts.h"
#include "bobr.h"

// ---------------------------------------------------------------- pins (CYD)
#define PIN_SDA        27
#define PIN_SCL        22
#define PIN_BACKLIGHT  21
#define PIN_TOUCH_IRQ  36      // XPT2046 PENIRQ: LOW while the screen is touched
#define PIN_LED_R       4      // RGB LED on the back, active LOW
#define PIN_LED_G      16
#define PIN_LED_B      17

// ---------------------------------------------------------------- settings
#define STATE_SAVE_MS   (6UL * 60UL * 60UL * 1000UL)   // save the BSEC calibration every 6 hours
#define FLIP_HOLD_MS    3000UL
#define SENSOR_RETRY_MS 5000UL

const uint8_t bsecConfig[] = {
#include "config/bme680/bme680_iaq_33v_3s_4d/bsec_iaq.txt"
};

// ---------------------------------------------------------------- colours
TFT_eSPI tft;
TFT_eSprite spr(&tft);
Bsec2 env;
Preferences prefs;

static uint16_t C_BG, C_FG, C_DIM, C_LINE, C_BUBBLE;
static uint16_t C_GOOD, C_FINE, C_STALE, C_STUFFY, C_BAD, C_GREY;

struct Reading {
  float temp = NAN, hum = NAN, pres = NAN, iaq = NAN;
  uint8_t acc = 0;
  bool fresh = false;
} rd;

static bool     sensorOk = false;
static uint8_t  sensorAddr = 0;
static uint32_t lastRetry = 0, lastSave = 0;
static bool     savedAtAcc3 = false;
static uint8_t  rotation = 1;
static uint8_t  bsecState[BSEC_MAX_STATE_BLOB_SIZE];

// what is on screen now, so we only redraw what changed
static String shownTemp, shownHum, shownPres, shownAir;

// ---------------------------------------------------------------- helpers
static void ledsOff() {
  pinMode(PIN_LED_R, OUTPUT); digitalWrite(PIN_LED_R, HIGH);
  pinMode(PIN_LED_G, OUTPUT); digitalWrite(PIN_LED_G, HIGH);
  pinMode(PIN_LED_B, OUTPUT); digitalWrite(PIN_LED_B, HIGH);
}

static void initColours() {
  C_BG     = tft.color565(14, 17, 20);
  C_FG     = tft.color565(240, 238, 232);
  C_DIM    = tft.color565(128, 134, 142);
  C_LINE   = tft.color565(40, 45, 52);
  C_BUBBLE = tft.color565(42, 47, 55);
  C_GOOD   = tft.color565(98, 196, 120);
  C_FINE   = tft.color565(160, 206, 96);
  C_STALE  = tft.color565(232, 178, 64);
  C_STUFFY = tft.color565(236, 140, 60);
  C_BAD    = tft.color565(226, 92, 72);
  C_GREY   = tft.color565(128, 134, 142);
}

static void text(TFT_eSPI &g, const uint8_t *font, const String &s, int x, int y, uint16_t fg, uint16_t bg,
                 uint8_t datum = TL_DATUM) {
  g.loadFont(font);
  g.setTextColor(fg, bg);
  g.setTextDatum(datum);
  g.drawString(s, x, y);
  g.unloadFont();
}

static int widthOf(TFT_eSPI &g, const uint8_t *font, const String &s) {
  g.loadFont(font);
  int w = g.textWidth(s);
  g.unloadFont();
  return w;
}

static void pushBobr(TFT_eSPI &g, const uint16_t *img, int w, int h, int x, int y) {
  g.setSwapBytes(true);
  g.pushImage(x, y, w, h, img);
  g.setSwapBytes(false);
}

// ---------------------------------------------------------------- screens
static void drawSplash() {
  tft.fillScreen(C_BG);
  pushBobr(tft, bobr_big, BOBR_BIG_W, BOBR_BIG_H, (320 - BOBR_BIG_W) / 2, 18);
  text(tft, f_title, "BÓBR AIR", 160, 150, C_FG, C_BG, TC_DATUM);
  text(tft, f_sub, "Sto lat, Szymon!", 160, 194, C_DIM, C_BG, TC_DATUM);
}

static void drawStatic() {
  tft.fillScreen(C_BG);
  text(tft, f_small, "BÓBR AIR", 14, 10, C_DIM, C_BG);
  text(tft, f_small, "indoor", 306, 10, C_DIM, C_BG, TR_DATUM);
  text(tft, f_small, "HUMIDITY", 214, 34, C_DIM, C_BG);
  text(tft, f_small, "PRESSURE", 214, 88, C_DIM, C_BG);
  tft.drawFastHLine(12, 148, 296, C_LINE);
  shownTemp = shownHum = shownPres = shownAir = "";
}

static void drawTemp(const String &t) {
  if (t == shownTemp) return;
  shownTemp = t;
  spr.setColorDepth(16);
  spr.createSprite(200, 82);
  spr.fillSprite(C_BG);
  spr.loadFont(f_huge);
  spr.setTextColor(C_FG, C_BG);
  spr.setTextDatum(TL_DATUM);
  spr.drawString(t, 0, 0);
  int w = spr.textWidth(t);
  spr.unloadFont();
  spr.loadFont(f_unit22);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString("°C", w + 4, 10);
  spr.unloadFont();
  spr.pushSprite(12, 26);
  spr.deleteSprite();
}

static void drawValue(const String &v, const char *unit, int y, String &shown) {
  if (v == shown) return;
  shown = v;
  spr.setColorDepth(16);
  spr.createSprite(100, 36);
  spr.fillSprite(C_BG);
  spr.loadFont(f_val);
  spr.setTextColor(C_FG, C_BG);
  spr.setTextDatum(TL_DATUM);
  spr.drawString(v, 0, 0);
  int w = spr.textWidth(v);
  spr.unloadFont();
  spr.loadFont(f_unit14);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString(unit, w + 3, 12);
  spr.unloadFont();
  spr.pushSprite(214, y);
  spr.deleteSprite();
}

struct AirLook { const char *label; uint16_t col; const char *msg; };

static AirLook airLook(float iaq) {
  if (iaq <= 50)  return {"Good",   C_GOOD,   nullptr};
  if (iaq <= 100) return {"Fine",   C_FINE,   nullptr};
  if (iaq <= 150) return {"Stale",  C_STALE,  nullptr};
  if (iaq <= 200) return {"Stuffy", C_STUFFY, "Open a window!"};
  return {"Bad", C_BAD, "Time for a walk!"};
}

// air quality block: y 156..240
static void drawAir(const char *label, uint16_t dot, const String &sub, int iaq, const char *msg) {
  String key = String(label) + "|" + sub + "|" + iaq + "|" + (msg ? msg : "");
  if (key == shownAir) return;
  shownAir = key;
  const int Y0 = 156;
  spr.setColorDepth(16);
  spr.createSprite(320, 84);
  spr.fillSprite(C_BG);
  // label
  spr.loadFont(f_small);
  spr.setTextColor(C_DIM, C_BG);
  spr.setTextDatum(TL_DATUM);
  spr.drawString("AIR QUALITY", 14, 160 - Y0);
  spr.unloadFont();
  // dot + word + sub line
  spr.fillSmoothCircle(23, 190 - Y0, 9, dot, C_BG);
  spr.loadFont(f_label);
  spr.setTextColor(C_FG, C_BG);
  spr.drawString(label, 42, 176 - Y0);
  spr.unloadFont();
  spr.loadFont(f_text);
  spr.setTextColor(C_DIM, C_BG);
  spr.drawString(sub, 42, 204 - Y0);
  spr.unloadFont();
  // scale bar 0..300 with a marker
  const int bx0 = 14, bx1 = 196, by = 226 - Y0;
  const int seg[6] = {0, 50, 100, 150, 200, 300};
  const uint16_t segCol[5] = {C_GOOD, C_FINE, C_STALE, C_STUFFY, C_BAD};
  for (int i = 0; i < 5; i++) {
    int xa = bx0 + (bx1 - bx0) * seg[i] / 300;
    int xb = bx0 + (bx1 - bx0) * seg[i + 1] / 300 - 2;
    spr.fillRect(xa, by, xb - xa, 4, segCol[i]);
  }
  if (iaq >= 0) {
    int px = bx0 + (bx1 - bx0) * min(iaq, 300) / 300;
    spr.fillTriangle(px, by - 2, px - 4, by - 8, px + 4, by - 8, C_FG);
  }
  // the bóbr, and what he says
  if (msg) {
    pushBobr(spr, bobr_hot, BOBR_HOT_W, BOBR_HOT_H, 266 - BOBR_HOT_W / 2, 232 - Y0 - BOBR_HOT_H + 4);
    spr.loadFont(f_bubble);
    int tw = spr.textWidth(msg);
    int bw = tw + 16, bx = 244 - bw, byy = 158 - Y0;
    spr.fillSmoothRoundRect(bx, byy, bw, 24, 8, C_BUBBLE, C_BG);
    spr.fillTriangle(bx + bw - 22, byy + 23, bx + bw - 10, byy + 23, bx + bw - 6, byy + 33, C_BUBBLE);
    spr.setTextColor(C_FG, C_BUBBLE);
    spr.drawString(msg, bx + 8, byy + 6);
    spr.unloadFont();
  } else {
    pushBobr(spr, bobr_dim, BOBR_DIM_W, BOBR_DIM_H, 270 - BOBR_DIM_W / 2, 232 - Y0 - BOBR_DIM_H + 4);
  }
  spr.pushSprite(0, Y0);
  spr.deleteSprite();
}

static void drawAll() {
  if (!sensorOk) {
    drawTemp("--.-");
    drawValue("--", "%", 46, shownHum);
    drawValue("----", "hPa", 100, shownPres);
    drawAir("No sensor", C_GREY, "Check the 4 solder joints", -1, nullptr);
    return;
  }
  if (isnan(rd.temp)) {
    drawTemp("--.-");
    drawValue("--", "%", 46, shownHum);
    drawValue("----", "hPa", 100, shownPres);
    drawAir("Starting", C_GREY, "learning the room...", -1, nullptr);
    return;
  }
  drawTemp(String(rd.temp, 1));
  drawValue(String((int)lroundf(rd.hum)), "%", 46, shownHum);
  drawValue(String((int)lroundf(rd.pres)), "hPa", 100, shownPres);
  if (rd.acc == 0 || isnan(rd.iaq)) {
    drawAir("Starting", C_GREY, "learning the room...", -1, nullptr);
  } else {
    int iaq = (int)lroundf(rd.iaq);
    AirLook a = airLook(rd.iaq);
    String sub = "IAQ " + String(iaq) + (rd.acc < 3 ? ", still learning" : "");
    drawAir(a.label, a.col, sub, iaq, a.msg);
  }
}

// ---------------------------------------------------------------- BSEC2
static void onData(const bme68xData data, const bsecOutputs outputs, const Bsec2 bsec) {
  for (uint8_t i = 0; i < outputs.nOutputs; i++) {
    const bsecData &o = outputs.output[i];
    switch (o.sensor_id) {
      case BSEC_OUTPUT_IAQ:                                rd.iaq = o.signal; rd.acc = o.accuracy; break;
      case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE: rd.temp = o.signal; break;
      case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:    rd.hum = o.signal; break;
      case BSEC_OUTPUT_RAW_PRESSURE:                       rd.pres = o.signal / 100.0f; break;   // Pa -> hPa
      default: break;
    }
  }
  rd.fresh = true;
  Serial.printf("T %.1f  H %.0f  P %.0f  IAQ %.0f (acc %u)\n", rd.temp, rd.hum, rd.pres, rd.iaq, rd.acc);
}

static bool loadState() {
  size_t n = prefs.getBytes("bsec", bsecState, sizeof(bsecState));
  if (n != sizeof(bsecState)) return false;
  return env.setState(bsecState);
}

static void saveState() {
  if (env.getState(bsecState)) {
    prefs.putBytes("bsec", bsecState, sizeof(bsecState));
    Serial.println("BSEC state saved");
  }
}

static bool startSensor() {
  const uint8_t addrs[2] = {BME68X_I2C_ADDR_HIGH, BME68X_I2C_ADDR_LOW};   // 0x77 (CJ-680 default), 0x76
  for (uint8_t a : addrs) {
    if (env.begin(a, Wire)) { sensorAddr = a; break; }
  }
  if (!sensorAddr) return false;
  if (!env.setConfig(bsecConfig)) Serial.printf("BSEC config failed: %d\n", env.status);
  if (!loadState()) Serial.println("No saved BSEC state yet");
  env.setTemperatureOffset(TEMP_OFFSET_LP);
  bsecSensor list[] = {
    BSEC_OUTPUT_IAQ, BSEC_OUTPUT_RAW_TEMPERATURE, BSEC_OUTPUT_RAW_PRESSURE, BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS, BSEC_OUTPUT_STABILIZATION_STATUS, BSEC_OUTPUT_RUN_IN_STATUS,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE, BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_STATIC_IAQ, BSEC_OUTPUT_CO2_EQUIVALENT, BSEC_OUTPUT_BREATH_VOC_EQUIVALENT
  };
  if (!env.updateSubscription(list, ARRAY_LEN(list), BSEC_SAMPLE_RATE_LP)) {
    Serial.printf("BSEC subscription failed: %d\n", env.status);
    return false;
  }
  env.attachCallback(onData);
  Serial.printf("BME680 found at 0x%02X, BSEC %d.%d.%d.%d\n", sensorAddr, env.version.major, env.version.minor,
                env.version.major_bugfix, env.version.minor_bugfix);
  return true;
}

// ---------------------------------------------------------------- touch: hold 3 s to turn the screen
static bool touchUsable = true;

static void checkFlip() {
  static uint32_t downSince = 0;
  static bool armed = true;
  if (!touchUsable) return;
  bool down = digitalRead(PIN_TOUCH_IRQ) == LOW;
  if (!down) { downSince = 0; armed = true; return; }       // finger lifted: ready for the next long press
  if (!armed) return;
  if (!downSince) { downSince = millis(); return; }
  if (millis() - downSince < FLIP_HOLD_MS) return;
  armed = false;
  downSince = 0;
  rotation = (rotation == 1) ? 3 : 1;
  prefs.putUChar("rot", rotation);
  tft.setRotation(rotation);
  drawStatic();
  drawAll();
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  ledsOff();
  pinMode(PIN_TOUCH_IRQ, INPUT);
  int low = 0;                                   // a pin that reads LOW all the time = no touch signal
  for (int i = 0; i < 20; i++) { if (digitalRead(PIN_TOUCH_IRQ) == LOW) low++; delay(5); }
  touchUsable = low < 20;
  prefs.begin("bobr", false);
  rotation = prefs.getUChar("rot", 1);
  if (rotation != 1 && rotation != 3) rotation = 1;

  tft.init();
  tft.setRotation(rotation);
#ifdef CYD2USB
  // CYD2USB panels look washed out with the default gamma curve
  tft.writecommand(0x26); tft.writedata(2); delay(120);
  tft.writecommand(0x26); tft.writedata(1);
#endif
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, HIGH);
  initColours();

  drawSplash();
  uint32_t t0 = millis();

  Wire.begin(PIN_SDA, PIN_SCL);
  sensorOk = startSensor();
  lastRetry = millis();

  while (millis() - t0 < 2800) delay(10);
  drawStatic();
  drawAll();
}

void loop() {
  if (sensorOk) {
    if (!env.run()) {
      if (env.status < BSEC_OK || env.sensor.status < BME68X_OK) {
        Serial.printf("BSEC %d / BME68x %d, restarting sensor\n", env.status, env.sensor.status);
        sensorOk = false;
        rd = Reading();
        drawAll();
      }
    }
  } else if (millis() - lastRetry > SENSOR_RETRY_MS) {
    lastRetry = millis();
    sensorOk = startSensor();
    drawAll();
  }

  if (rd.fresh) {
    rd.fresh = false;
    drawAll();
    if (rd.acc >= 3 && !savedAtAcc3) { saveState(); savedAtAcc3 = true; lastSave = millis(); }
    if (millis() - lastSave > STATE_SAVE_MS) { saveState(); lastSave = millis(); }
  }
  checkFlip();
  delay(5);
}

# BÓBR AIR: notes for Claude

Szymon built this desk climate station from a kit: an ESP32 display board in a 3D printed case, with a Bosch BME680 sensor. The hardware is finished and soldered. You help him write his own firmware for it. Read this file first; it has everything about the hardware.

## Before you change anything

1. Ask which board he has, if you don't know yet: **one USB port** (env `cyd`) or **two USB ports**, micro-USB and USB-C (env `cyd2usb`). Wrong env: the screen stays white or the colours look wrong.
2. Make sure PlatformIO works: `pio --version`. If it's missing: `pip install platformio` (or `brew install platformio`). The first build downloads the toolchain and libraries, which takes a few minutes.
3. On a new machine, start by flashing the starter in `src/main.cpp`. It checks the screen, the sensor and the touch panel. If it says "Sensor OK", the hardware is fine and any later problem is in the code.

## Build, flash, check

```sh
pio run -e cyd                      # build
pio run -e cyd -t upload            # build and flash, the port is found automatically
python3 tools/serial_log.py 20      # restart the board and print 20 s of serial output
```

- Both envs, and the example in `examples/bobr-air`, build with this platformio.ini (checked with PlatformIO 6.1.18).
- Several serial ports: add `--upload-port /dev/cu.usbserial-XXXX` (macOS), `/dev/ttyUSB0` (Linux) or `COM5` (Windows).
- `pio device monitor` is interactive. Use `tools/serial_log.py` instead; it needs pyserial, which PlatformIO's own Python has (`~/.platformio/penv/bin/python tools/serial_log.py 20`).
- Upload fails with "Failed to connect": hold the BOOT button on the board while it says "Connecting...". Still no luck: another USB cable (many only charge), or `upload_speed = 115200`.
- No port at all on Windows: install the CH340 driver.
- You can't see the screen. After a flash, ask Szymon what he sees, or ask for a photo.
- Commit after every step that works, so you can go back.

## The board: ESP32-2432S028R ("Cheap Yellow Display")

ESP32-WROOM-32 (2 cores, 240 MHz, 520 KB RAM, no PSRAM), 4 MB flash, CH340 USB serial, 2.4 GHz WiFi and Bluetooth. Partition scheme `huge_app.csv`: 3 MB for the program, no OTA.

| What | Pins | Notes |
|---|---|---|
| Display, 320 x 240 | HSPI: MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, no reset pin | ILI9341 (`cyd`) or ST7789 (`cyd2usb`). TFT_eSPI is configured by `build_flags` in platformio.ini. Never edit the library's User_Setup.h. |
| Backlight | IO21 | HIGH = on. PWM works for dimming (ledc). |
| Touch, XPT2046 | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 | Own SPI pins, not the display bus, so TFT_eSPI's touch functions don't work. Use `XPT2046_Touchscreen` (PaulStoffregen) with a second `SPIClass(VSPI)`. IRQ 36 reads LOW while touched. Calibrate the raw values. |
| Sensor, BME680 | I2C on connector CN1: SDA IO27, SCL IO22, 3.3 V, GND | Address 0x77 (try 0x76 too). Chip ID register 0xD0 reads 0x61. |
| RGB LED | R IO4, G IO16, B IO17 | LOW = on. On the back, hidden inside the case. |
| Light sensor (LDR) | IO34 | Analog input only. Good for dimming the screen at night. |
| Speaker connector | IO26 | Through a small amplifier. No speaker in the kit. |
| SD card slot | VSPI: CS 5, MOSI 23, MISO 19, SCK 18 | Probably hard to reach inside the case. |
| BOOT button | IO0 | |
| Connector P3 | IO35 (input only), IO22, IO21 | IO22 is the sensor's SCL, IO21 is the backlight. Don't use them for anything else. |

The two-port board (`cyd2usb`): its USB-C port only works with a USB-A to USB-C cable (no CC resistors), the micro-USB port works with any data cable. Its panel needs the gamma fix you see in `src/main.cpp` (`#ifdef CYD2USB`).

## In the case

- The screen is used in landscape: `tft.setRotation(1)`. If the picture is upside down, use 3. The finished BÓBR AIR lets him flip it by holding a finger on the screen for 3 seconds.
- The sensor sits in its own room below the screen, with vents, away from the warm board. It lies in a T-shaped pocket, chip towards the lid vents, held by 2 x M2 x 4 screws. Its 4 wires run through a slot into the pocket under the board's CN1 connector. The ESP32 and the backlight still warm the case, so the temperature reads high. Bosch BSEC2 corrects this with `setTemperatureOffset()`. BÓBR AIR uses BSEC's default `TEMP_OFFSET_LP` (1.3 °C); the real value for this case is unknown. Compare with a thermometer after an hour and adjust.
- WiFi works through the plastic case.

## Air quality: Bosch BSEC2

The BME680 gives temperature, humidity, pressure and a gas resistance. Bosch's closed library BSEC2 turns the gas resistance into an air quality index. `examples/bobr-air/src/main.cpp` is a complete, working setup. Copy from it.

- Libraries (already in platformio.ini): `Bosch-BSEC2-Library` and `Bosch-BME68x-Library`. BSEC2 links a precompiled `libalgobsec.a`; its `extra_script.py` does that for PlatformIO.
- Config: `bme680_iaq_33v_3s_4d` (BME680, 3.3 V, one sample every 3 s, 4 days of history), loaded with `#include "config/bme680/bme680_iaq_33v_3s_4d/bsec_iaq.txt"`. Sample rate `BSEC_SAMPLE_RATE_LP`.
- Call `env.run()` in every `loop()`. It reads the sensor when BSEC wants it. Don't block `loop()` for long (no long `delay()`, slow HTTP requests in a task or with short timeouts), or BSEC reports timing errors.
- Outputs: IAQ 0 to 500 with an accuracy of 0 to 3, static IAQ, CO2 equivalent, breath VOC equivalent, heat compensated temperature and humidity, pressure in Pa.
- IAQ accuracy goes from 0 to 3 over hours to days. Save the state (`getState`, `BSEC_MAX_STATE_BLOB_SIZE` bytes) to NVS with Preferences, and load it at start (`setState`), so a power cut doesn't start the learning again. BÓBR AIR saves every 6 hours, namespace `bobr`, key `bsec`.
- While BSEC2 runs, it owns the sensor. Don't read the BME680 directly at the same time.
- The IAQ scale BÓBR AIR uses: 0 to 50 Good, 51 to 100 Fine, 101 to 150 Stale, 151 to 200 Stuffy, over 200 Bad.

## Text, fonts and pictures

- TFT_eSPI's built-in fonts (1, 2, 4, 6, 7, 8 and the FreeFonts) only have ASCII. No °, no ó, no Polish letters.
- Smooth fonts (`.vlw`, anti-aliased, any characters) look much better. `examples/bobr-air/tools/gen_assets.py` makes them from the Inter font in `examples/bobr-air/tools/fonts/` as C arrays, plus the bóbr pictures in RGB565. To add characters (for example `ąćęłńóśźżĄĆĘŁŃÓŚŹŻ`), add them to the character strings in `FONTS` and run it again.
- Use sprites (`TFT_eSprite`) for values that change, so the screen doesn't flicker. A full-screen 16-bit sprite needs 150 KB of RAM, so use smaller ones.

## Where things are

| Path | What |
|---|---|
| `src/main.cpp` | The starter: hardware check. His own program goes here. |
| `platformio.ini` | Build setup, envs `cyd` and `cyd2usb` |
| `examples/bobr-air/` | The finished BÓBR AIR firmware: BSEC2, smooth fonts, the bóbr. Build it with `pio run -d examples/bobr-air -e cyd -t upload` |
| `tools/serial_log.py` | Restart the board and read its serial output |
| `docs/` | The website: build guide and web installer, served by GitHub Pages |

**Way back:** https://remustoma.github.io/bobr-air installs the finished BÓBR AIR from Chrome or Edge in 2 minutes. It overwrites whatever is on the board.

## Rules

- Keep both envs building.
- No WiFi passwords or API keys in git. Put them in `src/secrets.h` (it's in `.gitignore`) or use WiFiManager.
- Keep the backlight on IO21 and the sensor on IO27 and IO22. Those wires are soldered.
- Never touch eFuses, secure boot or flash encryption. Those changes are permanent and can lock the board.

## Ideas he might like

- A weeks counter: weeks lived out of 5,218 (that's 100 years, "sto lat"). Needs WiFi and NTP for the date.
- The bóbr speaks Polish.
- Outdoor weather next to the indoor values (Open-Meteo needs no API key).
- A 24 hour chart of the air quality.
- Dim the screen at night with the light sensor on IO34.
- Send the values to Home Assistant with MQTT.

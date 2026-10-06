# BÓBR AIR

A desk climate station Szymon built himself: a 2.8 inch ESP32 display board (the "Cheap Yellow Display") and a Bosch BME680 sensor in a 3D printed case. Build guide and installer: **https://remustoma.github.io/bobr-air**

The hardware is done. The software is up to you.

## Option 1: write your own with Claude (recommended)

1. Install Claude Code. macOS and Linux: `curl -fsSL https://claude.ai/install.sh | bash`. Windows and other ways: [code.claude.com/docs/en/setup](https://code.claude.com/docs/en/setup). It needs a paid Claude plan.
2. Get this kit and start Claude in it:
   ```sh
   git clone https://github.com/remustoma/bobr-air
   cd bobr-air
   claude
   ```
3. Connect the board with a USB data cable and say: **"Flash the starter and check my hardware."**
4. Then ask for what you want, in your own words. Claude writes the code, builds it, flashes it and reads the board's log.

Claude reads [CLAUDE.md](CLAUDE.md) first: pins, the two board versions, the sensor, how to build and flash. Other AI tools read [AGENTS.md](AGENTS.md), which points to the same notes.

## Option 2: install the finished BÓBR AIR

No code: open **https://remustoma.github.io/bobr-air** on a laptop with Chrome or Edge and press Install. It takes 2 minutes. This is also the way back if your own code goes wrong.

## What's in here

| Path | What |
|---|---|
| `src/main.cpp` | The starter: checks the screen, the sensor and the touch panel. Replace it with your program. |
| `platformio.ini` | Build setup for both board versions: `cyd` (one USB port) and `cyd2usb` (two USB ports) |
| `CLAUDE.md` | Hardware notes for Claude |
| `examples/bobr-air/` | The finished BÓBR AIR firmware: air quality with Bosch BSEC2, smooth fonts, the bóbr |
| `tools/serial_log.py` | Restarts the board and prints its log for a few seconds |
| `docs/` | The website with the build guide and the web installer |

## Ideas

- A weeks counter: the weeks you have lived, out of 5,218. That's sto lat.
- The bóbr speaks Polish.
- Outdoor weather next to the indoor values.
- A 24 hour chart of the air quality.
- Dim the screen at night with the light sensor.

## By hand, without AI

Install PlatformIO (`pip install platformio`), connect the board, then `pio run -e cyd -t upload` (one USB port) or `pio run -e cyd2usb -t upload` (two USB ports).

Made by Remus for Szymon, October 2026. Fonts: Inter, SIL Open Font License. Air quality: Bosch BSEC2, under Bosch's license.

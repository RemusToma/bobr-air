# BÓBR AIR, the finished firmware

The software the web installer puts on the board: temperature, humidity, pressure and air quality, with the bóbr. Air quality comes from Bosch BSEC2.

```sh
pio run -e cyd -t upload        # one USB port
pio run -e cyd2usb -t upload    # two USB ports
```

From the repo root, add `-d examples/bobr-air`.

`tools/gen_assets.py` makes `src/fonts.h` (smooth fonts from Inter) and `src/bobr.h` (the bóbr pictures). Run it again after you change the characters or the pictures: `pip install pillow numpy manifold3d`, then `python3 tools/gen_assets.py`.

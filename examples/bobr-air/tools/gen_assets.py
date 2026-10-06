"""Generate src/fonts.h (TFT_eSPI smooth fonts, VLW format) and src/bobr.h (RGB565 images) for BÓBR AIR.

Run from anywhere:   pip install pillow numpy manifold3d
                     python3 tools/gen_assets.py

To add characters (for example Polish letters), add them to the font's character string in FONTS.
The bóbr is drawn from ellipses (see bobr_img) and anti-aliased on the screen background colour BG.
Fonts: Inter (SIL Open Font License, see fonts/OFL.txt).
"""
import struct, sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'src')
INTER = os.path.join(HERE, 'fonts') + os.sep

from manifold3d import CrossSection


class c:
    """Ellipse helper, same as in the case generator: unions and differences of ellipses."""
    @staticmethod
    def ell(cx, cy, rx, ry, rot=0):
        return CrossSection.circle(1.0, 64).scale((rx, ry)).rotate(rot).translate((cx, cy))

ASCII = ''.join(chr(i) for i in range(33, 127))
FONTS = [
    # name,      file,                size, characters
    ('f_huge',   'Inter-Bold.otf',     74, '0123456789.-'),
    ('f_val',    'Inter-Bold.otf',     30, '0123456789.-'),
    ('f_label',  'Inter-Bold.otf',     22, ASCII),
    ('f_unit22', 'Inter-SemiBold.otf', 22, '°C'),
    ('f_unit14', 'Inter-SemiBold.otf', 14, ASCII + '°'),
    ('f_small',  'Inter-SemiBold.otf', 10, ASCII + 'Ó'),
    ('f_text',   'Inter-Regular.otf',  12, ASCII + 'Ó'),
    ('f_bubble', 'Inter-Bold.otf',     12, ASCII),
    ('f_title',  'Inter-Bold.otf',     34, ASCII + 'Ó'),
    ('f_sub',    'Inter-SemiBold.otf', 16, ASCII + 'Ó'),
]


def vlw(path, size, chars):
    font = ImageFont.truetype(path, size)
    glyphs = []
    for ch in sorted(set(chars), key=ord):
        x0, y0, x1, y1 = font.getbbox(ch, anchor='ls')
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        img = Image.new('L', (max(w, 1), max(h, 1)), 0)
        if w and h:
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor='ls')
        adv = int(round(font.getlength(ch)))
        glyphs.append((ord(ch), h, w, adv, -y0, x0, np.asarray(img, np.uint8)[:h, :w].tobytes() if w and h else b''))
    asc_d = -font.getbbox('d', anchor='ls')[1]
    desc_p = font.getbbox('p', anchor='ls')[3]
    ascent = max([g[4] for g in glyphs] + [asc_d])          # cover accents and the degree sign
    descent = max(desc_p, 0)
    out = struct.pack('>6i', len(glyphs), 11, size, 0, ascent, descent)
    for u, h, w, adv, dy, dx, _ in glyphs:
        assert 0 <= h < 256 and 0 <= w < 256 and 0 <= adv < 256 and -128 <= dx < 128, (u, h, w, adv, dx)
        out += struct.pack('>7i', u, h, w, adv, dy, dx, 0)
    for g in glyphs:
        out += g[6]
    return out


def parse_check(data):
    """Parse the VLW like TFT_eSPI does and return a few facts (self-check)."""
    n, _, size, _, asc, desc = struct.unpack('>6i', data[:24])
    ptr = 24 + 28 * n
    total = 0
    for i in range(n):
        u, h, w, adv, dy, dx, _ = struct.unpack('>7i', data[24 + 28 * i: 24 + 28 * (i + 1)])
        total += w * h
    assert ptr + total == len(data), 'bitmap size mismatch'
    return n, size, asc, desc


def c_array(name, data):
    lines = []
    for i in range(0, len(data), 24):
        lines.append('  ' + ','.join(f'0x{b:02X}' for b in data[i:i + 24]) + ',')
    return f'const uint8_t {name}[] PROGMEM = {{\n' + '\n'.join(lines) + '\n};\n'


def bobr_img(height, rgb, bg, facing='left'):
    """Anti-aliased bóbr composited on the screen background, as RGB565 words."""
    k = 4
    s = (c.ell(0.06, 0.31, 0.24, 0.28) + c.ell(0.01, 0.55, 0.18, 0.21, -10) + c.ell(-0.05, 0.79, 0.15, 0.14)
         + c.ell(-0.19, 0.815, 0.10, 0.062, 12) + c.ell(-0.16, 0.705, 0.085, 0.04, -22)
         + c.ell(0.06, 0.905, 0.042, 0.042) + c.ell(0.37, 0.075, 0.22, 0.085, 12)
         + c.ell(0.22, 0.12, 0.10, 0.06, 25) + c.ell(-0.06, 0.035, 0.14, 0.04) + c.ell(-0.14, 0.53, 0.115, 0.048, 35))
    s = s - c.ell(-0.21, 0.755, 0.11, 0.028, 4) - c.ell(-0.06, 0.848, 0.033, 0.033)
    b = s.bounds()
    sc = height * k / (b[3] - b[1])
    W = int(np.ceil((b[2] - b[0]) * sc / k)) + 2
    H = height + 2
    big = Image.new('L', (W * k, H * k), 0)
    d = ImageDraw.Draw(big)
    def tr(p):
        x = (p[0] - b[0]) * sc + k
        if facing == 'right':
            x = W * k - x
        return (x, H * k - ((p[1] - b[1]) * sc + k))
    polys = s.to_polygons()
    def area(p):
        return sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p))) / 2
    for p in polys:
        if area(p) > 0:
            d.polygon([tr(q) for q in p], fill=255)
    for p in polys:
        if area(p) < 0:
            d.polygon([tr(q) for q in p], fill=0)
    a = np.asarray(big.resize((W, H), Image.BOX), float)[..., None] / 255.0
    col = np.array(rgb, float) * a + np.array(bg, float) * (1 - a)
    col = np.clip(np.round(col), 0, 255).astype(np.uint16)
    r, g, bb = col[..., 0], col[..., 1], col[..., 2]
    words = ((r >> 3) << 11) | ((g >> 2) << 5) | (bb >> 3)
    return W, H, words.flatten()


def c_words(name, W, H, words):
    lines = []
    for i in range(0, len(words), 16):
        lines.append('  ' + ','.join(f'0x{int(w):04X}' for w in words[i:i + 16]) + ',')
    return (f'#define {name.upper()}_W {W}\n#define {name.upper()}_H {H}\n'
            f'const uint16_t {name}[] PROGMEM = {{\n' + '\n'.join(lines) + '\n};\n')


if __name__ == '__main__':
    BG = (14, 17, 20)
    parts = ['// Generated by gen_assets.py: smooth fonts (Inter, SIL OFL) in TFT_eSPI VLW format\n#pragma once\n#include <Arduino.h>\n']
    for name, f, size, chars in FONTS:
        data = vlw(INTER + f, size, chars)
        n, sz, asc, desc = parse_check(data)
        print(f'{name:9s} {f:20s} {size:3d}px glyphs={n:3d} ascent={asc} descent={desc} bytes={len(data)}')
        parts.append(c_array(name, data))
    open(os.path.join(OUT, 'fonts.h'), 'w').write('\n'.join(parts))
    imgs = ['// Generated by gen_assets.py: bóbr images, RGB565 on the screen background\n#pragma once\n#include <Arduino.h>\n']
    for name, h, rgb, facing in [('bobr_dim', 54, (110, 84, 60), 'left'), ('bobr_hot', 62, (181, 124, 72), 'left'),
                                 ('bobr_big', 120, (181, 124, 72), 'left')]:
        W, H, words = bobr_img(h, rgb, BG, facing)
        print(f'{name}: {W}x{H}')
        imgs.append(c_words(name, W, H, words))
    open(os.path.join(OUT, 'bobr.h'), 'w').write('\n'.join(imgs))

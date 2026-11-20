#!/usr/bin/env python3
"""Draws the built-in sleep image, images/splash.png: the whole screen a 50%
dither, and in the middle of what the date/time bar leaves (the top 20 rows
are the bar's) the PX-42F logo, images/px42f_logo.png (white on black),
scaled to fit, its black round the white frame left out, with a drop shadow.

    python3 images/make_splash.py
    python3 images/gen_image.py images/splash.png splash_image

No dependencies: the logo is read with fonts/gen_font.py's decoder, the PNG
written with zlib (8-bit gray).
"""
import os
import struct
import sys
import zlib

sys.dont_write_bytecode = True  # no __pycache__ in the source folders
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'fonts'))
from gen_font import decode_png  # noqa: E402

W, H = 250, 122
BAR = 20          # splash.c's BAR_HEIGHT: the date/time bar covers these rows
LOGO_H = 92       # the logo's white part, scaled to this height
SHADOW = 4        # solid black, down and right


def load_logo():
    """The logo as gray rows (0-255), cropped to its white part."""
    w, h, bpp, _, raster = decode_png(os.path.join(HERE, 'px42f_logo.png'))
    gray = [[sum(raster[(y * w + x) * bpp:(y * w + x) * bpp + min(bpp, 3)]) // min(bpp, 3)
             for x in range(w)] for y in range(h)]
    xs = [x for row in gray for x, v in enumerate(row) if v > 128]
    ys = [y for y, row in enumerate(gray) if any(v > 128 for v in row)]
    return [row[min(xs):max(xs) + 1] for row in gray[min(ys):max(ys) + 1]]


def scale(gray, out_w, out_h):
    """Box filter: each output pixel the average of the source area it covers."""
    h, w = len(gray), len(gray[0])
    out = []
    for oy in range(out_h):
        y0, y1 = oy * h / out_h, (oy + 1) * h / out_h
        row = []
        for ox in range(out_w):
            x0, x1 = ox * w / out_w, (ox + 1) * w / out_w
            total = area = 0.0
            for y in range(int(y0), min(h, int(y1 + 0.9999))):
                fy = min(y + 1, y1) - max(y, y0)
                for x in range(int(x0), min(w, int(x1 + 0.9999))):
                    fx = min(x + 1, x1) - max(x, x0)
                    total += gray[y][x] * fx * fy
                    area += fx * fy
            row.append(total / area)
        out.append(row)
    return out


def main():
    ink = [[(x + y) & 1 == 0 for x in range(W)] for y in range(H)]  # 50%

    src = load_logo()
    lw = round(len(src[0]) * LOGO_H / len(src))
    logo = scale(src, lw, LOGO_H)

    # The logo's own black outside its white frame is the background: what
    # black reaches from the edge is left out, the dither shows there.
    h, w = LOGO_H + 2, lw + 2                     # a pixel of room round it
    white = [[0 < x < w - 1 and 0 < y < h - 1 and logo[y - 1][x - 1] >= 128
              for x in range(w)] for y in range(h)]
    outside = [[False] * w for _ in range(h)]
    stack = [(0, 0)]
    while stack:
        x, y = stack.pop()
        if 0 <= x < w and 0 <= y < h and not outside[y][x] and not white[y][x]:
            outside[y][x] = True
            stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]

    ox = (W - w - SHADOW) // 2
    oy = BAR + (H - BAR - h - SHADOW) // 2
    for y in range(h):                            # the shadow first
        for x in range(w):
            if not outside[y][x]:
                ink[oy + y + SHADOW][ox + x + SHADOW] = True
    for y in range(h):
        for x in range(w):
            if not outside[y][x]:
                ink[oy + y][ox + x] = not white[y][x]

    raw = b''.join(b'\0' + bytes(0 if p else 255 for p in row) for row in ink)
    png = b'\x89PNG\r\n\x1a\n'
    for kind, data in ((b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 0, 0, 0, 0)),
                       (b'IDAT', zlib.compress(raw, 9)), (b'IEND', b'')):
        png += struct.pack('>I', len(data)) + kind + data
        png += struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    out = os.path.join(HERE, 'splash.png')
    with open(out, 'wb') as f:
        f.write(png)
    print(f'wrote {out} (logo {lw}x{LOGO_H})')


if __name__ == '__main__':
    main()

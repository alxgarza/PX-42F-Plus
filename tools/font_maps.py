#!/usr/bin/env python3
"""Draws a map of each calculator font as a PNG: the 128 characters in a
16 x 8 grid, the code of each row (0x00, 0x10, ...) and column (0-F) on the
edges, each pixel drawn SCALE x SCALE.

  python3 tools/font_maps.py [OUT_DIR]     (default build/font_maps)

Fonts: the built-in fonts/font_*_data.c (display_font_t, MSB-first rows,
packed back to back); the mini one is Plus42's own characters
(fonts/gen_font_6x8.py).
"""
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCALE = 3
PAD = 2  # pixels around each glyph, in font pixels


def c_font(name):
    """(width, height, glyphs) from fonts/<name>_data.c; glyphs[code] is a
    list of rows of 0/1."""
    text = open(os.path.join(ROOT, 'fonts', name + '_data.c')).read()
    width = int(re.search(r'\.width\s*=\s*(\d+)', text).group(1))
    height = int(re.search(r'\.height\s*=\s*(\d+)', text).group(1))
    per = int(re.search(r'\.bytes_per_char\s*=\s*(\d+)', text).group(1))
    body = text[text.index('{', text.index('_bits[]')):text.index('};')]
    data = [int(b, 16) for b in re.findall(r'0x([0-9a-fA-F]{2})', body)]
    glyphs = []
    for code in range(len(data) // per):
        bits = [(data[code * per + i // 8] >> (7 - i % 8)) & 1 for i in range(per * 8)]
        glyphs.append([bits[r * width:(r + 1) * width] for r in range(height)])
    return width, height, glyphs


def write_png(path, pixels):
    """pixels: rows of gray levels 0-255."""
    h, w = len(pixels), len(pixels[0])
    raw = b''.join(b'\x00' + bytes(row) for row in pixels)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def font_map(path, font, label_font):
    width, height, glyphs = font
    lw, lh, lglyphs = label_font
    cell_w, cell_h = (width + 2 * PAD) * SCALE, (height + 2 * PAD) * SCALE
    margin_x, margin_y = 4 * lw * SCALE // 2 + 8, lh * SCALE // 2 + 8
    W, H = margin_x + 16 * cell_w + 1, margin_y + 8 * cell_h + 1
    img = [[255] * W for _ in range(H)]

    def label(s, x, y):  # half-scale label text
        for ch in s:
            g = lglyphs[ord(ch)]
            for r in range(lh):
                for c in range(lw):
                    if g[r][c]:
                        for dy in range(SCALE // 2 + 1):
                            for dx in range(SCALE // 2 + 1):
                                yy, xx = y + r * (SCALE // 2 + 1) + dy, x + c * (SCALE // 2 + 1) + dx
                                if yy < H and xx < W:
                                    img[yy][xx] = 0
            x += lw * (SCALE // 2 + 1)

    for col in range(16):
        label('%X' % col, margin_x + col * cell_w + cell_w // 2 - lw, 2)
    for row in range(8):
        label('%02X' % (row * 16), 2, margin_y + row * cell_h + cell_h // 2 - lh)
    for i in range(17):  # grid
        for y in range(margin_y, H):
            img[y][margin_x + i * cell_w if i < 16 else W - 1] = 170
    for i in range(9):
        for x in range(margin_x, W):
            img[margin_y + i * cell_h if i < 8 else H - 1][x] = 170
    for code, g in enumerate(glyphs[:128]):
        x0 = margin_x + (code % 16) * cell_w + PAD * SCALE
        y0 = margin_y + (code // 16) * cell_h + PAD * SCALE
        for r in range(height):
            for c in range(width):
                v = 0 if g[r][c] else 235
                for dy in range(SCALE):
                    for dx in range(SCALE):
                        img[y0 + r * SCALE + dy][x0 + c * SCALE + dx] = v
    write_png(path, img)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'font_maps')
    os.makedirs(out, exist_ok=True)
    label_font = c_font('font_8x12')
    fonts = [('mini_6x8', c_font('font_6x8')),
             ('small_8x12', c_font('font_8x12')),
             ('large_10x16', c_font('font_10x16'))]
    for name, font in fonts:
        path = os.path.join(out, name + '.png')
        font_map(path, font, label_font)
        print(path)


if __name__ == '__main__':
    main()

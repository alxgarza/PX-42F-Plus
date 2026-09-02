#!/usr/bin/env python3
"""Generates a continuous-bit-packed C font table from a PNG glyph sheet.

The sheet is a grid of monospaced cells, one glyph per cell, in row-major
ASCII order (cell (col, row) = code point row*cols + col). Cells are
separated by `--gap` pixels of spacing (no border padding assumed).

Only the PNG features this project's font sheets actually use are
supported: 8-bit depth, color type 0/2/4/6 (gray, RGB, gray+alpha, RGBA),
no interlacing. That covers ordinary image-editor PNG exports without
pulling in a PNG library dependency for what is otherwise a small build
tool.

Each glyph is packed MSB-first, one bit per pixel, row-major within the
glyph, with no padding between rows or glyphs -- ceil(width*height/8)
bytes per character. This is the format `display_font_t` in font.h
expects: a plain bitmap, decoupled from any display controller's memory
layout.
"""
import argparse
import struct
import sys
import zlib


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    elif pb <= pc:
        return b
    return c


def decode_png(path):
    """Returns (width, height, bytes_per_pixel, pixel_bytes) with pixel_bytes
    being the unfiltered, non-interlaced raster: height rows of
    width*bytes_per_pixel bytes each, per PNG's own channel layout."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG file')

    pos = 8
    idat = bytearray()
    w = h = bit_depth = color_type = None
    while pos < len(data):
        length, = struct.unpack('>I', data[pos:pos + 4])
        chunk_type = data[pos + 4:pos + 8]
        chunk_data = data[pos + 8:pos + 8 + length]
        if chunk_type == b'IHDR':
            w, h, bit_depth, color_type, compression, filter_method, interlace = \
                struct.unpack('>IIBBBBB', chunk_data)
            if compression != 0 or filter_method != 0:
                raise ValueError(f'{path}: unsupported PNG compression/filter method')
            if interlace != 0:
                raise ValueError(f'{path}: interlaced PNGs are not supported')
            if bit_depth != 8:
                raise ValueError(f'{path}: only 8-bit depth is supported, got {bit_depth}')
        elif chunk_type == b'IDAT':
            idat += chunk_data
        elif chunk_type == b'IEND':
            break
        pos += 8 + length + 4

    bpp_by_color_type = {0: 1, 2: 3, 4: 2, 6: 4}
    if color_type not in bpp_by_color_type:
        raise ValueError(f'{path}: unsupported PNG color type {color_type} (palette images are not supported)')
    bpp = bpp_by_color_type[color_type]

    raw = zlib.decompress(bytes(idat))
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    idx = 0
    for y in range(h):
        ftype = raw[idx]
        idx += 1
        line = bytearray(raw[idx:idx + stride])
        idx += stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif ftype == 4:
                line[i] = (line[i] + _paeth(a, b, c)) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line

    return w, h, bpp, color_type, bytes(out)


def pixel_is_ink(pixel_bytes, bpp, color_type, gray_threshold=128, alpha_threshold=128):
    """True = glyph ink (drawn), False = background."""
    if color_type == 0:  # gray
        gray = pixel_bytes[0]
        alpha = 255
    elif color_type == 2:  # RGB
        gray = sum(pixel_bytes[0:3]) // 3
        alpha = 255
    elif color_type == 4:  # gray+alpha
        gray, alpha = pixel_bytes[0], pixel_bytes[1]
    else:  # RGBA
        gray = sum(pixel_bytes[0:3]) // 3
        alpha = pixel_bytes[3]
    return alpha >= alpha_threshold and gray < gray_threshold


def extract_glyph_bits(raster, img_w, bpp, color_type, cell_w, cell_h, cols, x0, y0):
    """Returns a list of cell_h strings ('#'/'.') and the packed bit list
    (row-major, top-to-bottom, left-to-right) for one glyph cell."""
    stride = img_w * bpp
    bits = []
    rows_ascii = []
    for row in range(cell_h):
        y = y0 + row
        line = ''
        for col in range(cell_w):
            x = x0 + col
            off = y * stride + x * bpp
            ink = pixel_is_ink(raster[off:off + bpp], bpp, color_type)
            bits.append(1 if ink else 0)
            line += '#' if ink else '.'
        rows_ascii.append(line)
    return bits, rows_ascii


def pack_bits_msb_first(bits):
    out = bytearray((len(bits) + 7) // 8)
    for i, bit in enumerate(bits):
        if bit:
            out[i // 8] |= 0x80 >> (i % 8)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('png', help='Source glyph sheet PNG')
    ap.add_argument('--cell-w', type=int, required=True, help='Glyph width in pixels')
    ap.add_argument('--cell-h', type=int, required=True, help='Glyph height in pixels')
    ap.add_argument('--cols', type=int, required=True, help='Glyphs per row in the sheet')
    ap.add_argument('--rows', type=int, required=True, help='Rows of glyphs in the sheet')
    ap.add_argument('--gap', type=int, default=1, help='Pixels of spacing between cells (default: 1)')
    ap.add_argument('--advance', type=int, default=None,
                     help='Pixel-columns to move right after drawing a glyph (default: cell-w + 1). '
                          'Use cell-w (no extra gap) if the glyphs already reserve blank columns '
                          'for spacing, as font_8x12 does -- otherwise characters get double-spaced.')
    ap.add_argument('--first-char', type=int, default=0, help='Code point of the sheet\'s first cell (default: 0)')
    ap.add_argument('--name', required=True, help='C identifier base, e.g. font_10x16')
    ap.add_argument('--out', required=True, help='Output .c file path')
    ap.add_argument('--preview', action='store_true', help='Print an ASCII-art preview of every glyph to stderr')
    args = ap.parse_args()

    img_w, img_h, bpp, color_type, raster = decode_png(args.png)

    expected_w = args.cols * args.cell_w + (args.cols - 1) * args.gap
    expected_h = args.rows * args.cell_h + (args.rows - 1) * args.gap
    if (img_w, img_h) != (expected_w, expected_h):
        sys.exit(f'error: {args.png} is {img_w}x{img_h}px, but --cols/--cell-w/--gap etc. '
                  f'imply {expected_w}x{expected_h}px -- check the grid geometry')

    num_glyphs = args.cols * args.rows
    bytes_per_char = (args.cell_w * args.cell_h + 7) // 8

    packed_glyphs = []
    for code in range(num_glyphs):
        col = code % args.cols
        row = code // args.cols
        x0 = col * (args.cell_w + args.gap)
        y0 = row * (args.cell_h + args.gap)
        bits, ascii_rows = extract_glyph_bits(raster, img_w, bpp, color_type,
                                               args.cell_w, args.cell_h, args.cols, x0, y0)
        packed_glyphs.append(pack_bits_msb_first(bits))
        if args.preview:
            print(f'-- code {args.first_char + code} (0x{args.first_char + code:02X}) --', file=sys.stderr)
            for line in ascii_rows:
                print(line, file=sys.stderr)

    last_char = args.first_char + num_glyphs - 1
    advance = args.advance if args.advance is not None else args.cell_w + 1

    with open(args.out, 'w') as f:
        f.write(f'/* Generated by fonts/gen_font.py from {args.png} -- do not edit by hand. */\n')
        f.write(f'#include "font.h"\n')
        f.write(f'#include "{args.name}.h"\n\n')
        f.write(f'static const uint8_t {args.name}_bits[] = {{\n')
        for code, glyph in enumerate(packed_glyphs):
            hex_bytes = ', '.join(f'0x{b:02X}' for b in glyph)
            f.write(f'    /* {args.first_char + code:3d} */ {hex_bytes},\n')
        f.write('};\n\n')
        f.write(f'const display_font_t {args.name} = {{\n')
        f.write(f'    .width = {args.cell_w},\n')
        f.write(f'    .height = {args.cell_h},\n')
        f.write(f'    .advance = {advance},\n')
        f.write(f'    .first_char = {args.first_char},\n')
        f.write(f'    .last_char = {last_char},\n')
        f.write(f'    .bytes_per_char = {bytes_per_char},\n')
        f.write(f'    .data = {args.name}_bits,\n')
        f.write(f'}};\n')

    print(f'{args.out}: {num_glyphs} glyphs, {bytes_per_char} bytes/char, '
          f'{bytes_per_char * num_glyphs} bytes total (codes {args.first_char}-{last_char}, '
          f'advance {advance}px)')


if __name__ == '__main__':
    main()

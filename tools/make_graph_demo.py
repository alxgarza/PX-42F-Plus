#!/usr/bin/env python3
"""Writes demos/PXGRAPH.raw, a Plus42 program showing the PX's graphics, from
Plus42's own command table, so its codes can't be wrong.

    tools/make_graph_demo.py [--preview DIR]

--preview decodes the bytes written, runs them on a small RPN machine against the
real px_draw.c and px_layer.c, and saves each page as a PNG: a look at the program
without the calculator. It proves the codes and the drawing, not the firmware's
commands, whose stand-ins here are a few lines each.
"""
import ctypes
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(ROOT, 'demos', 'PXGRAPH.raw')

# Program lines. A number or a "string" is itself; 'NAME n' takes a number or a
# "variable"; the rest are command names, as core_tables.cc's comments call them.
def pen(width, black):
    return [width, black, 'PEN']

def wait():
    return ['XEQ 14']

def shape(cmd, a, b, c, d):
    return [a, b, c, d, cmd]

def clear():
    return ['CLLCD']

PROGRAM = (
    ['LBL "PXGRAPH"', 'DEG', 'CF 34', 'CF 35']
    # 1: the whole panel, by WIDTH and HEIGHT; the status bar is gone.
    + clear() + pen(1, 1)
    + [1, 1, 'WIDTH', 'HEIGHT', 'BOX']
    + [1, 1, 'WIDTH', 'HEIGHT', 'DLINE']
    + [1, 'HEIGHT', 'WIDTH', 1, 'DLINE']
    + wait()
    # 2: a star of lines, then lines of each width.
    + clear() + pen(1, 1)
    + [0, 'STO 00', 'LBL 02',
       'RCL 00', 'COS', 56, 'MUL', 70, 'ADD', 'STO 01',
       'RCL 00', 'SIN', 56, 'MUL', 62, 'ADD', 'STO 02',
       70, 62, 'RCL 01', 'RCL 02', 'DLINE',
       15, 'STO+ 00', 'RCL 00', 360, 'X>Y?', 'GTO 02']
    + pen(1, 1) + shape('DLINE', 140, 20, 240, 20)
    + pen(2, 1) + shape('DLINE', 140, 40, 240, 40)
    + pen(4, 1) + shape('DLINE', 140, 62, 240, 62)
    + pen(8, 1) + shape('DLINE', 140, 84, 240, 84)
    + pen(16, 1) + shape('DLINE', 150, 106, 230, 106)
    + wait()
    # 3: boxes: outlines of 1, 2 and 4 dots, filled, and white inside.
    + clear()
    + pen(1, 1) + shape('BOX', 10, 10, 70, 45)
    + pen(2, 1) + shape('BOX', 90, 10, 70, 45)
    + pen(4, 1) + shape('BOX', 170, 10, 70, 45)
    + pen(0, 1) + shape('BOX', 10, 65, 110, 50)
    + pen(0, 0) + shape('BOX', 35, 78, 60, 24)
    + pen(0, 1) + shape('BOX', 130, 65, 110, 50)
    + pen(3, 0) + shape('BOX', 142, 73, 86, 34)
    + wait()
    # 4: ellipses: circles of each width, filled, white inside.
    + clear()
    + pen(1, 1) + shape('ELLIPSE', 42, 32, 28, 28)
    + pen(3, 1) + shape('ELLIPSE', 125, 32, 28, 28)
    + pen(7, 1) + shape('ELLIPSE', 208, 32, 28, 28)
    + pen(0, 1) + shape('ELLIPSE', 68, 92, 60, 26)
    + pen(0, 0) + shape('ELLIPSE', 68, 92, 30, 12)
    + pen(2, 1) + shape('ELLIPSE', 190, 92, 50, 20)
    + wait()
    # 5: polygons from matrices: a filled star, with its hole, and an outline.
    + clear() + pen(0, 1)
    + [5, 'STO 03', 144, 'STO 04', 66, 'STO 05', 'XEQ 13']
    + pen(2, 1)
    + [6, 'STO 03', 60, 'STO 04', 186, 'STO 05', 'XEQ 13']
    + wait()
    # 6: PIXEL: an axis across and one down, and a sine curve 1 dot at a time.
    + clear()
    + [-62, 1, 'PIXEL', 1, -126, 'PIXEL']
    + [1, 'STO 00', 'LBL 03',
       'RCL 00', 720, 'MUL', 'WIDTH', 'DIV', 'SIN', 50, 'MUL', 62, 'SUB', 'CHS',
       'RCL 00', 'PIXEL',
       1, 'STO+ 00', 'RCL 00', 'WIDTH', 'X<Y?', 'GTO 04', 'GTO 03', 'LBL 04']
    + wait()
    # 7: all of them lined up: a box and a pixel just off each corner (x 50-149,
    # y 20-69), then AGRAPH's four modes over black and white.
    + clear() + pen(1, 1) + shape('BOX', 50, 20, 100, 50)
    + [19, 49, 'PIXEL', 19, 150, 'PIXEL', 70, 49, 'PIXEL', 70, 150, 'PIXEL']
    + pen(0, 1) + shape('BOX', 160, 10, 80, 12)
    # Starts with a blank column: a string starting with 0x7F would append to ALPHA.
    + ['"\x00\x7f\x09\x09\x06\x00\x63\x14\x08\x14\x63"']
    + ['CF 34', 'CF 35', 80, 170, 'AGRAPH']
    + ['SF 35', 96, 170, 'AGRAPH']
    + ['SF 34', 'CF 35', 12, 180, 'AGRAPH']
    + ['SF 35', 12, 210, 'AGRAPH', 112, 170, 'AGRAPH']
    + ['CF 34', 'CF 35']
    + wait()
    # 8: a white pen erasing: a ball hops across, a second between hops.
    + clear()
    + [20, 'STO 00', 'LBL 05']
    + pen(0, 1) + ['RCL 00', 62, 14, 14, 'ELLIPSE', 'PSE']
    + pen(0, 0) + ['RCL 00', 62, 14, 14, 'ELLIPSE']
    + [30, 'STO+ 00', 'RCL 00', 240, 'X>Y?', 'GTO 05']
    # The end, also from EXIT: CLD takes the drawing away, so the stack comes back.
    + ['LBL 12'] + pen(1, 1) + ['CF 34', 'CF 35', 'CLD', 'STOP']
    # Waits for a key. EXIT ends the program.
    + ['LBL 14', 'GETKEY', 33, 'X=Y?', 'GTO 12', 'RTN']
    # A polygon of R03 corners, R04 degrees apart, around x R05, y 62.
    + ['LBL 13', 'RCL 03', 2, 'NEWMAT', 'STO "PG"', 'INDEX "PG"',
       'RCL 03', 'STO 06', -90, 'STO 07',
       'LBL 06',
       'RCL 07', 'COS', 50, 'MUL', 'RCL 05', 'ADD', 'STOEL', 'J+',
       'RCL 07', 'SIN', 50, 'MUL', 62, 'ADD', 'STOEL', 'J+',
       'RCL 04', 'STO+ 07',
       'DSE 06', 'GTO 06',
       'RCL "PG"', 'POLYGON', 'CLV "PG"', 'RTN', 'END']
)

def read_table():
    """Each command's (scode, code1, code2), by the name in core_tables.cc's comment."""
    table = {}
    with open(os.path.join(ROOT, 'src/core/core_tables.cc'), encoding='latin-1') as f:
        for m in re.finditer(r'/\* (\w+) \*/\s+docmd_\w+,\s+"[^"]*",\s+0x(\w\w), 0x(\w\w), 0x(\w\w), 0x(\w\w),', f.read()):
            table[m.group(1)] = (int(m.group(3), 16), int(m.group(4), 16), int(m.group(5), 16))
    return table


NAMES = {'J+': 'J_ADD', 'X=Y?': 'X_EQ_Y', 'X>Y?': 'X_GT_Y', 'X<Y?': 'X_LT_Y', 'STO+': 'STO_ADD'}


def number_bytes(n):
    return bytes((0x1c if c == '-' else 0x10 + int(c)) for c in str(n)) + b'\0'


def encode_line(line, table):
    if isinstance(line, int):
        return number_bytes(line)
    if line.startswith('"'):
        text = line[1:-1].encode('latin-1')
        return bytes([0xf0 + len(text)]) + text
    if line == 'END':
        return bytes([0xc0, 0, 0x0d])
    parts = line.split(' ', 1)
    name = NAMES.get(parts[0], parts[0])
    if len(parts) == 1:
        scode, code1, code2 = table[name]
        return bytes([code1, code2]) if code1 else bytes([code2])
    arg = parts[1]
    if parts[0] == 'LBL':
        if arg.startswith('"'):
            text = arg[1:-1].encode('ascii')
            return bytes([0xc0, 0, 0xf1 + len(text), 0]) + text
        return bytes([1 + int(arg)])
    if parts[0] == 'GTO':
        return bytes([0xb1 + int(arg), 0])
    if parts[0] == 'XEQ':
        return bytes([0xe0, 0, int(arg)])
    scode, code1, code2 = table[name]
    if arg.startswith('"'):
        text = arg[1:-1].encode('ascii')
        return bytes([0xf1 + len(text), scode]) + text
    n = int(arg)
    if parts[0] == 'RCL' and n <= 15:
        return bytes([0x20 + n])
    if parts[0] == 'STO' and n <= 15:
        return bytes([0x30 + n])
    return bytes([code2, n])


def encode(table):
    code = b''.join(encode_line(l, table) for l in PROGRAM)
    assert PROGRAM[-1] == 'END'
    return code


def decode(code, table):
    """The bytes back to lines, by the table alone."""
    by_two = {(c1, c2): name for name, (s, c1, c2) in table.items() if c1}
    by_one = {c2: name for name, (s, c1, c2) in table.items() if not c1 and c2}
    by_scode = {s: name for name, (s, c1, c2) in table.items() if s}
    back = {v: k for k, v in NAMES.items()}
    lines, i, labels = [], 0, {}
    while i < len(code):
        b = code[i]
        if 1 <= b <= 0x0f:
            labels[b - 1] = len(lines)
            lines.append(('LBL', b - 1))
            i += 1
        elif 0x10 <= b <= 0x1c:
            j = code.index(0, i)
            text = ''.join('-' if c == 0x1c else str(c - 0x10) for c in code[i:j])
            lines.append(int(text))
            i = j + 1
        elif 0x20 <= b <= 0x2f:
            lines.append(('RCL', b - 0x20))
            i += 1
        elif 0x30 <= b <= 0x3f:
            lines.append(('STO', b - 0x30))
            i += 1
        elif b == 0xc0:
            assert code[i + 1] == 0
            if code[i + 2] == 0x0d:
                lines.append(('END',))
                i += 3
            else:
                n = code[i + 2] - 0xf1
                assert code[i + 3] == 0
                lines.append(('LBL', code[i + 4:i + 4 + n].decode()))
                i += 4 + n
        elif 0xb1 <= b <= 0xbf:
            assert code[i + 1] == 0
            lines.append(('GTO', b - 0xb1))
            i += 2
        elif b == 0xe0:
            assert code[i + 1] == 0
            lines.append(('XEQ', code[i + 2]))
            i += 3
        elif 0xf0 <= b:
            n = b - 0xf0
            if code[i + 1] in by_scode and code[i + 1] >= 0x80 and n >= 1 and by_scode[code[i + 1]] in ('STO', 'RCL', 'INDEX', 'CLV'):
                lines.append((by_scode[code[i + 1]], code[i + 2:i + 1 + n].decode()))
            else:
                lines.append(('STR', code[i + 1:i + 1 + n]))
            i += 1 + n
        elif (b, code[i + 1] if i + 1 < len(code) else -1) in by_two:
            lines.append((by_two[(b, code[i + 1])],))
            i += 2
        elif b in (0x92, 0x96, 0x97, 0xa8, 0xa9):
            lines.append((by_one[b], code[i + 1]))
            i += 2
        else:
            lines.append((by_one[b],))
            i += 1
    return lines, labels


class Canvas(ctypes.Structure):
    _fields_ = [('bits', ctypes.c_void_p), ('w', ctypes.c_int), ('h', ctypes.c_int), ('stride', ctypes.c_int)]


class Point(ctypes.Structure):
    _fields_ = [('x', ctypes.c_int16), ('y', ctypes.c_int16)]


def draw_library(tmp):
    lib = os.path.join(tmp, 'draw.so')
    subprocess.check_call(['cc', '-shared', '-fPIC', '-I' + ROOT, os.path.join(ROOT, 'px_draw.c'),
                           os.path.join(ROOT, 'px_layer.c'), '-o', lib])
    return ctypes.CDLL(lib)


def save_png(path, bits, w, h, stride):
    rows = b''
    for y in range(h):
        row = bits[y * stride:(y + 1) * stride]
        rows += b'\0' + bytes(b ^ 0xff for b in row)
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 1, 0, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def preview(lines, labels, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    W, H, ST = 250, 122, 32
    with tempfile.TemporaryDirectory() as tmp:
        lib = draw_library(tmp)
        size = H * ST
        ink_b, wipe_b = ctypes.create_string_buffer(size), ctypes.create_string_buffer(size)
        ink = Canvas(ctypes.addressof(ink_b), W, H, ST)
        wipe = Canvas(ctypes.addressof(wipe_b), W, H, ST)
        ref = ctypes.byref

        def draw_both(fn, *args, width, black):
            fn(ref(ink), *args, width, black)
            fn(ref(wipe), *args, width, 0 if black else 1)

        s = [0.0, 0.0, 0.0, 0.0]
        def push(v):
            s.insert(0, v)
            s.pop()
        def pop2(v):
            s[:] = [v, s[2], s[3], s[3]]
        regs, variables, alpha, ij = {}, {}, b'', [None, 0]
        pen_width, pen_black = 1, 1
        flags = {34: 0, 35: 0}
        pc, calls, page, steps = 0, [], 1, 0

        def snapshot():
            nonlocal page
            picture = ctypes.create_string_buffer(size)
            lib.px_layer_compose(picture, ref(ink), ref(wipe))
            save_png(os.path.join(out_dir, 'page%d.png' % page), picture.raw, W, H, ST)
            page += 1

        def rnd(v):
            return int(math.floor(abs(v) + 0.5)) * (1 if v >= 0 else -1)

        while pc < len(lines):
            steps += 1
            assert steps < 200000, 'runaway'
            line = lines[pc]
            pc += 1
            if isinstance(line, int):
                push(float(line))
                continue
            op = line[0]
            if op in ('LBL', 'DEG'):
                pass
            elif op == 'STR':
                alpha = alpha + line[1][1:] if line[1][:1] == b'\x7f' else line[1]
            elif op == 'CF':
                flags[line[1]] = 0
            elif op == 'SF':
                flags[line[1]] = 1
            elif op == 'CLD':
                ctypes.memset(ink_b, 0, size)
                ctypes.memset(wipe_b, 0, size)
            elif op == 'CLLCD':
                ctypes.memset(ink_b, 0, size)
                ctypes.memset(wipe_b, 0, size)
            elif op == 'PEN':
                assert s[0] in (0, 1) and 0 <= s[1] <= 64
                pen_black, pen_width = int(s[0]), int(s[1])
            elif op in ('BOX', 'DLINE', 'ELLIPSE'):
                t, z, y, x = [rnd(v) for v in (s[3], s[2], s[1], s[0])]
                if op == 'BOX':
                    draw_both(lib.px_draw_box, t - 1, z - 1, y, x, width=pen_width, black=pen_black)
                elif op == 'DLINE':
                    draw_both(lib.px_draw_line, t - 1, z - 1, y - 1, x - 1, width=pen_width, black=pen_black)
                else:
                    draw_both(lib.px_draw_ellipse, t - 1, z - 1, y, x, width=pen_width, black=pen_black)
            elif op == 'POLYGON':
                m = s[0]
                assert isinstance(m, list) and len(m[0]) == 2 and 1 <= len(m) <= 64
                pts = (Point * len(m))(*[Point(rnd(r[0]) - 1, rnd(r[1]) - 1) for r in m])
                draw_both(lib.px_draw_polygon, pts, len(m), width=pen_width, black=pen_black)
            elif op == 'PIXEL':
                x, y = rnd(s[0]), rnd(s[1])
                if x < 0:
                    for i in range(H):
                        lib.px_layer_dot(ref(ink), ref(wipe), -x - 1, i, 1)
                if y < 0:
                    for i in range(W):
                        lib.px_layer_dot(ref(ink), ref(wipe), i, -y - 1, 1)
                if x > 0 and y > 0:
                    lib.px_layer_dot(ref(ink), ref(wipe), x - 1, y - 1, 1)
            elif op == 'AGRAPH':
                mode = flags[34] * 2 + flags[35]
                lib.px_layer_pattern(ref(ink), ref(wipe), rnd(s[0]) - 1, rnd(s[1]) - 1, alpha, len(alpha), mode)
            elif op == 'WIDTH':
                push(float(W))
            elif op == 'HEIGHT':
                push(float(H))
            elif op == 'ADD':
                pop2(s[1] + s[0])
            elif op == 'SUB':
                pop2(s[1] - s[0])
            elif op == 'MUL':
                pop2(s[1] * s[0])
            elif op == 'DIV':
                pop2(s[1] / s[0])
            elif op == 'CHS':
                s[0] = -s[0]
            elif op == 'SIN':
                s[0] = math.sin(math.radians(s[0]))
            elif op == 'COS':
                s[0] = math.cos(math.radians(s[0]))
            elif op == 'STO':
                if isinstance(line[1], str):
                    variables[line[1]] = s[0]
                else:
                    regs[line[1]] = s[0]
            elif op == 'RCL':
                push(variables[line[1]] if isinstance(line[1], str) else regs[line[1]])
            elif op == 'STO_ADD':
                regs[line[1]] += s[0]
            elif op == 'NEWMAT':
                rows, cols = int(s[1]), int(s[0])
                s[:] = [[[0.0] * cols for _ in range(rows)], s[2], s[3], s[3]]
            elif op == 'INDEX':
                ij = [line[1], 0]
            elif op == 'STOEL':
                m = variables[ij[0]]
                m[ij[1] // 2][ij[1] % 2] = s[0]
            elif op == 'J_ADD':
                ij[1] += 1
            elif op == 'CLV':
                del variables[line[1]]
            elif op == 'DSE':
                regs[line[1]] -= 1
                if regs[line[1]] <= 0:
                    pc += 1
            elif op == 'X_GT_Y':
                if not s[0] > s[1]:
                    pc += 1
            elif op == 'X_LT_Y':
                if not s[0] < s[1]:
                    pc += 1
            elif op == 'X_EQ_Y':
                if s[0] != s[1]:
                    pc += 1
            elif op == 'XEQ':
                calls.append(pc)
                pc = labels[line[1]]
            elif op == 'GTO':
                pc = labels[line[1]]
            elif op == 'RTN':
                pc = calls.pop()
            elif op == 'GETKEY':
                snapshot()
                push(13.0)
            elif op == 'PSE':
                if abs(regs[0] - 110) < 1:
                    snapshot()
            elif op == 'STOP':
                break
            else:
                raise SystemExit('the machine has no %r' % (line,))
        print('%d pages, %d steps' % (page - 1, steps))


def main():
    table = read_table()
    code = encode(table)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'wb') as f:
        f.write(code)
    lines, labels = decode(code, table)
    print('%s: %d bytes, %d lines' % (os.path.relpath(OUT), len(code), len(lines)))
    if '--preview' in sys.argv:
        preview(lines, labels, sys.argv[sys.argv.index('--preview') + 1])


if __name__ == '__main__':
    main()

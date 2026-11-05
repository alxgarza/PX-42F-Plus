#!/usr/bin/env python3
"""Writes src/core/core_tables.{h,cc}: Plus42's own, plus the PX's commands.

Run it after dropping in a new Plus42, instead of merging those two files, and
after changing COMMANDS. They are appended, so Plus42's CMD_* numbers don't
move; the numbers mean nothing outside the firmware anyway, programs being
stored in "raw" form even in state files (Thomas Okken).

    tools/px_core_tables.py           write them
    tools/px_core_tables.py --check   fail if they're out of date

Stops if Plus42 has taken one of the names or codes: its new command wins the
code, so pick another. The PX's own commands go on XROM 0 (a001-a03f).
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# comment, then (name, handler, XROM code, stack arguments, argument types).
# The commands take no parameter, as XROM-coded ones must: a name comes from ALPHA.
COMMANDS = [
    ('HP-41 Time Module, PX-42 addition (XROM 26,22 and 26,23)', [
        ('SETDATE', 'docmd_setdate', 0xa696, 1, '0x01'),
        ('SETIME', 'docmd_setime', 0xa697, 1, '0x01'),
    ]),
    ('HP-41 Time Module, XROM 26 (px_commands.cc)', [
        ('CLKT', 'docmd_clkt', 0xa688, 0, 'NA_T'),
        ('CLKTD', 'docmd_clktd', 0xa689, 0, 'NA_T'),
        ('CLOCK', 'docmd_clock', 0xa68a, 0, 'NA_T'),
        ('CORRECT', 'docmd_correct', 0xa68b, 1, '0x01'),
        ('RCLAF', 'docmd_rclaf', 0xa692, 0, 'NA_T'),
        ('SETAF', 'docmd_setaf', 0xa695, 1, '0x01'),
        ('T+X', 'docmd_t_plus_x', 0xa69b, 1, '0x01'),
        ('ALMCAT', 'docmd_almcat', 0xa682, 0, 'NA_T'),
        ('ALMNOW', 'docmd_almnow', 0xa683, 0, 'NA_T'),
        ('XYZALM', 'docmd_xyzalm', 0xa69d, 3, '0x01'),
        ('CLALMA', 'docmd_clalma', 0xa69f, 0, 'NA_T'),
        ('CLALMX', 'docmd_clalmx', 0xa6a0, 1, '0x01'),
        ('CLRALMS', 'docmd_clralms', 0xa6a1, 0, 'NA_T'),
        ('RCLALM', 'docmd_rclalm', 0xa6a2, 1, '0x01'),
    ]),
    ('HP-41 Time Module, the stopwatch (px_commands.cc)', [
        ('RCLSW', 'docmd_rclsw', 0xa693, 0, 'NA_T'),
        ('RUNSW', 'docmd_runsw', 0xa694, 0, 'NA_T'),
        ('SETSW', 'docmd_setsw', 0xa698, 1, '0x01'),
        ('STOPSW', 'docmd_stopsw', 0xa699, 0, 'NA_T'),
        ('SW', 'docmd_sw', 0xa69a, 0, 'NA_T'),
        ('SWPT', 'docmd_swpt', 0xa6a3, 1, '0x01'),
    ]),
    # Not the Time Module's: XROM 0, which no HP-41 module uses and Thomas Okken has left
    # to the PX. 0,0 stays empty: on the HP-41, XROM m,0 is the module's header.
    ('The PX-42\'s speaker (px_commands.cc)', [
        ('PLAYWAV', 'docmd_playwav', 0xa001, 0, 'NA_T'),
        ('PLAYMID', 'docmd_playmid', 0xa002, 0, 'NA_T'),
        ('STOPSND', 'docmd_stopsnd', 0xa003, 0, 'NA_T'),
        ('SNDWAIT', 'docmd_sndwait', 0xa004, 0, 'NA_T'),
    ]),
    ('The PX-42\'s shapes (px_commands.cc)', [
        ('PEN', 'docmd_pen', 0xa005, 2, '0x01'),
        ('BOX', 'docmd_box', 0xa006, 4, '0x01'),
        ('DLINE', 'docmd_dline', 0xa007, 4, '0x01'),
        ('ELLIPSE', 'docmd_ellipse', 0xa008, 4, '0x01'),
        ('POLYGON', 'docmd_polygon', 0xa009, 1, '0x04'),
    ]),
]

# The Catalog's PX42 page (up and down from FCN...): (tab, its commands), six to a row,
# the likely ones first. Every command above is in it exactly once; main() checks.
PX_CATALOG = [
    ('CLOCK', ['CLOCK', 'CLKT', 'CLKTD', 'SETDATE', 'SETIME', 'T+X', 'CORRECT', 'RCLAF', 'SETAF']),
    ('ALARM', ['XYZALM', 'ALMCAT', 'ALMNOW', 'RCLALM', 'CLALMA', 'CLALMX', 'CLRALMS']),
    ('SW', ['SW', 'SWPT', 'RUNSW', 'STOPSW', 'SETSW', 'RCLSW']),
    ('SOUND', ['PLAYWAV', 'PLAYMID', 'STOPSND', 'SNDWAIT']),
    ('DRAW', ['PEN', 'BOX', 'DLINE', 'ELLIPSE', 'POLYGON']),
]
# What a soft key shows of a name too long for a tab (5 letters), as Plus42 does it: the
# other letters are marked in the table (bit 7), and only draw_key() leaves them out.
SOFT_KEYS = {
    'SETDATE': 'SDATE', 'SETIME': 'STIME', 'CORRECT': 'CORCT',
    'XYZALM': 'XYZAL', 'ALMCAT': 'ALCAT', 'ALMNOW': 'ALNOW', 'RCLALM': 'RCALM',
    'CLALMA': 'CLMA', 'CLALMX': 'CLMX', 'CLRALMS': 'CLRMS',
    'STOPSW': 'STPSW',
    'PLAYWAV': 'PLWAV', 'PLAYMID': 'PLMID', 'STOPSND': 'STSND', 'SNDWAIT': 'SNDWT',
    'ELLIPSE': 'ELIPS', 'POLYGON': 'PGON',
}
# Well past Plus42's own sections (59 in 1.5.2), which it adds to from the end.
CATSECT_PX = 100

UPSTREAM_ROW = re.compile(r'^\s*\{ /\* \S+ \*/\s+\w+,\s+"([^"]*)",\s*0x\w\w,\s*0x\w\w,\s*0x(\w\w),\s*0x(\w\w),', re.M)
SENTINEL = re.compile(r'^#define CMD_SENTINEL\s+(\d+)\n', re.M)
TABLE_END = re.compile(r'\n\};\n(?=\n/\*\n=+\nHP-42S program storage format)')
INCLUDES_END = '#include "core_helpers.h"\n'


def identifier(name):
    return name.replace('+', '_PLUS_')


def replace_once(pattern, new, text, what):
    result, n = pattern.subn(lambda _: new, text)
    if n != 1:
        sys.exit('%s: found %d times in Plus42\'s tables, expected once' % (what, n))
    return result


def check_own():
    for what, values in (('name', [c[0] for _, g in COMMANDS for c in g]), ('code', [c[2] for _, g in COMMANDS for c in g])):
        twice = sorted({v for v in values if values.count(v) > 1})
        if twice:
            sys.exit('two PX commands have the same %s: %s' % (what, twice))


def check_free(cc):
    taken = {(int(a, 16) << 8) | int(b, 16): name for name, a, b in UPSTREAM_ROW.findall(cc)}
    names = set(taken.values())
    if len(taken) < 100:
        sys.exit('only %d commands recognized in Plus42\'s table: has its layout changed?' % len(taken))
    for _, commands in COMMANDS:
        for name, _, code, _, _ in commands:
            if len(name) > 7:
                sys.exit('%s: XEQ can only type 7 letters' % name)
            if code in taken:
                sys.exit('%s: Plus42\'s %s has code %04x now' % (name, taken[code], code))
            if name in names:
                sys.exit('%s: Plus42 has a command with this name now' % name)


def table_name(name):
    """The name as core_tables.cc spells it, the letters a soft key leaves out marked."""
    kept = SOFT_KEYS.get(name, name)
    out = ''
    i = 0
    for c in name:
        if i < len(kept) and c == kept[i]:
            out += c
            i += 1
        else:
            out += '\\%03o' % (ord(c) | 0x80)
    if i != len(kept):
        sys.exit('SOFT_KEYS: %s isn\'t letters of %s' % (kept, name))
    return out


def check_catalog():
    commands = sorted(name for _, group in COMMANDS for name, *_ in group)
    listed = sorted(name for _, names in PX_CATALOG for name in names)
    if listed != commands:
        sys.exit('PX_CATALOG and COMMANDS differ: %s' % sorted(set(listed) ^ set(commands) or {'a command twice'}))
    if len(PX_CATALOG) > 6 or any(len(tab) > 5 for tab, _ in PX_CATALOG):
        sys.exit('PX_CATALOG: at most 6 tabs of 5 letters')
    for tab, names in PX_CATALOG:
        shown = [SOFT_KEYS.get(name, name) for name in names]
        if any(len(s) > 5 for s in shown) or len(set(shown)) != len(shown):
            sys.exit('%s: soft keys must be different and at most 5 letters: %s' % (tab, shown))


def header(h):
    first = int(SENTINEL.search(h).group(1)) if SENTINEL.search(h) else 0
    new = ''
    n = first
    for comment, commands in COMMANDS:
        new += '/* %s */\n' % comment
        for name, *_ in commands:
            new += '#define %-16s%d\n' % ('CMD_' + identifier(name), n)
            n += 1
        new += '\n'
    new += '#define CMD_SENTINEL    %d\n' % n
    new += '\n/* The Catalog\'s PX42 page and its tabs (sections). */\n'
    new += '#define CATSECT_PX      %d\n' % CATSECT_PX
    new += '#define CATSECT_PX_TAB  %d\n' % (CATSECT_PX + 1)
    new += '#define PX_CAT_TABS     %d\n' % len(PX_CATALOG)
    new += '#define PX_CAT_TAB(s)   ((s) >= CATSECT_PX_TAB && (s) < CATSECT_PX_TAB + PX_CAT_TABS)\n'
    new += 'struct px_cat_tab { const char *title; int title_length; const int *cmds; int rows; };\n'
    new += 'extern const px_cat_tab px_cat_tabs[PX_CAT_TABS];\n'
    return replace_once(SENTINEL, new, h, 'CMD_SENTINEL')


def table(cc):
    if cc.count(INCLUDES_END) != 1:
        sys.exit('%s: expected once in Plus42\'s table' % INCLUDES_END.strip())
    cc = cc.replace(INCLUDES_END, INCLUDES_END + '#include "px_commands.h"\n')
    new = ''
    for comment, commands in COMMANDS:
        new += '\n\n    /* %s */' % comment
        for name, handler, code, args, types in commands:
            new += '\n    { %-18s%-19s%-23s0x00, 0x00, 0x%02x, 0x%02x, %2d, ARG_NONE,   %d, 0, %s },' % (
                '/* %s */' % identifier(name), handler + ',', '"%s",' % table_name(name), code >> 8, code & 0xff, len(name), args, types)
    new += '\n};\n'
    tabs = ''
    for tab, names in PX_CATALOG:
        cells = ['CMD_' + identifier(name) for name in names]
        cells += ['CMD_NULL'] * (-len(cells) % 6)
        new += '\nstatic const int px_cat_%s[] = {\n' % tab.lower()
        for row in range(0, len(cells), 6):
            new += '    %s,\n' % ', '.join(cells[row:row + 6])
        new += '};\n'
        tabs += '    { "%s", %d, px_cat_%s, %d },\n' % (tab, len(tab), tab.lower(), len(cells) // 6)
    new += '\nconst px_cat_tab px_cat_tabs[PX_CAT_TABS] = {\n%s};\n' % tabs
    return replace_once(TABLE_END, new, cc, 'the end of the command table')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--upstream', default=os.path.join(ROOT, 'plus42-upstream', 'common'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'src', 'core'))
    args = ap.parse_args()

    check_own()
    check_catalog()
    stale = []
    for filename, make in (('core_tables.h', header), ('core_tables.cc', table)):
        with open(os.path.join(args.upstream, filename), newline='') as f:
            text = f.read()
        if make is table:
            check_free(text)
        result = make(text)
        path = os.path.join(args.out, filename)
        if args.check:
            with open(path, newline='') as f:
                if f.read() != result:
                    stale.append(filename)
        else:
            with open(path, 'w', newline='') as f:
                f.write(result)
    if stale:
        sys.exit('out of date, run tools/px_core_tables.py: ' + ', '.join(stale))


if __name__ == '__main__':
    main()

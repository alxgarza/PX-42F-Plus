#!/usr/bin/env python3
"""Builds manual.html, the user manual, from tools/manual/manual.template.html,
and ui_guidelines.html, the settings' UI rules, from ui_guidelines.template.html:
remakes the settings screenshots (tools/menu_shots/run.sh) and the font maps
(tools/font_maps.py), then puts every image into the page itself, so the
manual is one file that opens anywhere. The pictures are made in a temporary
folder, gone when it's done: the page is all that's left.

    python3 tools/manual/make_manual.py

Placeholders: {{SCREEN}} (tools/manual/screen.png, a screenshot from the
calculator), {{LARGE}} {{SMALL}} {{MINI}} (the font maps), {{SHOT:name}}
(a settings screen, menu_shots/<name>.png), {{ICON:name}} (status_icons/<name>.png,
the status bar's indicators: tools/manual/status_icons.c) and {{SKIN:name}}
(tools/manual/skins/<name>.png: the left half of a Plus42 desktop skin's GIF, as
served from paxerlabs.com/px42f/plus42_skin/, 300 px wide, 64 colors)."""
import base64
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def uri(path):
    with open(path, 'rb') as f:
        return 'data:image/png;base64,' + base64.b64encode(f.read()).decode()


PAGES = (('tools/manual/manual.template.html', 'manual.html'),
         ('tools/manual/ui_guidelines.template.html', 'ui_guidelines.html'))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        dirs = make_pictures(tmp)
        for template, out in PAGES:
            page = fill(os.path.join(ROOT, template), *dirs)
            with open(os.path.join(ROOT, out), 'w') as f:
                f.write(page)
            print('%s: %d bytes' % (out, len(page)))


def make_pictures(tmp):
    shots, maps, icons = (os.path.join(tmp, d) for d in ('menu_shots', 'font_maps', 'status_icons'))
    subprocess.run([os.path.join(ROOT, 'tools/menu_shots/run.sh'), shots], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools/font_maps.py'), maps], check=True,
                   stdout=subprocess.DEVNULL)
    os.makedirs(icons)
    exe = os.path.join(tmp, 'status_icons_exe')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wno-comment', '-I' + os.path.join(ROOT, 'tools/menu_shots/stub'),
                    '-I' + ROOT, '-I' + os.path.join(ROOT, 'fonts')]
                   + [os.path.join(ROOT, f) for f in ('tools/manual/status_icons.c', 'display_draw.c', 'icons.c',
                                                       'pngwrite.c', 'fonts/font_8x12_data.c')]
                   + ['-o', exe], check=True)
    subprocess.run([exe, icons], check=True)
    return shots, maps, icons


def fill(template, shots, maps, icons):
    with open(template) as f:
        page = f.read()
    page = page.replace('{{SCREEN}}', uri(os.path.join(ROOT, 'tools/manual/screen.png')))
    for key, name in (('LARGE', 'large_10x16'), ('SMALL', 'small_8x12'), ('MINI', 'mini_6x8')):
        page = page.replace('{{%s}}' % key, uri(os.path.join(maps, name + '.png')))
    page = re.sub(r'\{\{SHOT:(\w+)\}\}', lambda m: uri(os.path.join(shots, m.group(1) + '.png')), page)
    page = re.sub(r'\{\{ICON:(\w+)\}\}', lambda m: uri(os.path.join(icons, m.group(1) + '.png')), page)
    page = re.sub(r'\{\{SKIN:(\w+)\}\}', lambda m: uri(os.path.join(ROOT, 'tools/manual/skins', m.group(1) + '.png')),
                  page)
    left = re.findall(r'\{\{[^}]*\}\}', page)
    if left:
        sys.exit('placeholders left: %s' % ', '.join(left))
    return page


if __name__ == '__main__':
    main()

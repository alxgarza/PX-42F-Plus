#!/usr/bin/env python3
"""Builds manual.html, the user manual, from tools/manual/manual.template.html:
remakes the settings screenshots (tools/menu_shots/run.sh) and the font maps
(tools/font_maps.py), then puts every image into the page itself, so the
manual is one file that opens anywhere.

    python3 tools/manual/make_manual.py

Placeholders: {{SCREEN}} (tools/manual/screen.png, a screenshot from the
calculator), {{LARGE}} {{SMALL}} {{MINI}} (the font maps), {{SHOT:name}}
(build/menu_shots/<name>.png) and {{ICON:name}} (build/status_icons/<name>.png,
the status bar's indicators: tools/manual/status_icons.c)."""
import base64
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def uri(path):
    with open(os.path.join(ROOT, path), 'rb') as f:
        return 'data:image/png;base64,' + base64.b64encode(f.read()).decode()


def main():
    subprocess.run([os.path.join(ROOT, 'tools/menu_shots/run.sh')], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools/font_maps.py')], check=True,
                   stdout=subprocess.DEVNULL)
    icons = os.path.join(ROOT, 'build/status_icons')
    os.makedirs(icons, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, 'status_icons')
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wno-comment', '-I' + os.path.join(ROOT, 'tools/menu_shots/stub'),
                        '-I' + ROOT, '-I' + os.path.join(ROOT, 'fonts')]
                       + [os.path.join(ROOT, f) for f in ('tools/manual/status_icons.c', 'display_draw.c', 'icons.c',
                                                           'pngwrite.c', 'fonts/font_8x12_data.c')]
                       + ['-o', exe], check=True)
        subprocess.run([exe, icons], check=True)
    with open(os.path.join(ROOT, 'tools/manual/manual.template.html')) as f:
        page = f.read()
    page = page.replace('{{SCREEN}}', uri('tools/manual/screen.png'))
    for key, name in (('LARGE', 'large_10x16'), ('SMALL', 'small_8x12'), ('MINI', 'mini_6x8')):
        page = page.replace('{{%s}}' % key, uri('build/font_maps/%s.png' % name))
    page = re.sub(r'\{\{SHOT:(\w+)\}\}', lambda m: uri('build/menu_shots/%s.png' % m.group(1)), page)
    page = re.sub(r'\{\{ICON:(\w+)\}\}', lambda m: uri('build/status_icons/%s.png' % m.group(1)), page)
    left = re.findall(r'\{\{[^}]*\}\}', page)
    if left:
        sys.exit('placeholders left: %s' % ', '.join(left))
    with open(os.path.join(ROOT, 'manual.html'), 'w') as f:
        f.write(page)
    print('manual.html: %d bytes' % len(page))


if __name__ == '__main__':
    main()

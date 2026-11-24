#!/bin/bash
# Builds and runs the host parser tests (tests/test_parsers.c) with
# AddressSanitizer and UBSan, against the built-in sounds as WAV files,
# the ALPHA keyboard's rules (tests/test_alphakeys.c), the HP 82240B
# printer's bytes (tests/test_hp82240.c), the app table's shapes
# (tests/test_draw.c), the standard screens key by key (tests/test_ui.c) and the
# Catalog's PX42 page (tests/test_catalog.cc).
#   tests/run.sh
set -e
cd "$(dirname "$0")/.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
python3 -I tools/make_sounds.py --wav "$TMP/sounds" >/dev/null
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. -Ifonts -Itests tests/test_parsers.c png1bit.c wav.c fontsheet.c pngwrite.c appfile.c soundname.c midiplay.c dutyshape.c images/splash_image.c \
   fonts/font_8x12_data.c fonts/font_10x16_data.c fonts/font_6x8_data.c -lm -o "$TMP/test_parsers"
# Saving a small file so a failure can't lose the old one.
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. tests/test_safefile.c safefile.c -o "$TMP/test_safefile"
"$TMP/test_safefile" "$TMP"
# The ALPHA keyboard's rules (keyboard.h needs the Pico SDK's types: menu_shots' stand-ins).
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -Wno-comment -I. -Itools/menu_shots/stub tests/test_alphakeys.c alphakeys.c -o "$TMP/test_alphakeys"
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -Wno-comment -DUI_CHECK_TABS -I. -Itools/menu_shots/stub tests/test_ui.c ui.c -o "$TMP/test_ui"
"$TMP/test_ui"
"$TMP/test_alphakeys"
# The HP 82240B printer's bytes and infrared frames.
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. tests/test_hp82240.c hp82240.c -o "$TMP/test_hp82240"
"$TMP/test_hp82240"
# A USB thermal printer's ESC/POS bytes. With a file name the test also
# writes a sample job: tools/usb_printer_test/yc_print.py --file sends it.
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. -Ifonts tests/test_escpos.c escpos.c fonts/font_6x8_data.c -o "$TMP/test_escpos"
"$TMP/test_escpos"
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. tests/test_time.c timemodule.c alarms.c stopwatch.c rtc_format.c ra8900.c -o "$TMP/test_time"
"$TMP/test_time"
# The settings record: today's and every older layout an update must read.
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. tests/test_settings.c settings_record.c -o "$TMP/test_settings"
"$TMP/test_settings"
# The shapes of the app table.
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. tests/test_draw.c px_draw.c px_layer.c -o "$TMP/test_draw"
"$TMP/test_draw"
PNG_WRITE_OUT="$TMP/screen.png" "$TMP/test_parsers" $(find "$TMP/sounds" -name '*.wav' | sort)
# The PNG writer's checksums, which png1bit.c ignores: every chunk's CRC,
# and the Adler-32 (zlib checks it) of an image of the right size.
python3 -I - "$TMP/screen.png" <<'PY'
import struct, sys, zlib
data = open(sys.argv[1], 'rb').read()
assert data[:8] == b'\x89PNG\r\n\x1a\n', 'PNG signature'
pos, idat, kinds = 8, b'', []
while pos < len(data):
    length, kind = struct.unpack('>I4s', data[pos:pos + 8])
    body = data[pos + 8:pos + 8 + length]
    crc, = struct.unpack('>I', data[pos + 8 + length:pos + 12 + length])
    assert crc == zlib.crc32(kind + body), 'CRC of ' + kind.decode()
    kinds.append(kind)
    if kind == b'IDAT':
        idat += body
    pos += 12 + length
assert kinds == [b'IHDR', b'IDAT', b'IEND'] and pos == len(data), 'chunks'
assert len(zlib.decompress(idat)) == 122 * 33, 'image data size'
print('PNG writer: checksums good')
PY
# The Catalog's PX42 page, by pressing keys on the real core (binary math: no libbid here).
CORE_FLAGS="-std=c++11 -w -g -O0 -fsanitize=address,undefined -DPX42 -D_WCHAR_T -DFP_BINARY -Itests/catalog_stub -Isrc/core -I."
mkdir "$TMP/core"
for f in src/core/core_*.cc tests/test_catalog.cc; do
    c++ $CORE_FLAGS -c "$f" -o "$TMP/core/$(basename "$f").o" &
done
for job in $(jobs -p); do wait "$job"; done
c++ -fsanitize=address,undefined "$TMP"/core/*.o -o "$TMP/test_catalog"
"$TMP/test_catalog"
# The command tables are Plus42's plus the PX's; plus42-upstream/ isn't tracked.
if [ -d plus42-upstream/common ]; then
    python3 -I tools/px_core_tables.py --check
fi

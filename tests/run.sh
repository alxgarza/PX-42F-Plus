#!/bin/bash
# Builds and runs the host parser tests (tests/test_parsers.c) with
# AddressSanitizer and UBSan, against the built-in sounds as WAV files,
# the ALPHA keyboard's rules (tests/test_alphakeys.c), the HP 82240B
# printer's bytes (tests/test_hp82240.c) and the app table's shapes
# (tests/test_draw.c).
#   tests/run.sh
set -e
cd "$(dirname "$0")/.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
python3 -I tools/make_sounds.py --wav "$TMP/sounds" >/dev/null
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. -Ifonts -Itests tests/test_parsers.c png1bit.c wav.c fontsheet.c pngwrite.c appfile.c midiplay.c dutyshape.c images/splash_image.c \
   fonts/font_8x12_data.c fonts/font_10x16_data.c fonts/font_6x8_data.c -lm -o "$TMP/test_parsers"
# The ALPHA keyboard's rules (keyboard.h needs the Pico SDK's types: menu_shots' stand-ins).
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -Wno-comment -I. -Itools/menu_shots/stub tests/test_alphakeys.c alphakeys.c -o "$TMP/test_alphakeys"
"$TMP/test_alphakeys"
# The HP 82240B printer's bytes and infrared frames.
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. tests/test_hp82240.c hp82240.c -o "$TMP/test_hp82240"
"$TMP/test_hp82240"
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

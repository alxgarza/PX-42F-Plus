#!/bin/bash
# Builds and runs the host parser tests (tests/test_parsers.c) with
# AddressSanitizer and UBSan, against the built-in sounds as WAV files.
#   tests/run.sh
set -e
cd "$(dirname "$0")/.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
python3 -I tools/make_sounds.py --wav "$TMP/sounds" >/dev/null
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   -I. -Ifonts tests/test_parsers.c png1bit.c wav.c fontsheet.c images/splash_image.c \
   fonts/font_8x12_data.c fonts/font_10x16_data.c -o "$TMP/test_parsers"
"$TMP/test_parsers" $(find "$TMP/sounds" -name '*.wav' | sort)

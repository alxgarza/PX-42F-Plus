#!/bin/bash
# The sounds as the PX-42F's speaker gets them (px_sound.c), as WAV files:
#   tools/px_sound/run.sh [OUT_DIR]                    every built-in sound
#   tools/px_sound/run.sh OUT_DIR [-ms N] FILE.wav...  your own files
# OUT_DIR defaults to ~/Documents/PX-42F Media/PX Sounds.
set -e
OUT=${1:-"$HOME/Documents/PX-42F Media/PX Sounds"}
shift || true
ARGS=()
for a in "$@"; do  # files relative to where it was run from
    if [ -f "$a" ]; then ARGS+=("$(cd "$(dirname "$a")" && pwd)/$(basename "$a")"); else ARGS+=("$a"); fi
done
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
cd "$(dirname "$0")/../.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -O1 -g -Wall -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined \
   -Itools/px_sound/stub -Itools/menu_shots/stub -I. \
   tools/px_sound/px_sound.c wav.c sounds_data.c -o "$TMP/px_sound"
"$TMP/px_sound" "$OUT" ${ARGS[@]+"${ARGS[@]}"}

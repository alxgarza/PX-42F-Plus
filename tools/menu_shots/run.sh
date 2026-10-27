#!/bin/bash
# Screenshots of every settings screen, from the firmware's own drawing code
# run on this computer (menu_shots.c): for the user manual.
#   tools/menu_shots/run.sh [OUT_DIR]     (default build/menu_shots)
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-build/menu_shots}
mkdir -p "$OUT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
tools/menu_shots/build.sh "$TMP/menu_shots"
# From the top list (Off selected), each screen in turn and back. Never EXIT
# on the top list: that leaves the menu.
"$TMP/menu_shots" "$OUT" \
  shot:settings \
  DOWN ENTER shot:ir_remote EXIT \
  DOWN ENTER shot:date_time \
    ENTER shot:set_time EXIT \
    DOWN ENTER shot:time_format EXIT \
    DOWN ENTER shot:set_date EXIT \
    DOWN ENTER shot:date_format EXIT \
    DOWN ENTER shot:time_calibration \
      ENTER shot:correction EXIT \
      DOWN ENTER shot:exact_time EXIT \
    EXIT \
  EXIT \
  DOWN ENTER shot:sounds ENTER shot:key_sound EXIT EXIT \
  DOWN ENTER shot:fonts \
    ENTER shot:font_large ENTER shot:font_large_preview EXIT EXIT \
    DOWN DOWN ENTER ENTER shot:font_mini_preview EXIT EXIT \
  EXIT \
  DOWN ENTER shot:sleep_timer EXIT \
  DOWN ENTER shot:splash \
    ENTER shot:splash_timer EXIT \
    DOWN ENTER shot:splash_image ENTER shot:splash_preview EXIT EXIT \
  EXIT \
  DOWN ENTER shot:printer EXIT \
  DOWN ENTER shot:states \
    ENTER shot:save_state EXIT \
    DOWN ENTER shot:load_state EXIT \
  EXIT \
  DOWN ENTER shot:programs \
    ENTER shot:import_programs EXIT \
    DOWN ENTER shot:export_programs ENTER shot:export_name EXIT \
  EXIT \
  DOWN ENTER shot:usb_drive EXIT \
  DOWN ENTER shot:info EXIT

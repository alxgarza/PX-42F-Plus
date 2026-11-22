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
# From the home screen (Off picked), each screen in turn and back. Never
# EXIT on the home screen: that leaves the menu. Home: Off, USB Drive,
# Plus42, Alarms, Apps, Games, Settings, a blank row (the favourites go
# there: kept in HOME.TXT, so a folder stands in for the drive), Help and
# Info at the right of the third row (6 skips the blanks); the gear:
# Date & Time, Fonts, Sounds, Printer, Splash Image, Sleep Timer; Plus42:
# States, Programs. On the icons 6 steps to the next one; F1-F6 are the
# soft keys: icons ENTER ← ↑ ↓ → EXIT, with SHIFT MOVE FAV/UNFAV _ _ _ BACK (plain tabs);
# a list ENTER .. ↑ ↓ EXIT.
mkdir -p "$TMP/drive/SPLASH" "$TMP/drive/BIN/APPS" "$TMP/drive/BIN/GAMES"
# The splash list's screenshot, a real one, for the preview.
cp tools/manual/screen.png "$TMP/drive/SPLASH/20261020-174015.png"
PX_DRIVE="$TMP/drive" "$TMP/menu_shots" "$OUT" \
  shot:home \
  6 ENTER shot:usb_menu \
    ENTER shot:usb_drive EXIT \
    6 ENTER shot:format_drive EXIT \
    6 ENTER shot:update_firmware EXIT \
  EXIT \
  6 ENTER shot:plus42 \
    ENTER shot:states \
      F1 shot:save_state EXIT \
      F2 shot:load_state EXIT \
    EXIT \
    6 ENTER shot:programs \
      F2 shot:export_programs ENTER shot:export_name EXIT \
    EXIT \
  EXIT \
  6 ENTER shot:alarms F2 shot:alarm_edit DOWN DOWN ENTER shot:alarm_repeat EXIT DOWN DOWN DOWN ENTER shot:alarm_sound EXIT EXIT F1 shot:new_alarm EXIT EXIT \
  6 ENTER shot:apps SHIFT F2 6 6 SHIFT F2 shot:apps_fav SHIFT F1 6 6 2 shot:apps_moving F1 shot:apps_moved SHIFT F1 6 6 F6 shot:apps_cancelled EXIT \
  6 ENTER shot:games SHIFT F2 EXIT \
  6 ENTER shot:settings \
    ENTER shot:date_time \
      ENTER shot:set_time EXIT \
      DOWN ENTER shot:time_format EXIT \
      DOWN ENTER shot:set_date EXIT \
      DOWN ENTER shot:date_format EXIT \
      DOWN ENTER shot:time_calibration \
        ENTER shot:correction EXIT \
        DOWN ENTER shot:exact_time EXIT \
      EXIT \
    EXIT \
    6 ENTER shot:fonts \
      ENTER shot:font_large DOWN ENTER shot:font_large_preview EXIT \
      DOWN DOWN ENTER DOWN ENTER shot:font_mini_preview EXIT \
    EXIT \
    6 ENTER shot:sounds ENTER shot:key_sound EXIT EXIT \
    6 ENTER shot:printer ENTER shot:printer_pick DOWN DOWN ENTER shot:printer_ir ENTER DOWN ENTER shot:screen_print EXIT shot:printer_form \
      UP ENTER DOWN ENTER shot:printer_escpos ENTER shot:printer_escpos_form \
      ENTER UP ENTER EXIT shot:printer_cancelled EXIT \
    6 ENTER shot:splash \
      ENTER shot:splash_timer EXIT \
      DOWN ENTER shot:splash_image DOWN ENTER shot:splash_preview EXIT \
    EXIT \
    6 ENTER shot:sleep_timer EXIT \
    6 ENTER shot:menu_style EXIT \
    6 ENTER shot:reset EXIT \
  EXIT \
  6 6 6 6 ENTER shot:help EXIT \
  6 ENTER shot:info EXIT \
  6 2 shot:home_favs SHIFT F1 6 6 6 shot:moving F1 shot:moved \
  6 SHIFT F1 2 shot:moving_blank F1 shot:moved_blank \
  UP UP UP UP SHIFT shot:home_unfav F2 shot:unfavved
# The places those keys leave (menu_places.c), checked, not only drawn:
# BASIC and IR made favourites, their folder places left blank; GRAPH moved
# a row down, its place left blank, and a second move cancelled; BASIC
# carried past IR and GOLDRUN on the home screen, Help down a row, IR sent
# back. A blank line is an empty place.
places() {
    if ! diff -u <(printf '%s\n' "${@:2}") "$TMP/drive$1"; then
        echo "menu_shots: $1 isn't what the keys should leave" >&2
        exit 1
    fi
}
places /BIN/APPS/ORDER.TXT "" CONTACTS "" "" MIDI NOTES SHEET SUNMAP TIMER UNITS "" GRAPH
places /HOME.TXT "USB Drive" Plus42 Alarms Apps Games Settings "" "" GAMES/GOLDRUN APPS/BASIC \
    "" "" "" "" "" "" "" "" "" Info "" "" "" "" "" Help
# The list style (Settings -> Menu Style -> List): one list of everything.
PX_MENU_LIST=1 "$TMP/menu_shots" "$OUT" \
  shot:list_menu \
  DOWN ENTER shot:list_apps EXIT \
  DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN shot:list_menu_end

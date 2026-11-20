#!/bin/bash
# Builds the menu_shots tool (menu_shots.c) to the path given.
#   tools/menu_shots/build.sh OUT_EXE
set -e
cd "$(dirname "$0")/../.."
OUT=$1
# The firmware version, as CMakeLists.txt gives it to the build (Info shows it).
VERSION=$(sed -n 's/^pico_set_program_version([^ ]* "\(.*\)")/\1/p' CMakeLists.txt)
cc -std=c11 -g -Wall -Wno-unused-parameter -Wno-comment -fsanitize=address,undefined -DUI_CHECK_TABS \
   -DPICO_PROGRAM_VERSION_STRING="\"$VERSION\"" -Itools/menu_shots/stub -I. -Ifonts -Isrc/core \
   tools/menu_shots/menu_shots.c settings_menu.c menu_places.c safefile.c ui.c ui_screen.c display_draw.c rtc_format.c splash.c icons.c menu_icons.c \
   png1bit.c pngwrite.c images/splash_image.c timemodule.c alarms.c \
   fonts/font_8x12_data.c fonts/font_10x16_data.c fonts/font_6x8_data.c -o "$OUT"

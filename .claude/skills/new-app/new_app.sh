#!/bin/bash
# Makes a new PX-42F app project next to the firmware repo, from the App Template,
# with the firmware's latest app API (px_app.h, px_draw, ui) in it.
#
#   new_app.sh FOLDER NAME KIND
#     FOLDER  the new sibling folder, e.g. PX-Chess (made next to PX-42F-Plus)
#     NAME    the .bin and menu name: up to 28 letters, digits, _ or -
#     KIND    APPS or GAMES (/BIN/APPS or /BIN/GAMES)
#
# It builds the .bin and runs the simulator once, then makes the first commit.
set -e
[ $# = 3 ] || { echo "usage: $0 FOLDER NAME KIND" >&2; exit 2; }
FOLDER=$1 NAME=$2 KIND=$3

cd "$(dirname "$0")/../../.."          # PX-42F-Plus
FIRMWARE=$PWD
PARENT=$(dirname "$FIRMWARE")
TEMPLATE=$PARENT/PX-42F-App-Template
DEST=$PARENT/$FOLDER

[[ $FOLDER =~ ^[A-Za-z0-9._-]+$ ]] || { echo "bad folder name: $FOLDER" >&2; exit 2; }
[[ $NAME =~ ^[A-Za-z0-9_-]{1,28}$ ]] || { echo "bad NAME (1-28 of A-Z a-z 0-9 _ -): $NAME" >&2; exit 2; }
[[ $KIND = APPS || $KIND = GAMES ]] || { echo "KIND must be APPS or GAMES: $KIND" >&2; exit 2; }
[ -d "$TEMPLATE/px42f" ] || { echo "no template at $TEMPLATE" >&2; exit 1; }
[ -e "$DEST" ] && { echo "already exists: $DEST" >&2; exit 1; }

mkdir "$DEST"
# The template's working tree, not its last commit: it is kept current with the firmware.
for part in .gitignore .vscode Makefile px42f sim src; do
    rsync -a --exclude .DS_Store "$TEMPLATE/$part" "$DEST/"
done

# The latest API: the firmware's own files win over the template's copies.
cp "$FIRMWARE/px_app.h" "$DEST/px42f/px_app.h"
for f in px_draw.c px_draw.h ui.c ui.h; do cp "$FIRMWARE/$f" "$DEST/sim/$f"; done

# NAME and KIND (the comments after them stay).
sed -i '' -E "s/^NAME := [A-Za-z0-9_-]+#/NAME := $NAME#/; s/^KIND := [A-Z]+#/KIND := $KIND#/" "$DEST/Makefile"
sed -i '' -E "s#build/HELLO\.bin, shown in the menu as HELLO#build/$NAME.bin, shown in the menu as $NAME#" "$DEST/Makefile"
grep -q "^NAME := $NAME#" "$DEST/Makefile" && grep -q "^KIND := $KIND#" "$DEST/Makefile" \
    || { echo "couldn't set NAME/KIND in $DEST/Makefile" >&2; exit 1; }

API=$(sed -nE 's/^#define PX_APP_API_VERSION ([0-9]+).*/\1/p' "$DEST/px42f/px_app.h")
cat > "$DEST/README.md" <<EOF
# $NAME

A PX-42F $( [ $KIND = GAMES ] && echo game || echo app ), \`/BIN/$KIND/$NAME.bin\`.
Built on the PX-42F app framework, API version $API.

| Command | What it does |
|---|---|
| \`make\` | Builds \`build/$NAME.bin\` |
| \`make sim\` | Runs it on the Mac with \`SIM_SCRIPT\`; screenshots in \`build/sim-out\` |
| \`make live\` | The simulator in the browser |
| \`make install\` | Copies it onto the calculator's drive (USB Drive mode) |

The framework (\`px42f/\`, \`sim/\`) is kept the same as the template's by
\`../PX-42F-App-Template/tools/sync_framework.sh\`; don't edit it here.
EOF

cd "$DEST"
make
make sim
git init -q
git add -A
git commit -q -m "$NAME: a new PX-42F app from the template (API $API)"
echo "made $DEST: $NAME in /BIN/$KIND, API $API"

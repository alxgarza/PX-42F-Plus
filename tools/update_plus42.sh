#!/bin/bash
# Moves the Plus42 core in src/core/ to a newer Plus42 release, keeping the
# PX's patches (the PX42 blocks).
#
#   tools/update_plus42.sh [NEW_TAG]       (default: Thomas's newest tag)
#   tools/update_plus42.sh --from TAG [NEW_TAG]
#
# The old release is PLUS42_VERSION in settings_menu.c ("1.5.2" = tag v1.5.2)
# unless --from names it. For each file of src/core/ that is Plus42's own
# (it exists in plus42-upstream/common/) and changed between the two tags,
# a three-way merge: the old tag is the base, our file one side, the new tag
# the other, so our patches stay and Thomas's changes come in. Not
# core_tables.*: tools/px_core_tables.py writes them from upstream's.
# Upstream's master is fast-forwarded to the new tag first; after the merges
# the tables are regenerated, PLUS42_VERSION is set, and tests/run.sh runs.
#
# Needs src/core/ and settings_menu.c committed first: a merge that went
# wrong is undone with "git checkout -- src/core settings_menu.c", and a
# second run can't merge the same changes twice.
set -e
cd "$(dirname "$0")/.."
UP=plus42-upstream

OLD=
if [ "$1" = "--from" ]; then
    OLD=$2
    shift 2
fi
NEW=$1

[ -d "$UP/.git" ] || { echo "No $UP/ (git clone https://codeberg.org/thomasokken/plus42desktop.git $UP)"; exit 1; }
if ! git diff --quiet HEAD -- src/core settings_menu.c; then
    echo "src/core/ or settings_menu.c has uncommitted changes: commit them first."
    exit 1
fi

echo "Fetching Plus42's tags"
git -C "$UP" fetch -q --tags origin
if [ -z "$OLD" ]; then
    OLD=v$(sed -n 's/^#define PLUS42_VERSION *"\(.*\)"/\1/p' settings_menu.c)
fi
[ -n "$NEW" ] || NEW=$(git -C "$UP" tag --sort=-creatordate | head -1)
for tag in "$OLD" "$NEW"; do
    git -C "$UP" rev-parse -q --verify "refs/tags/$tag" >/dev/null || { echo "No tag $tag in $UP"; exit 1; }
done
if [ "$OLD" = "$NEW" ]; then
    echo "Already at $NEW."
    exit 0
fi
echo "Plus42 $OLD -> $NEW"

# First, so a release master can't reach stops us before anything changes.
# The merges below read both releases by tag, wherever master is.
echo "Fast-forwarding $UP to $NEW"
git -C "$UP" checkout -q master
git -C "$UP" merge -q --ff-only "$NEW" || { echo "$NEW isn't ahead of $UP's master: nothing changed."; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
merged=() conflicted=() added=() removed=()
while IFS=$'\t' read -r status path; do
    f=$(basename "$path")
    case "$status" in
        A) added+=("$f"); continue ;;
        D) removed+=("$f"); continue ;;
    esac
    [ -f "src/core/$f" ] || continue            # a desktop-only file
    case "$f" in core_tables.*) continue ;; esac
    git -C "$UP" show "$OLD:common/$f" > "$TMP/base"
    git -C "$UP" show "$NEW:common/$f" > "$TMP/new"
    if git merge-file -L PX-42F -L "$OLD" -L "$NEW" "src/core/$f" "$TMP/base" "$TMP/new"; then
        merged+=("$f")
    else
        conflicted+=("$f")
    fi
done < <(git -C "$UP" diff --name-status --no-renames "$OLD" "$NEW" -- common)

python3 -I tools/px_core_tables.py
sed -i '' "s/^#define PLUS42_VERSION .*/#define PLUS42_VERSION  \"${NEW#v}\"/" settings_menu.c

echo
echo "Merged cleanly: ${merged[*]:-none}"
if [ ${#added[@]} -gt 0 ]; then
    echo "New in Plus42's common/ (add to src/core/ and CMakeLists.txt if the core uses them): ${added[*]}"
fi
if [ ${#removed[@]} -gt 0 ]; then
    echo "Gone from Plus42's common/ (remove from src/core/ and CMakeLists.txt): ${removed[*]}"
fi
if [ ${#conflicted[@]} -gt 0 ]; then
    echo "CONFLICTS (<<<<<<< PX-42F ... >>>>>>> $NEW) in: ${conflicted[*]}"
    echo "Resolve them, then run tests/run.sh and build."
    exit 1
fi
echo
tests/run.sh
echo
echo "Plus42 ${NEW#v}: build, flash, and update the version in CLAUDE.md."

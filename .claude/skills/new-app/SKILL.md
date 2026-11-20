---
name: new-app
description: Create a new PX-42F app or game as its own sibling project folder, from the App Template with the firmware's latest app API, then write the app. Use when the user asks for a new app, game or program for the PX-42F's Apps/Games menu.
---

Every app is its own project next to this repo (`../<Folder>`), its own git repo. Never put
an app inside PX-42F-Plus or inside another app's project.

1. **Names.** Settle three things from the request; ask only if the user hasn't said and it matters:
   - Folder: what the user says; otherwise the newer pattern `PX-<Name>` (older apps are
     `PX-42F-<Name>`, Contacts is just `Contacts`).
   - `NAME`: the `.bin` and menu name, ≤28 of `A-Z a-z 0-9 _ -` (usually upper case: `TVM`, `SUDOKU`).
   - `KIND`: `GAMES` or `APPS`.
2. **Make it:** `.claude/skills/new-app/new_app.sh <Folder> <NAME> <KIND>` (from this repo; ~1 min).
   It copies the template (`.gitignore .vscode Makefile px42f sim src`), puts the firmware's
   own `px_app.h`, `px_draw.*`, `ui.*` in it (the latest API, even if the template lags),
   sets `NAME`/`KIND`, writes a README, runs `make` and `make sim`, and makes the first commit.
   It refuses a folder that exists.
3. **Write the app** in `src/` (replace the template's `src/app.c` demo). Read first:
   `px42f/px_app.h` (the contract, every call and `PX_APP_API_VERSION`) and
   `../PX-42F-App-Template/framework_manual.html` for recipes. Follow the standard screens
   (`ui_*()`, `ui_dialog()`) and `ui_guidelines.html` (this repo) for titles, soft-key tabs, lists
   and forms rather than drawing your own. Sibling apps (`../PX-TVM`, `../PX-Sudoku`...) show the
   house style. Keep the app's RAM within what `make` prints (160 KB from API 9).
   `math.h` needs `-lm` at the end of the `.elf` link line in the Makefile (the template
   leaves it out). Keep the app's logic (math, rules) in its own `.c` with no `px` calls,
   with a `make test` target that runs `tests/test_*.c` on the Mac (see `../PX-TVM`).
   Soft keys: `px->ui_tabs()` (the small font, as Plus42's menu), SHIFT for a second row
   (HELP ... BACK), help with `ui_view_leave(title, text, "BACK")`.
4. **Icon:** draw `icon.txt` (32 lines of 32, `#` black, `.` white) for the app; without it the
   menu shows the name's first letter in a frame.
5. **Try it:** set `SIM_SCRIPT` in the Makefile to walk through the app with `shot:NAME`s,
   `make sim`, and look at the PNGs in `build/sim-out/` yourself. Fix what looks wrong.
6. **README:** say what the app does and its keys. Commit in the app's repo.
7. Report the folder, the menu name, and the screenshots. It runs on the device only after
   `make install` (calculator in USB Drive mode) — do that only when the user asks, and never
   claim it works on the hardware without their report.

If the firmware's `px_app.h` changed since, `../PX-42F-App-Template/tools/sync_framework.sh`
brings every app project (and the template) up to date.

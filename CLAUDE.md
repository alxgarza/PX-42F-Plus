# PX-42F-Plus

Plus42 (Thomas Okken's enhanced HP-42S, built on Free42) as the firmware of the **PX-42F**,
a custom RP2350A calculator board. The Plus42 core is in `src/core/` (upstream copy in
`plus42-upstream/`, from https://codeberg.org/thomasokken/plus42desktop; not tracked);
everything else at the top level is the board: display, keyboard, power, USB, storage.
The sister repo PX-42F (remote `px42f`) runs Free42 on the same board: board-code fixes
can be cherry-picked between them.

- The Catalog's PX42 page (up from the first row): its tabs and commands are `PX_CATALOG` in `tools/px_core_tables.py` (the script refuses a command missing from it), what a soft key shows of a long name is `SOFT_KEYS` (Plus42's way: the other letters marked with bit 7 in the table, `draw_key()` leaves them out, program lines and XEQ don't), the sections `CATSECT_PX` from 100, generated into `core_tables.*`; the rest is `PX42` patches in `core_display.cc` and `core_keydown.cc` wherever Plus42 names its catalog sections. A section it doesn't know is read as a variable and crashes: `tests/test_catalog.cc` presses the keys on the real core (binary math, the commands stood in for).
- PX changes to the core, kept small so a new Plus42 can be dropped in: `my_fopen` etc.
  (`core_main.cc`) and 5 rows (`core_main.cc`) and ATOP (`core_globals.cc`) at Memory Clear under `PX42`, the screen hooks under `PX42`
  in `core_display.cc`, TOP.FCN's PX row DIRS UNITS R↑ X<> VIEW EQN, shifted DIR.FCN UNIT.FCN PLOT %CH SST↑ EQN.FCN (`px_top_fcn2_shifted[]`, read where Plus42 does TOP.FCN's: `core_display.cc`, `core_keydown.cc`, and `core_main.cc`'s redraw on SHIFT), the one SHIFT 0 opens (two places in `core_keydown.cc`), ▼ the HP-42S row (`MENU_PX_TOP_FCN2` = 100, after Plus42's last menu, `core_globals.h/.cc`; the equation editor's `getmenu()` keeps TOP.FCN one row, `core_equations.cc`; `tests/test_catalog.cc`), SETDATE/SETIME (`core_commands7.cc`, `shell.h`), DELAY (`docmd_delay`, `core_commands2.cc` -> `shell_set_print_delay()`: `px_settings.print_delay_ms`, the HP 82240B's time per line in `printer.c`, 1.8 s again at Memory Clear), and the commands
  in `core_tables.*`. Those two files are generated: `tools/px_core_tables.py` (its `COMMANDS` list)
  appends the PX's commands to Plus42's, so never edit them or merge them; `tests/run.sh` checks
  them. New commands' handlers go in `px_commands.cc` (the shapes: PEN, BOX, DLINE, ELLIPSE, POLYGON, on the panel's 250 x 122, from 1,1 as Plus42 counts (apps: 0,0), in two layers over everything (`px_layer.c`, `free42_shape_canvases()`), as long as `mode_message_lines` lasts, the status bar hidden meanwhile; PIXEL, AGRAPH use it too, and WIDTH, HEIGHT say 250, 122: PX42 patches in `pixel_helper()`, `docmd_pixel`, `docmd_agraph` (`core_commands2.cc`), `draw_pattern()` (`core_display.cc`), `docmd_width/height` (`core_commands9.cc`); USB PRLCD still prints Plus42's own bitmap, so not these; the Time Module's clock: CLKT, CLKTD, CLOCK, CORRECT, RCLAF, SETAF, T+X; the alarms: XYZALM, ALMCAT, ALMNOW, CLALMA, CLALMX, CLRALMS, RCLALM; the stopwatch: SW, SWPT, RUNSW, STOPSW, SETSW, RCLSW; PLAYWAV and PLAYMID start and go on; SNDWAIT waits, interruptibly; STOPSND; R/S stops them, key sounds don't: `sounds_file_playing()`, `free42_key_down()`). Names at most 7
  letters (XEQ types no more; the script refuses longer), no parameter (XROM-coded; a name comes from
  ALPHA), the HP-41 Time Module's own XROM codes (`a680` + n for 26,n), or for the PX's own commands XROM 0 (`a000` + n, from 0,1; no HP-41 module uses it, and Thomas Okken has left it to the PX). If Plus42 ever takes a code of ours the script stops and ours moves. `free42_app.cc` and `shell.cc` are the PX's own (names kept from the Free42
  port). Plus42 needs C++ exceptions (`std::bad_alloc`) and `PICO_MALLOC_PANIC=0`.
- Plus42 can't read Free42 `.f42` states (different magic); state files are `.p42`.
- Now Plus42 1.5.2 (`PLUS42_VERSION` in `settings_menu.c`, the Info screen). Updating:
  `tools/update_plus42.sh [NEW_TAG]` (src/core/ committed first): in `plus42-upstream/` it fetches the tag,
  `git merge-file`s each changed `common/` file into `src/core/` (old tag = base), but not `core_tables.*`,
  fast-forwards its master, runs `tools/px_core_tables.py`, sets `PLUS42_VERSION`, runs `tests/run.sh`;
  conflicts are left marked `<<<<<<< PX-42F` for a person. Then build, and this line's version.

## Build

- `/build` skill, or by hand:
  `cmake -S . -B build -G Ninja` (only if `build/` is missing), then
  `~/.pico-sdk/ninja/v1.13.2/ninja -C build` — ninja is **not on PATH**; use this one
  (it's the version CMake configured with; v1.12.1 complains about the build log).
- Pico SDK 2.3.0 at `~/.pico-sdk/sdk/2.3.0`. Board header: `boards/px42f.h` (`PICO_BOARD px42f`).
- Output: `build/PX-42F-Plus.uf2`, `build/PX-42F-Plus.elf` (+ `.elf.map`, `.dis`).
- Host tests: `tests/run.sh` builds `tests/test_parsers.c` on the Mac (ASan/UBSan) and runs it:
  the PNG decoder (`png1bit.c`), WAV parser (`wav.c`), MIDI player (`midiplay.c`: a song with 30 notes at once, cut short at every length and damaged 400 ways) and font sheet slicer (`fontsheet.c`)
  against good, truncated and damaged files, plus the built-in sounds and fonts (also drawn as
  sheets and read back); the PNG writer (`pngwrite.c`) read back by the decoder and checked by
  Python's zlib; and the app header check (`appfile.c`) against bad `.bin` headers. Run it after
  touching any of them — they read files users drop on the drive, and a parser bug there
  freezes or resets the calculator. It also builds `tests/test_alphakeys.c`: the ALPHA keyboard's
  rules (`alphakeys.c`) key by key in [ ] / [a] / [A] -- run it after touching them. And
  `tests/test_hp82240.c`: the HP 82240B printer's bytes and IR frames (`hp82240.c`). And `tests/test_time.c`:
  the Time Module's numbers (`timemodule.c`), the accuracy factor's above all, and the clock's display.
  And `tests/test_settings.c`: the settings record (`settings_record.c`, no hardware; `settings.c` keeps the flash
  sectors) -- today's layout and every older one an update must read. A new settings version: add the old one to
  `older[]` there and to the test's `history[]` (the test stops until you do).
- Graphics demo: `demos/PXGRAPH.raw`, written by `tools/make_graph_demo.py` from Plus42's command table; `--preview DIR` decodes the bytes and draws each page to a PNG with the real `px_draw.c`, `px_layer.c`.
- User manual: `manual.html` (the screen, fonts, settings, the added commands: a new command gets a row in section 11; not a Plus42/HP-42S manual), built by
  `python3 tools/manual/make_manual.py` from `tools/manual/manual.template.html`. Rebuild it after
  changing a settings screen. Its settings screenshots come from `tools/menu_shots/run.sh`: the
  real `settings_menu.c`, `display_draw.c`, `rtc_format.c`, `splash.c` compiled on the Mac with
  stand-ins (`menu_shots.c`, `stub/`) and driven by a key script — also a way to see a settings
  screen without the device. Drawing (`display_draw.c`) and date/time text (`rtc_format.c`) are
  kept free of hardware for that. The status-bar icons come from `tools/manual/status_icons.c`.
- Settings tour movie and sound tryouts: all in `~/Documents/PX-42F Media/`, outside the repo
  (the user's choice: one folder, not on the Desktop, not syncing GBs to Dropbox). Write anything
  media-related there, never to the Desktop or `build/`. `python3 ~/Documents/"PX-42F Media"/scripts/make_movie.py
  [--repo DIR] [--intro MOVIE] [--voice NAME] [--rate WPM]` turns the storyboard `scripts/settings_tour.txt`
  (keys + narration per step; `@title`, `@calculator`, `@image`) into a narrated MP4 and an `.srt` in
  `Settings Tour/` (or `--clips`: one MP4 per step, `Clips/010 <phrase>.mp4`, 020..., for the user to
  join in iMovie with transitions there), filming this repo's menu_shots tool; working files in `work/`; encoded by
  `scripts/encode.swift` (AVFoundation, no ffmpeg). Needs ImageMagick. Voice: Kokoro `af_heart` by
  default (OpenVox's "Heart"; `scripts/kokoro_say.py`, mlx-audio in `tts-venv/`, model in
  `voice-model/` as HF_HOME) or any macOS `say` voice. `Typewriter Sounds/`: key-sound variations
  tried out. The user's intro movie lives in their OneDrive (PX42/VoodooTron/Voodootron alive.mp4);
  the end card is `scripts/voodootron_end.jpg`.
- Flashing over USB (only when the user asks): `picotool reboot -f -u`, wait until
  `picotool info` sees it, then `picotool load -x build/PX-42F-Plus.uf2`
  (`~/.pico-sdk/picotool/2.3.0/picotool/picotool`). `load -f` alone gives up too early.
  A reflash is a Memory Clear, on purpose (the user's choice): RESET and a flash clear all memory.
- `/flash` (`tools/flash.sh`) loads the build into the connected calculator and waits for
  it to come back. On the calculator: Settings → USB Drive → Update Firmware saves pending settings and calls the
  ROM's update mode from core 1 (the new firmware starts with Memory Clear, like any flash)
  (`power_update_mode()`, the `__real_` ROM function: main.c's wrapper would stop core 1).
- The user tests on the hardware (sound, current, screen) and reports back (currents, photos of fault
  screens). Never claim something works on the device without their report.

## Hardware (schematic: `PX42_TH_V1.1.kicad_sch`, pin table: `PINOUT.html`)

- RP2350A, 16 MB W25Q128 flash, 12 MHz crystal, Li-ion cell via RT9080 LDO (3.3 V),
  TP4057 charger. No PSRAM (GP8 = its unused CS, R3 pull-up). No VBUS sense GPIO.
- Keyboard 8x5 matrix: rows GP0-7 (outputs, idle high, pulls off), columns GP9-13
  (inputs, pull-ups). ON = the EXIT key position.
- Display ST7305 250x122 mono reflective, SPI0: SCK 18, MOSI 19, RS 20, RST 21, CS 22.
- Speaker: 16 ohm, low side MOSFET, gate GP15 (PWM slice 7B), 146 kHz PWM whose duty follows
  the waveform (0-100%, ~200 mA peaks). `speaker.c` only plays duty streams, by DMA: two
  channels, each a 1024-sample block (32 ms), chained to each other, paced by PWM slice 0's
  wrap DREQ at 32 kHz, writing the carrier's compare register (16-bit writes land in both
  halves; channel A's pin is GP14, never the PWM's). A block's DMA interrupt re-arms it and
  wakes the filler (WFI with interrupts masked around the check). No processor per sample, so
  a flash write parking a core can't freeze the speaker at one level. The DMA's clock is off
  except during a sound (`sound_clocks()`). Core 0 plays: core 1's `speaker_start()` hands
  over a fill function (state STARTING), core 0's idle loop (`speaker_core0_poll()`) sets up
  the DMA (its interrupt on core 0's NVIC), refills each freed block, and stops after the
  last sample (busy-polling the last few ms). The block interrupt SEVs, so neither core
  misses it between a check and WFE; `speaker_wait()` (core 1) wakes each block to feed the
  watchdog. `speaker_start()` raises core 1 to 150 MHz and `idle_wait()` doesn't lower it
  while a sound plays (the PWM runs from clk_sys). Waiting: BOOT (the sounds guard),
  OFF, Free42's TONE/BEEP and the error squeak (`sounds_play_tone(n, true)`); not waiting:
  key sounds, ON, apps' `_async`. Files: core 1 opens (and scans for the peak), core 0
  `fread()`s -- with `setvbuf()` to a static buffer, so stdio doesn't malloc on core 0;
  `sounds_stop()` closes it (before USB Drive, Format Drive, a new sound).
- Sounds (`sounds.c`): WAV files in `/SOUNDS` on the drive — Key Sound choices, loaded into
  RAM: a set `KEY/<name>/ENTRY.wav, OP.wav, ENTER.wav` (≤100 ms each; `free42_key_sound()`
  picks typing / operation / ENTER from the calculator's state; built-in set "Typewriter": a letter key, a heavier key, a brighter key) or one
  `KEY/<name>.wav` for every key (≤250 ms), `BOOT.wav` (every start but ON from OFF; no setting, only the file), `ON.wav`, `OFF.wav`, `TONES/TONE0-9.wav`,
  `ERROR.wav` (Free42's squeak, and any error Plus42 shows: `display_error()` wrapped at link time, `refresh()` plays it, Settings → Sounds → Calc Error Sound; flag 26 silences both, Silence Plus42) — those streamed, any length. Any PCM/float WAV, normalized
  and converted on the calculator. Built-ins are the same WAVs embedded in `sounds_data.c`,
  generated by `tools/make_sounds.py` (rerun after changing a sound; `--wav DIR` writes
  them out to listen to; `tools/px_sound/run.sh [DIR] [-ms N] [FILE.wav...]` writes what the speaker
  gets instead: sounds.c's own duty stream as 32 kHz WAVs, default `~/Documents/PX-42F Media/PX Sounds`); any missing from the drive is written at boot, and one still as written
  is updated when its built-in changes (checksums in `/SOUNDS/BUILTIN.TXT`); a built-in plays
  when its file is missing. Settings → Sounds: Key Sound, On / Off Sound, Calc Error Sound, Silence Plus42.
- MIDI (`midiplay.c`, no hardware calls, so the host tests build it; glue in `sounds.c`): a Standard MIDI File
  (format 0/1, ≤128 KB, `/MIDI/*.mid`) drives a 24-voice synth, 16 channels, mono. `sounds_midi_play()` (core 1)
  reads the file into a malloc'd buffer, `midi_open()` finds the tracks and plays the events once without sound for
  the length, then `speaker_start(midi_fill)`: core 0 steps the sequencer every 32 samples (1 ms) and mixes into
  `dutyshape.c` (the loudness-following level of `stream_fill`, shared). Core 0 never mallocs: `sounds_stop()`
  (core 1) frees the buffer. Instruments are 11 additive timbres picked by General MIDI family (`timbre_of()`;
  piano, bell, organ, plucked, bass, strings, brass, reed, flute, lead, pad) with their own ADSR; channel 10 is
  synthesized drums (kick/snare/hats/toms/cymbals/clicks). The mix is raised by `MASTER` and a peak limiter (instant down, 250 ms up) keeps chords from distorting: a note alone is nearly full scale like the WAV sounds (it was 4x too soft at first). Only apps play it (`px->midi_play()`, API version 4;
  `midi_status()` for length, position, channels); `sounds_key_play()` doesn't cut a song playing. No
  instrument samples (the synth only). Unverified on the hardware: loudness, the
  timbres, core 0's load with 24 voices. `tools/px_sound/run.sh OUT song.mid` renders a song to a WAV on the Mac.
  The player is the app `../PX-42F-MIDI-Player`.
- Calculator screen (`free42_app.cc`): Plus42 draws everything (stack, menus, messages,
  program listing, equation editor, plots) into its own bitmap of disp_r x disp_c 6x8 cells
  (`shell_blitter()`). The PX doesn't scale it: it redraws each cell's character with its
  own font (`display_cell_char()`, a PX42 patch in `core_display.cc` that records what
  `draw_char()` put there and checks nothing drew over it), the menu as PX tabs from the
  keys' labels (`display_key_label()`), and only non-character cells (plots, cursors) from
  Plus42's pixels. Plus42's row count (ROW+/ROW-/SETDS, DISP menu page 3; 5 after Memory
  Clear) picks the font (`LAYOUTS`): the menu is always the same small-font tabs in the
  bottom 16 px; the lines of text split the rest into equal slots, centred (with no menu
  they split all 110 px). Biggest font whose slot fits with a menu up: 2-5 rows large 10x16
  (22 cols, always dotted dividers), 6-8 small 8x12 (31 cols, dividers when all gaps ≥3 px),
  9-12 mini 6x8 (41 cols; Plus42's own characters, `fonts/gen_font_6x8.py`). Plus42's own label characters (x\200
  … \203\200, small capitals and a narrow colon) are drawn as X: Y: Z: T: (`font_char()`).
  `tools/font_maps.py` draws the three fonts as PNG maps (build/font_maps/). The large font's
  HP-42S symbols (0-31, 127) are drawn in `fonts/symbols_10x16.py`, which writes them into
  `font_10x16.png` and regenerates `font_10x16_data.c` (edit them there, not in the sheet).
  Plus42's header line (`{ HOME }` path, flags, BASE/TVM) is shown in the 12 px status bar
  instead (`display_header_text()`), or with HEADER off the date and time; right to left:
  battery, angle (° π G: ∡ is HPOLAR's), shift, ↑↓, ▶ running, bell, printer (Plus42's annunciator 3, `print_icon`: while sending, off about 250 ms after the last line; the header gives way only then). The core's `HEADER_SHOWN` /
  `HEADER_ROWS` (PX42, `core_display.h`): the header takes no display row. Skin flags 0 (Thomas's advice): CATALOG's first row is
  DIRS PGM REAL CPX MAT UNITS, FCN and MEM on the second. Every font code is the HP-42S character (0-31: ÷ × √ ∫ ▒ Σ ▶
  π ¿ ≤ LF ≥ ≠ ↵ ↓ → ← µ £ ° Å Ñ Ä ∡ ᴇ Æ … Esc Ö Ü ^ •; 94 ↑, 126 ~).
- Fonts (`fonts.c`, Settings → Fonts, `/FONTS/LARGE|SMALL|MINI`, PNG sheets 175x135,
  128x96, 96x64): the calculator screen's three layouts, the status bar and menu tabs
  (small). `/FONTS/BUILTIN/LARGE|SMALL|MINI.png`: the built-in fonts drawn as sheets
  (`font_sheet_draw()`), read-only (FatFs `FF_USE_CHMOD`), rewritten at `fonts_init()` only
  when they don't match. Settings version 9 added the mini font; 6-8 are still read. The settings
  menu, boot/crash screens and splash clock use the built-in 8x12/10x16 fonts.
- Held keys (`keyhold.c`, one place for both key loops — main.c and the settings menu): EXIT
  2 s = off; SHIFT and "−" (PRINT) together 0.5 s = a screenshot (`screenshot.c`, `screenshot_take()`): the screen
  as it was when SHIFT went down; with a real printer (HP 82240B, or USB ESC/POS not `usbprint_unavailable()`; the user's
  choice: not USB Serial) an alert asks SAVE / PRINT / CANCEL, without one SAVE / CANCEL, at once, the keys still down (`settings_menu_dialog()`, the menu's port, also from apps,
  `ui_dialog_buttons()`: SAVE first and ringed, the default, CANCEL last = EXIT, ←; the user's choice), printed turned (Landscape) whatever Screen Print says, any key stops it; SAVE writes `/SCREENSHOTS/<date>-<time>.png` (250x122 1-bit PNG,
  uncompressed, ~4 KB; usable as a splash image), Free42's shift put back. Not in USB Drive
  mode (the computer has the drive). In apps too: `key_hold_app_screenshot()` (keyhold.c), from
  app.c's key path on SHIFT down; the app gets SHIFT pressed again, cancelling its first press. And `key_hold_app_minus()`:
  "-" going down with SHIFT held is held back there up to 0.5 s -- a busy app (PX-Graph replotting) only looked between
  keys and mostly missed the gesture. No banner (the user's choice): the alert is the sign it was caught. SHIFT held ≥0.3 s then an arrow, in ALPHA: BST/SST
  (`free42_shift_arrow()`). Add new hold gestures there, not in the loops.
- ALPHA keyboard, as the DM42's: the rules in `alphakeys.c` (no core calls; `free42_app.cc`'s `alpha_state()`
  describes the core, `free42_key_down()` does what `alpha_key()` returns), the tables in `keymap.h`
  (also the menu_shots tool's). `px_settings.alpha_mode` [ ] HP-42S keys /
  [a] / [A] the printed letters (`alpha_key_matrix`); ALPHA (SHIFT then ENTER) in ALPHA switches -- it doesn't
  leave ALPHA there, EXIT does; remembered (`settings_save_later()`), badge in the status bar. In [a]/[A]: the dot key types ',' (SHIFT+dot '.'),
  the ALPHA menu's first page is hidden (`alpha_menu_hidden()`: not drawn in [a]/[A] unless SHIFT is down;
  the arrows page the menu, SHIFT or not; page 2 and the group menus are drawn and the top row works them
  without SHIFT; a pick from a group goes back to the first page, hidden; the panel has no = $ on the arrows),
  SHIFT+STO/RCL = ASTO/ARCL, SHIFT+I..O = π ( ) " < > ^, SHIFT+other letter = same letter,
  SHIFT+digit = digit, SHIFT+÷×−+ = symbol, R/S and SHIFT+R/S type '?' (they don't run there), SHIFT+top row = soft key.
  First ALPHA ever starts in [ ], and RESET (Memory Clear) puts it back there; numeric local labels (LBL 01) need [ ].
- Alarms (HP-41 Time Module; manual: HP-41CX vol. 2, section 16, appendix B): the table and its rules in `alarms.c` (no
  hardware, `tests/test_time.c`), what each kind does in `alarm_service()` (`free42_app.cc`, from `free42_service()`, so between a
  program's slices and never in settings or apps). No core patch: a control alarm in a running program is `docmd_xeq()` (a real
  subroutine), at the keyboard `core_keydown_command(name, 1)`, so labels must be ASCII. In RAM, `/ALARMS.DAT` written at OFF through `safefile.c` (as `/STOPWATCH.DAT`: a new file beside the old, which only then goes; `tests/test_safefile.c`; a label read from the file is held to what XYZALM allows)
  (`alarms_save()`), read at boot, deleted by Memory Clear. While off: `RETAINED_NEXT_ALARM` (`retained.h`) is in `next_wake()`; when due,
  `power_off_wake()` boots fully, `power_woke_for_alarm()`, and it turns itself off afterwards unless a key was pressed. Bell in the status bar
  (`icon_bell()`) when any is set, Settings -> Alarms lists them (`ui_list()`, tabs NEW EDIT DEL, EDIT/DEL only with alarms); NEW/EDIT open XYZALM's fields as a `ui_form()` (`alarm_form()`, SAVE; Repeat, Kind, Message/Program, Sound pull down; `af_changed()` does Repeat's Other..., Type..., playing the sound; a time already past is refused; an app's alarm can only be deleted) (`alarm_t.sound`: Plus42's tones, `/SOUNDS/*.wav` or `/MIDI/*.mid`; `alarm_sound()` falls back to the tones if the file is gone, and a song outlasts the 20 s). Not done from the HP-41: past-due alarms activating at OFF, the
  bypassed-alarm order, the catalog's T/D/R keys, catalog-2 function names. An alarm has rung on the hardware (the user, Nov 2026); the rest is unverified.
  Apps set message alarms too (`px->alarm_set()`/`alarm_clear()`, API 6, `app.c`): `alarms_add_owned()` keeps the app's name in `alarm_t.owner`
  ("" for XYZALM's), `alarms_remove_owned()` takes back only that app's; an owned alarm can't be a control or conditional one, here or in a file
  (`ALM3`). They ring like any message alarm, outside apps (`alarm_service()` doesn't run in one). Unverified on the hardware.
- Stopwatch (HP-41 Time Module; manual: HP-41CX vol. 2, section 17): the rules in `stopwatch.c` (no hardware; the clock passed in, the registers as functions; `tests/test_time.c`), the keys and the top line in `free42_app.cc`, REGS and the commands in `px_commands.cc`. SW/SWPT return ERR_INTERRUPTIBLE, and `free42_service()` doesn't call the core while `sw_showing`, so the keys are all the stopwatch's and a program goes on after EXIT. Counts from the always-on clock, so it runs through OFF; `rtc.c` tells it when the clock is set (`stopwatch_clock_moved()`). `/STOPWATCH.DAT` written at OFF if changed, deleted by Memory Clear. A negative time's zero is in `next_alarm_ms()`: it wakes the calculator from OFF, and shows as `ALARM_TIMER`, not in the alarm table. Unverified on the hardware.
- Standard screens (`ui.c`, `ui.h`; types and `ui_*()` in `px_app.h`, API 10): lists with soft-key tabs (only those that do something, in fixed places: F1 what ENTER does, ▲ ▼ on F4 F5 by themselves with more than one row, EXIT on F6 by itself -- the port's word, `ui_port_t.leave`: NULL "EXIT" (apps), the settings menu's `menu_ui` "BACK" on every screen (the user's choice; `CONFIRM()` too); an "EXIT" or "BACK" tab = the EXIT key), radio buttons, check boxes, pull-downs, forms (`px_ui_form_t`, a `changed()` callback), text (no tabs: the top row types A-F), numbers, the calendar, the time picker. No hardware: drawing and keys come through a `ui_port_t` -- the settings menu's (`menu_ui`, from `wait_key()`), apps' (`app_ui` in `app.c`), the template simulator's, and `tests/test_ui.c`'s (keys scripted, results checked; the ALPHA letters held to `keymap.h`). `ui_screen.c`: the display drawing both firmware ports and the apps' `fill()`/`text()` share. Every settings screen uses them: `show_message()`/`run_list()`/`choose()` take tabs (`TABS(...)`, a question `CONFIRM(verb)`), not hint lines. The rules: `ui_guidelines.html` (built with the manual from `tools/manual/ui_guidelines.template.html`); `UI_CHECK_TABS` (the menu_shots tool, test_ui) stops on a tab word not in its list or out of place -- add a new word there and in the guidelines. Settings forms (each change at once, no SAVE): Sounds (Key Sound a pull-down, the rest check boxes), Fonts (three pull-downs), Splash Image (Timer, Image a pull-down, Show Time a check box), Printer (Printer a pull-down, Screen Print one shown only with the HP 82240B and USB ESC/POS; a printer picked is confirmed in an alert, OK CANCEL); no tab is ever OK (ENTER instead; not in the word list); a font or image picked is previewed, kept only with USE. A screen of its own draws `ui_title()`/`ui_tabs()` and reads `tab_key()`; apps have both too (API 10, `PX_UI_TOP`/`PX_UI_BOTTOM` the space between), and `ui_view()`, long text a page at a time (`ui_view_leave()`: F6 named, "BACK" for an app's help, as Help and Info; ▲ ▼ EXIT; Help and Info use it), and `ui_dialog(icon, text, left, right)`, a classic b/w alert: the screen dimmed (checkerboard), a double-bordered box, a stop/note/caution icon (drawn in `ui.c`), buttons in the Hello screen's style, the left the default, ringed (the arrows move the ring, ENTER presses it; F1 the left, F6/EXIT/← the right) (apps, and Settings → Reset: OK CANCEL). The icon grid is the menu's alone. The template's `tools/sync_framework.sh` copies `ui.c`/`ui.h` into each app's `sim/`.
- Settings menu (`settings_menu.c`, SHIFT+EXIT): pages of 32x32 icons (`run_grid()`: 7 x 3 between a header bar
  -- the picked icon's name, date, time, shift sign -- and soft-key tabs in the small font, as Plus42's menu, for the top row, codes 1-6:
  ENTER ← ▲ ▼ → EXIT (BACK in the folders, and F6 is BACK on Alarms, Plus42, States, Settings, USB Drive, Help, Info: `run_page()`'s `leave`, `ui_view_leave()`, ui.c takes a BACK tab as EXIT; the user's layout; `ui.c`'s tab check lets ▲ ▼ sit on F3 F4 for it), with SHIFT on home and in the folders MOVE FAV/UNFAV (only where it applies) · · · BACK (only back to the plain tabs; an icon with neither keeps the plain tabs), moving DONE ← ▲ ▼ → CANCL (puts it back; EXIT too); a soft key acts by its tab's word (`grid_soft_key()`); `grid_step()`: ▲ ▼ and 8 2 rows; ← →, the keypad's ↑↓ and 4 6 the icon before / after, wrapping to the next row / the top; 5 = ENTER); scrolls by row; icons drawn as text in `menu_icons.c`). Home:
  Off, Settings (the gear: a page, `run_page()`, of Date & Time, Fonts, Sounds, Printer, Splash Image (Timer, Image, Show Time: `splash_clock`, settings version 15; off, no bar and no minute wakes),
  Sleep Timer, Menu Style, Reset: `free42_memory_clear_restart()`, Plus42's memory only (the user's choice: alarms, stopwatch,
  settings stay; written first, as at OFF), both copies dropped, then `watchdog_reboot()`, which brings no memory back), Plus42 (a page of States (the saved states listed, tabs SAVE LOAD RENAM DEL BACK), Programs (Plus42 `.raw`
  files in `/PROGRAMS` listed, tabs IMPRT EXPRT (one or all) RENAM DEL BACK; `free42_*_programs()` around the core's
  own import/export, the current directory's programs; `shell_message()` flags a file error)),
  Apps and Games (folders: `do_folder()`, a grid of that folder's `.bin`s, favourites left out, in places kept in
  `ORDER.TXT` in the folder as `/HOME.TXT` (`folder_list()`; new ones by name into the first blank);
  SHIFT then FAV (`grid_tab_fn`, `GRID_SECOND`) moves one to the home screen's first blank, its folder place left blank; there SHIFT then UNFAV on a
  favourite: back to its folder's first blank, its home place left blank), USB Drive (a page of icons, `run_page()`, a list in the List style: Connect to PC, Format Drive, Update Firmware),
  Info, Alarms, Help, and the favourites (`home_list()`); at first (`home_default[]`) Off, USB Drive, Plus42, Alarms,
  Apps, Games, Settings, a blank row, Help and Info at the right of the third; FAV takes the first blank (`home_free()`). SHIFT then MOVE (SHIFT then ENTER doesn't, the user's choice) moves a home or folder icon (`grid_moves_t`;
  Off pinned first): it's carried over the others, which stay put, and put down it swaps with what's there, so its old
  place is left blank (`HOME_BLANK`, names[] NULL) -- the user's wish; a row of
  empty places opens past the last while carrying; picking skips blanks (`grid_step()`). The places are `/HOME.TXT`
  on the drive, one line each after Off, by name (APPS/NAME, GAMES/NAME), an empty line a blank; a menu icon the file
  doesn't name goes where it is by default; a file without "Apps" is from before the folders (it named every app): ignored. The places
  (lists, files, FAV/UNFAV) are `menu_places.c`, no hardware: written through `safefile.c` (a reset mid-write had cut
  them), only when something moved (no wear levelling), names matched without case; folders compared by text, not
  pointer (`APPS_DIR` in two files can be two copies). `tools/menu_shots/run.sh` checks the HOME.TXT and ORDER.TXT its
  keys leave (`places`), so a change to the keys there changes those lines too. `px_settings.menu_style` List (settings version 13) is the menu as it was: one
  `run_list()` of everything (`run_text_menu()`), Apps and Games as lists; changing it (`style_changed`) unwinds every
  screen and `settings_menu_run()` opens the other style. A new item shifts the key scripts: `tools/menu_shots/run.sh` and the tour's
  storyboard (`~/Documents/PX-42F Media/scripts/settings_tour.txt`).
  A boot that dies loading sounds moves the folder to `/SOUNDS.BAD` (POWMAN scratch 6).
- Apps and Games (`app.c`, `appfile.c`, the contract `px_app.h`): native `.bin` apps in
  `/BIN/GAMES` and `/BIN/APPS` (`app_folders()`), each with its folder next to it for its files, on the home screen:
  the app's own (`px_app_icon_t`, "PXIC" + 32x32 right after the header, part of the image so an older firmware
  still runs it; `app_icon()` reads only the file's start; the template's `icon.txt` -> `pxicon.py` -> `.px_icon`
  section), else `MENU_ICON_FRAME` with its first letter.
  A `.bin` = 24-byte header (magic PXAP, API version, image/bss size, Thumb entry, CRC-32 of the
  rest) + code linked at `PX_APP_BASE`: from API 9, 160 KB at 0x20058000 (the 32 KB under
  `RETAINED_RAMSTATE` and the 128 KB that keeps the memory while off); API 1-8 apps the top 128 KB at
  0x20060000 they were linked for (`app_base()`, `app_ram()` in `appfile.c`). `app_run()` checks the whole file first (`app_header_check()`, CRC), then
  `free42_save_to_drive()` (memory to `/OFFSTATE.p42`, RAM copy dropped: a crashing app loses
  nothing, `free42_init()` takes the file), copies it, zeroes bss, calls entry(&api) on core 1;
  after, `free42_memory_to_ram()`. The table (`px_api_t`) only grows: add at the end, raise
  `PX_APP_API_VERSION`. Keys reach apps as places RxCy with down/up events
  (`keyboard_held()`, one change per call); ON = `PX_KEY_QUIT` after the firmware's "Quit NAME?" dialog
  (`ask_quit()` in `on_pressed()`, app.c: STAY swallows the press; apps with `on_key(1)` ask themselves) (held 2 s: off, no question), as do the Sleep
  Timer and an empty battery. `api_ui_dialog()` puts the app's screen back after it and stops `millis()` meanwhile (`run.paused_ms`); keys pressed in it are swallowed when let go (`run.swallow`); the template's `sim.c` does the same (`SIM_ASK`). `sync_framework.sh` now covers every sibling with a `px42f/` (PX-*, Contacts too). Every API call that waits ends with `CKPT_APP`, so a hang in the
  app's own loop shows APP, and returns at 150 MHz (`wait()`): frames take the same time, key
  or not. The developer manual is `../PX-42F-App-Template/framework_manual.html`. The app projects are siblings of this repo, not in it:
  `../PX-42F-App-Template` (the framework: `px42f/` = copy of `px_app.h`, runtime, linker script,
  `pxbin.py`; `sim/` = the app on the Mac against a stand-in table, PNG shots; VS Code tasks),
  `../PX-42F-Snake`, `../PX-42F-Gold-Runner` (a Lode Runner clone: levels in `levels/builtin.txt`, `LEVELS.TXT` on the drive replaces them), `../PX-42F-IR-Remote` (all public on GitHub, alxgarza/PX-42F-*; the framework's `tools/sync_framework.sh` keeps their `px42f/` and `sim/` the same as the template's and `px_app.h` as this repo's) (the IR remote, moved out of the firmware: `.ir` files
  in `/BIN/APPS/IR`, Flipper's 14 protocols, `make test` against Flipper's encoders). After
  changing `px_app.h`, copy it into each project's `px42f/`. API version 10 added the standard screens (`ui_*()`, above); 9 moved apps to 160 KB from 0x20058000 (`appfile.c` still runs 1-8 at 0x20060000; the template's `app.ld` and `pxbin.py`, which now reads `PX_APP_BASE`/`PX_APP_RAM` from `px_app.h`). API version 8 added `sprite()` (`px_draw_sprite()`: OR / XOR / masked, mirrored, a byte at a time; `tests/test_draw.c` checks it against the pixel-by-pixel way everywhere). API version 5 added the shapes (`box`, `ellipse`, `thick_line`, `polygon`): `px_draw.c` draws into any `px_canvas_t`, so Plus42 can use it for its own display later; `tests/test_draw.c` has them as pictures; the template's `sim/` has a copy (`sync_framework.sh`). API version 3 added `on_key()`
  (a short ON press as `PX_KEY_ON` for an app with its own exit; held 2 s still off), version 2
  decimal numbers (`appdec.c`): `px_dec_t` is a `BID_UINT128`, each `PX_DEC_*` op one call into the
  libbid Plus42 already links (by reference, global rounding/flags -- Plus42 never reads the
  flags nor changes the rounding, and apps can't), so they cost almost no flash. The
  simulator's stand-in is doubles (`sim/sim_dec.c`). Also `../PX-42F-MIDI-Player` (the MIDI player, local git only so far), `../PX-42F-BASIC`, PX-BASIC (Palo
  Alto Tiny BASIC on those decimals; `make test` runs programs on the Mac), `../PX-42F-Spreadsheet`
  (SPREADSHEET: A1-Z999, Excel's formulas on those decimals, CSV sheets in its folder; local git only;
  `make test` runs the engine on the Mac).
- IR LED (GP14, high = on, through a transistor): driven by PIO0 (`irtx.c`/`irtx.pio`), not
  PWM: GP14 shares PWM slice 7 with the speaker. PIO0's clock is on only while sending. Apps
  send (`px->ir_send()`), and so does the HP 82240B printer (Settings → Printer): `hp82240.c`
  makes its bytes (text in ISO 8859-1, characters it lacks as graphics cut from Plus42's print
  bitmap; PRLCD per `screen_print` (Settings → Printer → HP 82240B → Screen Print): Portrait (`SCREEN_PRINT_UPRIGHT`) = `free42_draw_paper()`
  redraws the screen into the framebuffer's top left in font_mini (header bar, lines, dotted lines,
  Plus42's own menu row; only ≤27 columns, not plots), sent as rows across (~9 lines); Landscape (`SCREEN_PRINT_AS_IS`, and
  whatever doesn't fit) = the panel's framebuffer, redrawn by `free42_draw_screen()`, turned a quarter
  as Plus42 turns a too-wide display, 32 lines of 8 dot columns; the screen is redrawn after either) and frames (HP's "Technical Interfacing Guide", in
  `tests/test_hp82240.c`); `irtx_send_redeye()` sends them with irtx.pio's `redeye` program,
  every half-bit exactly 448 cycles (the printer allows 1%); `printer.c` paces them to the
  printer's 200-byte buffer at 1.8 s a line (it can't answer): a listing waits in `free42_service()`
  (`printer_ready()`), the keys still read, EXIT stops it; PRLCD blocks core 1 until it's sent or EXIT/R/S stops it between lines (`free42_print_stop()`, a key already held when it began doesn't count). The user has
  printed on the real printer: PRP, PRLCD and stopping it work. Apps print too (API 7: `printer()`, `print_ready()`, `print_text()`, `print_screen()` in `app.c`): text through `free42_print_app_text()` (`shell.cc`: Plus42's glyphs for the characters the 82240B lacks, 24 a line; UTF-8 on USB), the screen through `printer_hp82240b_screen()` or `printer_write_screen()`; Plus42's own printing is unchanged. App files opened "w" are crash-safe (`app.c`: written to name.NEW, swapped in by `safefile_take_new()` at a good `api_close()`, `safefile_recover()` at the next read; `NEW_PATH_MAX` in `safefile.c` fits an app's path). No IR receiver on the board (learning codes would need one).
- USB thermal printer (Settings → Printer → USB ESC/POS, `PRINTER_ESCPOS`; a pick is confirmed in a `ui_dialog()` note, OK CANCEL, CANCEL puts the one before back; Screen Print for it too: Portrait the framebuffer upright, Landscape `escpos_turned_row()`, turned as the HP's and 3x): a 58 mm ESC/POS printer on the USB-C port
  (tried: YICHIP "YC Printer demo" 28E9:0289, printer class, 384 dots), the PX its USB host only while printing, no 5 V
  given (the printer has its battery). `escpos.c` (no hardware, `tests/test_escpos.c`; with a file name it writes a sample
  job for `tools/usb_printer_test/yc_print.py --file`): text in the printer's font, only bytes code pages 437 and 850
  share (`ESC t 0` is ignored: it stays in 850); a line with any other character is a picture (`GS v 0`) of Plus42's
  line bitmap, 2 x 3 per dot = the font's 12 x 24 cell, 24 rows, 6 blank on top after another picture (the printer
  leaves ~8 dots more after a picture; text lines 30 apart, `ESC 3`); `ESC *` inline images were tried: each went on
  its own line and lost the text before it. PRLCD the framebuffer upright (`GS v 0`). A Mac sees this printer only
  through its own cable (USB-A), not USB-C to USB-C (no CC resistors?); the PX doesn't care. Core 1 puts
  it in printer.c's ring (`printer_escpos_*()`, waits for room; `printer_ready()` paces listings); core 0's
  `usbprint_poll()` (`usbprint.c`): first bytes → `stdio_usb_deinit()` + `tuh_init(0)` (not over a sound), the
  printer's bulk OUT found, `ESCPOS_INIT`, the ring sent, `ESCPOS_FEED` after 1.5 s idle, back to device
  (`stdio_usb_init()`, `usbprint_device_started()`) after 30 s (the user's choice), at once for Connect to PC
  (`usbprint_release()`); no printer in 5 s → dropped, and prints for 10 s after (`usbprint_unavailable()`).
  `usbprint_shutdown()` at power-off stops whichever role. TinyUSB 0.18's RP2 host never resets the bus
  (`hcd_port_reset()` empty, RESET_BUS lasts 2 us): `--wrap`ped with SE0 through the PHY override, the connect
  interrupt masked, then waiting for SPEED (20 ms wasn't enough); and a second reset before Set Address
  (`--wrap=hcd_setup_send`, as Windows/macOS) -- without them this printer stalled Set Address. The SDK's TinyUSB is
  unpatched (a 64-byte first descriptor read wasn't needed). `CFG_TUH_API_EDPT_XFER 1` or `tuh_edpt_xfer()`'s
  callback never runs. Apps: `PX_PRINTER_THERMAL` (3). The standalone test that found all this: `tools/usb_printer_test/`.
  Printing worked from the test firmware (Nov 2026); the firmware's own path is unverified on the hardware.
- Clock chip (board V1.1 only): Epson RA8900CE UB (X1B000271A00500, ±5 ppm, temperature compensated; Epson's
  application manual ETM44E) on I2C1, GP26 SDA / GP27 SCL, address 0x32, 100 kHz (the pads' pull-ups on too); its
  /INT (open drain) on GP17, input + pull-up (UART0 RX before: never read, dropped from `init_serial()`).
  `rtc_clock_init()` looks for it (`rtc_has_chip()`; V1 has nothing there: the pins go back to pull-downs);
  `ra8900.c` the registers and the wake countdown (no hardware, `tests/test_time.c`). The always-on timer stays the
  clock everything reads, kept to the chip: at each boot and hourly on (`rtc_chip_poll()`, main loop) into the chip's
  second at once (`ra8900_settle()`, the middle if far off), then exact at its next turn: the update interrupt (UIE)
  pulses /INT, `edge_irq()` (core 1, a raw GPIO handler) sets the timer (`!STATUS` measures it: +0 ms on the board).
  While off the chip wakes it instead of the timer's alarm (`rtc_chip_wake_at()` in `power_down()`, GPIO wake 1, falling
  edge; `PWRUP_CHIP_BIT`): its countdown (`ra8900_wake_timer()`: seconds up to 4095, minutes beyond, a turn under 100 ms
  away skipped) pulses /INT as its second turns, so the wake sets the timer to that second + `time_us_64()`
  (`rtc_chip_wake(true)`). With the chip: no 10-minute wakes, no LPOSC measuring at OFF (`LPOSC_NOMINAL_HZ`), so it
  wakes only for alarms, the stopwatch's zero, the splash's minute. Setting the time writes the chip (`chip_set()`,
  at the timer's second turning, with RESET); a chip with VLF set (lost the time) gets ours. With it a RESET or a
  power-on keeps the time. With it the date is held to 2000-2099, the years it counts (`chip_can_hold()`: `rtc_set_date()`,
  `rtc_add_ms()` refuse others), and Settings → Date & Time leaves out Time Calibration (the timer it trims is reset to
  the chip hourly). Info's RTC line says Present (V1.1) or Not Present (V1), `!STATUS` the chip's time.
  I2C1's clock is off (`never_used`, `cpuclock.c`) except around each access (`chip_bus()`/`chip_bus_end()`):
  with it off `i2c_init()` hung forever, a blank screen on the first V1.1 boot. Not used: FOUT, its temperature,
  its alarm registers (minutes only; the countdown does it). Found, set, read and the exact catch shown on the
  V1.1 board, an alarm woke it from OFF and the splash's minute wakes crossed an hour (the user, Nov 2026); RESET /
  power-on keeping the time and the current are unverified. Two lessons from it: the power manager's GPIO wake
  latch (PWRUP STATUS) is write-1-to-clear and the SDK's `powman_enable_gpio_wakeup()` "clears" it with a 0
  (`clear_chip_edge()` in power.c); and the wakes while off and the power-down must run at the slow speed
  (`cpuclock_set_fast(false)`, which the LPOSC measuring used to do): left at 150 MHz the splash redraw hung now and
  then (watchdog, `OFF_WAKE_SPLASH`). The off path's checkpoints are `CKPT_OFF_*` (the watchdog's scratch is
  powered down while off: a hang there used to show NONE).
- CAS (Giac 2.1.0, Bernard Parisse, GPL3): a program of its own in its own repo, `../PX-42F-CAS` (local git), never
  linked with this firmware (Plus42 is GPL2 only; a licence to link was asked of both authors, Nov 2026). It builds
  from this repo's board code by path (`PX_DIR`: display.c, display_draw.c, keyboard.c, cpuclock.c, two fonts,
  `cas.h`; its Mac test also pngwrite.c and `tools/menu_shots/stub/`; `display.c`'s `PX_CAS_PROGRAM` keeps its screen in its own RAM) -- a change to those must still build
  there. Flash (`flash_layout.h`): firmware 0-4 MB, drive 4-11 MB (was 12 MB to the end: a volume formatted bigger
  is formatted again, `statefs_reformatted()`, bootlog), CAS 11-16 MB. Handoff (`cas.h`, `cas.c`) by restart, a word
  in retained RAM (`RETAINED_CAS_HANDOFF`): the CAS icon (home, third row; List style last) or `!CAS` saves what OFF
  saves (`free42_save_for_restart()`: the memory to the drive, the RAM copy dropped), leaves `CAS_HANDOFF_TO_CAS`,
  `watchdog_reboot()`; `main()` first thing (`cas_boot_check()`) jumps to the CAS's vector table at 11 MB; EXIT on
  its empty line leaves `CAS_HANDOFF_FROM_CAS` and restarts: `cas_returned()` makes the boot look like ON from OFF.
  `tools/flash.sh` loads `../PX-42F-CAS/build/PX-42F-CAS.uf2` too when it's built. The rest (Giac's build, its
  patches, the screen, measurements) is in that repo's README.
- Battery: GP28/ADC2, 1M/1M divider. Free: GP23-25, 29.

## Architecture

- **Core 1** runs everything: Plus42, display, keyboard, main loop (`core1_main` in
  `main.c`, 32 KB stack `core1_stack`). **Core 0** only runs USB (pico_stdio_usb background
  IRQ at lowest priority), the printer pump, the sounds (`speaker_core0_poll()`) and its WFE
  idle loop; core 1 parks it in RAM for flash writes (`flash_safe_execute`) -- the DMA keeps
  the sound going meanwhile (a write longer than a block replays the last 32 ms).
- The drive (FatFs + flashdisk cache) is shared by both cores (core 0 reads sounds): every
  `statefs.c` function and the FILE callbacks hold `fs_mutex` (`FS_LOCKED()`, recursive,
  released by `cleanup`). Nothing else may call FatFs or flashdisk directly. `fil_read()`
  feeds the watchdog only on core 1.
- USB composite: CDC0 console ("PX-42F Console", `!` commands: `!HELP`/`!?` (also printed
  whenever a terminal opens the port), `!STATUS`, `!BOOTLOG`, `!PRINTER`, `!PRINTTEST`, `!SCREENSHOT`,
  `!SETDATE`, `!SETTIME`; other characters are keypresses), MSC drive "PX-42F", picotool reset,
  CDC1 printer.
- Flash (`flash_layout.h`): program 0-4 MB-8 KB; settings in two sectors written in turn, 0x3FF000 and 0x3FE000
  (`settings.c`, the record's layouts in `settings_record.c`: a count at the page's end picks the newer; a power cut mid-save keeps the other); FAT16 disk 4-11 MB, the CAS 11-16 MB
  (`flashdisk.c` write-back cache, `statefs.c` FatFs; Plus42's file I/O routes through
  `statefs_fopen`).
- Clocks (`cpuclock.c`): 150 MHz @1.10 V while working, 48 MHz from PLL_USB @1.00 V when
  idle; clk_peri fixed at 48 MHz (display SPI 24 MHz). Full speed whenever a computer is
  connected over USB.
- No auto power-off (Sleep Timer) while a computer is connected over USB — OFF powers USB
  down, and then flashing needs BOOTSEL+RESET.
- Idle (`idle.c`): on battery, sleep until a key interrupt / next minute / 1 s
  (`keyboard_wait_any`, a core-1 hardware alarm + column IRQs); 10 ms polling on USB.
- OFF (`power.c`): POWMAN power-down, only SRAM bank 1 + always-on domain kept, ON key
  (GPIO level-low) wakes = full reboot. Calculator memory is saved to retained RAM
  (`ramstate.c`) and restored by `free42_init()`; too big for that, to `/OFFSTATE.p42` (`memory_to_drive()`), and if that fails too it stays on and says so, unless the battery is empty. With a Splash Time, a POWMAN timer alarm
  wakes the chip each minute: `power_off_wake()` (first thing in `main()`) redraws the
  clock bar and powers down again. It also wakes every 10 min while off (`RECAL_MS`) to measure
  the LPOSC again (~70 ms at 1.00 V, scaled by the power-off's two-voltage measurement kept in
  POWMAN scratch 4-5): the rate follows temperature, which left the clock 0.4% off overnight.
- Clock (`rtc.c`): POWMAN always-on timer, ms since 1970. On: from the crystal; off: from the
  LPOSC (this board's: ~27.6-27.8 kHz, not 32.768), measured with the frequency counter at each
  power-off (`rtc_lposc_hz_at()`, ~0.5 s, feeds the watchdog). The LPOSC's rate follows its
  supply voltage (datasheet 8.4.1: up to ±20%), and while off that's `POWER_OFF_MV` (0.90 V),
  below anything the processor runs at: measured at 1.10 V alone, the clock gained 2.4% while
  off. So it's measured at 1.10 V and 1.00 V and extended to 0.90 V along 72% of the straight
  line through them (`LPOSC_CURVE_PERCENT`): the straight line itself left the clock 0.8% slow
  while off (two runs). A key-OFF used to measure at 1.10 V (clock +5% while off), a Sleep
  Timer OFF at 1.00 V (+2%). The V1.1 board has an RA8900CE clock chip (below), which corrects it. `time_trim` (tenths s/day)
  adjusts both rates. Setting the time exactly twice ≥6 h apart measures the drift
  (`cal_drift`), offered in Settings → Date & Time → Time Calibration → Measured (which
  also takes the exact time, HH:MM:SS, to measure). Date/time formats are
  Free42's (DMY/MDY/YMD, CLK12/CLK24) mirrored in px_settings; the extra DOW-MON-DAY format
  ("FRI-OCT-9") is MDY to Free42. A full chip reset (RESET button, battery) loses the time on V1; V1.1 takes it back from the clock chip.
- Settings written from frequent/program-driven paths (SETIME, DMY...) use
  `settings_save_later()` — flushed at OFF — so a looping program can't wear the flash.
- Retained RAM layout: `retained.h` (framebuffer 0x20057000, RAM state 0x20060000-top, the 32 KB between free until an app runs);
  `HEAP_LIMIT` in `CMakeLists.txt` must equal `RETAINED_BASE`. It only holds because
  `shell.cc` overrides `_sbrk()`: the SDK's grows the heap to the end of RAM, over the retained
  RAM. `shell_get_mem()` (MEM) = heap left below `__HeapLimit` + malloc's free blocks.

## Hard-won rules (each one cost a debugging round)

- **Deep sleep (SLEEPDEEP) only inside core 1's idle wait** (`cpuclock_deep_sleep_begin/end`).
  Left on permanently it corrupted both cores' stacks around flash writes.
- Sleep clock gating (`keep_in_sleep` in `cpuclock.c`) must keep the memory system
  (SRAM, bus fabric, XIP, ROM, SIO). Anything that must run while both cores sleep needs
  its clock kept — this bites even outside the idle wait (any WFI/WFE on both cores, e.g. on
  battery with no USB traffic): `speaker_play()` keeps the PWM and DMA clocks for each sound (the DMA
  block interrupt is what ends its WFI); the ADC is enabled only for each battery reading.
- Don't use the SDK's `best_effort_wfe_or_timeout()` for long waits on core 1: nothing
  wakes it on battery. Use a core-local hardware alarm (see `keyboard_wait_any`).
- Don't link `pico_low_power` (its boot hook clears persistent data), and don't use the
  SDK `__persistent_data()` macro (without that library it silently lands in `.bss`).
  Retained data lives at fixed addresses from `retained.h`.
- Display control pins: set level and direction **before** `gpio_set_function()` — after a
  power-down the pad is isolated holding its level, and RST must never dip.
- Before powering down: `stdio_usb_deinit()` + USBCTRL reset (~400 uA otherwise), unlock
  the VREG (`vreg_ctrl` UNLOCK) or it won't enter low-power mode, quiesce both cores' NVICs,
  disable the watchdog last. `powman_set_power_state()`'s timeout is too short — wait for
  WAITING/CHANGING.
- The core voltage (POWMAN VREG) survives every reboot except a power-up. The SDK only
  restores it at boot because of `SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST/MIN` in CMakeLists —
  without them, wakes and reflashes ran 150 MHz at the idle 1.00 V.
- A hang is `watchdog_enable_caused_reboot()` (our `watchdog_enable()` left the SDK's magic in
  scratch[4]), not `watchdog_caused_reboot()`: that one is also true after the boot ROM leaves the
  bootloader (picotool, a UF2 copy) and after `watchdog_reboot()` — those showed "WATCHDOG RESET"
  with a stale checkpoint (FS_OPEN, PROGRAM_SLICE) and made `free42_init()` treat the boot as a
  crash. The power manager refuses a power-down while a wake-up is pending: ON pressed during a
  wake while off (a turn-on: reboot, no report), or the alarm already due (set a moment later,
  asked again); anything else reports "OFF REFUSED" with the pending bits (`crash_note_off_refused()`). A checkpoint
  around one call (file open, sound) puts the previous one back (`debug_checkpoint_save/restore`).
- `POWMAN LAST_SWCORE_PWRUP` is a bitmask (bit 1 = ON key / pwrup0, bit 6 = alarm).
- POWMAN scratch: 0-5 the wakes while off (`power.c`), 6 sounds-loading guard, 7 splash-PNG-
  and font-loading guard (`main.c`, one after the other, different flag values).
  Watchdog scratch: 0-1 debug checkpoint, 2-3/5-7 crash record, 4 = SDK.
- The RAM-kept memory is re-validated after a good load (`ramstate_keep()`), so a crash
  comes back with the memory as of the last ON — but not twice in a row (`after_crash`).
  The RESET button (RUN pin) is Memory Clear: a boot that isn't ON-from-OFF and wasn't the
  watchdog's (`!woke_from_off && !watchdog_caused_reboot()`; crashes go through the watchdog,
  a RUN-pin reset clears its record; a power-up counts too; a flash or Update Firmware also
  ends up here -- the boot log shows them as RESET -- and that's wanted: a flash clears memory). Not POWMAN's `CHIP_RESET` `HAD_RUN_LOW` -- it stays set through every restart after
  a RESET press -- and not a stopped always-on clock (RESET didn't reliably leave one, so the
  Hello wait never came). Memory Clear = `ramstate_discard()` and
  `/OFFSTATE.p42` deleted (the overflow for a memory bigger than the 128 KB RAM copy); every
  other restart brings the memory back. `!STATUS` prints the last reset's cause.
  `/BOOTLOG.TXT` (`bootlog.c`, last ~2 KB, also `!BOOTLOG`): a line per start that isn't a plain ON with the memory
  from RAM (those were nearly every start, each rewriting the FAT and root folder: no wear levelling) -- what restarted
  it, the RAM copy's status (`ramstate_status()`: ok / empty / bad length / BAD CHECKSUM), whether a drive copy existed,
  where the memory came from. For "why did the memory clear?".
  After RESET the Hello screen waits for a key (only dismisses), a byte on the console (looked at with
  `tud_cdc_n_available()`, left for the main loop: a `!STATUS` still works) or the Sleep Timer (`hello_wait()`).
  Off from the settings menu (an app ended by an empty battery too) checks the battery at once, so an empty
  one gets no off sound.
- A drive that isn't FAT is only formatted if it's blank (0xFF); otherwise it's left alone,
  unmounted (USB Disk mode still exposes it; Settings → USB Drive → Format Drive, `statefs_format()`,
  formats it on request). The calculator powers off by itself below
  ~3.45 V (`battery_empty()`; the ADC can't read lower: it measures against the 3.3 V rail).
- Console writes time out after 50 ms (`PICO_STDIO_USB_STDOUT_TIMEOUT_US`): the SDK's
  500 ms default outlasted the watchdog's 100 ms then.
- The watchdog's window is 5 s (`WATCHDOG_MS`), not the 100 ms it was: one Plus42 command runs to its end
  without coming back (2000 N! reset the calculator, COMB has a longer loop). Apps' contract (`px_app.h`) stays 100 ms.
- picotool's reboot-to-BOOTSEL is the boot ROM arming the watchdog for 10 ms; core 1 feeding
  the watchdog postponed it forever (flashing needed retries or the BOOT button). main.c wraps
  `rom_reset_usb_boot_extra` (`--wrap`) to stop core 1 first.
- `free42_settings_requested()` consumes the request; use `free42_settings_pending()` to peek.
- Plus42 calls happen on core 1 only. `shell_blitter()` only writes the framebuffer;
  `free42_app.cc` sends it to the panel at the end of each key/timeout (`refresh()`), and
  every 250 ms while a program runs.

## Debugging

- A crash shows on the boot screen as `FAULT coreN PC=… LR=… A=… C=… S=… <checkpoint>`;
  `/fault` decodes it (needs the same build that was flashed). C = CFSR.
  The report stays up until a key is pressed (a minute at most).
- `debug_checkpoint()` codes (`debug_checkpoint.h`) show where a watchdog reset happened.
- `/status` runs `!STATUS` over the console (needs the console port not held open by a
  terminal app).
- `/screenshot` (`tools/screenshot.py`) fetches the screen as a PNG over the console
  (`!SCREENSHOT`): look at what the firmware drew instead of asking for a photo. Calculator
  screen only — the settings menu doesn't read the console.

## Power baseline (measured on the board, battery)

- Idle 4.4 mA, running 25 mA, settings menu ~idle, OFF ~135 uA (no splash),
  ~200 uA with the splash showing. Pico 2 reference (SDK): power-down with one SRAM bank
  140 uA, all SRAM off 80 uA; sleep 6.9 mA; dormant 3.7 mA.
- Ideas not done yet: keep the RAM state in the 16 KB XIP cache instead of SRAM bank 1
  (~-40 uA off), flash deep power-down, LPOSC calibration for the clock while off,
  printer to disk file / IR.

## Style

Match the surrounding code: C11, 4-space indent, block comments that explain *why*
(hardware facts, measured numbers, past failures). No new dependencies.

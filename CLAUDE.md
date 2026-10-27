# PX-42F-Plus

Plus42 (Thomas Okken's enhanced HP-42S, built on Free42) as the firmware of the **PX-42F**,
a custom RP2350A calculator board. The Plus42 core is in `src/core/` (upstream copy in
`plus42-upstream/`, from https://codeberg.org/thomasokken/plus42desktop; not tracked);
everything else at the top level is the board: display, keyboard, power, USB, storage.
The sister repo PX-42F (remote `px42f`) runs Free42 on the same board: board-code fixes
can be cherry-picked between them.

- PX changes to the core, kept small so a new Plus42 can be dropped in: `my_fopen` etc.
  (`core_main.cc`) and 5 rows (`core_main.cc`) and ATOP (`core_globals.cc`) at Memory Clear under `PX42`, the screen hooks under `PX42`
  in `core_display.cc`, SETDATE/SETIME (`core_commands7.cc`, `core_tables.*`,
  `shell.h`). `free42_app.cc` and `shell.cc` are the PX's own (names kept from the Free42
  port). Plus42 needs C++ exceptions (`std::bad_alloc`) and `PICO_MALLOC_PANIC=0`.
- Plus42 can't read Free42 `.f42` states (different magic); state files are `.p42`.

## Build

- `/build` skill, or by hand:
  `cmake -S . -B build -G Ninja` (only if `build/` is missing), then
  `~/.pico-sdk/ninja/v1.13.2/ninja -C build` — ninja is **not on PATH**; use this one
  (it's the version CMake configured with; v1.12.1 complains about the build log).
- Pico SDK 2.3.0 at `~/.pico-sdk/sdk/2.3.0`. Board header: `boards/px42f.h` (`PICO_BOARD px42f`).
- Output: `build/PX-42F-Plus.uf2`, `build/PX-42F-Plus.elf` (+ `.elf.map`, `.dis`).
- Host tests: `tests/run.sh` builds `tests/test_parsers.c` on the Mac (ASan/UBSan) and runs it:
  the PNG decoder (`png1bit.c`), WAV parser (`wav.c`), font sheet slicer (`fontsheet.c`) and
  `.ir` reader (`irfile.c`) against good, truncated and damaged files, plus the built-in sounds
  and fonts; the PNG writer (`pngwrite.c`) read back by the decoder and checked by Python's
  zlib; and the IR encoders (`irproto.c`) against Flipper's (`tests/ir_vectors.h`). Run it after
  touching any of them — they read files users drop on the drive, and a parser bug there
  freezes or resets the calculator.
- User manual: `manual.html` (the screen, fonts, settings; not a Plus42/HP-42S manual), built by
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
  A reflash reboots: the calculator's memory is lost unless it was turned OFF first.
- `/flash` (`tools/flash.sh`) loads the build into the connected calculator and waits for
  it to come back. On the calculator: Settings → USB Drive → Update Firmware saves the memory to
  `/OFFSTATE.p42` (RAM copy discarded) and calls the ROM's update mode from core 1
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
  the waveform (0-100%, ~200 mA peaks). `speaker.c` only plays duty streams: a 32 kHz
  sample interrupt (PWM slice 0 as a timer) fed from a ring buffer.
- Sounds (`sounds.c`): WAV files in `/SOUNDS` on the drive — Key Sound choices, loaded into
  RAM: a set `KEY/<name>/ENTRY.wav, OP.wav, ENTER.wav` (≤100 ms each; `free42_key_sound()`
  picks typing / operation / ENTER from the calculator's state; built-in set "Typewriter": a letter key, a heavier key, a brighter key) or one
  `KEY/<name>.wav` for every key (≤250 ms), `BOOT.wav` (every start but ON from OFF; no setting, only the file), `ON.wav`, `OFF.wav`, `TONES/TONE0-9.wav`,
  `ERROR.wav` (Free42's squeak) — those streamed, any length. Any PCM/float WAV, normalized
  and converted on the calculator. Built-ins are the same WAVs embedded in `sounds_data.c`,
  generated by `tools/make_sounds.py` (rerun after changing a sound; `--wav DIR` writes
  them out to listen to; `tools/px_sound/run.sh [DIR] [-ms N] [FILE.wav...]` writes what the speaker
  gets instead: sounds.c's own duty stream as 32 kHz WAVs, default `~/Documents/PX-42F Media/PX Sounds`); any missing from the drive is written at boot, and one still as written
  is updated when its built-in changes (checksums in `/SOUNDS/BUILTIN.TXT`); a built-in plays
  when its file is missing. Settings → Sounds: Key Sound, On / Off Sound.
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
  battery, angle (° π G: ∡ is HPOLAR's), shift, ↑↓, ▶ running. The core's `HEADER_SHOWN` /
  `HEADER_ROWS` (PX42, `core_display.h`): the header takes no display row. Skin flags 3: CATALOG's first row is
  FCN … MEM, as on the HP-42S. Every font code is the HP-42S character (0-31: ÷ × √ ∫ ▒ Σ ▶
  π ¿ ≤ LF ≥ ≠ ↵ ↓ → ← µ £ ° Å Ñ Ä ∡ ᴇ Æ … Esc Ö Ü ^ •; 94 ↑, 126 ~).
- Fonts (`fonts.c`, Settings → Fonts, `/FONTS/LARGE|SMALL|MINI`, PNG sheets 175x135,
  128x96, 96x64): the calculator screen's three layouts, the status bar and menu tabs
  (small). `/FONTS/BUILTIN/LARGE|SMALL|MINI.png`: the built-in fonts drawn as sheets
  (`font_sheet_draw()`), read-only (FatFs `FF_USE_CHMOD`), rewritten at `fonts_init()` only
  when they don't match. Settings version 9 added the mini font; 6-8 are still read. The settings
  menu, boot/crash screens and splash clock use the built-in 8x12/10x16 fonts.
- Held keys (`keyhold.c`, one place for both key loops — main.c and the settings menu): EXIT
  2 s = off; SHIFT and "−" (PRINT) together 0.5 s = a screenshot (`screenshot.c`): the screen
  as it was when SHIFT went down, to `/SCREENSHOTS/<date>-<time>.png` (250x122 1-bit PNG,
  uncompressed, ~4 KB; usable as a splash image), Free42's shift put back. Not in USB Drive
  mode (the computer has the drive). SHIFT held ≥0.3 s then an arrow, in ALPHA: BST/SST
  (`free42_shift_arrow()`). Add new hold gestures there, not in the loops.
- ALPHA keyboard, as the DM42's (`free42_app.cc`): `px_settings.alpha_mode` [ ] HP-42S keys /
  [a] / [A] the printed letters (`alpha_key_matrix`); SHIFT then ↓/↑ switches, remembered
  (`settings_save_later()`), badge in the status bar. In [a]/[A]: SHIFT+letter = same letter,
  SHIFT+digit = digit, SHIFT+÷×−+ = symbol, R/S runs and SHIFT R/S = '?', SHIFT+top row = soft key.
  First ALPHA ever starts in [ ]; numeric local labels (LBL 01) need [ ].
- Settings menu (`settings_menu.c`, SHIFT+EXIT): Off, IR Remote, Date & Time, Sounds, Fonts, Sleep Timer,
  Splash (Timer, Image), Printer, States (save/load/rename/delete), Programs (Plus42 `.raw`
  files in `/PROGRAMS`: import, export one or all, delete; `free42_*_programs()` around the core's
  own import/export, the current directory's programs; `shell_message()` flags a file error),
  USB Drive (Connect to Computer, Format Drive), Info. A new item shifts the key scripts: `tools/menu_shots/run.sh` and the tour's
  storyboard (`~/Documents/PX-42F Media/scripts/settings_tour.txt`).
  A boot that dies loading sounds moves the folder to `/SOUNDS.BAD` (POWMAN scratch 6).
- IR remote (`remote.c`, Settings → IR Remote): `/IR/<name>.ir`, Flipper's format plus
  `button: RxCy` (the key's place: row 0-6, column 0-5; R6C0 = ON is reserved) and `label:`
  (`irfile.c`). The calculator shows a map of the keys with labels; a key sends its code, and
  repeats while held. Protocols (`irproto.c`): Flipper's 14 (NEC/NECext/NEC42/NEC42ext,
  Samsung32, RC5/RC5X, RC6, SIRC/15/20, Kaseikyo, RCA, Pioneer) with Flipper's exact timings
  and repeats — `tests/ir_vectors.h` is made from Flipper's encoders by
  `tools/ir_vectors/make.sh`; regenerate after touching a protocol. Raw codes aren't sent.
  The LED (GP14, high = on, through a transistor) is driven by PIO0 (`irtx.c`/`irtx.pio`), not
  PWM: GP14 shares PWM slice 7 with the speaker. PIO0's clock is on only while sending.
  The keyboard matrix has XEQ and TAN on row 7; files use where keys sit (R0C5, R1C5).
  No IR receiver on the board (learning codes would need one).
- Battery: GP28/ADC2, 1M/1M divider. Free: GP23-25, 29.

## Architecture

- **Core 1** runs everything: Plus42, display, keyboard, main loop (`core1_main` in
  `main.c`, 32 KB stack `core1_stack`). **Core 0** only runs USB (pico_stdio_usb background
  IRQ at lowest priority), the printer pump and its WFE idle loop; core 1 parks it in RAM
  for flash writes (`flash_safe_execute`).
- USB composite: CDC0 console ("PX-42F Console", `!` commands: `!HELP`/`!?` (also printed
  whenever a terminal opens the port), `!STATUS`, `!PRINTER`, `!PRINTTEST`, `!SCREENSHOT`,
  `!SETDATE`, `!SETTIME`; other characters are keypresses), MSC drive "PX-42F", picotool reset,
  CDC1 printer.
- Flash: program 0-4 MB-4 KB; settings sector at 0x3FF000; FAT16 disk 4-16 MB
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
  (`ramstate.c`) and restored by `free42_init()`. With a Splash Time, a POWMAN timer alarm
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
  Timer OFF at 1.00 V (+2%). The real fix is the RA8900CE RTC on the next board. `time_trim` (tenths s/day)
  adjusts both rates. Setting the time exactly twice ≥6 h apart measures the drift
  (`cal_drift`), offered in Settings → Date & Time → Time Calibration → Measured (which
  also takes the exact time, HH:MM:SS, to measure). Date/time formats are
  Free42's (DMY/MDY/YMD, CLK12/CLK24) mirrored in px_settings; the extra DOW-MON-DAY format
  ("FRI-OCT-9") is MDY to Free42. A full chip reset (RESET button, battery) loses the time.
- Settings written from frequent/program-driven paths (SETIME, DMY...) use
  `settings_save_later()` — flushed at OFF — so a looping program can't wear the flash.
- Retained RAM layout: `retained.h` (framebuffer 0x2005F000, RAM state 0x20060000-top);
  `HEAP_LIMIT` in `CMakeLists.txt` must equal `RETAINED_BASE`. It only holds because
  `shell.cc` overrides `_sbrk()`: the SDK's grows the heap to the end of RAM, over the retained
  RAM. `shell_get_mem()` (MEM) = heap left below `__HeapLimit` + malloc's free blocks.

## Hard-won rules (each one cost a debugging round)

- **Deep sleep (SLEEPDEEP) only inside core 1's idle wait** (`cpuclock_deep_sleep_begin/end`).
  Left on permanently it corrupted both cores' stacks around flash writes.
- Sleep clock gating (`keep_in_sleep` in `cpuclock.c`) must keep the memory system
  (SRAM, bus fabric, XIP, ROM, SIO). Anything that must run while both cores sleep needs
  its clock kept — this bites even outside the idle wait (any WFI/WFE on both cores, e.g. on
  battery with no USB traffic): `speaker_play()` keeps the PWM clock for each sound (its
  sample interrupt is what ends its WFI); the ADC is enabled only for each battery reading.
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
  The RESET button (RUN pin, POWMAN `HAD_RUN_LOW` *and* the always-on timer stopped:
  `rtc_clock_init()` returns true) is Memory Clear. CHIP_RESET only records POWMAN's own last
  reset, so `HAD_RUN_LOW` stays set through every flash/crash restart after a RESET press —
  alone it cleared the memory at the next flash. Memory Clear = `ramstate_discard()` and
  `/OFFSTATE.p42` deleted (the overflow for a memory bigger than the 128 KB RAM copy); every
  other restart brings the memory back. `!STATUS` prints the last reset's cause.
  After RESET the Hello screen waits for a key (only dismisses) or the Sleep Timer (`hello_wait()`).
- A drive that isn't FAT is only formatted if it's blank (0xFF); otherwise it's left alone,
  unmounted (USB Disk mode still exposes it; Settings → USB Drive → Format Drive, `statefs_format()`,
  formats it on request). The calculator powers off by itself below
  ~3.45 V (`battery_empty()`; the ADC can't read lower: it measures against the 3.3 V rail).
- Console writes time out after 50 ms (`PICO_STDIO_USB_STDOUT_TIMEOUT_US`): the SDK's
  500 ms default outlasts the 100 ms watchdog.
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

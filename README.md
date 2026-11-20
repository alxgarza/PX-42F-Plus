# PX-42F

Firmware for the **PX-42F**, a handheld RPN calculator built around the
Raspberry Pi RP2350. It runs [Free42](https://thomasokken.com/free42/), Thomas
Okken's re-creation of the HP-42S, and adds what a standalone calculator needs:
a reflective display, battery power with a real OFF, a clock, sound, and a USB
drive for states, images, sounds and fonts.

## Hardware

| Part | Details |
|---|---|
| Processor | RP2350A, 16 MB W25Q128 flash, 12 MHz crystal |
| Display | ST7305, 250 × 122 monochrome reflective, SPI |
| Keyboard | 37 keys in an 8 × 5 matrix, HP-42S layout (EXIT is also ON) |
| Sound | 16 Ω speaker on a MOSFET, driven by 146 kHz PWM |
| Power | Li-ion cell, RT9080 3.3 V regulator, TP4057 charger over USB |

The pin assignments are in [`PINOUT.html`](PINOUT.html) and the board
definition in [`boards/px42f.h`](boards/px42f.h).

Measured on battery: about 4.4 mA idle, 25 mA while computing, and about
135 µA when off. Memory is kept while off.

## Using it

The calculator works like an HP-42S. On top of that:

| Keys | Does |
|---|---|
| EXIT | ON when the calculator is off |
| SHIFT, then EXIT | Opens the settings menu (where the HP-42S has OFF) |
| Hold EXIT for 2 s | Turns the calculator off |
| Hold SHIFT and − together for ½ s | Saves a screenshot to the drive |

The settings menu opens on a home screen of 32×32 icons, seven across and three
down, under a bar with the picked icon's name and the date and time, over tabs
for the top row of keys (OPEN, MOVE, ↑ ↓ ← →). At first the top row
is **Off**, **USB Drive**, **Plus42**, **Alarms**, **Apps**, **Games** and **Settings**
(the gear), the second row is left for your favourites, and **Help** and **Info**
are at the right of the third. In the Apps and Games folders (each `.bin` by
name), FAV moves one to the home screen's first empty place; there SHIFT turns
MOVE into UNFAV, which sends it back. The tabs and 8 2 / 4 6 move up,
down, left and right (past a row's end to the next row, past the bottom to the
top); ▲ ▼ step to the icon before and after; ENTER or 5 opens, and EXIT or ←
goes back.
With **Off** picked, EXIT turns the calculator off. MOVE (or SHIFT then ENTER)
picks an icon up to move it (it swaps with icons and blank places, so it can
leave gaps), DONE puts it down; Off stays first. The order and
the favourites are kept in `/HOME.TXT` on the drive.

**Plus42** opens a page with **States** and **Programs** (below). The gear's
**Menu Style** switches the whole menu to one list of text, as it was before the
icons (and back: Menu Style is in that list too).

The gear opens a page of icons:

- **Date & Time**: time, date, their formats, and clock calibration
- **Fonts**: the three calculator fonts
- **Sounds**: key click, and the boot, on, off and error sounds
- **Printer**: where Plus42's printer output goes
- **Splash Image**: a picture with a clock shown while off, and for how long
- **Sleep Timer**: turns off after this long without a keypress (never while
  connected to a computer)
- **Menu Style**: icons, or one list of text

**Plus42** has **States** (save, load, rename and delete calculator states) and
**Programs** (import and export `.raw` files). **USB Drive** shows the
calculator's drive on a computer.

### The USB drive

In **USB Drive** mode the calculator appears on a computer as a drive named
`PX-42F`. Eject it before leaving the mode.

| Folder | Holds |
|---|---|
| `STATE/` | Calculator states (`.f42`), the same format as Free42 on a computer |
| `SPLASH/` | Splash images: any PNG, best at 250 × 122 pixels |
| `SOUNDS/` | WAV files: `KEY/` key clicks (up to 250 ms), `TONES/TONE0`–`9` for TONE and BEEP, `BOOT`, `ON`, `OFF`, `ERROR` |
| `FONTS/` | Font sheets: `LARGE/` (175 × 135 PNG), `SMALL/` (128 × 96 PNG), `MINI/` (96 × 64 PNG); `BUILTIN/` has the built-in fonts' sheets, read-only |
| `SCREENSHOTS/` | Screenshots, 250 × 122 PNG, named by date and time |
| `BIN/` | Games and apps (`.bin`): `GAMES/` and `APPS/`, below |

Sounds can be any PCM or float WAV, at any sample rate; the calculator
normalizes and converts them. A font sheet is the 128 HP-42S characters in a
grid 16 across and 8 down: copy one from `FONTS/BUILTIN/` (`LARGE.png`,
`SMALL.png`, `MINI.png`) to the computer and redraw it.

### Games and apps

The home screen's app and game icons run programs written for the PX-42F itself: `.bin`
files in `BIN/GAMES` and `BIN/APPS` on the drive. ON always leaves one (held
2 s: off). The calculator's memory is saved to the drive before one starts,
so a game that crashes loses nothing. Each keeps its own files in a folder
next to it (`BIN/GAMES/SNAKE/` for `SNAKE.bin`). Each shows its own icon, which
comes inside its `.bin` (the template makes it from `icon.txt`); one without
gets a frame with its first letter.

They're written in C against [`px_app.h`](px_app.h), the contract between the
firmware and an app: the screen and fonts, keys, time, sound, files on the
drive, the USB console, the infrared LED and the battery. The
**PX-42F-App-Template** project is where a new one starts. It has a build
setup for VS Code and a simulator that runs the app on a computer. Two are
built on it:

- **PX-42F-Snake**: Snake.
- **PX-42F-IR-Remote**: the infrared remote control. It used to be built into
  this firmware. Its remotes go in `BIN/APPS/IR`, in the Flipper Zero's `.ir`
  format.

### The USB console

Over USB the calculator also shows up as two serial ports. The console port
takes typed keystrokes and these commands (in capitals, then Enter):

| Command | Does |
|---|---|
| `!HELP` | Lists the commands |
| `!STATUS` | Clock speed, core voltage, USB, battery, time |
| `!SETDATE YYYY-MM-DD` | Sets the date |
| `!SETTIME HH:MM[:SS]` | Sets the time (24-hour) |
| `!SCREENSHOT` | Sends the screen as a base64 PNG ([`tools/screenshot.py`](tools/screenshot.py) saves it) |
| `!BOOTLOG` | The last starts and app runs: what restarted the calculator and where its memory came from (also `BOOTLOG.TXT` on the drive) |
| `!PRINTER`, `!PRINTTEST` | Printer port state and a test line |

The second port is the printer: Free42's PRINT output arrives there as text.

## Building

You need the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
2.3.0 and the Arm GNU toolchain. The simplest way to get both is the Raspberry
Pi Pico extension for VS Code, which installs them under `~/.pico-sdk` where
`CMakeLists.txt` finds them.

```sh
cmake -S . -B build -G Ninja
ninja -C build
```

If `ninja` isn't on your PATH, use the one the extension installed: add
`-DCMAKE_MAKE_PROGRAM=$HOME/.pico-sdk/ninja/v1.13.2/ninja` to the `cmake`
line and run that `ninja`. The firmware is `build/PX-42F-Plus.uf2`.

[`tests/run.sh`](tests/run.sh) builds and runs the tests for the file parsers
(PNG, WAV, font sheets, app headers, and the PNG writer) on the computer, with
AddressSanitizer.

## Flashing

- **Calculator running and connected:** `tools/flash.sh` restarts it into its
  bootloader with `picotool`, loads the firmware and waits for it to come
  back. No buttons needed.
- **On the calculator, without a computer program:** Settings → USB Drive →
  Update Firmware saves the memory, then the `RP2350` drive appears: copy
  `PX-42F-Plus.uf2` onto it. The memory and the time come back.
- **Calculator off or not responding:** hold BOOT, press RESET, and copy
  `PX-42F-Plus.uf2` to the `RP2350` drive that appears. RESET clears the
  memory.

`tools/flash.sh` restarts the calculator, so its memory is lost unless it was
turned off first.

## Project layout

| Path | Contents |
|---|---|
| `src/core/` | The Free42 core, with this port's display and key handling (`free42_app.cc`) |
| top level | The board: display, keyboard, power, clock, sound, USB, storage, settings menu |
| `fonts/`, `images/` | Built-in fonts and splash image, and the scripts that convert them |
| `tools/` | Flashing, console, screenshot and sound-generation scripts |
| `tests/` | Host tests for the file parsers |
| `third_party/` | Intel Decimal Floating-Point Math Library, FatFs |

[`CLAUDE.md`](CLAUDE.md) has the detailed engineering notes: architecture,
power management, and the hardware lessons learned along the way.

## Credits and licenses

- **Free42** © Thomas Okken, GNU General Public License version 2. This
  firmware includes and modifies it, so it is distributed under the same
  license.
- **Intel Decimal Floating-Point Math Library** © Intel Corporation, BSD-style
  license ([`third_party/IntelRDFPMathLib20U1/eula.txt`](third_party/IntelRDFPMathLib20U1/eula.txt)).
- **FatFs** © ChaN, BSD-style license ([`third_party/fatfs/`](third_party/fatfs/)).
- **Raspberry Pi Pico SDK** © Raspberry Pi Ltd, BSD 3-Clause license.
- The large built-in font is based on **Spleen** by Frederic Cambus
  (BSD 2-Clause license).

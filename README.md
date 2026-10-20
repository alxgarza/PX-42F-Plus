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

In the settings menu, ▲ ▼ move, ENTER selects, and EXIT or ← goes back.
With **Off** highlighted, EXIT turns the calculator off.

The settings menu has:

- **Off**
- **IR Remote**: turns the calculator into a remote control (below)
- **Date & Time**: time, date, their formats, and clock calibration
- **Sounds**: key click, and the boot, on and off sounds
- **Fonts**: the two calculator fonts
- **Sleep Timer**: turns off after this long without a keypress (never while
  connected to a computer)
- **Splash Image**: a picture with a clock shown while off, and for how long
- **Printer**: where Free42's printer output goes
- **States**: save, load, rename and delete calculator states
- **USB Drive**: shows the calculator's drive on a computer
- **Info**

### The USB drive

In **USB Drive** mode the calculator appears on a computer as a drive named
`PX-42F`. Eject it before leaving the mode.

| Folder | Holds |
|---|---|
| `STATE/` | Calculator states (`.f42`), the same format as Free42 on a computer |
| `SPLASH/` | Splash images: any PNG, best at 250 × 122 pixels |
| `SOUNDS/` | WAV files: `KEY/` key clicks (up to 250 ms), `TONES/TONE0`–`9` for TONE and BEEP, `BOOT`, `ON`, `OFF`, `ERROR` |
| `FONTS/` | Font sheets: `LARGE/` (175 × 135 PNG) and `SMALL/` (128 × 96 PNG) |
| `SCREENSHOTS/` | Screenshots, 250 × 122 PNG, named by date and time |
| `IR/` | Remote controls (`.ir`), below |

Sounds can be any PCM or float WAV, at any sample rate; the calculator
normalizes and converts them. A font sheet is the 128 HP-42S characters in a
grid 16 across and 8 down: copy [`fonts/font_10x16.png`](fonts/font_10x16.png)
(large) or [`fonts/font_12x8.png`](fonts/font_12x8.png) (small) and redraw it.

### The remote control

The PX-42F has an infrared LED, so it can work as a TV or audio remote.
Each remote is a text file in `IR/` on the drive, in the same format as the
[Flipper Zero](https://flipperzero.one)'s `.ir` files, with two extra lines:
`button` (which key sends the code) and `label` (what the key shows on the
screen).

```
name: TV On/Off
label: Power
button: R0C5
protocol: NECext
address: 02 7D 00 00
command: 9F 60 00 00
```

`button` is the key's place on the keyboard: `R` and the row (0–6, from the
top), `C` and the column (0–5, from the left). The top two rows have 6 keys
and the others 5, so `R0C5` is XEQ, `R2C0` is ENTER and `R6C4` is +. `R6C0`
(ON) can't be used: it always goes back to the calculator. Labels longer than
the key is wide are cut short on the screen.

[`examples/IR/Samsung-TV.ir`](examples/IR/Samsung-TV.ir) is a complete example
for Samsung TVs, with the key map and instructions in its comments: copy it to
`IR/` to try it, or as the start of your own.

Choose a remote in **Settings → IR Remote**. The screen then shows a map of the
keyboard with each key's label; keys with no code are blank and do nothing. A
key sends its code when pressed and repeats it while held. The title bar shows
what was sent last. ON goes back to the calculator, and holding ON for 2 seconds
turns the calculator off.

The protocols are the Flipper's: NEC, NECext, NEC42, NEC42ext, Samsung32, RC5,
RC5X, RC6, SIRC, SIRC15, SIRC20, Kaseikyo, RCA and Pioneer. They send the same
timings as the Flipper, so codes from collections such as
[Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB) work: copy a file to
`IR/` and add a `button` line to each code you want on a key. Entries without
a `button` are ignored. Raw codes (`type: raw`) aren't supported yet.

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
line and run that `ninja`. The firmware is `build/PX-Free42.uf2`.

[`tests/run.sh`](tests/run.sh) builds and runs the tests for the file parsers
(PNG, WAV, font sheets, `.ir` files, and the PNG writer) and the infrared
encoders on the computer, with AddressSanitizer. The infrared test vectors in
`tests/ir_vectors.h` come from the Flipper firmware's own encoders
([`tools/ir_vectors/make.sh`](tools/ir_vectors/make.sh) regenerates them).

## Flashing

- **Calculator running and connected:** `tools/flash.sh` restarts it into its
  bootloader with `picotool`, loads the firmware and waits for it to come
  back. No buttons needed.
- **Calculator off or not responding:** hold BOOT, press RESET, and copy
  `PX-Free42.uf2` to the `RP2350` drive that appears.

Flashing restarts the calculator, so its memory is lost unless it was turned
off first.

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

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

In the settings menu, ▲ ▼ move, ENTER selects, and EXIT or ← goes back.
With **Off** highlighted, EXIT turns the calculator off.

The settings menu has:

- **Off**
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

Sounds can be any PCM or float WAV, at any sample rate; the calculator
normalizes and converts them. A font sheet is the 128 HP-42S characters in a
grid 16 across and 8 down: copy [`fonts/font_10x16.png`](fonts/font_10x16.png)
(large) or [`fonts/font_12x8.png`](fonts/font_12x8.png) (small) and redraw it.

### The USB console

Over USB the calculator also shows up as two serial ports. The console port
takes typed keystrokes and these commands (in capitals, then Enter):

| Command | Does |
|---|---|
| `!HELP` | Lists the commands |
| `!STATUS` | Clock speed, core voltage, USB, battery, time |
| `!SETDATE YYYY-MM-DD` | Sets the date |
| `!SETTIME HH:MM[:SS]` | Sets the time (24-hour) |
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
(PNG, WAV and font sheets) on the computer, with
AddressSanitizer.

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
| `tools/` | Flashing, console and sound-generation scripts |
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

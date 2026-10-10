---
name: flash
description: Flash the built firmware (build/PX-Free42.uf2) into the connected PX-42F over USB with picotool and confirm it boots. Use when the user asks to flash, load, or try a build on the calculator.
---

1. Make sure the build is current (run the `/build` steps first if sources changed since
   `build/PX-Free42.uf2` was made).
2. Warn once if it matters: a reflash restarts the calculator, so its memory is lost unless
   it was turned OFF first.
3. Run `tools/flash.sh` (timeout ~3 min). It reboots the calculator into BOOTSEL, waits for
   picotool to see it, loads and starts the firmware, and waits for the console port.
4. Confirm with `tools/console.py '!STATUS'` (clock 150 MHz at 1100 mV, sensible battery and
   time) and report briefly.
- No calculator found: it's off (OFF powers USB down) or unplugged — ask the user to turn
  it on, or to hold BOOTSEL while pressing RESET.
- `picotool info` can segfault while polling; harmless, the script keeps waiting.

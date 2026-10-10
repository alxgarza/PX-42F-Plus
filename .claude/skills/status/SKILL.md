---
name: status
description: Read the PX-42F's live status (clock speed, core voltage, USB state, battery, time, idle-wait counts) from its USB console with !STATUS, or send another ! console command. Use to check the device's state yourself instead of asking the user.
argument-hint: "[!COMMAND]"
---

Run `tools/console.py '$ARGUMENTS'` (default `!STATUS` when no argument). Other commands:
`!HELP`, `!PRINTER`, `!PRINTTEST`, `!SETDATE YYYY-MM-DD`, `!SETTIME HH:MM[:SS]`.
(The console prints its `!HELP` list whenever a port is opened; the script drops it.)

- It finds the console among `/dev/cu.usbmodem*` by which port answers.
- If it can't open the port, a terminal app (e.g. CoolTerm) has it: ask the user to
  disconnect it there.
- If there's no port at all, the calculator isn't plugged in, is off (OFF powers USB down),
  or is still booting.
- Note: while connected to a computer the calculator always runs at full speed (150 MHz,
  1.10 V) — the idle/battery numbers can't be seen over USB; the idle-wait counters show
  what happened on battery since boot.

Report the reply concisely; point out anything unexpected (e.g. battery level vs mV,
clock not matching the USB state).

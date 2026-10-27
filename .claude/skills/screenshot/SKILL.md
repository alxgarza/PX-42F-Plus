---
name: screenshot
description: Capture the PX-42F's screen over its USB console as a PNG and look at it. Use to see what the calculator is showing yourself (after flashing a display change, or when the user describes something on screen) instead of asking for a photo.
argument-hint: "[file.png]"
---

Run `tools/screenshot.py $ARGUMENTS` (default output `px42f-screenshot.png` in the temp folder; it prints the path), then Read the
PNG it names: 250 x 122 pixels, black and white.

- It sends `!SCREENSHOT` to the console, so the calculator must be on, connected, and showing
  the calculator screen: the settings menu doesn't read the console (ask the user to leave it).
- "No PX-42F port": not plugged in, off, or still booting. "can't open": a terminal app has
  the console port.
- "cut short" / "damaged": console bytes were lost; run it again.
- On the calculator itself, SHIFT and "-" held together for half a second save the screen to
  `/SCREENSHOTS` on its drive (works in the settings menu too); those files can be read from
  `/Volumes/PX-42F/SCREENSHOTS` while the calculator is in USB Drive mode.

A screenshot shows what the firmware drew, not what the panel displays: it can't show
contrast, ghosting or refresh problems — those still need the user's eyes.

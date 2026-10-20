#!/usr/bin/env python3
"""Saves the PX-42F's screen as a PNG on this computer.

    tools/screenshot.py [file.png]      (default: build/screenshot.png)

Sends !SCREENSHOT to the console (see console.py) and decodes the reply: a
250 x 122 black and white PNG. The calculator must be on, connected, and on
its calculator screen -- the settings menu doesn't read the console.
Standard library only."""
import base64, glob, os, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from console import ask

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "build", "screenshot.png")
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("No PX-42F port (/dev/cu.usbmodem*): is it plugged in and on?")
    for port in ports:
        try:
            reply = ask(port, "!SCREENSHOT", wait=3)
        except OSError as e:
            print(f"{port}: can't open ({e.strerror}) -- close any terminal app using it", file=sys.stderr)
            continue
        if "SCREENSHOT BEGIN" not in reply:
            continue
        if "SCREENSHOT END" not in reply:
            sys.exit("The reply was cut short -- try again")
        text = reply.split("SCREENSHOT BEGIN", 1)[1].split("SCREENSHOT END", 1)[0]
        try:
            png = base64.b64decode("".join(text.split()), validate=True)
            # The image data has its own checksum: catches console bytes lost on the way.
            assert png[:8] == b"\x89PNG\r\n\x1a\n" and png[12:16] == b"IHDR"
            idat = png.index(b"IDAT")
            zlib.decompress(png[idat + 4:-16])
        except Exception:
            sys.exit("The reply was damaged on the way -- try again")
        with open(out, "wb") as f:
            f.write(png)
        print(os.path.normpath(out))
        return
    sys.exit("No reply from any port (" + ", ".join(ports) + "): on, and not in the settings menu?")

if __name__ == "__main__":
    main()

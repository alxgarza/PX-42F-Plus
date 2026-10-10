#!/usr/bin/env python3
"""Sends one line to the PX-42F console and prints the reply.

    tools/console.py '!STATUS'      (the '!' command; Enter is added)

Finds the console among the /dev/cu.usbmodem* ports by trying each: the
console answers, the printer port doesn't. Standard library only. Fails if a
terminal app (CoolTerm...) already has the port open."""
import glob, os, select, sys, termios, time, tty

def ask(port, line, wait=1.5):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        # Opening raises DTR, and the console answers with its !HELP list:
        # let that arrive and drop it, so only the reply to our line is shown.
        drain_until = time.time() + 1.0
        quiet_since = time.time()
        while time.time() < drain_until and time.time() - quiet_since < 0.3:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    if os.read(fd, 4096):
                        quiet_since = time.time()
                except BlockingIOError:
                    pass
        os.write(fd, (line + "\r").encode())
        out, end = b"", time.time() + wait
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                try:
                    out += os.read(fd, 4096)
                except BlockingIOError:
                    pass
        return out.decode(errors="replace")
    finally:
        os.close(fd)

def main():
    line = sys.argv[1] if len(sys.argv) > 1 else "!STATUS"
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("No PX-42F port (/dev/cu.usbmodem*): is it plugged in and on?")
    for port in ports:
        try:
            reply = ask(port, line)
        except OSError as e:
            print(f"{port}: can't open ({e.strerror}) -- close any terminal app using it", file=sys.stderr)
            continue
        if reply.strip():
            print(f"[{port}]")
            print(reply.rstrip())
            return
    sys.exit("No reply from any port (" + ", ".join(ports) + ")")

if __name__ == "__main__":
    main()

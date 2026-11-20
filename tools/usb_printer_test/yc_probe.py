import sys, usb.core, usb.util, usb.backend.libusb1

be = usb.backend.libusb1.get_backend(find_library=lambda x: "/opt/homebrew/lib/libusb-1.0.dylib")
dev = usb.core.find(idVendor=0x28E9, idProduct=0x0289, backend=be)
usb.util.claim_interface(dev, 0)
OUT, IN = 0x01, 0x81

def ask(name, cmd):
    dev.write(OUT, cmd, timeout=2000)
    try:
        r = bytes(dev.read(IN, 64, timeout=800))
        print(f"{name}: {r.hex(' ')}")
    except usb.core.USBTimeoutError:
        print(f"{name}: no answer")

if "--image" not in sys.argv:
    ask("DLE EOT 1 printer", b"\x10\x04\x01")
    ask("DLE EOT 2 offline", b"\x10\x04\x02")
    ask("DLE EOT 3 error  ", b"\x10\x04\x03")
    ask("DLE EOT 4 paper  ", b"\x10\x04\x04")
    ask("GS I 1 model     ", b"\x1dI\x01")
    ask("GS I 2 type      ", b"\x1dI\x02")
    ask("GS I 3 version   ", b"\x1dI\x03")
    ask("GS I 67 name     ", b"\x1dI\x43")
    sys.exit()

# Raster image, GS v 0: 384 dots wide (58 mm), 96 rows: a border, a checkerboard band, stripes.
W, H = 384, 96
wb = W // 8
rows = []
for y in range(H):
    row = bytearray(wb)
    for x in range(W):
        on = (x < 4 or x >= W - 4 or y < 4 or y >= H - 4
              or (32 <= y < 64 and ((x // 16) + (y // 16)) % 2 == 0)
              or (y >= 64 and (x // 8) % 2 == 0))
        if on:
            row[x // 8] |= 0x80 >> (x % 8)
    rows.append(bytes(row))
job = (b"\x1b@" + b"\x1dv0\x00" + bytes([wb & 255, wb >> 8, H & 255, H >> 8])
       + b"".join(rows) + b"\x1bd\x03")
dev.write(OUT, job, timeout=10000)
print("image sent", len(job), "bytes")

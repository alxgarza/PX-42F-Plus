import sys, usb.core, usb.util, usb.backend.libusb1

be = usb.backend.libusb1.get_backend(find_library=lambda x: "/opt/homebrew/lib/libusb-1.0.dylib")
dev = usb.core.find(idVendor=0x28E9, idProduct=0x0289, backend=be)
if dev is None:
    sys.exit("printer not found")

cfg = dev.get_active_configuration()
intf = cfg[(0, 0)]
print(f"interface class {intf.bInterfaceClass}/{intf.bInterfaceSubClass}/{intf.bInterfaceProtocol}")
for ep in intf:
    d = "IN" if usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN else "OUT"
    print(f"  ep 0x{ep.bEndpointAddress:02x} {d} type {ep.bmAttributes & 3} max {ep.wMaxPacketSize}")

# USB Printer Class GET_DEVICE_ID: 2-byte big-endian length, then the IEEE 1284 ID string.
try:
    raw = bytes(dev.ctrl_transfer(0xA1, 0, 0, 0, 1024))
    print("1284 ID:", raw[2:].decode("latin-1"))
except usb.core.USBError as e:
    print("no 1284 ID:", e)
try:
    st = dev.ctrl_transfer(0xA1, 1, 0, 0, 1)[0]
    print(f"port status 0x{st:02x} (0x18 = ok, paper in)")
except usb.core.USBError as e:
    print("no port status:", e)

# --print: a short test slip; --file PATH: that file's bytes as they are
# (tests/test_escpos.c writes a sample job of the firmware's own bytes).
job_file = sys.argv[sys.argv.index("--file") + 1] if "--file" in sys.argv else None
if "--print" not in sys.argv and job_file is None:
    sys.exit()

try:
    if dev.is_kernel_driver_active(0):
        dev.detach_kernel_driver(0)
except (NotImplementedError, usb.core.USBError):
    pass
usb.util.claim_interface(dev, 0)
out = usb.util.find_descriptor(intf, custom_match=lambda e:
        usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)

ESC, GS = b"\x1b", b"\x1d"
job = (ESC + b"@"                      # reset
       + b"PX-42F USB print test\n"
       + ESC + b"E\x01" + b"Bold line\n" + ESC + b"E\x00"
       + GS + b"!\x11" + b"Big 2x\n" + GS + b"!\x00"
       + b"0123456789012345678901234567890123456789\n"
       + b"1.234567890E-12   SQRT  ENTER\n"
       + ESC + b"d\x04")               # feed 4 lines
if job_file is not None:
    job = open(job_file, "rb").read()
out.write(job, timeout=20000)
print("sent", len(job), "bytes")
usb.util.release_interface(dev, 0)

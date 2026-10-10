#!/bin/bash
# Flashes build/PX-Free42.uf2 into the PX-42F over USB and starts it.
# The calculator must be on and connected (it reboots itself into its
# bootloader on request), or already in BOOTSEL mode.
cd "$(dirname "$0")/.."
PT=~/.pico-sdk/picotool/2.3.0/picotool/picotool
UF2=build/PX-Free42.uf2
[ -f "$UF2" ] || { echo "no $UF2 -- build first"; exit 1; }
echo "Flashing $UF2 ($(stat -f '%Sm' "$UF2"))"

# Into BOOTSEL, wait for it, load. 'load -f' alone gives up before the
# RP2350 has come back, and now and then the reboot request itself doesn't
# take -- so check the load really happened, and try again if not.
loaded=no
for attempt in 1 2 3; do
    "$PT" reboot -f -u >/dev/null 2>&1   # fails harmlessly if already in BOOTSEL
    # picotool info sometimes crashes while polling -- harmless. The shell's
    # own crash notice goes to its stderr, so the whole loop's is silenced.
    {
        for i in $(seq 1 30); do
            "$PT" info >/dev/null 2>&1 && break
            sleep 1
        done
    } 2>/dev/null
    out=$("$PT" load -x "$UF2" 2>&1)
    if echo "$out" | grep -q "rebooted to start the application"; then
        loaded=yes
        break
    fi
    echo "Attempt $attempt didn't load -- retrying"
done
[ "$loaded" = yes ] || { echo "$out" | grep -v "Loading into Flash" | tail -3; echo "Flash failed"; exit 1; }
echo "Loaded and started"

# Back up as the calculator: wait for its console port (after a moment --
# the old one can still be listed while it restarts).
sleep 2
for i in $(seq 1 20); do
    ls /dev/cu.usbmodem* >/dev/null 2>&1 && { echo "Running: console port up"; exit 0; }
    sleep 1
done
echo "Flashed, but no console port appeared within 20s (is it on? check the screen)"
exit 1

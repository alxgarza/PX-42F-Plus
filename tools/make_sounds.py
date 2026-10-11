#!/usr/bin/env python3
"""Renders the PX-42F's built-in sounds as WAV files and embeds them in
sounds_data.c.

    tools/make_sounds.py              rewrite sounds_data.c
    tools/make_sounds.py --wav DIR    also write the .wav files to DIR, to
                                      listen to / edit them (Audacity...)

The calculator writes these same files into /SOUNDS on its drive (on first
boot, or whenever that folder is missing) and plays a built-in only when its
file is gone. So each one is just a plain WAV -- 16-bit mono PCM -- exactly
what a user would export from Audacity; the calculator normalizes and
converts every sound the same way when it plays it (sounds.c).

Standard library only; the typewriter's noise is seeded, so the output is
reproducible."""
import math, os, random, struct, sys, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Free42's key click: a copy of free42-upstream/android/app/src/main/res/raw/
# click9.wav (that folder isn't in git; this file is, so a fresh clone can
# rebuild the sounds and run the tests).
CLICK_WAV = os.path.join(ROOT, "tools/free42_click.wav")

EDGE_S = 0.005  # default fade-in, and the fade-out at the end


def sine_wave(phase):
    return math.sin(phase)


def full_wave(phase):
    """Soft-clipped sine, tanh(3 sin x)/tanh(3): flat-topped, so at the same
    peak its fundamental is 21% stronger, and its gentle harmonics (3rd 24%,
    5th 8%) sound louder still and let a small speaker voice low tones."""
    return math.tanh(3 * math.sin(phase)) / math.tanh(3)


def render(notes, length_s, wave_fn, rate):
    """notes: (hz, start_s, decay_s, level, attack_s); decay_s 0 = held,
    attack_s 0 = EDGE_S. Struck notes ring out exponentially."""
    n = int(length_s * rate)
    edge = max(1, min(int(EDGE_S * rate), n // 4))
    out = []
    for i in range(n):
        x = 0.0
        for hz, start_s, decay_s, level, attack_s in notes:
            t = i / rate - start_s
            if t < 0:
                continue
            attack = attack_s or EDGE_S
            if t < attack:
                env = level * t / attack
            elif decay_s > 0:
                env = level * math.exp(-(t - attack) / decay_s)
            else:
                env = level
            x += env * wave_fn(2 * math.pi * hz * t)
        if i >= n - edge:
            x *= (n - i) / edge
        out.append(x)
    return out


def bandpass(x, center_hz, q, rate):
    """Two-pole resonant band-pass (RBJ biquad, constant 0dB peak gain)."""
    w0 = 2 * math.pi * center_hz / rate
    alpha = math.sin(w0) / (2 * q)
    b0, b2 = alpha, -alpha
    a0, a1, a2 = 1 + alpha, -2 * math.cos(w0), 1 - alpha
    y, x1, x2, y1, y2 = [], 0.0, 0.0, 0.0, 0.0
    for s in x:
        v = (b0 * s + b2 * x2 - a1 * y1 - a2 * y2) / a0
        x2, x1, y2, y1 = x1, s, y1, v
        y.append(v)
    return y


def decaying(n, rate, start_s, attack_s, decay_s):
    env = []
    for i in range(n):
        t = i / rate - start_s
        if t < 0:
            env.append(0.0)
        elif t < attack_s:
            env.append(t / attack_s)
        else:
            env.append(math.exp(-(t - attack_s) / decay_s))
    return env


def typewriter(rate):
    """The key's light tick, then ~9ms later the type bar's clack: a burst of
    band-passed noise with a short metallic ring."""
    rnd = random.Random(42)
    n = int(0.045 * rate)
    noise = [rnd.uniform(-1, 1) for _ in range(n)]
    tick = bandpass(noise, 4500, 1.2, rate)
    tick_env = decaying(n, rate, 0.0, 0.0002, 0.0012)
    clack = bandpass(noise[::-1], 2600, 0.9, rate)
    clack_env = decaying(n, rate, 0.009, 0.0003, 0.0035)
    ring_env = decaying(n, rate, 0.009, 0.0003, 0.008)
    out = []
    for i in range(n):
        t = i / rate
        ring = 0.5 * math.sin(2 * math.pi * 3150 * t) + 0.3 * math.sin(2 * math.pi * 4720 * t)
        out.append(0.35 * tick[i] * tick_env[i] + clack[i] * clack_env[i] + 0.25 * ring * ring_env[i])
    return out


def free42_click():
    """Free42's own key click: the Android/iOS shells' click9.wav (the
    loudest of its nine volume levels -- the same recording at 6dB steps),
    8kHz, as is."""
    w = wave.open(CLICK_WAV)
    raw = w.readframes(w.getnframes())
    return [s / 32768.0 for s in struct.unpack("<%dh" % (len(raw) // 2), raw)], w.getframerate()


def wav_bytes(samples, rate):
    """16-bit mono PCM, normalized to -0.1dBFS (the calculator normalizes
    again anyway), silent ends trimmed."""
    peak = max(abs(v) for v in samples) or 1.0
    pcm = [max(-32767, min(32767, round(v / peak * 32400))) for v in samples]
    quiet = 32
    while pcm and abs(pcm[-1]) < quiet:
        pcm.pop()
    first = next((i for i, v in enumerate(pcm) if abs(v) >= quiet), 0)
    pcm = pcm[first:]
    data = struct.pack("<%dh" % len(pcm), *pcm)
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    header += b"data" + struct.pack("<I", len(data))
    return header + data


# Free42's TONE 0-9 and its error squeak, 250ms / 125ms.
TONE_HZ = [165, 220, 247, 277, 294, 330, 370, 415, 440, 554]
SQUEAK_HZ = 1865


def sounds():
    """(path in /SOUNDS, samples, rate), in the order the Key Sound list shows."""
    k = 32000
    out = [
        ("KEY/Tick.wav", render([(2200, 0, 0.006, 1.00, 0.0005), (5400, 0, 0.003, 0.35, 0.0005)],
                                0.030, sine_wave, k), k),           # a tap on wood
        ("KEY/Tock.wav", render([(1200, 0, 0.009, 1.00, 0.0008), (3100, 0, 0.004, 0.30, 0.0008)],
                                0.040, sine_wave, k), k),           # deeper, rounder wood
        ("KEY/Glass.wav", render([(2637, 0, 0.012, 1.00, 0.0004), (6900, 0, 0.006, 0.40, 0.0004)],
                                 0.045, sine_wave, k), k),          # a small bell, inharmonic overtone
        ("KEY/Pop.wav", render([(700, 0, 0.008, 1.00, 0.0015), (1400, 0, 0.004, 0.25, 0.0015)],
                               0.035, sine_wave, k), k),            # a soft, low bubble
        ("KEY/Beep.wav", render([(2400, 0, 0, 1.00, 0.002)], 0.015, full_wave, k), k),  # classic short beep
        ("KEY/Typewriter.wav", typewriter(k), k),
    ]
    click, click_rate = free42_click()
    out.append(("KEY/Free42.wav", click, click_rate))
    # A bell arpeggio, C6 E6 G6 C7, struck 140ms apart, each ringing out.
    out.append(("BOOT.wav", render([(1046.50, 0.00, 0.22, 1, 0), (1318.51, 0.14, 0.22, 1, 0),
                                    (1567.98, 0.28, 0.22, 1, 0), (2093.00, 0.42, 0.45, 1, 0)],
                                   1.4, sine_wave, 16000), 16000))
    # Turning on (from OFF): just the top of it -- G6 C7, short.
    out.append(("ON.wav", render([(1567.98, 0.00, 0.12, 1, 0), (2093.00, 0.09, 0.30, 1, 0)],
                                 0.7, sine_wave, 16000), 16000))
    # Turning off: the same bell, coming down -- C7 G6 E6 C6, a little quicker.
    out.append(("OFF.wav", render([(2093.00, 0.00, 0.18, 1, 0), (1567.98, 0.11, 0.18, 1, 0),
                                   (1318.51, 0.22, 0.18, 1, 0), (1046.50, 0.33, 0.40, 1, 0)],
                                  1.2, sine_wave, 16000), 16000))
    for i, hz in enumerate(TONE_HZ):
        out.append(("TONES/TONE%d.wav" % i, render([(hz, 0, 0, 1, 0)], 0.250, full_wave, 16000), 16000))
    # Free42's error squeak.
    out.append(("ERROR.wav", render([(SQUEAK_HZ, 0, 0, 1, 0)], 0.125, full_wave, k), k))
    return out


def main():
    wav_dir = None
    if len(sys.argv) == 3 and sys.argv[1] == "--wav":
        wav_dir = sys.argv[2]
    body = ["/* Generated by tools/make_sounds.py -- don't edit; rerun it. */",
            '#include "pico.h"', '#include "sounds.h"', ""]
    entries = []
    total = 0
    for i, (path, samples, rate) in enumerate(sounds()):
        data = wav_bytes(samples, rate)
        total += len(data)
        print("%-20s %6d bytes, %5.0f ms, %d Hz" % (path, len(data), 1000 * (len(data) - 44) / 2 / rate, rate))
        if wav_dir:
            dest = os.path.join(wav_dir, path)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(data)
        lines = ["    " + ", ".join("0x%02x" % b for b in data[j:j + 20]) + "," for j in range(0, len(data), 20)]
        body.append("static const uint8_t sound_%d[] = {\n%s\n};\n" % (i, "\n".join(lines)))
        entries.append('    {"%s", sound_%d, sizeof(sound_%d)},' % (path, i, i))
    body.append("const builtin_sound_t builtin_sounds[] = {\n%s\n};\n" % "\n".join(entries))
    body.append("const unsigned builtin_sound_count = count_of(builtin_sounds);\n")
    with open(os.path.join(ROOT, "sounds_data.c"), "w") as f:
        f.write("\n".join(body))
    print("total %d bytes" % total)


if __name__ == "__main__":
    main()

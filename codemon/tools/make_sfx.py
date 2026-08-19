#!/usr/bin/env python3
"""
Generate the placeholder sound effects in assets/sfx/.

These are short, ORIGINAL synthesized tones (no third-party / copyrighted
audio) so the game has working sound out of the box. Replace them with your
own licensed audio of the same names to change the sounds.

Run:  python3 codemon/tools/make_sfx.py
"""
import os
import wave
import math
import struct

SR = 22050
OUT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "assets", "sfx"))


def write(path, samples):
    w = wave.open(path, "w")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(b"".join(
        struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples))
    w.close()


def tone(freq, dur, vol=0.5, decay=True, square=False):
    n = int(SR * dur)
    out = []
    for i in range(n):
        t = i / SR
        env = (1 - i / n) if decay else 1.0
        v = math.sin(2 * math.pi * freq * t)
        if square:
            v = 1.0 if v >= 0 else -1.0
        out.append(v * vol * env)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    # bump: a short low thud for a blocked step.
    write(os.path.join(OUT, "bump.wav"),
          tone(150, 0.06, 0.5, square=True) + tone(90, 0.07, 0.4, square=True))
    # warp: a quick ascending blip for an area transition.
    warp = []
    for f, d in [(440, 0.05), (620, 0.05), (880, 0.06), (1240, 0.08)]:
        warp += tone(f, d, 0.45)
    write(os.path.join(OUT, "warp.wav"), warp)
    # step: a soft tick (available for footsteps).
    write(os.path.join(OUT, "step.wav"), tone(220, 0.03, 0.25, square=True))
    print("wrote bump.wav, warp.wav, step.wav to", OUT)


if __name__ == "__main__":
    main()

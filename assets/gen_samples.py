#!/usr/bin/env python3
"""Generate sample scene images (BMP) + music bed (WAV) for the ShortForge demo.

Run from the repo root:  python3 assets/gen_samples.py
"""
import math
import os
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 1296, 2304  # slightly larger than 1080x1920 -> pan/zoom travel room


def write_bmp(path, pixels):
    row_stride = (W * 3 + 3) & ~3
    img_size = row_stride * H
    with open(path, "wb") as f:
        f.write(struct.pack("<2sIHHI", b"BM", 54 + img_size, 0, 0, 54))
        f.write(struct.pack("<IIIHHIIIIII", 40, W, H, 1, 24, 0, img_size, 2835, 2835, 0, 0))
        pad = b"\x00" * (row_stride - W * 3)
        for y in range(H - 1, -1, -1):  # BMP is bottom-up
            f.write(pixels[y * W * 3:(y + 1) * W * 3])
            f.write(pad)


def scene(path, top, bottom, glow_xy, glow_color):
    px = bytearray(W * H * 3)
    gx, gy = glow_xy
    for y in range(H):
        t = y / (H - 1)
        r = top[0] + (bottom[0] - top[0]) * t
        g = top[1] + (bottom[1] - top[1]) * t
        b = top[2] + (bottom[2] - top[2]) * t
        for x in range(W):
            dx = (x / W - gx) * 1.6
            dy = (y / H - gy) * 2.8
            glow = max(0.0, 1.0 - (dx * dx + dy * dy) * 2.2)
            glow = glow * glow
            o = (y * W + x) * 3
            px[o] = min(255, int(r + glow_color[0] * glow * 90))
            px[o + 1] = min(255, int(g + glow_color[1] * glow * 90))
            px[o + 2] = min(255, int(b + glow_color[2] * glow * 90))
    write_bmp(path, bytes(px))
    print("wrote", path)


def music(path, seconds=12.0, rate=44100):
    freqs = [110.0, 164.81, 220.0, 277.18]  # A2 E3 A3 C#4 — soft pad
    n = int(seconds * rate)
    frames = bytearray()
    for i in range(n):
        t = i / rate
        lfo = 0.75 + 0.25 * math.sin(2 * math.pi * 0.15 * t)
        v = sum(math.sin(2 * math.pi * f * t) for f in freqs) / len(freqs)
        v *= 0.28 * lfo
        # fade in/out over 1s
        fade = min(1.0, t, seconds - t)
        v *= max(0.0, min(1.0, fade))
        s = int(max(-1.0, min(1.0, v)) * 32767)
        frames += struct.pack("<hh", s, s)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))
    print("wrote", path)


if __name__ == "__main__":
    scene(os.path.join(HERE, "scene1.bmp"), (18, 22, 48), (58, 28, 92), (0.5, 0.35), (0.5, 0.7, 1.0))
    scene(os.path.join(HERE, "scene2.bmp"), (8, 34, 30), (10, 66, 58), (0.62, 0.6), (0.4, 1.0, 0.8))
    scene(os.path.join(HERE, "scene3.bmp"), (44, 12, 18), (96, 30, 26), (0.4, 0.4), (1.0, 0.55, 0.35))
    music(os.path.join(HERE, "music.wav"))

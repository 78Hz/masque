#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""MASQUE: all 512 masks drawn, from the oscillator itself (tools/gen_masks.c -w on stdin): docs/img/masks.png"""
import sys, struct
from PIL import Image, ImageDraw

raw = sys.stdin.buffer.read()
N = 256
w = [struct.unpack_from(f"{N}f", raw, 4 * N * m) for m in range(512)]
CW, CH, COLS = 58, 40, 32
im = Image.new("RGB", (COLS * CW, (512 // COLS) * CH + 4), (8, 6, 8))
d = ImageDraw.Draw(im)
for m in range(512):
    x0, y0 = (m % COLS) * CW, (m // COLS) * CH
    v = w[m]
    mean = sum(v) / N
    pk = max(abs(a - mean) for a in v) or 1.0
    pts = [(x0 + 4 + i * (CW - 8) / N, y0 + 22 - 13 * (v[i] - mean) / pk) for i in range(N)]
    d.line([(x0 + 4, y0 + 22), (x0 + CW - 4, y0 + 22)], fill=(48, 12, 10))
    d.line(pts, fill=(255, 52, 36) if m % 16 != 15 else (255, 150, 120), width=1)
    d.text((x0 + 4, y0 + 1), str(m), fill=(150, 140, 140))
im.save(sys.argv[1])

# SPDX-License-Identifier: MPL-2.0
# MASQUE: the manual's figures, drawn from the oscillator (run by make_manual.py, in build/manual)
import struct, numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
raw = open("waves.bin","rb").read(); N = 256
W = np.array(struct.unpack(f"{512*N}f", raw)).reshape(512, N)
RED, DIM = "#d8281c", "#bbbbbb"
def norm(m):
    v = W[m] - W[m].mean(); return v / (np.abs(v).max() or 1)
x = np.arange(N) / N

# Figure 1: the sine's 32 sections, and what KEEP does
fig, axs = plt.subplots(4, 1, figsize=(7, 6.2), sharex=True)
for ax, (m, t) in zip(axs, [(31, "MASK 31: every bit kept — the sine"),
                            (15, "MASK 15: the top bit dropped — the first half, twice (rounded)"),
                            (7, "MASK 7: the top two dropped — a quarter, four times (a saw)"),
                            (8, "MASK 8: one middle bit kept — a square whose low half is small saws")]):
    for s in range(33):
        ax.axvline(s / 32, color="#eeeeee" if s % 4 else "#cccccc", lw=0.6, zorder=0)
    if m != 31:
        ax.plot(x, np.sin(2 * np.pi * x), color=DIM, lw=1, ls="--")
    ax.plot(x, norm(m), color=RED, lw=1.6)
    ax.set_title(t, fontsize=9.5, loc="left"); ax.set_yticks([]); ax.set_ylim(-1.2, 1.2)
    for sp in ("top", "right", "left"): ax.spines[sp].set_visible(False)
axs[-1].set_xticks([i / 8 for i in range(9)]); axs[-1].set_xticklabels([str(4 * i) for i in range(9)])
axs[-1].set_xlabel("section (the phase's top five bits: 0..31)", fontsize=9)
fig.tight_layout(); fig.savefig("fig_keep.png", dpi=200)

# Figure 2: the operations, each on a mask
fig, axs = plt.subplots(2, 4, figsize=(7, 3.2))
pairs = [(11, "11: KEEP only"), (11 + 32, "43: + FLIP"), (11 + 64, "75: + INVERT"), (11 + 128, "139: + SILENCE"),
         (31, "31: the sine"), (31 + 256, "287: + REPEAT"), (56, "56: a pulse"), (78, "78: the buzz")]
for ax, (m, t) in zip(axs.flat, pairs):
    ax.plot(x, norm(m), color=RED, lw=1.2); ax.axhline(0, color="#dddddd", lw=0.6, zorder=0)
    ax.set_title(t, fontsize=8.5); ax.set_xticks([]); ax.set_yticks([]); ax.set_ylim(-1.15, 1.15)
    for sp in ax.spines.values(): sp.set_color("#cccccc")
fig.tight_layout(); fig.savefig("fig_ops.png", dpi=200)


# Figure 3: a voice
from matplotlib.patches import FancyBboxPatch
fig, ax = plt.subplots(figsize=(7, 2.6)); ax.set_xlim(0, 100); ax.set_ylim(0, 36); ax.axis("off")
def box(x, y, w, h, t, sub="", hi=False):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.3,rounding_size=1.2",
                 fc="#fbe9e7" if hi else "#f4f4f4", ec=RED if hi else "#999999", lw=1.2))
    ax.text(x + w / 2, y + h / 2 + (1.6 if sub else 0), t, ha="center", va="center", fontsize=9, weight="bold")
    if sub: ax.text(x + w / 2, y + h / 2 - 2.4, sub, ha="center", va="center", fontsize=7, color="#555555")
def arr(x0, y0, x1, y1): ax.annotate("", (x1, y1), (x0, y0), arrowprops=dict(arrowstyle="->", color="#555555", lw=1))
box(1, 26, 17, 8, "OSC 1", "mask, 4x rate", True); box(1, 14, 17, 8, "OSC 2", "mask, 4x rate", True)
box(1, 2, 17, 8, "NOISE", "sample & hold")
box(24, 20, 14, 8, "ENV x2", "level"); box(24, 2, 14, 8, "ENV", "level")
box(43, 14, 13, 8, "MIX", "halfbands")
box(61, 14, 15, 8, "FILTER", "SVF 12/24 dB", True); box(61, 2, 15, 8, "ENV", "filter")
box(81, 23, 17, 8, "FX1", "CH DT BC RM"); box(81, 8, 17, 8, "FX2", "DL RV DT")
arr(18, 30, 24, 25); arr(18, 18, 24, 23); arr(18, 6, 24, 6)
arr(38, 24, 43, 19); arr(38, 6, 43, 16); arr(56, 18, 61, 18); arr(68.5, 10.3, 68.5, 13.7)
arr(76, 18, 81, 25); arr(89.5, 22.7, 89.5, 16.3); arr(89.5, 7.7, 89.5, 1)
ax.text(89.5, 0, "out", ha="center", va="top", fontsize=8)
fig.tight_layout(); fig.savefig("fig_voice.png", dpi=200)

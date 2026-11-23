/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the bitmask oscillator.
 *
 * The oscillator has one waveform, a sine, read by a 32-bit phase. The mask, a 9-bit number, decides how that phase
 * is read: its low five bits are a bit mask over the phase's top five bits (the waveform's 32 sections), its high four
 * bits are operations on the sections.
 *
 *   bits 0..4  KEEP     phase bit 27 + i is kept when bit i is set, cleared when not (an AND). A cleared bit makes
 *                       the sections it chose repeat those below it: 31 keeps everything (the sine), 15 drops the top
 *                       bit (the first half twice: rounded), 7 the top two (a quarter four times: a saw), 8 keeps one
 *                       middle bit (a square whose low half is small saws).
 *   bit 5      FLIP     the sections read backwards where the highest dropped bit of the phase is set
 *   bit 6      INVERT   the sections upside down where the dropped bits of the phase have odd parity
 *   bit 7      SILENCE  the sections silenced where the lowest dropped bit of the phase is set
 *   bit 8      REPEAT   each kept piece holds three turns of the sine instead of one
 *
 * With nothing dropped (KEEP = 31 or 15 with the top bit set), the operations act on the halves of the cycle, so every
 * mask of the form 16k - 1 stays rounded. Between two masks lie three sub-masks (the position's two fraction bits,
 * the mask envelope and the LFOs pass through them): they clear phase bits 25 and 26, below the sections, so the
 * pieces grow fine steps. Everything is precomputed per section, so a sample costs an AND, an XOR, a multiply and an
 * interpolated table read. After it: the DC taken out and the level evened (mask_table.h, tools/gen_masks.c). */
#pragma once
#include <stdint.h>
#include "fastmath.h"

#define BM_SIN_BITS 11
#define BM_SIN_N (1u << BM_SIN_BITS)
#define BM_MSB 0x80000000u
#define BM_WIN 0xF8000000u

typedef struct {
    uint32_t m;                            /* the mask, x4: its two low bits the sub-mask (0..2047) */
    uint32_t keep;                         /* the AND */
    uint32_t mul;                          /* 1, or 3 (REPEAT) */
    uint32_t x[32];                        /* per section: the XOR (FLIP: keep, else 0) */
    float g[32];                           /* per section: 1, -1 (INVERT) or 0 (SILENCE) */
    float dc, norm;
} bm_tab_t;

extern float bm_sin[BM_SIN_N + 1];
extern const float BM_DC[512], BM_NORM[512];

static inline int bm_parity5(uint32_t v)   /* the parity of bits 27..31 */
{
    v >>= 27;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (int)(v & 1u);
}

/* the sections' table for mask position m4 (the mask x 4: 0..2047) */
static inline void bm_build(bm_tab_t *t, uint32_t m4)
{
    uint32_t m = (m4 >> 2) & 511u, sub = m4 & 3u, ops = m >> 5, keep = 0xFFFFFFFFu, dm, top = 0, low = 0, s, i;
    for (i = 0; i < 5u; i++)
        if (!((m >> i) & 1u))
            keep &= ~(1u << (27u + i));
    if (sub & 1u)
        keep &= ~(1u << 25);
    if (sub & 2u)
        keep &= ~(1u << 26);
    dm = ~keep & BM_WIN;
    if (dm) {
        top = 1u << 31;
        while (!(dm & top))
            top >>= 1;
        low = dm & (0u - dm);
    }
    for (s = 0; s < 32u; s++) {
        uint32_t p = s << 27, d = p & dm;
        int kf = dm ? (p & top) != 0 : (p & BM_MSB) != 0;
        int ki = dm ? bm_parity5(d) : (p & BM_MSB) != 0;
        int kg = dm ? (p & low) != 0 : (p & BM_MSB) != 0;
        t->x[s] = (ops & 1u) && kf ? keep : 0u;
        t->g[s] = (ops & 4u) && kg ? 0.0f : (ops & 2u) && ki ? -1.0f : 1.0f;
    }
    t->m = m4;
    t->keep = keep;
    t->mul = ops & 8u ? 3u : 1u;
    t->dc = BM_DC[m];
    t->norm = BM_NORM[m];
}

/* one raw sample at phase p (before the DC and the level) */
static inline __attribute__((always_inline)) float bm_raw(const bm_tab_t *t, uint32_t p)
{
    uint32_t s = p >> 27, q = ((p & t->keep) ^ t->x[s]) * t->mul, i = q >> (32 - BM_SIN_BITS);
    float f = (float)((q >> (32 - BM_SIN_BITS - 16)) & 0xFFFFu) * (1.0f / 65536.0f), a = bm_sin[i];
    return (a + f * (bm_sin[i + 1] - a)) * t->g[s];
}

static inline void bm_sin_init(void)
{
    uint32_t i;
    for (i = 0; i <= BM_SIN_N; i++)
        bm_sin[i] = fm_sinf(FM_TWO_PI * (float)i / (float)BM_SIN_N);
}

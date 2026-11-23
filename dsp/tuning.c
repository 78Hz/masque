/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the tuning: KIST's (github.com/78Hz/kist, dsp/tuning.c): the A reference, equal divisions of the octave and
 * four historical temperaments on any root. A key is a step of the tuning: in an equal division each key is one
 * division, in a temperament one of its twelve notes. Everything that moves pitch in keys (SEMI, the octave, the
 * glides, the bends, the arpeggiator) moves it in steps; the arpeggiator's intervals land on the steps nearest them.
 *
 * The pitches are kept as log2(Hz) for every step from -64 to 191, and a pitch between two steps is read between
 * them (in log), so a glide or a bend passes smoothly through the tuning's own steps. */
#include "engine.h"

#define PURE_FIFTH 701.955001f             /* 1200 log2(3/2) */
#define PYTH_COMMA 23.460010f              /* 1200 log2(3^12 / 2^19) */
#define SYNT_COMMA 21.506290f              /* 1200 log2(81/80) */
#define SCHISMA 1.953721f                  /* the Pythagorean less the syntonic comma */
#define TLO (-64)
#define TN 256

static float l2tab[TN + 1];                /* log2(Hz) of step TLO + i */
static int edo_now = 12;

/* the twelve fifths of the circle, C-G .. F-C, in cents; each temperament's sum to 7 octaves */
static void fifths(int system, float f[12])
{
    int i;
    for (i = 0; i < 12; i++)
        f[i] = PURE_FIFTH;
    switch (system) {
    case TS_WERCKMEISTER3:                 /* C-G, G-D, D-A and B-F# narrowed by 1/4 Pythagorean comma */
        f[0] = f[1] = f[2] = f[5] = PURE_FIFTH - PYTH_COMMA / 4.0f;
        break;
    case TS_MEANTONE:                      /* every fifth 1/4 syntonic comma narrow; the wolf on G#-Eb */
        for (i = 0; i < 12; i++)
            f[i] = PURE_FIFTH - SYNT_COMMA / 4.0f;
        f[8] = 8400.0f - 11.0f * (PURE_FIFTH - SYNT_COMMA / 4.0f);
        break;
    case TS_KIRNBERGER3:                   /* C-G-D-A-E narrowed by 1/4 syntonic comma, F#-C# by a schisma */
        f[0] = f[1] = f[2] = f[3] = PURE_FIFTH - SYNT_COMMA / 4.0f;
        f[6] = PURE_FIFTH - SCHISMA;
        break;
    case TS_VALLOTTI:                      /* F-C-G-D-A-E-B narrowed by 1/6 Pythagorean comma, the rest pure */
        f[11] = f[0] = f[1] = f[2] = f[3] = f[4] = PURE_FIFTH - PYTH_COMMA / 6.0f;
        break;
    default:
        for (i = 0; i < 12; i++)
            f[i] = 700.0f;
        break;
    }
}

/* each pitch class's distance in cents from equal temperament, C at 0 (before A is pinned) */
static void positions(int system, float off[12])
{
    float f[12], c = 0.0f;
    int i, pc = 0;
    fifths(system, f);
    off[0] = 0.0f;
    for (i = 0; i < 11; i++) {
        c += f[i];
        pc = (pc + 7) % 12;
        while (c >= 1200.0f)
            c -= 1200.0f;
        off[pc] = c - 100.0f * (float)pc;
    }
}

/* a temperament's twelve notes in cents from equal temperament, A at 0 (for the screen too) */
void tuning_cents(int system, int root, float out[12])
{
    float off[12];
    int pc, a;
    if (system == TS_EQUAL) {
        for (pc = 0; pc < 12; pc++)
            out[pc] = 0.0f;
        return;
    }
    positions(system, off);
    root = ((root % 12) + 12) % 12;
    a = (9 - root + 12) % 12;              /* A, counted from the root */
    for (pc = 0; pc < 12; pc++)
        out[pc] = off[(pc - root + 12) % 12] - off[a];
}

void tuning_set(const mq_global_t *g)
{
    float ref = 0.01f * (float)g->a_hz100, l2a = fm_log2f(ref), t[12];
    int i, sys = g->system < NTS ? g->system : TS_EQUAL;
    edo_now = sys == TS_EQUAL ? (g->edo < 5 ? 5 : g->edo > 72 ? 72 : g->edo) : 12;
    tuning_cents(sys, g->root, t);
    for (i = 0; i <= TN; i++) {
        int key = TLO + i;
        if (sys == TS_EQUAL)
            l2tab[i] = l2a + (float)(key - 69) / (float)edo_now;
        else
            l2tab[i] = l2a + (float)(key - 69) / 12.0f + t[((key % 12) + 12) % 12] / 1200.0f;
    }
}

float tuning_l2(float steps)               /* log2(Hz) of a pitch in steps, between steps in log */
{
    float x = steps - (float)TLO;
    int i;
    if (x < 0.0f) x = 0.0f;
    if (x > (float)TN - 0.001f) x = (float)TN - 0.001f;
    i = (int)x;
    return l2tab[i] + (x - (float)i) * (l2tab[i + 1] - l2tab[i]);
}

int tuning_edo(void) { return edo_now; }

int tuning_interval(int semis)              /* an interval of the twelve, in the tuning's nearest steps */
{
    float s = (float)semis * (float)edo_now / 12.0f;
    return (int)(s + (s >= 0.0f ? 0.5f : -0.5f));
}

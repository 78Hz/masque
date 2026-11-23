/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the two effects after the voices, as the MASK1 has them:
 *   FX1  CH1..16 chorus (11..14 flangers, 15..16 phasers), DT1..16 distortion, BC1..16 bitcrusher (9..16 with an LFO
 *        of their own), RM1..16 ring modulator (1..4 a stereo tremolo)
 *   FX2  DL1..16 delay (the even ones ping-pong), RV1..16 the plate (plate.c), DT1..16 distortion
 * each with its balance, 0 dry .. 100 wet. */
#include "engine.h"
#include "rv_common.h"
#include "plat.h"

#define FS MQ_FS
#define CH_N 2048u                         /* the chorus's lines: 46 ms */
#define DL_N 32768u                        /* the delay's: 0.74 s, 16-bit floating point */

static float chbuf[2][CH_N] PLAT_POOL;
static uint16_t dlbuf[2][DL_N] PLAT_POOL;

typedef struct { float lp, drive, gain; } dist_t;

static struct {
    int fam1, fam2;                        /* the families now: a new one starts clean */
    uint32_t chw;
    float chph, chfb[2], ap[2][6], apfb[2];
    dist_t d1, d2;
    float bc_hold[2], bc_cnt, bc_ph;
    float rm_ph;
    uint32_t dlw;
    float dl_lp[2], dl_t[2];
} F;

static float tmp_l[MQ_BLOCK_MAX], tmp_r[MQ_BLOCK_MAX];

void fx_clear(void)
{
    memset(chbuf, 0, sizeof chbuf);
    memset(dlbuf, 0, sizeof dlbuf);
    memset(F.chfb, 0, sizeof F.chfb);
    memset(F.ap, 0, sizeof F.ap);
    F.apfb[0] = F.apfb[1] = 0.0f;
    F.dl_lp[0] = F.dl_lp[1] = 0.0f;
    plate_clear();
}

void fx_init(void)
{
    memset(&F, 0, sizeof F);
    F.fam1 = F.fam2 = -1;
    plate_init();
    fx_clear();
}

static inline float ch_tap(int c, float d)   /* d samples back, fractional */
{
    float r = (float)F.chw - d;
    int i;
    float f;
    if (r < 0.0f) r += (float)CH_N;
    i = (int)r;
    f = r - (float)i;
    return chbuf[c][i & (CH_N - 1)] + f * (chbuf[c][(i + 1) & (CH_N - 1)] - chbuf[c][i & (CH_N - 1)]);
}

/* ----------------------------------------------------------------------------------------------- chorus --- */
static void chorus(float *l, float *r, uint32_t n, int v, float bal)
{
    uint32_t s;
    if (v >= 14) {                         /* the phasers: six first-order allpasses, swept */
        float hz = v == 14 ? 0.25f : 0.9f, fb = v == 14 ? 0.55f : 0.35f;
        for (s = 0; s < n; s++) {
            float sw, a, x[2] = {l[s], r[s]};
            int c, k;
            F.chph += hz / FS;
            if (F.chph >= 1.0f) F.chph -= 1.0f;
            for (c = 0; c < 2; c++) {
                float ph = F.chph + (c ? 0.25f : 0.0f), y;
                if (ph >= 1.0f) ph -= 1.0f;
                sw = 0.5f + 0.5f * fm_sinf(FM_TWO_PI * ph);
                a = 0.55f + 0.42f * sw;    /* the allpasses' coefficient: their notches from ~200 Hz to ~6 kHz */
                y = x[c] + fb * F.apfb[c];
                for (k = 0; k < 6; k++) {
                    float z = F.ap[c][k], o = -a * y + z;
                    F.ap[c][k] = y + a * o;
                    y = o;
                }
                F.apfb[c] = fm_flush(y);
                x[c] = (1.0f - 0.5f * bal) * x[c] + 0.5f * bal * (x[c] + y);
            }
            l[s] = x[0];
            r[s] = x[1];
        }
        return;
    }
    {
        int fl = v >= 10;                  /* 11..14: flangers */
        float base = fl ? 0.0015f * FS : (0.006f + 0.0025f * (float)(v % 5)) * FS;
        float depth = fl ? 0.0012f * FS : (0.0015f + 0.0006f * (float)v) * FS;
        float hz = fl ? 0.08f + 0.12f * (float)(v - 10) : 0.15f + 0.22f * (float)v;
        float fb = fl ? 0.45f + 0.12f * (float)(v - 10) : 0.0f;
        for (s = 0; s < n; s++) {
            float ph = F.chph, wl, wr, ml, mr;
            F.chph += hz / FS;
            if (F.chph >= 1.0f) F.chph -= 1.0f;
            ml = base + depth * (0.5f + 0.5f * fm_sinf(FM_TWO_PI * ph));
            mr = base + depth * (0.5f + 0.5f * fm_cosf(FM_TWO_PI * ph));
            wl = ch_tap(0, ml);
            wr = ch_tap(1, mr);
            chbuf[0][F.chw] = l[s] + fb * wl;
            chbuf[1][F.chw] = r[s] + fb * wr;
            F.chw = (F.chw + 1u) & (CH_N - 1);
            l[s] = (1.0f - 0.5f * bal) * l[s] + bal * (fl ? wl : 0.8f * wl);
            r[s] = (1.0f - 0.5f * bal) * r[s] + bal * (fl ? wr : 0.8f * wr);
        }
    }
}

/* ------------------------------------------------------------------------------------------- distortion --- */
static void distort(dist_t *d, float *l, float *r, uint32_t n, int v, float bal)
{
    float drive = fm_exp2f(0.42f * (float)v + 0.5f), comp = 0.9f / fm_sqrtf(fm_sqrtf(drive));
    float tone = 1.0f - fm_expf(-FM_TWO_PI * (9000.0f - 350.0f * (float)v) / FS);
    uint32_t s;
    int fold = v >= 12, asym = v & 1;
    for (s = 0; s < n; s++) {
        float x[2] = {l[s], r[s]};
        int c;
        for (c = 0; c < 2; c++) {
            float y = x[c] * drive;
            if (fold)                      /* wave folding: the sine of the driven signal */
                y = fm_sinf(0.5f * y);
            else
                y = fm_tanhf(asym ? y + 0.35f : y) - (asym ? 0.3364f : 0.0f);
            y *= comp;
            if (c == 0) { d->lp += tone * (y - d->lp); y = d->lp; }
            else { d->gain += tone * (y - d->gain); y = d->gain; }
            x[c] = (1.0f - bal) * x[c] + bal * y;
        }
        l[s] = x[0];
        r[s] = x[1];
    }
}

/* ------------------------------------------------------------------------------------------- bitcrusher --- */
static void crush(float *l, float *r, uint32_t n, int v, float bal)
{
    static const float HOLD[8] = {1.0f, 1.0f, 2.0f, 2.0f, 3.0f, 4.0f, 6.0f, 9.0f};
    int k = v & 7, lfo = v >= 8;
    float q = fm_exp2f(11.0f - 1.15f * (float)k), iq = 1.0f / q, hold = HOLD[k];
    uint32_t s;
    for (s = 0; s < n; s++) {
        float h = hold;
        if (lfo) {
            F.bc_ph += (0.25f + 0.35f * (float)k) / FS;
            if (F.bc_ph >= 1.0f) F.bc_ph -= 1.0f;
            h = hold * (1.0f + 3.0f * (0.5f + 0.5f * fm_sinf(FM_TWO_PI * F.bc_ph)));
        }
        F.bc_cnt += 1.0f;
        if (F.bc_cnt >= h) {
            F.bc_cnt -= h;
            if (F.bc_cnt >= h) F.bc_cnt = 0.0f;
            F.bc_hold[0] = (float)(int)(l[s] * q + (l[s] >= 0.0f ? 0.5f : -0.5f)) * iq;
            F.bc_hold[1] = (float)(int)(r[s] * q + (r[s] >= 0.0f ? 0.5f : -0.5f)) * iq;
        }
        l[s] = (1.0f - bal) * l[s] + bal * F.bc_hold[0];
        r[s] = (1.0f - bal) * r[s] + bal * F.bc_hold[1];
    }
}

/* ------------------------------------------------------------------------------------------ ring / tremolo --- */
static void ring(float *l, float *r, uint32_t n, int v, float bal)
{
    uint32_t s;
    if (v < 4) {                           /* a stereo tremolo, left and right in turn */
        float hz = 1.5f + 1.8f * (float)v;
        for (s = 0; s < n; s++) {
            float c;
            F.rm_ph += hz / FS;
            if (F.rm_ph >= 1.0f) F.rm_ph -= 1.0f;
            c = fm_sinf(FM_TWO_PI * F.rm_ph);
            l[s] *= 1.0f - bal * (0.5f + 0.5f * c);
            r[s] *= 1.0f - bal * (0.5f - 0.5f * c);
        }
        return;
    }
    {
        float hz = 30.0f * fm_exp2f(0.58f * (float)(v - 4));   /* 30 Hz .. 2.5 kHz */
        for (s = 0; s < n; s++) {
            float c;
            F.rm_ph += hz / FS;
            if (F.rm_ph >= 1.0f) F.rm_ph -= 1.0f;
            c = fm_sinf(FM_TWO_PI * F.rm_ph);
            l[s] = (1.0f - bal) * l[s] + bal * l[s] * c;
            r[s] = (1.0f - bal) * r[s] + bal * r[s] * c;
        }
    }
}

/* ------------------------------------------------------------------------------------------------ delay --- */
static void delay(float *l, float *r, uint32_t n, int v, float bal)
{
    static const uint16_t MS[8] = {95, 140, 190, 250, 320, 400, 520, 700};
    float t = (float)MS[v >> 1] * 0.001f * FS, fb = 0.32f + 0.022f * (float)v, damp = 0.35f;
    int pp = v & 1;                        /* 2, 4, 6..: ping-pong */
    uint32_t s;
    if (t > (float)(DL_N - 4)) t = (float)(DL_N - 4);
    for (s = 0; s < n; s++) {
        float tl, tr, wl, wr, in = 0.5f * (l[s] + r[s]);
        uint32_t rl, rr;
        F.dl_t[0] += (t - F.dl_t[0]) * 0.0005f;   /* a new time glides there (tape-like) */
        F.dl_t[1] += ((pp ? t : 0.75f * t) - F.dl_t[1]) * 0.0005f;
        rl = (F.dlw - (uint32_t)F.dl_t[0]) & (DL_N - 1);
        rr = (F.dlw - (uint32_t)F.dl_t[1]) & (DL_N - 1);
        tl = h_dec(dlbuf[0][rl]);
        tr = h_dec(dlbuf[1][rr]);
        F.dl_lp[0] += damp * (tl - F.dl_lp[0]);
        F.dl_lp[1] += damp * (tr - F.dl_lp[1]);
        if (pp) {                          /* left feeds right, right feeds left; the input goes in on the left */
            wl = in + fb * F.dl_lp[1];
            wr = fb * F.dl_lp[0];
        } else {
            wl = l[s] + fb * F.dl_lp[0];
            wr = r[s] + fb * F.dl_lp[1];
        }
        dlbuf[0][F.dlw] = h_enc(fm_flush(wl));
        dlbuf[1][F.dlw] = h_enc(fm_flush(wr));
        F.dlw = (F.dlw + 1u) & (DL_N - 1);
        l[s] = (1.0f - 0.4f * bal) * l[s] + bal * tl;
        r[s] = (1.0f - 0.4f * bal) * r[s] + bal * tr;
    }
}

/* ------------------------------------------------------------------------------------------------ FX1, FX2 --- */
void fx_process(float *l, float *r, uint32_t n, const fx_ctl_t *c)
{
    int f1 = c->t1 ? (c->t1 - 1) / 16 : -1, v1 = c->t1 ? (c->t1 - 1) % 16 : 0;
    int f2 = c->t2 ? (c->t2 - 1) / 16 : -1, v2 = c->t2 ? (c->t2 - 1) % 16 : 0;
    uint32_t s;
    if (f1 != F.fam1) {                    /* a new effect starts from silence */
        F.fam1 = f1;
        memset(chbuf, 0, sizeof chbuf);
        memset(F.ap, 0, sizeof F.ap);
        F.d1.lp = F.d1.gain = 0.0f;
    }
    if (f2 != F.fam2) {
        F.fam2 = f2;
        if (f2 == 0) {
            memset(dlbuf, 0, sizeof dlbuf);
            F.dl_lp[0] = F.dl_lp[1] = 0.0f;
        } else if (f2 == 1) {
            plate_clear();
        }
        F.d2.lp = F.d2.gain = 0.0f;
    }
    if (c->b1 > 0.0f) {
        switch (f1) {
        case 0: chorus(l, r, n, v1, c->b1); break;
        case 1: distort(&F.d1, l, r, n, v1, c->b1); break;
        case 2: crush(l, r, n, v1, c->b1); break;
        case 3: ring(l, r, n, v1, c->b1); break;
        default: break;
        }
    }
    if (c->b2 > 0.0f) {
        switch (f2) {
        case 0: delay(l, r, n, v2, c->b2); break;
        case 1:
            plate_variant(v2);
            for (s = 0; s < n; s++) {
                tmp_l[s] = l[s];
                tmp_r[s] = r[s];
                l[s] *= 1.0f - 0.4f * c->b2;
                r[s] *= 1.0f - 0.4f * c->b2;
            }
            plate_process(tmp_l, tmp_r, l, r, n, 1.6f * c->b2);
            break;
        case 2: distort(&F.d2, l, r, n, v2, c->b2); break;
        default: break;
        }
    }
}

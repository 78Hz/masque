/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the two effects after the voices, one after the other. Either slot takes any effect, with its MIX (equal
 * power: 50 is as much effect as dry) and up to six settings of its own:
 *   CHORUS (up to three voices a side), FLANGER, PHASER (2..12 stages), DISTORTION (four curves, 4x oversampled),
 *   WAVEFOLDER (4x), BITCRUSHER, RING MOD, TREMOLO (tempo sync, auto-pan), MASK SHAPER, DELAY (tempo sync, ping-pong,
 *   tape wow), PLATE REVERB (KIST-P's plate), MASK SLICER.
 * Two effects that need the same memory cannot be together: the chorus and the flanger (their short line), the delay
 * and the mask slicer (the long one), and two plates. A new effect in a slot fades the old one out, clears it and
 * fades in (no click, no old tail).
 *
 * The mask shaper reads the signal's level as the phase of a masked sine (dsp/bitmask.h): mask 31 a gentle sine fold,
 * every other mask its own transfer curve; DRIVE how far the signal sweeps it; 4x oversampled. The mask slicer keeps
 * the last WINDOW of the sound and plays it back cut into the 32 sections of a cycle and put together again by a
 * mask, as the oscillators read their sine: mask 31 plays the window as it was (one window late); others repeat,
 * reverse, invert or silence its sections. Where a section's sound does not follow on from the last, it is faded in
 * and out (FADE) so the cuts do not click. */
#include "engine.h"
#include "bitmask.h"
#include "rv_common.h"
#include "plat.h"

#define FS MQ_FS
#define CH_N 2048u                         /* the chorus's and flanger's line: 46 ms */
#define DL_N 32768u                        /* the delay's and the slicer's: 0.74 s, 16-bit floating point */

static float chbuf[2][CH_N] PLAT_POOL;
static uint16_t dlbuf[2][DL_N] PLAT_POOL;

/* ------------------------------------------------------------------------------- the effects' settings --- */
enum { U_NUM, U_BIP, U_RATE, U_TRATE, U_CHMS, U_LOWCUT, U_TONE, U_CENTRE, U_STAGES, U_MANUAL, U_CURVE, U_BITS,
       U_SRATE, U_RINGHZ, U_MASK, U_DLMS, U_WINMS, U_NOTE, U_ONOFF, U_DECAY, U_DAMP, U_MS, U_FADE, U_FOLD };
typedef struct { const char *lab; uint8_t unit, at; int16_t lo, hi, def; } kdef_t;
typedef struct {
    const char *sname, *lname;
    uint8_t res;                           /* the memory it needs: 1 the short line, 2 the long one, 3 the plate */
    int8_t time_k, sync_k;                 /* a time that syncs: its knob, the SYNC knob (-1: none) */
    kdef_t k[MQ_FX_K];
    kdef_t note;                           /* the time as a note value, when synced */
} edef_t;

#define K_(l, u, a, lo, hi, d) {l, u, a, lo, hi, d}
#define NOK {0, 0, 0, 0, 0, 0}
static const edef_t E[NEFX] = {
    [E_OFF] = {"OFF", "OFF", 0, -1, -1, {NOK, NOK, NOK, NOK, NOK, NOK}, NOK},
    [E_CHORUS] = {"CHORUS", "CHORUS", 1, -1, -1,
                  {K_("RATE", U_RATE, 0, 0, 99, 45), K_("DEPTH", U_NUM, 1, 0, 99, 50), K_("VOICES", U_NUM, 2, 1, 3, 3),
                   K_("SPREAD", U_NUM, 3, 0, 99, 50), K_("DELAY", U_CHMS, 4, 0, 99, 30),
                   K_("LOW CUT", U_LOWCUT, 5, 0, 99, 40)}, NOK},
    [E_FLANGER] = {"FLANGER", "FLANGER", 1, -1, -1,
                   {K_("RATE", U_RATE, 0, 0, 99, 20), K_("DEPTH", U_NUM, 1, 0, 99, 60),
                    K_("FEEDBACK", U_BIP, 2, -99, 99, 60), K_("SPREAD", U_NUM, 3, 0, 99, 30),
                    K_("MANUAL", U_MANUAL, 4, 0, 99, 40), K_("TONE", U_TONE, 5, 0, 99, 70)}, NOK},
    [E_PHASER] = {"PHASER", "PHASER", 0, -1, -1,
                  {K_("RATE", U_RATE, 0, 0, 99, 35), K_("DEPTH", U_NUM, 1, 0, 99, 80),
                   K_("FEEDBACK", U_BIP, 2, -99, 99, 40), K_("SPREAD", U_NUM, 3, 0, 99, 50),
                   K_("STAGES", U_STAGES, 4, 1, 6, 3), K_("CENTRE", U_CENTRE, 5, 0, 99, 50)}, NOK},
    [E_DIST] = {"DISTORT", "DISTORTION", 0, -1, -1,
                {K_("DRIVE", U_NUM, 0, 0, 99, 40), K_("TONE", U_TONE, 1, 0, 99, 70), K_("CURVE", U_CURVE, 2, 0, 3, 0),
                 K_("BIAS", U_NUM, 3, 0, 99, 0), K_("LOW CUT", U_LOWCUT, 4, 0, 99, 0), K_("LEVEL", U_NUM, 5, 0, 99, 70)},
                NOK},
    [E_FOLD] = {"FOLDER", "WAVEFOLDER", 0, -1, -1,
                {K_("FOLD", U_FOLD, 0, 0, 99, 40), K_("BIAS", U_NUM, 1, 0, 99, 0), K_("TONE", U_TONE, 2, 0, 99, 70),
                 K_("LEVEL", U_NUM, 3, 0, 99, 70), NOK, NOK}, NOK},
    [E_CRUSH] = {"CRUSHER", "BITCRUSHER", 0, -1, -1,
                 {K_("BITS", U_BITS, 0, 1, 16, 8), K_("RATE", U_SRATE, 1, 0, 99, 30), K_("JITTER", U_NUM, 2, 0, 99, 0),
                  K_("TONE", U_TONE, 3, 0, 99, 85), NOK, NOK}, NOK},
    [E_RING] = {"RING MOD", "RING MOD", 0, -1, -1,
                {K_("FREQ", U_RINGHZ, 0, 0, 99, 75), K_("DEPTH", U_NUM, 1, 0, 99, 99),
                 K_("SPREAD", U_NUM, 2, 0, 99, 0), K_("SHAPE", U_NUM, 3, 0, 99, 0), NOK, NOK}, NOK},
    [E_TREM] = {"TREMOLO", "TREMOLO", 0, 0, 2,
                {K_("RATE", U_TRATE, 0, 0, 99, 50), K_("DEPTH", U_NUM, 1, 0, 99, 70), K_("SYNC", U_ONOFF, 2, 0, 1, 0),
                 K_("SHAPE", U_NUM, 3, 0, 99, 0), K_("SPREAD", U_NUM, 4, 0, 99, 0), NOK},
                K_("RATE", U_NOTE, 5, 1, MQ_NSYNC - 1, 7)},
    [E_SHAPER] = {"SHAPER", "MASK SHAPER", 0, -1, -1,
                  {K_("MASK", U_MASK, 0, 0, 511, 31), K_("DRIVE", U_NUM, 2, 0, 99, 30), K_("BIAS", U_NUM, 3, 0, 99, 0),
                   K_("TONE", U_TONE, 4, 0, 99, 85), NOK, NOK}, NOK},
    [E_DELAY] = {"DELAY", "DELAY", 2, 0, 2,
                 {K_("TIME", U_DLMS, 0, 0, 99, 70), K_("FEEDBACK", U_NUM, 1, 0, 99, 40), K_("SYNC", U_ONOFF, 2, 0, 1, 0),
                  K_("PING-PONG", U_ONOFF, 3, 0, 1, 0), K_("TONE", U_TONE, 4, 0, 99, 60), K_("WOW", U_NUM, 5, 0, 99, 0)},
                 K_("TIME", U_NOTE, 6, 1, MQ_NSYNC - 1, 7)},
    [E_PLATE] = {"PLATE", "PLATE REVERB", 3, -1, -1,
                 {K_("SIZE", U_NUM, 0, 0, 99, 60), K_("DECAY", U_DECAY, 1, 0, 99, 50), K_("DAMP", U_DAMP, 2, 0, 99, 50),
                  K_("PRE-DLY", U_MS, 3, 0, 99, 10), K_("LOW CUT", U_LOWCUT, 4, 0, 99, 20),
                  K_("WIDTH", U_NUM, 5, 0, 99, 99)}, NOK},
    [E_SLICER] = {"SLICER", "MASK SLICER", 2, 1, 2,
                  {K_("MASK", U_MASK, 0, 0, 511, 15), K_("WINDOW", U_WINMS, 2, 0, 99, 60),
                   K_("SYNC", U_ONOFF, 3, 0, 1, 0), K_("TONE", U_TONE, 4, 0, 99, 90), K_("FADE", U_FADE, 5, 0, 99, 50),
                   NOK},
                  K_("WINDOW", U_NOTE, 6, 1, MQ_NSYNC - 1, 4)},
};

static const char *const SYNC_N[MQ_NSYNC] = {"OFF", "1/64", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                             "1/4T", "1/4", "1/4D", "1/2T", "1/2"};
static const float SYNC_BEATS[MQ_NSYNC] = {0.0f, 0.0625f, 0.125f, 0.1666667f, 0.25f, 0.375f, 0.3333333f, 0.5f, 0.75f,
                                           0.6666667f, 1.0f, 1.5f, 1.3333333f, 2.0f};
static const char *const CURVES[4] = {"SOFT", "HARD", "TUBE", "FUZZ"};

static int okt(int t) { return t > E_OFF && t < NEFX; }
const char *masque_fx_short(int t) { return t >= 0 && t < NEFX ? E[t].sname : "?"; }
const char *masque_fx_long(int t) { return t >= 0 && t < NEFX ? E[t].lname : "?"; }
int masque_fx_allowed(int t, int other)
{
    return !okt(t) || !okt(other) || !E[t].res || E[t].res != E[other].res;
}
int masque_fx_synced(int t, const uint8_t p[MQ_FX_P])
{
    return okt(t) && E[t].sync_k >= 0 && p[E[t].k[E[t].sync_k].at];
}

void masque_fx_defaults(int t, uint8_t p[MQ_FX_P])
{
    int k;
    for (k = 0; k < MQ_FX_P; k++)
        p[k] = 0;
    if (!okt(t))
        return;
    for (k = 0; k < MQ_FX_K; k++) {
        const kdef_t *d = &E[t].k[k];
        if (!d->lab) continue;
        p[d->at] = (uint8_t)d->def;
        if (d->unit == U_MASK) p[d->at + 1] = (uint8_t)(d->def >> 8);
    }
    if (E[t].note.lab)
        p[E[t].note.at] = (uint8_t)E[t].note.def;
}

int masque_fx_knob(int t, int k, const uint8_t p[MQ_FX_P], mq_fxk_t *d)
{
    const kdef_t *x;
    if (!okt(t) || k < 0 || k >= MQ_FX_K || !E[t].k[k].lab)
        return 0;
    x = &E[t].k[k];
    if (k == E[t].time_k && p[E[t].k[E[t].sync_k].at])
        x = &E[t].note;                    /* synced: the time as a note value */
    d->label = x->lab;
    d->lo = x->lo;
    d->hi = x->hi;
    d->at = x->at;
    d->wide = x->unit == U_MASK;
    d->unit = x->unit;
    return 1;
}

int masque_fx_get(const mq_fxk_t *d, const uint8_t p[MQ_FX_P])
{
    if (d->wide) return p[d->at] | p[d->at + 1] << 8;
    if (d->lo < 0) return (int8_t)p[d->at];
    return p[d->at];
}

void masque_fx_set(const mq_fxk_t *d, uint8_t p[MQ_FX_P], int v)
{
    v = v < d->lo ? d->lo : v > d->hi ? d->hi : v;
    p[d->at] = (uint8_t)v;
    if (d->wide) p[d->at + 1] = (uint8_t)(v >> 8);
}

/* the values the knobs stand for (the DSP's and the panel's) */
static float rate_hz(float v) { return 0.05f * fm_exp2f(v / 14.0f); }           /* 0.05 .. 6.7 Hz */
static float trate_hz(float v) { return 0.1f * fm_exp2f(v / 13.0f); }           /* 0.1 .. 20 Hz */
static float chms(float v) { return 2.0f + 18.0f * v / 99.0f; }                 /* 2 .. 20 ms */
static float lowcut_hz(float v) { return 20.0f * fm_exp2f(v / 20.0f); }         /* 20 .. 620 Hz */
static float tone_hz(float v) { return 800.0f * fm_exp2f(v / 99.0f * 4.5f); }   /* 0.8 .. 18 kHz */
static float centre_hz(float v) { return 100.0f * fm_exp2f(v / 99.0f * 6.3f); } /* 100 Hz .. 7.9 kHz */
static float manual_ms(float v) { return 0.1f * fm_exp2f(v / 17.5f); }          /* 0.1 .. 5 ms */
static float hold_of(float v) { return fm_exp2f(v / 99.0f * 5.0f); }            /* 1 .. 32 samples */
static float ring_hz(float v) { return 0.3f * fm_exp2f(v / 8.0f); }             /* 0.3 Hz .. 1.6 kHz */
static float dl_ms(float v) { return 20.0f * fm_exp2f(v / 19.0f); }             /* 20 .. 740 ms */
static float win_ms(float v) { return 4.0f * fm_exp2f(v / 13.2f); }             /* 4 .. 720 ms */
static float decay_s(float v) { return 0.2f * fm_exp2f(v / 16.9f); }            /* 0.2 .. 11 s */
static float fade_ms(float v) { return 0.1f * fm_exp2f(v / 17.6f); }            /* 0.1 .. 5 ms */
static float fold_gain(float v) { return fm_exp2f(v / 99.0f * 4.0f); }          /* x1 .. x16 */

static int put_s(char *o, int n, const char *s) { while (*s) o[n++] = *s++; o[n] = 0; return n; }
static int put_i(char *o, int n, int x)
{
    char t[12];
    int i = 0;
    unsigned u;
    if (x < 0) { o[n++] = '-'; x = -x; }
    u = (unsigned)x;
    do { t[i++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (i) o[n++] = t[--i];
    o[n] = 0;
    return n;
}
static int put_f1(char *o, int n, float x)   /* one decimal below 10, else whole */
{
    if (x < 9.95f) {
        int t = (int)(x * 10.0f + 0.5f);
        n = put_i(o, n, t / 10);
        o[n++] = '.';
        o[n++] = (char)('0' + t % 10);
        o[n] = 0;
        return n;
    }
    return put_i(o, n, (int)(x + 0.5f));
}
static void put_hz(char *o, float hz)
{
    int n;
    if (hz >= 1000.0f) { n = put_f1(o, 0, hz / 1000.0f); put_s(o, n, "kHz"); }
    else if (hz < 10.0f) { n = put_f1(o, 0, hz); put_s(o, n, "Hz"); }
    else { n = put_i(o, 0, (int)(hz + 0.5f)); put_s(o, n, "Hz"); }
}
static void put_ms(char *o, float ms)
{
    int n;
    if (ms >= 1000.0f) { n = put_f1(o, 0, ms / 1000.0f); put_s(o, n, "s"); }
    else { n = put_f1(o, 0, ms); put_s(o, n, "ms"); }
}

void masque_fx_text(const mq_fxk_t *d, int v, char *o)
{
    float f = (float)v;
    o[0] = 0;
    switch (d->unit) {
    case U_RATE: put_hz(o, rate_hz(f)); break;
    case U_TRATE: put_hz(o, trate_hz(f)); break;
    case U_CHMS: put_ms(o, chms(f)); break;
    case U_LOWCUT: put_hz(o, lowcut_hz(f)); break;
    case U_TONE: put_hz(o, tone_hz(f)); break;
    case U_CENTRE: put_hz(o, centre_hz(f)); break;
    case U_STAGES: put_i(o, 0, 2 * v); break;
    case U_MANUAL: put_ms(o, manual_ms(f)); break;
    case U_CURVE: put_s(o, 0, CURVES[v & 3]); break;
    case U_SRATE: put_hz(o, FS / hold_of(f)); break;
    case U_RINGHZ: put_hz(o, ring_hz(f)); break;
    case U_DLMS: put_ms(o, dl_ms(f)); break;
    case U_WINMS: put_ms(o, win_ms(f)); break;
    case U_NOTE: put_s(o, 0, SYNC_N[v > 0 && v < MQ_NSYNC ? v : 0]); break;
    case U_ONOFF: put_s(o, 0, v ? "ON" : "OFF"); break;
    case U_DECAY: { int n = put_f1(o, 0, decay_s(f)); put_s(o, n, "s"); break; }
    case U_DAMP: put_hz(o, 100.0f * (10.0f + 90.0f * f / 99.0f)); break;
    case U_MS: { int n = put_i(o, 0, v); put_s(o, n, "ms"); break; }
    case U_FADE: put_ms(o, fade_ms(f)); break;
    case U_FOLD: { int n = put_s(o, 0, "x"); put_f1(o, n, fold_gain(f)); break; }
    default: put_i(o, 0, v); break;
    }
}

/* ------------------------------------------------------------------------------------------ the parts --- */
/* the 2x halfbands (15 taps, Kaiser 6, as the voices'): up (a sample in, two out) and down (two in, one out) */
#define HB1 0.300794f
#define HB3 (-0.06271f)
#define HB5 0.012712f
#define HB7 (-0.000676f)
typedef struct { float d[16]; int p; } hbu_t;   /* the last eight inputs, written twice */
typedef struct { float d[30]; int p; } hbd_t;   /* the last fifteen, written twice */
static inline void hb_up2(hbu_t *h, float x, float *y0, float *y1)
{
    const float *a;
    h->p = h->p ? h->p - 1 : 7;
    h->d[h->p] = h->d[h->p + 8] = x;
    a = h->d + h->p;
    *y0 = a[4];
    *y1 = 2.0f * (HB1 * (a[3] + a[4]) + HB3 * (a[2] + a[5]) + HB5 * (a[1] + a[6]) + HB7 * (a[0] + a[7]));
}
static inline float hb_down2(hbd_t *h, float x0, float x1)
{
    const float *d;
    h->p = h->p ? h->p - 1 : 14;
    h->d[h->p] = h->d[h->p + 15] = x0;
    h->p = h->p ? h->p - 1 : 14;
    h->d[h->p] = h->d[h->p + 15] = x1;
    d = h->d + h->p;
    return 0.5f * d[7] + HB1 * (d[6] + d[8]) + HB3 * (d[4] + d[10]) + HB5 * (d[2] + d[12]) + HB7 * (d[0] + d[14]);
}
typedef struct { hbu_t u1, u2a, u2b; hbd_t d1, d2a, d2b; } os4_t;   /* 1x -> 4x -> 1x, one channel */

static inline float csat_fx(float x)      /* a soft clip for the feedback paths: 1 at 1.5, the slope 1 at 0 */
{
    x = x > 1.5f ? 1.5f : x < -1.5f ? -1.5f : x;
    return x - 0.1481481f * x * x * x;
}
static inline float dcb(float *x1, float *y1, float x)   /* a DC blocker (about 5 Hz) */
{
    float y = x - *x1 + 0.9993f * *y1;
    *x1 = x;
    *y1 = fm_flush(y);
    return y;
}
static inline float lp1(float *s, float a, float x) { *s += a * (x - *s); return *s; }
static float lp_coef(float hz) { return 1.0f - fm_expf(-FM_TWO_PI * hz / FS); }
static inline float tri(float ph) { ph -= (float)(int)ph; return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; }
static float beats_s(int note, float bpm) { return SYNC_BEATS[note % MQ_NSYNC] * 60.0f / (bpm < 20.0f ? 20.0f : bpm); }

/* a slot: its effect and what that keeps (the lines are shared, one user at a time) */
typedef struct {
    int t;                                 /* the effect playing */
    float fade;                            /* its level: a new effect fades the old out, then itself in */
    uint8_t p[MQ_FX_P];                    /* its settings (kept while it fades out, as the new one's come in) */
    float pv[MQ_FX_K];                     /* the knobs' values now (the first moved by the LFOs and the matrix) */
    os4_t os[2];                           /* distortion, folder, shaper: 4x */
    bm_tab_t tab;                          /* the shaper's mask */
    float lp[2], hp[2], dx[2], dy[2];      /* a tone, a low cut, a DC blocker, each side */
    float ph;                              /* an LFO's phase */
    float ap[2][12], apfb[2];              /* the phaser */
    float bc_hold[2], bc_cnt, bc_len, bc_ph;
    uint32_t rng;
} slot_t;

static struct {
    slot_t s[2];
    /* the short line (chorus, flanger) */
    uint32_t chw;
    float chlp[2];
    /* the long line (delay, slicer) */
    uint32_t dlw;
    float dl_t[2], dl_lp[2], dl_hp[2], dl_wob;
    bm_tab_t mk_tab;
    uint8_t mk_cin[32], mk_cout[32];       /* the slicer's sections: their sound follows on from the last / to the next */
    uint32_t mk_ph, mk_inc, mk_start, mk_w, mk_w_next;
    float out_x[2], out_y[2];
} F PLAT_POOL;

static float wet_l[MQ_BLOCK_MAX] PLAT_POOL, wet_r[MQ_BLOCK_MAX] PLAT_POOL;

void fx_clear(void)
{
    int k;
    memset(chbuf, 0, sizeof chbuf);
    memset(dlbuf, 0, sizeof dlbuf);
    for (k = 0; k < 2; k++) {
        slot_t *s = &F.s[k];
        memset(s->os, 0, sizeof s->os);
        memset(s->ap, 0, sizeof s->ap);
        s->apfb[0] = s->apfb[1] = s->lp[0] = s->lp[1] = s->hp[0] = s->hp[1] = 0.0f;
    }
    F.chlp[0] = F.chlp[1] = 0.0f;
    F.dl_lp[0] = F.dl_lp[1] = F.dl_hp[0] = F.dl_hp[1] = 0.0f;
    plate_clear();
}

void fx_init(void)
{
    memset(&F, 0, sizeof F);
    F.s[0].fade = F.s[1].fade = 1.0f;
    F.s[0].rng = 0x51ED27u;
    F.s[1].rng = 0x2F00D5u;
    F.s[0].tab.m = F.s[1].tab.m = F.mk_tab.m = 0xFFFFFFFFu;
    F.mk_w = F.mk_w_next = 4410;
    F.mk_inc = (uint32_t)(4294967296.0f / 4410.0f);
    plate_init();
    fx_clear();
}

/* Hermite (4-point) read of the short line, d samples back */
static inline float ch_tap(int c, float d)
{
    float r = (float)F.chw - d, f, xm, x0, x1, x2;
    int i;
    if (r < 0.0f) r += (float)CH_N;
    i = (int)r;
    f = r - (float)i;
    xm = chbuf[c][(i - 1) & (CH_N - 1)];
    x0 = chbuf[c][i & (CH_N - 1)];
    x1 = chbuf[c][(i + 1) & (CH_N - 1)];
    x2 = chbuf[c][(i + 2) & (CH_N - 1)];
    return x0 + 0.5f * f * (x1 - xm + f * (2.0f * xm - 5.0f * x0 + 4.0f * x1 - x2 + f * (3.0f * (x0 - x1) + x2 - xm)));
}

/* ------------------------------------------------------------------------------------- chorus, flanger --- */
static void chorus(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n)
{
    float inc = rate_hz(S->pv[0]) / FS, dep = S->pv[1] / 99.0f, spr = 0.5f * S->pv[3] / 99.0f;
    int nv = (int)S->pv[2], k;
    float base = chms(S->pv[4]) * 0.001f * FS, depth = dep * 0.8f * (base - 2.0f), hc = lp_coef(lowcut_hz(S->pv[5]));
    float g;
    uint32_t s;
    nv = nv < 1 ? 1 : nv > 3 ? 3 : nv;
    g = nv == 1 ? 1.0f : nv == 2 ? 0.7f : 0.58f;
    for (s = 0; s < n; s++) {
        float wl = 0.0f, wr = 0.0f;
        S->ph += inc;
        if (S->ph >= 1.0f) S->ph -= 1.0f;
        for (k = 0; k < nv; k++) {         /* the voices a turn apart: a third, or a half */
            float o = (float)k / (float)nv;
            wl += ch_tap(0, base - depth * (0.5f + 0.5f * tri(S->ph + o)));
            wr += ch_tap(1, base - depth * (0.5f + 0.5f * tri(S->ph + o + spr)));
        }
        chbuf[0][F.chw] = il[s];
        chbuf[1][F.chw] = ir[s];
        F.chw = (F.chw + 1u) & (CH_N - 1);
        wl *= g;
        wr *= g;
        ol[s] = wl - lp1(&S->hp[0], hc, wl);   /* the wet without its lows: no mud */
        or_[s] = wr - lp1(&S->hp[1], hc, wr);
    }
}

static void flanger(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n)
{
    float inc = rate_hz(S->pv[0]) / FS, dep = S->pv[1] / 99.0f, fb = 0.95f * S->pv[2] / 99.0f;
    float spr = 0.5f * S->pv[3] / 99.0f, base = manual_ms(S->pv[4]) * 0.001f * FS + 1.0f;
    float depth = dep * 0.004f * FS, tc = lp_coef(tone_hz(S->pv[5]));
    uint32_t s;
    for (s = 0; s < n; s++) {
        float wl, wr;
        S->ph += inc;
        if (S->ph >= 1.0f) S->ph -= 1.0f;
        wl = ch_tap(0, base + depth * (0.5f + 0.5f * tri(S->ph)));
        wr = ch_tap(1, base + depth * (0.5f + 0.5f * tri(S->ph + spr)));
        lp1(&F.chlp[0], tc, wl);           /* the feedback damped (TONE) and held in */
        lp1(&F.chlp[1], tc, wr);
        chbuf[0][F.chw] = il[s] + fb * csat_fx(F.chlp[0]);
        chbuf[1][F.chw] = ir[s] + fb * csat_fx(F.chlp[1]);
        F.chw = (F.chw + 1u) & (CH_N - 1);
        ol[s] = wl;
        or_[s] = wr;
    }
    F.chlp[0] = fm_flush(F.chlp[0]);
    F.chlp[1] = fm_flush(F.chlp[1]);
}

/* ---------------------------------------------------------------------------------------------- phaser --- */
static void phaser(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n)
{
    float inc = rate_hz(S->pv[0]) / FS, dep = 3.0f * S->pv[1] / 99.0f, fb = 0.85f * S->pv[2] / 99.0f;
    float spr = 0.5f * S->pv[3] / 99.0f, c0 = centre_hz(S->pv[5]) * (FM_PI / FS);
    int st = 2 * (int)S->pv[4], c, k;
    uint32_t s;
    st = st < 2 ? 2 : st > 12 ? 12 : st;
    for (s = 0; s < n; s += 4) {           /* the sweep's coefficient every four samples */
        uint32_t j, e = s + 4u < n ? s + 4u : n;
        float a[2];
        S->ph += inc * 4.0f;
        if (S->ph >= 1.0f) S->ph -= 1.0f;
        for (c = 0; c < 2; c++) {          /* the allpasses' corner swept exponentially, octaves either way */
            float w = c0 * fm_exp2f(dep * fm_sinf(FM_TWO_PI * (S->ph + (c ? spr : 0.0f))));
            w = w > 1.4f ? 1.4f : w;
            a[c] = (1.0f - w) / (1.0f + w);
        }
        for (j = s; j < e; j++)
            for (c = 0; c < 2; c++) {
                float y = (c ? ir[j] : il[j]) + fb * S->apfb[c];
                for (k = 0; k < st; k++) {
                    float z = S->ap[c][k], o = -a[c] * y + z;
                    S->ap[c][k] = fm_flush(y + a[c] * o);
                    y = o;
                }
                S->apfb[c] = csat_fx(y);
                if (c) or_[j] = y; else ol[j] = y;
            }
    }
}

/* --------------------------------------------------------------------- distortion, folder, shaper: 4x --- */
enum { SH_SOFT, SH_HARD, SH_TUBE, SH_FUZZ, SH_FOLD, SH_MASK };
static void shape4(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n, float pre,
                   float bias, int mode, float post, float tone, float lowcut)
{
    uint32_t s;
    int c, j;
    float tc = lp_coef(tone), hc = lowcut > 25.0f ? lp_coef(lowcut) : 0.0f;
    for (s = 0; s < n; s++) {
        for (c = 0; c < 2; c++) {
            os4_t *o = &S->os[c];
            float x = c ? ir[s] : il[s], a, b, u[4], y;
            if (hc > 0.0f)                 /* the low cut first: a tighter drive */
                x -= lp1(&S->hp[c], hc, x);
            hb_up2(&o->u1, x, &a, &b);
            hb_up2(&o->u2a, a, &u[0], &u[1]);
            hb_up2(&o->u2b, b, &u[2], &u[3]);
            for (j = 0; j < 4; j++) {
                float z = u[j] * pre + bias;
                switch (mode) {
                case SH_HARD: z = 0.6667f * csat_fx(1.5f * z); break;   /* a hard knee */
                case SH_TUBE: z = z > 0.0f ? fm_tanhf(z) : 0.7f * fm_tanhf(1.4f * z); break;   /* uneven */
                case SH_FUZZ: z = fm_tanhf(3.0f * z + 0.4f * z * z); break;
                case SH_FOLD: z = fm_sinf(1.5707963f * z); break;   /* folds back on itself as it grows */
                case SH_MASK: {                /* the level as a masked sine's phase */
                    float ph = 0.25f * z;
                    ph -= fm_floorf(ph);
                    z = (bm_raw(&S->tab, (uint32_t)(ph * 4294967040.0f)) - S->tab.dc) * S->tab.norm;
                    break;
                }
                default: z = fm_tanhf(z); break;
                }
                u[j] = z;
            }
            y = hb_down2(&o->d1, hb_down2(&o->d2a, u[0], u[1]), hb_down2(&o->d2b, u[2], u[3]));
            y = dcb(&S->dx[c], &S->dy[c], y) * post;
            y = lp1(&S->lp[c], tc, y);
            if (c) or_[s] = y; else ol[s] = y;
        }
    }
    S->lp[0] = fm_flush(S->lp[0]);
    S->lp[1] = fm_flush(S->lp[1]);
}

static void distort(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n)
{
    float drive = fm_exp2f(S->pv[0] / 99.0f * 7.0f), bias = 0.8f * S->pv[3] / 99.0f;
    float post = fm_exp2f((S->pv[5] - 70.0f) / 18.0f) / fm_sqrtf(fm_sqrtf(drive));
    shape4(S, il, ir, ol, or_, n, drive, bias, (int)S->pv[2] & 3, post, tone_hz(S->pv[1]), lowcut_hz(S->pv[4]));
}

static void folder(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n)
{
    float g = fold_gain(S->pv[0]), post = fm_exp2f((S->pv[3] - 70.0f) / 18.0f);
    shape4(S, il, ir, ol, or_, n, g, S->pv[1] / 99.0f, SH_FOLD, post, tone_hz(S->pv[2]), 0.0f);
}

static void mask_shaper(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n)
{
    float drive = fm_exp2f(S->pv[1] / 99.0f * 4.0f), post = 0.8f / fm_sqrtf(1.5708f * drive);
    uint32_t m4 = (uint32_t)(fm_clampf(S->pv[0], 0.0f, 511.75f) * 4.0f);
    if (m4 != S->tab.m)
        bm_build(&S->tab, m4);
    shape4(S, il, ir, ol, or_, n, drive, S->pv[2] / 99.0f, SH_MASK, post, tone_hz(S->pv[3]), 0.0f);
}

/* ------------------------------------------------------------------------------------------- bitcrusher --- */
static void crush(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n)
{
    float q = fm_exp2f(S->pv[0] - 1.0f), iq = 1.0f / q, hold = hold_of(S->pv[1]), jit = S->pv[2] / 99.0f;
    float tc = lp_coef(tone_hz(S->pv[3]));
    uint32_t s;
    if (S->bc_len < 1.0f) S->bc_len = hold;
    for (s = 0; s < n; s++) {
        S->bc_cnt += 1.0f;
        if (S->bc_cnt >= S->bc_len) {      /* a new sample held: its length jittered (and swept, slowly) */
            S->bc_cnt -= S->bc_len;
            if (S->bc_cnt >= S->bc_len) S->bc_cnt = 0.0f;
            S->bc_ph += S->bc_len * 0.4f / FS;
            if (S->bc_ph >= 1.0f) S->bc_ph -= 1.0f;
            S->bc_len = hold * (1.0f + jit * (1.5f * (0.5f + 0.5f * fm_sinf(FM_TWO_PI * S->bc_ph)) +
                                              0.8f * eng_frand(&S->rng)));
            if (S->bc_len < 1.0f) S->bc_len = 1.0f;
            S->bc_hold[0] = fm_floorf(il[s] * q + 0.5f) * iq;
            S->bc_hold[1] = fm_floorf(ir[s] * q + 0.5f) * iq;
        }
        ol[s] = lp1(&S->lp[0], tc, S->bc_hold[0]);
        or_[s] = lp1(&S->lp[1], tc, S->bc_hold[1]);
    }
}

/* ----------------------------------------------------------------------------------- ring mod, tremolo --- */
static void ring(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n)
{
    float inc = ring_hz(S->pv[0]) / FS, dep = S->pv[1] / 99.0f, spr = 0.5f * S->pv[2] / 99.0f;
    float shp = 1.0f + 8.0f * S->pv[3] / 99.0f, norm = 1.0f / fm_tanhf(shp);
    uint32_t s;
    for (s = 0; s < n; s++) {
        float cl, cr;
        S->ph += inc;
        if (S->ph >= 1.0f) S->ph -= 1.0f;
        cl = fm_tanhf(shp * fm_sinf(FM_TWO_PI * S->ph)) * norm;   /* SHAPE: the sine squared off */
        cr = fm_tanhf(shp * fm_sinf(FM_TWO_PI * (S->ph + spr))) * norm;
        ol[s] = il[s] * (1.0f - dep + dep * cl);
        or_[s] = ir[s] * (1.0f - dep + dep * cr);
    }
}

static void tremolo(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n, const uint8_t *p,
                    float bpm)
{
    float hz = p[2] ? 1.0f / beats_s(p[5], bpm) : trate_hz(S->pv[0]);
    float inc = hz / FS, dep = S->pv[1] / 99.0f, shp = 1.0f + 8.0f * S->pv[3] / 99.0f, norm = 1.0f / fm_tanhf(shp);
    float spr = 0.5f * S->pv[4] / 99.0f;   /* 99: the two sides opposite, an auto-pan */
    uint32_t s;
    for (s = 0; s < n; s++) {
        float cl, cr;
        S->ph += inc;
        if (S->ph >= 1.0f) S->ph -= 1.0f;
        cl = fm_tanhf(shp * fm_sinf(FM_TWO_PI * S->ph)) * norm;
        cr = fm_tanhf(shp * fm_sinf(FM_TWO_PI * (S->ph + spr))) * norm;
        ol[s] = il[s] * (1.0f - dep * (0.5f + 0.5f * cl));
        or_[s] = ir[s] * (1.0f - dep * (0.5f + 0.5f * cr));
    }
}

/* ------------------------------------------------------------------------------------------------ delay --- */
static inline float dl_read(int c, float d)   /* d samples back, interpolated */
{
    float r = (float)F.dlw - d, f, a, b;
    int i;
    if (r < 0.0f) r += (float)DL_N;
    i = (int)r;
    f = r - (float)i;
    a = h_dec(dlbuf[c][i & (DL_N - 1)]);
    b = h_dec(dlbuf[c][(i + 1) & (DL_N - 1)]);
    return a + f * (b - a);
}

static void delay(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n, const uint8_t *p,
                  float bpm)
{
    float t = (p[2] ? beats_s(p[6], bpm) : dl_ms(S->pv[0]) * 0.001f) * FS;
    float fb = 0.97f * S->pv[1] / 99.0f, tc = lp_coef(tone_hz(S->pv[4])), hc = lp_coef(70.0f);
    float wob = 0.004f * S->pv[5] / 99.0f;
    int pp = p[3] != 0;
    uint32_t s;
    while (t > (float)(DL_N - 8)) t *= 0.5f;   /* a synced time too long for the line: half of it */
    for (s = 0; s < n; s++) {
        float tl, tr, wl, wr, w = 1.0f;
        if (wob > 0.0f) {                  /* the tape: a slow wow and a little flutter */
            F.dl_wob += 0.55f / FS;
            if (F.dl_wob >= 1.0f) F.dl_wob -= 1.0f;
            w = 1.0f + wob * (fm_sinf(FM_TWO_PI * F.dl_wob) + 0.3f * fm_sinf(FM_TWO_PI * 7.3f * F.dl_wob));
        }
        F.dl_t[0] += (t - F.dl_t[0]) * 0.0004f;   /* a new time glides there, its pitch bending like tape */
        F.dl_t[1] += ((pp ? t : 0.75f * t) - F.dl_t[1]) * 0.0004f;
        tl = dl_read(0, F.dl_t[0] * w);
        tr = dl_read(1, F.dl_t[1] * w);
        /* the repeats' tone: a low pass (TONE) and a low cut, and they saturate rather than run away */
        tl = lp1(&F.dl_lp[0], tc, tl);
        tr = lp1(&F.dl_lp[1], tc, tr);
        tl -= lp1(&F.dl_hp[0], hc, tl);
        tr -= lp1(&F.dl_hp[1], hc, tr);
        if (pp) {                          /* left feeds right, right left; the input goes in on the left */
            wl = 0.5f * (il[s] + ir[s]) + fb * tr;
            wr = fb * tl;
        } else {
            wl = il[s] + fb * tl;
            wr = ir[s] + fb * tr;
        }
        dlbuf[0][F.dlw] = h_enc(fm_flush(csat_fx(wl * 0.6667f) * 1.5f));
        dlbuf[1][F.dlw] = h_enc(fm_flush(csat_fx(wr * 0.6667f) * 1.5f));
        F.dlw = (F.dlw + 1u) & (DL_N - 1);
        ol[s] = tl;
        or_[s] = tr;
    }
    F.dl_lp[0] = fm_flush(F.dl_lp[0]);
    F.dl_lp[1] = fm_flush(F.dl_lp[1]);
}

/* ------------------------------------------------------------------------------------------------ plate --- */
static void plate(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n)
{
    static float in_l[MQ_BLOCK_MAX] PLAT_POOL, in_r[MQ_BLOCK_MAX] PLAT_POOL;
    float hc = lp_coef(lowcut_hz(S->pv[4])), wd = S->pv[5] / 99.0f;
    uint32_t s;
    plate_settings((int)S->pv[0], (int)S->pv[1], (int)S->pv[2], (int)S->pv[3]);
    for (s = 0; s < n; s++) {              /* the low cut into it */
        in_l[s] = il[s] - lp1(&S->hp[0], hc, il[s]);
        in_r[s] = ir[s] - lp1(&S->hp[1], hc, ir[s]);
        ol[s] = or_[s] = 0.0f;
    }
    plate_process(in_l, in_r, ol, or_, n, 1.6f);
    if (wd < 0.999f)                       /* WIDTH: the sides narrowed towards the middle */
        for (s = 0; s < n; s++) {
            float m = 0.5f * (ol[s] + or_[s]), d = 0.5f * (ol[s] - or_[s]) * wd;
            ol[s] = m + d;
            or_[s] = m - d;
        }
}

/* -------------------------------------------------------------------------------------- the mask slicer --- */
static uint32_t mk_map(const bm_tab_t *t, uint32_t ph) { return ((ph & t->keep) ^ t->x[ph >> 27]) * t->mul; }

static void mk_build(uint32_t m4)
{
    int s;
    bm_build(&F.mk_tab, m4);
    for (s = 0; s < 32; s++) {             /* a section follows on from the one before when its start is where that one
                                            * ended, the same way up */
        uint32_t a = mk_map(&F.mk_tab, (uint32_t)s << 27), b = mk_map(&F.mk_tab, ((uint32_t)s << 27) - 1u);
        int32_t d = (int32_t)(a - b);
        int prev = (s + 31) & 31;
        F.mk_cin[s] = (uint8_t)(d >= 0 && d < (1 << 22) && F.mk_tab.g[s] == F.mk_tab.g[prev] && F.mk_tab.mul == 1u);
    }
    for (s = 0; s < 32; s++)
        F.mk_cout[s] = F.mk_cin[(s + 1) & 31];
}

static void slicer(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n, const uint8_t *p,
                   float bpm)
{
    float w = (p[3] ? beats_s(p[6], bpm) : win_ms(S->pv[1]) * 0.001f) * FS;
    float tc = lp_coef(tone_hz(S->pv[3])), fmax = fade_ms(S->pv[4]) * 0.001f * FS;
    uint32_t m4 = (uint32_t)(fm_clampf(S->pv[0], 0.0f, 511.75f) * 4.0f), s;
    while (w > (float)(DL_N - 64)) w *= 0.5f;
    if (w < 64.0f) w = 64.0f;
    F.mk_w_next = (uint32_t)w;
    if (m4 != F.mk_tab.m)
        mk_build(m4);
    for (s = 0; s < n; s++) {
        uint32_t ph = F.mk_ph, q, sec = ph >> 27;
        float pos, sl = (float)F.mk_w / 32.0f, r = (float)(ph & 0x07FFFFFFu) * (1.0f / 134217728.0f) * sl;
        float fade = sl * 0.25f < fmax ? sl * 0.25f : fmax, env = 1.0f, g, a, b, f, rr;
        int i;
        dlbuf[0][F.dlw] = h_enc(il[s]);   /* the sound goes on into the line */
        dlbuf[1][F.dlw] = h_enc(ir[s]);
        q = mk_map(&F.mk_tab, ph);
        g = F.mk_tab.g[sec];
        if (fade < 1.0f) fade = 1.0f;
        if (!F.mk_cin[sec] && r < fade) env = r / fade;
        if (!F.mk_cout[sec] && sl - r < fade) env *= (sl - r) / fade;
        pos = (float)(q >> 8) * (1.0f / 16777216.0f) * (float)F.mk_w;   /* where in the window */
        rr = (float)F.mk_start + pos;
        i = (int)rr;
        f = rr - (float)i;
        a = h_dec(dlbuf[0][i & (DL_N - 1)]);
        b = h_dec(dlbuf[0][(i + 1) & (DL_N - 1)]);
        ol[s] = lp1(&S->lp[0], tc, (a + f * (b - a)) * g * env);
        a = h_dec(dlbuf[1][i & (DL_N - 1)]);
        b = h_dec(dlbuf[1][(i + 1) & (DL_N - 1)]);
        or_[s] = lp1(&S->lp[1], tc, (a + f * (b - a)) * g * env);
        F.dlw = (F.dlw + 1u) & (DL_N - 1);
        F.mk_ph = ph + F.mk_inc;
        if (F.mk_ph < ph) {                /* a new window: the one just past, played from the start; its new length */
            F.mk_w = F.mk_w_next;
            F.mk_inc = (uint32_t)(4294967296.0f / (float)F.mk_w);
            F.mk_start = (F.dlw - F.mk_w) & (DL_N - 1);
        }
    }
}

/* ------------------------------------------------------------------------------------------- the slots --- */
static void start(slot_t *S, int t)        /* an effect begins from silence */
{
    memset(S->os, 0, sizeof S->os);
    memset(S->ap, 0, sizeof S->ap);
    S->apfb[0] = S->apfb[1] = S->lp[0] = S->lp[1] = S->hp[0] = S->hp[1] = 0.0f;
    S->dx[0] = S->dx[1] = S->dy[0] = S->dy[1] = 0.0f;
    S->ph = 0.0f;
    S->bc_len = 0.0f;
    switch (E[t].res) {
    case 1: memset(chbuf, 0, sizeof chbuf); F.chlp[0] = F.chlp[1] = 0.0f; break;
    case 2:
        memset(dlbuf, 0, sizeof dlbuf);
        F.dl_lp[0] = F.dl_lp[1] = F.dl_hp[0] = F.dl_hp[1] = 0.0f;
        F.dl_t[0] = F.dl_t[1] = 0.0f;
        F.mk_ph = 0;
        F.mk_start = 0;
        break;
    case 3: plate_clear(); break;
    default: break;
    }
}

static void run(slot_t *S, const float *il, const float *ir, float *ol, float *or_, uint32_t n, const uint8_t *p,
                float bpm)
{
    switch (S->t) {
    case E_CHORUS: chorus(S, il, ir, ol, or_, n); break;
    case E_FLANGER: flanger(S, il, ir, ol, or_, n); break;
    case E_PHASER: phaser(S, il, ir, ol, or_, n); break;
    case E_DIST: distort(S, il, ir, ol, or_, n); break;
    case E_FOLD: folder(S, il, ir, ol, or_, n); break;
    case E_CRUSH: crush(S, il, ir, ol, or_, n); break;
    case E_RING: ring(S, il, ir, ol, or_, n); break;
    case E_TREM: tremolo(S, il, ir, ol, or_, n, p, bpm); break;
    case E_SHAPER: mask_shaper(S, il, ir, ol, or_, n); break;
    case E_DELAY: delay(S, il, ir, ol, or_, n, p, bpm); break;
    case E_PLATE: plate(S, il, ir, ol, or_, n); break;
    case E_SLICER: slicer(S, il, ir, ol, or_, n, p, bpm); break;
    default: {
        uint32_t s;
        for (s = 0; s < n; s++) { ol[s] = il[s]; or_[s] = ir[s]; }
        break;
    }
    }
}

/* the knobs' values, the first moved: a mask by mod x 128, the others by mod x 50 */
static void values(slot_t *S, const uint8_t *p, float mod)
{
    int k;
    for (k = 0; k < MQ_FX_K; k++) {
        mq_fxk_t d;
        if (!masque_fx_knob(S->t, k, p, &d)) {
            S->pv[k] = 0.0f;
            continue;
        }
        S->pv[k] = (float)masque_fx_get(&d, p);
        if (k == 0 && mod != 0.0f && d.unit != U_NOTE && d.unit != U_ONOFF)
            S->pv[0] = fm_clampf(S->pv[0] + mod * (d.wide ? 128.0f : 50.0f), (float)d.lo,
                                 d.wide ? 511.75f : (float)d.hi);
    }
}

/* one slot: the effect at its mix (equal power), faded across a change of effect */
static void slot(int k, const fx_ctl_t *c, float *l, float *r, uint32_t n)
{
    slot_t *S = &F.s[k];
    int t = okt(c->t[k]) ? c->t[k] : E_OFF, playing = S->t, other = F.s[k ^ 1].t;
    float mix = c->mix[k], dry = fm_cosf(1.5707963f * mix), wet = fm_sinf(1.5707963f * mix), step = (float)n / 480.0f;
    uint32_t s;
    if (t != playing) {                    /* out over 11 ms, then the new one starts clean and comes in */
        S->fade -= step;
        if (S->fade <= 0.0f) {
            S->fade = 0.0f;
            if (masque_fx_allowed(t, other)) {   /* (its memory free: the other slot's old effect gone) */
                S->t = playing = t;
                start(S, t);
            } else {
                S->t = playing = E_OFF;
            }
        }
    } else if (S->fade < 1.0f) {
        S->fade = S->fade + step > 1.0f ? 1.0f : S->fade + step;
    }
    if (playing == E_OFF || (mix <= 0.0f && E[playing].res != 2 && E[playing].res != 3))
        return;
    if (playing == t) {                    /* the settings are this effect's */
        memcpy(S->p, c->p[k], MQ_FX_P);
        values(S, S->p, c->mod[k]);
    }
    run(S, l, r, wet_l, wet_r, n, S->p, c->bpm);
    {
        float g = S->fade;
        for (s = 0; s < n; s++) {
            float ml = dry * l[s] + wet * wet_l[s], mr = dry * r[s] + wet * wet_r[s];
            l[s] += g * (ml - l[s]);
            r[s] += g * (mr - r[s]);
        }
    }
}

void fx_process(float *l, float *r, uint32_t n, const fx_ctl_t *c)
{
    uint32_t s;
    slot(0, c, l, r, n);
    slot(1, c, l, r, n);
    for (s = 0; s < n; s++) {              /* no DC out of the effects */
        l[s] = dcb(&F.out_x[0], &F.out_y[0], l[s]);
        r[s] = dcb(&F.out_x[1], &F.out_y[1], r[s]);
    }
}

/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the voices: two bitmask oscillators and a noise each, a two-pole state-variable filter (or two), four
 * loopable ADSRs, the mask and pitch delta envelopes, the LFOs and the play modes.
 *
 * A voice runs in steps of CR samples (the control rate, 5.5 kHz): its envelopes, masks, pitches and filter are worked
 * out once a step, the samples between follow. The oscillators run at four times the rate (their sharp edges alias
 * less), and come down through two halfbands before the noise and the filter. */
#include "engine.h"
#include "bitmask.h"
#include "mask_table.h"
#include "plat.h"

#define FS MQ_FS
#define CR 8u                              /* the control step, samples */
#define OS 4u                              /* the oscillators' oversampling */
#define NSTEP (MQ_BLOCK_MAX / CR)
#define DT ((float)CR / FS)

float bm_sin[BM_SIN_N + 1];

enum { E_O1, E_O2, E_N, E_F, NENV };
enum { ST_IDLE, ST_ATT, ST_DEC, ST_SUS, ST_REL };

typedef struct {
    float lvl;
    uint8_t st;
} env_t;

typedef struct {
    uint32_t ph, inc;
    float mdelta, mdir;                    /* the mask envelope: its offset from MASK now, and its way (+1 / -1) */
    float g, g_to;                         /* the gain now and at the step's end (envelope, level, the mask's own) */
    bm_tab_t tab;
} osc_t;

typedef struct {
    uint8_t used;                          /* allocated */
    uint8_t gate;                          /* its key is down (or the pedal holds it) */
    uint8_t pedal;                         /* released while the pedal is down */
    uint8_t core2, dead, quick;            /* the second core renders it; to free; a fast fade (paraphonic) */
    int8_t key;                            /* the key it sounds */
    int8_t stack[6];                       /* slurred keys still down under it, the latest last */
    uint8_t nstack;
    uint32_t age;
    float vel, at;                         /* velocity (after the curve) and pressure, 0..1 */
    float pitch, pitch_to, glide;          /* semitones: now, where it glides, the glide's coefficient a step */
    float penv;                            /* the pitch envelope's offset */
    float pan;
    float trk;                             /* the key, for the noise's and the filter's tracking */
    osc_t o[2];
    env_t env[NENV];
    float att[NENV], dec[NENV], rel[NENV];   /* this voice's rates (key scaling and velocity applied) */
    uint32_t nph, ninc, rng;
    float nval, ng, ng_to;
    float s1, s2, s3, s4, s5, s6;          /* the filters' states */
    float out_pk;                          /* the loudest sample of the last step: a ringing filter keeps it alive */
    float fg, fk, fg_to;                   /* the filter's coefficients */
    float h4[6], h2[14];                   /* the decimators' histories */
} voice_t;

static voice_t V[MQ_MAX_VOICES];
static mq_patch_t P;                       /* the patch the render plays */
static mq_global_t G = {4400, 2, 2, MQ_DEFAULT_VOICES, {0}};
static mq_meter_t meter;

/* what the patch gives, worked out when it comes */
static struct {
    float lv[NENV];                        /* each envelope's level: the oscillators' and the noise's a gain, the
                                            * filter's an amount in semitones */
    float t_a[NENV], t_d[NENV], t_r[NENV], sus[NENV];
    float x2g, x2k;                        /* the fixed second filter (LP1..7, HP1..7) */
    int x2hp;
    float fk;                              /* the filter's damping from RESO */
    float port_c;                          /* the glide's coefficient a step, 0: none */
    float pspd;                            /* the pitch envelope's speed, semitones a step */
    float mspd[2];                         /* the mask envelopes' speeds, masks a step */
    float lfo_hz[2];
    float vol;
} D;

/* the part: controllers and the LFOs */
static struct {
    float bend, bend_to, wheel, chat;
    int sustain;
    float lph[2], lval[2][NSTEP + 1], lhold[2], lenv_t;
    uint32_t lrng;
    float fx_t1, fx_t2, fx_b2, lfo_fx_dummy;
    float last_pitch;                      /* the last note's pitch, for the glides */
    int last_voice;
    uint32_t clock;
    int held;                              /* keys down (as played to the voices) */
    /* paraphonic: one set of envelopes for every voice */
    env_t penv[NENV];
    float plvl[NENV][NSTEP + 1];           /* their levels at each step of the block */
    float patt[NENV], pdec[NENV], prel[NENV];
    int allowed, shed_hold;
    uint32_t ok_blocks;
} S;

/* -------------------------------------------------------------------------------------- the patch's numbers --- */
static float env_time(int v) { return v <= 0 ? 0.0f : 0.001f * fm_exp2f((float)v / 7.3f); }   /* 1 ms .. 12 s */
static float level_gain(int l) { float x = (float)(l < 0 ? 0 : l) / 99.0f; return x * fm_sqrtf(x); }
static float rate_step(float t) { return t <= 0.0f ? 2.0f : DT / t; }   /* a linear attack's step */
static float exp_coef(float t) { return t <= 0.0f ? 0.0f : fm_expf(-6.9f * DT / t); }   /* -60 dB in t */
static float hz_of(float semis) { return 8.1757989f * fm_exp2f(semis / 12.0f); }

static const mq_env_t *penv_of(int e)
{
    return e == E_O1 ? &P.osc[0].env : e == E_O2 ? &P.osc[1].env : e == E_N ? &P.noise.env : &P.filt.env;
}

static void voice_rates(voice_t *v);

static void derive(void)
{
    int e, i;
    for (e = 0; e < NENV; e++) {
        const mq_env_t *q = penv_of(e);
        D.lv[e] = e == E_F ? (float)q->l * 0.75f : level_gain(q->l);   /* the filter's: up to +-74 semitones */
        D.t_a[e] = env_time(q->a);
        D.t_d[e] = env_time(q->d);
        D.t_r[e] = env_time(q->r);
        D.sus[e] = (float)q->s / 99.0f;
    }
    if (P.filt.extra >= FX_LP1) {
        static const float LPF[7] = {350.0f, 700.0f, 1200.0f, 2000.0f, 3300.0f, 5500.0f, 9000.0f};
        static const float HPF[7] = {40.0f, 80.0f, 150.0f, 280.0f, 500.0f, 900.0f, 1600.0f};
        int k = P.filt.extra >= FX_HP1 ? P.filt.extra - FX_HP1 : P.filt.extra - FX_LP1;
        float hz = P.filt.extra >= FX_HP1 ? HPF[k] : LPF[k];
        D.x2hp = P.filt.extra >= FX_HP1;
        D.x2g = fm_tanf(FM_PI * hz / FS);
        D.x2k = 1.4142136f;
    }
    {   /* RESO: 0 is a gentle damping (Q 0.5), 99 just rings */
        float r = (float)P.filt.reso / 99.0f;
        D.fk = 2.0f - 1.995f * r * (2.0f - r);
    }
    D.port_c = P.gen.port ? 1.0f - fm_expf(-DT / (0.004f * fm_exp2f((float)P.gen.port / 1.7f))) : 0.0f;
    D.pspd = P.pitch.spd ? fm_exp2f((float)P.pitch.spd / 4.0f) * DT : 0.0f;   /* 1.2 .. 1024 semitones a second */
    for (i = 0; i < 2; i++) {
        D.mspd[i] = P.osc[i].spd ? 0.5f * fm_exp2f((float)P.osc[i].spd / 10.0f) * DT : 0.0f;   /* 0.5 .. 470 masks/s */
        D.lfo_hz[i] = 0.03f * fm_exp2f((float)P.lfo[i].spd / 9.9f);   /* 0.03 .. 30 Hz */
    }
    D.vol = level_gain(P.gen.vol);
    for (i = 0; i < MQ_MAX_VOICES; i++)
        if (V[i].used)
            voice_rates(&V[i]);
}

/* the voice's envelope rates: the patch's times, the keyboard's rate scaling and velocity's attack */
static void voice_rates(voice_t *v)
{
    float ks = P.gen.rate ? fm_exp2f(-((float)v->key - 60.0f) / 24.0f * (float)P.gen.rate / 50.0f) : 1.0f;
    float va = 1.0f;
    int e;
    if (P.vel.dest == VD_ATK && P.vel.amt)  /* + : harder is faster */
        va = fm_exp2f(-(float)P.vel.amt / 99.0f * 4.0f * (v->vel - 0.5f));
    for (e = 0; e < NENV; e++) {
        v->att[e] = rate_step(D.t_a[e] * ks * va);
        v->dec[e] = exp_coef(D.t_d[e] * ks);
        v->rel[e] = exp_coef(D.t_r[e] * ks);
    }
}

/* ---------------------------------------------------------------------------------------------- envelopes --- */
static inline void env_gate(env_t *e, int reset)
{
    if (reset)
        e->lvl = 0.0f;
    e->st = ST_ATT;
}
static inline void env_release(env_t *e)
{
    if (e->st != ST_IDLE)
        e->st = ST_REL;
}
static inline void env_step(env_t *e, float att, float dec, float sus, float rel, int loop)
{
    switch (e->st) {
    case ST_ATT:
        e->lvl += att;
        if (e->lvl >= 1.0f) {
            e->lvl = 1.0f;
            e->st = ST_DEC;
        }
        break;
    case ST_DEC:
        e->lvl = sus + (e->lvl - sus) * dec;
        if (fm_fabsf(e->lvl - sus) < 0.002f) {
            e->lvl = sus;
            e->st = loop ? ST_ATT : ST_SUS;
        }
        break;
    case ST_REL:
        e->lvl *= rel;
        if (e->lvl < 0.0002f) {
            e->lvl = 0.0f;
            e->st = ST_IDLE;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------------------------------- the LFOs --- */
static float lfo_wave(int w, float ph, float *hold, uint32_t *rng, int wrapped)
{
    switch (w) {
    case LW_SIN: return fm_sinf(FM_TWO_PI * ph);
    case LW_SQR: return ph < 0.5f ? 1.0f : -1.0f;
    case LW_UP: return 2.0f * ph - 1.0f;
    case LW_DOWN: return 1.0f - 2.0f * ph;
    case LW_SAWS: { float x = ph * 2.0f; x -= (float)(int)x; return ph < 0.5f ? x : x - 1.0f; }   /* two small saws */
    case LW_STEP: return (float)(int)(ph * 4.0f) * (2.0f / 3.0f) - 1.0f;                        /* four stairs */
    case LW_HSIN: return 2.0f * fm_fabsf(fm_sinf(FM_PI * ph)) - 1.0f;                            /* half sines */
    default:
        if (wrapped)
            *hold = eng_frand(rng);
        return *hold;                                                                            /* sample and hold */
    }
}

/* the LFOs over a block: their values at each step, their delay and decay since the first key */
static void lfo_block(uint32_t steps)
{
    int i;
    uint32_t s;
    float wheel_amt[2] = {0, 0}, wheel_spd[2] = {0, 0};
    for (i = 0; i < 2; i++) {               /* the wheel's and aftertouch's hold on the LFOs */
        float w = P.wheel.amt / 99.0f * S.wheel, a = P.at.amt / 99.0f * S.chat;
        if (P.wheel.dest == (i ? MD_L2AMT : MD_L1AMT)) wheel_amt[i] += w;
        if (P.at.dest == (i ? MD_L2AMT : MD_L1AMT)) wheel_amt[i] += a;
        if (P.wheel.dest == (i ? MD_L2SPD : MD_L1SPD)) wheel_spd[i] += w;
        if (P.at.dest == (i ? MD_L2SPD : MD_L1SPD)) wheel_spd[i] += a;
    }
    for (s = 0; s <= steps; s++) {
        for (i = 0; i < 2; i++) {
            float amt = (float)P.lfo[i].amt / 99.0f + wheel_amt[i], env = 1.0f, hz, ph;
            float dl = env_time(P.lfo[i].dlay), dc = env_time(P.lfo[i].dcay), t = S.lenv_t;
            int wrapped;
            if (amt < 0.0f) amt = 0.0f;
            if (amt > 1.5f) amt = 1.5f;
            /* the delay holds it off while the wheel and aftertouch leave it alone (else they are in charge) */
            if (P.lfo[i].dlay && wheel_amt[i] == 0.0f && wheel_spd[i] == 0.0f)
                env = t < dl ? 0.0f : t < 1.25f * dl ? (t - dl) / (0.25f * dl) : 1.0f;
            if (P.lfo[i].dcay) {
                float t2 = t - (P.lfo[i].dlay ? 1.25f * dl : 0.0f);
                if (t2 > 0.0f)
                    env *= fm_expf(-3.0f * t2 / dc);
            }
            hz = D.lfo_hz[i] * fm_exp2f(3.0f * wheel_spd[i]);
            ph = S.lph[i] + hz * DT;
            wrapped = ph >= 1.0f;
            if (wrapped)
                ph -= (float)(int)ph;
            S.lph[i] = ph;
            S.lval[i][s] = lfo_wave(P.lfo[i].wave, ph, &S.lhold[i], &S.lrng, wrapped) * amt * env;
        }
        S.lenv_t += DT;
    }
}

/* ------------------------------------------------------------------------------------------- modulation --- */
typedef struct {
    float freq, vol, filt, mask, pan, o1freq, o1vol, o1mask, o2freq, o2vol, o2mask, nfreq, nvol;
} mods_t;

/* an amount (-1..1 of its range) into a destination, in that destination's units */
static void mod_lfo(mods_t *m, int dest, float x)
{
    switch (dest) {
    case LD_FREQ: m->freq += x * fm_fabsf(x) * 12.0f; break;   /* the square: fine vibrato low on the knob */
    case LD_VOL: m->vol += x; break;
    case LD_FILT: m->filt += x * 48.0f; break;
    case LD_MASK: m->mask += x * fm_fabsf(x) * 256.0f; break;
    case LD_PAN: m->pan += x; break;
    case LD_O1FREQ: m->o1freq += x * fm_fabsf(x) * 12.0f; break;
    case LD_O1VOL: m->o1vol += x; break;
    case LD_O1MASK: m->o1mask += x * fm_fabsf(x) * 256.0f; break;
    case LD_O2FREQ: m->o2freq += x * fm_fabsf(x) * 12.0f; break;
    case LD_O2VOL: m->o2vol += x; break;
    case LD_O2MASK: m->o2mask += x * fm_fabsf(x) * 256.0f; break;
    case LD_NFREQ: m->nfreq += x * 50.0f; break;
    case LD_NVOL: m->nvol += x; break;
    default: break;                        /* the effects': taken per block (fx_mods) */
    }
}
static void mod_ctl(mods_t *m, int dest, float x)   /* the wheel and aftertouch (x: amount x position) */
{
    switch (dest) {
    case MD_FLT: m->filt += x * 60.0f; break;
    case MD_VOL: m->vol += x; break;
    case MD_MASK: m->mask += x * fm_fabsf(x) * 256.0f; break;
    case MD_FREQ: m->freq += x * 12.0f; break;
    case MD_O1VOL: m->o1vol += x; break;
    case MD_O1MASK: m->o1mask += x * fm_fabsf(x) * 256.0f; break;
    case MD_O2VOL: m->o2vol += x; break;
    case MD_O2MASK: m->o2mask += x * fm_fabsf(x) * 256.0f; break;
    case MD_NFREQ: m->nfreq += x * 50.0f; break;
    case MD_NVOL: m->nvol += x; break;
    default: break;
    }
}
static inline float volmul(float x) { x += 1.0f; return x < 0.0f ? 0.0f : x; }

/* --------------------------------------------------------------------------------------------- the step --- */
static void voice_control(voice_t *v, uint32_t step)
{
    mods_t m;
    int i, para = P.gen.mode == PM_PARA || P.gen.mode == PM_PARA_SLUR;
    float lvl[NENV], base, a_ref = (float)G.a_hz10 * 0.1f;
    memset(&m, 0, sizeof m);
    mod_lfo(&m, P.lfo[0].dest, S.lval[0][step]);
    mod_lfo(&m, P.lfo[1].dest, S.lval[1][step]);
    if (P.wheel.amt)
        mod_ctl(&m, P.wheel.dest, (float)P.wheel.amt / 99.0f * S.wheel);
    if (P.at.amt) {
        float at = v->at > S.chat ? v->at : S.chat;
        mod_ctl(&m, P.at.dest, (float)P.at.amt / 99.0f * at);
    }
    if (P.vel.amt) {
        float x = (float)P.vel.amt / 99.0f, vv = v->vel;
        if (P.vel.dest == VD_FLT) m.filt += x * (vv - 0.5f) * 72.0f;
        else if (P.vel.dest == VD_VOL) m.vol += x > 0.0f ? -x * (1.0f - vv) : -x * vv;
        else if (P.vel.dest == VD_MASK) m.mask += x * vv * 128.0f;
    }
    /* the envelopes */
    if (!para)
        for (i = 0; i < NENV; i++)
            env_step(&v->env[i], v->att[i], v->dec[i], D.sus[i], v->rel[i], penv_of(i)->loop);
    for (i = 0; i < NENV; i++)
        lvl[i] = para ? S.plvl[i][step] : v->env[i].lvl;
    if (v->quick) {                        /* a paraphonic voice let go: out fast */
        v->o[0].g_to = v->o[1].g_to = v->ng_to = 0.0f;
    }
    /* the pitch: the glide, the pitch envelope, the bend */
    if (v->pitch != v->pitch_to) {
        if (v->glide > 0.0f) {
            v->pitch += (v->pitch_to - v->pitch) * v->glide;
            if (fm_fabsf(v->pitch_to - v->pitch) < 0.002f)
                v->pitch = v->pitch_to;
        } else {
            v->pitch = v->pitch_to;
        }
    }
    if (D.pspd > 0.0f) {
        float to = v->gate ? 0.0f : (float)P.pitch.end;
        if (v->penv < to) v->penv = v->penv + D.pspd > to ? to : v->penv + D.pspd;
        else if (v->penv > to) v->penv = v->penv - D.pspd < to ? to : v->penv - D.pspd;
    } else {
        v->penv = 0.0f;
    }
    base = v->pitch + v->penv + (float)P.gen.semi + S.bend * (float)P.gen.bend + m.freq;
    for (i = 0; i < 2; i++) {
        osc_t *o = &v->o[i];
        const mq_osc_t *q = &P.osc[i];
        float semis = base + (float)q->semi + 0.01f * (float)q->tune + (i ? m.o2freq : m.o1freq);
        float hz = a_ref * fm_exp2f((semis - 69.0f) / 12.0f), pos;
        uint32_t m4;
        if (hz > 18000.0f) hz = 18000.0f;
        if (hz < 1.0f) hz = 1.0f;
        o->inc = (uint32_t)(hz * (4294967296.0f / (OS * FS)));
        /* the mask envelope: from START towards MASK, at SPD, looping or not */
        if (D.mspd[i] > 0.0f && q->start) {
            float sp = D.mspd[i], st = (float)q->start;
            if (o->mdir > 0.0f) {          /* towards MASK (0) */
                if (o->mdelta > 0.0f) o->mdelta = o->mdelta - sp < 0.0f ? 0.0f : o->mdelta - sp;
                else o->mdelta = o->mdelta + sp > 0.0f ? 0.0f : o->mdelta + sp;
                if (o->mdelta == 0.0f) {
                    if (q->loop == LOOP_YES) o->mdelta = st;
                    else if (q->loop == LOOP_BIDI) o->mdir = -1.0f;
                }
            } else {                       /* back towards START (BIDI) */
                if (st > 0.0f) o->mdelta = o->mdelta + sp > st ? st : o->mdelta + sp;
                else o->mdelta = o->mdelta - sp < st ? st : o->mdelta - sp;
                if (o->mdelta == st)
                    o->mdir = 1.0f;
            }
        } else {
            o->mdelta = 0.0f;
        }
        pos = (float)q->mask + o->mdelta + m.mask + (i ? m.o2mask : m.o1mask);
        if (pos < 0.0f) pos = 0.0f;
        if (pos > 511.75f) pos = 511.75f;
        m4 = (uint32_t)(pos * 4.0f);
        if (m4 != o->tab.m)
            bm_build(&o->tab, m4);
        o->g_to = v->quick ? 0.0f : D.lv[i] * lvl[i] * o->tab.norm * volmul(m.vol) * volmul(i ? m.o2vol : m.o1vol);
        if (v == &V[S.last_voice])
            meter.mask_now[i] = pos;
    }
    /* the noise: a held random value, taken again FREQ times a second */
    {
        float f = (float)P.noise.freq + m.nfreq + (float)P.noise.track / 99.0f * (v->trk - 60.0f) * 0.75f, hz;
        if (f >= 99.0f) {
            v->ninc = 0xFFFFFFFFu;
        } else {
            if (f < 0.0f) f = 0.0f;
            hz = FS * fm_exp2f((f - 99.0f) / 7.5f);   /* 99: every sample; 0: 7 Hz */
            v->ninc = (uint32_t)(hz * (4294967296.0f / FS));
        }
        v->ng_to = v->quick ? 0.0f : D.lv[E_N] * lvl[E_N] * volmul(m.vol) * volmul(m.nvol);
    }
    /* the filter */
    {
        float c = (float)P.filt.cutf + (float)P.filt.trk / 99.0f * (v->pitch + (float)P.osc[0].semi - 60.0f) + m.filt;
        float fe = D.lv[E_F] * lvl[E_F], hz;
        if (P.vel.dest == VD_FEG && P.vel.amt)
            fe *= 1.0f + (float)P.vel.amt / 99.0f * (v->vel - 1.0f);
        c += fe;
        hz = hz_of(c);
        if (hz < 16.0f) hz = 16.0f;
        if (hz > 0.45f * FS) hz = 0.45f * FS;
        v->fg_to = fm_tanf(FM_PI * hz / FS);
        v->fk = D.fk;
    }
}

/* ------------------------------------------------------------------------------------------------ render --- */
#define HB1 0.300794f                      /* the second halfband (15 taps, Kaiser 6): its odd taps */
#define HB3 (-0.06271f)
#define HB5 0.012712f
#define HB7 (-0.000676f)

static void voice_block(voice_t *v, float *outl, float *outr, uint32_t n, uint32_t step0)
{
    uint32_t done = 0, step = step0;
    float x4[6 + OS * CR], x2[14 + 2 * CR], y[CR];
    while (done < n) {
        uint32_t k = n - done < CR ? n - done : CR, j;
        float g1, g2, d1, d2, ng, dn, fg, dfg, inv = 1.0f / (float)(OS * k);
        float panl, panr, lv;
        voice_control(v, step++);
        g1 = v->o[0].g;
        g2 = v->o[1].g;
        d1 = (v->o[0].g_to - g1) * inv;
        d2 = (v->o[1].g_to - g2) * inv;
        /* the oscillators, four samples a sample */
        for (j = 0; j < 6u; j++)
            x4[j] = v->h4[j];
        {
            const bm_tab_t *t1 = &v->o[0].tab, *t2 = &v->o[1].tab;
            uint32_t p1 = v->o[0].ph, p2 = v->o[1].ph, i1 = v->o[0].inc, i2 = v->o[1].inc, nn = OS * k;
            float dc1 = t1->dc, dc2 = t2->dc;
            int on1 = g1 > 1e-6f || v->o[0].g_to > 1e-6f, on2 = g2 > 1e-6f || v->o[1].g_to > 1e-6f;
            /* one oscillator at a time (the second adds to the first): fewer values live in the loop */
            if (on1) {
                for (j = 0; j < nn; j++) {
                    p1 += i1;
                    x4[6 + j] = (bm_raw(t1, p1) - dc1) * g1;
                    g1 += d1;
                }
            } else {
                for (j = 0; j < nn; j++)
                    x4[6 + j] = 0.0f;
                p1 += i1 * nn;
            }
            if (on2) {
                for (j = 0; j < nn; j++) {
                    p2 += i2;
                    x4[6 + j] += (bm_raw(t2, p2) - dc2) * g2;
                    g2 += d2;
                }
            } else {
                p2 += i2 * nn;
            }
            v->o[0].ph = p1;
            v->o[1].ph = p2;
            v->o[0].g = v->o[0].g_to;
            v->o[1].g = v->o[1].g_to;
            for (j = 0; j < 6u; j++)
                v->h4[j] = x4[OS * k + j];
        }
        /* down to twice the rate: (-1 0 9 16 9 0 -1) / 32 */
        for (j = 0; j < 14u; j++)
            x2[j] = v->h2[j];
        for (j = 0; j < 2u * k; j++) {
            const float *a = x4 + 2 * j;
            x2[14 + j] = 0.5f * a[3] + 0.28125f * (a[2] + a[4]) - 0.03125f * (a[0] + a[6]);
        }
        for (j = 0; j < 14u; j++)
            v->h2[j] = x2[2 * k + j];
        /* and to the rate */
        for (j = 0; j < k; j++) {
            const float *d = x2 + 2 * j;
            y[j] = 0.5f * d[7] + HB1 * (d[6] + d[8]) + HB3 * (d[4] + d[10]) + HB5 * (d[2] + d[12]) +
                   HB7 * (d[0] + d[14]);
        }
        /* the noise */
        ng = v->ng;
        dn = (v->ng_to - ng) / (float)k;
        if (ng > 1e-6f || v->ng_to > 1e-6f) {
            uint32_t ph = v->nph, inc = v->ninc;
            for (j = 0; j < k; j++) {
                uint32_t nph = ph + inc;
                if (nph < ph || inc == 0xFFFFFFFFu)
                    v->nval = eng_frand(&v->rng);
                ph = nph;
                y[j] += v->nval * ng;
                ng += dn;
            }
            v->nph = ph;
        }
        v->ng = v->ng_to;
        /* the filter: Simper's state-variable (trapezoidal), its cutoff gliding over the step */
        fg = v->fg;
        dfg = (v->fg_to - fg) / (float)k;
        {
            float k_ = v->fk, s1 = v->s1, s2 = v->s2, s3 = v->s3, s4 = v->s4, s5 = v->s5, s6 = v->s6;
            int mode = P.filt.mode, x = P.filt.extra;
            for (j = 0; j < k; j++) {
                float g = fg, a1 = 1.0f / (1.0f + g * (g + k_)), a2 = g * a1, a3 = g * a2, v0 = y[j], v1, v2, v3, o;
                v3 = v0 - s2;
                v1 = a1 * s1 + a2 * v3;
                v2 = s2 + a2 * s1 + a3 * v3;
                s1 = 2.0f * v1 - s1;
                s2 = 2.0f * v2 - s2;
                o = mode == FM_LP ? v2 : mode == FM_HP ? v0 - k_ * v1 - v2 : mode == FM_BP ? v1 : v0 - k_ * v1;
                if (x == FX_24DB) {        /* the same again, in series */
                    v0 = o;
                    v3 = v0 - s4;
                    v1 = a1 * s3 + a2 * v3;
                    v2 = s4 + a2 * s3 + a3 * v3;
                    s3 = 2.0f * v1 - s3;
                    s4 = 2.0f * v2 - s4;
                    o = mode == FM_LP ? v2 : mode == FM_HP ? v0 - k_ * v1 - v2 : mode == FM_BP ? v1 : v0 - k_ * v1;
                } else if (x >= FX_LP1) {  /* a fixed second filter */
                    float h = D.x2g, b1 = 1.0f / (1.0f + h * (h + D.x2k)), b2 = h * b1, b3 = h * b2;
                    v0 = o;
                    v3 = v0 - s6;
                    v1 = b1 * s5 + b2 * v3;
                    v2 = s6 + b2 * s5 + b3 * v3;
                    s5 = 2.0f * v1 - s5;
                    s6 = 2.0f * v2 - s6;
                    o = D.x2hp ? v0 - D.x2k * v1 - v2 : v2;
                }
                y[j] = o;
                fg += dfg;
            }
            v->s1 = fm_flush(s1);
            v->s2 = fm_flush(s2);
            v->s3 = fm_flush(s3);
            v->s4 = fm_flush(s4);
            v->s5 = fm_flush(s5);
            v->s6 = fm_flush(s6);
            v->fg = v->fg_to;
        }
        /* out, panned (constant power, roughly) */
        {
            float p = v->pan;
            if (P.lfo[0].dest == LD_PAN) p += S.lval[0][step - 1];
            if (P.lfo[1].dest == LD_PAN) p += S.lval[1][step - 1];
            if (p < -1.0f) p = -1.0f;
            if (p > 1.0f) p = 1.0f;
            lv = 0.35f * D.vol;
            panl = lv * (1.0f - 0.3f * p - 0.2f * p * p);
            panr = lv * (1.0f + 0.3f * p - 0.2f * p * p);
        }
        v->out_pk = 0.0f;
        for (j = 0; j < k; j++) {
            float a = fm_fabsf(y[j]);
            if (a > v->out_pk) v->out_pk = a;
            outl[done + j] += y[j] * panl;
            outr[done + j] += y[j] * panr;
        }
        done += k;
    }
    /* finished: nothing sounds and nothing will */
    {
        int para2 = P.gen.mode == PM_PARA || P.gen.mode == PM_PARA_SLUR, alive = 0, e;
        const env_t *ev = para2 ? S.penv : v->env;
        for (e = 0; e < E_F; e++)
            if (D.lv[e] > 0.0f && ev[e].st != ST_IDLE)
                alive = 1;
        if (v->quick && v->o[0].g < 1e-5f && v->o[1].g < 1e-5f && v->ng < 1e-5f)
            alive = 0;
        if (v->out_pk > 2e-4f)
            alive = 1;                     /* the filter still rings, or the decimators still hold sound */
        if (!alive && !v->gate)
            v->dead = 1;
        if (!alive && v->gate && D.lv[E_O1] + D.lv[E_O2] + D.lv[E_N] <= 0.0f)
            v->dead = 0;                   /* a silent patch: kept while held */
    }
}

/* --------------------------------------------------------------------------------------------- the notes --- */
static uint32_t age_now;

static int para_mode(void) { return P.gen.mode == PM_PARA || P.gen.mode == PM_PARA_SLUR; }
static int mono_mode(void)
{
    return P.gen.mode == PM_MONO || P.gen.mode == PM_MONO_PORTA || P.gen.mode == PM_MONO_RETRIG;
}
static int slur_mode(void) { return P.gen.mode == PM_SLUR || P.gen.mode == PM_PARA_SLUR; }

static void voice_free(voice_t *v) { v->used = 0; v->gate = 0; v->dead = 0; v->quick = 0; v->nstack = 0; }

static int voice_find_key(int key)          /* the voice sounding key, held or not: -1 none */
{
    int i, best = -1;
    for (i = 0; i < MQ_MAX_VOICES; i++)
        if (V[i].used && V[i].key == key && (best < 0 || V[i].age > V[best].age))
            best = i;
    return best;
}

static int voice_alloc(void)
{
    int i, best = -1, n = 0;
    uint32_t a = 0xFFFFFFFFu;
    for (i = 0; i < MQ_MAX_VOICES; i++)
        n += V[i].used;
    for (i = 0; i < MQ_MAX_VOICES && n < S.allowed; i++)
        if (!V[i].used)
            return i;
    for (i = 0; i < MQ_MAX_VOICES; i++)    /* the oldest let go */
        if (V[i].used && !V[i].gate && V[i].age < a) {
            a = V[i].age;
            best = i;
        }
    if (best >= 0)
        return best;
    for (i = 0; i < MQ_MAX_VOICES; i++)    /* the oldest held */
        if (V[i].used && V[i].age < a) {
            a = V[i].age;
            best = i;
        }
    return best < 0 ? 0 : best;
}

/* the paraphonic envelopes: the first key starts them, the last lets them go */
static void para_rates(void)
{
    int e;
    for (e = 0; e < NENV; e++) {
        S.patt[e] = rate_step(D.t_a[e]);
        S.pdec[e] = exp_coef(D.t_d[e]);
        S.prel[e] = exp_coef(D.t_r[e]);
    }
}

static void voice_start(voice_t *v, int key, float vel, int retrig, int glide_from_last)
{
    int e, i, was = v->used;
    float from = S.last_pitch;
    v->used = 1;
    v->gate = 1;
    v->pedal = 0;
    v->dead = 0;
    v->quick = 0;
    v->nstack = 0;
    v->key = (int8_t)key;
    v->trk = (float)key;
    v->vel = vel;
    v->at = 0.0f;
    v->age = ++age_now;
    v->pitch_to = (float)key;
    if (glide_from_last && D.port_c > 0.0f && from > 0.0f) {
        v->pitch = was ? v->pitch : from;
        v->glide = D.port_c;
    } else {
        v->pitch = (float)key;
        v->glide = 0.0f;
    }
    v->penv = (float)P.pitch.start;
    if (P.vel.dest == VD_PEG && P.vel.amt)
        v->penv *= 1.0f + (float)P.vel.amt / 99.0f * (vel - 1.0f);
    for (i = 0; i < 2; i++) {
        osc_t *o = &v->o[i];
        o->mdelta = (float)P.osc[i].start;
        o->mdir = 1.0f;
        if (!was || retrig) {
            o->ph = retrig || !was ? (i ? 0x40000000u : 0u) : o->ph;
            o->tab.m = 0xFFFFFFFFu;
        }
        if (!was) {
            o->g = 0.0f;
        }
    }
    v->out_pk = 1.0f;
    if (!was) {
        v->ng = 0.0f;
        v->s1 = v->s2 = v->s3 = v->s4 = v->s5 = v->s6 = 0.0f;
        for (i = 0; i < 6; i++) v->h4[i] = 0.0f;
        for (i = 0; i < 14; i++) v->h2[i] = 0.0f;
        v->rng = 0x9E3779B9u ^ (uint32_t)key * 2654435761u ^ age_now;
        v->fg = v->fg_to = 0.1f;
        for (e = 0; e < NENV; e++)
            v->env[e].lvl = 0.0f;
    }
    {   /* the pan: the spread, alternating and a little random */
        static int side;
        side = !side;
        v->pan = (float)P.gen.pans / 99.0f * (side ? 1.0f : -1.0f) * (0.6f + 0.4f * fm_fabsf(eng_frand(&v->rng)));
    }
    voice_rates(v);
    for (e = 0; e < NENV; e++)
        env_gate(&v->env[e], retrig);
    S.last_pitch = (float)key;
    S.last_voice = (int)(v - V);
}

/* a sounding voice moved to a new key: no envelope starts again */
static void voice_move(voice_t *v, int key, int glide)
{
    v->key = (int8_t)key;
    v->trk = (float)key;
    v->pitch_to = (float)key;
    v->glide = glide ? (D.port_c > 0.0f ? D.port_c : 0.35f) : 0.0f;
    v->age = ++age_now;
    S.last_pitch = (float)key;
    S.last_voice = (int)(v - V);
}

static struct { int8_t k[16]; int n; } mono;   /* the keys down, in order, for the mono modes */
static void mono_push(int key)
{
    int i, j;
    for (i = j = 0; i < mono.n; i++)
        if (mono.k[i] != key)
            mono.k[j++] = mono.k[i];
    mono.n = j;
    if (mono.n == 16) {
        for (i = 1; i < 16; i++) mono.k[i - 1] = mono.k[i];
        mono.n = 15;
    }
    mono.k[mono.n++] = (int8_t)key;
}
static void mono_pull(int key)
{
    int i, j;
    for (i = j = 0; i < mono.n; i++)
        if (mono.k[i] != key)
            mono.k[j++] = mono.k[i];
    mono.n = j;
}

static int any_gate(void)
{
    int i;
    for (i = 0; i < MQ_MAX_VOICES; i++)
        if (V[i].used && V[i].gate && !V[i].quick)
            return 1;
    return 0;
}

void eng_note_on(int key, float vel, int legato)
{
    int mode = P.gen.mode, i, held_before = any_gate();
    voice_t *v;
    if (key < 0 || key > 127)
        return;
    S.held++;
    if (mono_mode()) {
        int legato_now = mono.n > 0 || legato;
        voice_t *mv = &V[0];
        int sounding = mv->used;
        mono_push(key);
        for (i = 1; i < MQ_MAX_VOICES; i++)   /* the mono modes play on voice 0 only */
            if (V[i].used)
                V[i].quick = 1, V[i].gate = 0;
        if (sounding && legato_now) {
            voice_move(mv, key, mode == PM_MONO || D.port_c > 0.0f);
            if (mode != PM_MONO_PORTA && mode != PM_MONO && D.port_c <= 0.0f)
                mv->glide = 0.0f;
            if (mode == PM_MONO_RETRIG) {
                int e;
                for (e = 0; e < NENV; e++)
                    env_gate(&mv->env[e], 1);
                mv->o[0].mdelta = (float)P.osc[0].start;
                mv->o[1].mdelta = (float)P.osc[1].start;
                mv->penv = (float)P.pitch.start;
            }
            mv->gate = 1;
            mv->vel = vel;
        } else {
            voice_start(mv, key, vel, mode == PM_MONO_RETRIG || !sounding, mode == PM_MONO);
        }
        return;
    }
    /* slurred: a key next to one held (two semitones or less) takes that voice over */
    if (slur_mode() || legato) {
        int best = -1, bd = 99;
        for (i = 0; i < MQ_MAX_VOICES; i++) {
            int d = V[i].key - key;
            if (d < 0) d = -d;
            if (V[i].used && V[i].gate && !V[i].quick && (legato || (d >= 1 && d <= 2)) && d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best >= 0) {
            v = &V[best];
            if (v->nstack < 6)
                v->stack[v->nstack++] = v->key;
            voice_move(v, key, 1);
            if (!legato && D.port_c <= 0.0f)
                v->glide = 0.4f;           /* a slur always slides a little */
            return;
        }
    }
    i = voice_find_key(key);
    if (i < 0 || V[i].gate)
        i = voice_alloc();
    v = &V[i];
    if (para_mode()) {
        if (!held_before) {
            int e;
            para_rates();
            for (e = 0; e < NENV; e++)
                env_gate(&S.penv[e], 0);
        }
        voice_start(v, key, vel, 1, held_before);
        return;
    }
    if (mode == PM_MOLY)                   /* legato played: it glides from the last; staccato: it does not */
        voice_start(v, key, vel, 0, held_before);
    else if (mode == PM_POLY_RETRIG)
        voice_start(v, key, vel, 1, 1);
    else                                   /* POLY, SLUR: the envelopes go on from where they are */
        voice_start(v, key, vel, 0, mode == PM_POLY);
}

static void voice_let_go(voice_t *v)
{
    int e;
    if (S.sustain) {
        v->pedal = 1;
        return;
    }
    v->gate = 0;
    if (para_mode()) {
        if (any_gate()) {
            v->quick = 1;
        } else {
            for (e = 0; e < NENV; e++)
                env_release(&S.penv[e]);
        }
        return;
    }
    for (e = 0; e < NENV; e++)
        env_release(&v->env[e]);
}

void eng_note_off(int key)
{
    int i;
    if (S.held > 0)
        S.held--;
    if (mono_mode()) {
        voice_t *mv = &V[0];
        int was_top = mono.n && mono.k[mono.n - 1] == key;
        mono_pull(key);
        if (!mv->used)
            return;
        if (mono.n && was_top) {           /* back to the key still down, legato */
            voice_move(mv, mono.k[mono.n - 1], P.gen.mode == PM_MONO || D.port_c > 0.0f);
        } else if (!mono.n) {
            voice_let_go(mv);
        }
        return;
    }
    for (i = 0; i < MQ_MAX_VOICES; i++) {
        voice_t *v = &V[i];
        int j, k;
        if (!v->used || !v->gate)
            continue;
        for (j = k = 0; j < v->nstack; j++)   /* a slurred key under it let go: forgotten */
            if (v->stack[j] != key)
                v->stack[k++] = v->stack[j];
        v->nstack = (uint8_t)k;
        if (v->key != key)
            continue;
        if (v->nstack) {                   /* its top key let go: back to the one below */
            voice_move(v, v->stack[--v->nstack], 1);
            if (D.port_c <= 0.0f)
                v->glide = 0.4f;
        } else {
            voice_let_go(v);
        }
    }
}

void eng_all_off(void)
{
    int i;
    S.sustain = 0;
    mono.n = 0;
    S.held = 0;
    for (i = 0; i < MQ_MAX_VOICES; i++)
        if (V[i].used && V[i].gate)
            voice_let_go(&V[i]);
}

void eng_ctl(int type, int a)
{
    int i;
    switch (type) {
    case SQ_BEND: S.bend_to = (float)(a - 8192) / 8192.0f; break;
    case SQ_WHEEL: S.wheel = (float)a / 127.0f; break;
    case SQ_CHAT: S.chat = (float)a / 127.0f; break;
    case SQ_SUSTAIN:
        S.sustain = a >= 64;
        if (!S.sustain)
            for (i = 0; i < MQ_MAX_VOICES; i++)
                if (V[i].used && V[i].pedal) {
                    V[i].pedal = 0;
                    voice_let_go(&V[i]);
                }
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------------------------------- the queue --- */
enum { EV_ON, EV_OFF, EV_PRESS, EV_CC, EV_BEND, EV_LREC, EV_LPLAY, EV_LCLEAR };
typedef struct { uint8_t t, a, b, c; } ev_t;
#define QN 64u
static ev_t q[QN];
static volatile uint32_t q_w, q_r;
static void put(int t, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                            /* full: dropped */
    q[w % QN].t = (uint8_t)t;
    q[w % QN].a = (uint8_t)a;
    q[w % QN].b = (uint8_t)b;
    __asm__ volatile("" ::: "memory");
    q_w = w + 1;
}
static float vel_curve(int v)
{
    static const float E[6] = {0.45f, 0.65f, 1.0f, 1.4f, 2.0f, 2.8f};
    float x = (float)v / 127.0f;
    if (G.vel_curve >= 6)
        return 1.0f;
    return fm_powf(x < 0.008f ? 0.008f : x, E[G.vel_curve % 6]);
}
static float at_curve(int v)
{
    static const float E[6] = {0.45f, 0.65f, 1.0f, 1.4f, 2.0f, 2.8f};
    float x = (float)v / 127.0f;
    return x <= 0.0f ? 0.0f : fm_powf(x, E[G.at_curve % 6]);
}

void masque_note_on(int key, int vel) { put(EV_ON, key, vel); }
void masque_note_off(int key) { put(EV_OFF, key, 0); }
void masque_pressure(int key, int v) { put(EV_PRESS, key, v); }
void masque_cc(int cc, int v) { put(EV_CC, cc, v); }
void masque_bend(int v14) { put(EV_BEND, v14 & 127, v14 >> 7); }
void masque_loop_rec(void) { put(EV_LREC, 0, 0); }
void masque_loop_play(void) { put(EV_LPLAY, 0, 0); }
void masque_loop_clear(void) { put(EV_LCLEAR, 0, 0); }

void masque_note_on_isr(int key, int vel)
{
    if (vel <= 0)
        seq_note_off(key);
    else
        seq_note_on(key, vel);
}
void masque_note_off_isr(int key) { seq_note_off(key); }
void masque_pressure_isr(int key, int v)
{
    int i;
    for (i = 0; i < MQ_MAX_VOICES; i++)
        if (V[i].used && V[i].gate && V[i].key == key)
            V[i].at = at_curve(v);
}
void masque_chan_pressure_isr(int v) { seq_ctl(SQ_CHAT, v); }
void masque_cc_isr(int cc, int v)
{
    if (cc == 1)
        seq_ctl(SQ_WHEEL, v);
    else if (cc == 64)
        seq_ctl(SQ_SUSTAIN, v);
    else if (cc == 120 || cc == 123) {
        eng_all_off();
        seq_panic();
    }
}
void masque_bend_isr(int v14) { seq_ctl(SQ_BEND, v14); }
void masque_clock_isr(int what) { seq_clock(what); }

/* the sequencer's notes come here with their raw velocity; this is where the curve applies */
float eng_vel(int v) { return vel_curve(v); }

static void events(void)
{
    while (q_r != q_w) {
        ev_t e = q[q_r % QN];
        __asm__ volatile("" ::: "memory");
        q_r++;
        switch (e.t) {
        case EV_ON: masque_note_on_isr(e.a, e.b); break;
        case EV_OFF: masque_note_off_isr(e.a); break;
        case EV_PRESS: masque_pressure_isr(e.a, e.b); break;
        case EV_CC: masque_cc_isr(e.a, e.b); break;
        case EV_BEND: masque_bend_isr(e.a | e.b << 7); break;
        case EV_LREC: seq_loop_rec(); break;
        case EV_LPLAY: seq_loop_play(); break;
        case EV_LCLEAR: seq_loop_clear(); break;
        default: break;
        }
    }
}

/* ---------------------------------------------------------------------------------- the patch, double-buffered --- */
static mq_patch_t pbuf[2];
static mq_global_t gbuf;
static volatile int p_cur, p_new, g_new;
void masque_set_patch(const mq_patch_t *p)
{
    int w = !p_cur;
    pbuf[w] = *p;
    __asm__ volatile("" ::: "memory");
    p_cur = w;
    p_new = 1;
}
void masque_set_global(const mq_global_t *g)
{
    gbuf = *g;
    __asm__ volatile("" ::: "memory");
    g_new = 1;
}

/* ------------------------------------------------------------------------------------------- the render --- */
static float c2_l[MQ_BLOCK_MAX], c2_r[MQ_BLOCK_MAX], mix_l[MQ_BLOCK_MAX], mix_r[MQ_BLOCK_MAX];
static uint32_t job_n;
static volatile int shed_req;

static void cpu2_job(uint32_t arg)
{
    uint32_t i;
    (void)arg;
    for (i = 0; i < job_n; i++)
        c2_l[i] = c2_r[i] = 0.0f;
    for (i = 0; i < MQ_MAX_VOICES; i++)
        if (V[i].used && V[i].core2)
            voice_block(&V[i], c2_l, c2_r, job_n, 0);
}

static int finite_bounded(const float *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (!(x[i] > -64.0f && x[i] < 64.0f))
            return 0;
    return 1;
}

static void fx_mods(fx_ctl_t *c)           /* the effects' type and balance, after the LFOs and the controllers */
{
    float t1 = 0.0f, t2 = 0.0f, b1 = (float)P.fx.b1 / 100.0f, b2 = (float)P.fx.b2 / 100.0f;
    int i;
    for (i = 0; i < 2; i++) {
        float x = S.lval[i][0];
        if (P.lfo[i].dest == LD_FX1TYP) t1 += x * 8.0f;
        if (P.lfo[i].dest == LD_FX2TYP) t2 += x * 8.0f;
        if (P.lfo[i].dest == LD_FX2BAL) b2 += x * 0.5f;
    }
    if (P.wheel.dest == MD_FX1BAL) b1 += (float)P.wheel.amt / 99.0f * S.wheel;
    if (P.wheel.dest == MD_FX2BAL) b2 += (float)P.wheel.amt / 99.0f * S.wheel;
    if (P.at.dest == MD_FX1BAL) b1 += (float)P.at.amt / 99.0f * S.chat;
    if (P.at.dest == MD_FX2BAL) b2 += (float)P.at.amt / 99.0f * S.chat;
    c->b1 = fm_clampf(b1, 0.0f, 1.0f);
    c->b2 = fm_clampf(b2, 0.0f, 1.0f);
    c->t1 = P.fx.t1;
    c->t2 = P.fx.t2;
    if (P.fx.t1 && t1 != 0.0f) {            /* moved within its family, never out of it */
        int fam = (P.fx.t1 - 1) / 16, var = (P.fx.t1 - 1) % 16 + (int)(t1 + (t1 > 0 ? 0.5f : -0.5f));
        c->t1 = 1 + fam * 16 + (var < 0 ? 0 : var > 15 ? 15 : var);
    }
    if (P.fx.t2 && t2 != 0.0f) {
        int fam = (P.fx.t2 - 1) / 16, var = (P.fx.t2 - 1) % 16 + (int)(t2 + (t2 > 0 ? 0.5f : -0.5f));
        c->t2 = 1 + fam * 16 + (var < 0 ? 0 : var > 15 ? 15 : var);
    }
}

void masque_render(float *l, float *r, uint32_t n)
{
    uint32_t i, s, t0 = plat_ticks24(), nv = 0;
    int use2, handed, k;
    fx_ctl_t fc;
    if (n > MQ_BLOCK_MAX)
        n = MQ_BLOCK_MAX;
    if (g_new) {
        g_new = 0;
        G = gbuf;
        if (G.voices < 4 || G.voices > MQ_MAX_VOICES)
            G.voices = MQ_DEFAULT_VOICES;
        if (S.allowed > G.voices || !S.shed_hold)
            S.allowed = G.voices;
    }
    if (p_new) {
        int oldmode = P.gen.mode;
        p_new = 0;
        P = pbuf[p_cur];
        derive();
        seq_set(&P);
        if (para_mode() != (oldmode == PM_PARA || oldmode == PM_PARA_SLUR) || mono_mode() != (oldmode == PM_MONO ||
            oldmode == PM_MONO_PORTA || oldmode == PM_MONO_RETRIG))
            eng_all_off();
        if (para_mode())
            para_rates();
    }
    events();
    seq_block(n);
    if (shed_req) {
        shed_req = 0;
        if (S.allowed > 4)
            S.allowed--;
        S.shed_hold = 1;
        S.ok_blocks = 0;
        {   /* too many voices sounding now: the oldest let go goes at once */
            int used = 0;
            for (i = 0; i < MQ_MAX_VOICES; i++)
                used += V[i].used;
            if (used > S.allowed) {
                uint32_t a = 0xFFFFFFFFu;
                int best = -1;
                for (i = 0; i < MQ_MAX_VOICES; i++)
                    if (V[i].used && V[i].age < a && !V[i].gate) {
                        a = V[i].age;
                        best = (int)i;
                    }
                if (best >= 0)
                    voice_free(&V[best]);
            }
        }
    }
    /* the controllers glide; the LFOs for the block */
    S.bend += (S.bend_to - S.bend) * 0.5f;
    if (S.held == 0 && !any_gate()) {
        /* nothing down: the LFOs' delay starts again at the next key */
    }
    lfo_block((n + CR - 1) / CR);
    if (para_mode()) {                     /* the shared envelopes run once for every voice, a block ahead */
        uint32_t st, e;
        for (st = 0; st < (n + CR - 1) / CR; st++)
            for (e = 0; e < NENV; e++) {
                env_step(&S.penv[e], S.patt[e], S.pdec[e], D.sus[e], S.prel[e], penv_of((int)e)->loop);
                S.plvl[e][st] = S.penv[e].lvl;
            }
    }
    for (i = 0; i < n; i++)
        mix_l[i] = mix_r[i] = 0.0f;
    use2 = plat_cpu2_on();
    k = 0;
    for (i = 0; i < MQ_MAX_VOICES; i++) {
        V[i].core2 = 0;
        if (V[i].used) {
            nv++;
            if (use2 && (k++ & 1))
                V[i].core2 = 1;
        }
    }
    job_n = n;
    handed = use2 && nv > 1 && plat_cpu2_run(cpu2_job, 0);
    for (i = 0; i < MQ_MAX_VOICES; i++)
        if (V[i].used && (!handed || !V[i].core2))
            voice_block(&V[i], mix_l, mix_r, n, 0);
    if (handed && plat_cpu2_wait() != 0) {
        handed = 0;                        /* given up: its voices here, now */
        for (i = 0; i < MQ_MAX_VOICES; i++)
            if (V[i].used && V[i].core2)
                voice_block(&V[i], mix_l, mix_r, n, 0);
    }
    if (handed) {
        if (finite_bounded(c2_l, n) && finite_bounded(c2_r, n)) {
            for (i = 0; i < n; i++) {
                mix_l[i] += c2_l[i];
                mix_r[i] += c2_r[i];
            }
        } else {
            for (i = 0; i < MQ_MAX_VOICES; i++)
                if (V[i].used && V[i].core2)
                    voice_free(&V[i]);
        }
    }
    if (!finite_bounded(mix_l, n) || !finite_bounded(mix_r, n)) {   /* never into the speakers */
        for (i = 0; i < MQ_MAX_VOICES; i++)
            voice_free(&V[i]);
        for (i = 0; i < n; i++)
            mix_l[i] = mix_r[i] = 0.0f;
    }
    for (i = 0; i < MQ_MAX_VOICES; i++)
        if (V[i].used && V[i].dead)
            voice_free(&V[i]);
    fx_mods(&fc);
    fx_process(mix_l, mix_r, n, &fc);
    for (s = 0; s < n; s++) {
        l[s] = mix_l[s];
        r[s] = mix_r[s];
    }
    /* the meter */
    {
        uint32_t snd[4] = {0, 0, 0, 0}, dt = plat_ticks24() - t0;
        float load = (float)dt / ((float)n * (24000000.0f / FS));
        meter.load += (load - meter.load) * 0.05f;
        if (load < 0.6f && S.shed_hold && ++S.ok_blocks > 1700u) {   /* ten quiet seconds: a voice back */
            S.ok_blocks = 0;
            if (S.allowed < G.voices) S.allowed++;
            if (S.allowed >= G.voices) S.shed_hold = 0;
        }
        nv = 0;
        for (i = 0; i < MQ_MAX_VOICES; i++)
            if (V[i].used) {
                nv++;
                if (V[i].gate || V[i].env[E_O1].st != ST_IDLE) {
                    int key = V[i].key & 127;
                    snd[key >> 5] |= 1u << (key & 31);
                }
            }
        meter.voices = nv;
        if (nv > meter.voices_peak) meter.voices_peak = nv;
        meter.allowed = (uint32_t)S.allowed;
        for (i = 0; i < 4; i++)
            meter.sounding[i] = snd[i];
        meter.cpu2 = (uint8_t)use2;
        seq_meter(&meter);
    }
    if (S.held <= 0 && !any_gate())
        S.lenv_t = 0.0f;                   /* the LFOs' delay starts again with the next key */
}

void masque_panic(void)
{
    int i;
    for (i = 0; i < MQ_MAX_VOICES; i++)
        voice_free(&V[i]);
    mono.n = 0;
    S.held = 0;
    S.sustain = 0;
    seq_panic();
    fx_clear();
}

void masque_shed(void) { shed_req = 1; }
const mq_meter_t *masque_meter(void) { return &meter; }

void masque_init(void)
{
    int i;
    bm_sin_init();
    memset(V, 0, sizeof V);
    memset(&S, 0, sizeof S);
    S.lrng = 0x12345678u;
    S.allowed = MQ_DEFAULT_VOICES;
    for (i = 0; i < MQ_MAX_VOICES; i++) {
        V[i].o[0].tab.m = V[i].o[1].tab.m = 0xFFFFFFFFu;
        V[i].rng = 0xA5A5u + (uint32_t)i * 7919u;
    }
    S.bend = S.bend_to = 0.0f;
    seq_init();
    fx_init();
    P.gen.vol = 80;
    derive();
}

/* one cycle of a mask, for the screen */
void masque_mask_wave(float m, float *out, int n)
{
    static bm_tab_t t;
    uint32_t m4, i;
    if (m < 0.0f) m = 0.0f;
    if (m > 511.75f) m = 511.75f;
    m4 = (uint32_t)(m * 4.0f);
    bm_build(&t, m4);
    for (i = 0; i < (uint32_t)n; i++) {
        float x = (bm_raw(&t, (uint32_t)((float)i / (float)n * 4294967296.0f)) - t.dc) * t.norm;
        out[i] = x;
    }
}

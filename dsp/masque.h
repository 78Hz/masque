/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — a bitmask synthesizer for the M-VAVE FM-1, after Kodamo's MASK1.
 * The engine's interface: the patch (a sound), the part's global settings, notes and controllers in, audio out.
 *
 * Threads: masque_render() runs in the audio interrupt. The main loop reaches it through a small lock-free queue
 * (notes, controllers, looper and arp commands) and a double-buffered patch, which the render takes at its next
 * block. MIDI read inside the render calls the *_isr functions directly. */
#pragma once
#include <stdint.h>

#define MQ_FS 44117.647f                   /* the FM-1's real rate: 24 MHz / 544 */
#define MQ_MAX_VOICES 12
#define MQ_DEFAULT_VOICES 10               /* the MASK1's polyphony */
#define MQ_BLOCK_MAX 256u

/* ---------------------------------------------------------------------------------------------- enums --- */
enum { LOOP_NO, LOOP_YES, LOOP_BIDI, NLOOPS };                         /* the mask envelope's loop */
/* the filter's mode: the state-variable's four outputs, its peak (the band added), and a comb (a delay line tuned to the
 * cutoff, fed back with RESO: positive, or negative for the hollow, octave-down kind) */
enum { FM_LP, FM_HP, FM_BP, FM_NOTCH, FM_PEAK, FM_COMBP, FM_COMBN, NFMODES };
/* the second filter: off (12 dB), the first doubled (24 dB), or a fixed low or high pass after it */
enum { FX_12DB, FX_24DB, FX_LP1, FX_LP7 = FX_LP1 + 6, FX_HP1, FX_HP7 = FX_HP1 + 6, NEXTRAS };
enum { PM_POLY, PM_MONO, PM_SLUR, PM_MONO_PORTA, PM_PARA, PM_MOLY, PM_PARA_SLUR, PM_MONO_RETRIG, PM_POLY_RETRIG, NPMODES };
enum { LW_SIN, LW_SQR, LW_UP, LW_DOWN, LW_SAWS, LW_STEP, LW_HSIN, LW_RAND, NLWAVES };
/* the LFOs' destinations (16) */
enum { LD_FREQ, LD_VOL, LD_FILT, LD_MASK, LD_PAN, LD_O1FREQ, LD_O1VOL, LD_O1MASK, LD_O2FREQ, LD_O2VOL, LD_O2MASK,
       LD_NFREQ, LD_NVOL, LD_FX1MOD, LD_FX2MOD, LD_FX2BAL, NLDESTS };
/* the modulation matrix's sources: the note's own (velocity, release velocity, pressure, timbre (MPE's CC74), its
 * bend, its key, a random value per note, the filter envelope), the part's (channel aftertouch, the mod wheel, the
 * LFOs) and any CC (XS_CC0 + n) */
enum { XS_NONE, XS_VEL, XS_REL, XS_PRESS, XS_AT, XS_TIMBRE, XS_BEND, XS_WHEEL, XS_KEY, XS_LFO1, XS_LFO2, XS_FENV,
       XS_RAND, XS_CC0, NXSRC = XS_CC0 + 128 };
/* its destinations: the voice's, then the part's (from XD_L1AMT: they follow the last note) */
enum { XD_NONE, XD_PITCH, XD_O1PITCH, XD_O2PITCH, XD_MASK, XD_O1MASK, XD_O2MASK, XD_MSPD, XD_VOL, XD_O1VOL, XD_O2VOL,
       XD_NVOL, XD_NFREQ, XD_CUTF, XD_RESO, XD_DRIVE, XD_FENV, XD_PENV, XD_ATK, XD_REL, XD_PAN, XD_L1AMT, XD_L1SPD,
       XD_L2AMT, XD_L2SPD, XD_FX1BAL, XD_FX2BAL, XD_FX1MOD, XD_FX2MOD, NXDEST };
enum { XC_LIN, XC_SOFT, XC_HARD, XC_S, XC_BIP, NXCURVE };   /* BIP: the source rests at its middle (a CC at 64) */
enum { XZ_ALL, XZ_LO, XZ_HI, NXZONE };     /* MPE's zones: the row hears notes of the lower, the upper, or both */
#define MQ_MTX_ROWS 8
/* the tunings: an equal division of the octave (its size apart), or a twelve-note temperament (KIST's) */
enum { TS_EQUAL, TS_WERCKMEISTER3, TS_MEANTONE, TS_KIRNBERGER3, TS_VALLOTTI, NTS };
#define MQ_MIDI_MPE 17                     /* the part's MIDI: 0 omni, 1..16 one channel, 17 MPE */
/* the effects: either slot takes any of them, but one of each, and one of those that need the long delay line (the
 * delay, the mask slicer) at a time (masque_fx_allowed) */
enum { E_OFF, E_CHORUS, E_FLANGER, E_PHASER, E_DIST, E_FOLD, E_CRUSH, E_RING, E_TREM, E_SHAPER, E_DELAY, E_PLATE,
       E_SLICER, NEFX };
#define MQ_FX_P 8                          /* an effect's settings, as bytes (a mask takes two) */
#define MQ_FX_K 6                          /* its settings on the knobs (two rows: two beside TYPE and MIX, then four) */
#define MQ_NSYNC 14                        /* the note values a synced time takes: OFF, 1/64 .. 1/2 (dsp/fx.c) */
/* the arpeggiator's patterns */
enum { AR_NONE, AR_1OCT, AR_DBL, AR_2OCT, AR_ALT, AR_3OCT, AR_PONG, AR_PON2, AR_PON3, AR_RND, AR_RND2, AR_RND3,
       AR_MIN, AR_MAJ, AR_SUS2, AR_SUS4, AR_7TH, AR_MIN2, AR_MAJ2, AR_SU22, AR_SU42, AR_7TH2, AR_1O, AR_2O, AR_3O,
       AR_5TH, AR_5TH2, NARPS };

/* ---------------------------------------------------------------------------------------------- patch --- */
typedef struct {                           /* an ADSR: L A D S R and LOOP (loops attack and decay until release) */
    int8_t l;                              /* level 0..99 (the filter's: -99..99, its amount) */
    uint8_t a, d, s, r;                    /* 0..99 */
    uint8_t loop;                          /* 0 / 1 */
} mq_env_t;

typedef struct {
    uint16_t mask;                         /* 0..511: the one number */
    int8_t semi;                           /* -60..60 */
    int8_t tune;                           /* -99..99 cents */
    int8_t start;                          /* the mask envelope: where it starts, masks from MASK, -99..99 */
    uint8_t spd;                           /* its speed, 0 (off) .. 99 */
    uint8_t loop;                          /* LOOP_* */
    uint8_t rsv;
    mq_env_t env;
} mq_osc_t;

typedef struct {
    char name[10];                         /* upper case, spaces padded, no terminator needed */
    mq_osc_t osc[2];
    struct { uint8_t freq, track; mq_env_t env; } noise;   /* FREQ 0..99 (99 white), TRACK 0..99 % */
    struct { uint8_t cutf, reso, mode, trk, extra, drive; mq_env_t env; } filt;   /* CUTF 0..127, RESO, TRK, DRIVE 0..99 */
    struct { int8_t start; uint8_t spd; int8_t end; uint8_t rsv; } pitch;   /* semitones -48..48, SPD 0..40 */
    struct { uint8_t amt, spd, dest, wave, dlay, dcay; } lfo[2];
    struct {
        uint8_t src, dest;                 /* XS_*, XD_* */
        int8_t amt;                        /* -99..99 */
        uint8_t curve;                     /* XC_* */
        uint8_t lo, hi;                    /* the part of the source's travel used: 0..99, 1..100 % */
        uint8_t smooth;                    /* 0..99 (0: none .. 99: about half a second) */
        uint8_t zone;                      /* XZ_* */
    } mtx[MQ_MTX_ROWS];
    struct { uint8_t mode, vol; int8_t semi; uint8_t port, pans, rate, bend, rsv; } gen;
    struct {
        uint8_t t1, b1, t2, b2;            /* the two slots' effects (E_*), their mix 0..100 */
        uint8_t p1[MQ_FX_P], p2[MQ_FX_P];  /* their settings (masque_fx_knob); choosing an effect sets them */
    } fx;
    struct { uint8_t type, spd2, hold, trig; } arp;   /* SPD in 2 BPM steps: 10..200 = 20..400 BPM */
} mq_patch_t;

/* the part's settings, not the sound's (kept with the setup) */
typedef struct {
    uint16_t a_hz100;                      /* A, hundredths of a hertz: 30000..50099 */
    uint8_t system;                        /* TS_* */
    uint8_t edo;                           /* 5..72: steps to the octave, with TS_EQUAL */
    uint8_t root;                          /* the temperament's root, 0 = C */
    uint8_t vel_curve;                     /* 0..5, 6 = MAX (velocity off) */
    uint8_t at_curve;                      /* 0..5 */
    uint8_t voices;                        /* 4..12 */
    uint8_t midi;                          /* 0 omni, 1..16, MQ_MIDI_MPE */
    uint8_t rsv[3];
} mq_global_t;

/* what the screens show (written by the render, read by the main loop) */
typedef struct {
    uint32_t voices, voices_peak, allowed;
    float mask_now[2];                     /* where the last note's masks are now (envelopes, LFOs), for the scope */
    float load;                            /* the render's share of its time, 0..1 */
    float bpm;                             /* the tempo a synced effect follows (MIDI clock's, else the ARP's) */
    uint32_t sounding[4];                  /* the MIDI notes sounding: bit k % 32 of word k / 32 */
    uint8_t loop_state, arp_on, cpu2;      /* LS_* */
    /* the last note's expression, as the matrix sees it (0..1; bend -1..1), and which of them have come */
    float x_vel, x_press, x_at, x_timbre, x_bend;
    uint8_t x_has;                         /* bit 0 pressure, 1 aftertouch, 2 timbre, 3 bend */
    uint8_t mpe_lower, mpe_upper;          /* MPE's zones: their member channels (0: none) */
    int8_t x_ch;                           /* the last note's channel (0..15; -1 the FM-1's keys) */
    uint32_t loop_events, loop_len_ms, loop_pos_ms;
} mq_meter_t;

enum { LS_IDLE, LS_ARMED, LS_RECORDING, LS_PLAYING, LS_STOPPED };

/* ---------------------------------------------------------------------------------------------- calls --- */
void masque_init(void);
void masque_set_patch(const mq_patch_t *p);        /* main loop: taken at the next block */
void masque_set_global(const mq_global_t *g);
void masque_render(float *l, float *r, uint32_t n);    /* audio interrupt */
void masque_panic(void);
void masque_shed(void);                            /* the render ran long: one voice fewer for a while */
const mq_meter_t *masque_meter(void);

/* the main loop's notes and controllers (queued) */
void masque_note_on(int key, int vel);             /* vel 1..127 (raw: the part's curve applies) */
void masque_note_off(int key);
void masque_pressure(int key, int v);              /* a key's pressure 0..127 (poly aftertouch) */
void masque_cc(int cc, int v);
void masque_loop_rec(void);                        /* REC: arm / stop recording (and play) */
void masque_loop_play(void);                       /* PLAY: play / stop */
void masque_loop_clear(void);
/* the render's own (MIDI in): the same, straight in */
/* a MIDI channel message (status 0x80..0xEF), as it came: the part's MIDI setting (omni, a channel, MPE) decides what
 * it means (dsp/mpe.c: MPE as RP-053 has it, zones and all) */
void masque_midi_isr(int status, int d1, int d2);
void masque_clock_isr(int what);                   /* 0xF8 clock, 0xFA start, 0xFC stop */

/* the oscillator, for the screen and the tools: one cycle of mask m (0..511.75) into out[n], -1..1 */
void masque_mask_wave(float m, float *out, int n);
const char *masque_name_of(int kind, int v);       /* NAME_* below: a value as the panel shows it */
enum { NAME_LOOP, NAME_FMODE, NAME_EXTRA, NAME_PMODE, NAME_LWAVE, NAME_LDEST, NAME_XSRC, NAME_XDEST, NAME_FX1,
       NAME_FX2, NAME_ARP, NAME_VCURVE, NAME_XCURVE, NAME_XZONE, NAME_TS, NAME_MIDI };
/* the tuning, for the screen: each of the twelve notes' cents from equal temperament (A at 0) */
void masque_tuning_cents(int system, int root, float out[12]);
/* the effects, for the panel: a short name (the TYPE knob's) and the full one (the page's title); whether effect t
 * may go in a slot beside other (the other slot's); the settings an effect starts from; each knob k (0..MQ_FX_K-1):
 * its label, range, where its value is in p (two bytes when wide: a mask), and how it reads. A knob can depend on the
 * others (a synced time shows the note value) */
const char *masque_fx_short(int t);        /* "CHORUS", "SHAPER", "OFF" */
const char *masque_fx_long(int t);         /* "CHORUS", "MASK SHAPER" */
int masque_fx_allowed(int t, int other);
void masque_fx_defaults(int t, uint8_t p[MQ_FX_P]);
typedef struct { const char *label; int16_t lo, hi; uint8_t at, wide, unit; } mq_fxk_t;
int masque_fx_knob(int t, int k, const uint8_t p[MQ_FX_P], mq_fxk_t *d);   /* 0: no knob there */
int masque_fx_get(const mq_fxk_t *d, const uint8_t p[MQ_FX_P]);
void masque_fx_set(const mq_fxk_t *d, uint8_t p[MQ_FX_P], int v);
void masque_fx_text(const mq_fxk_t *d, int v, char *out);
int masque_fx_synced(int t, const uint8_t p[MQ_FX_P]);   /* its time follows the tempo */

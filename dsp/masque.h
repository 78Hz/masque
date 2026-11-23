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
       LD_NFREQ, LD_NVOL, LD_FX1TYP, LD_FX2TYP, LD_FX2BAL, NLDESTS };
/* the mod wheel's and aftertouch's destinations (16) */
enum { MD_FLT, MD_L1AMT, MD_L1SPD, MD_L2AMT, MD_L2SPD, MD_VOL, MD_MASK, MD_FREQ, MD_O1VOL, MD_O1MASK, MD_O2VOL,
       MD_O2MASK, MD_NFREQ, MD_NVOL, MD_FX1BAL, MD_FX2BAL, NMDESTS };
/* velocity's destinations (6) */
enum { VD_FLT, VD_FEG, VD_VOL, VD_MASK, VD_PEG, VD_ATK, NVDESTS };
/* FX1: off, then 16 of each family; FX2 the same */
enum { F1_OFF, F1_CH = 1, F1_DT = 17, F1_BC = 33, F1_RM = 49, NF1 = 65 };
enum { F2_OFF, F2_DL = 1, F2_RV = 17, F2_DT = 33, NF2 = 49 };
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
    struct { uint8_t dest; int8_t amt; } vel, wheel, at;   /* AMT -99..99 */
    struct { uint8_t mode, vol; int8_t semi; uint8_t port, pans, rate, bend, rsv; } gen;
    struct { uint8_t t1, b1, t2, b2; } fx;  /* types (F1_*, F2_* + variant), balances 0..100 */
    struct { uint8_t type, spd2, hold, trig; } arp;   /* SPD in 2 BPM steps: 10..200 = 20..400 BPM */
} mq_patch_t;

/* the part's settings, not the sound's (kept with the setup) */
typedef struct {
    uint16_t a_hz10;                       /* A, tenths of a hertz: 4000..4700 */
    uint8_t vel_curve;                     /* 0..5, 6 = MAX (velocity off) */
    uint8_t at_curve;                      /* 0..5 */
    uint8_t voices;                        /* 4..12 */
    uint8_t rsv[3];
} mq_global_t;

/* what the screens show (written by the render, read by the main loop) */
typedef struct {
    uint32_t voices, voices_peak, allowed;
    float mask_now[2];                     /* where the last note's masks are now (envelopes, LFOs), for the scope */
    float load;                            /* the render's share of its time, 0..1 */
    uint32_t sounding[4];                  /* the MIDI notes sounding: bit k % 32 of word k / 32 */
    uint8_t loop_state, arp_on, cpu2;      /* LS_* */
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
void masque_bend(int v14);                         /* 0..16383 */
void masque_loop_rec(void);                        /* REC: arm / stop recording (and play) */
void masque_loop_play(void);                       /* PLAY: play / stop */
void masque_loop_clear(void);
/* the render's own (MIDI in): the same, straight in */
void masque_note_on_isr(int key, int vel);
void masque_note_off_isr(int key);
void masque_pressure_isr(int key, int v);
void masque_chan_pressure_isr(int v);
void masque_cc_isr(int cc, int v);
void masque_bend_isr(int v14);
void masque_clock_isr(int what);                   /* 0xF8 clock, 0xFA start, 0xFC stop */

/* the oscillator, for the screen and the tools: one cycle of mask m (0..511.75) into out[n], -1..1 */
void masque_mask_wave(float m, float *out, int n);
const char *masque_name_of(int kind, int v);       /* NAME_* below: a value as the panel shows it */
enum { NAME_LOOP, NAME_FMODE, NAME_EXTRA, NAME_PMODE, NAME_LWAVE, NAME_LDEST, NAME_MDEST, NAME_VDEST, NAME_FX1,
       NAME_FX2, NAME_ARP, NAME_VCURVE };
void masque_fx_name(int which, int t, char *out);  /* "CH03", "RV12", "OFF" */

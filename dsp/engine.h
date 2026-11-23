/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the engine's inside: what the sequencer (seq.c) and the effects (fx.c) share with the voices (engine.c). */
#pragma once
#include <stdint.h>
#include "masque.h"
#include "fastmath.h"
#define memset __builtin_memset
#define memcpy __builtin_memcpy

/* the channels a note can come from: MIDI's 0..15, the FM-1's own keys, the arpeggiator */
#define CH_LOCAL 16
#define CH_ARP 17
/* the voices' side, for the sequencer: a note as the player (after the looper and the arpeggiator) plays it.
 * legato: the arpeggiator without TRIG (the sounding voice moves to the new key, nothing restarts) */
void eng_note_on(int key, float vel, int legato, int ch);
void eng_note_off(int key, int relvel, int ch);
void eng_all_off(void);
float eng_vel(int v);                       /* the part's velocity curve: 1..127 -> 0..1 */

/* the sequencer: what comes in from the player goes through here first */
void seq_init(void);
void seq_set(const mq_patch_t *p);
void seq_note_on(int key, int vel, int ch); /* raw velocity 1..127; ch 0..15, CH_LOCAL */
void seq_note_off(int key, int relvel, int ch);
void seq_expr(int ch, int kind, int v14);    /* a channel's expression (EX_*): recorded by the looper, then played */
void seq_block(uint32_t n);                 /* the block's time: arpeggio steps, the loop's events */
void seq_loop_rec(void);
void seq_loop_play(void);
void seq_loop_clear(void);
void seq_clock(int what);
void seq_ctl(int type, int a);              /* bend, wheel, pressure, sustain: recorded by the looper, then played */
enum { SQ_WHEEL = 1, SQ_SUSTAIN = 3, SQ_CC0 = 16 };   /* (SQ_CC0 + n: CC n's value, for the matrix) */
void eng_ctl(int type, int a);              /* the same, to the voices */
/* a channel's expression (dsp/mpe.c resolves what MIDI means): EX_BEND -8192..8191, EX_PRESS / EX_AT / EX_TIMBRE
 * 0..16383, EX_RANGE the channel's bend range in semitones x 100 (0: the sound's BEND), EX_RESET the channel forgets */
enum { EX_BEND, EX_PRESS, EX_AT, EX_TIMBRE, EX_RANGE, EX_RESET };
void eng_expr(int ch, int kind, int v);
void eng_poly_pressure(int key, int ch, int v);   /* a key's pressure (poly aftertouch, the FM-1's keys): 0..127 */

/* MPE and MIDI in (mpe.c): the part's channel setting, the zones, the bend ranges */
void mpe_set_mode(int midi);               /* 0 omni, 1..16, MQ_MIDI_MPE */
void mpe_midi(int status, int d1, int d2);
int mpe_zone_of(int ch);                    /* -1: in no zone; 0 the lower, 1 the upper */
int mpe_is_member(int ch);
void mpe_meter(mq_meter_t *m);
void mpe_init(void);

/* the tuning (tuning.c) */
void tuning_set(const mq_global_t *g);
float tuning_l2(float steps);               /* log2(Hz) of a pitch in steps */
int tuning_edo(void);
int tuning_interval(int semis);
void tuning_cents(int system, int root, float out[12]);
void seq_meter(mq_meter_t *m);
void seq_panic(void);
int seq_held_count(void);                   /* physical keys down (for the LFOs' delay) */

/* the effects */
typedef struct {
    int t1, t2;                             /* type (F1_*, F2_*), after the LFO */
    float b1, b2;                           /* balance 0..1, after the mods */
} fx_ctl_t;
void fx_init(void);
void fx_process(float *l, float *r, uint32_t n, const fx_ctl_t *c);
void fx_clear(void);

/* the plate (plate.c), FX2's reverbs */
void plate_init(void);
void plate_clear(void);
void plate_variant(int v);                  /* 0..15: size and decay */
void plate_process(const float *in_l, const float *in_r, float *out_l, float *out_r, uint32_t n, float wet);

static inline uint32_t eng_rand(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static inline float eng_frand(uint32_t *s) { return (float)(int32_t)eng_rand(s) * (1.0f / 2147483648.0f); }

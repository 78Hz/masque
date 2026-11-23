/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the engine's inside: what the sequencer (seq.c) and the effects (fx.c) share with the voices (engine.c). */
#pragma once
#include <stdint.h>
#include "masque.h"
#include "fastmath.h"
#define memset __builtin_memset
#define memcpy __builtin_memcpy

/* the voices' side, for the sequencer: a note as the player (after the looper and the arpeggiator) plays it.
 * legato: the arpeggiator without TRIG (the sounding voice moves to the new key, nothing restarts) */
void eng_note_on(int key, float vel, int legato);
void eng_note_off(int key);
void eng_all_off(void);
float eng_vel(int v);                       /* the part's velocity curve: 1..127 -> 0..1 */

/* the sequencer: what comes in from the player goes through here first */
void seq_init(void);
void seq_set(const mq_patch_t *p);
void seq_note_on(int key, int vel);        /* raw velocity 1..127 */
void seq_note_off(int key);
void seq_block(uint32_t n);                 /* the block's time: arpeggio steps, the loop's events */
void seq_loop_rec(void);
void seq_loop_play(void);
void seq_loop_clear(void);
void seq_clock(int what);
void seq_ctl(int type, int a);              /* bend, wheel, pressure, sustain: recorded by the looper, then played */
enum { SQ_BEND, SQ_WHEEL, SQ_CHAT, SQ_SUSTAIN };
void eng_ctl(int type, int a);              /* the same, to the voices */
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

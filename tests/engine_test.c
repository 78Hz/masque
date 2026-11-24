#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE
/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the engine alone, on a computer: every factory sound played (a chord, held, then let go) into
 * build/wav/, and checks: the sound is finite, it is heard, it is not too loud, it ends; the play modes allocate as
 * they should; the arpeggiator plays steps; the looper plays back what it recorded. Exit 0 if all pass.
 *   tools/build_tools.sh && build/engine_test [preset] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include "masque.h"
#include "presets.h"
#include "engine.h"

#define FS MQ_FS
#define BL 256u
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static float bl[BL], br[BL];

static void wav_write(const char *path, const float *l, const float *r, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t i, rate = 44118, bytes = n * 4u, v;
    uint16_t s;
    if (!f) return;
    fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 4; fwrite(&v, 4, 1, f); s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (i = 0; i < n; i++) {
        float a = tanhf(l[i] * 0.7f), b = tanhf(r[i] * 0.7f);
        int16_t x = (int16_t)(a * 30000.0f), y = (int16_t)(b * 30000.0f);
        fwrite(&x, 2, 1, f);
        fwrite(&y, 2, 1, f);
    }
    fclose(f);
}

/* render n samples into the buffers (may be NULL: discarded); returns the peak */
static float run(float *L, float *R, uint32_t n)
{
    uint32_t done = 0, i;
    float pk = 0.0f;
    while (done < n) {
        uint32_t k = n - done < BL ? n - done : BL;
        masque_render(bl, br, k);
        for (i = 0; i < k; i++) {
            if (L) { L[done + i] = bl[i]; R[done + i] = br[i]; }
            if (!isfinite(bl[i]) || !isfinite(br[i])) pk = 1e9f;
            if (fabsf(bl[i]) > pk) pk = fabsf(bl[i]);
            if (fabsf(br[i]) > pk) pk = fabsf(br[i]);
        }
        done += k;
    }
    return pk;
}

static double rms(const float *x, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++) s += (double)x[i] * x[i];
    return sqrt(s / (n ? n : 1));
}

static void play_preset(int p, int write)
{
    static float L[264800], R[264800];
    mq_patch_t pt;
    uint32_t n1 = (uint32_t)(FS * 2.0f), n2 = (uint32_t)(FS * 4.0f), t;
    float pk;
    double r1, rtail;
    char path[64], name[11];
    int i, chord = 1;
    preset_get(p, &pt);
    memcpy(name, pt.name, 10);
    name[10] = 0;
    for (i = 9; i >= 0 && name[i] == ' '; i--) name[i] = 0;
    masque_panic();
    masque_set_patch(&pt);
    run(0, 0, 512);
    if (pt.gen.mode == PM_MONO || pt.gen.mode == PM_MONO_PORTA || pt.gen.mode == PM_MONO_RETRIG || pt.arp.type)
        chord = 0;
    /* a phrase: C3 E3 G3 C4 (a chord), or a line for the mono sounds */
    if (chord) {
        masque_note_on(48, 100); masque_note_on(52, 90); masque_note_on(55, 96); masque_note_on(60, 110);
        pk = run(L, R, n1);
        masque_note_off(48); masque_note_off(52); masque_note_off(55); masque_note_off(60);
    } else {
        uint32_t q = n1 / 4;
        masque_note_on(48, 100); pk = run(L, R, q);
        masque_note_on(51, 100); masque_note_off(48); pk = fmaxf(pk, run(L + q, R + q, q));
        masque_note_on(55, 100); masque_note_off(51); pk = fmaxf(pk, run(L + 2 * q, R + 2 * q, q));
        masque_note_on(60, 100); masque_note_off(55);
        pk = fmaxf(pk, run(L + 3 * q, R + 3 * q, n1 - 3 * q));
        masque_note_off(60);
    }
    t = n1;
    pk = fmaxf(pk, run(L + t, R + t, n2));
    r1 = rms(L, n1);
    rtail = rms(L + n1 + n2 - (uint32_t)(FS * 0.5f), (uint32_t)(FS * 0.5f));
    printf("P%02d %-10s peak %6.3f  rms %6.3f  tail %8.5f  voices %u\n", p, name, pk, r1, rtail,
           masque_meter()->voices);
    CHECK(pk < 1e8f, "P%02d %s: not finite", p, name);
    CHECK(r1 > 0.004 && pk > 0.15f, "P%02d %s: too quiet (rms %.4f, peak %.3f)", p, name, r1, pk);
    CHECK(pk < 6.0f, "P%02d %s: too loud (peak %.2f)", p, name, pk);
    CHECK(rtail < 0.05, "P%02d %s: still sounding 4 s after (rms %.4f)", p, name, rtail);
    if (write) {
        mkdir("build/wav", 0755);
        snprintf(path, sizeof path, "build/wav/P%02d.wav", p);
        wav_write(path, L, R, n1 + n2);
    }
}

static int sounding(int k) { return (masque_meter()->sounding[k >> 5] >> (k & 31)) & 1u; }

static void modes(void)
{
    mq_patch_t p;
    preset_init(&p);
    p.osc[0].env.r = 0;
    /* POLY: four keys, four voices */
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(60, 100); masque_note_on(64, 100); masque_note_on(67, 100); masque_note_on(71, 100);
    run(0, 0, 2048);
    CHECK(masque_meter()->voices == 4, "POLY: %u voices for 4 keys", masque_meter()->voices);
    masque_note_off(60); masque_note_off(64); masque_note_off(67); masque_note_off(71);
    run(0, 0, (uint32_t)FS);
    CHECK(masque_meter()->voices == 0, "POLY: %u voices left after release", masque_meter()->voices);
    /* MONO: one */
    p.gen.mode = PM_MONO;
    masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(60, 100); masque_note_on(64, 100); masque_note_on(67, 100);
    run(0, 0, 2048);
    CHECK(masque_meter()->voices == 1, "MONO: %u voices", masque_meter()->voices);
    masque_note_off(67); run(0, 0, 1024);
    CHECK(masque_meter()->voices == 1 && sounding(64) && !sounding(67), "MONO: not back to 64 after 67 let go");
    masque_note_off(60); masque_note_off(64); run(0, 0, (uint32_t)FS);
    /* SLUR: 60 then 62 legato: one voice; 60 then 67: two */
    p.gen.mode = PM_SLUR;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(60, 100); run(0, 0, 1024); masque_note_on(62, 100); run(0, 0, 1024);
    CHECK(masque_meter()->voices == 1, "SLUR: %u voices for a step of 2", masque_meter()->voices);
    masque_note_on(69, 100); run(0, 0, 1024);
    CHECK(masque_meter()->voices == 2, "SLUR: %u voices for a leap", masque_meter()->voices);
    masque_note_off(60); masque_note_off(62); masque_note_off(69); run(0, 0, (uint32_t)FS);
    CHECK(masque_meter()->voices == 0, "SLUR: %u voices left", masque_meter()->voices);
    /* PARA: three keys, three voices, one envelope */
    p.gen.mode = PM_PARA;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(60, 100); masque_note_on(64, 100); masque_note_on(67, 100); run(0, 0, 2048);
    CHECK(masque_meter()->voices == 3, "PARA: %u voices", masque_meter()->voices);
    masque_note_off(60); masque_note_off(64); masque_note_off(67); run(0, 0, (uint32_t)FS);
    CHECK(masque_meter()->voices == 0, "PARA: %u voices left", masque_meter()->voices);
    /* the arpeggiator: one key, steps in a second at 120 BPM (8 a second) */
    p.gen.mode = PM_POLY;
    p.arp.type = AR_1OCT;
    p.arp.spd2 = 60;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(60, 100); masque_note_on(64, 100);
    {
        int i, changes = 0;
        uint32_t last = 0;
        for (i = 0; i < 172; i++) {
            uint32_t s;
            run(0, 0, 256);
            s = masque_meter()->sounding[1] ^ masque_meter()->sounding[2] * 7u;
            if (s && s != last) changes++;
            if (s) last = s;
        }
        CHECK(changes >= 6 && changes <= 10, "ARP: %d steps in a second (8 expected)", changes);
    }
    masque_note_off(60); masque_note_off(64); run(0, 0, (uint32_t)FS);
    CHECK(masque_meter()->voices == 0, "ARP: %u voices left", masque_meter()->voices);
    /* the sustain pedal holds a key let go; up, it goes */
    p.arp.type = AR_NONE;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(60, 100); masque_cc(64, 127); run(0, 0, 1024); masque_note_off(60); run(0, 0, (uint32_t)FS);
    CHECK(masque_meter()->voices == 1, "SUSTAIN: %u voices with the pedal down", masque_meter()->voices);
    masque_cc(64, 0); run(0, 0, (uint32_t)FS);
    CHECK(masque_meter()->voices == 0, "SUSTAIN: %u voices after the pedal", masque_meter()->voices);
    /* the arpeggiator without TRIG: one voice moving; HOLD: it goes on after the keys are let go */
    p.arp.type = AR_1OCT;
    p.arp.trig = 0;
    p.arp.hold = 1;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(60, 100); masque_note_on(64, 100); run(0, 0, (uint32_t)FS / 2);
    CHECK(masque_meter()->voices == 1, "ARP legato: %u voices", masque_meter()->voices);
    masque_note_off(60); masque_note_off(64); run(0, 0, (uint32_t)FS);
    CHECK(masque_meter()->voices == 1, "ARP HOLD: %u voices after the keys are let go", masque_meter()->voices);
    p.arp.type = AR_NONE;
    p.arp.trig = 1;
    p.arp.hold = 0;
    masque_set_patch(&p); run(0, 0, (uint32_t)FS);
    CHECK(masque_meter()->voices == 0, "ARP off: %u voices left", masque_meter()->voices);
    /* the looper: record a key, stop, it plays again by itself */
    p.arp.type = AR_NONE;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_loop_rec(); run(0, 0, 256);
    CHECK(masque_meter()->loop_state == LS_ARMED, "LOOP: not armed (%d)", masque_meter()->loop_state);
    masque_note_on(60, 100); run(0, 0, 4096); masque_note_off(60); run(0, 0, 4096 * 4);
    CHECK(masque_meter()->loop_state == LS_RECORDING, "LOOP: not recording");
    masque_loop_rec(); run(0, 0, 1024);
    CHECK(masque_meter()->loop_state == LS_PLAYING, "LOOP: not playing");
    {
        int i, heard = 0;
        for (i = 0; i < 400; i++) {
            run(0, 0, 256);
            if (sounding(60)) heard++;
        }
        CHECK(heard > 0, "LOOP: the recorded key never sounded again");
    }
    masque_loop_play(); run(0, 0, (uint32_t)FS);
    CHECK(masque_meter()->loop_state == LS_STOPPED, "LOOP: not stopped");
    CHECK(masque_meter()->voices == 0, "LOOP: %u voices after stop", masque_meter()->voices);
}

static void masks(void)
{
    static float w[1024];
    int m, i;
    for (m = 0; m < 512; m++) {
        float pk = 0, mean = 0;
        masque_mask_wave((float)m, w, 1024);
        for (i = 0; i < 1024; i++) {
            mean += w[i];
            if (fabsf(w[i]) > pk) pk = fabsf(w[i]);
        }
        mean /= 1024.0f;
        CHECK(pk > 0.3f && pk < 1.05f, "mask %d: peak %.3f", m, pk);
        CHECK(fabsf(mean) < 0.05f, "mask %d: DC %.3f", m, mean);
    }
}

/* the filter: the comb rings at its cutoff's pitch; full resonance and drive stay bounded */
static void filter(void)
{
    static float L[44100], R[44100];
    mq_patch_t p;
    int i, best = 0;
    double bm = 0, pk = 0;
    preset_init(&p);
    p.osc[0].env.l = 0;                    /* noise alone, through a comb tuned to A (69: 440 Hz) */
    p.noise.freq = 99;
    p.noise.env.l = 70; p.noise.env.s = 99; p.noise.env.d = 0;
    p.filt.mode = FM_COMBP; p.filt.cutf = 69; p.filt.reso = 99; p.filt.trk = 0;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(60, 100);
    run(L, R, 44100);
    masque_note_off(60);
    for (i = 300; i < 700; i += 2) {        /* the strongest partial, by a plain DFT */
        double re = 0, im = 0, m;
        int n;
        for (n = 4410; n < 44100; n++) {
            re += L[n] * cos(2 * M_PI * i * n / FS);
            im += L[n] * sin(2 * M_PI * i * n / FS);
        }
        m = re * re + im * im;
        if (m > bm) { bm = m; best = i; }
    }
    CHECK(best > 425 && best < 455, "COMB+: rings at %d Hz, not 440", best);
    run(0, 0, (uint32_t)FS * 2);
    /* a low pass at full resonance and full drive, a saw into it: loud, but bounded */
    preset_init(&p);
    p.osc[0].mask = 23; p.osc[0].env.s = 99;
    p.filt.mode = FM_LP; p.filt.cutf = 60; p.filt.reso = 99; p.filt.drive = 99; p.filt.extra = FX_24DB;
    p.filt.env.l = 60; p.filt.env.d = 50; p.filt.env.loop = 1;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(36, 127); masque_note_on(48, 127); masque_note_on(60, 127);
    pk = run(L, R, 44100);
    CHECK(pk < 6.0, "DRIVE + RESO 99: peak %.2f", pk);
    CHECK(rms(L, 44100) > 0.05, "DRIVE + RESO 99: silent (rms %.3f)", rms(L, 44100));
    masque_note_off(36); masque_note_off(48); masque_note_off(60);
    run(0, 0, (uint32_t)FS * 3);
    printf("filter: comb at %d Hz; driven resonance peak %.2f\n", best, pk);
}

/* the frequency the part plays now: a sine, counted by its rising zero crossings over half a second */
static double freq_now(void)
{
    static float L[22059], R[22059];
    int i, n = 0, first = -1, last = 0;
    run(0, 0, 4410);
    run(L, R, 22059);
    for (i = 1; i < 22059; i++)
        if (L[i - 1] < 0.0f && L[i] >= 0.0f) {
            double x = i - 1 + L[i - 1] / (L[i - 1] - L[i]);
            if (first < 0) first = (int)(x * 1000);
            last = (int)(x * 1000);
            n++;
        }
    return n > 1 ? (n - 1) * FS / ((last - first) / 1000.0) : 0.0;
}

static void sine_patch(mq_patch_t *p)
{
    preset_init(p);
    p->osc[0].env.s = 99; p->osc[0].env.r = 0;
    p->gen.pans = 0;
    p->fx.t1 = p->fx.t2 = 0;
    memset(p->mtx, 0, sizeof p->mtx);
}

static mq_global_t glob(int a100, int sys, int edo, int root, int midi)
{
    mq_global_t g;
    memset(&g, 0, sizeof g);
    g.a_hz100 = (uint16_t)a100; g.system = (uint8_t)sys; g.edo = (uint8_t)edo; g.root = (uint8_t)root;
    g.vel_curve = 2; g.at_curve = 2; g.voices = 10; g.midi = (uint8_t)midi;
    return g;
}

#define NEAR(x, want, cents) (fabs(1200.0 * log2((x) / (want))) < (cents))
static void tunings(void)
{
    mq_patch_t p;
    mq_global_t g;
    double f, c, e;
    sine_patch(&p);
    g = glob(44000, TS_EQUAL, 12, 0, 0);
    masque_set_global(&g); masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(69, 100); f = freq_now(); masque_note_off(69); run(0, 0, 4410);
    CHECK(NEAR(f, 440.0, 2), "TUNING: A plays %.2f Hz, not 440", f);
    g = glob(41500, TS_EQUAL, 12, 0, 0);
    masque_set_global(&g); run(0, 0, 256);
    masque_note_on(69, 100); f = freq_now(); masque_note_off(69); run(0, 0, 4410);
    CHECK(NEAR(f, 415.0, 2), "TUNING: A at 415 plays %.2f Hz", f);
    g = glob(44000, TS_EQUAL, 31, 0, 0);   /* 31 steps to the octave: A + 31 steps is the octave, + 1 a 31st */
    masque_set_global(&g); run(0, 0, 256);
    masque_note_on(100, 100); f = freq_now(); masque_note_off(100); run(0, 0, 4410);
    CHECK(NEAR(f, 880.0, 2), "TUNING 31-EDO: A + 31 steps plays %.2f Hz, not 880", f);
    masque_note_on(70, 100); f = freq_now(); masque_note_off(70); run(0, 0, 4410);
    CHECK(NEAR(f, 440.0 * pow(2.0, 1.0 / 31.0), 2), "TUNING 31-EDO: one step plays %.2f Hz", f);
    g = glob(44000, TS_MEANTONE, 12, 0, 0);   /* 1/4-comma meantone on C: the major third C-E pure (5/4) */
    masque_set_global(&g); run(0, 0, 256);
    masque_note_on(60, 100); c = freq_now(); masque_note_off(60); run(0, 0, 4410);
    masque_note_on(64, 100); e = freq_now(); masque_note_off(64); run(0, 0, 4410);
    CHECK(NEAR(e / c, 1.25, 2), "TUNING MEANTONE: C-E is %.4f, not 5/4", e / c);
    printf("tunings: A 440 / 415, 31-EDO, meantone C-E %.4f\n", e / c);
    g = glob(44000, TS_EQUAL, 12, 0, 0);
    masque_set_global(&g); run(0, 0, 256);
}

static void mpe(void)
{
    mq_patch_t p;
    mq_global_t g;
    double f;
    static float L[11025], R[11025];
    double quiet, loud, plain;
    sine_patch(&p);
    g = glob(44000, TS_EQUAL, 12, 0, MQ_MIDI_MPE);
    masque_set_global(&g); masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    CHECK(masque_meter()->mpe_lower == 15, "MPE: the power-on zone has %d members, not 15", masque_meter()->mpe_lower);
    /* a Member Channel's bend moves its note, at +-48 */
    masque_midi_isr(0xE1, (8192 + 341) & 127, (8192 + 341) >> 7);   /* +2 semitones on channel 2, before the note */
    masque_midi_isr(0x91, 69, 100);
    f = freq_now();
    CHECK(NEAR(f, 440.0 * pow(2.0, 2.0 / 12.0), 5), "MPE: a Member's bend of +2 plays %.2f Hz", f);
    /* the Master Channel's bend adds, at +-2 */
    masque_midi_isr(0xE0, 0x7F, 0x7F);
    f = freq_now();
    CHECK(NEAR(f, 440.0 * pow(2.0, 4.0 / 12.0), 5), "MPE: Member +2 and Master +2 play %.2f Hz", f);
    masque_midi_isr(0xE0, 0, 64); masque_midi_isr(0xE1, 0, 64); masque_midi_isr(0x81, 69, 0); run(0, 0, 4410);
    /* PRESS -> VOL: before any pressure the row leaves the note alone; with pressure 0 it is silent */
    p.mtx[0].src = XS_PRESS; p.mtx[0].dest = XD_VOL; p.mtx[0].amt = 99; p.mtx[0].hi = 100;
    masque_set_patch(&p); run(0, 0, 256);
    masque_midi_isr(0x92, 69, 100); run(0, 0, 2048);
    run(L, R, 11025); plain = rms(L, 11025);
    masque_midi_isr(0xD2, 0, 0); run(0, 0, 2048);
    run(L, R, 11025); quiet = rms(L, 11025);
    masque_midi_isr(0xD2, 127, 0); run(0, 0, 2048);
    run(L, R, 11025); loud = rms(L, 11025);
    CHECK(plain > 0.02, "MPE: a note with no pressure yet is silent (rms %.4f)", plain);
    CHECK(quiet < 0.002, "MPE: pressure 0 on PRESS -> VOL 99 still sounds (rms %.4f)", quiet);
    CHECK(loud > 0.02, "MPE: full pressure is silent (rms %.4f)", loud);
    masque_midi_isr(0x82, 69, 0); run(0, 0, 4410);
    /* an MCM: the Lower Zone to 7 Members; channel 10 is then in no zone, its bend +-2 (the sound's BEND) */
    masque_midi_isr(0xB0, 101, 0); masque_midi_isr(0xB0, 100, 6); masque_midi_isr(0xB0, 6, 7);
    run(0, 0, 256);
    CHECK(masque_meter()->mpe_lower == 7, "MPE: after the MCM %d members, not 7", masque_meter()->mpe_lower);
    masque_midi_isr(0xE9, 0x7F, 0x7F);
    masque_midi_isr(0x99, 69, 100);
    f = freq_now();
    CHECK(NEAR(f, 440.0 * pow(2.0, 2.0 / 12.0), 5), "MPE: channel 10 out of the zone bends to %.2f Hz", f);
    masque_midi_isr(0x89, 69, 0); masque_midi_isr(0xE9, 0, 64);
    /* RPN 0 on a Member sets every Member's range: 12 semitones, then a full bend on channel 3 is an octave */
    masque_midi_isr(0xB1, 101, 0); masque_midi_isr(0xB1, 100, 0); masque_midi_isr(0xB1, 6, 12);
    masque_midi_isr(0xE2, 0x7F, 0x7F); masque_midi_isr(0x92, 57, 100);
    f = freq_now();
    CHECK(NEAR(f, 440.0, 5), "MPE: RPN 0 = 12 on channel 2, a full bend on channel 3 plays %.2f Hz", f);
    masque_midi_isr(0x82, 57, 0); masque_midi_isr(0xE2, 0, 64); run(0, 0, 4410);
    printf("mpe: zones, member and master bend, pressure exists, MCM, RPN 0\n");
    g = glob(44000, TS_EQUAL, 12, 0, 0);
    masque_set_global(&g); masque_panic(); run(0, 0, 256);
}

static void matrix(void)
{
    mq_patch_t p;
    static float L[11025], R[11025];
    double soft, hard;
    sine_patch(&p);
    p.mtx[0].src = XS_VEL; p.mtx[0].dest = XD_VOL; p.mtx[0].amt = 80; p.mtx[0].hi = 100;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(69, 20); run(0, 0, 2048); run(L, R, 11025); soft = rms(L, 11025); masque_note_off(69); run(0, 0, 4410);
    masque_note_on(69, 127); run(0, 0, 2048); run(L, R, 11025); hard = rms(L, 11025); masque_note_off(69); run(0, 0, 4410);
    CHECK(hard > 2.5 * soft, "MATRIX: VEL -> VOL 80: hard %.4f, soft %.4f", hard, soft);
    /* the FM-1's own keys' pressure is AT: AT -> MASK moves the mask */
    p.mtx[0].src = XS_AT; p.mtx[0].dest = XD_MASK; p.mtx[0].amt = 99;
    masque_set_patch(&p); run(0, 0, 256);
    masque_note_on(69, 100); run(0, 0, 2048);
    CHECK(masque_meter()->mask_now[0] < 31.5f, "MATRIX: no pressure yet, yet the mask moved (%.2f)", masque_meter()->mask_now[0]);
    masque_pressure(69, 127); run(0, 0, 4096);
    CHECK(masque_meter()->mask_now[0] > 150.0f, "MATRIX: AT -> MASK 99 at full pressure: mask %.2f", masque_meter()->mask_now[0]);
    masque_note_off(69); run(0, 0, 4410);
    printf("matrix: velocity on the level, the keys' pressure on the mask\n");
}


/* the effects alone: every type finite and bounded; the delay's sync; the slicer at mask 31 the window, late; the
 * mask shaper at mask 31 and little drive nearly the signal */
static float fl[1 << 17], fr[1 << 17], gl[1 << 17], gr[1 << 17];
static void fx_run(const fx_ctl_t *c, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i += BL)
        fx_process(fl + i, fr + i, n - i < BL ? n - i : BL, c);
}
static void fx_src(uint32_t n, int kind)
{
    uint32_t i, rng = 1;
    for (i = 0; i < n; i++) {
        float x = 0.0f;
        rng = rng * 1664525u + 1013904223u;
        if (kind == 0)                         /* a chord with noise, coming and going */
            x = (0.25f * sinf(6.2831853f * 220.0f * i / FS) + 0.2f * sinf(6.2831853f * 331.0f * i / FS) +
                 0.1f * ((float)(rng >> 8) / 8388608.0f - 1.0f)) * ((i / 8000u) & 1u ? 1.0f : 0.3f);
        else if (kind == 1)
            x = i == 1000u ? 1.0f : 0.0f;
        else if (kind == 2)
            x = 0.2f * sinf(6.2831853f * 441.0f * i / FS);
        fl[i] = gl[i] = x;
        fr[i] = gr[i] = x;
    }
}
static double corr(const float *a, const float *b, uint32_t n)
{
    double ab = 0, aa = 0, bb = 0;
    uint32_t i;
    for (i = 0; i < n; i++) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    return ab / sqrt(aa * bb + 1e-30);
}
static void fx_set1(fx_ctl_t *c, int k, int t, float mix)
{
    c->t[k] = t;
    c->mix[k] = mix;
    masque_fx_defaults(t, c->p[k]);
}
static void effects(void)
{
    fx_ctl_t c;
    int k, t;
    uint32_t n = 88200, i;
    for (k = 0; k < 2; k++)                    /* every effect, in either slot: bounded, not silent */
        for (t = 1; t < NEFX; t++) {
            float pk = 0.0f;
            memset(&c, 0, sizeof c);
            fx_init();
            c.bpm = 120.0f;
            fx_set1(&c, k, t, 1.0f);
            fx_src(n, 0);
            fx_run(&c, n);
            for (i = 0; i < n; i++) {
                if (!isfinite(fl[i]) || !isfinite(fr[i])) pk = 1e9f;
                if (fabsf(fl[i]) > pk) pk = fabsf(fl[i]);
                if (fabsf(fr[i]) > pk) pk = fabsf(fr[i]);
            }
            CHECK(pk < 3.0f, "FX%d %s peak %.2f", k + 1, masque_fx_long(t), pk);
            CHECK(rms(fl + n / 2, n / 2) > 0.005, "FX%d %s silent", k + 1, masque_fx_long(t));
        }
    /* every knob of every effect at its ends: still bounded */
    for (t = 1; t < NEFX; t++) {
        int end, kk;
        for (end = 0; end < 2; end++) {
            float pk = 0.0f;
            memset(&c, 0, sizeof c);
            fx_init();
            c.bpm = 300.0f;
            fx_set1(&c, 0, t, 1.0f);
            for (kk = 0; kk < MQ_FX_K; kk++) {
                mq_fxk_t d;
                if (masque_fx_knob(t, kk, c.p[0], &d))
                    masque_fx_set(&d, c.p[0], end ? d.hi : d.lo);
            }
            fx_src(n / 2, 0);
            fx_run(&c, n / 2);
            for (i = 0; i < n / 2; i++) {
                if (!isfinite(fl[i])) pk = 1e9f;
                if (fabsf(fl[i]) > pk) pk = fabsf(fl[i]);
            }
            CHECK(pk < 6.0f, "%s at its %s: peak %.2f", masque_fx_long(t), end ? "top" : "bottom", pk);
        }
    }
    CHECK(!masque_fx_allowed(E_SLICER, E_DELAY) && !masque_fx_allowed(E_FLANGER, E_CHORUS) &&
          !masque_fx_allowed(E_PLATE, E_PLATE) && masque_fx_allowed(E_DIST, E_DIST) &&
          masque_fx_allowed(E_DELAY, E_PLATE), "the effects' memory rules");
    /* the delay: a quarter at 120 BPM is 0.5 s */
    memset(&c, 0, sizeof c);
    fx_init();
    c.bpm = 120.0f;
    fx_set1(&c, 1, E_DELAY, 1.0f);
    c.p[1][1] = 0; c.p[1][2] = 1; c.p[1][6] = 10; c.p[1][4] = 99;
    fx_src(n, 3);                              /* silence first, for the time to settle */
    fx_run(&c, n);
    fx_src(n, 1);
    fx_run(&c, n);
    {
        uint32_t at = 0;
        float pk = 0.0f;
        for (i = 0; i < n; i++)
            if (fabsf(fl[i]) > pk) { pk = fabsf(fl[i]); at = i; }
        CHECK(at > 1000u + 22058u - 40u && at < 1000u + 22058u + 40u, "delay sync: echo at %u", at - 1000u);
        printf("effects: all %d bounded in either slot; delay 1/4 at 120 BPM echoes after %.1f ms", NEFX - 1,
               (at - 1000u) * 1000.0 / FS);
    }
    /* the slicer, mask 31: the window, one window late */
    memset(&c, 0, sizeof c);
    fx_init();
    c.bpm = 120.0f;
    fx_set1(&c, 1, E_SLICER, 1.0f);
    c.p[1][0] = 31; c.p[1][1] = 0; c.p[1][4] = 99;
    fx_run(&c, 4096);                          /* the fade in from OFF */
    fx_src(n, 0);
    fx_run(&c, n);
    {
        uint32_t W = (uint32_t)(4.0f * exp2f((float)c.p[1][2] / 13.2f) * 0.001f * FS), best = 0;
        double bc = -2.0, kc;
        for (i = W - 30; i < W + 30; i++) {
            kc = corr(fl + 20000 + i, gl + 20000, 40000);
            if (kc > bc) { bc = kc; best = i; }
        }
        CHECK(bc > 0.98 && best >= W - 1 && best <= W + 1, "slicer mask 31: %u late, correlation %.3f", best, bc);
        printf("; slicer mask 31 %u samples late, the window %u (%.3f)", best, W, bc);
    }
    /* the mask shaper, mask 31, little drive: the signal, a little rounded */
    memset(&c, 0, sizeof c);
    fx_init();
    c.bpm = 120.0f;
    fx_set1(&c, 0, E_SHAPER, 1.0f);
    c.p[0][2] = 0; c.p[0][3] = 0; c.p[0][4] = 99;
    fx_run(&c, 4096);
    fx_src(n, 2);
    fx_run(&c, n);
    {
        double bc = -2.0, kc;
        for (i = 0; i < 40; i++) {
            kc = corr(fl + 20000 + i, gl + 20000, 40000);
            if (kc > bc) bc = kc;
        }
        CHECK(bc > 0.97, "mask shaper mask 31: correlation %.3f", bc);
        printf("; shaper mask 31 %.3f\n", bc);
    }
    fx_init();
}

static void cost(void)
{
    mq_patch_t p;
    uint32_t i;
    struct timespec a, b;
    double ms;
    preset_get(0, &p);
    p.fx.t1 = E_SHAPER; p.fx.b1 = 40;         /* the dearest pair: the 4x mask shaper and the plate */
    masque_fx_defaults(p.fx.t1, p.fx.p1);
    p.fx.t2 = E_PLATE;
    masque_fx_defaults(p.fx.t2, p.fx.p2);
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    for (i = 0; i < 10; i++)
        masque_note_on(48 + 3 * (int)i, 100);
    run(0, 0, 4096);
    clock_gettime(CLOCK_MONOTONIC, &a);
    run(0, 0, (uint32_t)FS * 4);
    clock_gettime(CLOCK_MONOTONIC, &b);
    ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
    printf("cost: 10 voices + mask shaper + plate, 4 s of sound in %.1f ms on this computer (%.1f %% of real time)\n", ms,
           ms / 40.0);
    for (i = 0; i < 10; i++)
        masque_note_off(48 + 3 * (int)i);
    run(0, 0, (uint32_t)FS * 3);
}

int main(int argc, char **argv)
{
    int p;
    masque_init();
    masks();
    if (argc > 1) {
        play_preset(atoi(argv[1]), 1);
    } else {
        for (p = 0; p < PRESET_COUNT; p++)
            play_preset(p, 1);
        modes();
        filter();
        tunings();
        mpe();
        matrix();
        effects();
        cost();
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}

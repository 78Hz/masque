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

static void cost(void)
{
    mq_patch_t p;
    uint32_t i;
    struct timespec a, b;
    double ms;
    preset_get(0, &p);
    p.fx.t2 = F2_RV + 10;
    masque_panic(); masque_set_patch(&p); run(0, 0, 256);
    for (i = 0; i < 10; i++)
        masque_note_on(48 + 3 * (int)i, 100);
    run(0, 0, 4096);
    clock_gettime(CLOCK_MONOTONIC, &a);
    run(0, 0, (uint32_t)FS * 4);
    clock_gettime(CLOCK_MONOTONIC, &b);
    ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
    printf("cost: 10 voices + plate, 4 s of sound in %.1f ms on this computer (%.1f %% of real time)\n", ms,
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
        cost();
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}

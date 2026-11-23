/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — what stands between the keys and the voices: the looper (one track, its events kept with their time) and
 * the arpeggiator (26 patterns, latchable, retriggered or slid), as the MASK1 has them. Everything here runs in the
 * render, a block at a time. */
#include "engine.h"
#include "plat.h"

#define FS MQ_FS
#define LOOP_MAX 4096u                     /* events */

typedef struct {
    uint32_t t;                            /* samples from the loop's start */
    uint8_t type, a, b, rsv;               /* LE_* */
} lev_t;
enum { LE_ON, LE_OFF, LE_CTL };
static lev_t loopbuf[LOOP_MAX] PLAT_POOL;

static struct {
    int state;                             /* LS_* */
    uint32_t n, len, pos, play_i;
    uint8_t rec_held[16], play_held[16];   /* bit sets of 128 keys */
} L;

static struct {
    int type, hold, trig;
    float bpm;
    int8_t list[16];                       /* the keys it plays, in the order pressed */
    uint8_t lvel[16];
    int n;
    int8_t seq[96];                        /* the pattern made from them */
    int nseq, idx;
    float t, step;                         /* samples into the step, the step's length */
    int sounding, svel;                    /* the key it holds now (-1 none) */
    int gate_off;                          /* its note-off still to come at half the step */
    uint32_t rng;
    uint32_t clocks, clock_seen;           /* MIDI clock: pulses, and when the last came (blocks ago) */
    int ext;
} A;

static uint8_t phys[128];                  /* keys down as played (live or from the loop): their velocities */
static int nphys;

static inline void bset(uint8_t *b, int k, int on)
{
    if (on) b[k >> 3] |= (uint8_t)(1u << (k & 7));
    else b[k >> 3] &= (uint8_t)~(1u << (k & 7));
}
static inline int bget(const uint8_t *b, int k) { return (b[k >> 3] >> (k & 7)) & 1; }

/* ------------------------------------------------------------------------------------------- arpeggiator --- */
static void arp_stop_note(void)
{
    if (A.sounding >= 0) {
        eng_note_off(A.sounding);
        A.sounding = -1;
    }
    A.gate_off = 0;
}

static void arp_build(void)
{
    static const int8_t CH[5][4] = {{0, 3, 7, -1}, {0, 4, 7, -1}, {0, 2, 7, -1}, {0, 5, 7, -1}, {0, 4, 7, 10}};
    int8_t s[16];
    int n = A.n, i, j, o, k = 0, oct = 1;
    for (i = 0; i < n; i++)                /* sorted, low to high */
        s[i] = A.list[i];
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && s[j - 1] > s[j]; j--) {
            int8_t t = s[j];
            s[j] = s[j - 1];
            s[j - 1] = t;
        }
#define ADD(x) do { int x_ = (x); if (k < 96 && x_ >= 0 && x_ < 128) A.seq[k++] = (int8_t)x_; } while (0)
    switch (A.type) {
    case AR_3OCT: oct++; /* fall through */
    case AR_2OCT: oct++; /* fall through */
    case AR_1OCT:
        for (o = 0; o < oct; o++)
            for (i = 0; i < n; i++) ADD(s[i] + 12 * o);
        break;
    case AR_DBL:
        for (i = 0; i < n; i++) { ADD(s[i]); ADD(s[i]); }
        break;
    case AR_ALT:
        for (i = 0, j = n - 1; i <= j; i++, j--) { ADD(s[i]); if (j != i) ADD(s[j]); }
        break;
    case AR_PON3: oct++; /* fall through */
    case AR_PON2: oct++; /* fall through */
    case AR_PONG: {
        int8_t up[48];
        int m = 0;
        for (o = 0; o < oct; o++)
            for (i = 0; i < n && m < 48; i++) up[m++] = (int8_t)(s[i] + 12 * o);
        for (i = 0; i < m; i++) ADD(up[i]);
        for (i = m - 2; i > 0; i--) ADD(up[i]);
        break;
    }
    case AR_RND3: oct++; /* fall through */
    case AR_RND2: oct++; /* fall through */
    case AR_RND:
        for (o = 0; o < oct; o++)
            for (i = 0; i < n; i++) ADD(s[i] + 12 * o);
        break;
    case AR_MIN: case AR_MAJ: case AR_SUS2: case AR_SUS4: case AR_7TH:
    case AR_MIN2: case AR_MAJ2: case AR_SU22: case AR_SU42: case AR_7TH2: {
        int c = A.type >= AR_MIN2 ? A.type - AR_MIN2 : A.type - AR_MIN, two = A.type >= AR_MIN2;
        for (i = 0; i < n; i++)
            for (o = 0; o <= two; o++)
                for (j = 0; j < 4; j++)
                    if (CH[c][j] >= 0) ADD(s[i] + CH[c][j] + 12 * o);
        break;
    }
    case AR_3O: case AR_2O: case AR_1O: {
        int m = A.type == AR_1O ? 1 : A.type == AR_2O ? 2 : 3;
        for (i = 0; i < n; i++)
            for (o = 0; o <= m; o++) ADD(s[i] + 12 * o);
        break;
    }
    case AR_5TH:
        for (i = 0; i < n; i++) { ADD(s[i]); ADD(s[i] + 7); }
        break;
    case AR_5TH2:
        for (i = 0; i < n; i++) { ADD(s[i]); ADD(s[i] + 7); ADD(s[i] + 12); ADD(s[i] + 19); }
        break;
    default:
        break;
    }
#undef ADD
    A.nseq = k;
    if (A.idx >= k)
        A.idx = 0;
}

static int arp_vel(int key)
{
    int i, v = 100;
    for (i = 0; i < A.n; i++)
        if (A.list[i] == key || (A.list[i] % 12) == (key % 12))
            v = A.lvel[i];
    return v;
}

static void arp_step(void)
{
    int key, legato;
    if (!A.nseq) {
        arp_stop_note();
        return;
    }
    if (A.type == AR_RND || A.type == AR_RND2 || A.type == AR_RND3)
        key = A.seq[eng_rand(&A.rng) % (uint32_t)A.nseq];
    else
        key = A.seq[A.idx++ % A.nseq];
    if (A.idx >= A.nseq)
        A.idx = 0;
    legato = !A.trig && A.sounding >= 0;
    if (legato) {
        int old = A.sounding;
        eng_note_on(key, eng_vel(arp_vel(key)), 1);
        if (old != key)
            eng_note_off(old);
        A.gate_off = 0;
    } else {
        arp_stop_note();
        eng_note_on(key, eng_vel(arp_vel(key)), 0);
        A.gate_off = 1;
    }
    A.sounding = key;
}

static void arp_add(int key, int vel)
{
    int i;
    if (A.hold && nphys == 1)              /* latched: a new chord starts the list again */
        A.n = 0;
    for (i = 0; i < A.n; i++)
        if (A.list[i] == key)
            return;
    if (A.n == 16) {
        for (i = 1; i < 16; i++) { A.list[i - 1] = A.list[i]; A.lvel[i - 1] = A.lvel[i]; }
        A.n = 15;
    }
    A.list[A.n] = (int8_t)key;
    A.lvel[A.n++] = (uint8_t)vel;
    arp_build();
    if (A.n == 1 && A.sounding < 0) {      /* the first key plays at once */
        A.idx = 0;
        A.t = 0.0f;
        A.clocks = 0;
        arp_build();
        arp_step();
    }
}

static void arp_remove(int key)
{
    int i, j;
    if (A.hold)
        return;
    for (i = j = 0; i < A.n; i++)
        if (A.list[i] != key) {
            A.list[j] = A.list[i];
            A.lvel[j++] = A.lvel[i];
        }
    A.n = j;
    arp_build();
    if (!A.n)
        arp_stop_note();
}

/* ------------------------------------------------------------------------------------------ to the voices --- */
static void play_on(int key, int vel)
{
    if (!phys[key])
        nphys++;
    phys[key] = (uint8_t)vel;
    if (A.type != AR_NONE)
        arp_add(key, vel);
    else
        eng_note_on(key, eng_vel(vel), 0);
}

static void play_off(int key)
{
    if (!phys[key])
        return;
    phys[key] = 0;
    nphys--;
    if (A.type != AR_NONE)
        arp_remove(key);
    else
        eng_note_off(key);
}

/* ------------------------------------------------------------------------------------------------ looper --- */
static void loop_put(int type, int a, int b)
{
    if (L.state != LS_RECORDING || L.n >= LOOP_MAX)
        return;
    loopbuf[L.n].t = L.pos;
    loopbuf[L.n].type = (uint8_t)type;
    loopbuf[L.n].a = (uint8_t)a;
    loopbuf[L.n].b = (uint8_t)b;
    L.n++;
}

static void loop_release_playing(void)
{
    int k;
    for (k = 0; k < 128; k++)
        if (bget(L.play_held, k)) {
            bset(L.play_held, k, 0);
            play_off(k);
        }
}

static void loop_end_recording(void)
{
    int k;
    L.len = L.pos > 0 ? L.pos : 1;
    for (k = 0; k < 128; k++)              /* keys still down: let go at the loop's end, in the loop */
        if (bget(L.rec_held, k) && L.n < LOOP_MAX) {
            loopbuf[L.n].t = L.len - 1;
            loopbuf[L.n].type = LE_OFF;
            loopbuf[L.n].a = (uint8_t)k;
            loopbuf[L.n].b = 0;
            L.n++;
        }
    for (k = 0; k < 16; k++)
        L.rec_held[k] = 0;
    L.state = LS_PLAYING;
    L.pos = 0;
    L.play_i = 0;
}

void seq_loop_rec(void)
{
    int k;
    switch (L.state) {
    case LS_RECORDING:
        loop_end_recording();
        break;
    case LS_ARMED:
        L.state = L.len ? LS_STOPPED : LS_IDLE;
        break;
    default:                               /* a new take: the old one goes */
        loop_release_playing();
        L.n = 0;
        L.len = 0;
        L.pos = 0;
        for (k = 0; k < 16; k++)
            L.rec_held[k] = 0;
        L.state = LS_ARMED;
        break;
    }
}

void seq_loop_play(void)
{
    switch (L.state) {
    case LS_RECORDING: loop_end_recording(); break;
    case LS_PLAYING: loop_release_playing(); L.state = LS_STOPPED; break;
    case LS_STOPPED: L.state = LS_PLAYING; L.pos = 0; L.play_i = 0; break;
    case LS_ARMED: L.state = L.len ? LS_STOPPED : LS_IDLE; break;
    default: break;
    }
}

void seq_loop_clear(void)
{
    loop_release_playing();
    L.state = LS_IDLE;
    L.n = L.len = L.pos = 0;
}

static void loop_play_event(const lev_t *e)
{
    switch (e->type) {
    case LE_ON:
        bset(L.play_held, e->a, 1);
        play_on(e->a, e->b);
        break;
    case LE_OFF:
        if (bget(L.play_held, e->a)) {
            bset(L.play_held, e->a, 0);
            play_off(e->a);
        }
        break;
    default:
        eng_ctl(e->a, e->b | (e->a == SQ_BEND ? (int)e->rsv << 7 : 0));
        break;
    }
}

/* ------------------------------------------------------------------------------------------ from outside --- */
void seq_note_on(int key, int vel)
{
    if (key < 0 || key > 127)
        return;
    if (L.state == LS_ARMED) {             /* the take starts with its first note */
        L.state = LS_RECORDING;
        L.pos = 0;
        L.n = 0;
    }
    if (L.state == LS_RECORDING) {
        loop_put(LE_ON, key, vel);
        bset(L.rec_held, key, 1);
    }
    if (bget(L.play_held, key)) {          /* the loop holds it too: the loop's lets go first */
        bset(L.play_held, key, 0);
        play_off(key);
    }
    play_on(key, vel);
}

void seq_note_off(int key)
{
    if (key < 0 || key > 127)
        return;
    if (L.state == LS_RECORDING && bget(L.rec_held, key)) {
        loop_put(LE_OFF, key, 0);
        bset(L.rec_held, key, 0);
    }
    play_off(key);
}

void seq_ctl(int type, int a)
{
    if (L.state == LS_RECORDING && L.n < LOOP_MAX) {
        loopbuf[L.n].t = L.pos;
        loopbuf[L.n].type = LE_CTL;
        loopbuf[L.n].a = (uint8_t)type;
        loopbuf[L.n].b = (uint8_t)(a & 127);
        loopbuf[L.n].rsv = (uint8_t)(a >> 7);
        L.n++;
    }
    eng_ctl(type, a);
}

void seq_clock(int what)
{
    if (what == 0xF8) {
        A.clock_seen = 0;
        A.ext = 1;
        if (A.type != AR_NONE && A.n && ++A.clocks >= 6u) {   /* a sixteenth every six pulses */
            A.clocks = 0;
            arp_step();
            A.t = 0.0f;
        }
    } else if (what == 0xFA) {
        A.clocks = 0;
        A.idx = 0;
    }
}

int seq_held_count(void) { return nphys; }

void seq_set(const mq_patch_t *p)
{
    int type = p->arp.type < NARPS ? p->arp.type : AR_NONE, k;
    A.bpm = 2.0f * (float)(p->arp.spd2 < 10 ? 10 : p->arp.spd2 > 200 ? 200 : p->arp.spd2);
    A.step = FS * 60.0f / (A.bpm * 4.0f);
    A.trig = p->arp.trig;
    if (A.hold && !p->arp.hold) {          /* latch let go: what is not down stops */
        A.hold = 0;
        A.n = 0;
        for (k = 0; k < 128; k++)
            if (phys[k]) {
                A.list[A.n] = (int8_t)k;
                A.lvel[A.n++] = phys[k];
                if (A.n == 16) break;
            }
        arp_build();
        if (!A.n) arp_stop_note();
    }
    A.hold = p->arp.hold;
    if (type != A.type) {
        int was = A.type;
        A.type = type;
        if (was != AR_NONE && type == AR_NONE) {   /* off: the keys down sound as keys again */
            arp_stop_note();
            A.n = 0;
            for (k = 0; k < 128; k++)
                if (phys[k])
                    eng_note_on(k, eng_vel(phys[k]), 0);
        } else if (was == AR_NONE) {       /* on: the keys down become its list */
            A.n = 0;
            for (k = 0; k < 128; k++)
                if (phys[k]) {
                    eng_note_off(k);
                    if (A.n < 16) {
                        A.list[A.n] = (int8_t)k;
                        A.lvel[A.n++] = phys[k];
                    }
                }
            A.idx = 0;
            A.t = 0.0f;
            A.sounding = -1;
        }
        arp_build();
    }
}

void seq_block(uint32_t n)
{
    /* the loop's events in this block */
    if (L.state == LS_RECORDING) {
        L.pos += n;
        if (L.pos > 0x7FFFFFFFu) loop_end_recording();
    } else if (L.state == LS_PLAYING && L.len) {
        uint32_t end = L.pos + n;
        while (1) {
            while (L.play_i < L.n && loopbuf[L.play_i].t < end && loopbuf[L.play_i].t < L.len)
                loop_play_event(&loopbuf[L.play_i++]);
            if (end < L.len)
                break;
            end -= L.len;                  /* round again */
            L.play_i = 0;
            loop_release_playing();
            if (end == 0)
                break;
            L.pos = 0;
            if (end > L.len) end = L.len;
        }
        L.pos = end;
    }
    /* the arpeggio */
    if (++A.clock_seen > 170u)             /* a second without MIDI clock: its own time again */
        A.ext = 0;
    if (A.type != AR_NONE && A.nseq) {
        float t = A.t + (float)n;
        if (A.gate_off && t >= 0.5f * A.step) {
            arp_stop_note();
        }
        if (!A.ext && t >= A.step) {
            t -= A.step;
            if (t > A.step) t = 0.0f;
            arp_step();
        }
        A.t = t;
    } else if (A.sounding >= 0 && A.type != AR_NONE) {
        arp_stop_note();
    }
}

void seq_meter(mq_meter_t *m)
{
    m->loop_state = (uint8_t)L.state;
    m->loop_events = L.n;
    m->loop_len_ms = (uint32_t)((float)L.len * (1000.0f / FS));
    m->loop_pos_ms = (uint32_t)((float)L.pos * (1000.0f / FS));
    m->arp_on = A.type != AR_NONE;
}

void seq_panic(void)
{
    int k;
    for (k = 0; k < 128; k++)
        phys[k] = 0;
    nphys = 0;
    for (k = 0; k < 16; k++)
        L.play_held[k] = L.rec_held[k] = 0;
    A.n = A.nseq = 0;
    A.sounding = -1;
    A.gate_off = 0;
}

void seq_init(void)
{
    memset(&L, 0, sizeof L);
    memset(&A, 0, sizeof A);
    A.sounding = -1;
    A.rng = 0xC0FFEEu;
    A.bpm = 120.0f;
    A.step = FS * 60.0f / (120.0f * 4.0f);
    nphys = 0;
    memset(phys, 0, sizeof phys);
}

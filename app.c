/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the app: the FM-1's panel and screen around the bitmask engine (dsp/).
 *
 * The MASK1's panel, on the FM-1's:
 *   HOME   the sound: its number and name, both masks drawn; K1 MASK 1, K2 MASK 2, K3 CUTF, K4 RESO
 *   SEL    OSC 1 -> OSC 2 -> NOISE -> FILTER (the four sections with an envelope)
 *   ENV    the envelope of the section shown (lit while it is): L A D S R LOOP, as the MASK1's Env button
 *   LFO    LFO 1 <-> LFO 2      EDIT  PITCH -> MOD -> GENERAL      FX  the effects      GLO  the part (GLOBAL)
 *   ARP    the arpeggiator      SEQ  the looper; REC records (from the first note), PLAY plays and stops
 *   SAVE   the WRITE page; SAVE again there does what its K4 says (WRITE, INIT, REVERT, SETUP)
 *   SELECT walks the rows of every page in turn; K1..K4 the row's four values; ALGORITHM the mask of the
 *   oscillator shown (OSC 1 elsewhere): the one number; PRESETS the sounds, P00..P39 then U000..U127.
 *   OCT- / OCT+ the FM-1's keys' octave; both: back to 0.
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include <stddef.h>
#include "fastmath.h"
#include "masque.h"
#include "presets.h"

#define SET_MAGIC 0x3151534Du            /* "MSQ1" */
#define SET_VERSION 1u
#define NUSER 128
#define BANK_N 32
#define NPROG (PRESET_COUNT + NUSER)     /* P00..P39, U000..U127 */

typedef struct {
    uint32_t magic;
    uint16_t version, size;
    mq_global_t glob;
    uint8_t midi_ch;                     /* 0 omni, 1..16 */
    int8_t octave;                       /* the FM-1's keys: -3..+3 */
    uint16_t prog;                       /* the sound last chosen */
    mq_patch_t edit;                     /* the sound as it was left, edits and all */
} settings_t;
static settings_t set;

typedef struct {
    uint32_t magic;
    uint16_t version, count;
    mq_patch_t p[BANK_N];
} bank_t;
#define BANK_MAGIC 0x4B42514Du           /* "MQBK" */
static bank_t banks[4];

static mq_patch_t cur;                   /* the sound being played and edited */
static mq_patch_t loaded;                /* as it was loaded: SAVE lights while they differ */
static uint8_t backup_hold;

/* ---------------------------------------------------------------------------------------- the engine --- */
static volatile uint32_t midi_count;
static volatile int pc_req = -1;         /* a program change came in: the main loop loads it */
static int cc_mapped(uint32_t cc);
#define CCQ 64u
static volatile uint16_t ccq[CCQ];       /* the sound's CCs (the map below), for the main loop: cc << 8 | value */
static volatile uint32_t ccq_w, ccq_r;
static void midi_in(void)                /* audio interrupt: what came in over USB and the TRS jack */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ch = st & 0x0Fu;
        if (st == 0xF8u || st == 0xFAu || st == 0xFCu) {
            masque_clock_isr((int)st);
            continue;
        }
        if (st < 0x80u || st >= 0xF0u)
            continue;
        if (set.midi_ch && ch != (uint32_t)(set.midi_ch - 1u))
            continue;
        midi_count++;
        switch (st & 0xF0u) {
        case 0x90u: masque_note_on_isr((int)d1, (int)d2); break;
        case 0x80u: masque_note_off_isr((int)d1); break;
        case 0xA0u: masque_pressure_isr((int)d1, (int)d2); break;
        case 0xB0u:
            if (cc_mapped(d1)) {         /* a value of the sound: the main loop's, as a knob's */
                if (ccq_w - ccq_r < CCQ)
                    ccq[ccq_w++ % CCQ] = (uint16_t)(d1 << 8 | d2);
            } else {
                masque_cc_isr((int)d1, (int)d2);
            }
            break;
        case 0xD0u: masque_chan_pressure_isr((int)d1); break;
        case 0xE0u: masque_bend_isr((int)(d1 | d2 << 7)); break;
        case 0xC0u: pc_req = (int)d1; break;   /* 0..39 the factory sounds, 40..127 the first user sounds */
        default: break;
        }
    }
}

static float out_l[APP_BLOCK], out_r[APP_BLOCK];
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float gain_now;
    float gain = (float)master_q12 / 4096.0f * 7000000.0f;
    uint32_t i;
    midi_in();
    masque_render(out_l, out_r, n);
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;   /* MASTER glides: no step at a block's edge */
        lr[2 * i] = (int32_t)(fm_tanhf(out_l[i] * 0.7f) * gain_now);
        lr[2 * i + 1] = (int32_t)(fm_tanhf(out_r[i] * 0.7f) * gain_now);
    }
}

void app_panic(void) { masque_panic(); }
void app_overload(void) { masque_shed(); }

/* --------------------------------------------------------------------------------------- the sounds --- */
static int user_empty(int u) { return banks[u / BANK_N].p[u % BANK_N].name[0] == 0; }

static void prog_patch(int prog, mq_patch_t *p)
{
    if (prog < PRESET_COUNT) {
        preset_get(prog, p);
    } else if (user_empty(prog - PRESET_COUNT)) {
        preset_init(p);
    } else {
        *p = banks[(prog - PRESET_COUNT) / BANK_N].p[(prog - PRESET_COUNT) % BANK_N];
    }
}

static int patch_same(const mq_patch_t *a, const mq_patch_t *b)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    uint32_t i;
    for (i = 0; i < sizeof *a; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

static void patch_clamp(mq_patch_t *p);

static void sound_changed(void)
{
    patch_clamp(&cur);
    masque_set_patch(&cur);
}

static void load_prog(int prog)
{
    set.prog = (uint16_t)prog;
    prog_patch(prog, &cur);
    loaded = cur;
    sound_changed();
}

static void banks_load(void)
{
    int b;
    for (b = 0; b < 4; b++) {
        if (plat_store_load(OBJ_BANK0 + (uint32_t)b, &banks[b], sizeof banks[b]) != (int)sizeof banks[b] ||
            banks[b].magic != BANK_MAGIC || banks[b].count != BANK_N) {
            memset(&banks[b], 0, sizeof banks[b]);
            banks[b].magic = BANK_MAGIC;
            banks[b].version = 1;
            banks[b].count = BANK_N;
        }
    }
}

/* -------------------------------------------------------------------------------------------- setup --- */
static void settings_defaults(void)
{
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.version = SET_VERSION;
    set.size = sizeof set;
    set.glob.a_hz10 = 4400;
    set.glob.vel_curve = 2;
    set.glob.at_curve = 2;
    set.glob.voices = MQ_DEFAULT_VOICES;
    set.prog = 0;
    preset_get(0, &set.edit);
}

static int settings_ok(void)
{
    return set.magic == SET_MAGIC && set.version == SET_VERSION && set.size == sizeof set &&
           set.glob.a_hz10 >= 4000 && set.glob.a_hz10 <= 4700 && set.glob.vel_curve <= 6 && set.glob.at_curve <= 5 &&
           set.glob.voices >= 4 && set.glob.voices <= MQ_MAX_VOICES && set.midi_ch <= 16 && set.octave >= -3 &&
           set.octave <= 3 && set.prog < NPROG;
}

void app_init(void)
{
    masque_init();
    banks_load();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok())
        settings_defaults();
    masque_set_global(&set.glob);
    prog_patch(set.prog, &loaded);
    cur = set.edit;                      /* the sound as it was left */
    sound_changed();
    plat_cpu2_set(1);                    /* half the voices, where the unit allows */
}

/* ------------------------------------------------------------------------------------------- text --- */
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0, neg = v < 0;
    uint32_t u = neg ? (uint32_t)-v : (uint32_t)v;
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void signed_text(int32_t v, char *b)
{
    if (v > 0)
        *b++ = '+';
    int_text(v, b);
}
static void prog_text(int prog, char *b)  /* P07, U012 */
{
    int u = prog - PRESET_COUNT;
    if (prog < PRESET_COUNT) {
        b[0] = 'P';
        b[1] = (char)('0' + prog / 10);
        b[2] = (char)('0' + prog % 10);
        b[3] = 0;
    } else {
        b[0] = 'U';
        b[1] = (char)('0' + u / 100);
        b[2] = (char)('0' + u / 10 % 10);
        b[3] = (char)('0' + u % 10);
        b[4] = 0;
    }
}
static void name_text(const mq_patch_t *p, char *b)
{
    int i, n = 0;
    for (i = 0; i < 10 && p->name[i]; i++)
        b[i] = p->name[i];
    b[i] = 0;
    for (n = i - 1; n >= 0 && b[n] == ' '; n--)
        b[n] = 0;
    if (!b[0])
        text_cat(b, "--");
}
static void prog_name(int prog, char *b)
{
    mq_patch_t p;
    if (prog >= PRESET_COUNT && user_empty(prog - PRESET_COUNT)) {
        b[0] = 0;
        text_cat(b, "(EMPTY)");
        return;
    }
    prog_patch(prog, &p);
    name_text(&p, b);
}

/* ----------------------------------------------------------------------------------- the parameters --- */
/* each value on the panel: where it lives, its range and how it reads */
enum { K_U8, K_S8, K_U16, K_NAMES, K_SPD2, K_FX1, K_FX2, K_GLOB, K_NONE };
typedef struct {
    const char *label;
    uint8_t kind, names;
    int16_t lo, hi;
    uint16_t off;                        /* offsetof in mq_patch_t (or mq_global_t / settings_t for K_GLOB) */
} pdesc_t;

#define PO(f) ((uint16_t)offsetof(mq_patch_t, f))
#define ENV_ROWS(e) {{{"L", K_S8, 0, 0, 99, PO(e.l)}, {"A", K_U8, 0, 0, 99, PO(e.a)}, {"D", K_U8, 0, 0, 99, PO(e.d)}, \
                     {"S", K_U8, 0, 0, 99, PO(e.s)}}, \
                    {{"R", K_U8, 0, 0, 99, PO(e.r)}, {"LOOP", K_U8, NAME_LOOP, 0, 1, PO(e.loop)}, {0, K_NONE, 0, 0, 0, 0}, \
                     {0, K_NONE, 0, 0, 0, 0}}}
#define NONE {0, K_NONE, 0, 0, 0, 0}

enum { SEC_HOME, SEC_OSC1, SEC_OSC2, SEC_NOISE, SEC_FILTER, SEC_PITCH, SEC_LFO1, SEC_LFO2, SEC_MOD, SEC_GENERAL,
       SEC_FX, SEC_ARP, SEC_LOOPER, SEC_GLOBAL, SEC_WRITE, NSEC };
static const char *const SEC_NAME[NSEC] = {"HOME", "OSC 1", "OSC 2", "NOISE", "FILTER", "PITCH", "LFO 1", "LFO 2",
                                          "MOD", "GENERAL", "FX", "ARP", "LOOPER", "GLOBAL", "WRITE"};

/* the rows: [section][row][knob]; the envelope rows separately */
#define MAXROWS 2
static const pdesc_t ROWS[NSEC][MAXROWS][4] = {
    [SEC_HOME] = {{{"MASK 1", K_U16, 0, 0, 511, PO(osc[0].mask)}, {"MASK 2", K_U16, 0, 0, 511, PO(osc[1].mask)},
                   {"CUTF", K_U8, 0, 0, 127, PO(filt.cutf)}, {"RESO", K_U8, 0, 0, 99, PO(filt.reso)}}},
    [SEC_OSC1] = {{{"MASK", K_U16, 0, 0, 511, PO(osc[0].mask)}, {"SEMI", K_S8, 0, -60, 60, PO(osc[0].semi)},
                   {"TUNE", K_S8, 0, -99, 99, PO(osc[0].tune)}, {"LEVEL", K_S8, 0, 0, 99, PO(osc[0].env.l)}},
                  {{"START", K_S8, 0, -99, 99, PO(osc[0].start)}, {"SPD", K_U8, 0, 0, 99, PO(osc[0].spd)},
                   {"LOOP", K_NAMES, NAME_LOOP, 0, 2, PO(osc[0].loop)}, NONE}},
    [SEC_OSC2] = {{{"MASK", K_U16, 0, 0, 511, PO(osc[1].mask)}, {"SEMI", K_S8, 0, -60, 60, PO(osc[1].semi)},
                   {"TUNE", K_S8, 0, -99, 99, PO(osc[1].tune)}, {"LEVEL", K_S8, 0, 0, 99, PO(osc[1].env.l)}},
                  {{"START", K_S8, 0, -99, 99, PO(osc[1].start)}, {"SPD", K_U8, 0, 0, 99, PO(osc[1].spd)},
                   {"LOOP", K_NAMES, NAME_LOOP, 0, 2, PO(osc[1].loop)}, NONE}},
    [SEC_NOISE] = {{{"FREQ", K_U8, 0, 0, 99, PO(noise.freq)}, {"TRACK", K_U8, 0, 0, 99, PO(noise.track)},
                    {"LEVEL", K_S8, 0, 0, 99, PO(noise.env.l)}, NONE}},
    [SEC_FILTER] = {{{"CUTF", K_U8, 0, 0, 127, PO(filt.cutf)}, {"RESO", K_U8, 0, 0, 99, PO(filt.reso)},
                     {"MODE", K_NAMES, NAME_FMODE, 0, NFMODES - 1, PO(filt.mode)},
                     {"TRK", K_U8, 0, 0, 99, PO(filt.trk)}},
                    {{"EXTRA", K_NAMES, NAME_EXTRA, 0, NEXTRAS - 1, PO(filt.extra)},
                     {"ENV", K_S8, 0, -99, 99, PO(filt.env.l)}, {"DRIVE", K_U8, 0, 0, 99, PO(filt.drive)}, NONE}},
    [SEC_PITCH] = {{{"START", K_S8, 0, -48, 48, PO(pitch.start)}, {"SPD", K_U8, 0, 0, 40, PO(pitch.spd)},
                    {"END", K_S8, 0, -48, 48, PO(pitch.end)}, NONE}},
    [SEC_LFO1] = {{{"AMT", K_U8, 0, 0, 99, PO(lfo[0].amt)}, {"SPD", K_U8, 0, 0, 99, PO(lfo[0].spd)},
                   {"DEST", K_NAMES, NAME_LDEST, 0, NLDESTS - 1, PO(lfo[0].dest)},
                   {"WAVE", K_NAMES, NAME_LWAVE, 0, NLWAVES - 1, PO(lfo[0].wave)}},
                  {{"DLAY", K_U8, 0, 0, 99, PO(lfo[0].dlay)}, {"DCAY", K_U8, 0, 0, 99, PO(lfo[0].dcay)}, NONE, NONE}},
    [SEC_LFO2] = {{{"AMT", K_U8, 0, 0, 99, PO(lfo[1].amt)}, {"SPD", K_U8, 0, 0, 99, PO(lfo[1].spd)},
                   {"DEST", K_NAMES, NAME_LDEST, 0, NLDESTS - 1, PO(lfo[1].dest)},
                   {"WAVE", K_NAMES, NAME_LWAVE, 0, NLWAVES - 1, PO(lfo[1].wave)}},
                  {{"DLAY", K_U8, 0, 0, 99, PO(lfo[1].dlay)}, {"DCAY", K_U8, 0, 0, 99, PO(lfo[1].dcay)}, NONE, NONE}},
    [SEC_MOD] = {{{"V-DEST", K_NAMES, NAME_VDEST, 0, NVDESTS - 1, PO(vel.dest)},
                  {"V-AMT", K_S8, 0, -99, 99, PO(vel.amt)},
                  {"M-DEST", K_NAMES, NAME_MDEST, 0, NMDESTS - 1, PO(wheel.dest)},
                  {"M-AMT", K_S8, 0, -99, 99, PO(wheel.amt)}},
                 {{"AT-DEST", K_NAMES, NAME_MDEST, 0, NMDESTS - 1, PO(at.dest)},
                  {"AT-AMT", K_S8, 0, -99, 99, PO(at.amt)}, NONE, NONE}},
    [SEC_GENERAL] = {{{"MODE", K_NAMES, NAME_PMODE, 0, NPMODES - 1, PO(gen.mode)},
                      {"VOL", K_U8, 0, 0, 99, PO(gen.vol)}, {"SEMI", K_S8, 0, -24, 24, PO(gen.semi)},
                      {"PORT", K_U8, 0, 0, 13, PO(gen.port)}},
                     {{"PANS", K_U8, 0, 0, 99, PO(gen.pans)}, {"RATE", K_U8, 0, 0, 99, PO(gen.rate)},
                      {"BEND", K_U8, 0, 0, 12, PO(gen.bend)}, NONE}},
    [SEC_FX] = {{{"1TYP", K_FX1, 0, 0, NF1 - 1, PO(fx.t1)}, {"1BAL", K_U8, 0, 0, 100, PO(fx.b1)},
                 {"2TYP", K_FX2, 0, 0, NF2 - 1, PO(fx.t2)}, {"2BAL", K_U8, 0, 0, 100, PO(fx.b2)}}},
    [SEC_ARP] = {{{"TYPE", K_NAMES, NAME_ARP, 0, NARPS - 1, PO(arp.type)}, {"SPD", K_SPD2, 0, 10, 200, PO(arp.spd2)},
                  {"HOLD", K_U8, NAME_LOOP, 0, 1, PO(arp.hold)}, {"TRIG", K_U8, NAME_LOOP, 0, 1, PO(arp.trig)}}},
};
/* the GLOBAL page's and the WRITE page's are the app's own */
enum { G_MIDICH, G_TUNE, G_VCURVE, G_ATCURVE, G_VOICES, G_OCTAVE };
static const pdesc_t GROWS[2][4] = {
    {{"MIDI CH", K_GLOB, G_MIDICH, 0, 16, 0}, {"A HZ", K_GLOB, G_TUNE, 4000, 4700, 0},
     {"VEL CRV", K_GLOB, G_VCURVE, 0, 6, 0}, {"AT CRV", K_GLOB, G_ATCURVE, 0, 5, 0}},
    {{"VOICES", K_GLOB, G_VOICES, 4, MQ_MAX_VOICES, 0}, {"OCTAVE", K_GLOB, G_OCTAVE, -3, 3, 0}, NONE, NONE}};
static const pdesc_t ENVROWS[4][2][4] = {ENV_ROWS(osc[0].env), ENV_ROWS(osc[1].env), ENV_ROWS(noise.env),
                                         ENV_ROWS(filt.env)};
static const char *const ACTIONS[4] = {"WRITE", "INIT", "REVERT", "SETUP"};

/* ------------------------------------------------------------------------------------------ the CC map --- */
/* every value of the sound on a MIDI CC (in on the part's channel, out over USB as a knob turns it). A CC's 0..127
 * covers the value's range evenly; MASK is 14-bit: its CC (20, 21) the top seven bits, CC 52 / 53 the two below */
#define CC_MASK1_LSB 52u
#define CC_MASK2_LSB 53u
typedef struct { uint8_t cc; pdesc_t d; } ccmap_t;
#define C(n, lab, kind, names, lo, hi, f) {n, {lab, kind, names, lo, hi, PO(f)}}
static const ccmap_t CCMAP[] = {
    C(20, "MASK 1", K_U16, 0, 0, 511, osc[0].mask), C(14, "SEMI 1", K_S8, 0, -60, 60, osc[0].semi),
    C(15, "TUNE 1", K_S8, 0, -99, 99, osc[0].tune), C(16, "START 1", K_S8, 0, -99, 99, osc[0].start),
    C(17, "SPD 1", K_U8, 0, 0, 99, osc[0].spd), C(18, "LOOP 1", K_NAMES, NAME_LOOP, 0, 2, osc[0].loop),
    C(19, "LEVEL 1", K_S8, 0, 0, 99, osc[0].env.l),
    C(21, "MASK 2", K_U16, 0, 0, 511, osc[1].mask), C(22, "SEMI 2", K_S8, 0, -60, 60, osc[1].semi),
    C(23, "TUNE 2", K_S8, 0, -99, 99, osc[1].tune), C(24, "START 2", K_S8, 0, -99, 99, osc[1].start),
    C(25, "SPD 2", K_U8, 0, 0, 99, osc[1].spd), C(26, "LOOP 2", K_NAMES, NAME_LOOP, 0, 2, osc[1].loop),
    C(27, "LEVEL 2", K_S8, 0, 0, 99, osc[1].env.l),
    C(34, "O1 A", K_U8, 0, 0, 99, osc[0].env.a), C(35, "O1 D", K_U8, 0, 0, 99, osc[0].env.d),
    C(36, "O1 S", K_U8, 0, 0, 99, osc[0].env.s), C(37, "O1 R", K_U8, 0, 0, 99, osc[0].env.r),
    C(116, "O1 LOOP", K_U8, NAME_LOOP, 0, 1, osc[0].env.loop),
    C(39, "O2 A", K_U8, 0, 0, 99, osc[1].env.a), C(40, "O2 D", K_U8, 0, 0, 99, osc[1].env.d),
    C(41, "O2 S", K_U8, 0, 0, 99, osc[1].env.s), C(42, "O2 R", K_U8, 0, 0, 99, osc[1].env.r),
    C(43, "O2 LOOP", K_U8, NAME_LOOP, 0, 1, osc[1].env.loop),
    C(44, "N A", K_U8, 0, 0, 99, noise.env.a), C(45, "N D", K_U8, 0, 0, 99, noise.env.d),
    C(46, "N S", K_U8, 0, 0, 99, noise.env.s), C(47, "N R", K_U8, 0, 0, 99, noise.env.r),
    C(48, "N LOOP", K_U8, NAME_LOOP, 0, 1, noise.env.loop),
    C(49, "F A", K_U8, 0, 0, 99, filt.env.a), C(50, "F D", K_U8, 0, 0, 99, filt.env.d),
    C(51, "F S", K_U8, 0, 0, 99, filt.env.s), C(114, "F R", K_U8, 0, 0, 99, filt.env.r),
    C(115, "F LOOP", K_U8, NAME_LOOP, 0, 1, filt.env.loop),
    C(74, "CUTF", K_U8, 0, 0, 127, filt.cutf), C(71, "RESO", K_U8, 0, 0, 99, filt.reso),
    C(28, "MODE", K_NAMES, NAME_FMODE, 0, NFMODES - 1, filt.mode), C(29, "TRK", K_U8, 0, 0, 99, filt.trk),
    C(30, "EXTRA", K_NAMES, NAME_EXTRA, 0, NEXTRAS - 1, filt.extra), C(31, "F ENV", K_S8, 0, -99, 99, filt.env.l),
    C(75, "DRIVE", K_U8, 0, 0, 99, filt.drive),
    C(76, "N FREQ", K_U8, 0, 0, 99, noise.freq), C(77, "N TRACK", K_U8, 0, 0, 99, noise.track),
    C(78, "N LEVEL", K_S8, 0, 0, 99, noise.env.l),
    C(79, "P START", K_S8, 0, -48, 48, pitch.start), C(80, "P SPD", K_U8, 0, 0, 40, pitch.spd),
    C(81, "P END", K_S8, 0, -48, 48, pitch.end),
    C(82, "L1 AMT", K_U8, 0, 0, 99, lfo[0].amt), C(83, "L1 SPD", K_U8, 0, 0, 99, lfo[0].spd),
    C(84, "L1 DEST", K_NAMES, NAME_LDEST, 0, NLDESTS - 1, lfo[0].dest),
    C(85, "L1 WAVE", K_NAMES, NAME_LWAVE, 0, NLWAVES - 1, lfo[0].wave),
    C(86, "L1 DLAY", K_U8, 0, 0, 99, lfo[0].dlay), C(87, "L1 DCAY", K_U8, 0, 0, 99, lfo[0].dcay),
    C(108, "L2 AMT", K_U8, 0, 0, 99, lfo[1].amt), C(109, "L2 SPD", K_U8, 0, 0, 99, lfo[1].spd),
    C(110, "L2 DEST", K_NAMES, NAME_LDEST, 0, NLDESTS - 1, lfo[1].dest),
    C(111, "L2 WAVE", K_NAMES, NAME_LWAVE, 0, NLWAVES - 1, lfo[1].wave),
    C(112, "L2 DLAY", K_U8, 0, 0, 99, lfo[1].dlay), C(113, "L2 DCAY", K_U8, 0, 0, 99, lfo[1].dcay),
    C(102, "V-DEST", K_NAMES, NAME_VDEST, 0, NVDESTS - 1, vel.dest), C(103, "V-AMT", K_S8, 0, -99, 99, vel.amt),
    C(104, "M-DEST", K_NAMES, NAME_MDEST, 0, NMDESTS - 1, wheel.dest), C(105, "M-AMT", K_S8, 0, -99, 99, wheel.amt),
    C(106, "AT-DEST", K_NAMES, NAME_MDEST, 0, NMDESTS - 1, at.dest), C(107, "AT-AMT", K_S8, 0, -99, 99, at.amt),
    C(3, "MODE", K_NAMES, NAME_PMODE, 0, NPMODES - 1, gen.mode), C(7, "VOL", K_U8, 0, 0, 99, gen.vol),
    C(12, "SEMI", K_S8, 0, -24, 24, gen.semi), C(5, "PORT", K_U8, 0, 0, 13, gen.port),
    C(8, "PANS", K_U8, 0, 0, 99, gen.pans), C(9, "RATE", K_U8, 0, 0, 99, gen.rate),
    C(13, "BEND", K_U8, 0, 0, 12, gen.bend),
    C(94, "1TYP", K_FX1, 0, 0, NF1 - 1, fx.t1), C(93, "1BAL", K_U8, 0, 0, 100, fx.b1),
    C(95, "2TYP", K_FX2, 0, 0, NF2 - 1, fx.t2), C(91, "2BAL", K_U8, 0, 0, 100, fx.b2),
    C(88, "ARP", K_NAMES, NAME_ARP, 0, NARPS - 1, arp.type), C(89, "ARP SPD", K_SPD2, 0, 10, 200, arp.spd2),
    C(90, "HOLD", K_U8, NAME_LOOP, 0, 1, arp.hold), C(92, "TRIG", K_U8, NAME_LOOP, 0, 1, arp.trig),
};
#undef C
#define NCCMAP ((int)(sizeof CCMAP / sizeof CCMAP[0]))

static int cc_mapped(uint32_t cc)            /* (the render's, from MIDI in) */
{
    int i;
    if (cc == CC_MASK1_LSB || cc == CC_MASK2_LSB)
        return 1;
    for (i = 0; i < NCCMAP; i++)
        if (CCMAP[i].cc == cc)
            return 1;
    return 0;
}

/* ---------------------------------------------------------------------------------------------- the UI --- */
static struct {
    int sec, row;
    int env;                             /* the envelope shown (OSC 1, OSC 2, NOISE, FILTER) */
    int last_osc;                        /* the oscillator ALGORITHM turns: 0 / 1 */
    int edit_sel, edit_lfo;              /* where SEL and LFO and EDIT went last */
    int w_slot, w_cur, w_act;            /* WRITE: the user slot, the name's cursor, the action */
    char msg[2][28];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t prs[NKEYS];
    int pending_prog;                    /* PRESETS turned: loaded when it rests a moment; -1 none */
    uint32_t prog_turned;
    float wave[2][120];                  /* the masks drawn */
    float wave_m[2];
    uint8_t redraw;
    uint32_t meter_t;
} ui;

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[64];
    for (i = 0; i < 27u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 27u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static int sec_rows(int s)
{
    if (s == SEC_GLOBAL || s == SEC_WRITE)
        return s == SEC_GLOBAL ? 2 : 1;
    if (s == SEC_LOOPER)
        return 1;
    if (ui.env && s >= SEC_OSC1 && s <= SEC_FILTER)
        return 2;
    return ROWS[s][1][0].label ? 2 : 1;
}

static const pdesc_t *row_desc(int s, int r)
{
    static const pdesc_t empty[4] = {NONE, NONE, NONE, NONE};
    static pdesc_t wr[4];
    if (s == SEC_GLOBAL)
        return GROWS[r];
    if (s == SEC_WRITE) {
        wr[0] = (pdesc_t){"SLOT", K_NONE, 0, 0, 0, 0};
        wr[1] = (pdesc_t){"CURSOR", K_NONE, 0, 0, 0, 0};
        wr[2] = (pdesc_t){"CHAR", K_NONE, 0, 0, 0, 0};
        wr[3] = (pdesc_t){"ACTION", K_NONE, 0, 0, 0, 0};
        return wr;
    }
    if (s == SEC_LOOPER)
        return empty;
    if (ui.env && s >= SEC_OSC1 && s <= SEC_FILTER)
        return ENVROWS[s - SEC_OSC1][r];
    return ROWS[s][r];
}

static int glob_get(int g)
{
    switch (g) {
    case G_MIDICH: return set.midi_ch;
    case G_TUNE: return set.glob.a_hz10;
    case G_VCURVE: return set.glob.vel_curve;
    case G_ATCURVE: return set.glob.at_curve;
    case G_VOICES: return set.glob.voices;
    default: return set.octave;
    }
}
static void glob_set(int g, int v)
{
    switch (g) {
    case G_MIDICH: set.midi_ch = (uint8_t)v; break;
    case G_TUNE: set.glob.a_hz10 = (uint16_t)v; break;
    case G_VCURVE: set.glob.vel_curve = (uint8_t)v; break;
    case G_ATCURVE: set.glob.at_curve = (uint8_t)v; break;
    case G_VOICES: set.glob.voices = (uint8_t)v; break;
    default: set.octave = (int8_t)v; break;
    }
    masque_set_global(&set.glob);
}

static int pget(const pdesc_t *d)
{
    const uint8_t *b = (const uint8_t *)&cur + d->off;
    switch (d->kind) {
    case K_S8: return *(const int8_t *)b;
    case K_U16: return *(const uint16_t *)(const void *)b;
    case K_GLOB: return glob_get(d->names);
    case K_NONE: return 0;
    default: return *b;
    }
}
static void pset(const pdesc_t *d, int v)
{
    uint8_t *b = (uint8_t *)&cur + d->off;
    v = clamp(v, d->lo, d->hi);
    switch (d->kind) {
    case K_S8: *(int8_t *)b = (int8_t)v; break;
    case K_U16: *(uint16_t *)(void *)b = (uint16_t)v; break;
    case K_GLOB: glob_set(d->names, v); return;
    case K_NONE: return;
    default: *b = (uint8_t)v; break;
    }
    sound_changed();
}

static uint8_t mask_msb[2], mask_lsb[2];
static void cc_apply(uint32_t cc, int v)     /* main loop: a CC of the map came in */
{
    int i;
    if (cc == CC_MASK1_LSB || cc == CC_MASK2_LSB) {
        int o = cc == CC_MASK2_LSB;
        mask_lsb[o] = (uint8_t)v;
        cur.osc[o].mask = (uint16_t)(mask_msb[o] * 4 + (v >> 5));
        sound_changed();
        ui.redraw = 1;
        return;
    }
    for (i = 0; i < NCCMAP; i++) {
        const pdesc_t *d = &CCMAP[i].d;
        if (CCMAP[i].cc != cc)
            continue;
        if (d->kind == K_U16) {              /* MASK: the top seven bits (the two below start again at 0) */
            int o = cc == 21u;
            mask_msb[o] = (uint8_t)v;
            mask_lsb[o] = 0;
            cur.osc[o].mask = (uint16_t)(v * 4);
            sound_changed();
        } else {
            int n = d->hi - d->lo + 1;
            pset(d, d->lo + v * n / 128);
        }
        ui.redraw = 1;
        return;
    }
}

/* a value turned on the panel goes out as its CC (USB), so a computer can record it */
static void cc_send_value(const pdesc_t *d)
{
    uint32_t ch = set.midi_ch ? set.midi_ch - 1u : 0u;
    int i;
    for (i = 0; i < NCCMAP; i++) {
        const pdesc_t *m = &CCMAP[i].d;
        uint32_t v;
        if (m->off != d->off || m->kind == K_GLOB || d->kind == K_GLOB || d->kind == K_NONE)
            continue;
        if (m->kind == K_U16) {
            int o = CCMAP[i].cc == 21u, mask = cur.osc[o].mask;
            plat_midi_send(0x0Bu | (0xB0u | ch) << 8 | (uint32_t)CCMAP[i].cc << 16 | (uint32_t)(mask >> 2) << 24, TO_USB);
            plat_midi_send(0x0Bu | (0xB0u | ch) << 8 | (o ? CC_MASK2_LSB : CC_MASK1_LSB) << 16 |
                           (uint32_t)((mask & 3) << 5) << 24, TO_USB);
            return;
        }
        {
            int n = m->hi - m->lo + 1, val = pget(m) - m->lo;
            v = (uint32_t)((val * 128 + n - 1) / n);
            if (v > 127u)
                v = 127u;
        }
        plat_midi_send(0x0Bu | (0xB0u | ch) << 8 | (uint32_t)CCMAP[i].cc << 16 | v << 24, TO_USB);
        return;
    }
}

/* every value kept in range: a sound loaded from a backup or another version never reaches the engine broken */
static void patch_clamp(mq_patch_t *p)
{
    mq_patch_t *keep = &cur;
    int s, r, k, e;
    if (p != &cur)
        return;
    for (s = 0; s < NSEC; s++)
        for (r = 0; r < MAXROWS; r++)
            for (k = 0; k < 4; k++) {
                const pdesc_t *d = &ROWS[s][r][k];
                if (d->label && d->kind != K_NONE && d->kind != K_GLOB) {
                    int v = pget(d);
                    if (v < d->lo || v > d->hi) {
                        uint8_t *b = (uint8_t *)keep + d->off;
                        v = clamp(v, d->lo, d->hi);
                        if (d->kind == K_S8) *(int8_t *)b = (int8_t)v;
                        else if (d->kind == K_U16) *(uint16_t *)(void *)b = (uint16_t)v;
                        else *b = (uint8_t)v;
                    }
                }
            }
    for (e = 0; e < 4; e++)
        for (r = 0; r < 2; r++)
            for (k = 0; k < 4; k++) {
                const pdesc_t *d = &ENVROWS[e][r][k];
                if (d->label) {
                    uint8_t *b = (uint8_t *)keep + d->off;
                    int lo = d->lo, v = d->kind == K_S8 ? *(int8_t *)b : *b;
                    if (e == 3 && k == 0 && r == 0)
                        lo = -99;          /* the filter's level is its amount, either way */
                    if (v < lo || v > d->hi)
                        v = clamp(v, lo, d->hi);
                    if (d->kind == K_S8) *(int8_t *)b = (int8_t)v;
                    else *b = (uint8_t)v;
                }
            }
}

static void value_text(const pdesc_t *d, int v, char *t)
{
    t[0] = 0;
    switch (d->kind) {
    case K_NAMES: text_cat(t, masque_name_of(d->names, v)); break;
    case K_FX1: masque_fx_name(1, v, t); break;
    case K_FX2: masque_fx_name(2, v, t); break;
    case K_SPD2: int_text(2 * v, t); break;
    case K_S8:
        if (d->lo < 0) signed_text(v, t);
        else int_text(v, t);
        break;
    case K_GLOB:
        if (d->names == G_MIDICH) {
            if (v) int_text(v, t);
            else text_cat(t, "OMNI");
        } else if (d->names == G_TUNE) {
            char u[4];
            int_text(v / 10, t);
            text_cat(t, ".");
            u[0] = (char)('0' + v % 10);
            u[1] = 0;
            text_cat(t, u);
        } else if (d->names == G_VCURVE) {
            text_cat(t, masque_name_of(NAME_VCURVE, v));
        } else if (d->names == G_ATCURVE) {
            int_text(v + 1, t);
        } else if (d->names == G_OCTAVE) {
            signed_text(v, t);
        } else {
            int_text(v, t);
        }
        break;
    default:
        if (d->names == NAME_LOOP && d->hi == 1)
            text_cat(t, v ? "ON" : "OFF");
        else
            int_text(v, t);
        break;
    }
}

static void go(int sec, int row, int speak)
{
    ui.sec = sec;
    ui.row = row < sec_rows(sec) ? row : 0;
    if (sec == SEC_OSC1 || sec == SEC_OSC2)
        ui.last_osc = sec - SEC_OSC1;
    if (sec == SEC_WRITE && set.prog >= PRESET_COUNT)
        ui.w_slot = set.prog - PRESET_COUNT;
    ui.redraw = 1;
    if (speak) {
        char t[24];
        t[0] = 0;
        text_cat(t, SEC_NAME[sec]);
        if (ui.env && sec >= SEC_OSC1 && sec <= SEC_FILTER)
            text_cat(t, " ENV");
        app_say(t, "");
    }
}

/* SELECT: every row of every page, in turn */
static void select_turn(int32_t e)
{
    int s = ui.sec, r = ui.row;
    while (e > 0) {
        if (++r >= sec_rows(s)) {
            r = 0;
            s = (s + 1) % NSEC;
        }
        e--;
    }
    while (e < 0) {
        if (--r < 0) {
            s = (s + NSEC - 1) % NSEC;
            r = sec_rows(s) - 1;
        }
        e++;
    }
    go(s, r, 0);
    {
        char t[8];
        int_text(r + 1, t);
        app_say(SEC_NAME[s], sec_rows(s) > 1 ? t : "");
    }
}

static void write_char(int32_t e)
{
    static const char CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+./#&";
    int n = (int)sizeof CH - 1, i, c = 0;
    for (i = 0; i < n; i++)
        if (CH[i] == cur.name[ui.w_cur])
            c = i;
    c = ((c + (int)e) % n + n) % n;
    cur.name[ui.w_cur] = CH[c];
    for (i = 0; i < 10; i++)
        if (!cur.name[i])
            cur.name[i] = ' ';
    masque_set_patch(&cur);
}

static void knob(int k, int32_t e)
{
    const pdesc_t *d;
    char t[24];
    if (ui.sec == SEC_WRITE) {
        if (k == 0)
            ui.w_slot = ((ui.w_slot + (int)e) % NUSER + NUSER) % NUSER;
        else if (k == 1)
            ui.w_cur = clamp(ui.w_cur + e, 0, 9);
        else if (k == 2)
            write_char(e);
        else
            ui.w_act = ((ui.w_act + (int)e) % 4 + 4) % 4;
        ui.redraw = 1;
        if (k == 3)
            app_say("ACTION", ACTIONS[ui.w_act]);
        else if (k == 0) {
            prog_text(PRESET_COUNT + ui.w_slot, t);
            app_say("SLOT", t);
        }
        return;
    }
    d = &row_desc(ui.sec, ui.row)[k];
    if (!d->label || d->kind == K_NONE)
        return;
    {
        int v = pget(d), lo = d->lo;
        if (ui.env && ui.sec == SEC_FILTER && ui.row == 0 && k == 0)
            lo = -99;                      /* the filter envelope's level: its amount, either way */
        if (d->kind == K_U16 && (e > 2 || e < -2))
            e *= 4;                        /* a quick turn of a mask goes four at a time */
        v = clamp(v + e, lo, d->hi);
        if (lo != d->lo) {                 /* (pset clamps to the descriptor's range) */
            int8_t *b = (int8_t *)((uint8_t *)&cur + d->off);
            *b = (int8_t)v;
            sound_changed();
        } else {
            pset(d, v);
        }
        value_text(d, pget(d), t);
        app_say(d->label, t);
        cc_send_value(d);
    }
}

/* ALGORITHM: the mask of the oscillator shown */
static void mask_turn(int32_t e)
{
    int o = ui.sec == SEC_OSC2 ? 1 : ui.sec == SEC_OSC1 ? 0 : ui.last_osc;
    char t[12];
    if (e > 2 || e < -2)
        e *= 4;
    cur.osc[o].mask = (uint16_t)clamp((int32_t)cur.osc[o].mask + e, 0, 511);
    sound_changed();
    int_text(cur.osc[o].mask, t);
    app_say(o ? "MASK 2" : "MASK 1", t);
    cc_send_value(&CCMAP[o ? 7 : 0].d);
}

static void do_write(void)
{
    char t[24];
    int r = 0;
    if (backup_hold) {
        app_say("NOT SAVED", "backup running");
        return;
    }
    switch (ui.w_act) {
    case 0: {                            /* WRITE: the sound into the user slot */
        int b = ui.w_slot / BANK_N;
        banks[b].p[ui.w_slot % BANK_N] = cur;
        if (!cur.name[0])
            cur.name[0] = ' ';
        r = plat_store_save(OBJ_BANK0 + (uint32_t)b, &banks[b], sizeof banks[b]);
        if (r == 0) {
            set.prog = (uint16_t)(PRESET_COUNT + ui.w_slot);
            set.edit = cur;
            loaded = cur;
            plat_store_save(OBJ_SETTINGS, &set, sizeof set);
            prog_text(set.prog, t);
            app_say("WRITTEN", t);
        }
        break;
    }
    case 1:                              /* INIT: a sine, nothing else */
        preset_init(&cur);
        sound_changed();
        app_say("INIT", "a new sound");
        return;
    case 2:                              /* REVERT: the sound as it was loaded */
        cur = loaded;
        sound_changed();
        app_say("REVERT", "as loaded");
        return;
    default:                             /* SETUP: for the next power-on */
        set.edit = cur;
        r = plat_store_save(OBJ_SETTINGS, &set, sizeof set);
        if (r == 0)
            app_say("SAVED", "for power-on");
        break;
    }
    if (r)
        app_say("NOT SAVED", r == -9 ? "flash unknown" : "store full");
}

static int fm1_note(uint32_t k) { return 53 + (int)k + 12 * set.octave; }   /* key 0 is F3 */

static void octave_set(int o)
{
    char t[8];
    int k;
    for (k = 0; k < NKEYS; k++)          /* keys held across the change let go first */
        if (ui.keys >> k & 1u)
            masque_note_off(fm1_note((uint32_t)k));
    ui.keys = 0;
    set.octave = (int8_t)clamp(o, -3, 3);
    signed_text(set.octave, t);
    app_say("OCTAVE", t);
}

static void button(uint32_t k, uint32_t btn)
{
    switch (k) {
    case B_HOME: go(SEC_HOME, 0, 1); break;
    case B_SEL: {                        /* OSC 1 -> OSC 2 -> NOISE -> FILTER */
        int s = ui.sec >= SEC_OSC1 && ui.sec <= SEC_FILTER ? ui.sec + 1 : SEC_OSC1 + ui.edit_sel;
        if (s > SEC_FILTER) s = SEC_OSC1;
        ui.edit_sel = s - SEC_OSC1;
        go(s, 0, 1);
        break;
    }
    case B_ENV:
        ui.env = !ui.env;
        if (ui.sec < SEC_OSC1 || ui.sec > SEC_FILTER)
            ui.sec = SEC_OSC1 + ui.edit_sel;
        go(ui.sec, 0, 1);
        break;
    case B_LFO: go(ui.sec == SEC_LFO1 ? SEC_LFO2 : ui.sec == SEC_LFO2 ? SEC_LFO1 : SEC_LFO1 + ui.edit_lfo, 0, 1);
        ui.edit_lfo = ui.sec - SEC_LFO1;
        break;
    case B_EDIT: go(ui.sec == SEC_PITCH ? SEC_MOD : ui.sec == SEC_MOD ? SEC_GENERAL : SEC_PITCH, 0, 1); break;
    case B_FX: go(SEC_FX, 0, 1); break;
    case B_GLO: go(SEC_GLOBAL, 0, 1); break;
    case B_ARP:
        if (ui.sec == SEC_ARP) {         /* on the ARP page: ARP latches (HOLD) */
            cur.arp.hold = !cur.arp.hold;
            sound_changed();
            app_say("HOLD", cur.arp.hold ? "ON" : "OFF");
        } else {
            go(SEC_ARP, 0, 1);
        }
        break;
    case B_SEQ: go(SEC_LOOPER, 0, 1); break;
    case B_REC: masque_loop_rec(); break;
    case B_PLAY: masque_loop_play(); break;
    case B_SAVE:
        if (ui.sec == SEC_WRITE)
            do_write();
        else
            go(SEC_WRITE, 0, 1);
        break;
    case B_OCTDN:
    case B_OCTUP:
        if (btn >> (k == B_OCTDN ? B_OCTUP : B_OCTDN) & 1u)
            octave_set(0);
        else
            octave_set(set.octave + (k == B_OCTDN ? -1 : 1));
        break;
    default:
        break;
    }
}

static void prog_turn(int32_t e)
{
    int p = ui.pending_prog >= 0 ? ui.pending_prog : set.prog;
    char t[24], n[16];
    p = ((p + (int)e) % NPROG + NPROG) % NPROG;
    ui.pending_prog = p;
    ui.prog_turned = plat_ms();
    prog_text(p, t);
    prog_name(p, n);
    app_say(t, n);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, now = plat_ms();
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int note = fm1_note(k);
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t v = touch_vel(k);
            ui.prs[k] = 0;
            masque_note_on(note, (int)v);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)note << 16 | v << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            masque_note_off(note);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)note << 16, TO_USB);
        } else if ((keys & bit) && touch_pressure(k) != ui.prs[k]) {   /* pressure: the voice's aftertouch */
            ui.prs[k] = touch_pressure(k);
            masque_pressure(note, ui.prs[k]);
            plat_midi_send(0x0Au | 0xA0u << 8 | (uint32_t)note << 16 | (uint32_t)ui.prs[k] << 24, TO_USB);
        }
    }
    for (k = 0; k < NB; k++)
        if ((btn >> k & 1u) && !(ui.btn >> k & 1u))
            button(k, btn);
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        if (k == EN_SELECT)
            select_turn(e);
        else if (k == EN_PRESET)
            prog_turn(e);
        else if (k == EN_ALGO)
            mask_turn(e);
        else
            knob((int)(k - EN_K1), e);
    }
    while (ccq_r != ccq_w) {
        uint16_t c = ccq[ccq_r % CCQ];
        ccq_r++;
        cc_apply(c >> 8, c & 0x7F);
    }
    if (pc_req >= 0) {
        int p = pc_req;
        char t[8];
        pc_req = -1;
        load_prog(p < NPROG ? p : NPROG - 1);
        prog_text(set.prog, t);
        app_say("PROGRAM", t);
    }
    if (ui.pending_prog >= 0 && now - ui.prog_turned >= 250u) {   /* the sound, once PRESETS rests */
        load_prog(ui.pending_prog);
        ui.pending_prog = -1;
        ui.redraw = 1;
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    {   /* the lights: where we are, ARP while it plays, the looper, SAVE while the sound is edited */
        const mq_meter_t *m = masque_meter();
        uint32_t lit = 0, kl = 0, b;
        int s = ui.sec;
        if (s >= SEC_OSC1 && s <= SEC_FILTER) lit |= 1u << B_SEL;
        if (ui.env) lit |= 1u << B_ENV;
        if (s == SEC_LFO1 || s == SEC_LFO2) lit |= 1u << B_LFO;
        if (s == SEC_PITCH || s == SEC_MOD || s == SEC_GENERAL) lit |= 1u << B_EDIT;
        if (s == SEC_FX) lit |= 1u << B_FX;
        if (s == SEC_GLOBAL) lit |= 1u << B_GLO;
        if (s == SEC_HOME) lit |= 1u << B_HOME;
        if (s == SEC_LOOPER) lit |= 1u << B_SEQ;
        if (m->arp_on || s == SEC_ARP) lit |= 1u << B_ARP;
        if (!patch_same(&cur, &loaded) || s == SEC_WRITE) lit |= 1u << B_SAVE;
        if (m->loop_state == LS_RECORDING || (m->loop_state == LS_ARMED && (now / 250u) & 1u)) lit |= 1u << B_REC;
        if (m->loop_state == LS_PLAYING) lit |= LED_PLAY_GREEN;
        for (b = 0; b < NKEYS; b++) {
            int n = fm1_note(b);
            if (n >= 0 && n < 128 && (m->sounding[n >> 5] >> (n & 31) & 1u))
                kl |= 1u << b;
        }
        plat_glow(1);
        plat_leds(lit, kl);
    }
}

void app_input_only(void) { input(); }

/* -------------------------------------------------------------------------------------------- drawing --- */
#define C_BG RGB(8, 6, 8)
#define C_RED RGB(255, 52, 36)                /* the MASK1's display */
#define C_REDD RGB(120, 22, 16)
#define C_REDDD RGB(48, 12, 10)
#define C_LBL RGB(150, 140, 140)
#define C_DIMT RGB(80, 72, 72)
#define C_WHT RGB(240, 234, 230)
#define C_HL RGB(34, 20, 18)

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static void wave_update(int o, float m)
{
    if (ui.wave_m[o] != m) {
        ui.wave_m[o] = m;
        masque_mask_wave(m, ui.wave[o], 120);
    }
}

/* a mask drawn: one cycle, its sections ticked under it */
static void draw_wave(int32_t x, int32_t y, int32_t w, int32_t h, int o, float m, uint16_t c)
{
    int32_t i, px = 0, py = 0;
    wave_update(o, m);
    cv_rect(x, y + h / 2, w, 1, C_REDDD);
    for (i = 0; i <= 32; i += 4)
        cv_rect(x + i * (w - 1) / 32, y + h - 2, 1, 2, C_REDD);
    for (i = 0; i < w; i++) {
        float v = ui.wave[o][i * 120 / w];
        int32_t yy = y + h / 2 - (int32_t)(v * (float)(h / 2 - 3));
        if (i && (yy - py > 1 || py - yy > 1))
            cv_line(px, py, x + i, yy, c);
        else
            cv_pset(x + i, yy, c);
        px = x + i;
        py = yy;
    }
}

static void draw_env(const mq_env_t *e, int bip)
{
    int32_t x0 = 12, y0 = 52, h = 60, x, xa, xd, xs, xr, yt, ys;
    float l = (float)(e->l < 0 ? -e->l : e->l) / 99.0f;
    if (!bip) l = 1.0f;
    xa = x0 + 4 + e->a * 50 / 99;
    xd = xa + 4 + e->d * 50 / 99;
    xs = xd + 40;
    xr = xs + 4 + e->r * 50 / 99;
    yt = y0 + h - (int32_t)(l * (float)h);
    ys = y0 + h - (int32_t)(l * (float)h * (float)e->s / 99.0f);
    cv_rect(x0, y0 + h, 216, 1, C_REDDD);
    cv_line(x0, y0 + h, xa, yt, C_RED);
    cv_line(xa, yt, xd, ys, C_RED);
    cv_line(xd, ys, xs, ys, C_RED);
    cv_line(xs, ys, xr, y0 + h, C_RED);
    if (e->loop) {                       /* the loop: attack and decay again */
        for (x = xa; x < xd; x += 4)
            cv_pset(x, y0 + 2, C_REDD);
        cv_text(xd + 4, y0 - 4, &FONT_XS, "LOOP", C_REDD);
    }
    if (bip && e->l < 0)
        cv_text(x0 + 150, y0 + 4, &FONT_XS, "INVERTED", C_REDD);
}

static void draw_filter(void)            /* the response, roughly: the cutoff and the resonance's peak */
{
    int32_t i, py = 0;
    float fc = (float)cur.filt.cutf, r = (float)cur.filt.reso / 99.0f;
    for (i = 0; i < 216; i++) {
        float n = (float)i * 127.0f / 215.0f, d = (n - fc) / 12.0f, g;   /* octaves from the cutoff */
        float pk = r * 3.0f / (1.0f + 16.0f * d * d);
        switch (cur.filt.mode) {
        case FM_LP: g = d < 0 ? 1.0f : 1.0f / (1.0f + d * d * (cur.filt.extra == FX_24DB ? 8.0f : 4.0f)); break;
        case FM_HP: g = d > 0 ? 1.0f : 1.0f / (1.0f + d * d * (cur.filt.extra == FX_24DB ? 8.0f : 4.0f)); break;
        case FM_BP: g = 1.0f / (1.0f + d * d * 4.0f); break;
        case FM_NOTCH: g = 1.0f - 1.0f / (1.0f + d * d * 30.0f * (1.1f - r)); pk = 0; break;
        case FM_PEAK: g = 1.0f; break;
        default: {   /* the comb: teeth at the cutoff's harmonics (between them for COMB-) */
            float h = fm_exp2f(d), ph = h - (float)(int)h, c = cur.filt.mode == FM_COMBN ? ph - 0.5f : ph < 0.5f ? ph : ph - 1.0f;
            g = 0.25f + 1.6f * r / (1.0f + 60.0f * c * c) * (h > 0.6f ? 1.0f : 0.0f) / (1.0f + 0.15f * h);
            pk = 0;
            break;
        }
        }
        g += pk;
        {
            int32_t y = 112 - (int32_t)(fm_minf(g, 2.4f) * 24.0f);
            if (i)
                cv_line(12 + i - 1, py, 12 + i, y, C_RED);
            py = y;
        }
    }
    cv_rect(12 + (int32_t)(fc * 215.0f / 127.0f), 50, 1, 64, C_REDD);
}

static void draw_lfo(int i)
{
    int32_t x, py = 0;
    float hold = 0.0f;
    for (x = 0; x < 216; x++) {
        float ph = (float)(x % 72) / 72.0f, v;
        switch (cur.lfo[i].wave) {
        case LW_SIN: v = fm_sinf(FM_TWO_PI * ph); break;
        case LW_SQR: v = ph < 0.5f ? 1.0f : -1.0f; break;
        case LW_UP: v = 2.0f * ph - 1.0f; break;
        case LW_DOWN: v = 1.0f - 2.0f * ph; break;
        case LW_SAWS: { float y = ph * 2.0f; y -= (float)(int)y; v = ph < 0.5f ? y : y - 1.0f; break; }
        case LW_STEP: v = (float)(int)(ph * 4.0f) * (2.0f / 3.0f) - 1.0f; break;
        case LW_HSIN: v = 2.0f * fm_fabsf(fm_sinf(FM_PI * ph)) - 1.0f; break;
        default: if (x % 72 == 0) hold = (float)((x * 7919 + 13) % 200 - 100) / 100.0f; v = hold; break;
        }
        {
            int32_t y = 82 - (int32_t)(v * 26.0f);
            if (x)
                cv_line(12 + x - 1, py, 12 + x, y, C_RED);
            py = y;
        }
    }
}

static void draw_cells(void)
{
    int r, k, rows = sec_rows(ui.sec);
    for (r = 0; r < 2; r++) {
        const pdesc_t *d = r < rows ? row_desc(ui.sec, r) : 0;
        int32_t y = 124 + r * 58, act = r == ui.row;
        if (act)
            cv_rect(0, y - 2, 240, 56, C_HL);
        for (k = 0; k < 4; k++) {
            char t[24];
            int32_t cx = 30 + k * 60;
            if (!d || !d[k].label)
                continue;
            if (ui.sec == SEC_WRITE) {
                t[0] = 0;
                if (k == 0) prog_text(PRESET_COUNT + ui.w_slot, t);
                else if (k == 1) int_text(ui.w_cur + 1, t);
                else if (k == 2) { t[0] = cur.name[ui.w_cur] == ' ' ? '_' : cur.name[ui.w_cur]; t[1] = 0; }
                else text_cat(t, ACTIONS[ui.w_act]);
            } else {
                int v = pget(&d[k]);
                if (ui.env && ui.sec == SEC_FILTER && r == 0 && k == 0)
                    signed_text(cur.filt.env.l, t);
                else
                    value_text(&d[k], v, t);
            }
            text_c(cx, y + 2, &FONT_XS, d[k].label, act ? C_LBL : C_DIMT);
            text_c(cx, y + 20, text_w(&FONT_M, t) > 58 ? &FONT_S : &FONT_M, t, act ? C_RED : C_REDD);
            if (act)
                text_c(cx, y + 46, &FONT_T, k == 0 ? "K1" : k == 1 ? "K2" : k == 2 ? "K3" : "K4", C_DIMT);
        }
    }
}

static void draw_header(void)
{
    char t[24], n[16];
    const mq_meter_t *m = masque_meter();
    uint32_t now = plat_ms();
    int p = ui.pending_prog >= 0 ? ui.pending_prog : set.prog;
    cv_rect(0, 0, 240, 26, RGB(20, 10, 10));
    prog_text(p, t);
    cv_text(8, 4, &FONT_B, t, C_RED);
    if (ui.pending_prog >= 0)
        prog_name(p, n);
    else
        name_text(&cur, n);
    cv_text(52, 4, &FONT_B, n, C_WHT);
    if (!patch_same(&cur, &loaded) && ui.pending_prog < 0)
        cv_text(52 + text_w(&FONT_B, n) + 4, 4, &FONT_B, "*", C_RED);
    {   /* the voices sounding, as dots; the looper's state */
        uint32_t i;
        for (i = 0; i < m->allowed && i < MQ_MAX_VOICES; i++)
            cv_rect(232 - (int32_t)i * 5, 10, 3, 6, i < m->voices ? C_RED : C_REDDD);
    }
    if (now < ui.msg_until) {            /* a message, over the section's title */
        int32_t x;
        cv_rect(0, 26, 240, 20, C_BG);
        x = cv_text(8, 28, &FONT_B, ui.msg[0], C_WHT);
        cv_text(x + 8, 28, &FONT_B, ui.msg[1], C_RED);
    } else {
        int32_t x;
        t[0] = 0;
        text_cat(t, SEC_NAME[ui.sec]);
        if (ui.env && ui.sec >= SEC_OSC1 && ui.sec <= SEC_FILTER)
            text_cat(t, " ENV");
        x = cv_text(8, 28, &FONT_B, t, C_LBL);
        (void)x;
        if (m->loop_state == LS_RECORDING || m->loop_state == LS_ARMED)
            cv_text(180, 28, &FONT_XS, m->loop_state == LS_ARMED ? "REC.." : "REC", C_RED);
        else if (m->loop_state == LS_PLAYING)
            cv_text(180, 28, &FONT_XS, "LOOP", C_RED);
        if (m->arp_on)
            cv_text(212, 28, &FONT_XS, "ARP", C_RED);
    }
}

static void draw_visual(void)
{
    const mq_meter_t *m = masque_meter();
    char t[24];
    switch (ui.sec) {
    case SEC_HOME: {
        int o;
        for (o = 0; o < 2; o++) {        /* both masks, where they are now when a key sounds */
            float mv = m->voices ? m->mask_now[o] : (float)cur.osc[o].mask;
            int32_t x = 8 + o * 116;
            draw_wave(x, 52, 108, 56, o, mv, o ? C_REDD + 0 : C_RED);
            t[0] = 0;
            int_text((int32_t)mv, t);
            cv_text(x, 104, &FONT_T, o ? "MASK 2" : "MASK 1", C_DIMT);
            cv_text(x + 108 - text_w(&FONT_XS, t), 102, &FONT_XS, t, C_RED);
        }
        break;
    }
    case SEC_OSC1:
    case SEC_OSC2:
    case SEC_NOISE:
    case SEC_FILTER:
        if (ui.env) {
            const mq_env_t *e = ui.sec == SEC_OSC1 ? &cur.osc[0].env : ui.sec == SEC_OSC2 ? &cur.osc[1].env :
                                ui.sec == SEC_NOISE ? &cur.noise.env : &cur.filt.env;
            draw_env(e, ui.sec == SEC_FILTER);
        } else if (ui.sec == SEC_FILTER) {
            draw_filter();
        } else if (ui.sec == SEC_NOISE) {
            int32_t x;
            uint32_t seed = 0x1234u, hold = 1u + (99u - cur.noise.freq) / 3u;
            int32_t y = 80;
            for (x = 0; x < 216; x++) {
                if ((uint32_t)x % hold == 0) {
                    seed = seed * 1103515245u + 12345u;
                    y = 80 + (int32_t)((seed >> 16) % 50u) - 25;
                }
                cv_pset(12 + x, y, C_RED);
            }
        } else {
            int o = ui.sec - SEC_OSC1;
            float mv = m->voices ? m->mask_now[o] : (float)cur.osc[o].mask;
            draw_wave(12, 50, 216, 62, o, mv, C_RED);
            if (cur.osc[o].spd && cur.osc[o].start) {   /* the mask envelope's way */
                char u[24];
                t[0] = 0;
                int_text(clamp(cur.osc[o].mask + cur.osc[o].start, 0, 511), t);
                text_cat(t, " > ");
                int_text(cur.osc[o].mask, u);
                text_cat(t, u);
                cv_text(150, 50, &FONT_XS, t, C_REDD);
            }
            if (m->voices) {
                int_text((int32_t)mv, t);
                cv_text(12, 50, &FONT_XS, t, C_REDD);
            }
        }
        break;
    case SEC_LFO1:
    case SEC_LFO2:
        draw_lfo(ui.sec - SEC_LFO1);
        break;
    case SEC_FX: {
        char a[8], b[8];
        masque_fx_name(1, cur.fx.t1, a);
        masque_fx_name(2, cur.fx.t2, b);
        text_c(66, 60, &FONT_L, a, cur.fx.t1 ? C_RED : C_REDDD);
        text_c(174, 60, &FONT_L, b, cur.fx.t2 ? C_RED : C_REDDD);
        cv_text(48, 96, &FONT_XS, "FX1", C_DIMT);
        cv_text(156, 96, &FONT_XS, "FX2", C_DIMT);
        cv_rect(120, 54, 1, 52, C_REDDD);
        break;
    }
    case SEC_ARP:
        text_c(120, 56, &FONT_L, masque_name_of(NAME_ARP, cur.arp.type), cur.arp.type ? C_RED : C_REDDD);
        int_text(2 * cur.arp.spd2, t);
        text_cat(t, " BPM");
        text_c(120, 94, &FONT_XS, t, C_REDD);
        break;
    case SEC_LOOPER: {
        static const char *const ST[5] = {"EMPTY", "WAITING FOR A NOTE", "RECORDING", "PLAYING", "STOPPED"};
        text_c(120, 54, &FONT_M, ST[m->loop_state < 5 ? m->loop_state : 0], C_RED);
        if (m->loop_len_ms) {
            int32_t w = (int32_t)(m->loop_pos_ms * 200u / (m->loop_len_ms ? m->loop_len_ms : 1u));
            cv_rect(20, 88, 200, 6, C_REDDD);
            if (m->loop_state == LS_PLAYING)
                cv_rect(20, 88, w > 200 ? 200 : w, 6, C_RED);
        }
        {
            char u[24];
            t[0] = 0;
            int_text((int32_t)m->loop_events, t);
            text_cat(t, " EVENTS");
            if (m->loop_len_ms) {
                text_cat(t, ", ");
                int_text((int32_t)(m->loop_len_ms / 1000u), u);
                text_cat(t, u);
                text_cat(t, ".");
                int_text((int32_t)(m->loop_len_ms / 100u % 10u), u);
                text_cat(t, u);
                text_cat(t, " S");
            }
            text_c(120, 100, &FONT_XS, t, C_REDD);
        }
        break;
    }
    case SEC_GLOBAL: {
        int32_t pct = (int32_t)(m->load * 100.0f + 0.5f);
        t[0] = 0;
        text_cat(t, "LOAD ");
        {
            char u[8];
            int_text(pct, u);
            text_cat(t, u);
        }
        text_cat(t, m->cpu2 ? " %  TWO CORES" : " %  ONE CORE");
        text_c(120, 60, &FONT_S, t, C_REDD);
        {
            char u[8];
            t[0] = 0;
            text_cat(t, "VOICES ");
            int_text((int32_t)m->allowed, u);
            text_cat(t, u);
            text_cat(t, "  PEAK ");
            int_text((int32_t)m->voices_peak, u);
            text_cat(t, u);
            text_c(120, 84, &FONT_S, t, C_REDD);
        }
        break;
    }
    case SEC_WRITE: {
        char n[16];
        prog_text(PRESET_COUNT + ui.w_slot, t);
        text_cat(t, " NOW: ");
        prog_name(PRESET_COUNT + ui.w_slot, n);
        text_cat(t, n);
        text_c(120, 52, &FONT_XS, t, C_REDD);
        {   /* the name, its cursor under it */
            int i;
            for (i = 0; i < 10; i++) {
                char c[2] = {cur.name[i] ? cur.name[i] : ' ', 0};
                int32_t x = 30 + i * 18;
                cv_text(x + 9 - text_w(&FONT_M, c) / 2, 72, &FONT_M, c, C_RED);
                cv_rect(x + 2, 100, 14, i == ui.w_cur ? 3 : 1, i == ui.w_cur ? C_RED : C_REDDD);
            }
        }
        text_c(120, 106, &FONT_T, "SAVE: DO THE ACTION", C_DIMT);
        break;
    }
    default: {   /* PITCH, MOD, GENERAL */
        if (ui.sec == SEC_GENERAL) {
            text_c(120, 60, &FONT_L, masque_name_of(NAME_PMODE, cur.gen.mode), C_RED);
        } else if (ui.sec == SEC_PITCH) {   /* the pitch envelope: from START to 0, to END on release */
            int32_t y0 = 82, ys = y0 - cur.pitch.start / 2, ye = y0 - cur.pitch.end / 2;
            int32_t xs = 40 + (40 - (cur.pitch.spd ? cur.pitch.spd : 40)) * 2;
            cv_rect(12, y0, 216, 1, C_REDDD);
            cv_line(12, ys, xs, y0, C_RED);
            cv_line(xs, y0, 150, y0, C_RED);
            cv_line(150, y0, 150 + (xs - 12), ye, C_RED);
        } else {
            text_c(120, 54, &FONT_XS, "VELOCITY, MOD WHEEL (CC 1)", C_DIMT);
            text_c(120, 70, &FONT_XS, "AND AFTERTOUCH: THE KEYS' PRESSURE", C_DIMT);
        }
        break;
    }
    }
}

static void draw(void)
{
    uint32_t half;
    for (half = 0; half < 2u; half++) {  /* the screen in two halves of 120 rows */
        cv_begin(240, 120, C_BG);
        cv_oy = -(int32_t)(120u * half);
        cv_y0 = 0;
        cv_y1 = 120;
        draw_header();
        draw_visual();
        draw_cells();
        cv_blit(0, 120u * half);
    }
}

/* a screen reader that has just started listening (or asks) hears the sound and the page */
static void speak(void)
{
    if (ann_on() && ann_where()) {
        char t[64], u[16];
        t[0] = 0;
        prog_text(set.prog, u);
        text_cat(t, u);
        text_cat(t, " ");
        name_text(&cur, u);
        text_cat(t, u);
        text_cat(t, ". ");
        text_cat(t, SEC_NAME[ui.sec]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

void app_ui_init(void)
{
    touch_on(1);
    ui.pending_prog = -1;
    ui.wave_m[0] = ui.wave_m[1] = -1.0f;
    lcd_fill(0, 0, 240, 240, C_BG);
    ui.redraw = 1;
}

void app_frame(void)
{
    const mq_meter_t *m = masque_meter();
    static uint32_t last_voices;
    static float last_mask[2];
    uint32_t now = plat_ms();
    input();
    speak();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (now - ui.meter_t >= 40u) {       /* the live masks and the voices, 25 a second */
        ui.meter_t = now;
        if (m->voices != last_voices || (m->voices && (m->mask_now[0] != last_mask[0] ||
                                                       m->mask_now[1] != last_mask[1])))
            ui.redraw = 1;
        if (ui.sec == SEC_LOOPER || ui.sec == SEC_GLOBAL || m->loop_state == LS_ARMED)
            ui.redraw = 1;
        last_voices = m->voices;
        last_mask[0] = m->mask_now[0];
        last_mask[1] = m->mask_now[1];
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    const mq_meter_t *m = masque_meter();
    if (!strcmp(what, "sec"))
        *got = ui.sec;
    else if (!strcmp(what, "row"))
        *got = ui.row;
    else if (!strcmp(what, "env"))
        *got = ui.env;
    else if (!strcmp(what, "prog"))
        *got = set.prog;
    else if (!strcmp(what, "mask1"))
        *got = cur.osc[0].mask;
    else if (!strcmp(what, "mask2"))
        *got = cur.osc[1].mask;
    else if (!strcmp(what, "cutf"))
        *got = cur.filt.cutf;
    else if (!strcmp(what, "drive"))
        *got = cur.filt.drive;
    else if (!strcmp(what, "fmode"))
        *got = cur.filt.mode;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "voices"))
        *got = (int)m->voices;
    else if (!strcmp(what, "loop"))
        *got = m->loop_state;
    else if (!strcmp(what, "arp"))
        *got = cur.arp.type;
    else if (!strcmp(what, "edited"))
        *got = !patch_same(&cur, &loaded);
    else if (!strcmp(what, "user0"))
        *got = !user_empty(0);
    else
        return 0;
    return 1;
}
#endif

/* backup and restore over USB (fm1-base device/backup.c): the setup and the user sounds */
int app_backup_playing(void) { return 0; }            /* a restore swaps sounds: fine while playing */
void app_backup_flush(void) {}                          /* everything is saved as it is written */
void app_backup_hold(int on) { backup_hold = (uint8_t)on; }
void app_backup_restored(void)
{
    banks_load();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok())
        settings_defaults();
    masque_set_global(&set.glob);
    prog_patch(set.prog, &loaded);
    cur = set.edit;
    sound_changed();
    ui.redraw = 1;
}

/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — MIDI and MPE in: what each channel message means, as MIDI 1.0 and MPE (MMA/AMEI RP-053 v1.0, "the spec")
 * say, after OPUS XIV's MpeInput (github.com/78Hz/pipe, src/engine/MpeInput.hpp), whose rules these are:
 *
 * The part's MIDI setting: OMNI (every channel, none of them MPE), one channel (1..16, the others ignored), or MPE.
 * In MPE the zones (§2.1) are the Lower Zone, its Master Channel 1 and Member Channels 2..(1+n), and the Upper Zone,
 * Master 16, Members (15-n)..15. Before any MPE Configuration Message (MCM) the power-on layout of §2.1.3: the Lower
 * Zone, Members 2..16. Channels in no zone are ordinary MIDI 1.0 channels.
 *
 *   Member Channel   Channel Pressure  -> its notes' PRESSURE (§2.5)
 *                    CC74              -> its notes' TIMBRE (§2.6)
 *                    Pitch Bend        -> its notes' pitch, x the Members' range (§2.4)
 *   Master Channel   Pitch Bend        -> added to every note of the zone, x the Master's range
 *                    Channel Pressure  -> AFTERTOUCH of every note of the zone
 *                    CC74              -> TIMBRE of the zone's notes whose own channel sends none
 *                    notes             -> played, under the Master's messages
 *   Other channels   Channel Pressure  -> AFTERTOUCH of that channel's notes; Pitch Bend, CC74 theirs (bend: the
 *                                         sound's BEND range unless the channel sets its own)
 *   Any channel      Poly Pressure     -> AFTERTOUCH of that key's note (the FM-1's own keys' pressure the same)
 *
 * Values are kept per channel whether a note plays or not, so what comes before a Note On starts the note (§3.3).
 * Pitch Bend Sensitivity (RPN 0) on a Member Channel sets all the zone's Members (§2.4); an MCM (RPN 6 on channel 1 or
 * 16) sets the zone, the Members to +-48 and the Masters to +-2 (§2.4), and stops the notes of the channels it moves
 * and resets them (§2.1.4); the newest zone takes precedence where they would overlap (§2.1.1). MPE+ (CC87): seven more
 * bits for the next Channel Pressure or CC74 on its channel. A source a controller never sends does not exist: the
 * matrix's rows that read it leave the sound alone (else a controller without pressure would play nothing). */
#include "engine.h"

static struct {
    int mode;                              /* 0 omni, 1..16, MQ_MIDI_MPE */
    int lower, upper;                      /* the zones' Member Channels (0: no zone) */
    struct { uint8_t rpn_msb, rpn_lsb; int16_t lsb; } c[16];
} M;

int mpe_zone_of(int ch)
{
    if (ch < 0 || ch > 15)
        return -1;
    if (M.upper > 0 && (ch == 15 || (ch >= 15 - M.upper && ch <= 14)))
        return 1;
    if (M.lower > 0 && ch <= M.lower)
        return 0;
    return -1;
}

static int member_with(int c, int lo, int up) { return (lo > 0 && c >= 1 && c <= lo) || (up > 0 && c >= 15 - up && c <= 14); }
int mpe_is_member(int ch) { return ch >= 0 && ch < 16 && member_with(ch, M.lower, M.upper); }
static int zone_with(int c, int lo, int up)
{
    if (up > 0 && (c == 15 || (c >= 15 - up && c <= 14)))
        return 1;
    if (lo > 0 && c <= lo)
        return 0;
    return -1;
}

static void layout(int lo, int up)
{
    int k;
    M.lower = lo;
    M.upper = up;
    for (k = 0; k < 16; k++)               /* Members +-48; Masters +-2; the rest keep the sound's BEND */
        eng_expr(k, EX_RANGE, member_with(k, lo, up) ? 4800 : (zone_with(k, lo, up) >= 0 ? 200 : 0));
}

void mpe_set_mode(int midi)
{
    int k;
    M.mode = midi;
    for (k = 0; k < 16; k++) {
        M.c[k].rpn_msb = M.c[k].rpn_lsb = 127;
        M.c[k].lsb = -1;
        eng_expr(k, EX_RESET, 0);
    }
    if (midi == MQ_MIDI_MPE)
        layout(15, 0);                     /* §2.1.3 */
    else
        layout(0, 0);
}

void mpe_init(void) { mpe_set_mode(0); }

static int with_lsb(int c, int msb)        /* 0..16383 */
{
    int v = msb << 7;
    if (M.c[c].lsb >= 0) {
        v |= M.c[c].lsb;
        M.c[c].lsb = -1;
    } else {
        v |= msb;                          /* (127 reaches the top) */
    }
    return v;
}

static void mcm(int c, int n)
{
    int lo = M.lower, up = M.upper, k, changed = 0;
    if (c != 0 && c != 15)
        return;
    n = n < 0 ? 0 : n > 15 ? 15 : n;
    if (c == 0) {
        lo = n;
        if (lo + up > 14) up = 14 - lo < 0 ? 0 : 14 - lo;
    } else {
        up = n;
        if (lo + up > 14) lo = 14 - up < 0 ? 0 : 14 - up;
    }
    for (k = 0; k < 16; k++)
        if (zone_with(k, M.lower, M.upper) != zone_with(k, lo, up) ||
            member_with(k, M.lower, M.upper) != member_with(k, lo, up) || k == c)
            changed |= 1 << k;
    for (k = 0; k < 16; k++)
        if (changed & (1 << k))
            eng_expr(k, EX_RESET, 0);      /* (the voices on it stop too: eng_expr's reset) */
    layout(lo, up);
}

void mpe_midi(int status, int d1, int d2)
{
    int st = status & 0xF0, c = status & 0x0F, mpe = M.mode == MQ_MIDI_MPE;
    if (M.mode >= 1 && M.mode <= 16 && c != M.mode - 1)
        return;
    d1 &= 127;
    d2 &= 127;
    switch (st) {
    case 0x90:
        if (d2)
            seq_note_on(d1, d2, c);
        else
            seq_note_off(d1, 64, c);
        break;
    case 0x80:
        seq_note_off(d1, d2, c);
        break;
    case 0xA0:
        eng_poly_pressure(d1, c, d2);
        break;
    case 0xD0:
        seq_expr(c, mpe && mpe_is_member(c) ? EX_PRESS : EX_AT, with_lsb(c, d1));
        break;
    case 0xE0:
        M.c[c].lsb = -1;
        seq_expr(c, EX_BEND, (d1 | d2 << 7) - 8192);
        break;
    case 0xB0:
        switch (d1) {
        case 74: seq_expr(c, EX_TIMBRE, with_lsb(c, d2)); break;
        case 87: M.c[c].lsb = (int16_t)d2; break;
        case 101: M.c[c].rpn_msb = (uint8_t)d2; break;
        case 100: M.c[c].rpn_lsb = (uint8_t)d2; break;
        case 99: case 98: M.c[c].rpn_msb = M.c[c].rpn_lsb = 127; break;   /* an NRPN: its data is not ours */
        case 6:
            if (M.c[c].rpn_msb == 0 && M.c[c].rpn_lsb == 0) {       /* Pitch Bend Sensitivity, semitones */
                int k;
                if (mpe_is_member(c)) {
                    for (k = 0; k < 16; k++)
                        if (mpe_is_member(k) && mpe_zone_of(k) == mpe_zone_of(c))
                            eng_expr(k, EX_RANGE, d2 * 100);
                } else {
                    eng_expr(c, EX_RANGE, d2 * 100);
                }
            } else if (mpe && M.c[c].rpn_msb == 0 && M.c[c].rpn_lsb == 6) {
                mcm(c, d2);
            }
            break;
        case 121:                          /* reset all controllers */
            eng_expr(c, EX_BEND, 0);
            M.c[c].rpn_msb = M.c[c].rpn_lsb = 127;
            M.c[c].lsb = -1;
            break;
        case 1: seq_ctl(SQ_WHEEL, d2); break;
        case 64: seq_ctl(SQ_SUSTAIN, d2); break;
        case 120: case 123: eng_all_off(); seq_panic(); break;
        default: break;
        }
        eng_ctl(SQ_CC0 + d1, d2);          /* every CC, for the matrix's CC sources */
        break;
    default:
        break;
    }
}

void mpe_meter(mq_meter_t *m)
{
    m->mpe_lower = (uint8_t)M.lower;
    m->mpe_upper = (uint8_t)M.upper;
}

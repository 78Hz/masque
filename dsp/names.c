/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the values as the panel shows them (the MASK1's own words, where it has them). */
#include "masque.h"
#include "engine.h"

static const char *const N_LOOP[NLOOPS] = {"NO", "YES", "BIDI"};
static const char *const N_FMODE[NFMODES] = {"LP", "HP", "BP", "NOTCH", "PEAK", "COMB+", "COMB-"};
static const char *const N_EXTRA[NEXTRAS] = {"12DB", "24DB", "LP1", "LP2", "LP3", "LP4", "LP5", "LP6", "LP7",
                                             "HP1", "HP2", "HP3", "HP4", "HP5", "HP6", "HP7"};
static const char *const N_PMODE[NPMODES] = {"POLY", "MONO", "SLUR", "MONO PORTA", "PARA", "MOLY", "PARA SLUR",
                                             "MONO RETRIG", "POLY RETRIG"};
static const char *const N_LWAVE[NLWAVES] = {"SIN", "SQR", "UP", "DOWN", "SAWS", "STEP", "HSIN", "RAND"};
static const char *const N_LDEST[NLDESTS] = {"FREQ", "VOL", "FILT", "MASK", "PAN", "O1 FREQ", "O1 VOL", "O1 MASK",
                                             "O2 FREQ", "O2 VOL", "O2 MASK", "N FREQ", "N VOL", "FX1 MOD",
                                             "FX2 MOD", "FX2 MIX"};
static const char *const N_ARP[NARPS] = {"NONE", "1OCT", "DBL", "2OCT", "ALT", "3OCT", "PONG", "PON2", "PON3", "RND",
                                         "RND2", "RND3", "MIN", "MAJ", "SUS2", "SUS4", "7TH", "MIN2", "MAJ2", "SU22",
                                         "SU42", "7TH2", "1O", "2O", "3O", "5TH", "5TH2"};
static const char *const N_VCURVE[7] = {"1", "2", "3", "4", "5", "6", "MAX"};
static const char *const N_XSRC[XS_CC0] = {"OFF", "VEL", "REL VEL", "PRESS", "AT", "TIMBRE", "BEND", "WHEEL", "KEY",
                                           "LFO1", "LFO2", "F ENV", "RAND"};
static const char *const N_XDEST[NXDEST] = {"OFF", "PITCH", "O1 PITCH", "O2 PITCH", "MASK", "O1 MASK", "O2 MASK",
                                            "MASK SPD", "VOL", "O1 VOL", "O2 VOL", "N VOL", "N FREQ", "CUTF", "RESO",
                                            "DRIVE", "F ENV", "P ENV", "ATK", "REL", "PAN", "LFO1 AMT", "LFO1 SPD",
                                            "LFO2 AMT", "LFO2 SPD", "FX1 MIX", "FX2 MIX", "FX1 MOD",
                                            "FX2 MOD"};
static const char *const N_XCURVE[NXCURVE] = {"LIN", "SOFT", "HARD", "S", "BIP"};
static const char *const N_XZONE[NXZONE] = {"ALL", "LO", "HI"};
static const char *const N_TS[NTS] = {"EQUAL", "WERCKM. III", "MEANTONE", "KIRNB. III", "VALLOTTI"};

const char *masque_name_of(int kind, int v)
{
    switch (kind) {
    case NAME_LOOP: return v >= 0 && v < NLOOPS ? N_LOOP[v] : "?";
    case NAME_FMODE: return v >= 0 && v < NFMODES ? N_FMODE[v] : "?";
    case NAME_EXTRA: return v >= 0 && v < NEXTRAS ? N_EXTRA[v] : "?";
    case NAME_PMODE: return v >= 0 && v < NPMODES ? N_PMODE[v] : "?";
    case NAME_LWAVE: return v >= 0 && v < NLWAVES ? N_LWAVE[v] : "?";
    case NAME_LDEST: return v >= 0 && v < NLDESTS ? N_LDEST[v] : "?";
    case NAME_XSRC: {
        static char cc[8];
        if (v >= 0 && v < XS_CC0) return N_XSRC[v];
        if (v >= XS_CC0 && v < NXSRC) {   /* CC n */
            int n = v - XS_CC0, i = 3;
            cc[0] = 'C'; cc[1] = 'C'; cc[2] = ' ';
            if (n >= 100) cc[i++] = (char)('0' + n / 100);
            if (n >= 10) cc[i++] = (char)('0' + n / 10 % 10);
            cc[i++] = (char)('0' + n % 10);
            cc[i] = 0;
            return cc;
        }
        return "?";
    }
    case NAME_XDEST: return v >= 0 && v < NXDEST ? N_XDEST[v] : "?";
    case NAME_XCURVE: return v >= 0 && v < NXCURVE ? N_XCURVE[v] : "?";
    case NAME_XZONE: return v >= 0 && v < NXZONE ? N_XZONE[v] : "?";
    case NAME_TS: return v >= 0 && v < NTS ? N_TS[v] : "?";
    case NAME_MIDI: {
        static char ch[4];
        if (v == 0) return "OMNI";
        if (v == MQ_MIDI_MPE) return "MPE";
        if (v < 1 || v > 16) return "?";
        ch[0] = (char)(v >= 10 ? '1' : '0' + v);
        ch[1] = v >= 10 ? (char)('0' + v - 10) : 0;
        ch[2] = 0;
        return ch;
    }
    case NAME_ARP: return v >= 0 && v < NARPS ? N_ARP[v] : "?";
    case NAME_VCURVE: return v >= 0 && v < 7 ? N_VCURVE[v] : "?";
    default: return "?";
    }
}

void masque_tuning_cents(int system, int root, float out[12]) { tuning_cents(system, root, out); }


/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the factory sounds, P00..P39: each the INIT sound and what it changes. */
#include "presets.h"

static void E(mq_env_t *e, int l, int a, int d, int s, int r, int loop)
{
    e->l = (int8_t)l;
    e->a = (uint8_t)a;
    e->d = (uint8_t)d;
    e->s = (uint8_t)s;
    e->r = (uint8_t)r;
    e->loop = (uint8_t)loop;
}
static void O(mq_patch_t *p, int i, int mask, int semi, int tune)
{
    p->osc[i].mask = (uint16_t)mask;
    p->osc[i].semi = (int8_t)semi;
    p->osc[i].tune = (int8_t)tune;
}
static void ME(mq_patch_t *p, int i, int start, int spd, int loop)   /* the mask envelope */
{
    p->osc[i].start = (int8_t)start;
    p->osc[i].spd = (uint8_t)spd;
    p->osc[i].loop = (uint8_t)loop;
}
static void F(mq_patch_t *p, int cutf, int reso, int mode, int trk, int extra)
{
    p->filt.cutf = (uint8_t)cutf;
    p->filt.reso = (uint8_t)reso;
    p->filt.mode = (uint8_t)mode;
    p->filt.trk = (uint8_t)trk;
    p->filt.extra = (uint8_t)extra;
}
static void LFO(mq_patch_t *p, int i, int amt, int spd, int dest, int wave, int dlay, int dcay)
{
    p->lfo[i].amt = (uint8_t)amt;
    p->lfo[i].spd = (uint8_t)spd;
    p->lfo[i].dest = (uint8_t)dest;
    p->lfo[i].wave = (uint8_t)wave;
    p->lfo[i].dlay = (uint8_t)dlay;
    p->lfo[i].dcay = (uint8_t)dcay;
}
static void FXS(mq_patch_t *p, int t1, int b1, int t2, int b2)
{
    p->fx.t1 = (uint8_t)t1;
    p->fx.b1 = (uint8_t)b1;
    p->fx.t2 = (uint8_t)t2;
    p->fx.b2 = (uint8_t)b2;
}
static void NAME(mq_patch_t *p, const char *s)
{
    int i;
    for (i = 0; i < 10; i++)
        p->name[i] = ' ';
    for (i = 0; i < 10 && s[i]; i++)
        p->name[i] = s[i];
}

void preset_init(mq_patch_t *p)
{
    int i;
    char *b = (char *)p;
    for (i = 0; i < (int)sizeof *p; i++)
        b[i] = 0;
    NAME(p, "INIT");
    O(p, 0, 31, 0, 0);
    O(p, 1, 31, 0, 0);
    E(&p->osc[0].env, 80, 0, 40, 70, 25, 0);
    E(&p->osc[1].env, 0, 0, 40, 70, 25, 0);
    p->noise.freq = 99;
    E(&p->noise.env, 0, 0, 30, 0, 20, 0);
    F(p, 127, 0, FM_LP, 0, FX_12DB);
    E(&p->filt.env, 0, 0, 40, 0, 30, 0);
    LFO(p, 0, 0, 50, LD_FREQ, LW_SIN, 0, 0);
    LFO(p, 1, 0, 30, LD_FILT, LW_SIN, 0, 0);
    p->vel.dest = VD_VOL;
    p->vel.amt = 40;
    p->wheel.dest = MD_L1AMT;
    p->wheel.amt = 60;
    p->at.dest = MD_FLT;
    p->at.amt = 30;
    p->gen.mode = PM_POLY;
    p->gen.vol = 80;
    p->gen.pans = 20;
    p->gen.bend = 2;
    FXS(p, F1_OFF, 30, F2_OFF, 25);
    p->arp.type = AR_NONE;
    p->arp.spd2 = 60;
    p->arp.trig = 1;
}

const int PRESET_COUNT = 40;

void preset_get(int n, mq_patch_t *p)
{
    preset_init(p);
    switch (n) {
    case 0:   /* the mask envelope, bouncing: a pad that never settles */
        NAME(p, "MASQUE");
        O(p, 0, 124, 0, -4); ME(p, 0, -60, 22, LOOP_BIDI);
        O(p, 1, 47, -12, 5); ME(p, 1, 40, 14, LOOP_BIDI);
        E(&p->osc[0].env, 70, 45, 60, 80, 62, 0);
        E(&p->osc[1].env, 62, 50, 60, 85, 64, 0);
        F(p, 86, 22, FM_LP, 30, FX_12DB); E(&p->filt.env, 18, 50, 70, 40, 60, 0);
        LFO(p, 0, 22, 18, LD_MASK, LW_SIN, 40, 0);
        FXS(p, F1_CH + 2, 45, F2_RV + 10, 40);
        break;
    case 1:   /* 29 and 30: bells from just off the sine */
        NAME(p, "GLASS BELL");
        O(p, 0, 29, 0, 0); O(p, 1, 30, 12, 4);
        E(&p->osc[0].env, 82, 0, 74, 0, 70, 0);
        E(&p->osc[1].env, 55, 0, 62, 0, 60, 0);
        p->vel.dest = VD_MASK; p->vel.amt = 18;
        FXS(p, F1_CH + 1, 25, F2_RV + 11, 42);
        break;
    case 2:   /* the manual's own example: 29, short */
        NAME(p, "MARIMBA");
        O(p, 0, 29, 0, 0); O(p, 1, 15, -12, 0);
        E(&p->osc[0].env, 86, 0, 48, 0, 44, 0);
        E(&p->osc[1].env, 40, 0, 34, 0, 30, 0);
        p->vel.dest = VD_VOL; p->vel.amt = 70;
        FXS(p, F1_OFF, 0, F2_RV + 4, 25);
        break;
    case 3:   /* 24: the square of small saws, filtered, one voice */
        NAME(p, "SAW SQUARE");
        O(p, 0, 24, -12, 0); O(p, 1, 23, -12, 7);
        E(&p->osc[0].env, 80, 0, 40, 85, 12, 0);
        E(&p->osc[1].env, 60, 0, 40, 85, 12, 0);
        F(p, 46, 38, FM_LP, 20, FX_12DB); E(&p->filt.env, 44, 0, 40, 12, 20, 0);
        p->gen.mode = PM_MONO_PORTA; p->gen.port = 4;
        p->vel.dest = VD_FEG; p->vel.amt = 50;
        break;
    case 4:
        NAME(p, "VAPOUR");
        O(p, 0, 19, 0, -6); O(p, 1, 19, 0, 7);
        E(&p->osc[0].env, 76, 0, 56, 0, 46, 0);
        E(&p->osc[1].env, 76, 0, 56, 0, 46, 0);
        F(p, 42, 26, FM_LP, 40, FX_12DB); E(&p->filt.env, 56, 0, 42, 0, 40, 0);
        p->gen.pans = 50;
        FXS(p, F1_CH + 3, 35, F2_DL + 5, 22);
        break;
    case 5:   /* 78: the buzz at the sine's pitch */
        NAME(p, "BUZZ LEAD");
        O(p, 0, 78, 0, 0); O(p, 1, 79, -12, 3);
        E(&p->osc[0].env, 80, 2, 50, 80, 22, 0);
        E(&p->osc[1].env, 55, 2, 50, 80, 22, 0);
        F(p, 92, 20, FM_LP, 50, FX_12DB); E(&p->filt.env, 14, 0, 40, 0, 30, 0);
        LFO(p, 0, 14, 52, LD_FREQ, LW_SIN, 48, 0);
        p->gen.mode = PM_MONO; p->gen.port = 3;
        FXS(p, F1_DT + 2, 30, F2_DL + 7, 28);
        break;
    case 6:   /* 56: the pulse */
        NAME(p, "PULSE PAD");
        O(p, 0, 56, 0, -5); O(p, 1, 57, 0, 6);
        E(&p->osc[0].env, 70, 52, 60, 80, 60, 0);
        E(&p->osc[1].env, 62, 56, 60, 80, 60, 0);
        F(p, 70, 18, FM_LP, 30, FX_12DB); E(&p->filt.env, 20, 55, 60, 30, 60, 0);
        LFO(p, 1, 20, 22, LD_FILT, LW_SIN, 0, 0);
        FXS(p, F1_CH + 4, 50, F2_RV + 12, 38);
        break;
    case 7:   /* the mask envelope looping up, a saw of masks */
        NAME(p, "MASK SWEEP");
        O(p, 0, 190, 0, 0); ME(p, 0, -90, 34, LOOP_YES);
        O(p, 1, 31, -12, 0);
        E(&p->osc[0].env, 72, 10, 50, 80, 40, 0);
        E(&p->osc[1].env, 50, 10, 50, 80, 40, 0);
        F(p, 96, 30, FM_LP, 30, FX_12DB);
        FXS(p, F1_OFF, 0, F2_DL + 8, 30);
        break;
    case 8:
        NAME(p, "SUPER SQ");
        O(p, 0, 40, 0, -9); O(p, 1, 40, 0, 9);
        E(&p->osc[0].env, 74, 0, 50, 80, 30, 0);
        E(&p->osc[1].env, 74, 0, 50, 80, 30, 0);
        F(p, 60, 15, FM_LP, 40, FX_12DB); E(&p->filt.env, 30, 0, 50, 30, 30, 0);
        p->vel.dest = VD_FLT; p->vel.amt = 70;
        p->gen.pans = 60;
        FXS(p, F1_CH + 5, 40, F2_RV + 6, 25);
        break;
    case 9:   /* the sine, its octave half-wave and a phaser that turns */
        NAME(p, "DRAWBARS");
        O(p, 0, 31, 0, 0); O(p, 1, 63, 0, 0);
        E(&p->osc[0].env, 80, 0, 0, 99, 8, 0);
        E(&p->osc[1].env, 58, 0, 0, 99, 8, 0);
        p->vel.amt = 0;
        p->wheel.dest = MD_FX1BAL; p->wheel.amt = 80;
        FXS(p, F1_CH + 15, 40, F2_RV + 3, 18);
        break;
    case 10:
        NAME(p, "TINE KEYS");
        O(p, 0, 31, 0, 0); O(p, 1, 29, 12, 0);
        E(&p->osc[0].env, 80, 0, 72, 22, 42, 0);
        E(&p->osc[1].env, 46, 0, 40, 0, 36, 0);
        p->vel.dest = VD_VOL; p->vel.amt = 65;
        p->gen.rate = 40;
        FXS(p, F1_RM + 1, 30, F2_RV + 5, 22);
        break;
    case 11:  /* 8, arpeggiated */
        NAME(p, "SQUARE ARP");
        O(p, 0, 8, -12, 0); O(p, 1, 20, 0, 0);
        E(&p->osc[0].env, 88, 0, 50, 10, 40, 0);
        E(&p->osc[1].env, 50, 0, 46, 0, 40, 0);
        F(p, 84, 30, FM_LP, 50, FX_12DB); E(&p->filt.env, 20, 0, 40, 0, 30, 0);
        p->arp.type = AR_1OCT; p->arp.spd2 = 66;
        FXS(p, F1_OFF, 0, F2_DL + 6, 30);
        break;
    case 12:  /* REPEAT: three turns a piece, formants */
        NAME(p, "FORMANT");
        O(p, 0, 287, 0, 0); O(p, 1, 303, 0, 4);
        E(&p->osc[0].env, 72, 20, 50, 80, 40, 0);
        E(&p->osc[1].env, 60, 20, 50, 80, 40, 0);
        F(p, 84, 40, FM_BP, 60, FX_12DB);
        LFO(p, 0, 16, 20, LD_MASK, LW_SIN, 0, 0);
        p->at.dest = MD_MASK; p->at.amt = 40;
        FXS(p, F1_CH + 2, 30, F2_RV + 8, 30);
        break;
    case 13:
        NAME(p, "VOX");
        O(p, 0, 319, 0, -3); O(p, 1, 351, 0, 3);
        E(&p->osc[0].env, 68, 42, 60, 85, 50, 0);
        E(&p->osc[1].env, 58, 46, 60, 85, 50, 0);
        F(p, 80, 30, FM_LP, 40, FX_12DB);
        LFO(p, 0, 12, 48, LD_FREQ, LW_SIN, 50, 0);
        LFO(p, 1, 30, 12, LD_MASK, LW_HSIN, 0, 0);
        FXS(p, F1_CH + 6, 40, F2_RV + 12, 40);
        break;
    case 14:
        NAME(p, "BRASS");
        O(p, 0, 23, 0, -3); O(p, 1, 27, 0, 4);
        E(&p->osc[0].env, 78, 12, 50, 80, 26, 0);
        E(&p->osc[1].env, 66, 14, 50, 80, 26, 0);
        F(p, 54, 12, FM_LP, 50, FX_12DB); E(&p->filt.env, 40, 24, 46, 40, 30, 0);
        p->vel.dest = VD_FEG; p->vel.amt = 60;
        FXS(p, F1_CH + 1, 20, F2_RV + 6, 25);
        break;
    case 15:  /* 7: the quarter sine, four to a cycle: a saw two octaves up */
        NAME(p, "STRINGS");
        O(p, 0, 7, -24, -8); O(p, 1, 7, -24, 8);
        E(&p->osc[0].env, 70, 50, 60, 85, 58, 0);
        E(&p->osc[1].env, 70, 52, 60, 85, 58, 0);
        F(p, 78, 8, FM_LP, 40, FX_12DB);
        LFO(p, 0, 10, 46, LD_FREQ, LW_SIN, 54, 0);
        p->gen.pans = 70;
        FXS(p, F1_CH + 7, 55, F2_RV + 13, 40);
        break;
    case 16:
        NAME(p, "SHRILL");
        O(p, 0, 1, -48, 0); O(p, 1, 3, -36, 5);
        E(&p->osc[0].env, 70, 0, 50, 70, 30, 0);
        E(&p->osc[1].env, 52, 0, 50, 70, 30, 0);
        F(p, 70, 45, FM_LP, 50, FX_24DB); E(&p->filt.env, 30, 0, 50, 20, 30, 0);
        p->gen.mode = PM_MONO; p->gen.port = 2;
        FXS(p, F1_OFF, 0, F2_DL + 3, 25);
        break;
    case 17:  /* a kick from the pitch envelope, a hat from the noise */
        NAME(p, "KICK HAT");
        O(p, 0, 31, -24, 0); O(p, 1, 15, -12, 0);
        E(&p->osc[0].env, 96, 0, 44, 0, 40, 0);
        E(&p->osc[1].env, 0, 0, 30, 0, 30, 0);
        p->noise.freq = 99; E(&p->noise.env, 36, 0, 20, 0, 18, 0);
        F(p, 127, 0, FM_LP, 0, FX_12DB);
        p->pitch.start = 36; p->pitch.spd = 26;
        p->vel.dest = VD_VOL; p->vel.amt = 80;
        p->gen.pans = 0;
        break;
    case 18:  /* noise through the band pass, repeated by the delay */
        NAME(p, "CLAP ECHO");
        O(p, 0, 31, 0, 0);
        E(&p->osc[0].env, 0, 0, 30, 0, 30, 0);
        p->noise.freq = 94; E(&p->noise.env, 94, 0, 46, 0, 44, 0);
        F(p, 92, 50, FM_BP, 0, FX_12DB);
        LFO(p, 0, 80, 92, LD_NVOL, LW_SAWS, 0, 30);
        FXS(p, F1_OFF, 0, F2_DL + 2, 38);
        break;
    case 19:
        NAME(p, "WIND");
        O(p, 0, 31, 0, 0);
        E(&p->osc[0].env, 0, 0, 30, 0, 30, 0);
        p->noise.freq = 99; E(&p->noise.env, 80, 60, 60, 90, 66, 0);
        F(p, 70, 82, FM_BP, 20, FX_12DB);
        LFO(p, 0, 60, 12, LD_FILT, LW_RAND, 0, 0);
        LFO(p, 1, 30, 8, LD_PAN, LW_SIN, 0, 0);
        p->at.dest = MD_FLT; p->at.amt = 60;
        FXS(p, F1_OFF, 0, F2_RV + 14, 45);
        break;
    case 20:
        NAME(p, "SUB BASS");
        O(p, 0, 31, -12, 0); O(p, 1, 23, -12, 0);
        E(&p->osc[0].env, 90, 0, 40, 90, 14, 0);
        E(&p->osc[1].env, 40, 0, 30, 30, 14, 0);
        F(p, 64, 10, FM_LP, 30, FX_12DB);
        p->gen.mode = PM_MONO_PORTA; p->gen.port = 3; p->gen.pans = 0;
        break;
    case 21:
        NAME(p, "ACID");
        O(p, 0, 19, -12, 0); O(p, 1, 24, -12, 0);
        E(&p->osc[0].env, 80, 0, 50, 80, 14, 0);
        E(&p->osc[1].env, 0, 0, 50, 80, 14, 0);
        F(p, 40, 82, FM_LP, 30, FX_24DB); E(&p->filt.env, 50, 0, 36, 0, 20, 0);
        p->vel.dest = VD_FEG; p->vel.amt = 70;
        p->gen.mode = PM_SLUR; p->gen.port = 3; p->gen.pans = 0;
        FXS(p, F1_DT + 3, 35, F2_DL + 4, 20);
        break;
    case 22:  /* paraphonic, the masks drifting under every voice at once */
        NAME(p, "DRONE");
        O(p, 0, 63, 0, 0); O(p, 1, 95, 7, 0);
        E(&p->osc[0].env, 70, 70, 70, 90, 78, 0);
        E(&p->osc[1].env, 52, 74, 70, 90, 78, 0);
        F(p, 72, 30, FM_LP, 20, FX_12DB);
        LFO(p, 0, 34, 6, LD_O1MASK, LW_SIN, 0, 0);
        LFO(p, 1, 30, 9, LD_O2MASK, LW_SIN, 0, 0);
        p->gen.mode = PM_PARA;
        FXS(p, F1_CH + 8, 40, F2_RV + 15, 48);
        break;
    case 23:
        NAME(p, "BIT PAD");
        O(p, 0, 45, 0, -4); O(p, 1, 109, 0, 4);
        E(&p->osc[0].env, 70, 44, 60, 80, 56, 0);
        E(&p->osc[1].env, 58, 48, 60, 80, 56, 0);
        F(p, 76, 20, FM_LP, 30, FX_12DB);
        LFO(p, 0, 18, 15, LD_MASK, LW_UP, 0, 0);
        FXS(p, F1_BC + 10, 45, F2_RV + 9, 35);
        break;
    case 24:
        NAME(p, "RING BELL");
        O(p, 0, 31, 0, 0); O(p, 1, 14, 0, 0);
        E(&p->osc[0].env, 80, 0, 68, 0, 60, 0);
        E(&p->osc[1].env, 45, 0, 50, 0, 50, 0);
        FXS(p, F1_RM + 9, 55, F2_RV + 10, 35);
        break;
    case 25:  /* 28, plucked, its pitch dropping into place */
        NAME(p, "KOTO");
        O(p, 0, 28, 0, 0); O(p, 1, 31, 12, 0);
        E(&p->osc[0].env, 84, 0, 56, 0, 46, 0);
        E(&p->osc[1].env, 30, 0, 30, 0, 30, 0);
        F(p, 96, 20, FM_LP, 50, FX_12DB); E(&p->filt.env, 20, 0, 40, 0, 40, 0);
        p->pitch.start = 1; p->pitch.spd = 18;
        FXS(p, F1_OFF, 0, F2_RV + 7, 28);
        break;
    case 26:
        NAME(p, "HARP");
        O(p, 0, 31, 0, 0); O(p, 1, 47, 0, 0);
        E(&p->osc[0].env, 80, 0, 62, 0, 58, 0);
        E(&p->osc[1].env, 44, 0, 40, 0, 40, 0);
        p->gen.rate = 50;
        FXS(p, F1_OFF, 0, F2_RV + 11, 36);
        break;
    case 27:  /* 23: the rounded saw */
        NAME(p, "SOFT SAW");
        O(p, 0, 23, 0, -6); O(p, 1, 23, 12, 6);
        E(&p->osc[0].env, 74, 30, 60, 85, 50, 0);
        E(&p->osc[1].env, 40, 34, 60, 85, 50, 0);
        F(p, 74, 10, FM_LP, 40, FX_12DB);
        FXS(p, F1_CH + 3, 45, F2_RV + 9, 30);
        break;
    case 28:
        NAME(p, "WARM POLY");
        O(p, 0, 25, 0, -7); O(p, 1, 26, 0, 7);
        E(&p->osc[0].env, 76, 6, 56, 70, 40, 0);
        E(&p->osc[1].env, 70, 6, 56, 70, 40, 0);
        F(p, 58, 24, FM_LP, 40, FX_12DB); E(&p->filt.env, 32, 8, 54, 30, 40, 0);
        p->gen.mode = PM_POLY_RETRIG;
        FXS(p, F1_CH + 4, 40, F2_RV + 7, 25);
        break;
    case 29:
        NAME(p, "FLUTE");
        O(p, 0, 31, 0, 0); O(p, 1, 30, 12, 0);
        E(&p->osc[0].env, 80, 18, 40, 85, 24, 0);
        E(&p->osc[1].env, 18, 18, 40, 85, 24, 0);
        p->noise.freq = 99; E(&p->noise.env, 16, 10, 40, 30, 20, 0);
        F(p, 100, 10, FM_LP, 60, FX_12DB);
        LFO(p, 0, 12, 50, LD_FREQ, LW_SIN, 52, 0);
        p->gen.mode = PM_MONO; p->gen.port = 1; p->gen.pans = 0;
        FXS(p, F1_OFF, 0, F2_RV + 9, 30);
        break;
    case 30:
        NAME(p, "CLAV");
        O(p, 0, 40, 0, 0); O(p, 1, 41, 12, 0);
        E(&p->osc[0].env, 80, 0, 44, 0, 18, 0);
        E(&p->osc[1].env, 50, 0, 30, 0, 18, 0);
        F(p, 44, 30, FM_HP, 50, FX_12DB);
        p->vel.dest = VD_FLT; p->vel.amt = 60;
        FXS(p, F1_CH + 9, 30, F2_OFF, 0);
        break;
    case 31:
        NAME(p, "BLEEPS");
        O(p, 0, 2, -24, 0); O(p, 1, 12, -12, 0);
        E(&p->osc[0].env, 78, 0, 46, 0, 40, 0);
        E(&p->osc[1].env, 56, 0, 44, 0, 40, 0);
        p->arp.type = AR_RND2; p->arp.spd2 = 75;
        FXS(p, F1_OFF, 0, F2_DL + 9, 34);
        break;
    case 32:  /* both mask envelopes bouncing, slowly, in a long room */
        NAME(p, "DEEP SPACE");
        O(p, 0, 159, -12, 0); ME(p, 0, -70, 8, LOOP_BIDI);
        O(p, 1, 223, 0, 6); ME(p, 1, 50, 6, LOOP_BIDI);
        E(&p->osc[0].env, 66, 70, 70, 90, 80, 0);
        E(&p->osc[1].env, 52, 74, 70, 90, 80, 0);
        F(p, 70, 30, FM_LP, 20, FX_12DB);
        FXS(p, F1_CH + 8, 40, F2_RV + 15, 55);
        break;
    case 33:
        NAME(p, "TAPE KEYS");
        O(p, 0, 30, 0, 0); O(p, 1, 31, -12, 0);
        E(&p->osc[0].env, 78, 0, 66, 30, 40, 0);
        E(&p->osc[1].env, 40, 0, 50, 40, 40, 0);
        F(p, 86, 10, FM_LP, 50, FX_LP1 + 4);
        LFO(p, 0, 9, 22, LD_FREQ, LW_SIN, 0, 0);
        FXS(p, F1_BC + 2, 25, F2_RV + 6, 26);
        break;
    case 34:  /* the pitch falling fast, the masks with it */
        NAME(p, "ZAPPER");
        O(p, 0, 22, 0, 0); ME(p, 0, 60, 60, LOOP_NO);
        O(p, 1, 31, 0, 0);
        E(&p->osc[0].env, 84, 0, 58, 0, 50, 0);
        E(&p->osc[1].env, 40, 0, 52, 0, 46, 0);
        p->pitch.start = 30; p->pitch.spd = 20;
        FXS(p, F1_OFF, 0, F2_DL + 3, 30);
        break;
    case 35:  /* SLUR: neighbouring keys played legato slide */
        NAME(p, "SLUR LEAD");
        O(p, 0, 27, 0, 0); O(p, 1, 27, -12, 5);
        E(&p->osc[0].env, 78, 4, 50, 80, 30, 0);
        E(&p->osc[1].env, 50, 4, 50, 80, 30, 0);
        F(p, 80, 30, FM_LP, 50, FX_12DB);
        LFO(p, 0, 12, 50, LD_FREQ, LW_SIN, 50, 0);
        p->gen.mode = PM_SLUR; p->gen.port = 4;
        FXS(p, F1_CH + 1, 25, F2_DL + 10, 26);
        break;
    case 36:  /* MOLY: staccato is poly, legato glides */
        NAME(p, "MOLY PAD");
        O(p, 0, 55, 0, -5); O(p, 1, 39, 0, 5);
        E(&p->osc[0].env, 70, 36, 60, 85, 56, 0);
        E(&p->osc[1].env, 60, 40, 60, 85, 56, 0);
        F(p, 70, 22, FM_LP, 30, FX_12DB);
        p->gen.mode = PM_MOLY; p->gen.port = 5;
        FXS(p, F1_CH + 5, 40, F2_RV + 11, 36);
        break;
    case 37:  /* PARA: one envelope for every note */
        NAME(p, "PARA STRGS");
        O(p, 0, 7, -24, -5); O(p, 1, 23, 0, 5);
        E(&p->osc[0].env, 70, 50, 60, 85, 58, 0);
        E(&p->osc[1].env, 50, 50, 60, 85, 58, 0);
        F(p, 64, 20, FM_LP, 30, FX_12DB); E(&p->filt.env, 30, 50, 60, 30, 60, 0);
        p->gen.mode = PM_PARA; p->gen.port = 3;
        FXS(p, F1_CH + 7, 50, F2_RV + 12, 36);
        break;
    case 38:
        NAME(p, "FOLD BASS");
        O(p, 0, 31, -12, 0); O(p, 1, 15, -24, 0);
        E(&p->osc[0].env, 82, 0, 50, 70, 16, 0);
        E(&p->osc[1].env, 50, 0, 50, 70, 16, 0);
        F(p, 58, 20, FM_LP, 30, FX_12DB);
        p->wheel.dest = MD_FX1BAL; p->wheel.amt = 60;
        p->gen.mode = PM_MONO_RETRIG; p->gen.pans = 0;
        FXS(p, F1_DT + 13, 55, F2_OFF, 0);
        break;
    case 39:
        NAME(p, "PHASE PAD");
        O(p, 0, 37, 0, -4); O(p, 1, 61, 0, 4);
        E(&p->osc[0].env, 70, 46, 60, 85, 56, 0);
        E(&p->osc[1].env, 60, 50, 60, 85, 56, 0);
        F(p, 76, 16, FM_LP, 30, FX_12DB);
        FXS(p, F1_CH + 14, 60, F2_RV + 10, 32);
        break;
    default:
        break;
    }
}

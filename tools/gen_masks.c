/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the masks' DC and level (dsp/mask_table.h), measured from the oscillator itself:
 *   tools/build_tools.sh && build/gen_masks > dsp/mask_table.h
 *   build/gen_masks -w | python3 tools/mask_sheet.py docs/img/masks.png     (all 512, drawn)
 * Each mask's cycle is read at 16384 points: its mean is the DC taken out, and its level is evened to a peak of 1,
 * held so that no mask is louder (in rms) than 0.8 (a square would be 1). */
#include <stdio.h>
#include "bitmask.h"
float bm_sin[BM_SIN_N + 1];
const float BM_DC[512], BM_NORM[512];
#define NP 16384u
int main(int argc, char **argv)
{
    static float dc[512], nm[512];
    bm_tab_t t;
    uint32_t m, i;
    bm_sin_init();
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'w') {   /* -w: every mask's cycle, 256 points, raw floats */
        for (m = 0; m < 512u; m++) {
            bm_build(&t, m << 2);
            for (i = 0; i < 256u; i++) {
                float x = bm_raw(&t, i << 24);
                fwrite(&x, 4, 1, stdout);
            }
        }
        return 0;
    }
    for (m = 0; m < 512u; m++) {
        double sum = 0.0, sq = 0.0, pk = 0.0;
        bm_build(&t, m << 2);
        for (i = 0; i < NP; i++)
            sum += bm_raw(&t, i * (0x100000000ull / NP));
        dc[m] = (float)(sum / NP);
        for (i = 0; i < NP; i++) {
            double x = bm_raw(&t, i * (0x100000000ull / NP)) - dc[m];
            sq += x * x;
            if (x > pk) pk = x;
            if (-x > pk) pk = -x;
        }
        {
            double rms = __builtin_sqrt(sq / NP), n = pk > 1e-6 ? 1.0 / pk : 1.0;
            if (rms * n > 0.8)
                n = 0.8 / rms;
            nm[m] = (float)n;
        }
    }
    printf("/* SPDX-License-Identifier: MPL-2.0 */\n/* MASQUE — each mask's DC and level: made by tools/gen_masks.c "
           "from dsp/bitmask.h (do not edit) */\n#pragma once\n");
    printf("const float BM_DC[512] = {");
    for (m = 0; m < 512u; m++) printf("%s%.7ef", !m ? "\n    " : m % 6 ? ", " : ",\n    ", (double)dc[m]);
    printf("};\nconst float BM_NORM[512] = {");
    for (m = 0; m < 512u; m++) printf("%s%.7ef", !m ? "\n    " : m % 6 ? ", " : ",\n    ", (double)nm[m]);
    printf("};\n");
    return 0;
}

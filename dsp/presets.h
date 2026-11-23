/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the factory sounds (presets.c) */
#pragma once
#include "masque.h"

extern const int PRESET_COUNT;
void preset_init(mq_patch_t *p);           /* INIT: a sine, nothing else */
void preset_get(int n, mq_patch_t *p);     /* P00..: n < PRESET_COUNT */

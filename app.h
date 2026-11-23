/* SPDX-License-Identifier: MPL-2.0 */
/* MASQUE — the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "MASQUE"
#define APP_SPLASH_RGB RGB(255, 59, 40)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity and pressure from the FM-1's own keys (lib/touch.h) */
#define APP_OVERLOAD                     /* the render ran long: a voice fewer */
#define APP_CANVAS_PX (240u * 120u)      /* the screen drawn in two halves: 57.6 KB of the pool */

/* the store: the setup, the panel calibration (the platform's), the user sounds in six banks of 24 (128 used) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BANK0, OBJ_BANK1, OBJ_BANK2, OBJ_BANK3, OBJ_BANK4, OBJ_BANK5, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xC0000u, 0xC2000u, 0xC4000u, 0xC6000u, 0xC8000u, 0xCA000u}
/* backup over USB (fm1.toml backup = true): the setup and the user sounds; the panel's calibration stays the unit's */
#define APP_BACKUP_OBJS {OBJ_SETTINGS, OBJ_BANK0, OBJ_BANK1, OBJ_BANK2, OBJ_BANK3, OBJ_BANK4, OBJ_BANK5}
#define APP_BACKUP_MAGIC 0x4253514Du            /* "MQSB" */
#define APP_BACKUP_VERSION 2u
#define APP_BACKUP_WHAT "replaces your sounds"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_overload(void);

/* the host simulator's checks (host/plat_host.c "expect") */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);

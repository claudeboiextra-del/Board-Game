/*
 * Sky Circuit – PS3 homebrew board game
 * PSL1GHT + Tiny3D, DualShock 3, dual logging to HDD + USB
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/process.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <io/pad.h>
#include <sys/spu.h>
#include <sys/thread.h>

#include <tiny3d.h>

#include "game.h"
#include "logger.h"

static int running = 1;
static padInfo padinfo;
static padData paddata[MAX_PADS];

static void sysutil_callback(uint64_t status, uint64_t param, void *userdata)
{
    (void)param; (void)userdata;
    switch (status) {
        case CELL_SYSUTIL_REQUEST_EXITGAME:
            LOG_I("Sysutil exit requested");
            running = 0;
            break;
        default:
            break;
    }
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* logging first so we capture everything */
    logger_init();
    LOG_I("Sky Circuit boot – PSL1GHT / Tiny3D");

    sysUtilRegisterCallback(0, sysutil_callback, NULL);

    /* video + Tiny3D */
    if (tiny3d_Init(TINY3D_Z16 | 1024 * 1024) < 0) {
        LOG_F("tiny3d_Init failed");
        logger_shutdown();
        return -1;
    }
    LOG_I("Tiny3D initialised");

    ioPadInit(7);
    LOG_I("Pad system ready");

    GameContext game;
    game_init(&game);

    uint32_t prev_buttons = 0;

    while (running) {
        sysUtilCheckCallback();

        ioPadGetInfo(&padinfo);
        uint32_t buttons = 0;
        int i;
        for (i = 0; i < MAX_PADS; ++i) {
            if (padinfo.status[i]) {
                ioPadGetData(i, &paddata[i]);
                /* map common DualShock 3 buttons */
                if (paddata[i].BTN_CROSS)   buttons |= 0x10;
                if (paddata[i].BTN_CIRCLE)  buttons |= 0x20;
                if (paddata[i].BTN_SQUARE)  buttons |= 0x40;
                if (paddata[i].BTN_TRIANGLE) buttons |= 0x80;
                if (paddata[i].BTN_START)   buttons |= 0x100;
            }
        }

        /* edge-trigger for most actions */
        uint32_t pressed = buttons & ~prev_buttons;
        prev_buttons = buttons;

        game_update(&game, pressed);
        game_render(&game);

        if (game.state == STATE_EXIT) {
            LOG_I("Game requested exit");
            running = 0;
        }

        /* light frame pacing – keep CPU cool, feel responsive */
        sys_timer_usleep(16000); /* ~60 fps target */
    }

    game_shutdown(&game);
    ioPadEnd();
    tiny3d_Exit();
    logger_shutdown();

    return 0;
}

/*
 * Board Game – PS3 homebrew
 * Multi DualShock: player i → port i; edges + held for minigames
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
#include "audio_sfx.h"

static int running = 1;
static padInfo padinfo;
static padData paddata[MAX_PADS];
static uint32_t prev_pad[PAD_COUNT];

static void sysutil_callback(uint64_t status, uint64_t param, void *userdata)
{
    (void)param; (void)userdata;
    if (status == SYSUTIL_EXIT_GAME) {
        LOG_I("Sysutil exit requested");
        running = 0;
    }
}

static uint32_t read_pad_buttons(int port)
{
    uint32_t b = 0;
    if (port < 0 || port >= MAX_PADS) return 0;
    if (!padinfo.status[port]) return 0;
    ioPadGetData(port, &paddata[port]);
    if (paddata[port].BTN_CROSS)    b |= 0x10;
    if (paddata[port].BTN_CIRCLE)   b |= 0x20;
    if (paddata[port].BTN_SQUARE)   b |= 0x40;
    if (paddata[port].BTN_TRIANGLE) b |= 0x80;
    if (paddata[port].BTN_START)    b |= 0x100;
    return b;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    logger_init();
    LOG_I("Board Game boot – multi-pad edges+held");

    sysUtilRegisterCallback(0, sysutil_callback, NULL);

    if (tiny3d_Init(TINY3D_Z16 | 1024 * 1024) < 0) {
        LOG_F("tiny3d_Init failed");
        logger_shutdown();
        return -1;
    }

    ioPadInit(7);

    if (sfx_init() != 0)
        LOG_W("Running without SFX");

    GameContext game;
    game_init(&game);
    memset(prev_pad, 0, sizeof(prev_pad));

    while (running) {
        int i;
        uint32_t held[PAD_COUNT];
        uint32_t pressed[PAD_COUNT];

        sysUtilCheckCallback();
        ioPadGetInfo(&padinfo);

        for (i = 0; i < PAD_COUNT; ++i) {
            held[i] = read_pad_buttons(i);
            pressed[i] = held[i] & ~prev_pad[i];
            prev_pad[i] = held[i];
        }

        game_update(&game, pressed, held);
        sfx_update();
        game_render(&game);

        if (game.state == STATE_EXIT)
            running = 0;

        usleep(16000);
    }

    game_shutdown(&game);
    sfx_shutdown();
    ioPadEnd();
    tiny3d_Exit();
    logger_shutdown();
    return 0;
}

/*
 * Tiny synthesizer SFX for PS3 (PSL1GHT audio port)
 * Pure square/saw blips – no WAV files.
 */
#include "audio_sfx.h"
#include "logger.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <audio/audio.h>

#define SFX_RATE     48000
#define SFX_CH       2
#define SFX_SAMPLES  256
#define SFX_BLOCKS   8

static int             sfx_ready;
static u32             sfx_port;
static audioPortConfig sfx_cfg;

static int   voice_on;
static float voice_phase;
static float voice_freq;
static float voice_freq_end;
static float voice_amp;
static int   voice_left;
static int   voice_total;
static int   voice_wave; /* 0 square, 1 saw */

static float note_hz(int n)
{
    return 440.0f * powf(2.0f, (float)(n - 69) / 12.0f);
}

int sfx_init(void)
{
    audioPortParam p;
    int ret;

    memset(&sfx_cfg, 0, sizeof(sfx_cfg));
    sfx_ready = 0;
    voice_on = 0;

    ret = audioInit();
    if (ret) {
        LOG_W("audioInit %d – SFX off", ret);
        return -1;
    }

    p.numChannels = AUDIO_PORT_2CH;
    p.numBlocks   = SFX_BLOCKS;
    p.attr        = 0;
    p.level       = 1.0f;

    ret = audioPortOpen(&p, &sfx_port);
    if (ret) {
        LOG_W("audioPortOpen %d", ret);
        audioQuit();
        return -1;
    }

    ret = audioGetPortConfig(sfx_port, &sfx_cfg);
    if (ret || !sfx_cfg.audioDataStart) {
        LOG_W("audioGetPortConfig fail");
        audioPortClose(sfx_port);
        audioQuit();
        return -1;
    }

    ret = audioPortStart(sfx_port);
    if (ret) {
        LOG_W("audioPortStart %d", ret);
        audioPortClose(sfx_port);
        audioQuit();
        return -1;
    }

    sfx_ready = 1;
    LOG_I("SFX ready (port %u, blocks %u)", sfx_port, sfx_cfg.numBlocks);
    return 0;
}

void sfx_shutdown(void)
{
    if (!sfx_ready) return;
    audioPortStop(sfx_port);
    audioPortClose(sfx_port);
    audioQuit();
    sfx_ready = 0;
}

void sfx_play(SfxId id)
{
    if (!sfx_ready || id == SFX_NONE) return;

    voice_on = 1;
    voice_phase = 0.0f;
    voice_wave = 0;

    switch (id) {
    case SFX_CLICK:
        voice_freq = note_hz(76); voice_freq_end = note_hz(76);
        voice_amp = 0.20f; voice_total = (int)(0.035f * SFX_RATE);
        break;
    case SFX_DICE:
        voice_freq = note_hz(60); voice_freq_end = note_hz(74);
        voice_amp = 0.26f; voice_total = (int)(0.11f * SFX_RATE); voice_wave = 1;
        break;
    case SFX_HOP:
        voice_freq = note_hz(67); voice_freq_end = note_hz(72);
        voice_amp = 0.16f; voice_total = (int)(0.045f * SFX_RATE);
        break;
    case SFX_LAND:
        voice_freq = note_hz(52); voice_freq_end = note_hz(46);
        voice_amp = 0.22f; voice_total = (int)(0.07f * SFX_RATE);
        break;
    case SFX_COIN:
        voice_freq = note_hz(84); voice_freq_end = note_hz(96);
        voice_amp = 0.24f; voice_total = (int)(0.09f * SFX_RATE);
        break;
    case SFX_STAR:
        voice_freq = note_hz(72); voice_freq_end = note_hz(96);
        voice_amp = 0.28f; voice_total = (int)(0.20f * SFX_RATE);
        break;
    case SFX_FAIL:
        voice_freq = note_hz(48); voice_freq_end = note_hz(36);
        voice_amp = 0.22f; voice_total = (int)(0.13f * SFX_RATE); voice_wave = 1;
        break;
    case SFX_WIN:
        voice_freq = note_hz(67); voice_freq_end = note_hz(91);
        voice_amp = 0.30f; voice_total = (int)(0.26f * SFX_RATE);
        break;
    case SFX_COUNT_FLASH:
        voice_freq = note_hz(90); voice_freq_end = note_hz(90);
        voice_amp = 0.18f; voice_total = (int)(0.028f * SFX_RATE);
        break;
    default:
        voice_on = 0;
        return;
    }
    voice_left = voice_total;
}

static float gen_sample(void)
{
    float t, env, f, s;
    if (!voice_on || voice_left <= 0) {
        voice_on = 0;
        return 0.0f;
    }
    t = 1.0f - (float)voice_left / (float)voice_total;
    env = 1.0f - t;
    if (env < 0.0f) env = 0.0f;
    f = voice_freq + (voice_freq_end - voice_freq) * t;
    voice_phase += f / (float)SFX_RATE;
    if (voice_phase >= 1.0f) voice_phase -= (float)(int)voice_phase;

    if (voice_wave == 1)
        s = voice_phase * 2.0f - 1.0f;
    else
        s = (voice_phase < 0.5f) ? 1.0f : -1.0f;

    voice_left--;
    return s * voice_amp * env;
}

void sfx_update(void)
{
    u32 i, b;
    f32 *buf;
    u64 read_idx;

    if (!sfx_ready || !sfx_cfg.audioDataStart) return;

    /*
     * PSL1GHT stores the current read block index just before the buffer.
     * Fill the next free block with mono→stereo samples.
     */
    read_idx = *(u64 *)((u8 *)sfx_cfg.audioDataStart - 8);
    b = (u32)((read_idx + 1) % (sfx_cfg.numBlocks ? sfx_cfg.numBlocks : SFX_BLOCKS));

    buf = (f32 *)((u8 *)sfx_cfg.audioDataStart +
          b * sfx_cfg.channelCount * SFX_SAMPLES * sizeof(f32));

    for (i = 0; i < SFX_SAMPLES; ++i) {
        f32 s = gen_sample();
        buf[i * 2 + 0] = s;
        buf[i * 2 + 1] = s;
    }
}

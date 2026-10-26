/*
 * Tiny synthesizer SFX for PS3 (PSL1GHT audio port)
 * No WAV assets – pure square/saw blips, light on the Cell.
 */
#include "audio_sfx.h"
#include "logger.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <audio/audio.h>
#include <sys/thread.h>

#define SFX_RATE        48000
#define SFX_CHANNELS    2
#define SFX_BLOCK       256
#define SFX_BLOCKS      8

static int          sfx_ready = 0;
static u32          sfx_port = 0;
static audioPortConfig sfx_cfg;

/* active one-shot voice */
static int          voice_on = 0;
static float        voice_phase = 0.0f;
static float        voice_freq = 440.0f;
static float        voice_freq_end = 440.0f;
static float        voice_amp = 0.0f;
static int          voice_samples = 0;
static int          voice_total = 0;
static int          voice_wave = 0; /* 0=square 1=saw 2=noise */

static float note_hz(int n) {
    /* MIDI-ish: n=69 => A4=440 */
    return 440.0f * powf(2.0f, (float)(n - 69) / 12.0f);
}

int sfx_init(void)
{
    audioPortParam p;
    int ret;

    memset(&sfx_cfg, 0, sizeof(sfx_cfg));
    ret = audioInit();
    if (ret != 0) {
        LOG_W("audioInit failed %d – SFX disabled", ret);
        return -1;
    }

    p.numChannels = AUDIO_PORT_2CH;
    p.numBlocks   = SFX_BLOCKS;
    p.attr        = 0;
    p.level       = 1.0f;

    ret = audioPortOpen(&p, &sfx_port);
    if (ret != 0) {
        LOG_W("audioPortOpen failed %d", ret);
        audioQuit();
        return -1;
    }

    ret = audioGetPortConfig(sfx_port, &sfx_cfg);
    if (ret != 0 || !sfx_cfg.audioDataStart) {
        LOG_W("audioGetPortConfig failed");
        audioPortClose(sfx_port);
        audioQuit();
        return -1;
    }

    ret = audioPortStart(sfx_port);
    if (ret != 0) {
        LOG_W("audioPortStart failed %d", ret);
        audioPortClose(sfx_port);
        audioQuit();
        return -1;
    }

    sfx_ready = 1;
    LOG_I("SFX audio port ready");
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
        voice_amp = 0.22f; voice_total = (int)(0.04f * SFX_RATE); voice_wave = 0;
        break;
    case SFX_DICE:
        voice_freq = note_hz(60); voice_freq_end = note_hz(72);
        voice_amp = 0.28f; voice_total = (int)(0.12f * SFX_RATE); voice_wave = 1;
        break;
    case SFX_HOP:
        voice_freq = note_hz(67); voice_freq_end = note_hz(71);
        voice_amp = 0.18f; voice_total = (int)(0.05f * SFX_RATE); voice_wave = 0;
        break;
    case SFX_LAND:
        voice_freq = note_hz(55); voice_freq_end = note_hz(50);
        voice_amp = 0.25f; voice_total = (int)(0.08f * SFX_RATE); voice_wave = 0;
        break;
    case SFX_COIN:
        voice_freq = note_hz(84); voice_freq_end = note_hz(96);
        voice_amp = 0.26f; voice_total = (int)(0.10f * SFX_RATE); voice_wave = 0;
        break;
    case SFX_STAR:
        voice_freq = note_hz(72); voice_freq_end = note_hz(96);
        voice_amp = 0.30f; voice_total = (int)(0.22f * SFX_RATE); voice_wave = 0;
        break;
    case SFX_FAIL:
        voice_freq = note_hz(50); voice_freq_end = note_hz(40);
        voice_amp = 0.24f; voice_total = (int)(0.14f * SFX_RATE); voice_wave = 1;
        break;
    case SFX_WIN:
        voice_freq = note_hz(67); voice_freq_end = note_hz(91);
        voice_amp = 0.32f; voice_total = (int)(0.28f * SFX_RATE); voice_wave = 0;
        break;
    case SFX_COUNT_FLASH:
        voice_freq = note_hz(88); voice_freq_end = note_hz(88);
        voice_amp = 0.20f; voice_total = (int)(0.03f * SFX_RATE); voice_wave = 0;
        break;
    default:
        voice_on = 0;
        return;
    }
    voice_samples = voice_total;
}

static float sample_voice(void)
{
    float t, env, f, s;
    if (!voice_on || voice_samples <= 0) {
        voice_on = 0;
        return 0.0f;
    }
    t = 1.0f - (float)voice_samples / (float)voice_total;
    env = 1.0f - t; /* linear decay */
    if (env < 0.0f) env = 0.0f;
    f = voice_freq + (voice_freq_end - voice_freq) * t;
    voice_phase += f / (float)SFX_RATE;
    if (voice_phase >= 1.0f) voice_phase -= 1.0f;

    if (voice_wave == 1)
        s = voice_phase * 2.0f - 1.0f; /* saw */
    else if (voice_wave == 2)
        s = ((float)(rand() % 1000) / 500.0f) - 1.0f;
    else
        s = (voice_phase < 0.5f) ? 1.0f : -1.0f; /* square */

    voice_samples--;
    return s * voice_amp * env;
}

void sfx_update(void)
{
    u64 i, j;
    float *buf;
    u32 write_idx;

    if (!sfx_ready || !sfx_cfg.audioDataStart) return;

    /* fill as many free blocks as available */
    for (j = 0; j < SFX_BLOCKS; ++j) {
        write_idx = audioGetPortWriteIndex(sfx_port);
        /* crude: always write next block slot – PSL1GHT ring */
        buf = (float *)((u8 *)sfx_cfg.audioDataStart +
              ((write_idx + j) % sfx_cfg.numBlocks) * sfx_cfg.channelCount * sfx_cfg.numBlocks * sizeof(float) / sfx_cfg.numBlocks);
        /* Safer fill using documented layout */
        buf = (float *)sfx_cfg.audioDataStart +
              ((audioGetPortWriteIndex(sfx_port)) % sfx_cfg.numBlocks) *
              (sfx_cfg.channelCount * SFX_BLOCK);

        for (i = 0; i < SFX_BLOCK; ++i) {
            float s = sample_voice();
            buf[i * 2 + 0] = s;
            buf[i * 2 + 1] = s;
        }
        /* only fill one block per frame to stay light */
        break;
    }
}

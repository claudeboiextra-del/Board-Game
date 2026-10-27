/*
 * Board Game - synthesized SFX + procedural BGM
 * PSL1GHT audioPort, pure synth (HEN-safe)
 */
#include "audio_sfx.h"
#include "logger.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <audio/audio.h>

#define SFX_RATE     48000
#define SFX_SAMPLES  256
#define SFX_BLOCKS   8
#define MAX_VOICES   10
#define MUSIC_VOICES 4

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef enum { WAVE_SINE=0, WAVE_SQUARE, WAVE_SAW, WAVE_NOISE, WAVE_TRI } WaveType;

typedef struct {
    int active;
    WaveType wave;
    float phase, freq, amp, decay, life, pan, env, attack;
    int duty;
} Voice;

static int sfx_ready;
static u32 sfx_port;
static audioPortConfig sfx_cfg;
static Voice voices[MAX_VOICES];
static float master_sfx = 0.55f;
static float master_music = 0.28f;
static MusicId cur_music = MUSIC_NONE;
static MusicId want_music = MUSIC_NONE;
static float music_fade = 1.f;
static int music_step, music_tick, music_tempo, music_pattern_len;
static const int *music_notes, *music_bass;
static uint32_t noise_reg = 0xACE1u;

static float noise_sample(void) {
    uint32_t bit = ((noise_reg >> 0) ^ (noise_reg >> 2) ^ (noise_reg >> 3) ^ (noise_reg >> 5)) & 1;
    noise_reg = (noise_reg >> 1) | (bit << 15);
    return ((noise_reg & 0xFF) / 127.5f) - 1.f;
}
static float midi_hz(int n) {
    if (n <= 0) return 0.f;
    return 440.f * powf(2.f, (n - 69) / 12.f);
}
static float wave_sample(Voice *v) {
    float t = v->phase, s = 0.f;
    switch (v->wave) {
    case WAVE_SINE: s = sinf(t * 2.f * (float)M_PI); break;
    case WAVE_SQUARE: s = (t < (v->duty / 16.f)) ? 1.f : -1.f; break;
    case WAVE_SAW: s = 2.f * t - 1.f; break;
    case WAVE_TRI: s = (t < 0.5f) ? (4.f * t - 1.f) : (3.f - 4.f * t); break;
    case WAVE_NOISE: s = noise_sample(); break;
    }
    return s;
}
static int alloc_voice(void) {
    int i;
    for (i = MUSIC_VOICES; i < MAX_VOICES; ++i)
        if (!voices[i].active) return i;
    return MUSIC_VOICES + (rand() % (MAX_VOICES - MUSIC_VOICES));
}
static void trigger(WaveType w, float freq, float amp, float decay, float life_sec, float pan, float attack, int duty) {
    int id = alloc_voice();
    Voice *v = &voices[id];
    v->active = 1; v->wave = w; v->phase = 0.f; v->freq = freq;
    v->amp = amp * master_sfx; v->decay = decay;
    v->life = life_sec < 0.f ? -1.f : life_sec * SFX_RATE;
    v->pan = pan; v->env = 0.f; v->attack = attack > 0.f ? attack : 0.002f;
    v->duty = duty > 0 ? duty : 8;
}

void sfx_play(SfxId id) {
    if (!sfx_ready) return;
    switch (id) {
    case SFX_CLICK:
        trigger(WAVE_SQUARE, 880.f, 0.35f, 0.9992f, 0.08f, 0.f, 0.001f, 6); break;
    case SFX_COIN:
        trigger(WAVE_SQUARE, 1320.f, 0.4f, 0.9994f, 0.12f, -0.2f, 0.001f, 8);
        trigger(WAVE_SINE, 1760.f, 0.25f, 0.9990f, 0.15f, 0.3f, 0.002f, 8); break;
    case SFX_STAR:
        trigger(WAVE_SINE, 523.f, 0.35f, 0.9996f, 0.25f, -0.3f, 0.005f, 8);
        trigger(WAVE_SINE, 659.f, 0.30f, 0.9995f, 0.28f, 0.f, 0.008f, 8);
        trigger(WAVE_SINE, 784.f, 0.28f, 0.9994f, 0.32f, 0.3f, 0.012f, 8); break;
    case SFX_FAIL:
        trigger(WAVE_SAW, 220.f, 0.4f, 0.9988f, 0.22f, 0.f, 0.001f, 8);
        trigger(WAVE_NOISE, 0.f, 0.2f, 0.9970f, 0.15f, 0.f, 0.001f, 8); break;
    case SFX_WIN:
        trigger(WAVE_SQUARE, 523.f, 0.3f, 0.9997f, 0.35f, -0.4f, 0.01f, 8);
        trigger(WAVE_SQUARE, 659.f, 0.28f, 0.9996f, 0.40f, 0.f, 0.02f, 8);
        trigger(WAVE_SQUARE, 784.f, 0.26f, 0.9995f, 0.45f, 0.4f, 0.03f, 8); break;
    case SFX_HOP:
        trigger(WAVE_SQUARE, 440.f, 0.25f, 0.9985f, 0.06f, 0.f, 0.001f, 4); break;
    case SFX_LAND:
        trigger(WAVE_NOISE, 0.f, 0.22f, 0.9960f, 0.07f, 0.f, 0.001f, 8);
        trigger(WAVE_TRI, 180.f, 0.2f, 0.9975f, 0.08f, 0.f, 0.001f, 8); break;
    case SFX_DICE:
        trigger(WAVE_NOISE, 0.f, 0.3f, 0.9950f, 0.1f, -0.2f, 0.001f, 8);
        trigger(WAVE_SQUARE, 600.f + (rand() % 200), 0.25f, 0.9980f, 0.12f, 0.2f, 0.001f, 5); break;
    case SFX_COUNT_FLASH:
        trigger(WAVE_SQUARE, 990.f, 0.32f, 0.9988f, 0.09f, 0.f, 0.001f, 10); break;
    case SFX_MG_START:
        trigger(WAVE_SAW, 330.f, 0.3f, 0.9990f, 0.2f, -0.3f, 0.01f, 8);
        trigger(WAVE_SQUARE, 440.f, 0.28f, 0.9988f, 0.22f, 0.3f, 0.015f, 6); break;
    case SFX_MG_GO:
        trigger(WAVE_SQUARE, 880.f, 0.45f, 0.9992f, 0.18f, 0.f, 0.001f, 8);
        trigger(WAVE_SINE, 1320.f, 0.3f, 0.9990f, 0.2f, 0.f, 0.002f, 8); break;
    case SFX_BUZZ:
        trigger(WAVE_SAW, 110.f, 0.35f, 0.9985f, 0.15f, 0.f, 0.001f, 8); break;
    case SFX_POWER:
        trigger(WAVE_SAW, 200.f, 0.3f, 0.9993f, 0.3f, -0.2f, 0.02f, 8); break;
    case SFX_WHOOSH:
        trigger(WAVE_NOISE, 0.f, 0.28f, 0.9975f, 0.18f, 0.f, 0.005f, 8); break;
    default: break;
    }
}

/* patterns */
static const int pat_menu_l[] = {60,0,64,0,67,0,64,0,60,0,64,0,72,0,67,0};
static const int pat_menu_b[] = {36,0,0,36,43,0,0,43,36,0,0,36,41,0,0,41};
static const int pat_vis_l[] = {72,74,76,79,76,74,72,0,67,69,71,74,71,69,67,0};
static const int pat_vis_b[] = {48,0,48,0,53,0,53,0,48,0,48,0,55,0,55,0};
static const int pat_geo_l[] = {55,0,57,0,59,0,55,0,52,0,55,0,59,62,59,0};
static const int pat_geo_b[] = {31,0,0,0,36,0,0,0,31,0,0,0,38,0,36,0};
static const int pat_met_l[] = {67,0,67,70,0,67,0,72,0,70,67,0,65,0,67,0};
static const int pat_met_b[] = {36,36,0,36,41,41,0,41,36,36,0,36,43,0,41,0};
static const int pat_mash_l[] = {60,60,0,60,63,63,0,63,65,65,0,65,67,0,65,0};
static const int pat_mash_b[] = {36,0,36,0,36,0,43,0,36,0,36,0,41,0,43,0};
static const int pat_tim_l[] = {76,0,0,72,0,0,74,0,76,0,0,79,0,0,76,0};
static const int pat_tim_b[] = {48,0,0,0,45,0,0,0,43,0,0,0,41,0,43,0};
static const int pat_re_l[] = {84,0,0,0,80,0,0,0,84,0,80,0,76,0,0,0};
static const int pat_re_b[] = {36,0,0,36,0,0,36,0,36,0,0,0,31,0,0,0};
static const int pat_rhy_l[] = {60,64,67,72,67,64,60,0,62,65,69,74,69,65,62,0};
static const int pat_rhy_b[] = {36,0,36,36,43,0,43,0,36,0,36,36,41,0,41,0};
static const int pat_res_l[] = {60,64,67,72,76,72,67,64,62,65,69,74,77,74,69,65};
static const int pat_res_b[] = {36,0,0,0,36,0,0,0,38,0,0,0,38,0,0,0};

static void music_apply(MusicId id) {
    cur_music = id; music_step = 0; music_tick = 0;
    switch (id) {
    case MUSIC_MENU: music_notes=pat_menu_l; music_bass=pat_menu_b; music_pattern_len=16; music_tempo=SFX_RATE/6; break;
    case MUSIC_BOARD_VISUAL: music_notes=pat_vis_l; music_bass=pat_vis_b; music_pattern_len=16; music_tempo=SFX_RATE/7; break;
    case MUSIC_BOARD_GEO: music_notes=pat_geo_l; music_bass=pat_geo_b; music_pattern_len=16; music_tempo=SFX_RATE/5; break;
    case MUSIC_BOARD_METRO: music_notes=pat_met_l; music_bass=pat_met_b; music_pattern_len=16; music_tempo=SFX_RATE/8; break;
    case MUSIC_MG_MASH: music_notes=pat_mash_l; music_bass=pat_mash_b; music_pattern_len=16; music_tempo=SFX_RATE/9; break;
    case MUSIC_MG_TIMING: music_notes=pat_tim_l; music_bass=pat_tim_b; music_pattern_len=16; music_tempo=SFX_RATE/6; break;
    case MUSIC_MG_REACT: music_notes=pat_re_l; music_bass=pat_re_b; music_pattern_len=16; music_tempo=SFX_RATE/7; break;
    case MUSIC_MG_RHYTHM: music_notes=pat_rhy_l; music_bass=pat_rhy_b; music_pattern_len=16; music_tempo=SFX_RATE/8; break;
    case MUSIC_RESULTS: music_notes=pat_res_l; music_bass=pat_res_b; music_pattern_len=16; music_tempo=SFX_RATE/5; break;
    default: music_notes=NULL; music_bass=NULL; music_pattern_len=0; music_tempo=SFX_RATE/4; break;
    }
}

void music_set(MusicId id) {
    if (id == want_music) return;
    want_music = id;
    if (cur_music == MUSIC_NONE) { music_apply(id); music_fade = 1.f; }
    else music_fade = 0.f;
}
MusicId music_current(void) { return cur_music; }

static void music_tick_voices(void) {
    int i;
    if (want_music != cur_music) {
        music_fade -= 0.04f;
        if (music_fade <= 0.f) { music_apply(want_music); music_fade = 1.f; }
    } else if (music_fade < 1.f) {
        music_fade += 0.03f; if (music_fade > 1.f) music_fade = 1.f;
    }
    if (!music_notes || cur_music == MUSIC_NONE) {
        for (i = 0; i < MUSIC_VOICES; ++i) voices[i].active = 0;
        return;
    }
    music_tick--;
    if (music_tick <= 0) {
        int n, b;
        music_tick = music_tempo;
        n = music_notes[music_step % music_pattern_len];
        b = music_bass[music_step % music_pattern_len];
        music_step++;
        if (n > 0) {
            Voice *v = &voices[0];
            v->active=1; v->wave=(cur_music==MUSIC_MG_MASH||cur_music==MUSIC_BOARD_METRO)?WAVE_SQUARE:WAVE_TRI;
            v->freq=midi_hz(n); v->amp=0.22f*master_music*music_fade; v->decay=0.9996f;
            v->life=(float)(music_tempo*1.2f); v->phase=0; v->env=0; v->attack=0.005f; v->pan=-0.15f; v->duty=6;
        } else voices[0].decay=0.995f;
        if (b > 0) {
            Voice *v = &voices[1];
            v->active=1; v->wave=WAVE_SAW; v->freq=midi_hz(b); v->amp=0.28f*master_music*music_fade;
            v->decay=0.9997f; v->life=(float)(music_tempo*1.4f); v->phase=0; v->env=0; v->attack=0.008f; v->pan=0.1f; v->duty=8;
        } else voices[1].decay=0.994f;
        if (cur_music==MUSIC_MENU||cur_music==MUSIC_MG_RHYTHM||cur_music==MUSIC_BOARD_VISUAL) {
            Voice *v=&voices[2]; int arpn=n>0?n+12:72;
            v->active=1; v->wave=WAVE_SINE; v->freq=midi_hz(arpn); v->amp=0.12f*master_music*music_fade;
            v->decay=0.9994f; v->life=(float)music_tempo; v->phase=0; v->env=0; v->attack=0.02f; v->pan=0.35f; v->duty=8;
        }
        if ((cur_music==MUSIC_MG_MASH||cur_music==MUSIC_BOARD_METRO) && (music_step%4)==1) {
            Voice *v=&voices[3];
            v->active=1; v->wave=WAVE_NOISE; v->freq=0; v->amp=0.18f*master_music*music_fade;
            v->decay=0.994f; v->life=SFX_RATE*0.06f; v->phase=0; v->env=0; v->attack=0.001f; v->pan=0; v->duty=8;
        }
    }
}

static void mix_block(float *out, int nframes) {
    int i, v;
    music_tick_voices();
    memset(out, 0, nframes * 2 * sizeof(float));
    for (v = 0; v < MAX_VOICES; ++v) {
        Voice *vc = &voices[v];
        if (!vc->active) continue;
        for (i = 0; i < nframes; ++i) {
            float s, l, r;
            if (vc->life >= 0.f) { vc->life -= 1.f; if (vc->life <= 0.f) { vc->active = 0; break; } }
            if (vc->env < 1.f) vc->env += (1.f / (vc->attack * SFX_RATE));
            if (vc->env > 1.f) vc->env = 1.f;
            s = wave_sample(vc) * vc->amp * vc->env;
            vc->amp *= vc->decay;
            if (vc->amp < 0.0008f && v >= MUSIC_VOICES) { vc->active = 0; break; }
            if (vc->wave != WAVE_NOISE) {
                vc->phase += vc->freq / (float)SFX_RATE;
                if (vc->phase >= 1.f) vc->phase -= 1.f;
            }
            l = s * (0.5f - 0.5f * vc->pan);
            r = s * (0.5f + 0.5f * vc->pan);
            out[i*2] += l; out[i*2+1] += r;
        }
    }
    for (i = 0; i < nframes * 2; ++i) {
        float x = out[i];
        if (x > 1.f) x = 1.f; if (x < -1.f) x = -1.f;
        out[i] = x;
    }
}

static float mix_buf[SFX_SAMPLES * 2];

int sfx_init(void) {
    audioPortParam p; int ret;
    memset(voices, 0, sizeof(voices));
    memset(&sfx_cfg, 0, sizeof(sfx_cfg));
    sfx_ready = 0; cur_music = MUSIC_NONE; want_music = MUSIC_NONE;
    ret = audioInit();
    if (ret) { LOG_W("audioInit %d - SFX off", ret); return -1; }
    p.numChannels = AUDIO_PORT_2CH; p.numBlocks = SFX_BLOCKS; p.attrib = 0; p.level = 1.0f;
    ret = audioPortOpen(&p, &sfx_port);
    if (ret) { LOG_W("audioPortOpen %d", ret); audioQuit(); return -1; }
    ret = audioGetPortConfig(sfx_port, &sfx_cfg);
    if (ret || !sfx_cfg.audioDataStart) {
        LOG_W("audioGetPortConfig fail"); audioPortClose(sfx_port); audioQuit(); return -1;
    }
    ret = audioPortStart(sfx_port);
    if (ret) { LOG_W("audioPortStart %d", ret); audioPortClose(sfx_port); audioQuit(); return -1; }
    sfx_ready = 1;
    music_set(MUSIC_MENU);
    LOG_I("SFX+BGM ready (port %u)", sfx_port);
    return 0;
}

void sfx_shutdown(void) {
    if (!sfx_ready) return;
    audioPortStop(sfx_port); audioPortClose(sfx_port); audioQuit();
    sfx_ready = 0;
}

void sfx_update(void) {
    u32 i, b, nblocks; f32 *buf; u64 base, read_idx;
    if (!sfx_ready || !sfx_cfg.audioDataStart) return;
    base = (u64)sfx_cfg.audioDataStart;
    nblocks = sfx_cfg.numBlocks ? sfx_cfg.numBlocks : SFX_BLOCKS;
    read_idx = *(u64 *)(uintptr_t)(base - 8);
    b = (u32)((read_idx + 1) % nblocks);
    buf = (f32 *)(uintptr_t)(base + (u64)b * (u64)sfx_cfg.channelCount * (u64)SFX_SAMPLES * sizeof(f32));
    mix_block(mix_buf, SFX_SAMPLES);
    for (i = 0; i < SFX_SAMPLES; ++i) {
        buf[i*2+0] = mix_buf[i*2+0];
        buf[i*2+1] = mix_buf[i*2+1];
    }
}

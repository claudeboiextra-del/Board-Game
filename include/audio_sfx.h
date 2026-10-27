#ifndef AUDIO_SFX_H
#define AUDIO_SFX_H

#include <stdint.h>

/* -------- SFX bank -------- */
typedef enum {
    SFX_CLICK = 0,
    SFX_COIN,
    SFX_STAR,
    SFX_FAIL,
    SFX_WIN,
    SFX_HOP,
    SFX_LAND,
    SFX_DICE,
    SFX_COUNT_FLASH,
    SFX_MG_START,
    SFX_MG_GO,
    SFX_BUZZ,
    SFX_POWER,
    SFX_WHOOSH,
    SFX_COUNT
} SfxId;

/* -------- Music tracks (procedural loops) -------- */
typedef enum {
    MUSIC_NONE = 0,
    MUSIC_MENU,
    MUSIC_BOARD_VISUAL,
    MUSIC_BOARD_GEO,
    MUSIC_BOARD_METRO,
    MUSIC_MG_MASH,
    MUSIC_MG_TIMING,
    MUSIC_MG_REACT,
    MUSIC_MG_RHYTHM,
    MUSIC_RESULTS,
    MUSIC_COUNT
} MusicId;

int  sfx_init(void);
void sfx_shutdown(void);
void sfx_update(void);
void sfx_play(SfxId id);

void music_set(MusicId id);
MusicId music_current(void);

#endif /* AUDIO_SFX_H */

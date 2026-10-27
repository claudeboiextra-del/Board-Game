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
    MUSIC_MENU,           /* title / setup / board select */
    MUSIC_BOARD_VISUAL,   /* bright party board */
    MUSIC_BOARD_GEO,      /* earthy adventure board */
    MUSIC_BOARD_METRO,    /* urban electronic board */
    MUSIC_MG_MASH,        /* mash / hold / hopper - driving pulse */
    MUSIC_MG_TIMING,      /* timing / zone / catch - tense arcade */
    MUSIC_MG_REACT,       /* reaction / avoid - urgent stingers */
    MUSIC_MG_RHYTHM,      /* rhythm / simon / count - beat memory */
    MUSIC_RESULTS,        /* end-of-match fanfare bed */
    MUSIC_COUNT
} MusicId;

int  sfx_init(void);
void sfx_shutdown(void);
void sfx_play(SfxId id);
void sfx_update(void);

void music_set(MusicId id);
MusicId music_current(void);
void music_stop(void);

#endif /* AUDIO_SFX_H */

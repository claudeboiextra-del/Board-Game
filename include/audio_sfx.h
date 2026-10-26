#ifndef AUDIO_SFX_H
#define AUDIO_SFX_H

#include <stdint.h>

typedef enum {
    SFX_NONE = 0,
    SFX_CLICK,      /* menu / confirm */
    SFX_DICE,       /* dice roll */
    SFX_HOP,        /* step on path */
    SFX_LAND,       /* land on space */
    SFX_COIN,       /* coin gain */
    SFX_STAR,       /* star / celebrate */
    SFX_FAIL,       /* coin loss / false start */
    SFX_WIN,        /* minigame / match win */
    SFX_COUNT_FLASH /* count minigame flash */
} SfxId;

int  sfx_init(void);
void sfx_shutdown(void);
void sfx_update(void);          /* fill audio ring – call every frame */
void sfx_play(SfxId id);

#endif /* AUDIO_SFX_H */

/* Board Game - multi-pad (player i → port i), 9 MGs, 3 boards */
#include "game.h"
#include "logger.h"
#include "audio_sfx.h"
#include <string.h>
#include <stdio.h>

/* ---------------- internal helpers ---------------- */

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Simple deterministic PRNG (xorshift) */
static u32 rng_state = 0xA5A5F00Du;
static u32 rng_next(void) {
    u32 x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}
static int rng_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(rng_next() % (u32)(hi - lo + 1));
}

/* ---------------- board data (extern from boards.c) ---------------- */
extern const BoardDef board_defs[BOARD_COUNT];

/* ---------------- MG type metadata ---------------- */
static const char *mg_names[MG_TYPE_COUNT] = {
    "Button Mash",
    "Timing Stop",
    "Reaction",
    "Hold Steady",
    "Catch Falling",
    "Zone Control",
    "Rhythm Tap",
    "Hopper Jump",
    "Memory Match",
    "Balance",
    "Sprint"
};

/* category for music selection */
enum { MG_CAT_MASH = 0, MG_CAT_TIMING, MG_CAT_REACT, MG_CAT_RHYTHM };
static int mg_category(MgType t) {
    switch (t) {
    case MG_MASH: case MG_HOPPER: case MG_SPRINT: return MG_CAT_MASH;
    case MG_TIMING: case MG_HOLD: case MG_CATCH: case MG_BALANCE: return MG_CAT_TIMING;
    case MG_REACT: case MG_MEMORY: return MG_CAT_REACT;
    case MG_ZONE: case MG_RHYTHM: return MG_CAT_RHYTHM;
    default: return MG_CAT_MASH;
    }
}

/* which MGs run simultaneous (all players at once) */
static int mg_is_simul(MgType t) {
    switch (t) {
    case MG_MASH: case MG_ZONE: case MG_RHYTHM: case MG_HOPPER: case MG_SPRINT:
        return 1;
    default:
        return 0;
    }
}

/* ---------------- game init / reset ---------------- */

void game_init(GameContext *g) {
    memset(g, 0, sizeof(*g));
    g->state = GS_MENU;
    g->menu_sel = 0;
    g->board_idx = 0;
    g->player_count = 4;
    g->turn = 0;
    g->star_cost = 20;
    g->max_turns = 20;
    g->mg_type = MG_MASH;
    g->mg_timer = 0;
    g->mg_phase = MG_INTRO;
    LOG_I("game_init");
}

void game_reset_players(GameContext *g) {
    int i;
    for (i = 0; i < MAX_PLAYERS; i++) {
        g->players[i].coins = 10;
        g->players[i].stars = 0;
        g->players[i].pos = 0;
        g->players[i].item = ITEM_NONE;
        g->players[i].alive = (i < g->player_count) ? 1 : 0;
        g->players[i].mg_score = 0;
        g->players[i].mg_done = 0;
    }
}

/* ---------------- dice / movement ---------------- */

static int roll_dice(void) {
    return rng_range(1, 6);
}

static void apply_space(GameContext *g, int p) {
    Player *pl = &g->players[p];
    const BoardDef *b = &board_defs[g->board_idx];
    SpaceType st = b->spaces[pl->pos % b->space_count];
    switch (st) {
    case SPACE_BLUE:
        pl->coins += 3;
        sfx_play(SFX_COIN);
        break;
    case SPACE_RED:
        pl->coins = clampi(pl->coins - 3, 0, 999);
        sfx_play(SFX_HURT);
        break;
    case SPACE_STAR:
        if (pl->coins >= g->star_cost) {
            pl->coins -= g->star_cost;
            pl->stars++;
            sfx_play(SFX_STAR);
        }
        break;
    case SPACE_ITEM:
        pl->item = (ItemType)rng_range(ITEM_MUSHROOM, ITEM_COUNT - 1);
        sfx_play(SFX_ITEM);
        break;
    case SPACE_EVENT:
        /* simple event: +5 or -5 coins */
        if (rng_next() & 1) {
            pl->coins += 5;
            sfx_play(SFX_COIN);
        } else {
            pl->coins = clampi(pl->coins - 5, 0, 999);
            sfx_play(SFX_HURT);
        }
        break;
    case SPACE_MINIGAME:
        g->pending_mg = 1;
        break;
    default:
        break;
    }
}

static void start_minigame(GameContext *g) {
    int i;
    g->state = GS_MINIGAME;
    g->mg_phase = MG_INTRO;
    g->mg_timer = 90; /* ~1.5s intro */
    g->mg_type = (MgType)rng_range(0, MG_TYPE_COUNT - 1);
    for (i = 0; i < MAX_PLAYERS; i++) {
        g->players[i].mg_score = 0;
        g->players[i].mg_done = 0;
        g->mg_p_meter[i] = 0.0f;
        g->mg_latched[i] = 0;
    }
    g->mg_seq_player = 0;
    /* pick music by category */
    switch (mg_category(g->mg_type)) {
    case MG_CAT_MASH: music_set(MUSIC_MG_MASH); break;
    case MG_CAT_TIMING: music_set(MUSIC_MG_TIMING); break;
    case MG_CAT_REACT: music_set(MUSIC_MG_REACT); break;
    case MG_CAT_RHYTHM: music_set(MUSIC_MG_RHYTHM); break;
    }
    sfx_play(SFX_MG_START);
    LOG_I("start_minigame type=%d", (int)g->mg_type);
}

/* ---------------- MG play logic ---------------- */

static void mg_award_and_finish(GameContext *g) {
    int i, best = -1, best_score = -1;
    int winners[MAX_PLAYERS];
    int nw = 0;
    for (i = 0; i < g->player_count; i++) {
        if (g->players[i].mg_score > best_score) {
            best_score = g->players[i].mg_score;
            best = i;
        }
    }
    for (i = 0; i < g->player_count; i++) {
        if (g->players[i].mg_score == best_score && best_score > 0) {
            winners[nw++] = i;
        }
    }
    if (nw == 0) {
        /* no one scored — give participation */
        for (i = 0; i < g->player_count; i++) {
            g->players[i].coins += 2;
        }
    } else {
        int share = 10 / nw;
        if (share < 3) share = 3;
        for (i = 0; i < nw; i++) {
            g->players[winners[i]].coins += share;
        }
    }
    g->mg_phase = MG_RESULT;
    g->mg_timer = 120;
    sfx_play(SFX_MG_WIN);
    music_set(MUSIC_BOARD); /* back toward board theme after result */
}

static void mg_update_simul(GameContext *g, const u32 *pressed, const u32 *held) {
    int i;
    int all_done = 1;
    float dt = 1.0f / 60.0f;

    switch (g->mg_type) {
    case MG_MASH:
        for (i = 0; i < g->player_count; i++) {
            if (pressed[i] & (PAD_CROSS | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE)) {
                g->players[i].mg_score++;
                g->mg_p_meter[i] = clampf(g->mg_p_meter[i] + 0.08f, 0.0f, 1.0f);
                sfx_play(SFX_MG_HIT);
            } else {
                g->mg_p_meter[i] = clampf(g->mg_p_meter[i] - 0.02f, 0.0f, 1.0f);
            }
        }
        break;

    case MG_ZONE:
        for (i = 0; i < g->player_count; i++) {
            /* stick-like: hold d-pad toward a target zone that moves */
            int target = ((g->mg_timer / 30) + i) % 4;
            u32 need = 0;
            if (target == 0) need = PAD_UP;
            else if (target == 1) need = PAD_RIGHT;
            else if (target == 2) need = PAD_DOWN;
            else need = PAD_LEFT;
            if (held[i] & need) {
                g->players[i].mg_score++;
                g->mg_p_meter[i] = clampf(g->mg_p_meter[i] + 0.05f, 0.0f, 1.0f);
            } else {
                g->mg_p_meter[i] = clampf(g->mg_p_meter[i] - 0.03f, 0.0f, 1.0f);
            }
        }
        break;

    case MG_RHYTHM:
        /* beat every 20 frames; press on beat */
        {
            int beat = (g->mg_timer % 20) == 0;
            for (i = 0; i < g->player_count; i++) {
                if (beat && (pressed[i] & (PAD_CROSS | PAD_CIRCLE))) {
                    g->players[i].mg_score += 2;
                    g->mg_p_meter[i] = 1.0f;
                    sfx_play(SFX_MG_HIT);
                } else if (pressed[i] & (PAD_CROSS | PAD_CIRCLE)) {
                    /* early/late miss */
                    g->mg_p_meter[i] = clampf(g->mg_p_meter[i] - 0.2f, 0.0f, 1.0f);
                } else {
                    g->mg_p_meter[i] = clampf(g->mg_p_meter[i] - 0.01f, 0.0f, 1.0f);
                }
            }
        }
        break;

    case MG_HOPPER:
        for (i = 0; i < g->player_count; i++) {
            if (pressed[i] & PAD_CROSS) {
                g->players[i].mg_score++;
                g->mg_p_meter[i] = 1.0f;
                sfx_play(SFX_MG_JUMP);
            } else {
                g->mg_p_meter[i] = clampf(g->mg_p_meter[i] - 0.04f, 0.0f, 1.0f);
            }
        }
        break;

    case MG_SPRINT:
        for (i = 0; i < g->player_count; i++) {
            if (pressed[i] & (PAD_LEFT | PAD_RIGHT)) {
                g->players[i].mg_score++;
                g->mg_p_meter[i] = clampf(g->mg_p_meter[i] + 0.1f, 0.0f, 1.0f);
                sfx_play(SFX_MG_HIT);
            } else {
                g->mg_p_meter[i] = clampf(g->mg_p_meter[i] - 0.02f, 0.0f, 1.0f);
            }
        }
        break;

    default:
        break;
    }

    g->mg_timer--;
    if (g->mg_timer <= 0) {
        mg_award_and_finish(g);
    }
}

static void mg_update_sequential(GameContext *g, const u32 *pressed, const u32 *held) {
    int p = g->mg_seq_player;
    Player *pl = &g->players[p];

    if (pl->mg_done) {
        /* advance to next */
        g->mg_seq_player++;
        if (g->mg_seq_player >= g->player_count) {
            mg_award_and_finish(g);
            return;
        }
        g->mg_timer = 180; /* per-player window */
        return;
    }

    switch (g->mg_type) {
    case MG_TIMING:
        /* stop the moving bar */
        g->mg_p_meter[p] += 0.03f;
        if (g->mg_p_meter[p] > 1.0f) g->mg_p_meter[p] = 0.0f;
        if (pressed[p] & PAD_CROSS) {
            float err = g->mg_p_meter[p] - 0.5f;
            if (err < 0) err = -err;
            int sc = (int)((1.0f - err * 2.0f) * 100.0f);
            if (sc < 0) sc = 0;
            pl->mg_score = sc;
            pl->mg_done = 1;
            sfx_play(sc > 70 ? SFX_MG_HIT : SFX_MG_MISS);
        }
        break;

    case MG_REACT:
        if (!g->mg_latched[p]) {
            /* wait random then flash */
            if (g->mg_timer < 60) {
                g->mg_latched[p] = 1;
                g->mg_p_meter[p] = 1.0f;
                sfx_play(SFX_MG_START);
            }
        } else {
            if (pressed[p] & PAD_CROSS) {
                int sc = g->mg_timer; /* higher remaining = faster */
                pl->mg_score = sc;
                pl->mg_done = 1;
                sfx_play(SFX_MG_HIT);
            }
        }
        break;

    case MG_HOLD:
        if (held[p] & PAD_CROSS) {
            pl->mg_score++;
            g->mg_p_meter[p] = clampf(g->mg_p_meter[p] + 0.02f, 0.0f, 1.0f);
        } else {
            g->mg_p_meter[p] = clampf(g->mg_p_meter[p] - 0.05f, 0.0f, 1.0f);
        }
        break;

    case MG_CATCH:
        /* falling object; press when meter near 0.7 */
        g->mg_p_meter[p] += 0.025f;
        if (g->mg_p_meter[p] > 1.0f) {
            g->mg_p_meter[p] = 0.0f;
            /* missed this one */
        }
        if (pressed[p] & PAD_CROSS) {
            float err = g->mg_p_meter[p] - 0.7f;
            if (err < 0) err = -err;
            if (err < 0.12f) {
                pl->mg_score += 5;
                sfx_play(SFX_MG_HIT);
            } else {
                sfx_play(SFX_MG_MISS);
            }
            g->mg_p_meter[p] = 0.0f;
        }
        break;

    case MG_MEMORY:
        /* show pattern then input */
        if (g->mg_timer > 90) {
            /* show phase */
            g->mg_p_meter[p] = (float)((g->mg_timer / 15) % 4) / 3.0f;
        } else {
            if (pressed[p] & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_CROSS)) {
                pl->mg_score += 3;
                sfx_play(SFX_MG_HIT);
            }
        }
        break;

    case MG_BALANCE:
        /* keep centered with left/right */
        {
            static float bal[4];
            if (g->mg_timer == 179) bal[p] = 0.5f;
            if (held[p] & PAD_LEFT) bal[p] -= 0.02f;
            if (held[p] & PAD_RIGHT) bal[p] += 0.02f;
            bal[p] += (rng_next() % 7 - 3) * 0.002f;
            bal[p] = clampf(bal[p], 0.0f, 1.0f);
            g->mg_p_meter[p] = bal[p];
            if (bal[p] > 0.35f && bal[p] < 0.65f) {
                pl->mg_score++;
            }
        }
        break;

    default:
        pl->mg_done = 1;
        break;
    }

    g->mg_timer--;
    if (g->mg_timer <= 0) {
        pl->mg_done = 1;
        g->mg_seq_player++;
        if (g->mg_seq_player >= g->player_count) {
            mg_award_and_finish(g);
        } else {
            g->mg_timer = 180;
        }
    }
}

static void mg_update(GameContext *g, const u32 *pressed, const u32 *held) {
    if (g->mg_phase == MG_INTRO) {
        g->mg_timer--;
        if (g->mg_timer <= 0) {
            g->mg_phase = MG_PLAY;
            if (mg_is_simul(g->mg_type)) {
                g->mg_timer = 300; /* 5s simul */
            } else {
                g->mg_timer = 180;
                g->mg_seq_player = 0;
            }
            sfx_play(SFX_MG_GO);
        }
        return;
    }
    if (g->mg_phase == MG_RESULT) {
        g->mg_timer--;
        if (g->mg_timer <= 0) {
            g->state = GS_BOARD;
            g->pending_mg = 0;
            music_set(MUSIC_BOARD);
        }
        return;
    }
    /* MG_PLAY */
    if (mg_is_simul(g->mg_type)) {
        mg_update_simul(g, pressed, held);
    } else {
        mg_update_sequential(g, pressed, held);
    }
}

/* ---------------- main game update ---------------- */

void game_update(GameContext *g, const u32 *pressed, const u32 *held) {
    int i;
    switch (g->state) {
    case GS_MENU:
        if (pressed[0] & PAD_UP) {
            g->menu_sel = (g->menu_sel + 2) % 3;
            sfx_play(SFX_CURSOR);
        }
        if (pressed[0] & PAD_DOWN) {
            g->menu_sel = (g->menu_sel + 1) % 3;
            sfx_play(SFX_CURSOR);
        }
        if (pressed[0] & PAD_LEFT) {
            if (g->menu_sel == 1) g->board_idx = (g->board_idx + BOARD_COUNT - 1) % BOARD_COUNT;
            if (g->menu_sel == 2) g->player_count = clampi(g->player_count - 1, 1, 4);
            sfx_play(SFX_CURSOR);
        }
        if (pressed[0] & PAD_RIGHT) {
            if (g->menu_sel == 1) g->board_idx = (g->board_idx + 1) % BOARD_COUNT;
            if (g->menu_sel == 2) g->player_count = clampi(g->player_count + 1, 1, 4);
            sfx_play(SFX_CURSOR);
        }
        if (pressed[0] & PAD_CROSS) {
            if (g->menu_sel == 0) {
                /* start */
                game_reset_players(g);
                g->turn = 0;
                g->state = GS_BOARD;
                g->board_phase = BP_ROLL;
                music_set(MUSIC_BOARD);
                sfx_play(SFX_CONFIRM);
            }
        }
        if (music_current() != MUSIC_MENU) music_set(MUSIC_MENU);
        break;

    case GS_BOARD:
        if (g->pending_mg) {
            start_minigame(g);
            break;
        }
        switch (g->board_phase) {
        case BP_ROLL:
            if (pressed[g->turn] & PAD_CROSS) {
                g->dice = roll_dice();
                g->board_phase = BP_MOVE;
                g->move_left = g->dice;
                sfx_play(SFX_DICE);
            }
            /* item use */
            if (pressed[g->turn] & PAD_SQUARE && g->players[g->turn].item != ITEM_NONE) {
                ItemType it = g->players[g->turn].item;
                g->players[g->turn].item = ITEM_NONE;
                if (it == ITEM_MUSHROOM) {
                    g->dice = 1 + roll_dice(); /* extra */
                    g->board_phase = BP_MOVE;
                    g->move_left = g->dice;
                    sfx_play(SFX_ITEM);
                } else if (it == ITEM_GOLDEN) {
                    g->players[g->turn].coins += 10;
                    sfx_play(SFX_COIN);
                }
            }
            break;
        case BP_MOVE:
            if (g->move_left > 0) {
                g->players[g->turn].pos++;
                g->move_left--;
                sfx_play(SFX_STEP);
                if (g->move_left == 0) {
                    apply_space(g, g->turn);
                    g->board_phase = BP_ENDTURN;
                    g->end_timer = 45;
                }
            }
            break;
        case BP_ENDTURN:
            g->end_timer--;
            if (g->end_timer <= 0) {
                g->turn = (g->turn + 1) % g->player_count;
                if (g->turn == 0) {
                    g->turn_count++;
                    if (g->turn_count >= g->max_turns) {
                        g->state = GS_GAMEOVER;
                        music_set(MUSIC_MENU);
                        sfx_play(SFX_STAR);
                        break;
                    }
                }
                g->board_phase = BP_ROLL;
            }
            break;
        }
        if (music_current() != MUSIC_BOARD) music_set(MUSIC_BOARD);
        break;

    case GS_MINIGAME:
        mg_update(g, pressed, held);
        break;

    case GS_GAMEOVER:
        if (pressed[0] & PAD_CROSS) {
            g->state = GS_MENU;
            music_set(MUSIC_MENU);
            sfx_play(SFX_CONFIRM);
        }
        break;
    }

    /* keep music voice alive */
    music_sync(g);
}

void game_shutdown(GameContext *g) { LOG_I("game_shutdown"); (void)g; }

/* Board Game - game logic: 3 boards, 11 minigames, per-pad input, 3D camera */
#include "game.h"
#include "logger.h"
#include "audio_sfx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static Board g_boards[BOARD_COUNT];

#define HOP_FRAMES 6
#define LAND_HOLD 20
#define DICE_CPU_DELAY 20
#define ROUND_HOLD 60
#define ITEM_CPU_DELAY 12
#define MG_RESULT_HOLD 200
#define MG_RESULT_SKIP 30
#define INTRO_IGNORE 15
#define MG_MASH_TIME 180
#define MG_TIMING_TIME 150
#define MG_HOLD_MAX 170
#define MG_COUNT_INPUT 150
#define MG_ZONE_MAX 200
#define MG_RHYTHM_T0 30
#define MG_RHYTHM_GAP 26
#define MG_RHYTHM_TOL 5
#define MG_CATCH_MAX 420
#define MG_SIMON_INPUT 360
#define MG_HOPPER_MAX 300
#define MG_LOCK_FRAMES 28

static const int MG_PAYOUT[4] = {18, 12, 6, 3};
static const uint32_t SIMON_BTN[3] = {0x10, 0x20, 0x40};

static const MgInfo MG_INFO[MG_TYPE_COUNT] = {
    {"BUTTON MASH", "MASH TO RAISE YOUR TOWER THE HIGHEST.",
     "THE TOWER SLOWLY SINKS SO NEVER STOP!", "X: MASH AS FAST AS YOU CAN", true},
    {"PERFECT TIMING", "STOP THE BAR IN THE MIDDLE.",
     "ONE TRY ONLY. CLOSER TO THE CENTER WINS.", "X: STOP THE BAR", false},
    {"QUICK DRAW", "PRESS X THE MOMENT THE LIGHT TURNS GREEN.",
     "TOO EARLY IS A FOUL. FASTER SCORES MORE.", "X: REACT", false},
    {"STEADY HOLD", "RELEASE X WHEN THE GAUGE HITS THE LINE.",
     "THE GAUGE FILLS WHILE YOU HOLD. CLOSEST WINS.", "HOLD X, THEN LET GO", false},
    {"FLASH COUNT", "COUNT HOW MANY TIMES THE LIGHT FLASHES.",
     "THEN TAP X THAT MANY TIMES AND PRESS O.", "X: ADD ONE   O: SUBMIT", false},
    {"RED LIGHT GREEN LIGHT", "PRESS X WHEN THE LIGHT TURNS GREEN.",
     "PRESS ON RED AND YOU FAIL. EARLIER IS BETTER.", "X: GO", false},
    {"STAY IN THE ZONE", "KEEP THE BAR INSIDE THE MOVING GREEN ZONE.",
     "HOLD X TO SLIDE RIGHT. RELEASE TO SLIDE LEFT.", "HOLD X / RELEASE", false},
    {"RHYTHM TAP", "TAP X EXACTLY ON THE BEAT.",
     "WATCH THE RING SHRINK. WILD TAPPING LOSES POINTS.", "X: TAP ON THE BEAT", false},
    {"ORB CATCH", "CATCH 4 ORBS IN YOUR BASKET.",
     "MOVE UNDER THE ORB BEFORE IT HITS THE FLOOR.", "SQ OR LEFT: LEFT   O OR RIGHT: RIGHT", false},
    {"COLOR MEMORY", "REPEAT THE COLOR PATTERN.",
     "WATCH THE CUBES LIGHT UP THEN COPY THE ORDER.", "X: GREEN   O: BLUE   SQ: RED", false},
    {"LIGHT HOPPER", "CLIMB YOUR PILLAR TO THE TOP.",
     "MASH X ON GREEN. FREEZE ON RED OR YOU SLIP!", "X: HOP (GREEN ONLY)", true}
};

const MgInfo *mg_info(MiniGameType t)
{
    if ((int)t < 0 || (int)t >= MG_TYPE_COUNT) t = MG_MASH;
    return &MG_INFO[(int)t];
}

static int rand_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (rand() % (hi - lo + 1));
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void spawn_popup(GameContext *g, float wx, float wy, float wz, int value, int kind) {
    float sx, sy;
    if (!render_project(g, wx, wy, wz, &sx, &sy)) return;
    g->popup.active = 1; g->popup.x = sx; g->popup.y = sy;
    g->popup.value = value; g->popup.kind = kind; g->popup.life = 1.1f;
}
static void spawn_fx(GameContext *g, float wx, float wy, float wz, uint32_t color, int count) {
    int i, n = 0; float sx, sy;
    if (!render_project(g, wx, wy, wz, &sx, &sy)) return;
    for (i = 0; i < MAX_FX && n < count; ++i) {
        if (g->fx[i].active) continue;
        g->fx[i].active = 1;
        g->fx[i].x = sx + (float)rand_range(-6, 6);
        g->fx[i].y = sy + (float)rand_range(-4, 4);
        g->fx[i].vx = (float)rand_range(-40, 40) * 0.1f;
        g->fx[i].vy = (float)rand_range(-60, -10) * 0.1f;
        g->fx[i].life = 0.35f + (float)rand_range(0, 20) * 0.02f;
        g->fx[i].color = color; n++;
    }
}
static void node_world(const GameContext *g, int node, float *x, float *z) {
    const BoardNode *n = &g->board->nodes[node];
    *x = (n->x - 0.5f) * WORLD_SX; *z = (n->y - 0.5f) * WORLD_SZ;
}
static void slot_target(const GameContext *g, int pi, float *x, float *z) {
    static const float o[4][2] = {{-0.72f,-0.55f},{0.72f,-0.55f},{-0.72f,0.55f},{0.72f,0.55f}};
    node_world(g, g->players[pi].node, x, z);
    *x += o[pi & 3][0]; *z += o[pi & 3][1];
}
static void snap_player(GameContext *g, int pi) {
    float x, z; slot_target(g, pi, &x, &z);
    g->players[pi].disp_x = x; g->players[pi].disp_y = z; g->players[pi].hop_t = 1.0f;
}
static int give_item(Player *p, ItemType it) {
    if (p->item_count >= MAX_ITEMS) return 0;
    p->items[p->item_count++] = it; return 1;
}
static ItemType take_item(Player *p, int slot) {
    ItemType it; int i;
    if (slot < 0 || slot >= p->item_count) return ITEM_NONE;
    it = p->items[slot];
    for (i = slot; i < p->item_count - 1; ++i) p->items[i] = p->items[i + 1];
    p->item_count--; return it;
}
static ItemType random_item(void) {
    static const ItemType pool[] = { ITEM_DOUBLE_DICE, ITEM_BOOST, ITEM_STEAL, ITEM_SHIELD, ITEM_SWAP, ITEM_BOOST };
    return pool[rand_range(0, 5)];
}
static int item_price(ItemType it) {
    switch (it) {
    case ITEM_DOUBLE_DICE: return 6;
    case ITEM_BOOST: return 5;
    case ITEM_STEAL: return 8;
    case ITEM_SHIELD: return 6;
    case ITEM_SWAP: return 7;
    default: return 99;
    }
}
static int find_last_place(GameContext *g) {
    int best = 0, i;
    for (i = 1; i < g->player_count; ++i) {
        if (g->players[i].stars < g->players[best].stars ||
            (g->players[i].stars == g->players[best].stars && g->players[i].coins < g->players[best].coins))
            best = i;
    }
    return best;
}
static int star_cost_for(GameContext *g, Player *p) {
    int last = find_last_place(g);
    if (p->id == last && g->player_count > 1) return 15;
    return 20;
}
static void shop_open(GameContext *g, Player *p) {
    ItemType all[5] = { ITEM_DOUBLE_DICE, ITEM_BOOST, ITEM_STEAL, ITEM_SHIELD, ITEM_SWAP };
    int i;
    for (i = 4; i > 0; --i) { int j = rand_range(0, i); ItemType t = all[i]; all[i] = all[j]; all[j] = t; }
    for (i = 0; i < 3; ++i) { g->shop_offer[i] = all[i]; g->shop_cost[i] = item_price(all[i]); }
    g->shop_star_cost = star_cost_for(g, p);
    g->item_cursor = 0;
}

/* ------------------------------------------------------------------ */
/* music                                                               */
/* ------------------------------------------------------------------ */
static void music_sync(GameContext *g)
{
    MusicId want = MUSIC_MENU;
    GameState st = g->state;
    if (st == STATE_PAUSE) return;
    switch (st) {
    case STATE_BOOT: case STATE_TITLE: case STATE_SETUP: case STATE_BOARD_SELECT:
    case STATE_EXIT: want = MUSIC_MENU; break;
    case STATE_RESULTS: want = MUSIC_RESULTS; break;
    case STATE_MG_INTRO: case STATE_MG_PLAY: case STATE_MG_RESULT:
        switch (g->mg_type) {
        case MG_MASH: case MG_HOLD: case MG_HOPPER: want = MUSIC_MG_MASH; break;
        case MG_TIMING: case MG_ZONE: case MG_CATCH: want = MUSIC_MG_TIMING; break;
        case MG_REACTION: case MG_AVOID: want = MUSIC_MG_REACT; break;
        default: want = MUSIC_MG_RHYTHM; break;
        }
        break;
    default:
        want = (g->board_idx == 0) ? MUSIC_BOARD_VISUAL : (g->board_idx == 1 ? MUSIC_BOARD_GEO : MUSIC_BOARD_METRO);
        break;
    }
    music_set(want);
}

/* ------------------------------------------------------------------ */
/* camera                                                              */
/* ------------------------------------------------------------------ */
static void cam_update(GameContext *g)
{
    float tp[3], tl[3], k = 0.07f; int i;
    GameState st = g->state;
    float t = g->anim_t;
    if (st == STATE_PAUSE) st = g->pause_from;
    switch (st) {
    case STATE_TITLE:
        tp[0] = sinf(t * 0.5f) * 3.0f; tp[1] = 5.2f; tp[2] = 22.0f;
        tl[0] = 0; tl[1] = 2.0f; tl[2] = 0; break;
    case STATE_BOOT: case STATE_SETUP: case STATE_BOARD_SELECT: case STATE_MATCH: case STATE_EXIT:
        tp[0] = sinf(t * 0.35f) * 2.0f; tp[1] = 38.0f; tp[2] = 28.0f;
        tl[0] = 0; tl[1] = 0; tl[2] = 0.5f; break;
    case STATE_MG_INTRO: case STATE_MG_PLAY: case STATE_MG_RESULT:
        tp[0] = 0; tp[1] = 7.5f; tp[2] = 18.0f;
        tl[0] = 0; tl[1] = 3.2f; tl[2] = 0; k = 0.12f; break;
    case STATE_RESULTS:
        tp[0] = 0; tp[1] = 6.0f; tp[2] = 18.0f;
        tl[0] = 0; tl[1] = 2.4f; tl[2] = 0; break;
    default: {
        const Player *p = &g->players[g->current_player];
        tl[0] = p->disp_x; tl[1] = 0; tl[2] = p->disp_y;
        tp[0] = tl[0]; tp[1] = 15.0f; tp[2] = tl[2] + 13.0f;
        k = 0.08f; break; }
    }
    if (g->frame <= 1) k = 1.0f;
    for (i = 0; i < 3; ++i) {
        g->cam_pos[i] += (tp[i] - g->cam_pos[i]) * k;
        g->cam_look[i] += (tl[i] - g->cam_look[i]) * k;
    }
    g->cam_fov = 45.0f;
}

/* ------------------------------------------------------------------ */
/* minigame setup                                                      */
/* ------------------------------------------------------------------ */
static void mg_gen_params(GameContext *g)
{
    int i;
    g->mg_target = 40 + rand_range(0, 25);
    g->mg_go_at = 30 + rand_range(0, 40);
    g->mg_beats_total = 0; g->mg_seq_len = 0;
    switch (g->mg_type) {
    case MG_RHYTHM: g->mg_target = MG_RHYTHM_TOL; g->mg_beats_total = 6; break;
    case MG_CATCH:  g->mg_beats_total = 4; for (i = 0; i < 8; ++i) g->mg_seq[i] = rand_range(15, 85); break;
    case MG_COUNT:  g->mg_target = 3 + rand_range(0, 3); break;
    case MG_AVOID:  g->mg_go_at = 25 + rand_range(0, 35); g->mg_target = g->mg_go_at + 40 + rand_range(0, 25); break;
    case MG_SIMON:  g->mg_seq_len = 5; for (i = 0; i < 8; ++i) g->mg_seq[i] = rand_range(0, 2); break;
    case MG_ZONE:   g->mg_target = rand_range(0, 628); break;
    default: break;
    }
}
static void mg_reset_play_vars(GameContext *g)
{
    int i;
    g->mg_timer = 0; g->mg_meter = 0; g->mg_marker = 0.12f; g->mg_marker_dir = 1;
    g->mg_pressed = 0; g->mg_human_score = 0; g->mg_hold_frames = 0;
    g->mg_zone_hits = 0; g->mg_flash_count = 0; g->mg_phase = 0;
    g->mg_beats_hit = 0; g->mg_misses = 0; g->mg_fall_y = 0.f; g->mg_fall_x = 0.5f;
    g->mg_zone_c = 0.5f; g->mg_zone_w = 0.14f;
    g->mg_phase_start = 0; g->mg_input_idx = 0; g->mg_beat_mask = 0;
    g->mg_prompt = 0; g->mg_safe = 1; g->mg_next_flash = 16;
    g->mg_locked = 0; g->mg_lock_t = 0; g->mg_hop_swap = 30 + rand_range(0, 25);
    for (i = 0; i < MAX_PLAYERS; ++i) {
        float base = (g->players[i].diff == DIFF_HARD) ? 0.17f : (g->players[i].diff == DIFF_EASY ? 0.09f : 0.13f);
        g->mg_p_meter[i] = 0; g->mg_hop_flash[i] = 0; g->mg_last_press[i] = 0;
        g->mg_p_latched[i] = (int)(base * (0.8f + 0.04f * (float)rand_range(0, 10)) * 1000.0f);
    }
    switch (g->mg_type) {
    case MG_MASH: g->mg_time_max = MG_MASH_TIME; break;
    case MG_HOPPER: g->mg_time_max = MG_HOPPER_MAX; break;
    case MG_TIMING: g->mg_time_max = MG_TIMING_TIME; break;
    case MG_HOLD: g->mg_time_max = MG_HOLD_MAX; break;
    case MG_ZONE: g->mg_time_max = MG_ZONE_MAX; break;
    case MG_REACTION: g->mg_time_max = g->mg_go_at + 60; break;
    case MG_AVOID: g->mg_time_max = g->mg_target; break;
    default: g->mg_time_max = 0; break;
    }
    switch (g->mg_type) {
    case MG_RHYTHM: g->mg_next_flash = MG_RHYTHM_T0; g->mg_time_max = MG_RHYTHM_T0 + (g->mg_beats_total - 1) * MG_RHYTHM_GAP + 20; break;
    case MG_CATCH:  g->mg_time_max = MG_CATCH_MAX; g->mg_marker = 0.5f; g->mg_fall_x = (float)g->mg_seq[0] * 0.01f; break;
    case MG_SIMON:  g->mg_next_flash = 30; break;
    default: break;
    }
}
static int cpu_mg_score(Player *p, MiniGameType t) {
    int base = 55, noise = rand_range(-15, 15);
    if (p->diff == DIFF_HARD) base = 78; else if (p->diff == DIFF_EASY) base = 35;
    if (t == MG_HOLD || t == MG_COUNT || t == MG_ZONE || t == MG_RHYTHM || t == MG_CATCH) base += 2;
    base += noise; if (base < 5) base = 5; if (base > 100) base = 100;
    return base;
}
static void mg_rank_and_pay(GameContext *g) {
    int i, j, order[MAX_PLAYERS];
    if (g->mg_awarded) return;
    g->mg_awarded = true;
    for (i = 0; i < g->player_count; ++i) order[i] = i;
    for (i = 0; i < g->player_count - 1; ++i)
        for (j = 0; j < g->player_count - 1 - i; ++j)
            if (g->players[order[j]].mg_score < g->players[order[j + 1]].mg_score) {
                int t = order[j]; order[j] = order[j + 1]; order[j + 1] = t;
            }
    for (i = 0; i < g->player_count; ++i) {
        int pid = order[i], pay = MG_PAYOUT[i < 4 ? i : 3];
        g->players[pid].mg_rank = i;
        g->players[pid].coins += pay;
        g->contest_rewards[pid] = pay;
    }
    if (g->player_count > 0) {
        g->players[order[0]].celebrating = true;
        spawn_fx(g, STAGE_SLOT_X(order[0], g->player_count), 3.5f, 0, 0xFFF1C40F, 14);
    }
    sfx_play(SFX_WIN);
}
static void mg_enter_ready(GameContext *g) {
    int i;
    g->state = STATE_MG_INTRO; g->state_timer = 0;
    g->mg_intro_phase = INTRO_READY; g->mg_intro_t = 0; g->mg_gate_player = -1;
    for (i = 0; i < MAX_PLAYERS; ++i)
        g->mg_ready[i] = (i >= g->player_count) || !g->players[i].is_human;
}
static void mg_begin(GameContext *g) {
    static int last = -1;
    int t, i;
    do { t = rand_range(0, MG_TYPE_COUNT - 1); } while (t == last);
    last = t;
    g->mg_type = (MiniGameType)t; g->mg_simul = mg_info(g->mg_type)->simul;
    g->mg_awarded = false;
    mg_gen_params(g); mg_reset_play_vars(g);
    for (i = 0; i < g->player_count; ++i) {
        g->players[i].mg_score = 0; g->players[i].mg_rank = i; g->contest_rewards[i] = 0;
    }
    g->mg_actor = 0;
    for (i = 0; i < g->player_count; ++i) if (g->players[i].is_human) { g->mg_actor = i; break; }
    mg_enter_ready(g);
    sfx_play(SFX_MG_START);
    LOG_I("Minigame type %d simul %d", (int)g->mg_type, (int)g->mg_simul);
}
static void mg_go_result(GameContext *g) {
    int i;
    if (!g->mg_simul)
        for (i = 0; i < g->player_count; ++i)
            if (!g->players[i].is_human) g->players[i].mg_score = cpu_mg_score(&g->players[i], g->mg_type);
    g->state = STATE_MG_RESULT; g->state_timer = 0;
}
static void mg_finish_play(GameContext *g) {
    int i, next = -1;
    if (g->mg_simul) { mg_go_result(g); return; }
    if (g->mg_actor >= 0 && g->mg_actor < g->player_count && g->players[g->mg_actor].is_human)
        g->players[g->mg_actor].mg_score = g->mg_human_score;
    for (i = g->mg_actor + 1; i < g->player_count; ++i)
        if (g->players[i].is_human) { next = i; break; }
    if (next >= 0) {
        g->mg_actor = next; mg_reset_play_vars(g);
        g->state = STATE_MG_INTRO; g->state_timer = 0;
        g->mg_intro_phase = INTRO_GATE; g->mg_intro_t = 0; g->mg_gate_player = next;
        sfx_play(SFX_CLICK);
        return;
    }
    mg_go_result(g);
}
static void mg_lock(GameContext *g, int score) {
    if (score < 0) score = 0;
    if (score > 100) score = 100;
    g->mg_human_score = score; g->mg_locked = 1; g->mg_lock_t = 0;
}
static void mg_start_play(GameContext *g) {
    mg_reset_play_vars(g);
    g->state = STATE_MG_PLAY; g->state_timer = 0;
}

void game_init(GameContext *g) {
    int i; LOG_I("game_init");
    memset(g, 0, sizeof(*g)); srand((unsigned)time(NULL));
    board_load_visual(&g_boards[0]); board_load_geo(&g_boards[1]); board_load_metro(&g_boards[2]);
    g->state = STATE_TITLE; g->board = &g_boards[0];
    g->player_count = 4; g->human_count = 1; g->max_rounds = 12;
    g->cpu_diff = DIFF_NORMAL; g->setup_cursor = 0; g->team_mode = false;
    g->mg_gate_player = -1;
    for (i = 0; i < MAX_PLAYERS; ++i) {
        g->players[i].id = i; g->players[i].is_human = (i == 0);
        g->players[i].diff = DIFF_NORMAL; g->players[i].coins = 12;
        g->players[i].node = g->board->start_node; g->players[i].color = i;
        g->players[i].pad_id = i;
        g->players[i].team = (i < 2) ? 0 : 1; g->players[i].shadow_s = 1.0f;
        snap_player(g, i);
    }
    g->cam_pos[0] = 0; g->cam_pos[1] = 5.2f; g->cam_pos[2] = 22.0f;
    g->cam_look[1] = 2.0f; g->cam_fov = 45.0f;
    give_item(&g->players[0], ITEM_DOUBLE_DICE);
}
static void relocate_star(GameContext *g) {
    int tries = 0, new_node;
    do { new_node = rand_range(0, g->board->node_count - 1); tries++; }
    while ((g->board->nodes[new_node].type == SPACE_SHOP || g->board->nodes[new_node].type == SPACE_WARP ||
            new_node == g->board->star_node) && tries < 40);
    if (g->board->nodes[g->board->star_node].type == SPACE_STAR)
        g->board->nodes[g->board->star_node].type = SPACE_NORMAL;
    g->board->star_node = new_node;
    g->board->nodes[new_node].type = SPACE_STAR;
}
static int cpu_choose_path(GameContext *g, Player *p, int from) {
    BoardNode *n = &g->board->nodes[from];
    int best, score_best, i;
    if (n->next_count <= 1) return n->next[0];
    best = n->next[0]; score_best = -999;
    for (i = 0; i < n->next_count; ++i) {
        int cand = n->next[i], score = 0;
        SpaceType t = g->board->nodes[cand].type;
        if (t == SPACE_STAR) score += 50;
        if (t == SPACE_PLUS_COIN) score += 12;
        if (t == SPACE_SHOP) score += 8;
        if (t == SPACE_WARP) score += (p->diff == DIFF_HARD) ? -5 : 5;
        if (t == SPACE_HAZARD) score -= 12;
        if (t == SPACE_MINUS_COIN) score -= 15;
        if (t == SPACE_BOWSER) score -= 22;
        if (p->diff == DIFF_HARD) score += rand_range(0, 5);
        else if (p->diff == DIFF_EASY) score += rand_range(-20, 20);
        else score += rand_range(-5, 8);
        if (score > score_best) { score_best = score; best = cand; }
    }
    return best;
}
static void start_hop(GameContext *g, Player *p, int next_node) {
    float x, z; p->node = next_node; p->hop_t = 0; g->hop_cooldown = HOP_FRAMES;
    g->move_remaining--; node_world(g, next_node, &x, &z);
    spawn_fx(g, x, 0.3f, z, 0x88FFFFFF, 3);
    sfx_play(SFX_HOP);
}
static void apply_land(GameContext *g, Player *p) {
    BoardNode *n = &g->board->nodes[p->node];
    float px, pz; int delta, cost, dest;
    const float py = 3.4f;
    node_world(g, p->node, &px, &pz);
    g->last_land_type = (int)n->type; g->land_flash = LAND_HOLD;
    sfx_play(SFX_LAND);
    switch (n->type) {
    case SPACE_PLUS_COIN:
        delta = 3 + rand_range(0, 3); p->coins += delta;
        spawn_popup(g, px, py, pz, delta, 0); spawn_fx(g, px, 1, pz, 0xFF2ECC71, 6); sfx_play(SFX_COIN); break;
    case SPACE_MINUS_COIN:
        if (p->shielded) { p->shielded = false; spawn_popup(g, px, py, pz, 0, 5); spawn_fx(g, px, 1, pz, 0xFF3498DB, 5); }
        else { delta = 3 + rand_range(0, 1); p->coins -= delta; if (p->coins < 0) p->coins = 0;
            spawn_popup(g, px, py, pz, -delta, 2); spawn_fx(g, px, 1, pz, 0xFFE74C3C, 5); sfx_play(SFX_FAIL); } break;
    case SPACE_EVENT:
        if (rand_range(0, 2) == 0 && give_item(p, random_item())) { spawn_popup(g, px, py, pz, 1, 5); spawn_fx(g, px, 1, pz, 0xFF9B59B6, 8); sfx_play(SFX_COIN); }
        else if (rand_range(0, 1)) { p->coins += 6; spawn_popup(g, px, py, pz, 6, 4); sfx_play(SFX_COIN); }
        else { delta = (p->coins > 3) ? 3 : p->coins; p->coins -= delta; spawn_popup(g, px, py, pz, -delta, 4); sfx_play(SFX_FAIL); } break;
    case SPACE_SHOP:
        spawn_popup(g, px, py, pz, 0, 3); shop_open(g, p); g->state = STATE_SHOP; g->state_timer = 0; return;
    case SPACE_STAR:
        cost = star_cost_for(g, p);
        if (p->coins >= cost) { p->coins -= cost; p->stars += 1; spawn_popup(g, px, py, pz, 1, 1);
            spawn_fx(g, px, 2, pz, 0xFFF1C40F, 12); p->celebrating = true; relocate_star(g); sfx_play(SFX_STAR); }
        else { spawn_popup(g, px, py, pz, 0, 2); sfx_play(SFX_FAIL); } break;
    case SPACE_HIDDEN_COIN:
        if (!n->hidden_revealed) { n->hidden_revealed = true; p->coins += 12; spawn_popup(g, px, py, pz, 12, 0); spawn_fx(g, px, 1, pz, 0xFF1ABC9C, 8); sfx_play(SFX_COIN); } break;
    case SPACE_HIDDEN_STAR:
        if (!n->hidden_revealed) { n->hidden_revealed = true; p->stars += 1; spawn_popup(g, px, py, pz, 1, 1); spawn_fx(g, px, 2, pz, 0xFFF1C40F, 10); p->celebrating = true; sfx_play(SFX_STAR); } break;
    case SPACE_WARP:
        dest = rand_range(0, g->board->node_count - 1);
        p->node = dest; snap_player(g, p->id); p->hop_t = 0;
        node_world(g, dest, &px, &pz);
        spawn_popup(g, px, py, pz, 0, 4); spawn_fx(g, px, 1, pz, 0xFF1ABC9C, 10); sfx_play(SFX_CLICK);
        break;
    case SPACE_BOWSER: {
        int ev = rand_range(0, 3);
        spawn_fx(g, px, 1, pz, 0xFFE74C3C, 12); sfx_play(SFX_BUZZ);
        if (p->shielded) { p->shielded = false; spawn_popup(g, px, py, pz, 0, 5); spawn_fx(g, px, 1, pz, 0xFF3498DB, 6); break; }
        if (ev == 0) {
            delta = (p->coins >= 10) ? 10 : p->coins; p->coins -= delta;
            spawn_popup(g, px, py, pz, -delta, 8);
        } else if (ev == 1) {
            int k, others = g->player_count - 1, each = 3;
            delta = 0;
            for (k = 0; k < g->player_count; ++k) {
                if (k == p->id) continue;
                if (p->coins >= each) { p->coins -= each; g->players[k].coins += each; delta += each; }
            }
            (void)others; spawn_popup(g, px, py, pz, -delta, 8);
        } else if (ev == 2) {
            p->node = g->board->start_node; snap_player(g, p->id); p->hop_t = 0;
            node_world(g, p->node, &px, &pz);
            spawn_popup(g, px, py, pz, 0, 9); spawn_fx(g, px, 1, pz, 0xFFE74C3C, 10);
        } else {
            if (p->item_count > 0) { p->item_count = 0; spawn_popup(g, px, py, pz, 0, 10); }
            else { delta = (p->coins >= 6) ? 6 : p->coins; p->coins -= delta; spawn_popup(g, px, py, pz, -delta, 8); }
        }
        sfx_play(SFX_FAIL);
        break; }
    case SPACE_HAZARD:
        if (rand_range(0, 1) == 0) {
            delta = 2 + rand_range(0, 2);
            if (p->shielded) { p->shielded = false; spawn_popup(g, px, py, pz, 0, 5); }
            else { p->coins -= delta; if (p->coins < 0) p->coins = 0; spawn_popup(g, px, py, pz, -delta, 2); sfx_play(SFX_FAIL); }
        } else { p->coins += 4; spawn_popup(g, px, py, pz, 4, 0); sfx_play(SFX_COIN); }
        spawn_fx(g, px, 1, pz, 0xFFE67E22, 8);
        break;
    default: break;
    }
}
static int buy_item(GameContext *g, Player *p, int slot) {
    if (slot < 0 || slot > 2 || g->shop_offer[slot] == ITEM_NONE) return 0;
    if (p->coins < g->shop_cost[slot] || p->item_count >= MAX_ITEMS) return 0;
    p->coins -= g->shop_cost[slot]; give_item(p, g->shop_offer[slot]); g->shop_offer[slot] = ITEM_NONE;
    sfx_play(SFX_COIN);
    return 1;
}
static int buy_star(GameContext *g, Player *p) {
    g->shop_star_cost = star_cost_for(g, p);
    if (p->coins < g->shop_star_cost) return 0;
    p->coins -= g->shop_star_cost; p->stars += 1; relocate_star(g); p->celebrating = true;
    g->shop_star_cost = star_cost_for(g, p);
    sfx_play(SFX_STAR);
    return 1;
}
/* cursor: 0 = star, 1..3 = items, 4 = leave. Sets state to TURN_START when the visit ends. */
static void do_shop(GameContext *g, Player *p, uint32_t buttons) {
    if (!p->is_human) {
        int i, best = -1;
        if (g->state_timer < 14) return;
        if (p->coins >= g->shop_star_cost && (p->stars < 3 || p->diff == DIFF_HARD)) buy_star(g, p);
        for (i = 0; i < 3; ++i)
            if (g->shop_offer[i] != ITEM_NONE && p->coins >= g->shop_cost[i] && p->item_count < MAX_ITEMS &&
                (best < 0 || rand_range(0, 1))) best = i;
        if (best >= 0 && p->diff != DIFF_EASY) buy_item(g, p, best);
        g->state = STATE_TURN_START; return;
    }
    if (buttons & 0x20) { g->item_cursor = (g->item_cursor + 1) % 5; sfx_play(SFX_CLICK); }
    if (buttons & 0x10) {
        if (g->item_cursor == 4) { sfx_play(SFX_CLICK); g->state = STATE_TURN_START; return; }
        if (g->item_cursor == 0) { if (!buy_star(g, p)) sfx_play(SFX_FAIL); }
        else if (!buy_item(g, p, g->item_cursor - 1)) sfx_play(SFX_FAIL);
    }
}
static void use_item(GameContext *g, Player *p, int slot) {
    ItemType it = take_item(p, slot); int i, best = -1, best_val = -1;
    sfx_play(SFX_CLICK);
    switch (it) {
    case ITEM_DOUBLE_DICE: g->double_dice = true; spawn_popup(g, p->disp_x, 3.4f, p->disp_y, 2, 7); break;
    case ITEM_BOOST: g->boost_steps = 3; spawn_popup(g, p->disp_x, 3.4f, p->disp_y, 3, 7); break;
    case ITEM_STEAL:
        for (i = 0; i < g->player_count; ++i) {
            if (i == p->id) continue;
            if (g->team_mode && g->players[i].team == p->team) continue;
            if (g->players[i].coins > best_val) { best_val = g->players[i].coins; best = i; }
        }
        if (best >= 0) {
            int steal = (g->players[best].coins >= 5) ? 5 : g->players[best].coins;
            g->players[best].coins -= steal; p->coins += steal;
            spawn_popup(g, p->disp_x, 3.4f, p->disp_y, steal, 0); sfx_play(SFX_COIN);
        } break;
    case ITEM_SHIELD: p->shielded = true; spawn_popup(g, p->disp_x, 3.4f, p->disp_y, 1, 7); break;
    case ITEM_SWAP:
        for (i = 0; i < g->player_count; ++i) {
            int val;
            if (i == p->id) continue;
            if (g->team_mode && g->players[i].team == p->team) continue;
            val = g->players[i].stars * 100 + g->players[i].coins;
            if (val > best_val) { best_val = val; best = i; }
        }
        if (best >= 0) {
            int tmp = p->node;
            p->node = g->players[best].node; g->players[best].node = tmp;
            snap_player(g, p->id); snap_player(g, best);
            spawn_popup(g, p->disp_x, 3.4f, p->disp_y, 0, 6);
        }
        break;
    default: break;
    }
}
static void apply_comeback(GameContext *g) {
    int last = find_last_place(g);
    if (g->player_count < 2) return;
    g->players[last].coins += 5;
    spawn_popup(g, g->players[last].disp_x, 3.4f, g->players[last].disp_y, 5, 0);
    sfx_play(SFX_COIN);
}
static void next_player(GameContext *g) {
    int i; g->double_dice = false; g->boost_steps = 0;
    for (i = 0; i < g->player_count; ++i) g->players[i].celebrating = false;
    g->current_player = (g->current_player + 1) % g->player_count;
    if (g->current_player == 0) {
        g->round++; relocate_star(g); apply_comeback(g);
        if (g->round > g->max_rounds) { g->state = STATE_RESULTS; g->state_timer = 0; sfx_play(SFX_WIN); }
        else { g->state = STATE_ROUND_END; g->state_timer = 0; }
    } else { g->state = STATE_TURN_START; g->state_timer = 0; }
}
static void update_fx(GameContext *g) {
    int i;
    for (i = 0; i < MAX_FX; ++i) {
        if (!g->fx[i].active) continue;
        g->fx[i].x += g->fx[i].vx; g->fx[i].y += g->fx[i].vy; g->fx[i].vy += 0.18f;
        g->fx[i].life -= 0.02f; if (g->fx[i].life <= 0) g->fx[i].active = 0;
    }
}
static void update_display_positions(GameContext *g) {
    int i;
    for (i = 0; i < g->player_count; ++i) {
        Player *pl = &g->players[i]; float tx, tz;
        slot_target(g, i, &tx, &tz);
        if (pl->hop_t < 1.0f) {
            float t, e;
            pl->hop_t += 0.16f; if (pl->hop_t > 1.0f) pl->hop_t = 1.0f;
            t = pl->hop_t; e = t * t * (3.0f - 2.0f * t);
            pl->disp_x += (tx - pl->disp_x) * (0.25f + 0.5f * e);
            pl->disp_y += (tz - pl->disp_y) * (0.25f + 0.5f * e);
            pl->shadow_s = 0.5f + 0.5f * (1.0f - sinf(t * 3.14159f));
        } else {
            float dx = tx - pl->disp_x, dz = tz - pl->disp_y;
            if (dx * dx + dz * dz < 0.0004f) { pl->disp_x = tx; pl->disp_y = tz; }
            else { pl->disp_x += dx * 0.3f; pl->disp_y += dz * 0.3f; }
            pl->shadow_s += (1.0f - pl->shadow_s) * 0.2f;
        }
    }
    if (g->popup.active) { g->popup.life -= 0.02f; g->popup.y -= 0.9f; if (g->popup.life <= 0) g->popup.active = 0; }
    update_fx(g);
}
static uint32_t buttons_any(const uint32_t pad[PAD_COUNT]) {
    int i; uint32_t b = 0;
    for (i = 0; i < PAD_COUNT; ++i) b |= pad[i];
    return b;
}
static uint32_t buttons_for_player(GameContext *g, int pid, const uint32_t pad[PAD_COUNT]) {
    int port;
    if (pid < 0 || pid >= g->player_count) return 0;
    if (!g->players[pid].is_human) return 0;
    port = g->players[pid].pad_id;
    if (port < 0 || port >= PAD_COUNT) port = pid % PAD_COUNT;
    return pad[port];
}

/* ------------------------------------------------------------------ */
/* minigame intro (ready-up / countdown / gate)                        */
/* ------------------------------------------------------------------ */
static void mg_intro_update(GameContext *g, const uint32_t pressed[PAD_COUNT])
{
    int i, fpn;
    g->mg_intro_t++;
    switch (g->mg_intro_phase) {
    case INTRO_READY: {
        int all = 1;
        if (g->mg_intro_t > INTRO_IGNORE) {
            for (i = 0; i < g->player_count; ++i) {
                if (g->players[i].is_human && !g->mg_ready[i]) {
                    int port = g->players[i].pad_id;
                    if (port >= 0 && port < PAD_COUNT && (pressed[port] & 0x10)) { g->mg_ready[i] = true; sfx_play(SFX_COIN); }
                }
            }
            if (pressed[0] & 0x80) {
                for (i = 0; i < MAX_PLAYERS; ++i) g->mg_ready[i] = true;
                sfx_play(SFX_CLICK);
            }
        }
        for (i = 0; i < g->player_count; ++i) if (!g->mg_ready[i]) all = 0;
        if (all && g->mg_intro_t > INTRO_IGNORE) {
            g->mg_intro_phase = INTRO_COUNT; g->mg_intro_t = 0;
        }
        break; }
    case INTRO_GATE: {
        int port = g->players[g->mg_gate_player].pad_id;
        if (g->mg_intro_t > 10 && port >= 0 && port < PAD_COUNT &&
            ((pressed[port] & 0x10) || (pressed[0] & 0x80))) {
            g->mg_intro_phase = INTRO_COUNT; g->mg_intro_t = 0; sfx_play(SFX_CLICK);
        }
        break; }
    case INTRO_COUNT:
    default:
        fpn = (g->mg_gate_player >= 0) ? 30 : 45;
        if (g->mg_intro_t % fpn == 1 && g->mg_intro_t <= 3 * fpn) sfx_play(SFX_COUNT_FLASH);
        if (g->mg_intro_t == 3 * fpn + 1) sfx_play(SFX_MG_GO);
        if (g->mg_intro_t >= 3 * fpn + 24) mg_start_play(g);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* minigame play                                                       */
/* ------------------------------------------------------------------ */
static float actor_x(const GameContext *g) { return STAGE_SLOT_X(g->mg_actor, g->player_count); }

static void mg_play_update(GameContext *g, const uint32_t pressed[PAD_COUNT], const uint32_t held[PAD_COUNT])
{
    uint32_t ap = 0, ah = 0; int i;
    const float ax = actor_x(g);
    if (!g->mg_simul && g->mg_actor >= 0 && g->mg_actor < g->player_count && g->players[g->mg_actor].is_human) {
        int port = g->players[g->mg_actor].pad_id;
        if (port >= 0 && port < PAD_COUNT) { ap = pressed[port]; ah = held[port]; }
    }
    if (!g->mg_simul && g->mg_locked) {
        g->mg_lock_t++;
        if (g->mg_lock_t >= MG_LOCK_FRAMES) mg_finish_play(g);
        return;
    }
    g->mg_timer++;
    switch (g->mg_type) {
    case MG_MASH: {
        for (i = 0; i < g->player_count; ++i) {
            int press = 0;
            if (g->players[i].is_human) {
                int port = g->players[i].pad_id;
                press = (port >= 0 && port < PAD_COUNT) ? ((pressed[port] & 0x10) != 0) : 0;
            } else press = (rand() % 1000) < g->mg_p_latched[i];
            if (press) {
                g->mg_p_meter[i] += 0.05f;
                if (g->players[i].is_human && (g->mg_timer & 1))
                    spawn_fx(g, STAGE_SLOT_X(i, g->player_count), 1.0f + g->mg_p_meter[i] * 7.0f, 0, 0xFF2ECC71, 1);
            }
            g->mg_p_meter[i] = clampf(g->mg_p_meter[i] - 0.002f, 0.f, 1.f);
        }
        if (g->mg_timer >= MG_MASH_TIME) {
            for (i = 0; i < g->player_count; ++i) g->players[i].mg_score = (int)(g->mg_p_meter[i] * 100.0f);
            mg_finish_play(g);
        }
        break; }
    case MG_HOPPER: {
        int since;
        if (g->mg_timer >= g->mg_hop_swap) {
            g->mg_safe = !g->mg_safe; g->mg_phase_start = g->mg_timer;
            g->mg_hop_swap = g->mg_timer + (g->mg_safe ? rand_range(35, 70) : rand_range(28, 50));
            sfx_play(g->mg_safe ? SFX_MG_GO : SFX_BUZZ);
        }
        since = g->mg_timer - g->mg_phase_start;
        for (i = 0; i < g->player_count; ++i) {
            int press = 0, human = g->players[i].is_human;
            if (human) {
                int port = g->players[i].pad_id;
                press = (port >= 0 && port < PAD_COUNT) ? ((pressed[port] & 0x10) != 0) : 0;
            } else {
                int delay = (g->players[i].diff == DIFF_HARD) ? 7 : (g->players[i].diff == DIFF_EASY ? 16 : 11);
                if (g->mg_safe) press = (rand() % 1000) < g->mg_p_latched[i];
                else if (since < delay) press = (rand() % 1000) < g->mg_p_latched[i];
                else press = (rand() % 1000) < 3;
            }
            if (g->mg_hop_flash[i] > 0) { g->mg_hop_flash[i]--; press = 0; }
            if (!press) continue;
            if (g->mg_safe) {
                g->mg_p_meter[i] = clampf(g->mg_p_meter[i] + 0.06f, 0.f, 1.f);
                if (human && (g->mg_timer & 1)) spawn_fx(g, STAGE_SLOT_X(i, g->player_count), 1.0f + g->mg_p_meter[i] * 7.0f, 0, 0xFF2ECC71, 1);
            } else if (since > 8) {
                g->mg_p_meter[i] = clampf(g->mg_p_meter[i] - 0.15f, 0.f, 1.f);
                g->mg_hop_flash[i] = 22;
                spawn_fx(g, STAGE_SLOT_X(i, g->player_count), 1.0f + g->mg_p_meter[i] * 7.0f, 0, 0xFFE74C3C, 4);
                if (human) sfx_play(SFX_FAIL);
            }
        }
        {
            int done = (g->mg_timer >= MG_HOPPER_MAX);
            for (i = 0; i < g->player_count; ++i) if (g->mg_p_meter[i] >= 0.999f) done = 1;
            if (done) {
                for (i = 0; i < g->player_count; ++i) g->players[i].mg_score = (int)(g->mg_p_meter[i] * 100.0f);
                mg_finish_play(g);
            }
        }
        break; }
    case MG_TIMING:
        g->mg_marker += g->mg_marker_dir * 0.024f;
        if (g->mg_marker >= 1) { g->mg_marker = 1; g->mg_marker_dir = -1; }
        if (g->mg_marker <= 0) { g->mg_marker = 0; g->mg_marker_dir = 1; }
        if (ap & 0x10) {
            float dist = fabsf(g->mg_marker - 0.5f);
            int sc = (int)((1.0f - dist * 2.0f) * 100.0f);
            if (sc < 5) sc = 5;
            spawn_fx(g, (g->mg_marker - 0.5f) * 12.0f, 3.2f, 0, 0xFFF1C40F, 8);
            sfx_play(sc > 70 ? SFX_COIN : SFX_CLICK); mg_lock(g, sc);
        } else if (g->mg_timer >= MG_TIMING_TIME) { sfx_play(SFX_FAIL); mg_lock(g, 5); }
        break;
    case MG_HOLD:
        if (ah & 0x10) {
            g->mg_hold_frames++;
            g->mg_meter = clampf((float)g->mg_hold_frames / (float)(g->mg_target + 25), 0.f, 1.f);
        } else if (g->mg_hold_frames > 3) {
            int diff = g->mg_hold_frames - g->mg_target; if (diff < 0) diff = -diff;
            int sc = 100 - diff * 3; if (sc < 5) sc = 5;
            spawn_fx(g, ax, 3.0f, 0, 0xFF3498DB, 8);
            sfx_play(sc > 70 ? SFX_COIN : SFX_CLICK); mg_lock(g, sc);
        } else g->mg_hold_frames = 0;
        if (!g->mg_locked && g->mg_timer >= MG_HOLD_MAX) {
            if (g->mg_hold_frames > 3) {
                int diff = g->mg_hold_frames - g->mg_target; if (diff < 0) diff = -diff;
                mg_lock(g, 100 - diff * 3 < 5 ? 5 : 100 - diff * 3);
            } else { sfx_play(SFX_FAIL); mg_lock(g, 5); }
        }
        break;
    case MG_COUNT:
        if (g->mg_phase == 0) {
            if (g->mg_timer >= g->mg_next_flash && g->mg_flash_count < g->mg_target) {
                g->mg_flash_count++; g->mg_phase_start = g->mg_timer;
                g->mg_next_flash = g->mg_timer + 16 + rand_range(0, 8);
                spawn_fx(g, 0, 5.0f, 0, 0xFFFFFFFF, 6); sfx_play(SFX_COUNT_FLASH);
            }
            if (g->mg_flash_count >= g->mg_target && g->mg_timer > g->mg_phase_start + 26) {
                g->mg_phase = 1; g->mg_meter = 0; g->mg_phase_start = g->mg_timer; sfx_play(SFX_MG_GO);
            }
        } else {
            if (ap & 0x10) { g->mg_meter += 1.0f; sfx_play(SFX_CLICK); }
            if ((ap & 0x20) || g->mg_timer - g->mg_phase_start >= MG_COUNT_INPUT) {
                int presses = (int)g->mg_meter;
                int diff = presses - g->mg_target; if (diff < 0) diff = -diff;
                int sc = (diff == 0) ? 100 : (100 - diff * 25); if (sc < 5) sc = 5;
                spawn_fx(g, ax, 3.0f, 0, 0xFF9B59B6, 8);
                sfx_play(sc >= 100 ? SFX_COIN : SFX_FAIL); mg_lock(g, sc);
            }
        }
        break;
    case MG_AVOID:
        if (g->mg_timer < g->mg_go_at) g->mg_phase = 0;
        else if (g->mg_timer < g->mg_target) g->mg_phase = 1;
        else { g->mg_phase = 2; sfx_play(SFX_FAIL); mg_lock(g, 8); break; }
        if (ap & 0x10) {
            if (g->mg_phase == 0) { sfx_play(SFX_FAIL); mg_lock(g, 5); }
            else {
                int sc = 100 - (g->mg_timer - g->mg_go_at); if (sc < 20) sc = 20;
                spawn_fx(g, ax, 3.0f, 0, 0xFF2ECC71, 8); sfx_play(SFX_COIN); mg_lock(g, sc);
            }
        }
        break;
    case MG_ZONE: {
        float ph = (float)g->mg_target * 0.01f;
        g->mg_zone_c = 0.5f + 0.3f * sinf((float)g->mg_timer * 0.045f + ph);
        g->mg_marker += (ah & 0x10) ? 0.014f : -0.014f;
        g->mg_marker = clampf(g->mg_marker, 0.f, 1.f);
        if (fabsf(g->mg_marker - g->mg_zone_c) <= g->mg_zone_w) g->mg_zone_hits++;
        if (g->mg_timer >= MG_ZONE_MAX) {
            int sc = (g->mg_zone_hits * 100) / (MG_ZONE_MAX * 3 / 4);
            if (sc > 100) sc = 100;
            if (sc < 5) sc = 5;
            sfx_play(sc > 60 ? SFX_COIN : SFX_CLICK); mg_lock(g, sc);
        }
        break; }
    case MG_RHYTHM: {
        int k, next_t, best = -1;
        /* upcoming beat for visuals + beat sound */
        k = (g->mg_timer - MG_RHYTHM_T0 + MG_RHYTHM_GAP - 1);
        k = (k < 0) ? 0 : k / MG_RHYTHM_GAP;
        if (k > g->mg_beats_total - 1) k = g->mg_beats_total - 1;
        next_t = MG_RHYTHM_T0 + k * MG_RHYTHM_GAP;
        g->mg_next_flash = next_t;
        g->mg_marker = clampf(1.0f - (float)(next_t - g->mg_timer) / (float)MG_RHYTHM_GAP, 0.f, 1.f);
        for (i = 0; i < g->mg_beats_total; ++i)
            if (g->mg_timer == MG_RHYTHM_T0 + i * MG_RHYTHM_GAP) {
                g->mg_phase_start = g->mg_timer; g->mg_flash_count = i + 1; sfx_play(SFX_COUNT_FLASH);
            }
        if (ap & 0x10) {
            for (i = 0; i < g->mg_beats_total; ++i) {
                int d = g->mg_timer - (MG_RHYTHM_T0 + i * MG_RHYTHM_GAP); if (d < 0) d = -d;
                if (d <= g->mg_target && !(g->mg_beat_mask & (1 << i))) { best = i; break; }
            }
            if (best >= 0) {
                g->mg_beat_mask |= (1 << best); g->mg_beats_hit++;
                spawn_fx(g, ax, 3.0f, 0, 0xFF2ECC71, 6); sfx_play(SFX_COIN);
            } else { g->mg_misses++; sfx_play(SFX_FAIL); }
        }
        if (g->mg_timer >= MG_RHYTHM_T0 + (g->mg_beats_total - 1) * MG_RHYTHM_GAP + 20) {
            int sc = (g->mg_beats_hit * 100) / g->mg_beats_total - g->mg_misses * 6;
            if (sc < 5) sc = 5;
            mg_lock(g, sc);
        }
        break; }
    case MG_CATCH: {
        float sp = 0.022f;
        g->mg_fall_y += 0.02f;
        if (ah & (0x20 | 0x02)) g->mg_marker += sp;
        if (ah & (0x40 | 0x01)) g->mg_marker -= sp;
        g->mg_marker = clampf(g->mg_marker, 0.08f, 0.92f);
        if (g->mg_fall_y >= 1.0f) {
            float dx = g->mg_marker - g->mg_fall_x; if (dx < 0) dx = -dx;
            if (dx < 0.11f) {
                g->mg_beats_hit++; sfx_play(SFX_COIN);
                spawn_fx(g, (g->mg_marker - 0.5f) * 12.0f, 1.0f, 0, 0xFFF1C40F, 8);
            } else { g->mg_misses++; sfx_play(SFX_FAIL); }
            g->mg_fall_y = 0.f;
            g->mg_fall_x = (float)g->mg_seq[(g->mg_beats_hit + g->mg_misses) & 7] * 0.01f;
        }
        if (g->mg_beats_hit >= g->mg_beats_total) {
            mg_lock(g, 60 + (MG_CATCH_MAX - g->mg_timer) * 40 / MG_CATCH_MAX);
        } else if (g->mg_timer >= MG_CATCH_MAX) {
            int sc = g->mg_beats_hit * 15; if (sc < 5) sc = 5;
            mg_lock(g, sc);
        }
        break; }
    case MG_SIMON:
        if (g->mg_pressed > 0) { g->mg_pressed--; if (g->mg_pressed == 0) g->mg_prompt = 0; }
        if (g->mg_phase == 0) {
            int t = g->mg_timer - 30;
            if (t >= 0 && t / 34 < g->mg_seq_len) {
                int idx = t / 34, off = t % 34;
                if (off == 0) { sfx_play(SFX_COUNT_FLASH); g->mg_flash_count = idx + 1; }
                g->mg_prompt = (off < 24) ? SIMON_BTN[g->mg_seq[idx]] : 0;
            } else if (t >= g->mg_seq_len * 34 + 10) {
                g->mg_phase = 1; g->mg_phase_start = g->mg_timer; g->mg_prompt = 0; g->mg_input_idx = 0;
                sfx_play(SFX_MG_GO);
            } else g->mg_prompt = 0;
        } else {
            int b;
            for (b = 0; b < 3; ++b) {
                if (ap & SIMON_BTN[b]) {
                    g->mg_prompt = SIMON_BTN[b]; g->mg_pressed = 10;
                    if (b == g->mg_seq[g->mg_input_idx]) {
                        g->mg_input_idx++; g->mg_beats_hit++; sfx_play(SFX_COIN);
                        if (g->mg_input_idx >= g->mg_seq_len) {
                            spawn_fx(g, ax, 3.0f, 0, 0xFFF1C40F, 10); mg_lock(g, 100);
                        }
                    } else {
                        int sc = g->mg_beats_hit * 100 / g->mg_seq_len; if (sc < 5) sc = 5;
                        sfx_play(SFX_FAIL); mg_lock(g, sc);
                    }
                    break;
                }
            }
            if (!g->mg_locked && g->mg_timer - g->mg_phase_start >= MG_SIMON_INPUT) {
                int sc = g->mg_beats_hit * 100 / g->mg_seq_len; if (sc < 5) sc = 5;
                sfx_play(SFX_FAIL); mg_lock(g, sc);
            }
        }
        break;
    case MG_REACTION:
    default: {
        int max_t = g->mg_go_at + 60;
        if (ap & 0x10) {
            if (g->mg_timer < g->mg_go_at) { sfx_play(SFX_FAIL); mg_lock(g, 5); }
            else {
                int sc = 100 - (g->mg_timer - g->mg_go_at) * 2; if (sc < 10) sc = 10;
                spawn_fx(g, ax, 3.0f, 0, 0xFF2ECC71, 10); sfx_play(SFX_COIN); mg_lock(g, sc);
            }
        } else if (g->mg_timer >= max_t) { sfx_play(SFX_FAIL); mg_lock(g, 8); }
        break; }
    }
}

/* ------------------------------------------------------------------ */
/* main update                                                         */
/* ------------------------------------------------------------------ */
static void start_match_setup(GameContext *g)
{
    int i;
    for (i = 0; i < g->player_count; ++i) {
        g->players[i].is_human = (i < g->human_count);
        g->players[i].diff = g->cpu_diff;
        g->players[i].coins = 12; g->players[i].stars = 0;
        g->players[i].node = g->board->start_node; g->players[i].item_count = 0;
        g->players[i].shielded = false; g->players[i].shadow_s = 1.0f;
        g->players[i].celebrating = false;
        g->players[i].color = i; g->players[i].pad_id = i;
        g->players[i].team = (g->team_mode && g->player_count == 4) ? ((i < 2) ? 0 : 1) : i;
        snap_player(g, i);
    }
    if (g->players[0].is_human) give_item(&g->players[0], ITEM_DOUBLE_DICE);
}

static void reload_board(GameContext *g)
{
    if (g->board_idx == 0) board_load_visual(g->board);
    else if (g->board_idx == 1) board_load_geo(g->board);
    else board_load_metro(g->board);
}

void game_update(GameContext *g, const uint32_t pad_pressed[PAD_COUNT], const uint32_t pad_held[PAD_COUNT])
{
    Player *p; BoardNode *n;
    uint32_t buttons;
    g->frame++; g->anim_t += 0.016f;

    {
        uint32_t any = buttons_any(pad_pressed);
        if ((any & 0x100) && g->state >= STATE_TURN_START && g->state <= STATE_MG_RESULT) {
            g->pause_from = g->state; g->state = STATE_PAUSE; sfx_play(SFX_CLICK);
            any = 0;
        } else if ((any & 0x100) && g->state == STATE_PAUSE) {
            g->state = g->pause_from; sfx_play(SFX_CLICK); any = 0;
        }
        buttons = any;
    }
    cam_update(g);
    if (g->state == STATE_PAUSE) {
        if (buttons & 0x10) { g->state = g->pause_from; sfx_play(SFX_CLICK); }
        if (buttons & 0x80) { sfx_play(SFX_CLICK); g->state = STATE_TITLE; g->state_timer = 0; }
        music_sync(g);
        return;
    }
    g->state_timer++;
    if (g->hop_cooldown > 0) g->hop_cooldown--;
    if (g->land_flash > 0) g->land_flash--;
    update_display_positions(g);

    p = &g->players[g->current_player];
    if (!(g->state == STATE_TITLE || g->state == STATE_SETUP || g->state == STATE_BOARD_SELECT ||
          g->state == STATE_RESULTS || g->state == STATE_MG_RESULT || g->state == STATE_ROUND_END)) {
        buttons = buttons_for_player(g, g->current_player, pad_pressed);
    }

    switch (g->state) {
    case STATE_TITLE:
        if (buttons & 0x10) { sfx_play(SFX_CLICK); g->state = STATE_SETUP; g->state_timer = 0; g->setup_cursor = 0; }
        if (buttons & 0x100) g->state = STATE_EXIT;
        break;
    case STATE_SETUP:
        if (buttons & 0x80) { g->setup_cursor = (g->setup_cursor + 1) % SETUP_ROWS; sfx_play(SFX_CLICK); }
        if (buttons & 0x20) {
            sfx_play(SFX_CLICK);
            switch (g->setup_cursor) {
            case 0:
                g->player_count = g->player_count >= 4 ? 2 : g->player_count + 1;
                if (g->human_count > g->player_count) g->human_count = g->player_count;
                if (g->player_count < 4) g->team_mode = false;
                break;
            case 1: g->human_count = g->human_count >= g->player_count ? 1 : g->human_count + 1; break;
            case 2: if (g->player_count == 4) g->team_mode = !g->team_mode; break;
            case 3: g->cpu_diff = (CpuDiff)(((int)g->cpu_diff + 1) % 3); break;
            case 4: {
                static const int ropts[] = {8, 12, 15, 20};
                int k;
                for (k = 0; k < 4; ++k) if (ropts[k] == g->max_rounds) { g->max_rounds = ropts[(k + 1) % 4]; break; }
                if (k == 4) g->max_rounds = 12;
            } break;
            }
        }
        if (buttons & 0x10) {
            start_match_setup(g); sfx_play(SFX_CLICK);
            g->state = STATE_BOARD_SELECT; g->state_timer = 0;
        }
        break;
    case STATE_BOARD_SELECT:
        if (buttons & 0x20) {
            int i; g->board_idx = (g->board_idx + 1) % BOARD_COUNT; g->board = &g_boards[g->board_idx];
            for (i = 0; i < g->player_count; ++i) { g->players[i].node = g->board->start_node; snap_player(g, i); }
            sfx_play(SFX_CLICK);
        }
        if (buttons & 0x10) {
            sfx_play(SFX_CLICK); g->round = 1; g->current_player = 0;
            reload_board(g);
            g->state = STATE_TURN_START; g->state_timer = 0;
        }
        if (buttons & 0x40) { sfx_play(SFX_CLICK); g->state = STATE_SETUP; g->state_timer = 0; }
        break;
    case STATE_TURN_START:
        p->celebrating = false; g->dice_value = g->dice_value2 = 0; g->move_remaining = 0;
        g->double_dice = false; g->boost_steps = 0; g->branch_choice = 0; g->item_cursor = 0;
        g->state = (p->item_count > 0) ? STATE_ITEM : STATE_DICE_ROLL; g->state_timer = 0; break;
    case STATE_ITEM:
        if (p->is_human) {
            if ((buttons & 0x20) && p->item_count > 0) { g->item_cursor = (g->item_cursor + 1) % (p->item_count + 1); sfx_play(SFX_CLICK); }
            if (buttons & 0x10) {
                if (g->item_cursor < p->item_count) use_item(g, p, g->item_cursor); else sfx_play(SFX_CLICK);
                g->state = STATE_DICE_ROLL; g->state_timer = 0;
            }
        } else if (g->state_timer >= ITEM_CPU_DELAY) {
            if (p->item_count > 0 && rand_range(0, 2) != 0) use_item(g, p, 0);
            g->state = STATE_DICE_ROLL; g->state_timer = 0;
        }
        break;
    case STATE_DICE_ROLL:
        if (p->is_human ? (buttons & 0x10) : (g->state_timer >= DICE_CPU_DELAY)) {
            if (p->is_human) g->dice_value = rand_range(1, 6);
            else {
                int bias = (p->diff == DIFF_HARD) ? 1 : (p->diff == DIFF_EASY ? -1 : 0);
                g->dice_value = rand_range(1, 6) + bias;
                if (g->dice_value < 1) g->dice_value = 1;
                if (g->dice_value > 6) g->dice_value = 6;
            }
            if (g->double_dice) { g->dice_value2 = rand_range(1, 6); g->move_remaining = g->dice_value + g->dice_value2 + g->boost_steps; }
            else { g->dice_value2 = 0; g->move_remaining = g->dice_value + g->boost_steps; }
            p->celebrating = (g->move_remaining >= 8);
            g->state = STATE_MOVE; g->state_timer = 0; g->hop_cooldown = 2;
            spawn_fx(g, p->disp_x, 2.0f, p->disp_y, 0xFFFFFFFF, 8); sfx_play(SFX_DICE);
        }
        break;
    case STATE_MOVE:
        if (g->move_remaining <= 0) { g->state = STATE_LAND; g->state_timer = 0; break; }
        if (g->hop_cooldown > 0) break;
        n = &g->board->nodes[p->node];
        if (n->next_count == 0) { g->move_remaining = 0; g->state = STATE_LAND; g->state_timer = 0; }
        else if (n->next_count == 1) start_hop(g, p, n->next[0]);
        else if (p->is_human) { g->branch_choice = 0; g->state = STATE_BRANCH; g->state_timer = 0; }
        else start_hop(g, p, cpu_choose_path(g, p, p->node));
        break;
    case STATE_BRANCH:
        n = &g->board->nodes[p->node];
        if ((buttons & 0x20) && n->next_count > 0) { g->branch_choice = (g->branch_choice + 1) % n->next_count; sfx_play(SFX_CLICK); }
        if (buttons & 0x10) {
            if (g->branch_choice >= n->next_count) g->branch_choice = 0;
            start_hop(g, p, n->next[g->branch_choice]); g->state = STATE_MOVE; g->state_timer = 0;
        }
        break;
    case STATE_LAND:
        if (g->state_timer == 1) apply_land(g, p);
        if (g->state == STATE_SHOP) break;
        if (g->state_timer >= LAND_HOLD) next_player(g);
        break;
    case STATE_SHOP:
        do_shop(g, p, buttons); if (g->state != STATE_SHOP) next_player(g); break;
    case STATE_ROUND_END:
        if (g->state_timer >= ROUND_HOLD) mg_begin(g);
        break;
    case STATE_MG_INTRO:
        mg_intro_update(g, pad_pressed);
        break;
    case STATE_MG_PLAY:
        mg_play_update(g, pad_pressed, pad_held);
        break;
    case STATE_MG_RESULT:
        if (g->state_timer == 1) mg_rank_and_pay(g);
        if (g->state_timer >= MG_RESULT_HOLD || (g->state_timer >= MG_RESULT_SKIP && (buttons & 0x10))) {
            g->state = STATE_TURN_START; g->state_timer = 0;
        }
        break;
    case STATE_RESULTS:
        if (g->state_timer > 20 && (buttons & 0x10)) {
            int i; sfx_play(SFX_CLICK);
            g->state = STATE_TITLE; g->state_timer = 0; g->round = 0; g->current_player = 0;
            for (i = 0; i < MAX_PLAYERS; ++i) {
                g->players[i].celebrating = false; g->players[i].coins = 12;
                g->players[i].stars = 0; g->players[i].item_count = 0; g->players[i].shielded = false;
            }
        }
        if (buttons & 0x100) { sfx_play(SFX_CLICK); g->state = STATE_EXIT; }
        break;
    default: break;
    }
    music_sync(g);
}

void game_shutdown(GameContext *g) { LOG_I("game_shutdown"); (void)g; }

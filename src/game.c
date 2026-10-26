/* Board Game core - restored + field inits
 * Minigames: Mash, Timing, Reaction (HOLD/COUNT share reaction until next polish pass)
 */
#include "game.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static Board g_boards[BOARD_COUNT];

#define HOP_FRAMES 9
#define LAND_HOLD 32
#define DICE_CPU_DELAY 36
#define ROUND_HOLD 40
#define ITEM_CPU_DELAY 20
#define MG_INTRO_HOLD 70
#define MG_RESULT_HOLD 90
#define MG_MASH_TIME 120
#define MG_TIMING_TIME 180
#define MG_REACT_MAX 150

static const int MG_PAYOUT[4] = {15, 10, 5, 2};

static int rand_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (rand() % (hi - lo + 1));
}

static void spawn_popup(GameContext *g, float x, float y, int value, int kind) {
    g->popup.active = 1; g->popup.x = x; g->popup.y = y;
    g->popup.value = value; g->popup.kind = kind; g->popup.life = 1.3f;
}

static void spawn_fx(GameContext *g, float x, float y, uint32_t color, int count) {
    int i, n = 0;
    for (i = 0; i < MAX_FX && n < count; ++i) {
        if (g->fx[i].active) continue;
        g->fx[i].active = 1;
        g->fx[i].x = x + (float)rand_range(-6, 6);
        g->fx[i].y = y + (float)rand_range(-4, 4);
        g->fx[i].vx = (float)rand_range(-40, 40) * 0.08f;
        g->fx[i].vy = (float)rand_range(-60, -10) * 0.08f;
        g->fx[i].life = 0.4f + (float)rand_range(0, 20) * 0.02f;
        g->fx[i].color = color; n++;
    }
}

static void node_world(GameContext *g, int node, float *x, float *y) {
    float ox = 40.0f, oy = 70.0f, sx = 640.0f, sy = 360.0f;
    BoardNode *n = &g->board->nodes[node];
    *x = ox + n->x * sx; *y = oy + n->y * sy;
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
    static const ItemType pool[] = {ITEM_DOUBLE_DICE, ITEM_BOOST, ITEM_STEAL, ITEM_SHIELD, ITEM_BOOST};
    return pool[rand_range(0, 4)];
}

static void mg_begin(GameContext *g) {
    int i;
    g->mg_type = (MiniGameType)rand_range(0, MG_COUNT - 1);
    if (g->mg_type > MG_REACTION) g->mg_type = (MiniGameType)(g->mg_type % 3); /* map HOLD/COUNT to first 3 until full logic lands */
    g->mg_timer = 0; g->mg_meter = 0; g->mg_marker = 0.15f; g->mg_marker_dir = 1;
    g->mg_go_at = 45 + rand_range(0, 55); g->mg_pressed = 0; g->mg_human_score = 0;
    g->mg_awarded = false; g->mg_hold_frames = 0; g->mg_flash_count = 0; g->mg_phase = 0;
    g->mg_target = 55; g->mg_next_flash = 25;
    for (i = 0; i < g->player_count; ++i) { g->players[i].mg_score = 0; g->players[i].mg_rank = i; }
    g->state = STATE_MG_INTRO; g->state_timer = 0;
    LOG_I("Minigame type %d", (int)g->mg_type);
}

static int cpu_mg_score(Player *p, MiniGameType t) {
    int base = 55, noise = rand_range(-15, 15);
    if (p->diff == DIFF_HARD) base = 78;
    else if (p->diff == DIFF_EASY) base = 35;
    base += noise; if (base < 5) base = 5; if (base > 100) base = 100;
    return base;
}

static void mg_rank_and_pay(GameContext *g) {
    int i, j, order[MAX_PLAYERS];
    if (g->mg_awarded) return; g->mg_awarded = true;
    for (i = 0; i < g->player_count; ++i) order[i] = i;
    for (i = 0; i < g->player_count - 1; ++i)
        for (j = 0; j < g->player_count - 1 - i; ++j)
            if (g->players[order[j]].mg_score < g->players[order[j+1]].mg_score) {
                int t = order[j]; order[j] = order[j+1]; order[j+1] = t;
            }
    for (i = 0; i < g->player_count; ++i) {
        int pid = order[i], pay = MG_PAYOUT[i < 4 ? i : 3];
        g->players[pid].mg_rank = i;
        g->players[pid].coins += pay;
        g->contest_rewards[pid] = pay;
    }
    if (g->player_count > 0) g->players[order[0]].celebrating = true;
    spawn_fx(g, 424, 200, 0xFFF1C40F, 12);
}

static void mg_finish_play(GameContext *g) {
    int i;
    for (i = 0; i < g->player_count; ++i) {
        if (!g->players[i].is_human)
            g->players[i].mg_score = cpu_mg_score(&g->players[i], g->mg_type);
        else
            g->players[i].mg_score = g->mg_human_score;
    }
    g->state = STATE_MG_RESULT; g->state_timer = 0;
}

void game_init(GameContext *g) {
    int i; LOG_I("game_init");
    memset(g, 0, sizeof(*g)); srand((unsigned)time(NULL));
    board_load_visual(&g_boards[0]); board_load_geo(&g_boards[1]);
    g->state = STATE_TITLE; g->board = &g_boards[0];
    g->player_count = 4; g->max_rounds = 15;
    for (i = 0; i < MAX_PLAYERS; ++i) {
        float x, y;
        g->players[i].id = i; g->players[i].is_human = (i == 0);
        g->players[i].diff = DIFF_NORMAL; g->players[i].coins = 12;
        g->players[i].node = g->board->start_node; g->players[i].color = i;
        g->players[i].team = (i < 2) ? 0 : 1; g->players[i].shadow_s = 1.0f;
        node_world(g, g->players[i].node, &x, &y);
        g->players[i].disp_x = x; g->players[i].disp_y = y; g->players[i].hop_t = 1.0f;
    }
    give_item(&g->players[0], ITEM_DOUBLE_DICE);
}

static void relocate_star(GameContext *g) {
    int tries = 0, new_node;
    do { new_node = rand_range(0, g->board->node_count - 1); tries++; }
    while ((g->board->nodes[new_node].type == SPACE_SHOP || new_node == g->board->star_node) && tries < 40);
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
        if (t == SPACE_PLUS_COIN) score += 10;
        if (t == SPACE_SHOP) score += 8;
        if (t == SPACE_MINUS_COIN) score -= 15;
        if (p->diff == DIFF_HARD) score += rand_range(0, 5);
        else if (p->diff == DIFF_EASY) score += rand_range(-20, 20);
        else score += rand_range(-5, 8);
        if (score > score_best) { score_best = score; best = cand; }
    }
    return best;
}

static void start_hop(GameContext *g, Player *p, int next_node) {
    float x, y; p->node = next_node; p->hop_t = 0; g->hop_cooldown = HOP_FRAMES;
    g->move_remaining--; node_world(g, next_node, &x, &y);
    spawn_fx(g, x, y, 0x88FFFFFF, 3);
}

static void apply_land(GameContext *g, Player *p) {
    BoardNode *n = &g->board->nodes[p->node];
    float px, py; int delta;
    node_world(g, p->node, &px, &py);
    g->last_land_type = (int)n->type; g->land_flash = LAND_HOLD;
    switch (n->type) {
        case SPACE_PLUS_COIN:
            delta = 3 + rand_range(0, 3); p->coins += delta;
            spawn_popup(g, px, py - 30, delta, 0); spawn_fx(g, px, py, 0xFF2ECC71, 6); break;
        case SPACE_MINUS_COIN:
            if (p->shielded) { p->shielded = false; spawn_popup(g, px, py - 30, 0, 5); spawn_fx(g, px, py, 0xFF3498DB, 5); }
            else { delta = 3 + rand_range(0, 1); p->coins -= delta; if (p->coins < 0) p->coins = 0;
                spawn_popup(g, px, py - 30, -delta, 2); spawn_fx(g, px, py, 0xFFE74C3C, 5); } break;
        case SPACE_EVENT:
            if (rand_range(0, 2) == 0 && give_item(p, random_item())) { spawn_popup(g, px, py - 30, 1, 5); spawn_fx(g, px, py, 0xFF9B59B6, 8); }
            else if (rand_range(0, 1)) { p->coins += 6; spawn_popup(g, px, py - 30, 6, 4); }
            else { delta = (p->coins > 3) ? 3 : p->coins; p->coins -= delta; spawn_popup(g, px, py - 30, -delta, 4); } break;
        case SPACE_SHOP:
            spawn_popup(g, px, py - 30, 0, 3); g->state = STATE_SHOP; g->state_timer = 0; g->item_cursor = 0; return;
        case SPACE_STAR:
            if (p->coins >= 20) { p->coins -= 20; p->stars += 1; spawn_popup(g, px, py - 30, 1, 1);
                spawn_fx(g, px, py, 0xFFF1C40F, 12); p->celebrating = true; relocate_star(g); }
            else spawn_popup(g, px, py - 30, 0, 2); break;
        case SPACE_HIDDEN_COIN:
            if (!n->hidden_revealed) { n->hidden_revealed = true; p->coins += 12; spawn_popup(g, px, py - 30, 12, 0); spawn_fx(g, px, py, 0xFF1ABC9C, 8); } break;
        case SPACE_HIDDEN_STAR:
            if (!n->hidden_revealed) { n->hidden_revealed = true; p->stars += 1; spawn_popup(g, px, py - 30, 1, 1); spawn_fx(g, px, py, 0xFFF1C40F, 10); p->celebrating = true; } break;
        default: break;
    }
}

static void do_shop(GameContext *g, Player *p, uint32_t buttons) {
    if (!p->is_human) {
        if (g->state_timer < 22) return;
        if (p->coins >= 20 && (p->stars < 3 || p->diff == DIFF_HARD)) { p->coins -= 20; p->stars += 1; relocate_star(g); }
        else if (p->coins >= 8 && p->item_count < MAX_ITEMS) { p->coins -= 8; give_item(p, random_item()); }
        g->state = STATE_TURN_START; return;
    }
    if (buttons & 0x20) g->item_cursor = (g->item_cursor + 1) % 3;
    if (buttons & 0x10) {
        if (g->item_cursor == 0 && p->coins >= 20) { p->coins -= 20; p->stars += 1; relocate_star(g); p->celebrating = true; }
        else if (g->item_cursor == 1 && p->coins >= 8 && p->item_count < MAX_ITEMS) { p->coins -= 8; give_item(p, random_item()); }
        g->state = STATE_TURN_START;
    }
}

static void use_item(GameContext *g, Player *p, int slot) {
    ItemType it = take_item(p, slot); int i, best = -1, best_coins = -1;
    switch (it) {
        case ITEM_DOUBLE_DICE: g->double_dice = true; spawn_popup(g, p->disp_x, p->disp_y - 40, 2, 5); break;
        case ITEM_BOOST: g->boost_steps = 3; spawn_popup(g, p->disp_x, p->disp_y - 40, 3, 5); break;
        case ITEM_STEAL:
            for (i = 0; i < g->player_count; ++i) {
                if (i == p->id) continue;
                if (g->team_mode && g->players[i].team == p->team) continue;
                if (g->players[i].coins > best_coins) { best_coins = g->players[i].coins; best = i; }
            }
            if (best >= 0) {
                int steal = (g->players[best].coins >= 5) ? 5 : g->players[best].coins;
                g->players[best].coins -= steal; p->coins += steal;
                spawn_popup(g, p->disp_x, p->disp_y - 40, steal, 0);
            } break;
        case ITEM_SHIELD: p->shielded = true; spawn_popup(g, p->disp_x, p->disp_y - 40, 1, 5); break;
        default: break;
    }
}

static void next_player(GameContext *g) {
    int i; g->double_dice = false; g->boost_steps = 0;
    for (i = 0; i < g->player_count; ++i) g->players[i].celebrating = false;
    g->current_player = (g->current_player + 1) % g->player_count;
    if (g->current_player == 0) {
        g->round++; relocate_star(g); g->state = STATE_ROUND_END; g->state_timer = 0;
        if (g->round > g->max_rounds) g->state = STATE_RESULTS;
    } else { g->state = STATE_TURN_START; g->state_timer = 0; }
}

static void update_fx(GameContext *g) {
    int i;
    for (i = 0; i < MAX_FX; ++i) {
        if (!g->fx[i].active) continue;
        g->fx[i].x += g->fx[i].vx; g->fx[i].y += g->fx[i].vy; g->fx[i].vy += 0.15f;
        g->fx[i].life -= 0.016f; if (g->fx[i].life <= 0) g->fx[i].active = 0;
    }
}

static void update_display_positions(GameContext *g) {
    int i;
    for (i = 0; i < g->player_count; ++i) {
        Player *pl = &g->players[i]; float tx, ty;
        node_world(g, pl->node, &tx, &ty); tx += (float)(i % 4) * 10.0f - 15.0f;
        if (pl->hop_t < 1.0f) {
            float t, e, ax, ay, arc;
            pl->hop_t += 0.11f; if (pl->hop_t > 1.0f) pl->hop_t = 1.0f;
            t = pl->hop_t; e = t * t * (3.0f - 2.0f * t);
            ax = pl->disp_x + (tx - pl->disp_x) * e; ay = pl->disp_y + (ty - pl->disp_y) * e;
            arc = sinf(t * 3.14159f) * 12.0f; pl->disp_x = ax; pl->disp_y = ay - arc;
            pl->shadow_s = 0.55f + 0.45f * (1.0f - sinf(t * 3.14159f));
        } else {
            float dx = tx - pl->disp_x, dy = ty - pl->disp_y;
            if (dx*dx + dy*dy < 0.25f) { pl->disp_x = tx; pl->disp_y = ty; }
            else { pl->disp_x += dx * 0.22f; pl->disp_y += dy * 0.22f; }
            pl->shadow_s += (1.0f - pl->shadow_s) * 0.15f;
        }
    }
    if (g->popup.active) { g->popup.life -= 0.016f; g->popup.y -= 0.7f; if (g->popup.life <= 0) g->popup.active = 0; }
    update_fx(g);
}

void game_update(GameContext *g, uint32_t buttons) {
    Player *p = &g->players[g->current_player]; BoardNode *n;
    g->frame++; g->anim_t += 0.016f; g->state_timer++;
    if (g->hop_cooldown > 0) g->hop_cooldown--;
    if (g->land_flash > 0) g->land_flash--;
    update_display_positions(g);

    switch (g->state) {
    case STATE_TITLE:
        if (buttons & 0x10) { g->state = STATE_SETUP; g->state_timer = 0; } break;
    case STATE_SETUP:
        if (buttons & 0x20) g->player_count = (g->player_count % 4) + 1;
        if (buttons & 0x40) g->team_mode = !g->team_mode;
        if (buttons & 0x10) {
            int i;
            for (i = 0; i < g->player_count; ++i) {
                float x, y;
                g->players[i].is_human = (i == 0); g->players[i].diff = DIFF_NORMAL;
                g->players[i].coins = 12; g->players[i].stars = 0;
                g->players[i].node = g->board->start_node; g->players[i].item_count = 0;
                g->players[i].shielded = false; g->players[i].shadow_s = 1.0f;
                g->players[i].team = g->team_mode ? ((i < 2) ? 0 : 1) : i;
                node_world(g, g->players[i].node, &x, &y);
                g->players[i].disp_x = x; g->players[i].disp_y = y; g->players[i].hop_t = 1.0f;
            }
            if (g->players[0].is_human) give_item(&g->players[0], ITEM_DOUBLE_DICE);
            g->state = STATE_BOARD_SELECT; g->state_timer = 0;
        } break;
    case STATE_BOARD_SELECT:
        if (buttons & 0x20) {
            int i; g->board_idx = (g->board_idx + 1) % BOARD_COUNT; g->board = &g_boards[g->board_idx];
            for (i = 0; i < g->player_count; ++i) {
                float x, y; g->players[i].node = g->board->start_node;
                node_world(g, g->players[i].node, &x, &y);
                g->players[i].disp_x = x; g->players[i].disp_y = y;
            }
        }
        if (buttons & 0x10) { g->round = 1; g->current_player = 0; g->state = STATE_TURN_START; g->state_timer = 0; }
        break;
    case STATE_TURN_START:
        p->celebrating = false; g->dice_value = g->dice_value2 = 0; g->move_remaining = 0;
        g->double_dice = false; g->boost_steps = 0; g->branch_choice = 0; g->item_cursor = 0;
        g->state = (p->item_count > 0) ? STATE_ITEM : STATE_DICE_ROLL; g->state_timer = 0; break;
    case STATE_ITEM:
        if (p->is_human) {
            if (buttons & 0x20 && p->item_count > 0) g->item_cursor = (g->item_cursor + 1) % (p->item_count + 1);
            if (buttons & 0x10) {
                if (g->item_cursor < p->item_count) use_item(g, p, g->item_cursor);
                g->state = STATE_DICE_ROLL; g->state_timer = 0;
            }
        } else if (g->state_timer >= ITEM_CPU_DELAY) {
            if (p->item_count > 0 && rand_range(0, 2) != 0) use_item(g, p, 0);
            g->state = STATE_DICE_ROLL; g->state_timer = 0;
        } break;
    case STATE_DICE_ROLL:
        if (p->is_human) {
            if (buttons & 0x10) {
                g->dice_value = rand_range(1, 6);
                if (g->double_dice) { g->dice_value2 = rand_range(1, 6); g->move_remaining = g->dice_value + g->dice_value2 + g->boost_steps; }
                else { g->dice_value2 = 0; g->move_remaining = g->dice_value + g->boost_steps; }
                p->celebrating = (g->move_remaining >= 8);
                g->state = STATE_MOVE; g->state_timer = 0; g->hop_cooldown = 3;
                spawn_fx(g, 760, 120, 0xFFFFFFFF, 8);
            }
        } else if (g->state_timer >= DICE_CPU_DELAY) {
            int bias = (p->diff == DIFF_HARD) ? 1 : (p->diff == DIFF_EASY ? -1 : 0);
            g->dice_value = rand_range(1, 6) + bias;
            if (g->dice_value < 1) g->dice_value = 1; if (g->dice_value > 6) g->dice_value = 6;
            if (g->double_dice) { g->dice_value2 = rand_range(1, 6); g->move_remaining = g->dice_value + g->dice_value2 + g->boost_steps; }
            else { g->dice_value2 = 0; g->move_remaining = g->dice_value + g->boost_steps; }
            p->celebrating = (g->move_remaining >= 8);
            g->state = STATE_MOVE; g->state_timer = 0; g->hop_cooldown = 3;
        } break;
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
        if (buttons & 0x20) { n = &g->board->nodes[p->node]; if (n->next_count > 0) g->branch_choice = (g->branch_choice + 1) % n->next_count; }
        if (buttons & 0x10) { n = &g->board->nodes[p->node]; if (g->branch_choice >= n->next_count) g->branch_choice = 0;
            start_hop(g, p, n->next[g->branch_choice]); g->state = STATE_MOVE; g->state_timer = 0; }
        break;
    case STATE_LAND:
        if (g->state_timer == 1) apply_land(g, p);
        if (g->state == STATE_SHOP) break;
        if (g->state_timer >= LAND_HOLD) {
            if ((g->round % 3) == 0 && g->current_player == g->player_count - 1) mg_begin(g);
            else next_player(g);
        } break;
    case STATE_SHOP:
        do_shop(g, p, buttons); if (g->state != STATE_SHOP) next_player(g); break;
    case STATE_MG_INTRO:
        if (g->state_timer >= MG_INTRO_HOLD || (buttons & 0x10)) {
            g->state = STATE_MG_PLAY; g->state_timer = 0; g->mg_timer = 0;
            g->mg_meter = 0; g->mg_marker = 0.15f; g->mg_marker_dir = 1; g->mg_pressed = 0; g->mg_human_score = 0;
        } break;
    case STATE_MG_PLAY:
        g->mg_timer++;
        switch (g->mg_type) {
        case MG_MASH:
            if (buttons & 0x10) { g->mg_meter += 0.048f; if (g->mg_meter > 1) g->mg_meter = 1; spawn_fx(g, 424, 280 - g->mg_meter * 120, 0xFF2ECC71, 2); }
            g->mg_meter -= 0.0025f; if (g->mg_meter < 0) g->mg_meter = 0;
            if (g->mg_timer >= MG_MASH_TIME) { g->mg_human_score = (int)(g->mg_meter * 100); mg_finish_play(g); }
            break;
        case MG_TIMING:
            g->mg_marker += g->mg_marker_dir * 0.016f;
            if (g->mg_marker >= 1) { g->mg_marker = 1; g->mg_marker_dir = -1; }
            if (g->mg_marker <= 0) { g->mg_marker = 0; g->mg_marker_dir = 1; }
            if (buttons & 0x10 && !g->mg_pressed) {
                float dist = fabsf(g->mg_marker - 0.5f);
                g->mg_human_score = (int)((1.0f - dist * 2.0f) * 100.0f);
                if (g->mg_human_score < 0) g->mg_human_score = 0;
                g->mg_pressed = 1; spawn_fx(g, 200 + g->mg_marker * 448, 260, 0xFFF1C40F, 8); mg_finish_play(g);
            } else if (g->mg_timer >= MG_TIMING_TIME) { g->mg_human_score = 5; mg_finish_play(g); }
            break;
        case MG_REACTION: case MG_HOLD: case MG_COUNT: default:
            if (buttons & 0x10) {
                if (g->mg_timer < g->mg_go_at) { g->mg_human_score = 5; g->mg_pressed = 1; mg_finish_play(g); }
                else if (!g->mg_pressed) {
                    int delay = g->mg_timer - g->mg_go_at;
                    g->mg_human_score = 100 - delay * 2; if (g->mg_human_score < 10) g->mg_human_score = 10;
                    g->mg_pressed = 1; spawn_fx(g, 424, 240, 0xFF2ECC71, 10); mg_finish_play(g);
                }
            } else if (g->mg_timer >= MG_REACT_MAX) { g->mg_human_score = 8; mg_finish_play(g); }
            break;
        } break;
    case STATE_MG_RESULT:
        if (g->state_timer == 1) mg_rank_and_pay(g);
        if (g->state_timer >= MG_RESULT_HOLD || (buttons & 0x10)) next_player(g);
        break;
    case STATE_ROUND_END:
        if (g->state_timer >= ROUND_HOLD) g->state = STATE_TURN_START; break;
    case STATE_RESULTS:
        if (buttons & 0x10) g->state = STATE_EXIT; break;
    default: break;
    }
}

void game_shutdown(GameContext *g) { LOG_I("game_shutdown"); (void)g; }

/*
 * Board Game – core loop
 * Hop-by-hop movement, branch choice, land juice, shop, contests.
 * Kept light for PS3 HEN.
 */

#include "game.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static Board g_boards[BOARD_COUNT];

/* frames between token hops – higher = slower / more readable */
#define HOP_FRAMES      10
#define LAND_HOLD       35
#define DICE_CPU_DELAY  40
#define ROUND_HOLD      50
#define CONTEST_HOLD    55

static int rand_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (rand() % (hi - lo + 1));
}

static void spawn_popup(GameContext *g, float x, float y, int value, int kind)
{
    g->popup.active = 1;
    g->popup.x = x;
    g->popup.y = y;
    g->popup.value = value;
    g->popup.kind = kind;
    g->popup.life = 1.2f;
}

static void node_world(GameContext *g, int node, float *x, float *y)
{
    /* same mapping as renderer playfield */
    float ox = 40.0f, oy = 70.0f, sx = 640.0f, sy = 360.0f;
    BoardNode *n = &g->board->nodes[node];
    *x = ox + n->x * sx;
    *y = oy + n->y * sy;
}

void game_init(GameContext *g)
{
    int i;
    LOG_I("game_init");
    memset(g, 0, sizeof(*g));
    srand((unsigned)time(NULL));

    board_load_visual(&g_boards[0]);
    board_load_geo(&g_boards[1]);

    g->state = STATE_TITLE;
    g->board_idx = 0;
    g->board = &g_boards[0];
    g->player_count = 4;
    g->max_rounds = 15;
    g->current_player = 0;
    g->round = 1;

    for (i = 0; i < MAX_PLAYERS; ++i) {
        float x, y;
        g->players[i].id = i;
        g->players[i].is_human = (i == 0);
        g->players[i].diff = DIFF_NORMAL;
        g->players[i].coins = 10;
        g->players[i].stars = 0;
        g->players[i].node = g->board->start_node;
        g->players[i].color = i;
        g->players[i].celebrating = false;
        node_world(g, g->players[i].node, &x, &y);
        g->players[i].disp_x = x;
        g->players[i].disp_y = y;
        g->players[i].hop_t = 1.0f;
    }

    LOG_I("Game ready – 1H+3CPU, Visual board, 15 rounds");
}

static void relocate_star(GameContext *g)
{
    int tries = 0;
    int new_node;
    do {
        new_node = rand_range(0, g->board->node_count - 1);
        tries++;
    } while ((g->board->nodes[new_node].type == SPACE_SHOP ||
              new_node == g->board->star_node) && tries < 40);

    if (g->board->nodes[g->board->star_node].type == SPACE_STAR)
        g->board->nodes[g->board->star_node].type = SPACE_NORMAL;

    g->board->star_node = new_node;
    g->board->nodes[new_node].type = SPACE_STAR;
    LOG_I("Star relocated to node %d", new_node);
}

static int cpu_choose_path(GameContext *g, Player *p, int from)
{
    BoardNode *n = &g->board->nodes[from];
    int best, score_best, i;
    if (n->next_count <= 1) return n->next[0];

    best = n->next[0];
    score_best = -999;
    for (i = 0; i < n->next_count; ++i) {
        int cand = n->next[i];
        int score = 0;
        SpaceType t = g->board->nodes[cand].type;
        if (t == SPACE_STAR) score += 50;
        if (t == SPACE_PLUS_COIN) score += 10;
        if (t == SPACE_SHOP) score += 5;
        if (t == SPACE_MINUS_COIN) score -= 15;
        if (t == SPACE_HIDDEN_COIN || t == SPACE_HIDDEN_STAR) score += 8;

        if (p->diff == DIFF_HARD) score += rand_range(0, 5);
        else if (p->diff == DIFF_EASY) score += rand_range(-20, 20);
        else score += rand_range(-5, 8);

        if (score > score_best) {
            score_best = score;
            best = cand;
        }
    }
    return best;
}

static void start_hop(GameContext *g, Player *p, int next_node)
{
    p->node = next_node;
    p->hop_t = 0.0f;
    g->hop_cooldown = HOP_FRAMES;
    g->move_remaining--;
}

static void apply_land(GameContext *g, Player *p)
{
    BoardNode *n = &g->board->nodes[p->node];
    float px, py;
    int delta;

    node_world(g, p->node, &px, &py);
    g->last_land_type = (int)n->type;
    g->land_flash = LAND_HOLD;

    LOG_I("Player %d lands on node %d type %d", p->id, p->node, n->type);

    switch (n->type) {
        case SPACE_PLUS_COIN:
            delta = 3 + rand_range(0, 2);
            p->coins += delta;
            spawn_popup(g, px, py - 30, delta, 0);
            break;
        case SPACE_MINUS_COIN:
            delta = 3;
            p->coins -= delta;
            if (p->coins < 0) p->coins = 0;
            spawn_popup(g, px, py - 30, -delta, 2);
            break;
        case SPACE_EVENT:
            if (rand_range(0, 1)) {
                p->coins += 5;
                spawn_popup(g, px, py - 30, 5, 4);
            } else {
                delta = (p->coins > 2) ? 2 : p->coins;
                p->coins -= delta;
                spawn_popup(g, px, py - 30, -delta, 4);
            }
            break;
        case SPACE_SHOP:
            spawn_popup(g, px, py - 30, 0, 3);
            g->state = STATE_SHOP;
            g->state_timer = 0;
            return;
        case SPACE_STAR:
            if (p->coins >= 20) {
                p->coins -= 20;
                p->stars += 1;
                spawn_popup(g, px, py - 30, 1, 1);
                relocate_star(g);
            } else {
                spawn_popup(g, px, py - 30, 0, 2); /* can't afford */
            }
            break;
        case SPACE_HIDDEN_COIN:
            if (!n->hidden_revealed) {
                n->hidden_revealed = true;
                p->coins += 10;
                spawn_popup(g, px, py - 30, 10, 0);
            }
            break;
        case SPACE_HIDDEN_STAR:
            if (!n->hidden_revealed) {
                n->hidden_revealed = true;
                p->stars += 1;
                spawn_popup(g, px, py - 30, 1, 1);
            }
            break;
        default:
            break;
    }
}

static void do_shop(GameContext *g, Player *p, uint32_t buttons)
{
    if (!p->is_human) {
        /* CPU decides once after a short think */
        if (g->state_timer < 25) return;
        if (p->coins >= 20 && p->diff != DIFF_EASY) {
            p->coins -= 20;
            p->stars += 1;
            relocate_star(g);
            LOG_I("CPU %d bought star in shop", p->id);
        } else if (p->coins >= 5) {
            p->coins -= 5;
            LOG_I("CPU %d bought item", p->id);
        }
        g->state = STATE_TURN_START;
        return;
    }

    /* Human: CROSS = buy star if possible, CIRCLE = leave */
    if (buttons & 0x10) {
        if (p->coins >= 20) {
            p->coins -= 20;
            p->stars += 1;
            relocate_star(g);
            LOG_I("Human bought star");
        }
        g->state = STATE_TURN_START;
    } else if (buttons & 0x20) {
        g->state = STATE_TURN_START;
    }
}

static void next_player(GameContext *g)
{
    g->current_player = (g->current_player + 1) % g->player_count;
    if (g->current_player == 0) {
        g->round++;
        g->star_relocated_this_round = false;
        relocate_star(g);
        g->state = STATE_ROUND_END;
        g->state_timer = 0;
        LOG_I("Round complete -> %d", g->round);
        if (g->round > g->max_rounds) {
            g->state = STATE_RESULTS;
            LOG_I("Match over");
        }
    } else {
        g->state = STATE_TURN_START;
        g->state_timer = 0;
    }
}

static void update_display_positions(GameContext *g)
{
    int i;
    for (i = 0; i < g->player_count; ++i) {
        Player *pl = &g->players[i];
        float tx, ty;
        node_world(g, pl->node, &tx, &ty);
        /* slight stack offset so tokens don't fully overlap */
        tx += (float)(i % 4) * 10.0f - 15.0f;

        if (pl->hop_t < 1.0f) {
            pl->hop_t += 0.12f;
            if (pl->hop_t > 1.0f) pl->hop_t = 1.0f;
            /* ease-out hop */
            {
                float t = pl->hop_t;
                float e = 1.0f - (1.0f - t) * (1.0f - t);
                pl->disp_x += (tx - pl->disp_x) * e * 0.35f + (tx - pl->disp_x) * 0.15f;
                pl->disp_y += (ty - pl->disp_y) * e * 0.35f + (ty - pl->disp_y) * 0.15f;
                /* arc */
                pl->disp_y -= sinf(t * 3.14159f) * 12.0f;
            }
        } else {
            pl->disp_x += (tx - pl->disp_x) * 0.25f;
            pl->disp_y += (ty - pl->disp_y) * 0.25f;
        }
    }

    if (g->popup.active) {
        g->popup.life -= 0.016f;
        g->popup.y -= 0.6f;
        if (g->popup.life <= 0.0f) g->popup.active = 0;
    }
}

void game_update(GameContext *g, uint32_t buttons)
{
    Player *p = &g->players[g->current_player];
    BoardNode *n;

    g->frame++;
    g->anim_t += 0.016f;
    g->state_timer++;
    if (g->hop_cooldown > 0) g->hop_cooldown--;
    if (g->land_flash > 0) g->land_flash--;

    update_display_positions(g);

    switch (g->state) {
    case STATE_TITLE:
        if (buttons & 0x10) {
            g->state = STATE_SETUP;
            g->state_timer = 0;
            LOG_I("Title -> Setup");
        }
        break;

    case STATE_SETUP:
        if (buttons & 0x20)
            g->player_count = (g->player_count % 4) + 1;
        if (buttons & 0x10) {
            int i;
            for (i = 0; i < g->player_count; ++i) {
                float x, y;
                g->players[i].is_human = (i == 0);
                g->players[i].diff = DIFF_NORMAL;
                g->players[i].coins = 10;
                g->players[i].stars = 0;
                g->players[i].node = g->board->start_node;
                node_world(g, g->players[i].node, &x, &y);
                g->players[i].disp_x = x;
                g->players[i].disp_y = y;
                g->players[i].hop_t = 1.0f;
            }
            g->state = STATE_BOARD_SELECT;
            g->state_timer = 0;
            LOG_I("Setup – %d players", g->player_count);
        }
        break;

    case STATE_BOARD_SELECT:
        if (buttons & 0x20) {
            int i;
            g->board_idx = (g->board_idx + 1) % BOARD_COUNT;
            g->board = &g_boards[g->board_idx];
            for (i = 0; i < g->player_count; ++i) {
                float x, y;
                g->players[i].node = g->board->start_node;
                node_world(g, g->players[i].node, &x, &y);
                g->players[i].disp_x = x;
                g->players[i].disp_y = y;
            }
            LOG_I("Board %s", g->board->name);
        }
        if (buttons & 0x10) {
            g->round = 1;
            g->current_player = 0;
            g->state = STATE_TURN_START;
            g->state_timer = 0;
            LOG_I("Match start on %s", g->board->name);
        }
        break;

    case STATE_TURN_START:
        p->celebrating = false;
        g->dice_value = 0;
        g->move_remaining = 0;
        g->branch_choice = 0;
        g->state = STATE_DICE_ROLL;
        g->state_timer = 0;
        LOG_D("Turn P%d", p->id);
        break;

    case STATE_DICE_ROLL:
        if (p->is_human) {
            if (buttons & 0x10) {
                g->dice_value = rand_range(1, 6);
                p->celebrating = (g->dice_value >= 5);
                g->move_remaining = g->dice_value;
                g->state = STATE_MOVE;
                g->state_timer = 0;
                g->hop_cooldown = 4;
                LOG_I("Human rolled %d", g->dice_value);
            }
        } else if (g->state_timer >= DICE_CPU_DELAY) {
            int bias = (p->diff == DIFF_HARD) ? 1 : (p->diff == DIFF_EASY ? -1 : 0);
            g->dice_value = rand_range(1, 6) + bias;
            if (g->dice_value < 1) g->dice_value = 1;
            if (g->dice_value > 6) g->dice_value = 6;
            p->celebrating = (g->dice_value >= 5);
            g->move_remaining = g->dice_value;
            g->state = STATE_MOVE;
            g->state_timer = 0;
            g->hop_cooldown = 4;
            LOG_I("CPU %d rolled %d", p->id, g->dice_value);
        }
        break;

    case STATE_MOVE:
        if (g->move_remaining <= 0) {
            g->state = STATE_LAND;
            g->state_timer = 0;
            break;
        }
        if (g->hop_cooldown > 0) break;

        n = &g->board->nodes[p->node];
        if (n->next_count == 0) {
            g->move_remaining = 0;
            g->state = STATE_LAND;
            g->state_timer = 0;
        } else if (n->next_count == 1) {
            start_hop(g, p, n->next[0]);
        } else {
            /* fork */
            if (p->is_human) {
                g->branch_choice = 0;
                g->state = STATE_BRANCH;
                g->state_timer = 0;
            } else {
                start_hop(g, p, cpu_choose_path(g, p, p->node));
            }
        }
        break;

    case STATE_BRANCH:
        /* LEFT/RIGHT via CIRCLE cycle, CROSS confirm */
        if (buttons & 0x20) {
            n = &g->board->nodes[p->node];
            if (n->next_count > 0)
                g->branch_choice = (g->branch_choice + 1) % n->next_count;
        }
        if (buttons & 0x10) {
            n = &g->board->nodes[p->node];
            if (g->branch_choice < 0) g->branch_choice = 0;
            if (g->branch_choice >= n->next_count) g->branch_choice = 0;
            start_hop(g, p, n->next[g->branch_choice]);
            g->state = STATE_MOVE;
            g->state_timer = 0;
        }
        break;

    case STATE_LAND:
        if (g->state_timer == 1)
            apply_land(g, p);
        if (g->state == STATE_SHOP) break;
        if (g->state_timer >= LAND_HOLD) {
            if ((g->round % 3) == 0 && g->current_player == g->player_count - 1) {
                g->state = STATE_MINIGAME_SIM;
                g->state_timer = 0;
            } else {
                next_player(g);
            }
        }
        break;

    case STATE_SHOP:
        do_shop(g, p, buttons);
        if (g->state != STATE_SHOP)
            next_player(g);
        break;

    case STATE_MINIGAME_SIM:
        if (g->state_timer == 1) {
            int i;
            LOG_I("Contest!");
            for (i = 0; i < g->player_count; ++i) {
                int roll = rand_range(0, 100);
                int bonus;
                if (g->players[i].diff == DIFF_HARD) roll += 15;
                if (g->players[i].diff == DIFF_EASY) roll -= 10;
                if (g->players[i].is_human) roll += 5;
                if (roll > 70) bonus = 10;
                else if (roll > 40) bonus = 5;
                else bonus = 2;
                g->players[i].coins += bonus;
                g->contest_rewards[i] = bonus;
            }
        }
        if (g->state_timer >= CONTEST_HOLD)
            next_player(g);
        break;

    case STATE_ROUND_END:
        if (g->state_timer >= ROUND_HOLD)
            g->state = STATE_TURN_START;
        break;

    case STATE_RESULTS:
        if (buttons & 0x10) g->state = STATE_EXIT;
        break;

    default:
        break;
    }
}

void game_shutdown(GameContext *g)
{
    LOG_I("game_shutdown");
    (void)g;
}

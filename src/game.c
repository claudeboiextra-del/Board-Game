/*
 * Board Game – fun pass
 * Items, double dice, team mode, sparks, paced hops, shops, contests.
 */

#include "game.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static Board g_boards[BOARD_COUNT];

#define HOP_FRAMES      9
#define LAND_HOLD       32
#define DICE_CPU_DELAY  36
#define ROUND_HOLD      45
#define CONTEST_HOLD    50
#define ITEM_CPU_DELAY  20

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
    g->popup.life = 1.3f;
}

static void spawn_fx(GameContext *g, float x, float y, uint32_t color, int count)
{
    int i, n = 0;
    for (i = 0; i < MAX_FX && n < count; ++i) {
        if (g->fx[i].active) continue;
        g->fx[i].active = 1;
        g->fx[i].x = x + (float)rand_range(-6, 6);
        g->fx[i].y = y + (float)rand_range(-4, 4);
        g->fx[i].vx = (float)rand_range(-40, 40) * 0.08f;
        g->fx[i].vy = (float)rand_range(-60, -10) * 0.08f;
        g->fx[i].life = 0.4f + (float)rand_range(0, 20) * 0.02f;
        g->fx[i].color = color;
        n++;
    }
}

static void node_world(GameContext *g, int node, float *x, float *y)
{
    float ox = 40.0f, oy = 70.0f, sx = 640.0f, sy = 360.0f;
    BoardNode *n = &g->board->nodes[node];
    *x = ox + n->x * sx;
    *y = oy + n->y * sy;
}

static int give_item(Player *p, ItemType it)
{
    if (p->item_count >= MAX_ITEMS) return 0;
    p->items[p->item_count++] = it;
    return 1;
}

static ItemType take_item(Player *p, int slot)
{
    ItemType it;
    int i;
    if (slot < 0 || slot >= p->item_count) return ITEM_NONE;
    it = p->items[slot];
    for (i = slot; i < p->item_count - 1; ++i)
        p->items[i] = p->items[i + 1];
    p->item_count--;
    return it;
}

static ItemType random_item(void)
{
    static const ItemType pool[] = {
        ITEM_DOUBLE_DICE, ITEM_BOOST, ITEM_STEAL, ITEM_SHIELD, ITEM_BOOST
    };
    return pool[rand_range(0, 4)];
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
    g->board = &g_boards[0];
    g->player_count = 4;
    g->max_rounds = 15;
    g->team_mode = false;

    for (i = 0; i < MAX_PLAYERS; ++i) {
        float x, y;
        g->players[i].id = i;
        g->players[i].is_human = (i == 0);
        g->players[i].diff = DIFF_NORMAL;
        g->players[i].coins = 12;
        g->players[i].stars = 0;
        g->players[i].node = g->board->start_node;
        g->players[i].color = i;
        g->players[i].team = (i < 2) ? 0 : 1;
        node_world(g, g->players[i].node, &x, &y);
        g->players[i].disp_x = x;
        g->players[i].disp_y = y;
        g->players[i].hop_t = 1.0f;
    }

    /* starter item for human so they can try systems immediately */
    give_item(&g->players[0], ITEM_DOUBLE_DICE);

    LOG_I("Game ready – items + team option");
}

static void relocate_star(GameContext *g)
{
    int tries = 0, new_node;
    do {
        new_node = rand_range(0, g->board->node_count - 1);
        tries++;
    } while ((g->board->nodes[new_node].type == SPACE_SHOP ||
              new_node == g->board->star_node) && tries < 40);

    if (g->board->nodes[g->board->star_node].type == SPACE_STAR)
        g->board->nodes[g->board->star_node].type = SPACE_NORMAL;

    g->board->star_node = new_node;
    g->board->nodes[new_node].type = SPACE_STAR;
    LOG_I("Star -> node %d", new_node);
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
        if (t == SPACE_SHOP) score += 8;
        if (t == SPACE_MINUS_COIN) score -= 15;
        if (t == SPACE_HIDDEN_COIN || t == SPACE_HIDDEN_STAR) score += 8;
        if (p->diff == DIFF_HARD) score += rand_range(0, 5);
        else if (p->diff == DIFF_EASY) score += rand_range(-20, 20);
        else score += rand_range(-5, 8);
        if (score > score_best) { score_best = score; best = cand; }
    }
    return best;
}

static void start_hop(GameContext *g, Player *p, int next_node)
{
    float x, y;
    p->node = next_node;
    p->hop_t = 0.0f;
    g->hop_cooldown = HOP_FRAMES;
    g->move_remaining--;
    node_world(g, next_node, &x, &y);
    spawn_fx(g, x, y, 0x88FFFFFF, 3);
}

static void apply_land(GameContext *g, Player *p)
{
    BoardNode *n = &g->board->nodes[p->node];
    float px, py;
    int delta;

    node_world(g, p->node, &px, &py);
    g->last_land_type = (int)n->type;
    g->land_flash = LAND_HOLD;

    switch (n->type) {
        case SPACE_PLUS_COIN:
            delta = 3 + rand_range(0, 3);
            p->coins += delta;
            spawn_popup(g, px, py - 30, delta, 0);
            spawn_fx(g, px, py, 0xFF2ECC71, 6);
            break;
        case SPACE_MINUS_COIN:
            if (p->shielded) {
                p->shielded = false;
                spawn_popup(g, px, py - 30, 0, 5);
                spawn_fx(g, px, py, 0xFF3498DB, 5);
            } else {
                delta = 3 + rand_range(0, 1);
                p->coins -= delta;
                if (p->coins < 0) p->coins = 0;
                spawn_popup(g, px, py - 30, -delta, 2);
                spawn_fx(g, px, py, 0xFFE74C3C, 5);
            }
            break;
        case SPACE_EVENT:
            if (rand_range(0, 2) == 0 && give_item(p, random_item())) {
                spawn_popup(g, px, py - 30, 1, 5);
                spawn_fx(g, px, py, 0xFF9B59B6, 8);
            } else if (rand_range(0, 1)) {
                p->coins += 6;
                spawn_popup(g, px, py - 30, 6, 4);
                spawn_fx(g, px, py, 0xFF9B59B6, 5);
            } else {
                delta = (p->coins > 3) ? 3 : p->coins;
                p->coins -= delta;
                spawn_popup(g, px, py - 30, -delta, 4);
            }
            break;
        case SPACE_SHOP:
            spawn_popup(g, px, py - 30, 0, 3);
            g->state = STATE_SHOP;
            g->state_timer = 0;
            g->item_cursor = 0;
            return;
        case SPACE_STAR:
            if (p->coins >= 20) {
                p->coins -= 20;
                p->stars += 1;
                spawn_popup(g, px, py - 30, 1, 1);
                spawn_fx(g, px, py, 0xFFF1C40F, 12);
                p->celebrating = true;
                relocate_star(g);
            } else {
                spawn_popup(g, px, py - 30, 0, 2);
            }
            break;
        case SPACE_HIDDEN_COIN:
            if (!n->hidden_revealed) {
                n->hidden_revealed = true;
                p->coins += 12;
                spawn_popup(g, px, py - 30, 12, 0);
                spawn_fx(g, px, py, 0xFF1ABC9C, 8);
            }
            break;
        case SPACE_HIDDEN_STAR:
            if (!n->hidden_revealed) {
                n->hidden_revealed = true;
                p->stars += 1;
                spawn_popup(g, px, py - 30, 1, 1);
                spawn_fx(g, px, py, 0xFFF1C40F, 10);
                p->celebrating = true;
            }
            break;
        default:
            break;
    }
}

static void do_shop(GameContext *g, Player *p, uint32_t buttons)
{
    /* shop offers: 0=star(20), 1=random item(8), 2=leave */
    if (!p->is_human) {
        if (g->state_timer < 22) return;
        if (p->coins >= 20 && (p->stars < 3 || p->diff == DIFF_HARD)) {
            p->coins -= 20; p->stars += 1; relocate_star(g);
            spawn_fx(g, p->disp_x, p->disp_y, 0xFFF1C40F, 8);
        } else if (p->coins >= 8 && p->item_count < MAX_ITEMS) {
            p->coins -= 8;
            give_item(p, random_item());
        }
        g->state = STATE_TURN_START;
        return;
    }

    if (buttons & 0x20)
        g->item_cursor = (g->item_cursor + 1) % 3;

    if (buttons & 0x10) {
        if (g->item_cursor == 0 && p->coins >= 20) {
            p->coins -= 20; p->stars += 1; relocate_star(g);
            spawn_fx(g, p->disp_x, p->disp_y, 0xFFF1C40F, 10);
            p->celebrating = true;
        } else if (g->item_cursor == 1 && p->coins >= 8 && p->item_count < MAX_ITEMS) {
            p->coins -= 8;
            give_item(p, random_item());
            spawn_fx(g, p->disp_x, p->disp_y, 0xFF9B59B6, 6);
        }
        g->state = STATE_TURN_START;
    }
}

static void use_item(GameContext *g, Player *p, int slot)
{
    ItemType it = take_item(p, slot);
    int i, best, best_coins;

    switch (it) {
        case ITEM_DOUBLE_DICE:
            g->double_dice = true;
            spawn_popup(g, p->disp_x, p->disp_y - 40, 2, 5);
            break;
        case ITEM_BOOST:
            g->boost_steps = 3;
            spawn_popup(g, p->disp_x, p->disp_y - 40, 3, 5);
            break;
        case ITEM_STEAL:
            best = -1; best_coins = -1;
            for (i = 0; i < g->player_count; ++i) {
                if (i == p->id) continue;
                if (g->team_mode && g->players[i].team == p->team) continue;
                if (g->players[i].coins > best_coins) {
                    best_coins = g->players[i].coins;
                    best = i;
                }
            }
            if (best >= 0) {
                int steal = (g->players[best].coins >= 5) ? 5 : g->players[best].coins;
                g->players[best].coins -= steal;
                p->coins += steal;
                spawn_popup(g, p->disp_x, p->disp_y - 40, steal, 0);
                spawn_fx(g, g->players[best].disp_x, g->players[best].disp_y, 0xFFE74C3C, 5);
            }
            break;
        case ITEM_SHIELD:
            p->shielded = true;
            spawn_popup(g, p->disp_x, p->disp_y - 40, 1, 5);
            spawn_fx(g, p->disp_x, p->disp_y, 0xFF3498DB, 6);
            break;
        default:
            break;
    }
}

static void next_player(GameContext *g)
{
    g->double_dice = false;
    g->boost_steps = 0;
    g->current_player = (g->current_player + 1) % g->player_count;
    if (g->current_player == 0) {
        g->round++;
        relocate_star(g);
        g->state = STATE_ROUND_END;
        g->state_timer = 0;
        if (g->round > g->max_rounds)
            g->state = STATE_RESULTS;
    } else {
        g->state = STATE_TURN_START;
        g->state_timer = 0;
    }
}

static void update_fx(GameContext *g)
{
    int i;
    for (i = 0; i < MAX_FX; ++i) {
        if (!g->fx[i].active) continue;
        g->fx[i].x += g->fx[i].vx;
        g->fx[i].y += g->fx[i].vy;
        g->fx[i].vy += 0.15f;
        g->fx[i].life -= 0.016f;
        if (g->fx[i].life <= 0.0f) g->fx[i].active = 0;
    }
}

static void update_display_positions(GameContext *g)
{
    int i;
    for (i = 0; i < g->player_count; ++i) {
        Player *pl = &g->players[i];
        float tx, ty;
        node_world(g, pl->node, &tx, &ty);
        tx += (float)(i % 4) * 10.0f - 15.0f;

        if (pl->hop_t < 1.0f) {
            float t, e;
            pl->hop_t += 0.14f;
            if (pl->hop_t > 1.0f) pl->hop_t = 1.0f;
            t = pl->hop_t;
            e = 1.0f - (1.0f - t) * (1.0f - t);
            pl->disp_x += (tx - pl->disp_x) * (0.2f + e * 0.35f);
            pl->disp_y += (ty - pl->disp_y) * (0.2f + e * 0.35f);
            pl->disp_y -= sinf(t * 3.14159f) * 14.0f;
        } else {
            pl->disp_x += (tx - pl->disp_x) * 0.28f;
            pl->disp_y += (ty - pl->disp_y) * 0.28f;
        }
    }

    if (g->popup.active) {
        g->popup.life -= 0.016f;
        g->popup.y -= 0.7f;
        if (g->popup.life <= 0.0f) g->popup.active = 0;
    }
    update_fx(g);
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
        }
        break;

    case STATE_SETUP:
        /* Circle: cycle player count; Square: toggle team mode */
        if (buttons & 0x20)
            g->player_count = (g->player_count % 4) + 1;
        if (buttons & 0x40)
            g->team_mode = !g->team_mode;
        if (buttons & 0x10) {
            int i;
            for (i = 0; i < g->player_count; ++i) {
                float x, y;
                g->players[i].is_human = (i == 0);
                g->players[i].diff = DIFF_NORMAL;
                g->players[i].coins = 12;
                g->players[i].stars = 0;
                g->players[i].node = g->board->start_node;
                g->players[i].item_count = 0;
                g->players[i].shielded = false;
                g->players[i].team = g->team_mode ? ((i < 2) ? 0 : 1) : i;
                node_world(g, g->players[i].node, &x, &y);
                g->players[i].disp_x = x;
                g->players[i].disp_y = y;
                g->players[i].hop_t = 1.0f;
            }
            if (g->players[0].is_human)
                give_item(&g->players[0], ITEM_DOUBLE_DICE);
            g->state = STATE_BOARD_SELECT;
            g->state_timer = 0;
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
        }
        if (buttons & 0x10) {
            g->round = 1;
            g->current_player = 0;
            g->state = STATE_TURN_START;
            g->state_timer = 0;
        }
        break;

    case STATE_TURN_START:
        p->celebrating = false;
        g->dice_value = 0;
        g->dice_value2 = 0;
        g->move_remaining = 0;
        g->double_dice = false;
        g->boost_steps = 0;
        g->branch_choice = 0;
        g->item_cursor = 0;
        if (p->item_count > 0)
            g->state = STATE_ITEM;
        else
            g->state = STATE_DICE_ROLL;
        g->state_timer = 0;
        break;

    case STATE_ITEM:
        if (p->is_human) {
            if (buttons & 0x20 && p->item_count > 0)
                g->item_cursor = (g->item_cursor + 1) % (p->item_count + 1); /* last = skip */
            if (buttons & 0x10) {
                if (g->item_cursor < p->item_count)
                    use_item(g, p, g->item_cursor);
                g->state = STATE_DICE_ROLL;
                g->state_timer = 0;
            }
        } else if (g->state_timer >= ITEM_CPU_DELAY) {
            /* CPU: use steal/shield/double opportunistically */
            if (p->item_count > 0 && rand_range(0, 2) != 0)
                use_item(g, p, 0);
            g->state = STATE_DICE_ROLL;
            g->state_timer = 0;
        }
        break;

    case STATE_DICE_ROLL:
        if (p->is_human) {
            if (buttons & 0x10) {
                g->dice_value = rand_range(1, 6);
                if (g->double_dice) {
                    g->dice_value2 = rand_range(1, 6);
                    g->move_remaining = g->dice_value + g->dice_value2 + g->boost_steps;
                } else {
                    g->dice_value2 = 0;
                    g->move_remaining = g->dice_value + g->boost_steps;
                }
                p->celebrating = (g->move_remaining >= 8);
                g->state = STATE_MOVE;
                g->state_timer = 0;
                g->hop_cooldown = 3;
                spawn_fx(g, 760, 120, 0xFFFFFFFF, 8);
                LOG_I("Roll %d+%d +boost%d = %d", g->dice_value, g->dice_value2,
                      g->boost_steps, g->move_remaining);
            }
        } else if (g->state_timer >= DICE_CPU_DELAY) {
            int bias = (p->diff == DIFF_HARD) ? 1 : (p->diff == DIFF_EASY ? -1 : 0);
            g->dice_value = rand_range(1, 6) + bias;
            if (g->dice_value < 1) g->dice_value = 1;
            if (g->dice_value > 6) g->dice_value = 6;
            if (g->double_dice) {
                g->dice_value2 = rand_range(1, 6);
                g->move_remaining = g->dice_value + g->dice_value2 + g->boost_steps;
            } else {
                g->dice_value2 = 0;
                g->move_remaining = g->dice_value + g->boost_steps;
            }
            p->celebrating = (g->move_remaining >= 8);
            g->state = STATE_MOVE;
            g->state_timer = 0;
            g->hop_cooldown = 3;
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
        } else if (p->is_human) {
            g->branch_choice = 0;
            g->state = STATE_BRANCH;
            g->state_timer = 0;
        } else {
            start_hop(g, p, cpu_choose_path(g, p, p->node));
        }
        break;

    case STATE_BRANCH:
        if (buttons & 0x20) {
            n = &g->board->nodes[p->node];
            if (n->next_count > 0)
                g->branch_choice = (g->branch_choice + 1) % n->next_count;
        }
        if (buttons & 0x10) {
            n = &g->board->nodes[p->node];
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
            for (i = 0; i < g->player_count; ++i) {
                int roll = rand_range(0, 100);
                int bonus;
                if (g->players[i].diff == DIFF_HARD) roll += 12;
                if (g->players[i].is_human) roll += 8;
                if (roll > 75) bonus = 12;
                else if (roll > 45) bonus = 6;
                else bonus = 3;
                g->players[i].coins += bonus;
                g->contest_rewards[i] = bonus;
                spawn_fx(g, g->players[i].disp_x, g->players[i].disp_y, 0xFF9B59B6, 4);
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

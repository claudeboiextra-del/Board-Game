/*
 * Board Game – core game state machine
 * Lightweight, expressive turn flow with dice, movement, shops, stars,
 * CPU AI (3 difficulty tiers) and RNG contest substitute.
 */

#include "game.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static Board g_boards[BOARD_COUNT];
static int   g_round_opts[MAX_ROUNDS_OPTS] = {10, 15, 20, 30};

static int rand_range(int lo, int hi)
{
    return lo + (rand() % (hi - lo + 1));
}

void game_init(GameContext *g)
{
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

    /* default: 1 human + 3 CPUs (Normal) */
    int i;
    for (i = 0; i < MAX_PLAYERS; ++i) {
        g->players[i].id = i;
        g->players[i].is_human = (i == 0);
        g->players[i].diff = DIFF_NORMAL;
        g->players[i].coins = 10;
        g->players[i].stars = 0;
        g->players[i].node = g->board->start_node;
        g->players[i].color = i;
        g->players[i].celebrating = false;
    }

    LOG_I("Game ready – default 1H+3CPU, Visual board, 15 rounds");
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

    /* clear old */
    if (g->board->nodes[g->board->star_node].type == SPACE_STAR)
        g->board->nodes[g->board->star_node].type = SPACE_NORMAL;

    g->board->star_node = new_node;
    g->board->nodes[new_node].type = SPACE_STAR;
    LOG_I("Star relocated to node %d", new_node);
}

static int cpu_choose_path(GameContext *g, Player *p, int from)
{
    BoardNode *n = &g->board->nodes[from];
    if (n->next_count <= 1) return n->next[0];

    /* simple heuristic weighted by difficulty */
    int best = n->next[0];
    int score_best = -999;
    int i;
    for (i = 0; i < n->next_count; ++i) {
        int cand = n->next[i];
        int score = 0;
        SpaceType t = g->board->nodes[cand].type;
        if (t == SPACE_STAR) score += 50;
        if (t == SPACE_PLUS_COIN) score += 10;
        if (t == SPACE_SHOP) score += 5;
        if (t == SPACE_MINUS_COIN) score -= 15;
        if (t == SPACE_HIDDEN_COIN || t == SPACE_HIDDEN_STAR) score += 8;

        /* Hard AI looks further; Easy is noisier */
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

static void apply_land(GameContext *g, Player *p)
{
    BoardNode *n = &g->board->nodes[p->node];
    LOG_I("Player %d lands on node %d type %d", p->id, p->node, n->type);

    switch (n->type) {
        case SPACE_PLUS_COIN:
            p->coins += 3 + rand_range(0, 2);
            LOG_I("  +coins -> %d", p->coins);
            break;
        case SPACE_MINUS_COIN:
            p->coins -= 3;
            if (p->coins < 0) p->coins = 0;
            LOG_I("  -coins -> %d", p->coins);
            break;
        case SPACE_EVENT:
            /* tiny random event */
            if (rand_range(0, 1)) { p->coins += 5; LOG_I("  event +5"); }
            else { p->coins = (p->coins > 2) ? p->coins - 2 : 0; LOG_I("  event -2"); }
            break;
        case SPACE_SHOP:
            g->state = STATE_SHOP;
            return; /* stay in shop state */
        case SPACE_STAR:
            if (p->coins >= 20) {
                p->coins -= 20;
                p->stars += 1;
                LOG_I("  bought star! stars=%d coins=%d", p->stars, p->coins);
                relocate_star(g);
            }
            break;
        case SPACE_HIDDEN_COIN:
            if (!n->hidden_revealed) {
                n->hidden_revealed = true;
                p->coins += 10;
                LOG_I("  hidden coin block! +10");
            }
            break;
        case SPACE_HIDDEN_STAR:
            if (!n->hidden_revealed) {
                n->hidden_revealed = true;
                p->stars += 1;
                LOG_I("  hidden star discovered!");
            }
            break;
        default:
            break;
    }
}

static void do_shop(GameContext *g, Player *p)
{
    /* simple auto-buy logic for CPU; human presses confirm */
    if (!p->is_human) {
        if (p->coins >= 20 && p->diff != DIFF_EASY) {
            p->coins -= 20;
            p->stars += 1;
            LOG_I("CPU %d bought star in shop", p->id);
            relocate_star(g);
        } else if (p->coins >= 5) {
            p->coins -= 5; /* buy a small boost item (flavour) */
            LOG_I("CPU %d bought item", p->id);
        }
        g->state = STATE_TURN_START; /* next player handled outside */
        return;
    }
    /* human: for now auto-skip after one frame – full menu later */
    g->state = STATE_TURN_START;
}

static void next_player(GameContext *g)
{
    g->current_player = (g->current_player + 1) % g->player_count;
    if (g->current_player == 0) {
        /* full round completed */
        g->round++;
        g->star_relocated_this_round = false;
        relocate_star(g);
        g->state = STATE_ROUND_END;
        LOG_I("Round %d finished, starting next", g->round - 1);
        if (g->round > g->max_rounds) {
            g->state = STATE_RESULTS;
            LOG_I("Match over – going to results");
        }
    } else {
        g->state = STATE_TURN_START;
    }
}

void game_update(GameContext *g, uint32_t buttons)
{
    g->frame++;
    g->anim_t += 0.016f;
    Player *p = &g->players[g->current_player];

    switch (g->state) {
    case STATE_TITLE:
        if (buttons & 0x10) { /* CROSS-ish */
            g->state = STATE_SETUP;
            LOG_I("Title -> Setup");
        }
        break;

    case STATE_SETUP:
        /* very light: cycle player count with buttons, confirm with cross */
        if (buttons & 0x20) g->player_count = (g->player_count % 4) + 1;
        if (buttons & 0x10) {
            /* set human/cpu: first is always human for simplicity */
            int i;
            for (i = 0; i < g->player_count; ++i) {
                g->players[i].is_human = (i == 0);
                g->players[i].diff = DIFF_NORMAL;
                g->players[i].coins = 10;
                g->players[i].stars = 0;
                g->players[i].node = g->board->start_node;
            }
            g->state = STATE_BOARD_SELECT;
            LOG_I("Setup done – %d players", g->player_count);
        }
        break;

    case STATE_BOARD_SELECT:
        if (buttons & 0x20) {
            g->board_idx = (g->board_idx + 1) % BOARD_COUNT;
            g->board = &g_boards[g->board_idx];
            LOG_I("Selected board %s", g->board->name);
        }
        if (buttons & 0x10) {
            g->state = STATE_MATCH;
            g->round = 1;
            g->current_player = 0;
            g->state = STATE_TURN_START;
            LOG_I("Match start on %s", g->board->name);
        }
        break;

    case STATE_TURN_START:
        LOG_D("Turn start P%d", p->id);
        p->celebrating = false;
        g->dice_value = 0;
        g->move_remaining = 0;
        g->state = STATE_DICE_ROLL;
        break;

    case STATE_DICE_ROLL:
        if (p->is_human) {
            if (buttons & 0x10) {
                g->dice_value = rand_range(1, 6);
                p->celebrating = (g->dice_value >= 5);
                g->move_remaining = g->dice_value;
                LOG_I("Human rolled %d", g->dice_value);
                g->state = STATE_MOVE;
            }
        } else {
            /* CPU always rolls after a short delay (frame based) */
            if ((g->frame % 30) == 0) {
                int bias = (p->diff == DIFF_HARD) ? 1 : (p->diff == DIFF_EASY ? -1 : 0);
                g->dice_value = rand_range(1, 6) + bias;
                if (g->dice_value < 1) g->dice_value = 1;
                if (g->dice_value > 6) g->dice_value = 6;
                p->celebrating = (g->dice_value >= 5);
                g->move_remaining = g->dice_value;
                LOG_I("CPU %d rolled %d (diff %d)", p->id, g->dice_value, p->diff);
                g->state = STATE_MOVE;
            }
        }
        break;

    case STATE_MOVE:
        if (g->move_remaining > 0) {
            int next;
            if (g->board->nodes[p->node].next_count == 0) {
                next = p->node; /* stuck */
            } else if (g->board->nodes[p->node].next_count == 1) {
                next = g->board->nodes[p->node].next[0];
            } else {
                next = p->is_human ? g->board->nodes[p->node].next[0] /* first path for human simplicity */
                                   : cpu_choose_path(g, p, p->node);
            }
            p->node = next;
            g->move_remaining--;
            LOG_D("P%d step -> node %d (left %d)", p->id, p->node, g->move_remaining);
        } else {
            g->state = STATE_LAND;
        }
        break;

    case STATE_LAND:
        apply_land(g, p);
        if (g->state == STATE_LAND) { /* not redirected to shop */
            /* after land, chance of simulated contest every few turns */
            if ((g->round % 3) == 0 && g->current_player == 0) {
                g->state = STATE_MINIGAME_SIM;
            } else {
                next_player(g);
            }
        }
        break;

    case STATE_SHOP:
        do_shop(g, p);
        if (g->state != STATE_SHOP) next_player(g);
        break;

    case STATE_MINIGAME_SIM:
        /* RNG substitute – distribute coins based on difficulty weight */
        {
            int i;
            LOG_I("Simulated contest (RNG)");
            for (i = 0; i < g->player_count; ++i) {
                int roll = rand_range(0, 100);
                int bonus = 0;
                if (g->players[i].diff == DIFF_HARD) roll += 15;
                if (g->players[i].diff == DIFF_EASY) roll -= 10;
                if (roll > 70) bonus = 10;
                else if (roll > 40) bonus = 5;
                else bonus = 1;
                g->players[i].coins += bonus;
                LOG_I("  P%d contest reward +%d (total %d)", i, bonus, g->players[i].coins);
            }
            next_player(g);
        }
        break;

    case STATE_ROUND_END:
        /* brief pause then continue */
        if ((g->frame % 45) == 0) g->state = STATE_TURN_START;
        break;

    case STATE_RESULTS:
        /* stay until exit */
        if (buttons & 0x10) g->state = STATE_EXIT;
        break;

    case STATE_EXIT:
    default:
        break;
    }
}

void game_shutdown(GameContext *g)
{
    LOG_I("game_shutdown");
    (void)g;
}

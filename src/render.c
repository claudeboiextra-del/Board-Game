/*
 * Tiny3D rendering for Board Game
 * Retro *feel* (bouncy tokens, punchy colour pulses, simple particle-ish
 * flashes) without trying to look like a specific console generation.
 */

#include "game.h"
#include "logger.h"
#include <tiny3d.h>
#include <stdio.h>
#include <math.h>

/* simple 2D helpers using Tiny3D's 2D mode */

static void draw_rect(float x, float y, float w, float h, u32 rgba)
{
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x,     y,     0);
    tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x + w, y,     0);
    tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x + w, y + h, 0);
    tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x,     y + h, 0);
    tiny3d_VertexColor(rgba);
    tiny3d_End();
}

static void draw_circle_approx(float cx, float cy, float r, u32 rgba, int segs)
{
    int i;
    tiny3d_SetPolygon(TINY3D_TRIANGLE_FAN);
    tiny3d_VertexPos(cx, cy, 0);
    tiny3d_VertexColor(rgba);
    for (i = 0; i <= segs; ++i) {
        float a = (float)i / (float)segs * 6.28318f;
        tiny3d_VertexPos(cx + cosf(a) * r, cy + sinf(a) * r, 0);
        tiny3d_VertexColor(rgba);
    }
    tiny3d_End();
}

static u32 player_color(int idx)
{
    static const u32 cols[] = {
        0xFFE74C3C, /* red */
        0xFF3498DB, /* blue */
        0xFF2ECC71, /* green */
        0xFFF1C40F  /* yellow */
    };
    return cols[idx & 3];
}

static void draw_board(GameContext *g, float ox, float oy, float scale)
{
    Board *b = g->board;
    int i, j;

    /* links first (under nodes) */
    for (i = 0; i < b->node_count; ++i) {
        BoardNode *n = &b->nodes[i];
        float x1 = ox + n->x * scale;
        float y1 = oy + n->y * scale;
        for (j = 0; j < n->next_count; ++j) {
            BoardNode *m = &b->nodes[n->next[j]];
            float x2 = ox + m->x * scale;
            float y2 = oy + m->y * scale;
            /* crude thick line via thin rect – enough for feel */
            float dx = x2 - x1, dy = y2 - y1;
            float len = sqrtf(dx*dx + dy*dy);
            if (len < 1.0f) continue;
            draw_rect(x1 - 1.5f, y1 - 1.5f, len, 3.0f, 0xFF555577);
        }
    }

    /* nodes */
    for (i = 0; i < b->node_count; ++i) {
        BoardNode *n = &b->nodes[i];
        float x = ox + n->x * scale;
        float y = oy + n->y * scale;
        u32 col = 0xFFAAAAAA;
        float r = 8.0f;

        switch (n->type) {
            case SPACE_PLUS_COIN:  col = 0xFF27AE60; break;
            case SPACE_MINUS_COIN: col = 0xFFC0392B; break;
            case SPACE_EVENT:      col = 0xFF8E44AD; break;
            case SPACE_SHOP:       col = 0xFFD35400; r = 10.0f; break;
            case SPACE_STAR:       col = 0xFFF1C40F; r = 12.0f; break;
            case SPACE_HIDDEN_COIN:
            case SPACE_HIDDEN_STAR:
                if (n->hidden_revealed) col = 0xFF1ABC9C;
                else col = 0xFF7F8C8D;
                break;
            default: break;
        }
        draw_circle_approx(x, y, r, col, 10);
    }
}

static void draw_tokens(GameContext *g, float ox, float oy, float scale)
{
    int i;
    for (i = 0; i < g->player_count; ++i) {
        Player *p = &g->players[i];
        BoardNode *n = &g->board->nodes[p->node];
        float x = ox + n->x * scale;
        float y = oy + n->y * scale;

        /* bounce when celebrating (high roll) */
        float bounce = 0.0f;
        if (p->celebrating) {
            bounce = fabsf(sinf(g->anim_t * 8.0f)) * 12.0f;
        }
        /* slight offset so stacked tokens are visible */
        x += (i % 2) * 6.0f - 3.0f;
        y += (i / 2) * 6.0f - 3.0f - bounce;

        u32 col = player_color(p->color);
        draw_circle_approx(x, y, 11.0f, col, 12);
        /* inner white for contrast */
        draw_circle_approx(x, y, 5.0f, 0xFFFFFFFF, 8);
    }
}

static void draw_hud(GameContext *g)
{
    /* top bar background */
    draw_rect(0, 0, 848, 40, 0xCC1A1A2E);

    char buf[64];
    int i;
    for (i = 0; i < g->player_count; ++i) {
        Player *p = &g->players[i];
        snprintf(buf, sizeof(buf), "P%d %s%d $%d",
                 i + 1, p->is_human ? "H" : "C", p->stars, p->coins);
        /* Tiny3D has no built-in font here – we draw coloured blocks as stand-in
           and rely on log / future font for full text. Colour blocks give life. */
        float bx = 20.0f + i * 200.0f;
        draw_rect(bx, 8, 12, 24, player_color(p->color));
        if (i == g->current_player)
            draw_rect(bx - 4, 4, 20, 32, 0x88FFFFFF); /* current turn highlight */
    }

    /* round counter as bar */
    float prog = (float)g->round / (float)g->max_rounds;
    draw_rect(20, 480, 800 * prog, 8, 0xFF3498DB);
    draw_rect(20, 480, 800, 8, 0x44FFFFFF);
}

void game_render(GameContext *g)
{
    tiny3d_Clear(0xFF0D0D1A, TINY3D_CLEAR_ALL);
    tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);
    tiny3d_BlendFunc(TINY3D_BLEND_DEFAULT);

    tiny3d_Project2D();

    switch (g->state) {
    case STATE_TITLE:
        draw_rect(200, 180, 448, 120, 0xFF2C3E50);
        draw_circle_approx(424, 240, 30 + 8 * sinf(g->anim_t * 3), 0xFFF1C40F, 16);
        break;

    case STATE_SETUP:
    case STATE_BOARD_SELECT:
        draw_rect(100, 100, 648, 300, 0xFF1A1A2E);
        /* board preview dots */
        draw_board(g, 150, 150, 400);
        break;

    case STATE_MATCH:
    case STATE_TURN_START:
    case STATE_DICE_ROLL:
    case STATE_MOVE:
    case STATE_LAND:
    case STATE_SHOP:
    case STATE_ROUND_END:
    case STATE_MINIGAME_SIM:
        draw_board(g, 50, 50, 700);
        draw_tokens(g, 50, 50, 700);
        draw_hud(g);

        /* dice flash when rolling */
        if (g->state == STATE_DICE_ROLL || g->state == STATE_MOVE) {
            float pulse = 0.5f + 0.5f * sinf(g->anim_t * 10);
            u32 c = ((u32)(pulse * 255) << 24) | 0x00FFFFFF;
            draw_circle_approx(760, 80, 28, c, 12);
        }
        break;

    case STATE_RESULTS:
        draw_rect(150, 120, 548, 280, 0xFF16213E);
        /* winner highlight – highest stars then coins */
        {
            int best = 0, i;
            for (i = 1; i < g->player_count; ++i) {
                if (g->players[i].stars > g->players[best].stars ||
                    (g->players[i].stars == g->players[best].stars &&
                     g->players[i].coins > g->players[best].coins))
                    best = i;
            }
            draw_circle_approx(424, 260, 40 + 10 * sinf(g->anim_t * 4),
                               player_color(best), 16);
        }
        break;

    default:
        break;
    }

    tiny3d_Flip();
}

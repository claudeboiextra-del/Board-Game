/*
 * Board Game renderer – readable track, chibi tokens, dice, HUD digits.
 * Knock-off couch-board energy: loud colours, bounce, clear spaces.
 */

#include "game.h"
#include "logger.h"
#include <tiny3d.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#define SCR_W 848.0f
#define SCR_H 512.0f

static void draw_rect(float x, float y, float w, float h, u32 rgba)
{
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x,     y,     0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x + w, y,     0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x + w, y + h, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x,     y + h, 0); tiny3d_VertexColor(rgba);
    tiny3d_End();
}

static void draw_circle(float cx, float cy, float r, u32 rgba, int segs)
{
    int i;
    if (segs < 6) segs = 6;
    tiny3d_SetPolygon(TINY3D_TRIANGLE_FAN);
    tiny3d_VertexPos(cx, cy, 0); tiny3d_VertexColor(rgba);
    for (i = 0; i <= segs; ++i) {
        float a = (float)i / (float)segs * 6.2831853f;
        tiny3d_VertexPos(cx + cosf(a) * r, cy + sinf(a) * r, 0);
        tiny3d_VertexColor(rgba);
    }
    tiny3d_End();
}

static void draw_line(float x1, float y1, float x2, float y2, float thickness, u32 rgba)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) return;
    float nx = -dy / len * (thickness * 0.5f);
    float ny =  dx / len * (thickness * 0.5f);

    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x1 + nx, y1 + ny, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x2 + nx, y2 + ny, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x2 - nx, y2 - ny, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x1 - nx, y1 - ny, 0); tiny3d_VertexColor(rgba);
    tiny3d_End();
}

static u32 player_color(int idx)
{
    static const u32 cols[] = {
        0xFFE74C3C, 0xFF3498DB, 0xFF2ECC71, 0xFFF1C40F
    };
    return cols[idx & 3];
}

static u32 player_color_dark(int idx)
{
    static const u32 cols[] = {
        0xFFC0392B, 0xFF2980B9, 0xFF27AE60, 0xFFD4AC0D
    };
    return cols[idx & 3];
}

static const unsigned char FONT3x5[][5] = {
    {0x7,0x5,0x5,0x5,0x7},
    {0x2,0x6,0x2,0x2,0x7},
    {0x7,0x1,0x7,0x4,0x7},
    {0x7,0x1,0x7,0x1,0x7},
    {0x5,0x5,0x7,0x1,0x1},
    {0x7,0x4,0x7,0x1,0x7},
    {0x7,0x4,0x7,0x5,0x7},
    {0x7,0x1,0x1,0x1,0x1},
    {0x7,0x5,0x7,0x5,0x7},
    {0x7,0x5,0x7,0x1,0x7},
};

static void draw_digit(float x, float y, int d, float s, u32 rgba)
{
    int row, col;
    if (d < 0 || d > 9) return;
    for (row = 0; row < 5; ++row) {
        unsigned char bits = FONT3x5[d][row];
        for (col = 0; col < 3; ++col) {
            if (bits & (1 << (2 - col)))
                draw_rect(x + col * s, y + row * s, s, s, rgba);
        }
    }
}

static void draw_number(float x, float y, int value, float s, u32 rgba)
{
    char buf[12];
    int i, n;
    if (value < 0) value = 0;
    n = snprintf(buf, sizeof(buf), "%d", value);
    for (i = 0; i < n; ++i)
        draw_digit(x + i * (s * 4.0f), y, buf[i] - '0', s, rgba);
}

static void draw_label_P(float x, float y, float s, u32 c)
{
    draw_rect(x, y, s, s * 5, c);
    draw_rect(x, y, s * 3, s, c);
    draw_rect(x + s * 2, y, s, s * 3, c);
    draw_rect(x, y + s * 2, s * 3, s, c);
}

static void draw_label_star(float x, float y, float r, u32 c)
{
    draw_rect(x - r * 0.2f, y - r, r * 0.4f, r * 2, c);
    draw_rect(x - r, y - r * 0.2f, r * 2, r * 0.4f, c);
    draw_circle(x, y, r * 0.45f, c, 8);
}

static void space_style(SpaceType t, bool revealed, u32 *col, float *r)
{
    *r = 14.0f;
    *col = 0xFFBDC3C7;
    switch (t) {
        case SPACE_PLUS_COIN:  *col = 0xFF2ECC71; *r = 15.0f; break;
        case SPACE_MINUS_COIN: *col = 0xFFE74C3C; *r = 15.0f; break;
        case SPACE_EVENT:      *col = 0xFF9B59B6; *r = 15.0f; break;
        case SPACE_SHOP:       *col = 0xFFE67E22; *r = 17.0f; break;
        case SPACE_STAR:       *col = 0xFFF1C40F; *r = 18.0f; break;
        case SPACE_HIDDEN_COIN:
        case SPACE_HIDDEN_STAR:
            *col = revealed ? 0xFF1ABC9C : 0xFF7F8C8D;
            *r = 14.0f;
            break;
        default: break;
    }
}

static void draw_space_icon(float x, float y, SpaceType t, float r)
{
    switch (t) {
        case SPACE_PLUS_COIN:
            draw_rect(x - 2, y - r * 0.5f, 4, r, 0xFFFFFFFF);
            draw_rect(x - r * 0.5f, y - 2, r, 4, 0xFFFFFFFF);
            break;
        case SPACE_MINUS_COIN:
            draw_rect(x - r * 0.5f, y - 2, r, 4, 0xFFFFFFFF);
            break;
        case SPACE_STAR:
            draw_label_star(x, y, r * 0.55f, 0xFFFFFFFF);
            break;
        case SPACE_SHOP:
            draw_rect(x - r * 0.4f, y - r * 0.3f, r * 0.8f, r * 0.6f, 0xFFFFFFFF);
            draw_rect(x - r * 0.25f, y - r * 0.5f, r * 0.5f, r * 0.25f, 0xFFFFFFFF);
            break;
        case SPACE_EVENT:
            draw_circle(x, y, r * 0.35f, 0xFFFFFFFF, 8);
            break;
        default:
            break;
    }
}

static void node_screen(const BoardNode *n, float ox, float oy, float sx, float sy,
                        float *out_x, float *out_y)
{
    *out_x = ox + n->x * sx;
    *out_y = oy + n->y * sy;
}

static void draw_board(GameContext *g)
{
    Board *b = g->board;
    float ox = 40.0f, oy = 70.0f;
    float sx = 640.0f, sy = 360.0f;
    int i, j;

    draw_rect(ox - 10, oy - 10, sx + 20, sy + 20, 0xFF1A2332);
    draw_rect(ox - 4,  oy - 4,  sx + 8,  sy + 8,  0xFF243447);

    for (i = 0; i < b->node_count; ++i) {
        BoardNode *n = &b->nodes[i];
        float x1, y1;
        node_screen(n, ox, oy, sx, sy, &x1, &y1);
        for (j = 0; j < n->next_count; ++j) {
            BoardNode *m = &b->nodes[n->next[j]];
            float x2, y2;
            node_screen(m, ox, oy, sx, sy, &x2, &y2);
            draw_line(x1, y1, x2, y2, 6.0f, 0xFF5D6D7E);
            draw_line(x1, y1, x2, y2, 2.5f, 0xFFAEB6BF);
        }
    }

    for (i = 0; i < b->node_count; ++i) {
        BoardNode *n = &b->nodes[i];
        float x, y, r;
        u32 col;
        node_screen(n, ox, oy, sx, sy, &x, &y);
        space_style(n->type, n->hidden_revealed, &col, &r);

        draw_circle(x + 2, y + 3, r + 2, 0x66000000, 12);
        draw_circle(x, y, r + 2, 0xFF1C2833, 12);
        draw_circle(x, y, r, col, 12);
        draw_circle(x - r * 0.25f, y - r * 0.25f, r * 0.35f, 0x55FFFFFF, 8);
        draw_space_icon(x, y, n->type, r);
    }
}

static void draw_character(float x, float y, int color_idx, int facing, float bounce, int idle_frame)
{
    u32 body = player_color(color_idx);
    u32 dark = player_color_dark(color_idx);
    float by = y - bounce;
    float ex = facing >= 0 ? 3.0f : -3.0f;

    draw_circle(x, y + 10, 12.0f, 0x55000000, 10);

    draw_rect(x - 8, by + 6, 6, 10, dark);
    draw_rect(x + 2, by + 6, 6, 10, dark);

    draw_circle(x, by - 2, 14.0f, body, 14);
    draw_circle(x, by + 2, 7.0f, 0x33FFFFFF, 8);

    draw_circle(x, by - 18, 12.0f, body, 12);

    draw_circle(x - 4 + ex * 0.3f, by - 20, 3.2f, 0xFFFFFFFF, 8);
    draw_circle(x + 4 + ex * 0.3f, by - 20, 3.2f, 0xFFFFFFFF, 8);
    draw_circle(x - 4 + ex, by - 20, 1.6f, 0xFF1A1A1A, 6);
    draw_circle(x + 4 + ex, by - 20, 1.6f, 0xFF1A1A1A, 6);

    draw_rect(x - 4, by - 12, 8, 2, 0xFF1A1A1A);

    if ((idle_frame / 20) & 1)
        draw_circle(x + 10, by - 28, 3.5f, dark, 6);
    else
        draw_circle(x + 10, by - 26, 3.5f, dark, 6);
}

static void draw_tokens(GameContext *g)
{
    float ox = 40.0f, oy = 70.0f;
    float sx = 640.0f, sy = 360.0f;
    int i;

    for (i = 0; i < g->player_count; ++i) {
        Player *p = &g->players[i];
        BoardNode *n = &g->board->nodes[p->node];
        float x, y;
        float bounce = 0.0f;
        float stack = (float)(i % 4) * 10.0f - 15.0f;

        node_screen(n, ox, oy, sx, sy, &x, &y);
        x += stack;

        if (p->celebrating)
            bounce = fabsf(sinf(g->anim_t * 10.0f)) * 16.0f;
        else if (i == g->current_player)
            bounce = fabsf(sinf(g->anim_t * 4.0f)) * 4.0f;

        draw_character(x, y, p->color, 1, bounce, (int)g->frame + i * 7);
    }
}

static void draw_die_face(float cx, float cy, float size, int value, u32 body, u32 pip)
{
    float h = size * 0.5f;
    float pr = size * 0.09f;
    float o = size * 0.22f;
    int v = value;
    if (v < 1) v = 1;
    if (v > 6) v = 6;

    draw_rect(cx - h, cy - h, size, size, 0xFF1A1A1A);
    draw_rect(cx - h + 3, cy - h + 3, size - 6, size - 6, body);

    if (v == 1 || v == 3 || v == 5)
        draw_circle(cx, cy, pr, pip, 8);
    if (v >= 2) {
        draw_circle(cx - o, cy - o, pr, pip, 8);
        draw_circle(cx + o, cy + o, pr, pip, 8);
    }
    if (v >= 4) {
        draw_circle(cx + o, cy - o, pr, pip, 8);
        draw_circle(cx - o, cy + o, pr, pip, 8);
    }
    if (v == 6) {
        draw_circle(cx - o, cy, pr, pip, 8);
        draw_circle(cx + o, cy, pr, pip, 8);
    }
}

static void draw_dice(GameContext *g)
{
    float cx = 760.0f, cy = 120.0f;
    float pulse = 1.0f + 0.08f * sinf(g->anim_t * 8.0f);

    if (g->state == STATE_DICE_ROLL) {
        int spin = 1 + ((int)(g->anim_t * 12.0f) % 6);
        draw_die_face(cx, cy, 64.0f * pulse, spin, 0xFFFFFFFF, 0xFF1A1A1A);
    } else if (g->dice_value > 0 &&
               (g->state == STATE_MOVE || g->state == STATE_LAND ||
                g->state == STATE_TURN_START || g->state == STATE_SHOP)) {
        draw_die_face(cx, cy, 64.0f, g->dice_value, 0xFFFFF8E7, 0xFF1A1A1A);
        draw_number(cx - 10, cy + 42, g->dice_value, 3.0f, 0xFFFFFFFF);
    }
}

static void draw_hud(GameContext *g)
{
    int i;

    draw_rect(0, 0, SCR_W, 56, 0xEE0D1117);
    draw_rect(0, 56, SCR_W, 3, 0xFF3498DB);

    for (i = 0; i < g->player_count; ++i) {
        Player *p = &g->players[i];
        float bx = 16.0f + i * 200.0f;
        u32 col = player_color(p->color);

        draw_rect(bx, 6, 188, 44, 0xFF1C2833);
        if (i == g->current_player)
            draw_rect(bx, 6, 188, 44, 0x443498DB);

        draw_circle(bx + 22, 28, 14, col, 10);
        draw_circle(bx + 18, 25, 3, 0xFFFFFFFF, 6);
        draw_circle(bx + 26, 25, 3, 0xFFFFFFFF, 6);
        draw_circle(bx + 18, 25, 1.4f, 0xFF111111, 5);
        draw_circle(bx + 26, 25, 1.4f, 0xFF111111, 5);

        draw_label_P(bx + 42, 12, 3.0f, 0xFFFFFFFF);
        draw_digit(bx + 56, 12, i + 1, 3.0f, 0xFFFFFFFF);

        draw_label_star(bx + 90, 18, 7.0f, 0xFFF1C40F);
        draw_number(bx + 102, 12, p->stars, 3.0f, 0xFFF1C40F);

        draw_circle(bx + 140, 20, 7.0f, 0xFFF39C12, 8);
        draw_circle(bx + 140, 20, 4.0f, 0xFFF1C40F, 8);
        draw_number(bx + 152, 12, p->coins, 3.0f, 0xFFF39C12);
    }

    {
        float prog = (g->max_rounds > 0)
            ? (float)g->round / (float)g->max_rounds : 0.0f;
        if (prog > 1.0f) prog = 1.0f;
        draw_rect(40, 490, 640, 12, 0xFF1C2833);
        draw_rect(40, 490, 640 * prog, 12, 0xFF3498DB);
        draw_number(690, 488, g->round, 2.5f, 0xFFFFFFFF);
    }
}

static void draw_title(GameContext *g)
{
    float bob = sinf(g->anim_t * 2.5f) * 8.0f;

    draw_rect(0, 0, SCR_W, SCR_H, 0xFF0B1320);
    draw_rect(120, 100, 608, 280, 0xFF162033);
    draw_rect(140, 120, 568, 240, 0xFF1B2838);

    draw_character(220, 300 + bob, 0, 1, fabsf(sinf(g->anim_t * 5)) * 10, (int)g->frame);
    draw_character(340, 300 - bob, 1, 1, fabsf(sinf(g->anim_t * 5 + 1)) * 10, (int)g->frame);
    draw_character(460, 300 + bob * 0.7f, 2, -1, fabsf(sinf(g->anim_t * 5 + 2)) * 10, (int)g->frame);
    draw_character(580, 300 - bob * 0.7f, 3, -1, fabsf(sinf(g->anim_t * 5 + 3)) * 10, (int)g->frame);

    draw_label_star(424, 160 + bob * 0.3f, 18.0f + 4.0f * sinf(g->anim_t * 3), 0xFFF1C40F);

    if (((int)(g->anim_t * 2)) & 1)
        draw_rect(300, 400, 248, 14, 0xFF2ECC71);
}

static void draw_results(GameContext *g)
{
    int best = 0, i;
    for (i = 1; i < g->player_count; ++i) {
        if (g->players[i].stars > g->players[best].stars ||
            (g->players[i].stars == g->players[best].stars &&
             g->players[i].coins > g->players[best].coins))
            best = i;
    }

    draw_rect(0, 0, SCR_W, SCR_H, 0xFF0B1320);
    draw_rect(180, 80, 488, 340, 0xFF162033);

    draw_character(424, 280, g->players[best].color, 1,
                   fabsf(sinf(g->anim_t * 6)) * 14, (int)g->frame);

    draw_label_star(424, 140, 22.0f, 0xFFF1C40F);
    draw_number(400, 170, g->players[best].stars, 4.0f, 0xFFF1C40F);
    draw_number(400, 210, g->players[best].coins, 3.0f, 0xFFF39C12);
}

void game_render(GameContext *g)
{
    tiny3d_Clear(0xFF0B1320, TINY3D_CLEAR_ALL);
    tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);
    /* enable, src, dst, func — standard alpha blend */
    tiny3d_BlendFunc(1,
        (blend_src_func)(TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA),
        (blend_dst_func)(TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_DST_ALPHA_ZERO),
        (blend_func)(TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD));

    tiny3d_Project2D();

    switch (g->state) {
    case STATE_TITLE:
        draw_title(g);
        break;

    case STATE_SETUP:
    case STATE_BOARD_SELECT:
        draw_rect(0, 0, SCR_W, SCR_H, 0xFF0B1320);
        draw_board(g);
        draw_tokens(g);
        draw_rect(200, 460, 448, 20, 0xFF2C3E50);
        if (((int)(g->anim_t * 3)) & 1)
            draw_rect(220, 465, 408, 10, 0xFF3498DB);
        break;

    case STATE_MATCH:
    case STATE_TURN_START:
    case STATE_DICE_ROLL:
    case STATE_MOVE:
    case STATE_LAND:
    case STATE_SHOP:
    case STATE_ROUND_END:
    case STATE_MINIGAME_SIM:
        draw_rect(0, 0, SCR_W, SCR_H, 0xFF0B1320);
        draw_board(g);
        draw_tokens(g);
        draw_hud(g);
        draw_dice(g);
        if (g->state == STATE_SHOP)
            draw_rect(300, 60, 248, 18, 0xFFE67E22);
        if (g->state == STATE_MINIGAME_SIM)
            draw_rect(300, 60, 248, 18, 0xFF9B59B6);
        break;

    case STATE_RESULTS:
        draw_results(g);
        break;

    default:
        break;
    }

    tiny3d_Flip();
}

#include "game.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void link_node(BoardNode *n, int a, int b)
{
    if (n[a].next_count < 4) n[a].next[n[a].next_count++] = b;
}
static void place(BoardNode *n, int i, float x, float y, SpaceType t)
{
    n[i].id = i; n[i].x = x; n[i].y = y; n[i].type = t; n[i].hidden_revealed = false;
}

/* Lagoon Isle: 20-space shore ring + two shortcut paths across the island */
void board_load_visual(Board *b)
{
    int i; BoardNode *n = b->nodes;
    memset(b, 0, sizeof(*b)); b->name = "Lagoon Isle"; b->node_count = 28; b->start_node = 0;
    for (i = 0; i < 20; ++i) {
        float t = (float)i / 20.0f * 6.2831853f;
        place(n, i, 0.50f + 0.42f * cosf(t), 0.50f + 0.38f * sinf(t), SPACE_NORMAL);
    }
    for (i = 0; i < 8; ++i) {
        float t = ((float)i + 0.5f) / 8.0f * 6.2831853f;
        place(n, 20 + i, 0.50f + 0.19f * cosf(t), 0.50f + 0.17f * sinf(t), SPACE_NORMAL);
    }
    for (i = 0; i < 19; ++i) link_node(n, i, i + 1);
    link_node(n, 19, 0);
    /* shortcut A: ring 2 -> inner 20..23 -> ring 9 */
    link_node(n, 2, 20); link_node(n, 20, 21); link_node(n, 21, 22); link_node(n, 22, 23); link_node(n, 23, 9);
    /* shortcut B: ring 12 -> inner 24..27 -> ring 18 */
    link_node(n, 12, 24); link_node(n, 24, 25); link_node(n, 25, 26); link_node(n, 26, 27); link_node(n, 27, 18);
    n[2].type = SPACE_PLUS_COIN;  n[5].type = SPACE_MINUS_COIN; n[6].type = SPACE_WARP;
    n[7].type = SPACE_EVENT;      n[9].type = SPACE_SHOP;       n[11].type = SPACE_PLUS_COIN;
    n[12].type = SPACE_HAZARD;    n[13].type = SPACE_EVENT;     n[15].type = SPACE_SHOP;
    n[16].type = SPACE_BOWSER;    n[17].type = SPACE_MINUS_COIN; n[18].type = SPACE_HIDDEN_COIN;
    n[20].type = SPACE_PLUS_COIN; n[21].type = SPACE_EVENT;     n[22].type = SPACE_BOWSER;
    n[24].type = SPACE_PLUS_COIN; n[25].type = SPACE_HIDDEN_STAR; n[26].type = SPACE_EVENT;
    b->star_node = 3; n[3].type = SPACE_STAR;
}

/* Geo Transit: rectangular loop with a crossing through the centre (4-way junction) */
void board_load_geo(Board *b)
{
    int i; BoardNode *n = b->nodes;
    memset(b, 0, sizeof(*b)); b->name = "Geo Transit"; b->node_count = 29; b->start_node = 0;
    for (i = 0; i < 7; ++i) place(n, i,      0.12f + 0.76f * ((float)i / 7.0f), 0.14f, SPACE_NORMAL);
    for (i = 0; i < 5; ++i) place(n, 7 + i,  0.88f, 0.14f + 0.72f * ((float)i / 5.0f), SPACE_NORMAL);
    for (i = 0; i < 7; ++i) place(n, 12 + i, 0.88f - 0.76f * ((float)i / 7.0f), 0.86f, SPACE_NORMAL);
    for (i = 0; i < 5; ++i) place(n, 19 + i, 0.12f, 0.86f - 0.72f * ((float)i / 5.0f), SPACE_NORMAL);
    place(n, 24, 0.473f, 0.32f, SPACE_NORMAL); place(n, 25, 0.500f, 0.50f, SPACE_EVENT);
    place(n, 26, 0.527f, 0.68f, SPACE_NORMAL); place(n, 27, 0.690f, 0.464f, SPACE_NORMAL);
    place(n, 28, 0.310f, 0.536f, SPACE_NORMAL);
    for (i = 0; i < 23; ++i) link_node(n, i, i + 1);
    link_node(n, 23, 0);
    link_node(n, 3, 24); link_node(n, 24, 25); link_node(n, 25, 26); link_node(n, 26, 15);
    link_node(n, 9, 27); link_node(n, 27, 25); link_node(n, 25, 28); link_node(n, 28, 21);
    n[1].type = SPACE_PLUS_COIN;  n[4].type = SPACE_SHOP;       n[5].type = SPACE_WARP;
    n[6].type = SPACE_EVENT;      n[8].type = SPACE_MINUS_COIN; n[11].type = SPACE_PLUS_COIN;
    n[13].type = SPACE_BOWSER;    n[14].type = SPACE_SHOP;      n[16].type = SPACE_EVENT;
    n[19].type = SPACE_BOWSER;    n[20].type = SPACE_WARP;      n[22].type = SPACE_HIDDEN_STAR;
    n[24].type = SPACE_PLUS_COIN; n[26].type = SPACE_HAZARD;    n[27].type = SPACE_PLUS_COIN;
    n[28].type = SPACE_MINUS_COIN;
    b->star_node = 10; n[10].type = SPACE_STAR;
}

/* Metro Plaza: two city loops joined by a centre crossing and two one-way bridge streets */
void board_load_metro(Board *b)
{
    int i; BoardNode *n = b->nodes;
    memset(b, 0, sizeof(*b)); b->name = "Metro Plaza"; b->node_count = 28; b->start_node = 0;
    for (i = 0; i < 13; ++i) {
        float t = (float)i / 13.0f * 6.2831853f;
        place(n, i, 0.26f + 0.19f * cosf(t), 0.50f + 0.34f * sinf(t), SPACE_NORMAL);
    }
    for (i = 0; i < 13; ++i) {
        float t = ((float)i + 0.5f) / 13.0f * 6.2831853f;
        place(n, 13 + i, 0.74f + 0.19f * cosf(t), 0.50f + 0.34f * sinf(t), SPACE_NORMAL);
    }
    place(n, 26, 0.500f, 0.342f, SPACE_NORMAL); place(n, 27, 0.500f, 0.658f, SPACE_NORMAL);
    for (i = 0; i < 12; ++i) link_node(n, i, i + 1);
    link_node(n, 12, 0);
    for (i = 13; i < 25; ++i) link_node(n, i, i + 1);
    link_node(n, 25, 13);
    link_node(n, 12, 26); link_node(n, 26, 20);   /* top street: left -> right  */
    link_node(n, 18, 27); link_node(n, 27, 1);    /* bottom street: right -> left */
    link_node(n, 0, 19);  link_node(n, 19, 0);    /* centre crossing, both ways */
    n[2].type = SPACE_PLUS_COIN;  n[4].type = SPACE_SHOP;       n[5].type = SPACE_EVENT;
    n[7].type = SPACE_MINUS_COIN; n[8].type = SPACE_BOWSER;     n[10].type = SPACE_WARP;
    n[11].type = SPACE_HAZARD;
    n[14].type = SPACE_PLUS_COIN; n[16].type = SPACE_EVENT;     n[17].type = SPACE_SHOP;
    n[21].type = SPACE_BOWSER;    n[22].type = SPACE_HIDDEN_COIN; n[23].type = SPACE_WARP;
    n[24].type = SPACE_MINUS_COIN;
    n[26].type = SPACE_PLUS_COIN; n[27].type = SPACE_PLUS_COIN;
    b->star_node = 15; n[15].type = SPACE_STAR;
}

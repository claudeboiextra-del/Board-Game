/* Three board layouts – Lagoon oval, Geo transit, Metro Plaza city */

#include "game.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void link_node(BoardNode *n, int a, int b)
{
    if (n[a].next_count < 4) n[a].next[n[a].next_count++] = b;
}

void board_load_visual(Board *b)
{
    int i;
    LOG_I("Loading Visual / Lagoon board");
    memset(b, 0, sizeof(*b));
    b->name = "Lagoon Isle";
    b->node_count = 20;
    b->start_node = 0;

    for (i = 0; i < 20; ++i) {
        float t = (float)i / 20.0f * 6.2831853f;
        b->nodes[i].id = i;
        b->nodes[i].type = SPACE_NORMAL;
        b->nodes[i].next_count = 0;
        b->nodes[i].x = 0.50f + 0.42f * cosf(t);
        b->nodes[i].y = 0.50f + 0.38f * sinf(t);
        b->nodes[i].hidden_revealed = false;
    }
    for (i = 0; i < 19; ++i) link_node(b->nodes, i, i + 1);
    link_node(b->nodes, 19, 0);
    link_node(b->nodes, 4, 14);
    link_node(b->nodes, 9, 16);

    b->nodes[2].type  = SPACE_PLUS_COIN;
    b->nodes[5].type  = SPACE_MINUS_COIN;
    b->nodes[6].type  = SPACE_WARP;
    b->nodes[7].type  = SPACE_EVENT;
    b->nodes[9].type  = SPACE_SHOP;
    b->nodes[11].type = SPACE_PLUS_COIN;
    b->nodes[12].type = SPACE_HAZARD;
    b->nodes[13].type = SPACE_EVENT;
    b->nodes[15].type = SPACE_SHOP;
    b->nodes[17].type = SPACE_MINUS_COIN;
    b->nodes[18].type = SPACE_HIDDEN_COIN;

    b->star_node = 3;
    b->nodes[3].type = SPACE_STAR;
    LOG_I("Lagoon Isle ready – %d nodes", b->node_count);
}

void board_load_geo(Board *b)
{
    int i;
    LOG_I("Loading Geo Transit board");
    memset(b, 0, sizeof(*b));
    b->name = "Geo Transit";
    b->node_count = 24;
    b->start_node = 0;

    for (i = 0; i < 6; ++i) {
        b->nodes[i].x = 0.12f + 0.76f * (i / 5.0f);
        b->nodes[i].y = 0.14f;
    }
    for (i = 0; i < 6; ++i) {
        b->nodes[6 + i].x = 0.88f;
        b->nodes[6 + i].y = 0.14f + 0.72f * (i / 5.0f);
    }
    for (i = 0; i < 6; ++i) {
        b->nodes[12 + i].x = 0.88f - 0.76f * (i / 5.0f);
        b->nodes[12 + i].y = 0.86f;
    }
    for (i = 0; i < 6; ++i) {
        b->nodes[18 + i].x = 0.12f;
        b->nodes[18 + i].y = 0.86f - 0.72f * (i / 5.0f);
    }
    for (i = 0; i < 24; ++i) {
        b->nodes[i].id = i;
        b->nodes[i].type = SPACE_NORMAL;
        b->nodes[i].next_count = 0;
        b->nodes[i].hidden_revealed = false;
    }
    for (i = 0; i < 23; ++i) link_node(b->nodes, i, i + 1);
    link_node(b->nodes, 23, 0);
    link_node(b->nodes, 3, 15);
    link_node(b->nodes, 9, 21);

    b->nodes[1].type  = SPACE_PLUS_COIN;
    b->nodes[4].type  = SPACE_SHOP;
    b->nodes[5].type  = SPACE_WARP;
    b->nodes[6].type  = SPACE_EVENT;
    b->nodes[8].type  = SPACE_MINUS_COIN;
    b->nodes[11].type = SPACE_PLUS_COIN;
    b->nodes[13].type = SPACE_HAZARD;
    b->nodes[14].type = SPACE_SHOP;
    b->nodes[16].type = SPACE_EVENT;
    b->nodes[19].type = SPACE_MINUS_COIN;
    b->nodes[20].type = SPACE_WARP;
    b->nodes[22].type = SPACE_HIDDEN_STAR;

    b->star_node = 10;
    b->nodes[10].type = SPACE_STAR;
    LOG_I("Geo Transit ready – %d nodes", b->node_count);
}

/* Metro Plaza – figure-8 city loop with central plaza shortcuts */
void board_load_metro(Board *b)
{
    int i;
    LOG_I("Loading Metro Plaza board");
    memset(b, 0, sizeof(*b));
    b->name = "Metro Plaza";
    b->node_count = 22;
    b->start_node = 0;

    /* Left loop (nodes 0-10) + right loop (11-21), figure-8 cross at 5/16 */
    for (i = 0; i < 11; ++i) {
        float t = (float)i / 11.0f * 6.2831853f;
        b->nodes[i].x = 0.32f + 0.22f * cosf(t);
        b->nodes[i].y = 0.50f + 0.34f * sinf(t);
    }
    for (i = 0; i < 11; ++i) {
        float t = (float)i / 11.0f * 6.2831853f;
        b->nodes[11 + i].x = 0.68f + 0.22f * cosf(t);
        b->nodes[11 + i].y = 0.50f + 0.34f * sinf(t);
    }
    for (i = 0; i < 22; ++i) {
        b->nodes[i].id = i;
        b->nodes[i].type = SPACE_NORMAL;
        b->nodes[i].next_count = 0;
        b->nodes[i].hidden_revealed = false;
    }
    for (i = 0; i < 10; ++i) link_node(b->nodes, i, i + 1);
    link_node(b->nodes, 10, 0);
    for (i = 11; i < 21; ++i) link_node(b->nodes, i, i + 1);
    link_node(b->nodes, 21, 11);
    /* plaza bridges between loops */
    link_node(b->nodes, 5, 16);
    link_node(b->nodes, 16, 5);
    link_node(b->nodes, 2, 13);
    link_node(b->nodes, 8, 19);

    b->nodes[1].type  = SPACE_PLUS_COIN;
    b->nodes[3].type  = SPACE_SHOP;
    b->nodes[4].type  = SPACE_EVENT;
    b->nodes[6].type  = SPACE_HAZARD;
    b->nodes[7].type  = SPACE_MINUS_COIN;
    b->nodes[9].type  = SPACE_WARP;
    b->nodes[12].type = SPACE_PLUS_COIN;
    b->nodes[14].type = SPACE_SHOP;
    b->nodes[15].type = SPACE_EVENT;
    b->nodes[17].type = SPACE_WARP;
    b->nodes[18].type = SPACE_MINUS_COIN;
    b->nodes[20].type = SPACE_HIDDEN_COIN;

    b->star_node = 11;
    b->nodes[11].type = SPACE_STAR;
    LOG_I("Metro Plaza ready – %d nodes", b->node_count);
}

/* Two distinct board layouts for Board Game */

#include "game.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void link_node(BoardNode *n, int a, int b)
{
    if (n[a].next_count < 4) n[a].next[n[a].next_count++] = b;
}

/* Board A – "Visual": simple looping track, vibrant spacing */
void board_load_visual(Board *b)
{
    LOG_I("Loading Visual board");
    memset(b, 0, sizeof(*b));
    b->name = "Visual Circuit";
    b->node_count = 24;
    b->start_node = 0;

    /* Arrange nodes in a soft figure-8 / loop for visual clarity */
    int i;
    for (i = 0; i < 24; ++i) {
        b->nodes[i].id = i;
        b->nodes[i].type = SPACE_NORMAL;
        b->nodes[i].next_count = 0;
        /* screen coords – later scaled by renderer */
        float t = (float)i / 24.0f * 6.28318f;
        b->nodes[i].x = 0.5f + 0.35f * cosf(t);
        b->nodes[i].y = 0.5f + 0.28f * sinf(t * 1.5f);
    }

    /* sequential path with a couple of shortcuts */
    for (i = 0; i < 23; ++i) link_node(b->nodes, i, i + 1);
    link_node(b->nodes, 23, 0);          /* close loop */
    link_node(b->nodes, 5, 12);          /* shortcut */
    link_node(b->nodes, 17, 3);          /* alternate */

    /* sprinkle space types */
    b->nodes[2].type  = SPACE_PLUS_COIN;
    b->nodes[4].type  = SPACE_MINUS_COIN;
    b->nodes[7].type  = SPACE_EVENT;
    b->nodes[9].type  = SPACE_SHOP;
    b->nodes[11].type = SPACE_PLUS_COIN;
    b->nodes[14].type = SPACE_HIDDEN_COIN;
    b->nodes[16].type = SPACE_EVENT;
    b->nodes[19].type = SPACE_SHOP;
    b->nodes[21].type = SPACE_HIDDEN_STAR;

    b->star_node = 8; /* initial star placement */
    b->nodes[8].type = SPACE_STAR;

    LOG_I("Visual board ready – %d nodes", b->node_count);
}

/* Board B – "Geographical": branching map with transit nodes */
void board_load_geo(Board *b)
{
    LOG_I("Loading Geographical board");
    memset(b, 0, sizeof(*b));
    b->name = "Geo Transit";
    b->node_count = 32;
    b->start_node = 0;

    int i;
    for (i = 0; i < 32; ++i) {
        b->nodes[i].id = i;
        b->nodes[i].type = SPACE_NORMAL;
        b->nodes[i].next_count = 0;
        /* more spread-out layout */
        float row = (float)(i % 8);
        float col = (float)(i / 8);
        b->nodes[i].x = 0.12f + row * 0.11f;
        b->nodes[i].y = 0.15f + col * 0.18f;
    }

    /* main spine + branches */
    for (i = 0; i < 7; ++i)  link_node(b->nodes, i, i + 1);
    link_node(b->nodes, 7, 8);
    for (i = 8; i < 15; ++i) link_node(b->nodes, i, i + 1);
    link_node(b->nodes, 15, 16);
    for (i = 16; i < 23; ++i) link_node(b->nodes, i, i + 1);
    link_node(b->nodes, 23, 24);
    for (i = 24; i < 31; ++i) link_node(b->nodes, i, i + 1);
    link_node(b->nodes, 31, 0); /* long loop back */

    /* branches / shortcuts */
    link_node(b->nodes, 3, 18);
    link_node(b->nodes, 11, 27);
    link_node(b->nodes, 20, 5);
    link_node(b->nodes, 28, 12);

    /* space flavour */
    b->nodes[1].type  = SPACE_PLUS_COIN;
    b->nodes[4].type  = SPACE_SHOP;
    b->nodes[6].type  = SPACE_EVENT;
    b->nodes[10].type = SPACE_MINUS_COIN;
    b->nodes[13].type = SPACE_HIDDEN_COIN;
    b->nodes[17].type = SPACE_PLUS_COIN;
    b->nodes[19].type = SPACE_SHOP;
    b->nodes[22].type = SPACE_EVENT;
    b->nodes[25].type = SPACE_HIDDEN_STAR;
    b->nodes[29].type = SPACE_MINUS_COIN;

    b->star_node = 14;
    b->nodes[14].type = SPACE_STAR;

    LOG_I("Geographical board ready – %d nodes", b->node_count);
}

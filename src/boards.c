/* Two distinct board layouts – clear on-screen tracks */

#include "game.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void link_node(BoardNode *n, int a, int b)
{
    if (n[a].next_count < 4) n[a].next[n[a].next_count++] = b;
}

/* Board A – oval loop that fills the playfield */
void board_load_visual(Board *b)
{
    LOG_I("Loading Visual board");
    memset(b, 0, sizeof(*b));
    b->name = "Visual Circuit";
    b->node_count = 20;
    b->start_node = 0;

    {
        int i;
        for (i = 0; i < 20; ++i) {
            float t = (float)i / 20.0f * 6.2831853f;
            /* classic oval track, slightly tilted for depth feel */
            b->nodes[i].id = i;
            b->nodes[i].type = SPACE_NORMAL;
            b->nodes[i].next_count = 0;
            b->nodes[i].x = 0.50f + 0.42f * cosf(t);
            b->nodes[i].y = 0.50f + 0.38f * sinf(t);
            b->nodes[i].hidden_revealed = false;
        }
        for (i = 0; i < 19; ++i) link_node(b->nodes, i, i + 1);
        link_node(b->nodes, 19, 0);
        /* one optional shortcut chord */
        link_node(b->nodes, 4, 14);
    }

    b->nodes[2].type  = SPACE_PLUS_COIN;
    b->nodes[5].type  = SPACE_MINUS_COIN;
    b->nodes[7].type  = SPACE_EVENT;
    b->nodes[9].type  = SPACE_SHOP;
    b->nodes[11].type = SPACE_PLUS_COIN;
    b->nodes[13].type = SPACE_EVENT;
    b->nodes[15].type = SPACE_SHOP;
    b->nodes[17].type = SPACE_MINUS_COIN;
    b->nodes[18].type = SPACE_HIDDEN_COIN;

    b->star_node = 3;
    b->nodes[3].type = SPACE_STAR;

    LOG_I("Visual board ready – %d nodes", b->node_count);
}

/* Board B – rounded rectangle track with inner cross shortcuts */
void board_load_geo(Board *b)
{
    LOG_I("Loading Geographical board");
    memset(b, 0, sizeof(*b));
    b->name = "Geo Transit";
    b->node_count = 24;
    b->start_node = 0;

    /* Perimeter of a rounded rectangle in normalised coords */
    {
        /* 6 per side: top, right, bottom, left */
        int i;
        for (i = 0; i < 6; ++i) { /* top L->R */
            b->nodes[i].x = 0.12f + (0.76f) * (i / 5.0f);
            b->nodes[i].y = 0.14f;
        }
        for (i = 0; i < 6; ++i) { /* right T->B */
            b->nodes[6 + i].x = 0.88f;
            b->nodes[6 + i].y = 0.14f + (0.72f) * (i / 5.0f);
        }
        for (i = 0; i < 6; ++i) { /* bottom R->L */
            b->nodes[12 + i].x = 0.88f - (0.76f) * (i / 5.0f);
            b->nodes[12 + i].y = 0.86f;
        }
        for (i = 0; i < 6; ++i) { /* left B->T */
            b->nodes[18 + i].x = 0.12f;
            b->nodes[18 + i].y = 0.86f - (0.72f) * (i / 5.0f);
        }
        for (i = 0; i < 24; ++i) {
            b->nodes[i].id = i;
            b->nodes[i].type = SPACE_NORMAL;
            b->nodes[i].next_count = 0;
            b->nodes[i].hidden_revealed = false;
        }
        for (i = 0; i < 23; ++i) link_node(b->nodes, i, i + 1);
        link_node(b->nodes, 23, 0);
        /* cross shortcuts */
        link_node(b->nodes, 3, 15);
        link_node(b->nodes, 9, 21);
    }

    b->nodes[1].type  = SPACE_PLUS_COIN;
    b->nodes[4].type  = SPACE_SHOP;
    b->nodes[6].type  = SPACE_EVENT;
    b->nodes[8].type  = SPACE_MINUS_COIN;
    b->nodes[11].type = SPACE_PLUS_COIN;
    b->nodes[14].type = SPACE_SHOP;
    b->nodes[16].type = SPACE_EVENT;
    b->nodes[19].type = SPACE_MINUS_COIN;
    b->nodes[22].type = SPACE_HIDDEN_STAR;

    b->star_node = 10;
    b->nodes[10].type = SPACE_STAR;

    LOG_I("Geographical board ready – %d nodes", b->node_count);
}

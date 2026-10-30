#include "game.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Board builder: boards are described in world units (x +-24, z +-13) */
/* and converted to the normalised node coordinates the game uses.     */
/* ------------------------------------------------------------------ */
#define STEP 3.7f   /* target distance between neighbouring spaces */

static float wx_of(const Board *b, int i) { return (b->nodes[i].x - 0.5f) * WORLD_SX; }
static float wz_of(const Board *b, int i) { return (b->nodes[i].y - 0.5f) * WORLD_SZ; }

static void lnk(Board *b, int a, int c)
{
    BoardNode *n = &b->nodes[a]; int i;
    for (i = 0; i < n->next_count; ++i) if (n->next[i] == c) return;
    if (n->next_count < 4) n->next[n->next_count++] = c;
}
static int add(Board *b, float wx, float wz, SpaceType t)
{
    int i = b->node_count++;
    BoardNode *n = &b->nodes[i];
    n->id = i; n->type = t; n->next_count = 0; n->hidden_revealed = false;
    n->x = 0.5f + wx / WORLD_SX; n->y = 0.5f + wz / WORLD_SZ;
    return i;
}
/* One-way path a -> c through optional waypoints (x,z pairs); spaces are spread evenly.
   Returns the index of the middle new space (or -1 if the path has none). */
static int route(Board *b, int a, int c, const float *wp, int nwp)
{
    float px[10], pz[10], seg[10], total = 0.f; int np = 0, i, k, prev = a, mid = -1;
    px[np] = wx_of(b, a); pz[np] = wz_of(b, a); np++;
    for (i = 0; i < nwp && np < 8; ++i) { px[np] = wp[i * 2]; pz[np] = wp[i * 2 + 1]; np++; }
    px[np] = wx_of(b, c); pz[np] = wz_of(b, c); np++;
    for (i = 0; i + 1 < np; ++i) {
        float dx = px[i + 1] - px[i], dz = pz[i + 1] - pz[i];
        seg[i] = sqrtf(dx * dx + dz * dz); total += seg[i];
    }
    k = (int)(total / STEP + 0.5f); if (k < 1) k = 1;
    for (i = 1; i < k; ++i) {
        float d = total * (float)i / (float)k, x = px[0], z = pz[0]; int s;
        for (s = 0; s + 1 < np; ++s) {
            if (d <= seg[s] || s + 2 == np) {
                float t = (seg[s] > 0.001f) ? d / seg[s] : 0.f;
                x = px[s] + (px[s + 1] - px[s]) * t; z = pz[s] + (pz[s + 1] - pz[s]) * t; break;
            }
            d -= seg[s];
        }
        { int idx = add(b, x, z, SPACE_NORMAL); lnk(b, prev, idx); prev = idx; if (i == k / 2) mid = idx; }
    }
    lnk(b, prev, c);
    return mid;
}
/* fill every still-plain space with a deterministic mix of coin / event / hazard spaces */
static void decorate(Board *b, unsigned seed)
{
    int i;
    for (i = 0; i < b->node_count; ++i) {
        unsigned r; SpaceType t = SPACE_NORMAL;
        seed = seed * 1664525u + 1013904223u; r = (seed >> 16) % 100u;
        if (b->nodes[i].type != SPACE_NORMAL) continue;
        if (r < 30) t = SPACE_PLUS_COIN;
        else if (r < 42) t = SPACE_MINUS_COIN;
        else if (r < 56) t = SPACE_EVENT;
        else if (r < 61) t = SPACE_HAZARD;
        b->nodes[i].type = t;
    }
}
static int nearest(const Board *b, float x, float z)
{
    int i, best = 0; float bd = 1e9f;
    for (i = 0; i < b->node_count; ++i) {
        float dx = wx_of(b, i) - x, dz = wz_of(b, i) - z, d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static void setup(Board *b, const char *name) { memset(b, 0, sizeof(*b)); b->name = name; }
static void put(Board *b, int i, SpaceType t) { if (i >= 0 && i < b->node_count) b->nodes[i].type = t; }

/* ------------------------------------------------------------------ */
/* Lagoon Isle: shore ring with a three-lane crossing through the island */
/* ------------------------------------------------------------------ */
void board_load_visual(Board *b)
{
    int S, E, A, B, m_mid, m_n, m_s, spur;
    static const float north[] = { -19, -8, -9, -12.5f, 9, -12.5f, 19, -8 };
    static const float south[] = { 19, 8, 9, 12.5f, -9, 12.5f, -19, 8 };
    static const float lane_n[] = { -7, -5.5f, 0, -6.5f, 7, -5.5f };
    static const float lane_s[] = { -7, 5.5f, 0, 6.5f, 7, 5.5f };
    setup(b, "Lagoon Isle");
    S = add(b, -22, 0, SPACE_NORMAL); E = add(b, 22, 0, SPACE_NORMAL);
    A = add(b, -12, 0, SPACE_NORMAL); B = add(b, 12, 0, SPACE_NORMAL);
    b->start_node = S;
    route(b, S, E, north, 4);                       /* north shore */
    route(b, E, S, south, 4);                       /* south shore */
    spur = route(b, S, A, NULL, 0);                 /* beach road into the island */
    m_mid = route(b, A, B, NULL, 0);                /* centre lane */
    m_n = route(b, A, B, lane_n, 3);                /* north lane  */
    m_s = route(b, A, B, lane_s, 3);                /* south lane  */
    route(b, B, E, NULL, 0);                        /* road back to the east beach */
    /* ladders: the north and south lanes can drop into the centre lane after the shop */
    { int after = b->nodes[m_mid].next[0];
      route(b, m_n, after, NULL, 0); route(b, m_s, after, NULL, 0);
      /* second way in: a ferry from the south-west shore up to the west junction */
      route(b, nearest(b, -15, 10), A, NULL, 0); }
    decorate(b, 7u);
    put(b, m_mid, SPACE_SHOP); put(b, m_n, SPACE_BOWSER); put(b, m_s, SPACE_HIDDEN_STAR);
    put(b, A, SPACE_EVENT); put(b, B, SPACE_PLUS_COIN); put(b, spur, SPACE_PLUS_COIN);
    put(b, S, SPACE_NORMAL);
    /* a few fixed highlights on the shore */
    put(b, b->nodes[S].next[0], SPACE_PLUS_COIN);
    { int i, nshop = 0, nbow = 0, nwarp = 0;
      for (i = b->node_count - 1; i > 3; --i) {
          if (b->nodes[i].type != SPACE_PLUS_COIN && b->nodes[i].type != SPACE_MINUS_COIN && b->nodes[i].type != SPACE_EVENT) continue;
          if (i % 7 == 0 && nshop < 1) { b->nodes[i].type = SPACE_SHOP; nshop++; }
          else if (i % 11 == 0 && nbow < 2) { b->nodes[i].type = SPACE_BOWSER; nbow++; }
          else if (i % 5 == 0 && nwarp < 2) { b->nodes[i].type = SPACE_WARP; nwarp++; }
      } }
    b->star_node = E; put(b, E, SPACE_STAR);
}

/* ------------------------------------------------------------------ */
/* Geo Transit: city block with two one-way avenues, a cross street     */
/* and two side pockets                                                 */
/* ------------------------------------------------------------------ */
void board_load_geo(Board *b)
{
    int TL, TR, BR, BL, Jt1, Jt2, Jb1, Jb2, M1, M2, Re1, Re2, Le1, Le2, mid_bridge, pe, pw, st1, st2;
    static const float pocket_e[] = { 17, -4, 14, 0, 17, 4 };
    static const float pocket_w[] = { -17, 4, -14, 0, -17, -4 };
    setup(b, "Geo Transit");
    TL = add(b, -22, -12, SPACE_NORMAL); TR = add(b, 22, -12, SPACE_NORMAL);
    BR = add(b, 22, 12, SPACE_NORMAL);   BL = add(b, -22, 12, SPACE_NORMAL);
    Jt1 = add(b, -8, -12, SPACE_NORMAL); Jt2 = add(b, 8, -12, SPACE_NORMAL);
    Jb2 = add(b, 8, 12, SPACE_NORMAL);   Jb1 = add(b, -8, 12, SPACE_NORMAL);
    M1 = add(b, -8, 0, SPACE_EVENT);     M2 = add(b, 8, 0, SPACE_EVENT);
    Re1 = add(b, 22, -5, SPACE_NORMAL);  Re2 = add(b, 22, 5, SPACE_NORMAL);
    Le1 = add(b, -22, 5, SPACE_NORMAL);  Le2 = add(b, -22, -5, SPACE_NORMAL);
    b->start_node = TL;
    route(b, TL, Jt1, NULL, 0); route(b, Jt1, Jt2, NULL, 0); route(b, Jt2, TR, NULL, 0);        /* top edge    */
    route(b, TR, Re1, NULL, 0); route(b, Re1, Re2, NULL, 0); route(b, Re2, BR, NULL, 0);        /* right edge  */
    route(b, BR, Jb2, NULL, 0); route(b, Jb2, Jb1, NULL, 0); route(b, Jb1, BL, NULL, 0);        /* bottom edge */
    route(b, BL, Le1, NULL, 0); route(b, Le1, Le2, NULL, 0); route(b, Le2, TL, NULL, 0);        /* left edge   */
    pe = route(b, Re1, Re2, pocket_e, 3);                                                        /* east pocket */
    pw = route(b, Le1, Le2, pocket_w, 3);                                                        /* west pocket */
    st1 = route(b, Jt1, M1, NULL, 0); route(b, M1, Jb1, NULL, 0);                                /* west avenue */
    st2 = route(b, Jt2, M2, NULL, 0); route(b, M2, Jb2, NULL, 0);                                /* east avenue */
    mid_bridge = route(b, M1, M2, NULL, 0);                                                      /* cross street */
    decorate(b, 21u);
    put(b, mid_bridge, SPACE_SHOP); put(b, pe, SPACE_BOWSER); put(b, pw, SPACE_BOWSER);
    put(b, st1, SPACE_PLUS_COIN); put(b, st2, SPACE_PLUS_COIN);
    put(b, TL, SPACE_NORMAL); put(b, b->nodes[TL].next[0], SPACE_PLUS_COIN);
    { int i, nshop = 0, nwarp = 0, nhid = 0;
      for (i = b->node_count - 1; i > 4; --i) {
          if (b->nodes[i].type != SPACE_PLUS_COIN && b->nodes[i].type != SPACE_MINUS_COIN && b->nodes[i].type != SPACE_EVENT) continue;
          if (i % 9 == 0 && nshop < 1) { b->nodes[i].type = SPACE_SHOP; nshop++; }
          else if (i % 6 == 0 && nwarp < 2) { b->nodes[i].type = SPACE_WARP; nwarp++; }
          else if (i % 13 == 0 && nhid < 1) { b->nodes[i].type = SPACE_HIDDEN_STAR; nhid++; }
      } }
    b->star_node = M1; put(b, M1, SPACE_STAR);
}

/* ------------------------------------------------------------------ */
/* Metro Plaza: two loops, a central plaza ring and one-way sky lanes    */
/* ------------------------------------------------------------------ */
void board_load_metro(Board *b)
{
    int w[17], e[17], p[8], i, mn, ms, ce;
    static const float lane_n[] = { -3, -9.2f, 3, -9.2f };
    static const float lane_s[] = { 3, 9.2f, -3, 9.2f };
    setup(b, "Metro Plaza");
    for (i = 0; i < 17; ++i) {
        float t = (float)i / 17.0f * 6.2831853f;
        w[i] = add(b, -15.5f + 8.5f * cosf(t), 11.0f * sinf(t), SPACE_NORMAL);
    }
    for (i = 0; i < 17; ++i) {
        float t = 3.14159265f + (float)i / 17.0f * 6.2831853f;
        e[i] = add(b, 15.5f + 8.5f * cosf(t), 11.0f * sinf(t), SPACE_NORMAL);
    }
    for (i = 0; i < 8; ++i) {
        float t = (float)i / 8.0f * 6.2831853f;
        p[i] = add(b, 2.8f * cosf(t), 4.8f * sinf(t), SPACE_NORMAL);
    }
    b->start_node = w[0];
    for (i = 0; i < 17; ++i) { lnk(b, w[i], w[(i + 1) % 17]); lnk(b, e[i], e[(i + 1) % 17]); }
    for (i = 0; i < 8; ++i) lnk(b, p[i], p[(i + 1) % 8]);
    /* plaza doors: west loop <-> plaza <-> east loop */
    lnk(b, w[0], p[4]); lnk(b, p[4], w[0]);
    lnk(b, e[0], p[0]); lnk(b, p[0], e[0]);
    /* one-way sky lanes above and below the plaza */
    mn = route(b, w[15], e[2], lane_n, 2);
    ms = route(b, e[15], w[2], lane_s, 2);
    ce = p[2];
    decorate(b, 33u);
    put(b, mn, SPACE_BOWSER); put(b, ms, SPACE_BOWSER);
    put(b, p[2], SPACE_SHOP); put(b, p[6], SPACE_SHOP); put(b, p[0], SPACE_EVENT); put(b, p[4], SPACE_EVENT);
    put(b, p[1], SPACE_PLUS_COIN); put(b, p[3], SPACE_PLUS_COIN); put(b, p[5], SPACE_PLUS_COIN); put(b, p[7], SPACE_PLUS_COIN);
    put(b, w[0], SPACE_NORMAL); put(b, w[1], SPACE_PLUS_COIN);
    put(b, w[8], SPACE_WARP); put(b, e[8], SPACE_WARP);
    put(b, w[5], SPACE_HIDDEN_COIN); put(b, e[12], SPACE_HIDDEN_STAR);
    put(b, w[11], SPACE_BOWSER); put(b, e[4], SPACE_BOWSER);
    put(b, e[9], SPACE_SHOP);
    b->star_node = e[8]; put(b, e[8], SPACE_STAR);
    (void)ce;
}

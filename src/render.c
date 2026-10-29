/* Board Game renderer - CPU-projected, painter-sorted 3D drawn as Tiny3D 2D quads.
 *
 * The scene is built from convex solids (cylinders, boxes, spheres, cones...) that are
 * projected on the CPU with the camera stored in GameContext, lit per face with a
 * directional light, sorted back-to-front by layer / group depth / quad depth and emitted
 * as flat-coloured TINY3D_QUADS in 2D mode. Triangles are degenerate quads. This avoids
 * any dependency on Tiny3D's matrix API. HUD, text, dice and popups are 2D overlays. */
#include "game.h"
#include "logger.h"
#include <tiny3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define SCR_W 848.0f
#define SCR_H 512.0f
#define PI_F 3.14159265f

/* ------------------------------------------------------------------ */
/* 2D primitives                                                       */
/* ------------------------------------------------------------------ */
static void draw_rect(float x, float y, float w, float h, u32 rgba) {
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x, y, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x + w, y, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x + w, y + h, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x, y + h, 0); tiny3d_VertexColor(rgba);
    tiny3d_End();
}
static void draw_circle(float cx, float cy, float r, u32 rgba, int segs) {
    int i; if (segs < 6) segs = 6;
    tiny3d_SetPolygon(TINY3D_TRIANGLE_FAN);
    tiny3d_VertexPos(cx, cy, 0); tiny3d_VertexColor(rgba);
    for (i = 0; i <= segs; ++i) {
        float a = (float)i / (float)segs * 6.2831853f;
        tiny3d_VertexPos(cx + cosf(a) * r, cy + sinf(a) * r, 0); tiny3d_VertexColor(rgba);
    }
    tiny3d_End();
}
static void draw_line(float x1, float y1, float x2, float y2, float th, u32 rgba) {
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy), nx, ny;
    if (len < 0.5f) return;
    nx = -dy / len * (th * 0.5f); ny = dx / len * (th * 0.5f);
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x1 + nx, y1 + ny, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x2 + nx, y2 + ny, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x2 - nx, y2 - ny, 0); tiny3d_VertexColor(rgba);
    tiny3d_VertexPos(x1 - nx, y1 - ny, 0); tiny3d_VertexColor(rgba);
    tiny3d_End();
}
static u32 player_color(int i) { static const u32 c[] = {0xFFE74C3C, 0xFF3498DB, 0xFF2ECC71, 0xFFF1C40F}; return c[i & 3]; }
static u32 player_color_dark(int i) { static const u32 c[] = {0xFFC0392B, 0xFF2980B9, 0xFF27AE60, 0xFFD4AC0D}; return c[i & 3]; }
static u32 col_scale(u32 c, float s) {
    int r = (int)(((c >> 16) & 255) * s), gg = (int)(((c >> 8) & 255) * s), b = (int)((c & 255) * s);
    if (r > 255) r = 255;
    if (gg > 255) gg = 255;
    if (b > 255) b = 255;
    return (c & 0xFF000000u) | ((u32)r << 16) | ((u32)gg << 8) | (u32)b;
}
static u32 col_alpha(u32 c, int a) { return (c & 0x00FFFFFFu) | ((u32)a << 24); }

/* ------------------------------------------------------------------ */
/* 3x5 bitmap font + batched text                                      */
/* ------------------------------------------------------------------ */
static const unsigned char *glyph(char ch) {
    static const unsigned char F[][5] = {
        {0x2,0x5,0x7,0x5,0x5},{0x6,0x5,0x6,0x5,0x6},{0x3,0x4,0x4,0x4,0x3},{0x6,0x5,0x5,0x5,0x6},
        {0x7,0x4,0x6,0x4,0x7},{0x7,0x4,0x6,0x4,0x4},{0x3,0x4,0x5,0x5,0x3},{0x5,0x5,0x7,0x5,0x5},
        {0x7,0x2,0x2,0x2,0x7},{0x1,0x1,0x1,0x5,0x2},{0x5,0x5,0x6,0x5,0x5},{0x4,0x4,0x4,0x4,0x7},
        {0x5,0x7,0x7,0x5,0x5},{0x6,0x5,0x5,0x5,0x5},{0x2,0x5,0x5,0x5,0x2},{0x6,0x5,0x6,0x4,0x4},
        {0x2,0x5,0x5,0x6,0x3},{0x6,0x5,0x6,0x5,0x5},{0x3,0x4,0x2,0x1,0x6},{0x7,0x2,0x2,0x2,0x2},
        {0x5,0x5,0x5,0x5,0x7},{0x5,0x5,0x5,0x5,0x2},{0x5,0x5,0x7,0x7,0x5},{0x5,0x5,0x2,0x5,0x5},
        {0x5,0x5,0x2,0x2,0x2},{0x7,0x1,0x2,0x4,0x7},
        {0x7,0x5,0x5,0x5,0x7},{0x2,0x6,0x2,0x2,0x7},{0x7,0x1,0x7,0x4,0x7},{0x7,0x1,0x3,0x1,0x7},
        {0x5,0x5,0x7,0x1,0x1},{0x7,0x4,0x7,0x1,0x7},{0x7,0x4,0x7,0x5,0x7},{0x7,0x1,0x1,0x1,0x1},
        {0x7,0x5,0x7,0x5,0x7},{0x7,0x5,0x7,0x1,0x7},
        {0,0,0,0,0x2},{0,0,0,0x2,0x4},{0x2,0x2,0x2,0,0x2},{0x7,0x1,0x3,0,0x2},{0,0x2,0,0x2,0},
        {0,0,0x7,0,0},{0,0x2,0x7,0x2,0},{0x1,0x1,0x2,0x4,0x4},{0x2,0x2,0,0,0},{0,0,0,0,0}
    };
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') return F[ch - 'A'];
    if (ch >= '0' && ch <= '9') return F[26 + (ch - '0')];
    switch (ch) {
    case '.': return F[36]; case ',': return F[37]; case '!': return F[38]; case '?': return F[39];
    case ':': return F[40]; case '-': return F[41]; case '+': return F[42]; case '/': return F[43];
    case '\'': return F[44]; default: return F[45];
    }
}
#define TXT_CHUNK 240
static void text_pass(float x, float y, const char *s, float sc, u32 col) {
    int inq = 0; float px = x;
    for (; *s; ++s) {
        const unsigned char *gl = glyph(*s);
        int r, c;
        for (r = 0; r < 5; ++r) {
            unsigned char b = gl[r]; c = 0;
            while (c < 3) {
                int run;
                if (!(b & (1 << (2 - c)))) { ++c; continue; }
                run = 1; while (c + run < 3 && (b & (1 << (2 - (c + run))))) ++run;
                if (inq == 0) tiny3d_SetPolygon(TINY3D_QUADS);
                {
                    float x0 = px + c * sc, y0 = y + r * sc, x1 = x0 + run * sc, y1 = y0 + sc;
                    tiny3d_VertexPos(x0, y0, 0); tiny3d_VertexColor(col);
                    tiny3d_VertexPos(x1, y0, 0); tiny3d_VertexColor(col);
                    tiny3d_VertexPos(x1, y1, 0); tiny3d_VertexColor(col);
                    tiny3d_VertexPos(x0, y1, 0); tiny3d_VertexColor(col);
                }
                if (++inq >= TXT_CHUNK) { tiny3d_End(); inq = 0; }
                c += run;
            }
        }
        px += 4 * sc;
    }
    if (inq) tiny3d_End();
}
static float text_w(const char *s, float sc) { int n = (int)strlen(s); return n > 0 ? n * 4 * sc - sc : 0; }
static void draw_text(float x, float y, const char *s, float sc, u32 col, int shadow) {
    if (shadow) text_pass(x + sc * 0.7f, y + sc * 0.7f, s, sc, 0xCC000000);
    text_pass(x, y, s, sc, col);
}
static void draw_text_c(float cx, float y, const char *s, float sc, u32 col, int shadow) {
    draw_text(cx - text_w(s, sc) * 0.5f, y, s, sc, col, shadow);
}
static void draw_numi(float x, float y, int v, float sc, u32 col, int shadow) {
    char b[16]; snprintf(b, sizeof(b), "%d", v); draw_text(x, y, b, sc, col, shadow);
}

/* ------------------------------------------------------------------ */
/* 3D: vectors, camera, quad buffer                                    */
/* ------------------------------------------------------------------ */
typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static V3 vnorm(V3 a) { float l = sqrtf(vdot(a, a)); return l > 1e-6f ? vmul(a, 1.0f / l) : v3(0, 0, 0); }

typedef struct { V3 pos, r, u, f; float focal; } Cam;
static Cam C;
static V3 LIGHT;

static void cam_setup(const GameContext *g, Cam *c) {
    V3 up = v3(0, 1, 0), p = v3(g->cam_pos[0], g->cam_pos[1], g->cam_pos[2]);
    V3 l = v3(g->cam_look[0], g->cam_look[1], g->cam_look[2]);
    float fov = g->cam_fov > 5.0f ? g->cam_fov : 45.0f;
    c->pos = p; c->f = vnorm(vsub(l, p));
    if (c->f.x == 0 && c->f.y == 0 && c->f.z == 0) c->f = v3(0, 0, -1);
    c->r = vnorm(vcross(c->f, up));
    if (c->r.x == 0 && c->r.y == 0 && c->r.z == 0) c->r = v3(1, 0, 0);
    c->u = vcross(c->r, c->f);
    c->focal = (SCR_H * 0.5f) / tanf(fov * 0.5f * PI_F / 180.0f);
}
static int proj(const Cam *c, V3 p, float *sx, float *sy, float *dep) {
    V3 d = vsub(p, c->pos);
    float vz = vdot(d, c->f);
    if (vz < 0.4f) return 0;
    *sx = SCR_W * 0.5f + vdot(d, c->r) / vz * c->focal;
    *sy = SCR_H * 0.5f - vdot(d, c->u) / vz * c->focal;
    *dep = vz; return 1;
}
int render_project(const GameContext *g, float x, float y, float z, float *sx, float *sy) {
    Cam c; float d; cam_setup(g, &c);
    return proj(&c, v3(x, y, z), sx, sy, &d);
}

enum { L_FLOOR = 1, L_DECAL, L_PATH, L_PATHTOP, L_PAD, L_PADTOP, L_SHADOW, L_OBJ, L_FX };
#define MAX_Q 4096
typedef struct { float x[4], y[4]; float gd, qd; u32 col; int layer; } Q;
static Q qb[MAX_Q];
static unsigned short ord[MAX_Q];
static int nq, g_layer;
static float g_gd;
#define F_CONVEX 1
#define F_UNLIT  2

static void grp(V3 c) { V3 d = vsub(c, C.pos); g_gd = vdot(d, C.f); }

static void wq(V3 a, V3 b, V3 c, V3 d, u32 col, int fl, V3 oc) {
    V3 p[4], n, ctr, view; float sx[4], sy[4], dz = 0; int i;
    if (nq >= MAX_Q) return;
    p[0] = a; p[1] = b; p[2] = c; p[3] = d;
    for (i = 0; i < 4; ++i) { float dep; if (!proj(&C, p[i], &sx[i], &sy[i], &dep)) return; dz += dep; }
    if ((sx[0] < -60 && sx[1] < -60 && sx[2] < -60 && sx[3] < -60) ||
        (sx[0] > SCR_W + 60 && sx[1] > SCR_W + 60 && sx[2] > SCR_W + 60 && sx[3] > SCR_W + 60) ||
        (sy[0] < -60 && sy[1] < -60 && sy[2] < -60 && sy[3] < -60) ||
        (sy[0] > SCR_H + 60 && sy[1] > SCR_H + 60 && sy[2] > SCR_H + 60 && sy[3] > SCR_H + 60)) return;
    n = vnorm(vcross(vsub(b, a), vsub(d, a)));
    ctr = vmul(vadd(vadd(a, b), vadd(c, d)), 0.25f);
    view = vsub(ctr, C.pos);
    if (fl & F_CONVEX) {
        if (vdot(n, vsub(ctr, oc)) < 0) n = vmul(n, -1.0f);
        if (vdot(n, view) >= 0) return;
    } else if (vdot(n, view) > 0) n = vmul(n, -1.0f);
    if (!(fl & F_UNLIT)) {
        float l = vdot(n, LIGHT); if (l < 0) l = 0;
        col = col_scale(col, 0.42f + 0.62f * l);
    }
    for (i = 0; i < 4; ++i) { qb[nq].x[i] = sx[i]; qb[nq].y[i] = sy[i]; }
    qb[nq].gd = g_gd; qb[nq].qd = dz * 0.25f; qb[nq].col = col; qb[nq].layer = g_layer;
    nq++;
}
static int qcmp(const void *A, const void *B) {
    const Q *a = &qb[*(const unsigned short *)A], *b = &qb[*(const unsigned short *)B];
    if (a->layer != b->layer) return a->layer - b->layer;
    if (a->gd != b->gd) return (a->gd > b->gd) ? -1 : 1;
    if (a->qd != b->qd) return (a->qd > b->qd) ? -1 : 1;
    return 0;
}
static void world_flush(void) {
    int i, k = 0;
    for (i = 0; i < nq; ++i) ord[i] = (unsigned short)i;
    if (nq > 1) qsort(ord, (size_t)nq, sizeof(unsigned short), qcmp);
    i = 0;
    while (i < nq) {
        int m = nq - i; if (m > 200) m = 200;
        tiny3d_SetPolygon(TINY3D_QUADS);
        for (k = 0; k < m; ++k) {
            const Q *q = &qb[ord[i + k]]; int j;
            for (j = 0; j < 4; ++j) { tiny3d_VertexPos(q->x[j], q->y[j], 0); tiny3d_VertexColor(q->col); }
        }
        tiny3d_End();
        i += m;
    }
    nq = 0;
}

/* ---- solids ---- */
static void tri(V3 a, V3 b, V3 c, u32 col, int fl, V3 oc) { wq(a, b, c, c, col, fl, oc); }
static void prim_disc(V3 c, float r, u32 col, int segs, int fl) {
    int i;
    for (i = 0; i < segs; ++i) {
        float a0 = (float)i / segs * 6.2831853f, a1 = (float)(i + 1) / segs * 6.2831853f;
        tri(c, v3(c.x + cosf(a0) * r, c.y, c.z + sinf(a0) * r), v3(c.x + cosf(a1) * r, c.y, c.z + sinf(a1) * r), col, fl, c);
    }
}
static void prim_ellipse(V3 c, float rx, float rz, u32 col, int segs, int fl) {
    int i;
    for (i = 0; i < segs; ++i) {
        float a0 = (float)i / segs * 6.2831853f, a1 = (float)(i + 1) / segs * 6.2831853f;
        tri(c, v3(c.x + cosf(a0) * rx, c.y, c.z + sinf(a0) * rz), v3(c.x + cosf(a1) * rx, c.y, c.z + sinf(a1) * rz), col, fl, c);
    }
}
static void prim_ring(V3 c, float r0, float r1, u32 col, int segs, int fl) {
    int i;
    for (i = 0; i < segs; ++i) {
        float a0 = (float)i / segs * 6.2831853f, a1 = (float)(i + 1) / segs * 6.2831853f;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        wq(v3(c.x + c0 * r0, c.y, c.z + s0 * r0), v3(c.x + c1 * r0, c.y, c.z + s1 * r0),
           v3(c.x + c1 * r1, c.y, c.z + s1 * r1), v3(c.x + c0 * r1, c.y, c.z + s0 * r1), col, fl, c);
    }
}
static void prim_cyl(V3 c, float r, float h, u32 side, u32 cap, int segs, int fl) {
    int i; V3 oc = v3(c.x, c.y + h * 0.5f, c.z), top = v3(c.x, c.y + h, c.z);
    for (i = 0; i < segs; ++i) {
        float a0 = (float)i / segs * 6.2831853f, a1 = (float)(i + 1) / segs * 6.2831853f;
        float x0 = cosf(a0) * r, z0 = sinf(a0) * r, x1 = cosf(a1) * r, z1 = sinf(a1) * r;
        wq(v3(c.x + x0, c.y, c.z + z0), v3(c.x + x1, c.y, c.z + z1), v3(c.x + x1, c.y + h, c.z + z1),
           v3(c.x + x0, c.y + h, c.z + z0), side, fl | F_CONVEX, oc);
        if (cap) tri(top, v3(c.x + x0, c.y + h, c.z + z0), v3(c.x + x1, c.y + h, c.z + z1), cap, fl | F_CONVEX, oc);
    }
}
static void prim_cone(V3 b, float r, float h, u32 col, int segs, float rot, int fl) {
    int i; V3 apex = v3(b.x, b.y + h, b.z), oc = v3(b.x, b.y + h * 0.3f, b.z);
    for (i = 0; i < segs; ++i) {
        float a0 = rot + (float)i / segs * 6.2831853f, a1 = rot + (float)(i + 1) / segs * 6.2831853f;
        tri(apex, v3(b.x + cosf(a0) * r, b.y, b.z + sinf(a0) * r), v3(b.x + cosf(a1) * r, b.y, b.z + sinf(a1) * r), col, fl | F_CONVEX, oc);
    }
}
static void prim_cone_dn(V3 b, float r, float h, u32 col, int segs, float rot, int fl) {
    int i; V3 apex = v3(b.x, b.y - h, b.z), oc = v3(b.x, b.y - h * 0.3f, b.z);
    for (i = 0; i < segs; ++i) {
        float a0 = rot + (float)i / segs * 6.2831853f, a1 = rot + (float)(i + 1) / segs * 6.2831853f;
        V3 p0 = v3(b.x + cosf(a0) * r, b.y, b.z + sinf(a0) * r), p1 = v3(b.x + cosf(a1) * r, b.y, b.z + sinf(a1) * r);
        tri(apex, p0, p1, col, fl | F_CONVEX, oc);
        tri(b, p1, p0, col_scale(col, 1.1f), fl | F_CONVEX, oc);
    }
}
static void prim_box(V3 c, V3 h, float yaw, u32 col, int fl) {
    float cy = cosf(yaw), sy = sinf(yaw); V3 p[8]; int i;
    for (i = 0; i < 8; ++i) {
        float lx = (i & 1) ? h.x : -h.x, ly = (i & 2) ? h.y : -h.y, lz = (i & 4) ? h.z : -h.z;
        p[i] = v3(c.x + lx * cy + lz * sy, c.y + ly, c.z - lx * sy + lz * cy);
    }
    fl |= F_CONVEX;
    wq(p[2], p[3], p[7], p[6], col, fl, c);           /* top */
    wq(p[4], p[5], p[7], p[6], col, fl, c);           /* +z */
    wq(p[0], p[1], p[3], p[2], col, fl, c);           /* -z */
    wq(p[1], p[5], p[7], p[3], col, fl, c);           /* +x */
    wq(p[0], p[4], p[6], p[2], col, fl, c);           /* -x */
}
static void prim_sphere(V3 c, V3 r, u32 col, int rings, int segs, int fl) {
    int i, j;
    for (j = 0; j < rings; ++j) {
        float f0 = -PI_F * 0.5f + (float)j / rings * PI_F, f1 = -PI_F * 0.5f + (float)(j + 1) / rings * PI_F;
        float y0 = sinf(f0) * r.y, y1 = sinf(f1) * r.y, k0 = cosf(f0), k1 = cosf(f1);
        for (i = 0; i < segs; ++i) {
            float a0 = (float)i / segs * 6.2831853f, a1 = (float)(i + 1) / segs * 6.2831853f;
            V3 A = v3(c.x + cosf(a0) * k0 * r.x, c.y + y0, c.z + sinf(a0) * k0 * r.z);
            V3 B = v3(c.x + cosf(a1) * k0 * r.x, c.y + y0, c.z + sinf(a1) * k0 * r.z);
            V3 Cc = v3(c.x + cosf(a1) * k1 * r.x, c.y + y1, c.z + sinf(a1) * k1 * r.z);
            V3 D = v3(c.x + cosf(a0) * k1 * r.x, c.y + y1, c.z + sinf(a0) * k1 * r.z);
            wq(A, B, Cc, D, col, fl | F_CONVEX, c);
        }
    }
}
/* upright discs (coins, rings, stars) facing direction `yaw` (0 = toward +z / camera) */
static V3 up_pt(V3 c, float yaw, float th, float r, float off) {
    return v3(c.x + cosf(yaw) * cosf(th) * r - sinf(yaw) * off, c.y + sinf(th) * r, c.z + sinf(yaw) * cosf(th) * r + cosf(yaw) * off);
}
static void prim_coin(V3 c, float r, float yaw, float thick, u32 front, u32 rim, int segs, int fl) {
    int i; V3 cf = up_pt(c, yaw, 0, 0, thick), cb = up_pt(c, yaw, 0, 0, -thick);
    for (i = 0; i < segs; ++i) {
        float a0 = (float)i / segs * 6.2831853f, a1 = (float)(i + 1) / segs * 6.2831853f;
        V3 f0 = up_pt(c, yaw, a0, r, thick), f1 = up_pt(c, yaw, a1, r, thick);
        V3 b0 = up_pt(c, yaw, a0, r, -thick), b1 = up_pt(c, yaw, a1, r, -thick);
        tri(cf, f0, f1, front, fl | F_CONVEX, c);
        tri(cb, b0, b1, col_scale(front, 0.9f), fl | F_CONVEX, c);
        wq(f0, f1, b1, b0, rim, fl | F_CONVEX, c);
    }
}
static void prim_upring(V3 c, float r0, float r1, float yaw, float thick, u32 col, int segs, int fl) {
    int i;
    for (i = 0; i < segs; ++i) {
        float a0 = (float)i / segs * 6.2831853f, a1 = (float)(i + 1) / segs * 6.2831853f;
        wq(up_pt(c, yaw, a0, r0, thick), up_pt(c, yaw, a1, r0, thick), up_pt(c, yaw, a1, r1, thick), up_pt(c, yaw, a0, r1, thick), col, fl, c);
        wq(up_pt(c, yaw, a0, r0, -thick), up_pt(c, yaw, a1, r0, -thick), up_pt(c, yaw, a1, r1, -thick), up_pt(c, yaw, a0, r1, -thick), col_scale(col, 0.85f), fl, c);
        wq(up_pt(c, yaw, a0, r1, thick), up_pt(c, yaw, a1, r1, thick), up_pt(c, yaw, a1, r1, -thick), up_pt(c, yaw, a0, r1, -thick), col_scale(col, 0.7f), fl, c);
    }
}
static void prim_star(V3 c, float r, float yaw, float thick, u32 col, int fl) {
    int i; float th0 = PI_F * 0.5f;
    V3 cf = up_pt(c, yaw, 0, 0, thick), cb = up_pt(c, yaw, 0, 0, -thick);
    for (i = 0; i < 10; ++i) {
        float a0 = th0 + (float)i / 10 * 6.2831853f, a1 = th0 + (float)(i + 1) / 10 * 6.2831853f;
        float r0 = (i & 1) ? r * 0.42f : r, r1 = (i & 1) ? r : r * 0.42f;
        V3 f0 = up_pt(c, yaw, a0, r0, thick), f1 = up_pt(c, yaw, a1, r1, thick);
        V3 b0 = up_pt(c, yaw, a0, r0, -thick), b1 = up_pt(c, yaw, a1, r1, -thick);
        tri(cf, f0, f1, col, fl | F_CONVEX, c);
        tri(cb, b0, b1, col_scale(col, 0.85f), fl | F_CONVEX, c);
        wq(f0, f1, b1, b0, col_scale(col, 0.7f), fl, c);
    }
}

/* ------------------------------------------------------------------ */
/* characters                                                          */
/* ------------------------------------------------------------------ */
static void draw_shadow(float x, float gy, float z, float r) {
    int sv = g_layer; float gd = g_gd;
    g_layer = L_SHADOW; grp(v3(x, gy, z));
    prim_disc(v3(x, gy + 0.03f, z), r, 0x70000000, 9, F_UNLIT);
    g_layer = sv; g_gd = gd;
}
/* mode: 0 normal */
static void draw_char3d(float x, float y, float z, int ci, float yaw, float sq, float arms) {
    u32 body = player_color(ci), dark = player_color_dark(ci);
    float sy = sinf(yaw), cy = cosf(yaw);
    V3 F = v3(sy, 0, cy), R = v3(cy, 0, -sy), P = v3(x, y, z), hc;
    int sv = g_layer;
    g_layer = L_OBJ; grp(v3(x, y + 1.0f, z));
    /* feet */
    prim_box(vadd(vadd(P, vmul(R, 0.24f)), vadd(vmul(F, 0.08f), v3(0, 0.11f, 0))), v3(0.17f, 0.11f, 0.24f), yaw, dark, 0);
    prim_box(vadd(vadd(P, vmul(R, -0.24f)), vadd(vmul(F, 0.08f), v3(0, 0.11f, 0))), v3(0.17f, 0.11f, 0.24f), yaw, dark, 0);
    /* body */
    prim_sphere(v3(x, y + 0.2f + 0.72f * sq, z), v3(0.52f, 0.56f * sq, 0.50f), body, 4, 8, 0);
    /* arms */
    prim_sphere(vadd(P, v3(R.x * 0.60f, 0.78f * sq + arms, R.z * 0.60f)), v3(0.17f, 0.17f, 0.17f), body, 2, 6, 0);
    prim_sphere(vadd(P, v3(-R.x * 0.60f, 0.78f * sq + arms, -R.z * 0.60f)), v3(0.17f, 0.17f, 0.17f), body, 2, 6, 0);
    /* head */
    hc = v3(x, y + 0.2f + 1.45f * sq, z);
    prim_sphere(hc, v3(0.46f, 0.44f, 0.46f), body, 4, 8, 0);
    /* face */
    prim_box(vadd(vadd(hc, vmul(F, 0.40f)), vadd(vmul(R, 0.17f), v3(0, 0.05f, 0))), v3(0.09f, 0.11f, 0.05f), yaw, 0xFFFFFFFF, 0);
    prim_box(vadd(vadd(hc, vmul(F, 0.40f)), vadd(vmul(R, -0.17f), v3(0, 0.05f, 0))), v3(0.09f, 0.11f, 0.05f), yaw, 0xFFFFFFFF, 0);
    prim_box(vadd(vadd(hc, vmul(F, 0.45f)), vadd(vmul(R, 0.17f), v3(0, 0.04f, 0))), v3(0.045f, 0.065f, 0.04f), yaw, 0xFF1A1A1A, 0);
    prim_box(vadd(vadd(hc, vmul(F, 0.45f)), vadd(vmul(R, -0.17f), v3(0, 0.04f, 0))), v3(0.045f, 0.065f, 0.04f), yaw, 0xFF1A1A1A, 0);
    prim_box(vadd(vadd(hc, vmul(F, 0.44f)), v3(0, -0.17f, 0)), v3(0.11f, 0.03f, 0.04f), yaw, 0xFF1A1A1A, 0);
    /* cap */
    prim_cyl(v3(hc.x, hc.y + 0.30f, hc.z), 0.38f, 0.22f, dark, dark, 8, 0);
    prim_box(vadd(vadd(hc, vmul(F, 0.34f)), v3(0, 0.33f, 0)), v3(0.30f, 0.025f, 0.22f), yaw, col_scale(dark, 0.85f), 0);
    prim_box(vadd(vadd(hc, vmul(F, 0.36f)), v3(0, 0.45f, 0)), v3(0.10f, 0.07f, 0.03f), yaw, 0xFFFFFFFF, 0);
    g_layer = sv;
}

/* ------------------------------------------------------------------ */
/* board scene                                                         */
/* ------------------------------------------------------------------ */
#define PAD_TOP 0.42f
static void space_colors(SpaceType t, int revealed, u32 *cap, float *r) {
    *cap = 0xFF95A5A6; *r = 1.2f;
    switch (t) {
    case SPACE_PLUS_COIN: *cap = 0xFF27AE60; break;
    case SPACE_MINUS_COIN: *cap = 0xFFC0392B; break;
    case SPACE_EVENT: *cap = 0xFF8E44AD; break;
    case SPACE_SHOP: *cap = 0xFFD35400; *r = 1.3f; break;
    case SPACE_STAR: *cap = 0xFFF39C12; *r = 1.45f; break;
    case SPACE_HIDDEN_COIN: case SPACE_HIDDEN_STAR: *cap = revealed ? 0xFF16A085 : 0xFF7F8C8D; break;
    case SPACE_WARP: *cap = 0xFF1ABC9C; *r = 1.3f; break;
    case SPACE_HAZARD: *cap = 0xFFE67E22; break;
    default: break;
    }
}
static void draw_pad(V3 c, SpaceType t, int revealed) {
    u32 cap; float r; V3 b = v3(c.x, 0, c.z);
    space_colors(t, revealed, &cap, &r);
    g_layer = L_PAD; grp(c);
    prim_cyl(b, r + 0.16f, 0.16f, 0xFF1C2833, 0, 10, 0);
    prim_ring(v3(c.x, 0.165f, c.z), r, r + 0.16f, 0xFF2C3E50, 10, 0);
    {
        V3 tb = v3(c.x, 0.16f, c.z);
        prim_cyl(tb, r, PAD_TOP - 0.16f, col_scale(cap, 0.65f), cap, 10, 0);
    }
}
static void draw_icon(V3 c, SpaceType t, int revealed, float tm, int idx) {
    float bob = 0.12f * sinf(tm * 3.0f + (float)idx), sp = tm * 2.2f + (float)idx;
    V3 p = v3(c.x, PAD_TOP + 1.25f + bob, c.z);
    g_layer = L_OBJ; grp(v3(c.x, PAD_TOP + 0.8f, c.z));
    if ((t == SPACE_HIDDEN_COIN || t == SPACE_HIDDEN_STAR) && !revealed) {
        prim_box(v3(c.x, PAD_TOP + 0.3f, c.z), v3(0.3f, 0.3f, 0.3f), 0.5f, 0xFF95A5A6, 0);
        return;
    }
    switch (t) {
    case SPACE_PLUS_COIN: case SPACE_HIDDEN_COIN:
        prim_coin(p, 0.55f, sp, 0.08f, 0xFFF1C40F, 0xFFB7950B, 10, 0); break;
    case SPACE_MINUS_COIN:
        prim_coin(p, 0.55f, sp, 0.08f, 0xFFE74C3C, 0xFF7B241C, 10, 0); break;
    case SPACE_EVENT:
        prim_box(p, v3(0.4f, 0.4f, 0.4f), sp, 0xFFB67AD8, 0); break;
    case SPACE_STAR: case SPACE_HIDDEN_STAR: {
        float pul = 0.6f + 0.4f * sinf(tm * 4.0f);
        prim_star(v3(c.x, PAD_TOP + 1.6f + bob, c.z), 0.95f, sp, 0.14f, 0xFFF9D342, 0);
        g_layer = L_PADTOP; grp(c);
        prim_ring(v3(c.x, PAD_TOP + 0.02f, c.z), 0.7f, 0.7f + 0.35f * pul, col_alpha(0xFFFFE066, 150), 12, F_UNLIT);
        break; }
    case SPACE_SHOP:
        prim_box(v3(c.x, PAD_TOP + 0.45f, c.z), v3(0.6f, 0.45f, 0.5f), 0.3f, 0xFFF5CBA7, 0);
        prim_cone(v3(c.x, PAD_TOP + 0.9f, c.z), 0.95f, 0.65f, 0xFFC0392B, 4, 0.785f + 0.3f, 0);
        prim_box(v3(c.x + 0.16f, PAD_TOP + 0.3f, c.z + 0.5f), v3(0.14f, 0.26f, 0.03f), 0.3f, 0xFF6E2C00, 0);
        break;
    case SPACE_WARP: {
        prim_upring(p, 0.5f, 0.85f, sp * 0.6f, 0.05f, 0xFF1ABC9C, 12, 0);
        prim_upring(p, 0.2f, 0.4f, sp * 0.6f + 1.57f, 0.04f, 0xFF76D7C4, 8, 0);
        break; }
    case SPACE_HAZARD: {
        int k;
        for (k = 0; k < 4; ++k) {
            float a = (float)k / 4 * 6.2831853f + 0.4f;
            prim_cone(v3(c.x + cosf(a) * 0.55f, PAD_TOP, c.z + sinf(a) * 0.55f), 0.2f, 0.8f, 0xFFECF0F1, 4, 0.3f, 0);
        }
        prim_cone(v3(c.x, PAD_TOP, c.z), 0.24f, 1.0f, 0xFFECF0F1, 4, 0.3f, 0);
        break; }
    default: break;
    }
}
static V3 node_pos(const GameContext *g, int i) {
    const BoardNode *n = &g->board->nodes[i];
    return v3((n->x - 0.5f) * WORLD_SX, PAD_TOP, (n->y - 0.5f) * WORLD_SZ);
}
static void ribbon(V3 a, V3 b, float w, float y, u32 col, int layer, int fl) {
    V3 d = vsub(b, a), s; float l = sqrtf(d.x * d.x + d.z * d.z);
    if (l < 0.01f) return;
    s = v3(-d.z / l * w * 0.5f, 0, d.x / l * w * 0.5f);
    g_layer = layer; grp(vmul(vadd(a, b), 0.5f));
    wq(v3(a.x + s.x, y, a.z + s.z), v3(b.x + s.x, y, b.z + s.z), v3(b.x - s.x, y, b.z - s.z), v3(a.x - s.x, y, a.z - s.z), col, fl, a);
}
static void palm(float x, float z, float h, float sc) {
    int k;
    g_layer = L_OBJ; grp(v3(x, h * 0.5f, z));
    prim_cyl(v3(x, 0, z), 0.2f * sc, h, 0xFF8D6E4B, 0, 6, 0);
    for (k = 0; k < 6; ++k) {
        float a = (float)k / 6 * 6.2831853f + x, ca = cosf(a), sa = sinf(a);
        V3 top = v3(x, h, z), tip = v3(x + ca * 1.7f * sc, h - 0.55f * sc, z + sa * 1.7f * sc);
        V3 s1 = v3(x + ca * 0.8f * sc - sa * 0.4f * sc, h + 0.12f * sc, z + sa * 0.8f * sc + ca * 0.4f * sc);
        V3 s2 = v3(x + ca * 0.8f * sc + sa * 0.4f * sc, h + 0.12f * sc, z + sa * 0.8f * sc - ca * 0.4f * sc);
        wq(top, s1, tip, s2, (k & 1) ? 0xFF2E9E4F : 0xFF3DBE63, 0, top);
    }
}
static void crystal(float x, float z, float h, float r, u32 col, float rot) {
    int i; V3 oc = v3(x, h * 0.4f, z);
    g_layer = L_OBJ; grp(oc);
    for (i = 0; i < 4; ++i) {
        float a0 = rot + (float)i / 4 * 6.2831853f, a1 = rot + (float)(i + 1) / 4 * 6.2831853f;
        V3 b0 = v3(x + cosf(a0) * r, 0, z + sinf(a0) * r), b1 = v3(x + cosf(a1) * r, 0, z + sinf(a1) * r);
        V3 m0 = v3(x + cosf(a0) * r * 0.75f, h * 0.7f, z + sinf(a0) * r * 0.75f), m1 = v3(x + cosf(a1) * r * 0.75f, h * 0.7f, z + sinf(a1) * r * 0.75f);
        wq(b0, b1, m1, m0, col, F_CONVEX, oc);
        tri(v3(x, h, z), m0, m1, col_scale(col, 1.25f), F_CONVEX, oc);
    }
}
static void building(float x, float z, float w, float d, float h, u32 col, float tm) {
    int i, j; V3 c = v3(x, h * 0.5f, z);
    g_layer = L_OBJ; grp(c);
    prim_box(c, v3(w, h * 0.5f, d), 0, col, 0);
    for (i = 0; i < 3; ++i)
        for (j = 0; j < (int)(h / 1.5f); ++j) {
            float wx = x - w * 0.55f + (float)i * w * 0.55f, wy = 1.0f + (float)j * 1.4f;
            u32 lc = (((i * 7 + j * 3 + (int)(x * 3)) & 3) == 0) ? 0xFF3A4A66 : 0xFFF5D76E;
            (void)tm;
            if (wy + 0.4f > h) continue;
            wq(v3(wx - 0.25f, wy, z + d + 0.02f), v3(wx + 0.25f, wy, z + d + 0.02f), v3(wx + 0.25f, wy + 0.5f, z + d + 0.02f),
               v3(wx - 0.25f, wy + 0.5f, z + d + 0.02f), lc, F_UNLIT, c);
        }
}
static void draw_scenery(const GameContext *g) {
    float tm = g->anim_t; int i;
    if (g->board_idx == 0) {
        /* Lagoon: water, island, palms */
        int ix, iz;
        for (ix = 0; ix < 8; ++ix) for (iz = 0; iz < 4; ++iz) {
            float x0 = -26.0f + ix * 6.5f, z0 = -16.0f + iz * 8.0f;
            float sh = 0.5f + 0.5f * sinf(tm * 1.2f + ix * 1.7f + iz * 2.3f);
            u32 col = ((ix + iz) & 1) ? 0xFF1F8FC4 : 0xFF2AA6D8;
            col = col_scale(col, 0.92f + 0.12f * sh);
            g_layer = L_FLOOR; grp(v3(x0 + 3.2f, 0, z0 + 4));
            wq(v3(x0, 0, z0), v3(x0 + 6.5f, 0, z0), v3(x0 + 6.5f, 0, z0 + 8), v3(x0, 0, z0 + 8), col, 0, v3(x0, 0, z0));
        }
        g_layer = L_DECAL; grp(v3(0, 0, 0));
        prim_ellipse(v3(0, 0.03f, 0), 12.6f, 6.1f, 0xFFE8D39A, 22, 0);
        g_layer = L_DECAL + 0; grp(v3(0, 0.01f, 0));
        prim_ellipse(v3(0, 0.05f, 0), 11.3f, 5.1f, 0xFF4FB46A, 22, 0);
        palm(-6.0f, -1.5f, 3.4f, 1.1f); palm(5.5f, -2.2f, 3.0f, 1.0f); palm(0.5f, 1.0f, 3.8f, 1.2f); palm(-3.0f, 2.2f, 2.6f, 0.9f);
        palm(-17.5f, -6.5f, 3.0f, 1.0f); palm(17.0f, -6.0f, 3.2f, 1.0f);
        g_layer = L_OBJ; grp(v3(8, 0.3f, 2));
        prim_sphere(v3(8.0f, 0.2f, 2.0f), v3(0.7f, 0.5f, 0.6f), 0xFF7F8C8D, 2, 6, 0);
        prim_sphere(v3(-9.0f, 0.2f, 1.0f), v3(0.6f, 0.4f, 0.5f), 0xFF95A5A6, 2, 6, 0);
    } else if (g->board_idx == 1) {
        int ix, iz;
        for (ix = 0; ix < 8; ++ix) for (iz = 0; iz < 4; ++iz) {
            float x0 = -26.0f + ix * 6.5f, z0 = -16.0f + iz * 8.0f;
            u32 col = ((ix + iz) & 1) ? 0xFF2B1B45 : 0xFF361F58;
            g_layer = L_FLOOR; grp(v3(x0 + 3.2f, 0, z0 + 4));
            wq(v3(x0, 0, z0), v3(x0 + 6.5f, 0, z0), v3(x0 + 6.5f, 0, z0 + 8), v3(x0, 0, z0 + 8), col, 0, v3(x0, 0, z0));
        }
        for (i = -4; i <= 4; ++i) {
            ribbon(v3(i * 3.4f, 0.02f, -10.0f), v3(i * 3.4f, 0.02f, 10.0f), 0.08f, 0.02f, 0x661ABC9C, L_DECAL, F_UNLIT);
        }
        for (i = -3; i <= 3; ++i)
            ribbon(v3(-17.0f, 0.02f, i * 3.2f), v3(17.0f, 0.02f, i * 3.2f), 0.08f, 0.02f, 0x66E67E22, L_DECAL, F_UNLIT);
        crystal(0.0f, -3.0f, 4.6f, 1.0f, 0xFF9B59B6, 0.3f);
        crystal(2.0f, -1.0f, 3.0f, 0.7f, 0xFF1ABC9C, 0.8f);
        crystal(-2.3f, -1.5f, 2.6f, 0.7f, 0xFFE67E22, 0.1f);
        crystal(0.5f, 2.5f, 3.2f, 0.8f, 0xFF3498DB, 0.5f);
        crystal(-16.5f, -8.5f, 3.6f, 0.9f, 0xFF9B59B6, 0.2f); crystal(16.5f, -8.5f, 3.2f, 0.9f, 0xFF1ABC9C, 0.6f);
        crystal(-16.5f, 8.5f, 2.2f, 0.7f, 0xFFE67E22, 0.4f); crystal(16.5f, 8.5f, 2.4f, 0.7f, 0xFF3498DB, 0.9f);
        crystal(-9.0f, -9.5f, 2.6f, 0.7f, 0xFF3498DB, 0.3f); crystal(9.0f, -9.5f, 3.0f, 0.8f, 0xFF9B59B6, 0.7f);
    } else {
        int ix, iz;
        for (ix = 0; ix < 8; ++ix) for (iz = 0; iz < 4; ++iz) {
            float x0 = -26.0f + ix * 6.5f, z0 = -16.0f + iz * 8.0f;
            u32 col = ((ix + iz) & 1) ? 0xFF2A2F45 : 0xFF313852;
            g_layer = L_FLOOR; grp(v3(x0 + 3.2f, 0, z0 + 4));
            wq(v3(x0, 0, z0), v3(x0 + 6.5f, 0, z0), v3(x0 + 6.5f, 0, z0 + 8), v3(x0, 0, z0 + 8), col, 0, v3(x0, 0, z0));
        }
        g_layer = L_DECAL; grp(v3(0, 0, 0));
        prim_disc(v3(0, 0.03f, 0), 2.6f, 0xFF5D6D7E, 14, 0);
        g_layer = L_OBJ; grp(v3(0, 0.6f, 0));
        prim_cyl(v3(0, 0, 0), 1.1f, 0.5f, 0xFF7F8C8D, 0xFF3498DB, 10, 0);
        prim_sphere(v3(0, 1.2f, 0), v3(0.35f, 0.35f, 0.35f), 0xFF85C1E9, 2, 6, 0);
        building(-15.0f, -11.5f, 2.2f, 1.6f, 7.0f, 0xFF34495E, tm); building(-9.5f, -11.5f, 2.0f, 1.6f, 4.5f, 0xFF3F5873, tm);
        building(-4.0f, -11.5f, 2.4f, 1.6f, 8.0f, 0xFF2E4057, tm); building(2.0f, -11.5f, 2.0f, 1.6f, 5.5f, 0xFF3F5873, tm);
        building(8.0f, -11.5f, 2.4f, 1.6f, 7.5f, 0xFF34495E, tm); building(14.0f, -11.5f, 2.0f, 1.6f, 5.0f, 0xFF2E4057, tm);
        building(-19.0f, -2.0f, 1.8f, 2.0f, 5.0f, 0xFF3F5873, tm); building(19.0f, -2.0f, 1.8f, 2.0f, 6.0f, 0xFF34495E, tm);
        building(-19.0f, 6.0f, 1.6f, 1.6f, 2.5f, 0xFF2E4057, tm); building(19.0f, 6.0f, 1.6f, 1.6f, 2.2f, 0xFF3F5873, tm);
    }
}
static float face_yaw[MAX_PLAYERS], prev_x[MAX_PLAYERS], prev_z[MAX_PLAYERS];
static float yaw_for(int i, float x, float z, int moving_ok) {
    float dx = x - prev_x[i], dz = z - prev_z[i];
    if (moving_ok && dx * dx + dz * dz > 0.0009f) {
        float t = atan2f(dx, dz), d = t - face_yaw[i];
        while (d > PI_F) d -= 6.2831853f;
        while (d < -PI_F) d += 6.2831853f;
        face_yaw[i] += d * 0.35f;
    } else {
        float d = -face_yaw[i];
        while (d > PI_F) d -= 6.2831853f;
        while (d < -PI_F) d += 6.2831853f;
        face_yaw[i] += d * 0.08f;
    }
    prev_x[i] = x; prev_z[i] = z;
    return face_yaw[i];
}
static void draw_board_scene(const GameContext *g) {
    const Board *b = g->board; int i, j;
    const Player *cur = &g->players[g->current_player];
    int active = (g->state != STATE_TITLE && g->state != STATE_SETUP && g->state != STATE_BOARD_SELECT);
    draw_scenery(g);
    /* path ribbons */
    for (i = 0; i < b->node_count; ++i) {
        const BoardNode *n = &b->nodes[i]; V3 a = node_pos(g, i);
        for (j = 0; j < n->next_count; ++j) {
            V3 m = node_pos(g, n->next[j]); int hl = 0;
            if (g->state == STATE_BRANCH && i == cur->node) hl = (j == g->branch_choice) ? 2 : 1;
            ribbon(a, m, 1.15f, 0.06f, 0xFF1C2833, L_PATH, 0);
            if (hl == 2) { ribbon(a, m, 0.9f, 0.09f, 0xFFF1C40F, L_PATHTOP, F_UNLIT); ribbon(a, m, 0.35f, 0.1f, 0xFFFFFFFF, L_PATHTOP, F_UNLIT); }
            else if (hl == 1) ribbon(a, m, 0.6f, 0.09f, 0xFF7F8C8D, L_PATHTOP, 0);
            else ribbon(a, m, 0.55f, 0.09f, 0xFFBDC3C7, L_PATHTOP, 0);
        }
    }
    for (i = 0; i < b->node_count; ++i) {
        V3 p = node_pos(g, i);
        draw_pad(p, b->nodes[i].type, b->nodes[i].hidden_revealed);
        draw_icon(p, b->nodes[i].type, b->nodes[i].hidden_revealed, g->anim_t, i);
        if (active && g->land_flash > 0 && i == cur->node) {
            float k = 1.0f - (float)g->land_flash / 20.0f;
            g_layer = L_PADTOP; grp(p);
            prim_ring(v3(p.x, PAD_TOP + 0.03f, p.z), 1.2f + k * 1.5f, 1.5f + k * 1.8f, col_alpha(0xFFFFFFFF, (int)(170 * (1.0f - k))), 14, F_UNLIT);
        }
    }
    /* players */
    for (i = 0; i < g->player_count; ++i) {
        const Player *p = &g->players[i]; float h = 0, sq = 1.0f, arms = 0, yaw;
        float mv = (p->hop_t < 1.0f) ? 1.0f : 0.0f;
        if (p->hop_t < 1.0f) h = sinf(p->hop_t * PI_F) * 1.5f;
        if (p->celebrating) { h += fabsf(sinf(g->anim_t * 10.0f)) * 0.9f; arms = 0.7f; }
        else if (active && i == g->current_player && p->hop_t >= 1.0f) h += fabsf(sinf(g->anim_t * 4.0f)) * 0.15f;
        if (p->hop_t >= 1.0f) sq = 1.0f + 0.04f * sinf(g->anim_t * 3.0f + i);
        yaw = yaw_for(i, p->disp_x, p->disp_y, mv > 0.5f || (p->hop_t >= 1.0f && active));
        draw_shadow(p->disp_x, PAD_TOP, p->disp_y, 0.62f * p->shadow_s * (1.0f - h * 0.12f));
        draw_char3d(p->disp_x, PAD_TOP + h, p->disp_y, p->color, yaw, sq, arms);
        if (p->shielded) {
            g_layer = L_FX; grp(v3(p->disp_x, PAD_TOP + h + 1.2f, p->disp_y));
            prim_sphere(v3(p->disp_x, PAD_TOP + h + 1.2f, p->disp_y), v3(1.0f, 1.3f, 1.0f), 0x443498DB, 3, 8, F_UNLIT);
        }
    }
    if (active) {
        float bob = sinf(g->anim_t * 5.0f) * 0.25f;
        g_layer = L_FX; grp(v3(cur->disp_x, 4.5f, cur->disp_y));
        prim_cone_dn(v3(cur->disp_x, PAD_TOP + 3.9f + bob, cur->disp_y), 0.45f, 0.8f, 0xFFF1C40F, 4, g->anim_t * 3.0f, F_UNLIT);
    }
}

/* ------------------------------------------------------------------ */
/* minigame stage                                                      */
/* ------------------------------------------------------------------ */
static void obj(V3 c) { g_layer = L_OBJ; grp(c); }
static int is_simul_play(const GameContext *g) { return g->mg_simul; }
static float pillar_h(const GameContext *g, int i) {
    if (g->state == STATE_MG_RESULT) return 0.5f + (float)(g->player_count - g->players[i].mg_rank) * 0.9f;
    if (is_simul_play(g)) return 0.4f + g->mg_p_meter[i] * 5.2f;
    return 0.0f;
}
static void stage_char_pos(const GameContext *g, int i, float *x, float *y, float *z) {
    float ph = pillar_h(g, i);
    *x = STAGE_SLOT_X(i, g->player_count);
    if (ph > 0.0f) { *y = ph; *z = 0.0f; } else { *y = 0.25f; *z = 1.5f; }
}
static void draw_stage_base(const GameContext *g, u32 plat, u32 trim) {
    int ix, iz, i; (void)g;
    for (ix = 0; ix < 6; ++ix) for (iz = 0; iz < 3; ++iz) {
        float x0 = -30.0f + ix * 10.0f, z0 = -16.0f + iz * 14.0f;
        g_layer = L_FLOOR; grp(v3(x0 + 5, -1.2f, z0 + 7));
        wq(v3(x0, -1.2f, z0), v3(x0 + 10, -1.2f, z0), v3(x0 + 10, -1.2f, z0 + 14), v3(x0, -1.2f, z0 + 14),
           ((ix + iz) & 1) ? 0xFF0F1626 : 0xFF131B2E, 0, v3(x0, -1.2f, z0));
    }
    obj(v3(0, -0.5f, 0));
    g_layer = L_FLOOR; grp(v3(0, 0, 0));
    prim_box(v3(0, -0.6f, 0), v3(15.5f, 0.6f, 6.6f), 0, plat, 0);
    for (i = -3; i <= 3; ++i)
        ribbon(v3(i * 4.0f, 0.01f, -6.0f), v3(i * 4.0f, 0.01f, 6.0f), 0.1f, 0.01f, trim, L_DECAL, F_UNLIT);
    ribbon(v3(-15.0f, 0.01f, -4.8f), v3(15.0f, 0.01f, -4.8f), 0.18f, 0.01f, trim, L_DECAL, F_UNLIT);
    ribbon(v3(-15.0f, 0.01f, 5.0f), v3(15.0f, 0.01f, 5.0f), 0.18f, 0.01f, trim, L_DECAL, F_UNLIT);
}
static void draw_stage_chars(const GameContext *g) {
    int i, n = g->player_count; float t = g->anim_t;
    int play = (g->state == STATE_MG_PLAY || g->state == STATE_MG_INTRO);
    for (i = 0; i < n; ++i) {
        float x, y, z, ph = pillar_h(g, i), arms = 0, bounce = 0; int actor = 0;
        stage_char_pos(g, i, &x, &y, &z);
        if (ph > 0.0f) {
            u32 pc = col_scale(player_color(i), 0.75f); int k;
            obj(v3(x, ph * 0.5f, 0));
            prim_box(v3(x, ph * 0.5f, 0), v3(0.95f, ph * 0.5f, 0.95f), 0, pc, 0);
            for (k = 1; k < (int)ph; ++k) prim_box(v3(x, (float)k, 0), v3(0.97f, 0.04f, 0.97f), 0, col_scale(pc, 1.3f), 0);
        } else {
            obj(v3(x, 0.12f, z));
            prim_cyl(v3(x, 0, z), 1.25f, 0.25f, player_color_dark(i), player_color(i), 10, 0);
        }
        if (!g->mg_simul && play && g->state == STATE_MG_PLAY && i == g->mg_actor) { actor = 1; }
        if (g->state == STATE_MG_INTRO && g->mg_intro_phase == INTRO_GATE && i == g->mg_gate_player) actor = 1;
        if (actor) { bounce = fabsf(sinf(t * 6.0f)) * 0.3f; arms = 0.3f; }
        if (g->state == STATE_MG_RESULT && g->players[i].mg_rank == 0) { bounce = fabsf(sinf(t * 9.0f)) * 0.8f; arms = 0.7f; }
        if (g->mg_simul && g->state == STATE_MG_PLAY) {
            if (g->mg_type == MG_MASH && g->players[i].is_human) bounce = fabsf(sinf(t * 20.0f)) * 0.12f * (g->mg_p_meter[i] > 0.02f);
            if (g->mg_type == MG_HOPPER) {
                if (g->mg_hop_flash[i] > 0) x += sinf(t * 70.0f) * 0.12f;
                else if (g->mg_safe) bounce = fabsf(sinf(t * 12.0f + i)) * 0.15f;
            }
        }
        draw_shadow(x, y, z, 0.7f);
        draw_char3d(x, y + bounce, z, g->players[i].color, 0.0f, 1.0f, arms);
        if (actor) {
            g_layer = L_FX; grp(v3(x, y + 4.0f, z));
            prim_cone_dn(v3(x, y + 3.9f + sinf(t * 5) * 0.2f, z), 0.4f, 0.7f, 0xFFF1C40F, 4, t * 3.0f, F_UNLIT);
        }
    }
}
static void lamp(V3 c, float r, u32 col, int lit) {
    obj(c);
    prim_sphere(c, v3(r, r, r), col, 3, 10, lit ? F_UNLIT : 0);
}
static void draw_mg_props(const GameContext *g) {
    float t = g->anim_t; int i; const float PZ = -2.4f;
    int resting = (g->state == STATE_MG_RESULT);
    if (resting) return;
    switch (g->mg_type) {
    case MG_MASH: {
        obj(v3(0, 5.9f, -1.6f));
        prim_box(v3(0, 5.9f, -1.6f), v3(12.0f, 0.05f, 0.05f), 0, 0xFFF1C40F, F_UNLIT);
        obj(v3(0, 7.4f, -2.0f));
        prim_star(v3(0, 7.4f, -2.0f), 0.8f, t * 2.0f, 0.12f, 0xFFF9D342, 0);
        break; }
    case MG_HOPPER: {
        int safe = g->mg_safe; u32 lc = safe ? 0xFF2ECC71 : 0xFFE74C3C;
        float pul = 1.0f + 0.06f * sinf(t * 14.0f);
        obj(v3(0, 5.9f, -1.6f));
        prim_box(v3(0, 5.9f, -1.6f), v3(12.0f, 0.05f, 0.05f), 0, 0xFFF1C40F, F_UNLIT);
        obj(v3(0, 7.6f, -4.0f));
        prim_box(v3(0, 7.6f, -4.1f), v3(1.5f, 1.5f, 0.2f), 0, 0xFF1C2833, 0);
        prim_sphere(v3(0, 7.6f, -3.8f), v3(1.15f * pul, 1.15f * pul, 0.5f), lc, 3, 10, F_UNLIT);
        break; }
    case MG_TIMING: case MG_ZONE: {
        float mx = (g->mg_marker - 0.5f) * 12.0f;
        obj(v3(0, 3.4f, PZ));
        prim_box(v3(0, 3.4f, PZ), v3(6.4f, 0.25f, 0.7f), 0, 0xFF1C2833, 0);
        obj(v3(0, 3.7f, PZ));
        if (g->mg_type == MG_TIMING) prim_box(v3(0, 3.7f, PZ), v3(0.96f, 0.07f, 0.5f), 0, 0xFF27AE60, 0);
        else prim_box(v3((g->mg_zone_c - 0.5f) * 12.0f, 3.7f, PZ), v3(g->mg_zone_w * 12.0f, 0.07f, 0.5f), 0, 0xFF27AE60, 0);
        obj(v3(mx, 4.2f, PZ + 0.2f));
        prim_box(v3(mx, 4.2f, PZ + 0.2f), v3(0.14f, 0.7f, 0.3f), 0, 0xFFF1C40F, F_UNLIT);
        prim_sphere(v3(mx, 5.0f, PZ + 0.2f), v3(0.25f, 0.25f, 0.25f), 0xFFFFFFFF, 2, 6, F_UNLIT);
        if (g->mg_type == MG_ZONE) {
            float f = (float)g->mg_zone_hits / 150.0f; if (f > 1) f = 1;
            obj(v3(0, 2.6f, PZ));
            prim_box(v3(0, 2.6f, PZ), v3(6.0f, 0.12f, 0.3f), 0, 0xFF1C2833, 0);
            if (f > 0.01f) prim_box(v3(-6.0f + f * 6.0f, 2.7f, PZ + 0.1f), v3(f * 6.0f, 0.12f, 0.25f), 0, 0xFF1ABC9C, F_UNLIT);
        }
        break; }
    case MG_REACTION: {
        u32 c = 0xFF5D6D7E; int lit = 0;
        if (g->state == STATE_MG_PLAY) {
            if (g->mg_locked && g->mg_human_score <= 8) { c = 0xFFE74C3C; lit = 1; }
            else if (g->mg_timer >= g->mg_go_at) { c = 0xFF2ECC71; lit = 1; }
        }
        obj(v3(0, 4.6f, PZ));
        prim_cyl(v3(0, 0, PZ), 0.3f, 2.8f, 0xFF34495E, 0xFF34495E, 6, 0);
        prim_sphere(v3(0, 4.6f, PZ), v3(1.8f, 1.8f, 1.8f), c, 4, 12, lit ? F_UNLIT : 0);
        if (lit && c == 0xFF2ECC71) { g_layer = L_FX; grp(v3(0, 4.6f, PZ)); prim_upring(v3(0, 4.6f, PZ + 0.6f), 2.0f, 2.3f + 0.2f * sinf(t * 18), 0, 0.02f, 0x88FFFFFF, 14, F_UNLIT); }
        break; }
    case MG_AVOID: {
        int ph = g->mg_phase; if (g->state != STATE_MG_PLAY) ph = 2;
        obj(v3(0, 4.2f, PZ));
        prim_box(v3(0, 4.2f, PZ), v3(1.3f, 2.5f, 0.6f), 0, 0xFF2C3E50, 0);
        prim_cyl(v3(0, 0, PZ), 0.25f, 1.7f, 0xFF34495E, 0xFF34495E, 6, 0);
        lamp(v3(0, 5.3f, PZ + 0.7f), 0.85f, ph == 0 ? 0xFFFF4B3E : 0xFF5B1A14, ph == 0);
        lamp(v3(0, 3.1f, PZ + 0.7f), 0.85f, ph == 1 ? 0xFF3CFF8A : 0xFF14532D, ph == 1);
        break; }
    case MG_HOLD: {
        float fh = g->mg_meter * 6.6f, ty = 0.5f + (float)g->mg_target / (float)(g->mg_target + 25) * 6.6f;
        obj(v3(0, 3.8f, PZ - 0.5f));
        prim_box(v3(0, 3.8f, PZ - 0.5f), v3(0.8f, 3.6f, 0.1f), 0, 0xFF1C2833, 0);
        if (fh > 0.05f) { obj(v3(0, 0.5f + fh * 0.5f, PZ)); prim_box(v3(0, 0.5f + fh * 0.5f, PZ), v3(0.5f, fh * 0.5f, 0.3f), 0, 0xFF3498DB, 0); }
        obj(v3(0, ty, PZ + 0.1f));
        prim_box(v3(0, ty, PZ + 0.1f), v3(0.95f, 0.07f, 0.4f), 0, 0xFFF1C40F, F_UNLIT);
        break; }
    case MG_COUNT: {
        int on = (g->state == STATE_MG_PLAY && g->mg_phase == 0 && g->mg_flash_count > 0 && g->mg_timer - g->mg_phase_start < 9);
        obj(v3(0, 4.6f, PZ));
        prim_sphere(v3(0, 4.6f, PZ), v3(1.6f, 1.6f, 1.6f), on ? 0xFFFFFFFF : 0xFF3B4B6B, 4, 12, on ? F_UNLIT : 0);
        if (g->state == STATE_MG_PLAY && g->mg_phase == 1) {
            int n = (int)g->mg_meter; if (n > 12) n = 12;
            for (i = 0; i < n; ++i) { obj(v3(-4.4f + i * 0.8f, 2.2f, PZ)); prim_box(v3(-4.4f + i * 0.8f, 2.2f, PZ), v3(0.3f, 0.3f, 0.3f), t + i, 0xFF9B59B6, 0); }
        }
        break; }
    case MG_RHYTHM: {
        float R = 1.5f + (1.0f - g->mg_marker) * 3.0f; int flash = (g->state == STATE_MG_PLAY && g->mg_timer - g->mg_phase_start < 6 && g->mg_flash_count > 0);
        obj(v3(0, 4.4f, PZ));
        prim_sphere(v3(0, 4.4f, PZ), v3(1.4f, 1.4f, 1.4f), flash ? 0xFFFFE066 : 0xFFB9770E, 4, 12, flash ? F_UNLIT : 0);
        if (g->state == STATE_MG_PLAY) { g_layer = L_FX; grp(v3(0, 4.4f, PZ + 1.0f)); prim_upring(v3(0, 4.4f, PZ + 1.2f), R - 0.15f, R, 0, 0.02f, 0xFFFFFFFF, 18, F_UNLIT); }
        for (i = 0; i < 6; ++i) { obj(v3(-2.0f + i * 0.8f, 2.2f, PZ)); prim_sphere(v3(-2.0f + i * 0.8f, 2.2f, PZ), v3(0.28f, 0.28f, 0.28f), (g->mg_beat_mask & (1 << i)) ? 0xFF2ECC71 : 0xFF566573, 2, 6, 0); }
        break; }
    case MG_CATCH: {
        float ox = (g->mg_fall_x - 0.5f) * 12.0f, oy = 1.9f + (1.0f - g->mg_fall_y) * 7.0f, bx = (g->mg_marker - 0.5f) * 12.0f;
        if (g->state == STATE_MG_PLAY) { obj(v3(ox, oy, PZ + 1.0f)); prim_sphere(v3(ox, oy, PZ + 1.0f), v3(0.65f, 0.65f, 0.65f), 0xFFF9D342, 3, 8, F_UNLIT); }
        obj(v3(bx, 0.8f, PZ + 1.0f));
        prim_cyl(v3(bx, 0.05f, PZ + 1.0f), 1.25f, 1.0f, 0xFF3498DB, 0xFF16324F, 10, 0);
        for (i = 0; i < 4; ++i) { obj(v3(-1.5f + i, 0.2f, PZ - 1.2f)); prim_sphere(v3(-1.5f + i, 0.3f, PZ - 1.2f), v3(0.28f, 0.28f, 0.28f), i < g->mg_beats_hit ? 0xFFF9D342 : 0xFF566573, 2, 6, 0); }
        break; }
    case MG_SIMON: {
        static const u32 cols[3] = {0xFF2ECC71, 0xFF3498DB, 0xFFE74C3C}; static const uint32_t btn[3] = {0x10, 0x20, 0x40};
        int k, filled = (g->mg_phase == 0) ? g->mg_flash_count : g->mg_input_idx;
        for (k = 0; k < 3; ++k) {
            int lit = (g->mg_prompt == btn[k]); float x = -4.2f + 4.2f * k, y = 3.6f + (lit ? 0.35f : 0.0f);
            V3 c = v3(x, y, PZ); obj(c);
            prim_box(c, v3(1.1f, 1.1f, 1.1f), t * 0.2f + k, lit ? col_scale(cols[k], 1.4f) : col_scale(cols[k], 0.5f), lit ? F_UNLIT : 0);
        }
        for (i = 0; i < g->mg_seq_len; ++i) { obj(v3(-1.6f + i * 0.8f, 6.3f, PZ)); prim_sphere(v3(-1.6f + i * 0.8f, 6.3f, PZ), v3(0.26f, 0.26f, 0.26f), i < filled ? 0xFFF1C40F : 0xFF566573, 2, 6, 0); }
        break; }
    default: break;
    }
}
static void draw_title_scene(const GameContext *g) {
    int i; float t = g->anim_t;
    draw_stage_base(g, 0xFF1B2A4A, 0xFF3498DB);
    for (i = 0; i < 4; ++i) {
        float x = STAGE_SLOT_X(i, 4), b = fabsf(sinf(t * 3.0f + i * 1.3f)) * 0.5f;
        obj(v3(x, 0.12f, 1.0f)); prim_cyl(v3(x, 0, 1.0f), 1.25f, 0.25f, player_color_dark(i), player_color(i), 10, 0);
        draw_shadow(x, 0.25f, 1.0f, 0.7f * (1.0f - b * 0.3f));
        draw_char3d(x, 0.25f + b, 1.0f, i, 0.0f, 1.0f, b);
    }
    obj(v3(0, 5.4f, -1.0f));
    prim_star(v3(0, 5.4f + sinf(t * 2.0f) * 0.2f, -1.0f), 1.5f, t * 1.5f, 0.25f, 0xFFF9D342, 0);
}
static void draw_results_scene(const GameContext *g, const int rank[MAX_PLAYERS]) {
    int i, n = g->player_count; float t = g->anim_t;
    draw_stage_base(g, 0xFF2A1A4A, 0xFFF1C40F);
    for (i = 0; i < n; ++i) {
        float x = STAGE_SLOT_X(i, n), h = 0.5f + (float)(n - rank[i]) * 0.8f, b = 0, arms = 0;
        u32 pc = col_scale(player_color(i), 0.75f);
        obj(v3(x, h * 0.5f, 0)); prim_box(v3(x, h * 0.5f, 0), v3(0.95f, h * 0.5f, 0.95f), 0, pc, 0);
        if (rank[i] == 0) { b = fabsf(sinf(t * 8.0f)) * 0.8f; arms = 0.7f; }
        draw_shadow(x, h, 0, 0.7f);
        draw_char3d(x, h + b, 0, g->players[i].color, 0.0f, 1.0f, arms);
    }
}
static void final_ranks(const GameContext *g, int rank[MAX_PLAYERS]) {
    int i, j;
    for (i = 0; i < g->player_count; ++i) {
        rank[i] = 0;
        for (j = 0; j < g->player_count; ++j) {
            if (j == i) continue;
            if (g->players[j].stars > g->players[i].stars ||
                (g->players[j].stars == g->players[i].stars && g->players[j].coins > g->players[i].coins) ||
                (g->players[j].stars == g->players[i].stars && g->players[j].coins == g->players[i].coins && j < i)) rank[i]++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 2D overlays                                                         */
/* ------------------------------------------------------------------ */
static const char *ITEM_NAMES[] = {"", "DOUBLE DICE", "BOOST", "STEAL", "SHIELD", "SWAP"};
static const char *ITEM_DESC[] = {"", "ROLL TWO DICE", "+3 STEPS", "TAKE 5 COINS", "BLOCK ONE LOSS", "SWAP SPOTS"};
static const char *RANK_NAMES[] = {"1ST", "2ND", "3RD", "4TH"};

static void draw_label_star(float x, float y, float r, u32 c) {
    int i;
    tiny3d_SetPolygon(TINY3D_TRIANGLE_FAN);
    tiny3d_VertexPos(x, y, 0); tiny3d_VertexColor(c);
    for (i = 0; i <= 10; ++i) {
        float a = -1.5707963f + (float)i / 10.0f * 6.2831853f, rr = (i & 1) ? r * 0.45f : r * 1.15f;
        tiny3d_VertexPos(x + cosf(a) * rr, y + sinf(a) * rr, 0); tiny3d_VertexColor(c);
    }
    tiny3d_End();
}
static void draw_item_icon(float x, float y, ItemType it, int sel) {
    u32 bg = sel ? 0xFFF1C40F : 0xFF34495E;
    draw_rect(x, y, 28, 28, bg); draw_rect(x + 2, y + 2, 24, 24, 0xFF1C2833);
    switch (it) {
    case ITEM_DOUBLE_DICE: draw_rect(x + 6, y + 6, 8, 8, 0xFFFFFFFF); draw_rect(x + 14, y + 14, 8, 8, 0xFFFFFFFF); break;
    case ITEM_BOOST: draw_rect(x + 12, y + 5, 4, 18, 0xFF2ECC71); draw_rect(x + 6, y + 12, 16, 4, 0xFF2ECC71); break;
    case ITEM_STEAL: draw_circle(x + 14, y + 14, 8, 0xFFE74C3C, 10); break;
    case ITEM_SHIELD: draw_circle(x + 14, y + 14, 9, 0xFF3498DB, 10); draw_circle(x + 14, y + 14, 5, 0xFF1C2833, 8); break;
    case ITEM_SWAP: draw_rect(x + 6, y + 8, 6, 12, 0xFFF1C40F); draw_rect(x + 16, y + 8, 6, 12, 0xFFE74C3C); draw_rect(x + 8, y + 12, 12, 4, 0xFFFFFFFF); break;
    default: break;
    }
}
static void draw_minimap(const GameContext *g, float mx, float my, float mw, float mh) {
    const Board *b = g->board; int i, j;
    draw_rect(mx - 4, my - 4, mw + 8, mh + 8, 0xFF0D1117);
    draw_rect(mx, my, mw, mh, g->board_idx == 0 ? 0xFF0A3A4A : (g->board_idx == 1 ? 0xFF22103A : 0xFF1A2038));
    for (i = 0; i < b->node_count; ++i) {
        const BoardNode *n = &b->nodes[i];
        for (j = 0; j < n->next_count; ++j) {
            const BoardNode *m = &b->nodes[n->next[j]];
            draw_line(mx + n->x * mw, my + n->y * mh, mx + m->x * mw, my + m->y * mh, 2, 0xFF5D6D7E);
        }
    }
    for (i = 0; i < b->node_count; ++i) {
        const BoardNode *n = &b->nodes[i]; u32 c = 0xFF95A5A6;
        if (n->type == SPACE_STAR) c = 0xFFF1C40F; else if (n->type == SPACE_SHOP) c = 0xFFE67E22;
        else if (n->type == SPACE_PLUS_COIN) c = 0xFF2ECC71; else if (n->type == SPACE_MINUS_COIN) c = 0xFFE74C3C;
        else if (n->type == SPACE_WARP) c = 0xFF1ABC9C; else if (n->type == SPACE_HAZARD) c = 0xFFE67E22;
        else if (n->type == SPACE_EVENT) c = 0xFF9B59B6;
        draw_circle(mx + n->x * mw, my + n->y * mh, n->type == SPACE_STAR ? 5.f : 3.5f, c, 10);
    }
    draw_circle(mx + b->nodes[b->start_node].x * mw, my + b->nodes[b->start_node].y * mh, 6, 0xFFFFFFFF, 10);
}
static void draw_die_face(float cx, float cy, float size, int value, u32 body, u32 pip) {
    float h = size * 0.5f, pr = size * 0.09f, o = size * 0.22f; int v = value;
    if (v < 1) v = 1;
    if (v > 6) v = 6;
    draw_rect(cx - h + 4, cy + h - 2, size - 4, 6, 0x55000000); draw_rect(cx - h, cy - h, size, size, 0xFF1A1A1A);
    draw_rect(cx - h + 3, cy - h + 3, size - 6, size - 6, body); draw_rect(cx - h + 3, cy - h + 3, size - 6, 4, 0x33FFFFFF);
    if (v == 1 || v == 3 || v == 5) draw_circle(cx, cy, pr, pip, 8);
    if (v >= 2) { draw_circle(cx - o, cy - o, pr, pip, 8); draw_circle(cx + o, cy + o, pr, pip, 8); }
    if (v >= 4) { draw_circle(cx + o, cy - o, pr, pip, 8); draw_circle(cx - o, cy + o, pr, pip, 8); }
    if (v == 6) { draw_circle(cx - o, cy, pr, pip, 8); draw_circle(cx + o, cy, pr, pip, 8); }
}
static void draw_dice(const GameContext *g) {
    float cx = 770, cy = 120, pulse = 1 + 0.06f * sinf(g->anim_t * 8);
    if (g->state == STATE_DICE_ROLL || g->state == STATE_ITEM) {
        int spin = 1 + ((int)(g->anim_t * 12) % 6);
        draw_die_face(cx, cy, 52 * pulse, spin, 0xFFFFFFFF, 0xFF1A1A1A);
        if (g->double_dice) draw_die_face(cx - 56, cy + 10, 40 * pulse, spin, 0xFFFFF8E7, 0xFF1A1A1A);
    } else if (g->dice_value > 0 && (g->state == STATE_MOVE || g->state == STATE_BRANCH || g->state == STATE_LAND || g->state == STATE_SHOP)) {
        draw_die_face(cx, cy, 48, g->dice_value, 0xFFFFF8E7, 0xFF1A1A1A);
        if (g->dice_value2 > 0) draw_die_face(cx - 56, cy + 8, 40, g->dice_value2, 0xFFFFF8E7, 0xFF1A1A1A);
        draw_text_c(cx, cy + 36, "STEPS", 2, 0xFFFFFFFF, 1);
        draw_numi(cx - 6, cy + 52, g->move_remaining, 4, 0xFF3498DB, 1);
    }
}
static void draw_popup(const GameContext *g) {
    char b[24]; u32 col = 0xFFFFFFFF; float a;
    if (!g->popup.active) return;
    a = g->popup.life; (void)a;
    switch (g->popup.kind) {
    case 0: snprintf(b, sizeof(b), "+%d", g->popup.value); col = 0xFF2ECC71; break;
    case 1: snprintf(b, sizeof(b), "+1 STAR!"); col = 0xFFF1C40F; break;
    case 2: if (g->popup.value < 0) snprintf(b, sizeof(b), "%d", g->popup.value); else snprintf(b, sizeof(b), "NO STAR!"); col = 0xFFE74C3C; break;
    case 3: snprintf(b, sizeof(b), "SHOP!"); col = 0xFFE67E22; break;
    case 4: if (g->popup.value > 0) snprintf(b, sizeof(b), "+%d", g->popup.value); else if (g->popup.value < 0) snprintf(b, sizeof(b), "%d", g->popup.value); else snprintf(b, sizeof(b), "WARP!"); col = 0xFF9B59B6; break;
    case 5: snprintf(b, sizeof(b), g->popup.value ? "ITEM!" : "BLOCKED!"); col = 0xFF3498DB; break;
    case 6: snprintf(b, sizeof(b), "SWAP!"); col = 0xFF1ABC9C; break;
    default: snprintf(b, sizeof(b), g->popup.value == 2 ? "DOUBLE DICE!" : (g->popup.value == 3 ? "BOOST +3!" : "SHIELD UP!")); col = 0xFF3498DB; break;
    }
    draw_text_c(g->popup.x, g->popup.y, b, 4, col, 1);
}
static void draw_fx(const GameContext *g) {
    int i;
    for (i = 0; i < MAX_FX; ++i) if (g->fx[i].active) draw_rect(g->fx[i].x, g->fx[i].y, 4, 4, g->fx[i].color);
}
static void draw_hint(const char *s) {
    float w = text_w(s, 3) + 32;
    draw_rect(424 - w * 0.5f, 462, w, 28, 0xCC0A0E14);
    draw_text_c(424, 468, s, 3, 0xFFFFFFFF, 1);
}
static void draw_hud(const GameContext *g) {
    int i, j; char b[24];
    draw_rect(0, 0, SCR_W, 56, 0xF00A0E14); draw_rect(0, 56, SCR_W, 3, g->team_mode ? 0xFFE67E22 : 0xFF3498DB);
    for (i = 0; i < g->player_count; ++i) {
        const Player *p = &g->players[i]; float bx = 16 + i * 205;
        draw_rect(bx, 6, 193, 44, 0xFF1C2833);
        if (i == g->current_player) { draw_rect(bx, 6, 193, 44, 0x553498DB); draw_rect(bx, 48, 193, 3, 0xFFF1C40F); }
        if (g->team_mode) draw_rect(bx, 6, 6, 44, p->team == 0 ? 0xFFE74C3C : 0xFF3498DB);
        draw_circle(bx + 24, 28, 14, player_color(p->color), 12);
        if (p->is_human) snprintf(b, sizeof(b), "P%d", i + 1); else snprintf(b, sizeof(b), "CPU");
        draw_text(bx + 44, 9, b, 2, 0xFFFFFFFF, 0);
        draw_label_star(bx + 52, 36, 7, 0xFFF1C40F); draw_numi(bx + 64, 28, p->stars, 3, 0xFFF1C40F, 0);
        draw_circle(bx + 112, 36, 7, 0xFFF39C12, 8); draw_numi(bx + 124, 28, p->coins, 3, 0xFFF39C12, 0);
        if (i == g->current_player)
            for (j = 0; j < p->item_count; ++j) draw_item_icon(bx + 150 + j * 20 - 4, 8, p->items[j], 0);
    }
    snprintf(b, sizeof(b), "ROUND %d/%d", g->round > g->max_rounds ? g->max_rounds : g->round, g->max_rounds);
    draw_text(16, 492, b, 2, 0xFFFFFFFF, 1);
}
static void draw_item_panel(const GameContext *g) {
    const Player *p = &g->players[g->current_player]; int i;
    draw_rect(250, 170, 348, 150, 0xEE1A2332); draw_rect(258, 178, 332, 134, 0xFF243447);
    draw_text_c(424, 186, "USE AN ITEM?", 3, 0xFFF1C40F, 1);
    for (i = 0; i < p->item_count; ++i) draw_item_icon(300 + i * 60, 220, p->items[i], i == g->item_cursor);
    draw_rect(300 + p->item_count * 60, 220, 28, 28, g->item_cursor >= p->item_count ? 0xFFF1C40F : 0xFF2C3E50);
    draw_text(304 + p->item_count * 60, 230, "NO", 2, 0xFFFFFFFF, 0);
    if (g->item_cursor < p->item_count) {
        draw_text_c(424, 262, ITEM_NAMES[p->items[g->item_cursor]], 3, 0xFFFFFFFF, 1);
        draw_text_c(424, 286, ITEM_DESC[p->items[g->item_cursor]], 2, 0xFFBDC3C7, 0);
    } else draw_text_c(424, 262, "SKIP", 3, 0xFFFFFFFF, 1);
}
static void draw_shop_panel(const GameContext *g) {
    const Player *p = &g->players[g->current_player]; int c = g->item_cursor % 3; char b[16];
    int cost = 20;
    { int best = 0, k; for (k = 1; k < g->player_count; ++k) if (g->players[k].stars < g->players[best].stars || (g->players[k].stars == g->players[best].stars && g->players[k].coins < g->players[best].coins)) best = k; if (p->id == best && g->player_count > 1) cost = 15; }
    draw_rect(210, 140, 428, 230, 0xEE1A2332); draw_rect(218, 148, 412, 214, 0xFF243447);
    draw_text_c(424, 156, "SHOP", 4, 0xFFE67E22, 1);
    draw_rect(236, 190, 110, 100, c == 0 ? 0xFFF1C40F : 0xFF1C2833); draw_label_star(291, 222, 14, 0xFFFFFFFF);
    snprintf(b, sizeof(b), "%d", cost); draw_text_c(291, 250, b, 3, c == 0 ? 0xFF1A1A1A : 0xFFF39C12, 0);
    draw_text_c(291, 272, "STAR", 2, c == 0 ? 0xFF1A1A1A : 0xFFBDC3C7, 0);
    draw_rect(366, 190, 110, 100, c == 1 ? 0xFF9B59B6 : 0xFF1C2833); draw_circle(421, 222, 14, 0xFFFFFFFF, 10);
    draw_text_c(421, 250, "8", 3, 0xFFF39C12, 0); draw_text_c(421, 272, "ITEM", 2, 0xFFBDC3C7, 0);
    draw_rect(496, 190, 110, 100, c == 2 ? 0xFF7F8C8D : 0xFF1C2833); draw_text_c(551, 232, "LEAVE", 2, 0xFFFFFFFF, 0);
    snprintf(b, sizeof(b), "COINS %d", p->coins); draw_text_c(424, 306, b, 3, 0xFFF39C12, 1);
    draw_text_c(424, 336, "O: CHANGE   X: BUY", 2, 0xFFBDC3C7, 0);
}
static void sky(u32 top, u32 bot) {
    draw_rect(0, 0, SCR_W, SCR_H * 0.5f, top); draw_rect(0, SCR_H * 0.5f, SCR_W, SCR_H * 0.5f, bot);
}
static u32 mg_color(MiniGameType t) {
    static const u32 c[MG_TYPE_COUNT] = {0xFF2ECC71, 0xFFF1C40F, 0xFFE74C3C, 0xFF3498DB, 0xFF9B59B6, 0xFFC0392B, 0xFF1ABC9C, 0xFFF39C12, 0xFF2980B9, 0xFF8E44AD, 0xFFE67E22};
    return c[(int)t < MG_TYPE_COUNT ? (int)t : 0];
}
static void draw_label_at(const GameContext *g, int i) {
    float x, y, z, sx, sy; char b[16]; const Player *p = &g->players[i];
    stage_char_pos(g, i, &x, &y, &z);
    if (!render_project(g, x, y + 2.9f, z, &sx, &sy)) return;
    if (p->is_human) snprintf(b, sizeof(b), "P%d", i + 1); else snprintf(b, sizeof(b), "CPU");
    draw_text_c(sx, sy - 6, b, 3, player_color(p->color), 1);
}
static void draw_mg_banner(const GameContext *g, const char *extra) {
    const MgInfo *mi = mg_info(g->mg_type); char b[48];
    draw_rect(0, 0, SCR_W, 40, 0xE00A0E14); draw_rect(0, 40, SCR_W, 3, mg_color(g->mg_type));
    draw_text(16, 10, mi->name, 3, 0xFFFFFFFF, 1);
    if (extra) { snprintf(b, sizeof(b), "%s", extra); draw_text(SCR_W - 16 - text_w(b, 3), 10, b, 3, 0xFFF1C40F, 1); }
}
static void draw_mg_intro_overlay(const GameContext *g) {
    const MgInfo *mi = mg_info(g->mg_type); int i, n = g->player_count; char b[32];
    int ph = g->mg_intro_phase; u32 mc = mg_color(g->mg_type);
    if (ph == INTRO_READY) {
        draw_rect(50, 20, 748, 312, 0xE00B1020); draw_rect(50, 20, 748, 4, mc);
        draw_rect(50, 24, 748, 62, mc); draw_text_c(424, 38, mi->name, 5, 0xFFFFFFFF, 1);
        draw_text_c(424, 98, "GOAL", 2, 0xFFF1C40F, 0); draw_text_c(424, 118, mi->goal, 3, 0xFFFFFFFF, 1);
        draw_text_c(424, 156, "RULE", 2, 0xFFF1C40F, 0); draw_text_c(424, 176, mi->rule, 3, 0xFFDDE6F0, 1);
        draw_text_c(424, 214, "CONTROLS", 2, 0xFFF1C40F, 0); draw_text_c(424, 234, mi->controls, 3, 0xFF7FDBFF, 1);
        {
            const char *m = mi->simul ? "ALL PLAYERS AT ONCE" : "TAKE TURNS";
            float w = text_w(m, 3) + 28;
            draw_rect(424 - w * 0.5f, 278, w, 32, mi->simul ? 0xFF1E8449 : 0xFFB9770E);
            draw_text_c(424, 286, m, 3, 0xFFFFFFFF, 1);
        }
        for (i = 0; i < n; ++i) {
            float bx = 424.0f + ((float)i - (float)(n - 1) * 0.5f) * 200.0f - 90.0f; int rdy = g->mg_ready[i], hum = g->players[i].is_human;
            draw_rect(bx, 346, 180, 70, 0xE00B1020); draw_rect(bx, 346, 180, 6, player_color(i));
            if (hum) snprintf(b, sizeof(b), "PLAYER %d", i + 1); else snprintf(b, sizeof(b), "CPU %d", i + 1);
            draw_text_c(bx + 90, 358, b, 2, player_color(i), 0);
            if (rdy) draw_text_c(bx + 90, 382, hum ? "READY!" : "CPU READY", hum ? 3 : 2, 0xFF2ECC71, 1);
            else if (((int)(g->anim_t * 3)) & 1) draw_text_c(bx + 90, 382, "PRESS X", 3, 0xFFFFFFFF, 1);
            else draw_text_c(bx + 90, 382, "PRESS X", 3, 0xFF8899AA, 1);
        }
        draw_text_c(424, 432, "WAITING FOR ALL PLAYERS", 2, 0xFFBDC3C7, 0);
        draw_text_c(424, 452, "P1 TRIANGLE: START NOW", 2, 0xFF8899AA, 0);
    } else if (ph == INTRO_GATE) {
        int gp = g->mg_gate_player;
        draw_mg_banner(g, mi->simul ? "ALL AT ONCE" : "TAKE TURNS");
        draw_rect(204, 120, 440, 150, 0xE00B1020); draw_rect(204, 120, 440, 6, player_color(gp));
        snprintf(b, sizeof(b), "PLAYER %d", gp + 1);
        draw_text_c(424, 138, b, 5, player_color(gp), 1);
        draw_text_c(424, 184, "GET READY!", 3, 0xFFFFFFFF, 1);
        if (((int)(g->anim_t * 3)) & 1) draw_text_c(424, 218, "PRESS X", 4, 0xFFF1C40F, 1);
        draw_text_c(424, 252, mi->controls, 2, 0xFF7FDBFF, 0);
    } else {
        int fpn = (g->mg_gate_player >= 0) ? 30 : 45, k = g->mg_intro_t / fpn; float fr = (float)(g->mg_intro_t % fpn) / (float)fpn;
        draw_mg_banner(g, NULL);
        if (k < 3) {
            snprintf(b, sizeof(b), "%d", 3 - k);
            draw_text_c(424, 150, b, 22.0f - 6.0f * fr, 0xFFFFFFFF, 1);
        } else draw_text_c(424, 150, "GO!", 16.0f, 0xFF2ECC71, 1);
        draw_text_c(424, 410, mi->controls, 3, 0xFF7FDBFF, 1);
    }
}
static void draw_bar(float x, float y, float w, float h, float f, u32 col) {
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    draw_rect(x - 2, y - 2, w + 4, h + 4, 0xFF0A0E14); draw_rect(x, y, w, h, 0xFF1C2833);
    if (f > 0.001f) draw_rect(x, y, w * f, h, col);
}
static void draw_mg_play_overlay(const GameContext *g) {
    const MgInfo *mi = mg_info(g->mg_type); char b[48]; int i;
    if (!mi->simul) snprintf(b, sizeof(b), "PLAYER %d", g->mg_actor + 1); else snprintf(b, sizeof(b), "ALL AT ONCE");
    draw_mg_banner(g, b);
    for (i = 0; i < g->player_count; ++i) draw_label_at(g, i);
    draw_rect(0, 472, SCR_W, 40, 0xE00A0E14);
    draw_text_c(424, 484, mi->controls, 3, 0xFF7FDBFF, 1);
    if (g->mg_time_max > 0) draw_bar(224, 452, 400, 8, 1.0f - (float)g->mg_timer / (float)g->mg_time_max, 0xFFE74C3C);
    switch (g->mg_type) {
    case MG_REACTION:
        if (g->mg_locked) {} else if (g->mg_timer < g->mg_go_at) draw_text_c(424, 70, "WAIT...", 6, 0xFFBDC3C7, 1);
        else draw_text_c(424, 70, "NOW!", 8, 0xFF2ECC71, 1);
        break;
    case MG_AVOID:
        if (!g->mg_locked) { if (g->mg_phase == 0) draw_text_c(424, 70, "WAIT...", 6, 0xFFE74C3C, 1); else draw_text_c(424, 70, "GO!", 8, 0xFF2ECC71, 1); }
        break;
    case MG_HOPPER:
        draw_text_c(424, 392, g->mg_safe ? "GO! HOP!" : "STOP!", 6, g->mg_safe ? 0xFF2ECC71 : 0xFFE74C3C, 1);
        break;
    case MG_MASH: draw_text_c(424, 56, "MASH X!", 5, 0xFFFFFFFF, 1); break;
    case MG_HOLD: if (!g->mg_locked) draw_text_c(424, 60, g->mg_hold_frames > 0 ? "NOW LET GO AT THE LINE" : "HOLD X", 3, 0xFFFFFFFF, 1); break;
    case MG_COUNT:
        if (g->mg_phase == 0) draw_text_c(424, 60, "WATCH THE LIGHT", 4, 0xFFFFFFFF, 1);
        else { snprintf(b, sizeof(b), "YOUR COUNT: %d", (int)g->mg_meter); draw_text_c(424, 60, b, 4, 0xFF9B59B6, 1); draw_text_c(424, 92, "X: ADD   O: SUBMIT", 3, 0xFFFFFFFF, 1); }
        break;
    case MG_RHYTHM: snprintf(b, sizeof(b), "HITS %d/%d  MISSES %d", g->mg_beats_hit, g->mg_beats_total, g->mg_misses); draw_text_c(424, 56, b, 3, 0xFFFFFFFF, 1); break;
    case MG_CATCH: snprintf(b, sizeof(b), "CAUGHT %d/%d", g->mg_beats_hit, g->mg_beats_total); draw_text_c(424, 56, b, 4, 0xFFF9D342, 1); break;
    case MG_SIMON:
        if (g->mg_phase == 0) draw_text_c(424, 60, "WATCH THE PATTERN", 4, 0xFFFFFFFF, 1);
        else draw_text_c(424, 60, "YOUR TURN! COPY IT", 4, 0xFF2ECC71, 1);
        break;
    case MG_ZONE: draw_text_c(424, 60, "HOLD X TO SLIDE RIGHT", 3, 0xFFFFFFFF, 1); break;
    case MG_TIMING: if (!g->mg_locked) draw_text_c(424, 60, "PRESS X IN THE MIDDLE", 3, 0xFFFFFFFF, 1); break;
    default: break;
    }
    if (!mi->simul && g->mg_locked) {
        snprintf(b, sizeof(b), "SCORE %d", g->mg_human_score);
        draw_text_c(424, 150, b, 7, g->mg_human_score >= 60 ? 0xFF2ECC71 : 0xFFE74C3C, 1);
    }
}
static void draw_mg_result_overlay(const GameContext *g) {
    int i, n = g->player_count, win = 0; char b[48];
    for (i = 0; i < n; ++i) if (g->players[i].mg_rank == 0) win = i;
    draw_mg_banner(g, "RESULTS");
    snprintf(b, sizeof(b), g->players[win].is_human ? "WINNER: PLAYER %d" : "WINNER: CPU %d", win + 1);
    draw_text_c(424, 48, b, 4, player_color(win), 1);
    for (i = 0; i < n; ++i) {
        float x, y, z, sx, sy; const Player *p = &g->players[i];
        stage_char_pos(g, i, &x, &y, &z);
        if (!render_project(g, x, 0.0f, 0.0f, &sx, &sy)) continue;
        (void)y; (void)z;
        draw_rect(sx - 78, 372, 156, 90, 0xE00B1020); draw_rect(sx - 78, 372, 156, 5, player_color(p->color));
        draw_text_c(sx, 382, RANK_NAMES[p->mg_rank & 3], 3, p->mg_rank == 0 ? 0xFFF1C40F : 0xFFFFFFFF, 1);
        snprintf(b, sizeof(b), "SCORE %d", p->mg_score); draw_text_c(sx, 408, b, 2, 0xFFFFFFFF, 0);
        snprintf(b, sizeof(b), "+%d COINS", g->contest_rewards[i]); draw_text_c(sx, 430, b, 2, 0xFFF39C12, 0);
        draw_label_at(g, i);
    }
    if (g->state_timer >= 30 && (((int)(g->anim_t * 3)) & 1)) draw_text_c(424, 480, "X: CONTINUE", 3, 0xFFFFFFFF, 1);
}
static void draw_setup_overlay(const GameContext *g) {
    static const char *LAB[SETUP_ROWS] = {"PLAYERS", "HUMANS", "TEAMS", "CPU LEVEL", "ROUNDS"};
    static const char *DIFFN[3] = {"EASY", "NORMAL", "HARD"};
    int row; char b[24];
    draw_rect(0, 0, SCR_W, SCR_H, 0xC00B1320);
    draw_text_c(424, 14, "GAME SETUP", 5, 0xFFFFFFFF, 1);
    draw_rect(36, 62, 420, 336, 0xFF162033);
    for (row = 0; row < SETUP_ROWS; ++row) {
        float y = 74 + row * 62; u32 bg = (row == g->setup_cursor) ? 0xFF2C3E50 : 0xFF1C2833;
        draw_rect(48, y, 396, 52, bg); if (row == g->setup_cursor) draw_rect(48, y, 7, 52, 0xFF3498DB);
        draw_text(68, y + 8, LAB[row], 2, 0xFFBDC3C7, 0);
        switch (row) {
        case 0: snprintf(b, sizeof(b), "%d", g->player_count); break;
        case 1: snprintf(b, sizeof(b), "%d", g->human_count); break;
        case 2: snprintf(b, sizeof(b), "%s", g->player_count == 4 ? (g->team_mode ? "ON  2 V 2" : "OFF") : "OFF  NEEDS 4"); break;
        case 3: snprintf(b, sizeof(b), "%s", DIFFN[(int)g->cpu_diff % 3]); break;
        default: snprintf(b, sizeof(b), "%d", g->max_rounds); break;
        }
        draw_text(68, y + 26, b, 3, row == g->setup_cursor ? 0xFFF1C40F : 0xFFFFFFFF, 1);
    }
    draw_minimap(g, 490, 74, 320, 240);
    draw_text_c(650, 326, g->board->name, 3, 0xFFFFFFFF, 1);
    draw_rect(60, 436, 728, 52, 0xFF243447);
    draw_text_c(424, 444, "TRIANGLE: NEXT ROW    O: CHANGE", 3, 0xFFFFFFFF, 1);
    if (((int)(g->anim_t * 3)) & 1) draw_text_c(424, 468, "X: CONTINUE", 3, 0xFF2ECC71, 1);
}
static void draw_pause_overlay(void) {
    draw_rect(0, 0, SCR_W, SCR_H, 0xAA0B1320);
    draw_rect(224, 150, 400, 210, 0xEE1A2332); draw_rect(232, 158, 384, 194, 0xFF243447);
    draw_text_c(424, 176, "PAUSED", 7, 0xFFFFFFFF, 1);
    draw_text_c(424, 246, "X OR START: RESUME", 3, 0xFF2ECC71, 1);
    draw_text_c(424, 288, "TRIANGLE: QUIT TO TITLE", 3, 0xFFE74C3C, 1);
}
static void draw_title_text(const GameContext *g) {
    float bob = sinf(g->anim_t * 2.5f) * 4.0f;
    draw_text_c(424, 40 + bob, "BOARD GAME", 12, 0xFFF1C40F, 1);
    draw_text_c(424, 120, "A PARTY FOR 1 TO 4 PLAYERS", 3, 0xFFFFFFFF, 1);
    if (((int)(g->anim_t * 2)) & 1) draw_text_c(424, 410, "PRESS X TO START", 5, 0xFF2ECC71, 1);
    draw_text_c(424, 470, "START: QUIT", 2, 0xFF8899AA, 0);
}
static void draw_results_text(const GameContext *g, const int rank[MAX_PLAYERS]) {
    int i, n = g->player_count, win = 0; char b[48];
    for (i = 0; i < n; ++i) if (rank[i] == 0) win = i;
    if (g->team_mode && n >= 4) {
        int ts[2] = {0, 0}, bw;
        for (i = 0; i < n; ++i) ts[g->players[i].team & 1] += g->players[i].stars;
        bw = ts[1] > ts[0] ? 1 : 0;
        if (ts[0] == ts[1]) snprintf(b, sizeof(b), "TEAMS TIE!"); else snprintf(b, sizeof(b), "TEAM %d WINS!", bw + 1);
    } else snprintf(b, sizeof(b), g->players[win].is_human ? "PLAYER %d WINS!" : "CPU %d WINS!", win + 1);
    draw_text_c(424, 30, "WINNER", 3, 0xFFFFFFFF, 1);
    draw_text_c(424, 56, b, 7, 0xFFF1C40F, 1);
    for (i = 0; i < n; ++i) {
        float sx, sy; const Player *p = &g->players[i];
        if (!render_project(g, STAGE_SLOT_X(i, n), 0.0f, 0.0f, &sx, &sy)) continue;
        draw_rect(sx - 78, 360, 156, 100, 0xE00B1020); draw_rect(sx - 78, 360, 156, 5, player_color(p->color));
        draw_text_c(sx, 370, RANK_NAMES[rank[i] & 3], 3, rank[i] == 0 ? 0xFFF1C40F : 0xFFFFFFFF, 1);
        snprintf(b, sizeof(b), "STARS %d", p->stars); draw_text_c(sx, 396, b, 2, 0xFFF1C40F, 0);
        snprintf(b, sizeof(b), "COINS %d", p->coins); draw_text_c(sx, 416, b, 2, 0xFFF39C12, 0);
        snprintf(b, sizeof(b), p->is_human ? "P%d" : "CPU %d", i + 1); draw_text_c(sx, 438, b, 2, player_color(p->color), 0);
    }
    if (((int)(g->anim_t * 2)) & 1) draw_text_c(424, 474, "X: TITLE    START: QUIT", 3, 0xFFFFFFFF, 1);
}
static void board_hints(const GameContext *g) {
    const Player *p = &g->players[g->current_player]; char b[48];
    switch (g->state) {
    case STATE_DICE_ROLL:
        if (p->is_human) { snprintf(b, sizeof(b), "PLAYER %d: X TO ROLL", g->current_player + 1); draw_hint(b); }
        else { snprintf(b, sizeof(b), "CPU %d IS ROLLING...", g->current_player + 1); draw_hint(b); }
        break;
    case STATE_ITEM: if (p->is_human) draw_hint("O: SELECT ITEM    X: USE / SKIP"); break;
    case STATE_BRANCH: draw_hint("O: CHOOSE PATH    X: GO THIS WAY"); break;
    case STATE_SHOP: break;
    case STATE_MOVE: case STATE_LAND: if (!p->is_human) { snprintf(b, sizeof(b), "CPU %d", g->current_player + 1); draw_hint(b); } break;
    default: break;
    }
}
static void draw_round_end(const GameContext *g) {
    char b[32]; snprintf(b, sizeof(b), "ROUND %d COMPLETE", g->round - 1);
    draw_rect(150, 190, 548, 120, 0xE00B1020);
    draw_text_c(424, 206, b, 5, 0xFFFFFFFF, 1);
    draw_text_c(424, 256, "MINIGAME TIME!", 4, 0xFFF1C40F, 1);
}

/* ------------------------------------------------------------------ */
/* frame                                                               */
/* ------------------------------------------------------------------ */
void game_render(GameContext *g) {
    GameState st = g->state; int rank[MAX_PLAYERS] = {0, 0, 0, 0};
    tiny3d_Clear(0xFF0B1320, TINY3D_CLEAR_ALL);
    tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);
    tiny3d_BlendFunc(1, (blend_src_func)(TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA),
                     (blend_dst_func)(TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_DST_ALPHA_ZERO),
                     (blend_func)(TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD));
    tiny3d_Project2D();
    cam_setup(g, &C);
    LIGHT = vnorm(v3(0.35f, 0.85f, 0.45f));
    nq = 0;
    if (st == STATE_PAUSE) st = g->pause_from;

    switch (st) {
    case STATE_TITLE:
        sky(0xFF0B1320, 0xFF16213A);
        draw_title_scene(g); world_flush();
        draw_title_text(g);
        break;
    case STATE_SETUP:
        sky(0xFF0B1320, 0xFF0B1320);
        draw_board_scene(g); world_flush();
        draw_setup_overlay(g);
        break;
    case STATE_BOARD_SELECT: {
        char b[48];
        sky(g->board_idx == 0 ? 0xFF6EC6F0 : (g->board_idx == 1 ? 0xFF140A24 : 0xFF0F1830), g->board_idx == 0 ? 0xFF2A9FD0 : (g->board_idx == 1 ? 0xFF2A1440 : 0xFF2A2048));
        draw_board_scene(g); world_flush();
        draw_rect(0, 0, SCR_W, 70, 0xE00A0E14);
        draw_text_c(424, 8, "CHOOSE A BOARD", 3, 0xFFBDC3C7, 0);
        snprintf(b, sizeof(b), "%s", g->board->name); draw_text_c(424, 30, b, 5, 0xFFFFFFFF, 1);
        draw_minimap(g, 640, 90, 180, 130);
        draw_hint("O: CHANGE BOARD   X: START   SQ: BACK");
        break; }
    case STATE_MG_INTRO: case STATE_MG_PLAY: case STATE_MG_RESULT: {
        u32 mc = mg_color(g->mg_type);
        sky(col_scale(mc, 0.30f), col_scale(mc, 0.16f));
        draw_stage_base(g, 0xFF1B2A4A, mc);
        draw_stage_chars(g); draw_mg_props(g); world_flush();
        if (st == STATE_MG_INTRO) draw_mg_intro_overlay(g);
        else if (st == STATE_MG_PLAY) draw_mg_play_overlay(g);
        else draw_mg_result_overlay(g);
        draw_fx(g);
        break; }
    case STATE_RESULTS:
        final_ranks(g, rank);
        sky(0xFF1A0E30, 0xFF2A1A4A);
        draw_results_scene(g, rank); world_flush();
        draw_results_text(g, rank);
        draw_fx(g);
        break;
    default:
        sky(g->board_idx == 0 ? 0xFF6EC6F0 : (g->board_idx == 1 ? 0xFF140A24 : 0xFF0F1830), g->board_idx == 0 ? 0xFF2A9FD0 : (g->board_idx == 1 ? 0xFF2A1440 : 0xFF2A2048));
        draw_board_scene(g); world_flush();
        draw_fx(g); draw_hud(g); draw_dice(g); draw_popup(g);
        if (st == STATE_ITEM && g->players[g->current_player].is_human) draw_item_panel(g);
        if (st == STATE_SHOP && g->players[g->current_player].is_human) draw_shop_panel(g);
        if (st == STATE_ROUND_END) draw_round_end(g);
        board_hints(g);
        break;
    }
    if (g->state == STATE_PAUSE) draw_pause_overlay();
    tiny3d_Flip();
}

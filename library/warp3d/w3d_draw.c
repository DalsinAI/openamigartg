/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library: the drawing calls. Vertices become the stream's fixed
 * point (positions and texture coordinates 16.16, colours ARGB32, z as 32
 * bits, w as it is), back faces are culled here, and points and lines are
 * drawn as small quads, so the 3D core only ever sees triangles. */
#include <proto/exec.h>

#include "w3d_internal.h"
#include "w3d_calls.h"

/* e^x, for fog: the 68040 has no exponential instruction. */
static float fexp(float v)
{
    float r = 1.0f, term = 1.0f;
    int i, k = 0;
    if (v < -30.0f) return 0.0f;
    while (v > 0.5f || v < -0.5f) { v *= 0.5f; k++; }
    for (i = 1; i < 10; i++) { term *= v / (float)i; r += term; }
    while (k--) r *= r;
    return r;
}

static ULONG clampf255(float v)
{
    LONG c = w3d_f2l(v * 255.0f);
    return c < 0 ? 0 : c > 255 ? 255 : (ULONG)c;
}

/* The fog factor a vertex carries when fog is worked out per vertex
 * (W3D_FOG_INTERPOLATED, version 5's Z fog and fog coordinates); 255 = none. */
static ULONG vertex_fog(struct w3dctx *x, const W3D_Vertex *v, float fogcoord, int have_coord)
{
    W3D_Context *c = x->ctx;
    float f = 1.0f, d;
    if (!(c->state & W3D_FOGGING)) return 255;
    if (x->fogmode_v5 >= W3D_FOG_Z_LINEAR && x->fogmode_v5 <= W3D_FOG_Z_EXP_2) {
        d = have_coord ? fogcoord : (float)v->z;
        switch (x->fogmode_v5) {
        case W3D_FOG_Z_LINEAR:
            f = x->zfog_end == x->zfog_start ? 1.0f : (x->zfog_end - d) / (x->zfog_end - x->zfog_start);
            break;
        case W3D_FOG_Z_EXP: f = fexp(-x->zfog_density * d); break;
        default: f = fexp(-(x->zfog_density * d) * (x->zfog_density * d));
        }
    } else if (x->fogmode_v5 < 0 && x->fogmode == W3D_FOG_INTERPOLATED) {
        float s = c->fog.fog_start, e = c->fog.fog_end;
        f = s == e ? 1.0f : (v->w - e) / (s - e);
    } else if (have_coord) {
        f = fogcoord;                           /* W3D_FOG_COORD: a factor the program worked out */
    } else return 255;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return clampf255(f);
}

/* One Warp3D vertex in the stream's layout. tw, th: the texture's size, or
 * 0 when u and v are already 0..1. */
static void convert(struct w3dctx *x, const W3D_Vertex *v, float su, float sv, struct ovtx *o, float fogc, int have_fogc)
{
    W3D_Context *c = x->ctx;
    double z = v->z;
    float w = v->w;
    o->x = w3d_f2l(v->x * 65536.0f);
    o->y = w3d_f2l(v->y * 65536.0f);
    o->argb = (c->state & W3D_GOURAUD) ? w3d_colour(&v->color) : w3d_colour(&x->current);
    o->u = w3d_f2l(v->u * su);
    o->v = w3d_f2l(v->v * sv);
    o->z = z <= 0.0 ? 0 : z >= 1.0 ? 0xFFFFFFFFUL : (ULONG)w3d_f2l((float)z * 1073741823.0f) << 2;
    if (!(w > 0.0f)) w = 1e-6f;                 /* w must be positive */
    o->w = w3d_fbits(w);
    if (W3DBase->trace > 100000)
        w3d_trace("  v %ld %ld z %lx w %lx u %ld v %ld argb %lx", (long)o->x >> 16, (long)o->y >> 16, (unsigned long)o->z,
                  (unsigned long)o->w, (long)o->u, (long)o->v, (unsigned long)o->argb);
    o->spec = (vertex_fog(x, v, fogc, have_fogc) << 24)
            | ((c->state & W3D_SPECULAR) ? (clampf255(v->spec.r) << 16) | (clampf255(v->spec.g) << 8) | clampf255(v->spec.b) : 0);
}

void w3d_vertex(struct w3dctx *x, const W3D_Vertex *v, struct w3dtex *t, struct ovtx *o)
{
    convert(x, v, t ? 65536.0f / (float)t->w : 0.0f, t ? 65536.0f / (float)t->h : 0.0f, o, 0.0f, 0);
}

/* Front-facing, by the winding SetFrontFace gave (y runs down the screen). */
static int culled(struct w3dctx *x, const struct ovtx *a, const struct ovtx *b, const struct ovtx *c)
{
    W3D_Context *ctx = x->ctx;
    float area;
    if (!(ctx->state & W3D_CULLFACE)) return 0;
    area = ((float)(b->x - a->x)) * ((float)(c->y - a->y)) - ((float)(c->x - a->x)) * ((float)(b->y - a->y));
    return ctx->FrontFaceOrder == W3D_CW ? area < 0.0f : area > 0.0f;
}

/* The texture a primitive names, when texture mapping is on and the
 * texture is one of this context's: a pointer a program left stale or
 * never set draws untextured instead of reading wild memory. */
static W3D_Texture *use_tex(struct w3dctx *x, W3D_Texture *tex)
{
    struct Node *n;
    if (!tex || !(x->ctx->state & W3D_TEXMAPPING)) return 0;
    if (tex == x->known_tex) return tex;
    for (n = (struct Node *)x->ctx->tex.mlh_Head; n->ln_Succ; n = n->ln_Succ)
        if (n == &tex->link) { x->known_tex = tex; return tex; }
    w3d_trace("not a texture of this context: %lx", (unsigned long)tex);
    return 0;
}

/* The polygon stipple of a primitive (W3D_POLYGON_STIPPLE), kept in the
 * context's own copy. */
static void stipple(struct w3dctx *x, const unsigned char *pat)
{
    if (!pat || !(x->ctx->state & W3D_POLYGON_STIPPLE)) return;
    if (x->stipple) {
        int i, same = 1;
        for (i = 0; i < 128; i++) if (x->stipple[i] != pat[i]) { same = 0; break; }
        if (same) return;
        w3d_flush(x);
    } else if (!(x->stipple = w3d_alloc(128))) return;
    w3d_copy(x->stipple, pat, 128);
    x->r3d_dirty = 1;
}

/* Vertex i of a primitive: from an array, or through an array of pointers.
 * A fan's later pieces start again from its first vertex (first). */
struct vsrc {
    const W3D_Vertex *v;
    W3D_Vertex *const *pv;
    ULONG first, at;                            /* vertex 0 is `first`, vertex j > 0 is at + j */
};
static const W3D_Vertex *vget(const struct vsrc *s, ULONG j)
{
    ULONG i = j ? s->at + j : s->first;
    return s->pv ? s->pv[i] : &s->v[i];
}

/* n vertices drawn as a list, strip or fan. With face culling on, strips
 * and fans become lists of the triangles that face front. */
static ULONG draw(struct w3dctx *x, W3D_Texture *tex, const struct vsrc *src, ULONG n, int prim, const unsigned char *pat)
{
    struct ovtx *o;
    struct w3dtex *t;
    ULONG i, m;
    int cull = (x->ctx->state & W3D_CULLFACE) != 0;
    if (n < 3) return W3D_ILLEGALINPUT;
    tex = use_tex(x, tex);
    t = tex ? TEX(tex) : 0;
    w3d_trace("draw prim %ld n %ld tex %lx state %lx", (long)prim, (long)n, (unsigned long)tex, (unsigned long)x->ctx->state);
    stipple(x, pat);
    m = cull && prim != OGPU_LAY_LIST ? (n - 2) * 3 : n;
    o = w3d_vspace(x, m);
    if (!o) return W3D_QUEUEFAILED;
    w3d_select_texture(x, tex);
    if (!cull || prim == OGPU_LAY_LIST) {
        for (i = 0; i < n; i++) w3d_vertex(x, vget(src, i), t, &o[i]);
        if (cull) {                             /* a list: keep the front faces */
            ULONG k = 0;
            for (i = 0; i + 2 < n; i += 3)
                if (!culled(x, &o[i], &o[i + 1], &o[i + 2])) {
                    if (k != i) { o[k] = o[i]; o[k + 1] = o[i + 1]; o[k + 2] = o[i + 2]; }
                    k += 3;
                }
            x->vused -= (n - k) * sizeof(struct ovtx);
            m = k;
        }
        if (m) w3d_emit(x, o, m, prim);
    } else {
        struct ovtx a, b, c;
        ULONG k = 0;
        w3d_vertex(x, vget(src, 0), t, &a);
        w3d_vertex(x, vget(src, 1), t, &b);
        for (i = 2; i < n; i++) {
            const struct ovtx *p0, *p1;
            w3d_vertex(x, vget(src, i), t, &c);
            if (prim == OGPU_LAY_STRIP && (i & 1)) { p0 = &b; p1 = &a; }  /* odd strip triangles turn the other way */
            else { p0 = &a; p1 = &b; }
            if (!culled(x, p0, p1, &c)) { o[k] = *p0; o[k + 1] = *p1; o[k + 2] = c; k += 3; }
            if (prim == OGPU_LAY_FAN) b = c;
            else { a = b; b = c; }
        }
        x->vused -= (m - k) * sizeof(struct ovtx);
        if (k) w3d_emit(x, o, k, OGPU_LAY_LIST);
    }
    w3d_maybe_flush(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_DrawTriangle(REG(a0, W3D_Context *c), REG(a1, W3D_Triangle *tri), REG(a6, struct W3DBase *libbase))
{
    W3D_Vertex *const pv[3] = { &tri->v1, &tri->v2, &tri->v3 };
    struct vsrc s;
    s.v = 0; s.pv = pv; s.first = 0; s.at = 0;
    return draw(CTX(c), tri->tex, &s, 3, OGPU_LAY_LIST, tri->st_pattern);
}

LIBCALL ULONG LIB_W3D_DrawTriangleV(REG(a0, W3D_Context *c), REG(a1, W3D_TriangleV *tri), REG(a6, struct W3DBase *libbase))
{
    W3D_Vertex *const pv[3] = { tri->v1, tri->v2, tri->v3 };
    struct vsrc s;
    s.v = 0; s.pv = pv; s.first = 0; s.at = 0;
    return draw(CTX(c), tri->tex, &s, 3, OGPU_LAY_LIST, tri->st_pattern);
}

/* Fans and strips, in pieces of up to 4096 vertices that share their edges
 * (an even number of triangles a piece, so a strip keeps its turning). */
static ULONG draw_array(struct w3dctx *x, W3D_Texture *tex, const W3D_Vertex *v, W3D_Vertex *const *pv, ULONG n, int prim,
                        const unsigned char *pat)
{
    struct vsrc s;
    ULONG done = 0, r = W3D_SUCCESS;
    if (n < 3) return W3D_ILLEGALINPUT;
    s.v = v; s.pv = pv; s.first = 0;
    while (r == W3D_SUCCESS) {
        ULONG take = n - done > 4096 ? 4096 : n - done;
        s.at = done;
        if (prim == OGPU_LAY_STRIP || !done) s.first = done;
        r = draw(x, tex, &s, take, prim, pat);
        if (done + take >= n) break;
        done += take - 2;
    }
    return r;
}

LIBCALL ULONG LIB_W3D_DrawTriFan(REG(a0, W3D_Context *c), REG(a1, W3D_Triangles *t), REG(a6, struct W3DBase *libbase))
{
    return draw_array(CTX(c), t->tex, t->v, 0, (ULONG)t->vertexcount, OGPU_LAY_FAN, t->st_pattern);
}

LIBCALL ULONG LIB_W3D_DrawTriStrip(REG(a0, W3D_Context *c), REG(a1, W3D_Triangles *t), REG(a6, struct W3DBase *libbase))
{
    return draw_array(CTX(c), t->tex, t->v, 0, (ULONG)t->vertexcount, OGPU_LAY_STRIP, t->st_pattern);
}

LIBCALL ULONG LIB_W3D_DrawTriFanV(REG(a0, W3D_Context *c), REG(a1, W3D_TrianglesV *t), REG(a6, struct W3DBase *libbase))
{
    return draw_array(CTX(c), t->tex, 0, t->v, (ULONG)t->vertexcount, OGPU_LAY_FAN, t->st_pattern);
}

LIBCALL ULONG LIB_W3D_DrawTriStripV(REG(a0, W3D_Context *c), REG(a1, W3D_TrianglesV *t), REG(a6, struct W3DBase *libbase))
{
    return draw_array(CTX(c), t->tex, 0, t->v, (ULONG)t->vertexcount, OGPU_LAY_STRIP, t->st_pattern);
}

/* ---- points and lines -------------------------------------------------------------------- */

/* A quad from four converted corners, as two triangles. */
static ULONG quad(struct w3dctx *x, W3D_Texture *tex, struct ovtx *q)
{
    struct ovtx *o = w3d_vspace(x, 6);
    if (!o) return W3D_QUEUEFAILED;
    w3d_select_texture(x, tex);
    o[0] = q[0]; o[1] = q[1]; o[2] = q[2];
    o[3] = q[2]; o[4] = q[1]; o[5] = q[3];
    w3d_emit(x, o, 6, OGPU_LAY_LIST);
    return W3D_SUCCESS;
}

static ULONG point(struct w3dctx *x, const W3D_Vertex *v, W3D_Texture *tex, float size)
{
    struct ovtx q[4];
    LONG h;
    int i;
    tex = use_tex(x, tex);
    if (size < 1.0f) size = 1.0f;
    w3d_vertex(x, v, tex ? TEX(tex) : 0, &q[0]);
    h = w3d_f2l(size * 32768.0f);               /* half the size, 16.16 */
    for (i = 1; i < 4; i++) q[i] = q[0];
    q[0].x -= h; q[0].y -= h;
    q[1].x += h; q[1].y -= h;
    q[2].x -= h; q[2].y += h;
    q[3].x += h; q[3].y += h;
    return quad(x, tex, q);
}

LIBCALL ULONG LIB_W3D_DrawPoint(REG(a0, W3D_Context *c), REG(a1, W3D_Point *p), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    float size = p->pointsize != 1.0f ? p->pointsize : x->point_size;
    ULONG r = point(x, &p->v1, p->tex, size);
    w3d_maybe_flush(x);
    return r;
}

/* A line of the given width between a and b (converted), as a quad. */
static ULONG segment(struct w3dctx *x, W3D_Texture *tex, const struct ovtx *a, const struct ovtx *b, float width)
{
    struct ovtx q[4];
    float dx = (float)(b->x - a->x), dy = (float)(b->y - a->y), len2 = dx * dx + dy * dy, nx, ny, s;
    LONG ox, oy;
    if (len2 <= 0.0f) return W3D_SUCCESS;
    /* half the width across the line, in 16.16: (-dy, dx) / length * width / 2 */
    s = width * 0.5f * 65536.0f;
    {
        /* 1 / length by Newton's method from 1 / the longer side (the 68040 has no square root to spare) */
        float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, r = 1.0f / (ax > ay ? ax : ay);
        int i;
        for (i = 0; i < 5; i++) r = r * (1.5f - 0.5f * len2 * r * r);
        nx = -dy * r * s; ny = dx * r * s;
    }
    ox = w3d_f2l(nx); oy = w3d_f2l(ny);
    q[0] = *a; q[1] = *a; q[2] = *b; q[3] = *b;
    q[0].x += ox; q[0].y += oy; q[1].x -= ox; q[1].y -= oy;
    q[2].x += ox; q[2].y += oy; q[3].x -= ox; q[3].y -= oy;
    return quad(x, tex, q);
}

/* One line, stippled when asked: 16 pattern bits, each `factor` pixels long, low bit first. */
static ULONG line(struct w3dctx *x, const W3D_Vertex *v1, const W3D_Vertex *v2, W3D_Texture *tex, float width,
                  int stip, UWORD pattern, int factor)
{
    struct ovtx a, b;
    struct w3dtex *t;
    tex = use_tex(x, tex);
    t = tex ? TEX(tex) : 0;
    if (width < 1.0f) width = 1.0f;
    w3d_vertex(x, v1, t, &a);
    w3d_vertex(x, v2, t, &b);
    if (!stip || pattern == 0xFFFF) return segment(x, tex, &a, &b, width);
    {
        float dx = (v2->x - v1->x), dy = (v2->y - v1->y);
        float len = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        LONG n = w3d_f2l(len), i;
        if (factor < 1) factor = 1;
        for (i = 0; i < n; i += factor) {
            if ((pattern >> ((i / factor) & 15)) & 1) {
                W3D_Vertex s = *v1, e = *v1;
                float f0 = (float)i / len, f1 = (float)(i + factor) / len;
                if (f1 > 1.0f) f1 = 1.0f;
                s.x = v1->x + dx * f0; s.y = v1->y + dy * f0; s.u = v1->u + (v2->u - v1->u) * f0; s.v = v1->v + (v2->v - v1->v) * f0;
                s.z = v1->z + (v2->z - v1->z) * f0; s.w = v1->w + (v2->w - v1->w) * f0;
                e.x = v1->x + dx * f1; e.y = v1->y + dy * f1; e.u = v1->u + (v2->u - v1->u) * f1; e.v = v1->v + (v2->v - v1->v) * f1;
                e.z = v1->z + (v2->z - v1->z) * f1; e.w = v1->w + (v2->w - v1->w) * f1;
                w3d_vertex(x, &s, t, &a);
                w3d_vertex(x, &e, t, &b);
                if (segment(x, tex, &a, &b, width) != W3D_SUCCESS) return W3D_QUEUEFAILED;
            }
        }
    }
    return W3D_SUCCESS;
}

static int stippled(struct w3dctx *x, W3D_Bool st_enable)
{
    return st_enable || (x->ctx->state & W3D_LINE_STIPPLE);
}

LIBCALL ULONG LIB_W3D_DrawLine(REG(a0, W3D_Context *c), REG(a1, W3D_Line *l), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG r = line(x, &l->v1, &l->v2, l->tex, l->linewidth != 1.0f ? l->linewidth : x->line_width, stippled(x, l->st_enable),
                   l->st_enable ? l->st_pattern : x->line_pattern, l->st_enable ? l->st_factor : x->line_factor);
    w3d_maybe_flush(x);
    return r;
}

static ULONG lines(struct w3dctx *x, W3D_Lines *l, int loop)
{
    int i, n = l->vertexcount;
    ULONG r = W3D_SUCCESS;
    if (n < 2) return W3D_ILLEGALINPUT;
    for (i = 0; i + 1 < n && r == W3D_SUCCESS; i++)
        r = line(x, &l->v[i], &l->v[i + 1], l->tex, l->linewidth, stippled(x, l->st_enable),
                 l->st_enable ? l->st_pattern : x->line_pattern, l->st_enable ? l->st_factor : x->line_factor);
    if (loop && n > 2 && r == W3D_SUCCESS)
        r = line(x, &l->v[n - 1], &l->v[0], l->tex, l->linewidth, stippled(x, l->st_enable),
                 l->st_enable ? l->st_pattern : x->line_pattern, l->st_enable ? l->st_factor : x->line_factor);
    w3d_maybe_flush(x);
    return r;
}

LIBCALL ULONG LIB_W3D_DrawLineStrip(REG(a0, W3D_Context *c), REG(a1, W3D_Lines *l), REG(a6, struct W3DBase *libbase))
{
    return lines(CTX(c), l, 0);
}

LIBCALL ULONG LIB_W3D_DrawLineLoop(REG(a0, W3D_Context *c), REG(a1, W3D_Lines *l), REG(a6, struct W3DBase *libbase))
{
    return lines(CTX(c), l, 1);
}

/* ---- vertex arrays (version 4 and 5) -------------------------------------------------- */

LIBCALL ULONG LIB_W3D_VertexPointer(REG(a0, W3D_Context *c), REG(a1, void *p), REG(d0, int stride), REG(d1, ULONG mode),
                                    REG(d2, ULONG flags), REG(a6, struct W3DBase *libbase))
{
    if (mode > W3D_VERTEX_D_D_D) return W3D_ILLEGALINPUT;
    c->VertexPointer = p; c->VPStride = stride; c->VPMode = mode; c->VPFlags = flags;
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_TexCoordPointer(REG(a0, W3D_Context *c), REG(a1, void *p), REG(d0, int stride), REG(d1, int unit),
                                      REG(d2, int off_v), REG(d3, int off_w), REG(d4, ULONG flags), REG(a6, struct W3DBase *libbase))
{
    if (unit < 0 || unit >= W3D_MAX_TMU) return W3D_ILLEGALINPUT;
    c->TexCoordPointer[unit] = p; c->TPStride[unit] = stride;
    c->TPVOffs[unit] = off_v; c->TPWOffs[unit] = off_w; c->TPFlags[unit] = (int)flags;
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_ColorPointer(REG(a0, W3D_Context *c), REG(a1, void *p), REG(d0, int stride), REG(d1, ULONG format),
                                   REG(d2, ULONG mode), REG(d3, ULONG flags), REG(a6, struct W3DBase *libbase))
{
    c->ColorPointer = p; c->CPStride = stride; c->CPMode = format | mode; c->CPFlags = flags;
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SecondaryColorPointer(REG(a0, W3D_Context *c), REG(a1, void *p), REG(d0, int stride), REG(d1, ULONG format),
                                            REG(d2, ULONG mode), REG(d3, ULONG flags), REG(a6, struct W3DBase *libbase))
{
    c->SecondaryColorPointer = p; c->SCPStride = stride; c->SCPMode = format | mode; c->SCPFlags = flags;
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_FogCoordPointer(REG(a0, W3D_Context *c), REG(a1, void *p), REG(d0, int stride), REG(d1, ULONG mode),
                                      REG(d2, ULONG flags), REG(a6, struct W3DBase *libbase))
{
    if (mode > W3D_FOGCOORD_DOUBLE) return W3D_ILLEGALINPUT;
    c->FogCoordPointer = p; c->FCPStride = (ULONG)stride; c->FCPMode = mode; c->FCPFlags = flags;
    return W3D_SUCCESS;
}

/* All the pointers at once from one interleaved array: x, y, z (floats), then
 * the fields the format names, in the order of its bits. */
LIBCALL ULONG LIB_W3D_InterleavedArray(REG(a0, W3D_Context *c), REG(a1, void *p), REG(d0, int stride), REG(d1, ULONG format),
                                       REG(d2, ULONG flags), REG(a6, struct W3DBase *libbase))
{
    UBYTE *at = (UBYTE *)p + 12;
    int u;
    c->VertexPointer = p; c->VPStride = stride; c->VPMode = W3D_VERTEX_F_F_F; c->VPFlags = 0;
    c->FogCoordPointer = 0; c->ColorPointer = 0; c->SecondaryColorPointer = 0;
    if (format & (W3D_VFORMAT_FOG | W3D_VFORMAT_FOG_FACTOR)) {
        c->FogCoordPointer = at; c->FCPStride = (ULONG)stride; c->FCPMode = W3D_FOGCOORD_FLOAT; at += 4;
    }
    if (format & W3D_VFORMAT_COLOR) { c->ColorPointer = at; c->CPStride = stride; c->CPMode = W3D_COLOR_FLOAT | W3D_CMODE_RGBA; at += 16; }
    else if (format & W3D_VFORMAT_PACK_COLOR) { c->ColorPointer = at; c->CPStride = stride; c->CPMode = W3D_COLOR_UBYTE | W3D_CMODE_RGBA; at += 4; }
    if (format & W3D_VFORMAT_SCOLOR) { c->SecondaryColorPointer = at; c->SCPStride = stride; c->SCPMode = W3D_COLOR_FLOAT | W3D_CMODE_RGBA; at += 16; }
    else if (format & W3D_VFORMAT_PACK_SCOLOR) { c->SecondaryColorPointer = at; c->SCPStride = stride; c->SCPMode = W3D_COLOR_UBYTE | W3D_CMODE_RGBA; at += 4; }
    for (u = 0; u < 16; u++)
        if (format & (W3D_VFORMAT_TCOORD_0 << u)) {
            c->TexCoordPointer[u] = at; c->TPStride[u] = stride; c->TPVOffs[u] = 4; c->TPWOffs[u] = 8;
            c->TPFlags[u] = (int)(flags & W3D_TEXCOORD_NORMALIZED);
            at += 12;
        } else c->TexCoordPointer[u] = 0;
    return W3D_SUCCESS;
}

static float getf(const UBYTE *p, int dbl) { return dbl ? (float)*(const double *)p : *(const float *)p; }

static void colour_at(const UBYTE *p, ULONG mode, W3D_Color *o)
{
    float ch[4];
    int i, n = (mode & (W3D_CMODE_RGB | W3D_CMODE_BGR)) ? 3 : 4;
    for (i = 0; i < n; i++) ch[i] = (mode & W3D_COLOR_UBYTE) ? p[i] / 255.0f : ((const float *)p)[i];
    o->a = 1.0f;
    if (mode & W3D_CMODE_RGB) { o->r = ch[0]; o->g = ch[1]; o->b = ch[2]; }
    else if (mode & W3D_CMODE_BGR) { o->b = ch[0]; o->g = ch[1]; o->r = ch[2]; }
    else if (mode & W3D_CMODE_ARGB) { o->a = ch[0]; o->r = ch[1]; o->g = ch[2]; o->b = ch[3]; }
    else if (mode & W3D_CMODE_BGRA) { o->b = ch[0]; o->g = ch[1]; o->r = ch[2]; o->a = ch[3]; }
    else { o->r = ch[0]; o->g = ch[1]; o->b = ch[2]; o->a = ch[3]; }
}

/* Vertex i of the arrays, converted. */
static void array_vertex(struct w3dctx *x, ULONG i, struct w3dtex *t, struct ovtx *o)
{
    W3D_Context *c = x->ctx;
    W3D_Vertex v;
    const UBYTE *p = (const UBYTE *)c->VertexPointer + (LONG)i * c->VPStride;
    float su, sv, fogc = 0.0f;
    int have_fogc = 0;
    switch (c->VPMode) {
    case W3D_VERTEX_F_F_F: v.x = getf(p, 0); v.y = getf(p + 4, 0); v.z = getf(p + 8, 0); break;
    case W3D_VERTEX_F_F_D: v.x = getf(p, 0); v.y = getf(p + 4, 0); v.z = *(const double *)(p + 8); break;
    default: v.x = getf(p, 1); v.y = getf(p + 8, 1); v.z = *(const double *)(p + 16); break;
    }
    v.u = v.v = 0.0f; v.w = 1.0f; v.tex3d = 0.0f; v.l = 0.0f;
    if (c->TexCoordPointer[0]) {
        const UBYTE *q = (const UBYTE *)c->TexCoordPointer[0] + (LONG)i * c->TPStride[0];
        v.u = *(const float *)q;
        v.v = *(const float *)(q + c->TPVOffs[0]);
        v.w = *(const float *)(q + c->TPWOffs[0]);      /* off_w may be negative: w before u, as MiniGL keeps it */
    }
    if (c->ColorPointer) colour_at((const UBYTE *)c->ColorPointer + (LONG)i * c->CPStride, c->CPMode, &v.color);
    else v.color = x->current;
    v.spec.r = v.spec.g = v.spec.b = 0.0f;
    if (c->SecondaryColorPointer) {
        W3D_Color s;
        colour_at((const UBYTE *)c->SecondaryColorPointer + (LONG)i * c->SCPStride, c->SCPMode, &s);
        v.spec.r = s.r; v.spec.g = s.g; v.spec.b = s.b;
    }
    if (c->FogCoordPointer && (c->state & W3D_FOG_COORD)) {
        fogc = getf((const UBYTE *)c->FogCoordPointer + i * c->FCPStride, c->FCPMode == W3D_FOGCOORD_DOUBLE);
        have_fogc = 1;
    }
    if (!t) su = sv = 0.0f;
    else if (c->TPFlags[0] & W3D_TEXCOORD_NORMALIZED) su = sv = 65536.0f;
    else { su = 65536.0f / (float)t->w; sv = 65536.0f / (float)t->h; }
    convert(x, &v, su, sv, o, fogc, have_fogc);
}

static ULONG index_at(const void *ix, ULONG type, ULONG i)
{
    if (!ix) return i;
    if (type == W3D_INDEX_UBYTE) return ((const UBYTE *)ix)[i];
    if (type == W3D_INDEX_UWORD) return ((const UWORD *)ix)[i];
    return ((const ULONG *)ix)[i];
}

/* DrawArray and DrawElements: triangles as lists (culled here), points and lines as quads. */
static ULONG arrays(struct w3dctx *x, ULONG prim, ULONG base, ULONG count, ULONG type, const void *ix)
{
    W3D_Context *c = x->ctx;
    W3D_Texture *tex = use_tex(x, c->CurrentTex[0]);
    struct w3dtex *t = tex ? TEX(tex) : 0;
    ULONG i;
    if (!c->VertexPointer) return W3D_ILLEGALINPUT;
    w3d_trace("arrays prim %ld base %ld count %ld type %ld idx %lx vp %lx stride %ld mode %ld cp %lx cmode %lx tp %lx tstride %ld voff %ld woff %ld tflags %ld tex %lx",
              (long)prim, (long)base, (long)count, (long)type, (unsigned long)ix, (unsigned long)c->VertexPointer, (long)c->VPStride,
              (long)c->VPMode, (unsigned long)c->ColorPointer, (unsigned long)c->CPMode, (unsigned long)c->TexCoordPointer[0],
              (long)c->TPStride[0], (long)c->TPVOffs[0], (long)c->TPWOffs[0], (long)c->TPFlags[0], (unsigned long)tex);
    switch (prim) {
    case W3D_PRIMITIVE_TRIANGLES: case W3D_PRIMITIVE_TRIFAN: case W3D_PRIMITIVE_TRISTRIP: {
        ULONG ntri = prim == W3D_PRIMITIVE_TRIANGLES ? count / 3 : count >= 3 ? count - 2 : 0, k = 0, done = 0;
        struct ovtx a, b, cc;
        if (!ntri) return W3D_ILLEGALINPUT;
        while (done < ntri) {
            ULONG chunk = ntri - done > 2048 ? 2048 : ntri - done, j;
            struct ovtx *o = w3d_vspace(x, chunk * 3);
            if (!o) return W3D_QUEUEFAILED;
            w3d_select_texture(x, tex);
            k = 0;
            for (j = 0; j < chunk; j++) {
                ULONG tnum = done + j, i0, i1, i2;
                if (prim == W3D_PRIMITIVE_TRIANGLES) { i0 = tnum * 3; i1 = i0 + 1; i2 = i0 + 2; }
                else if (prim == W3D_PRIMITIVE_TRIFAN) { i0 = 0; i1 = tnum + 1; i2 = tnum + 2; }
                else if (tnum & 1) { i0 = tnum + 1; i1 = tnum; i2 = tnum + 2; }
                else { i0 = tnum; i1 = tnum + 1; i2 = tnum + 2; }
                array_vertex(x, base + index_at(ix, type, i0), t, &a);
                array_vertex(x, base + index_at(ix, type, i1), t, &b);
                array_vertex(x, base + index_at(ix, type, i2), t, &cc);
                if (culled(x, &a, &b, &cc)) continue;
                o[k] = a; o[k + 1] = b; o[k + 2] = cc; k += 3;
            }
            if (k) w3d_emit(x, o, k, OGPU_LAY_LIST);
            x->vused -= (chunk * 3 - k) * sizeof(struct ovtx);
            done += chunk;
        }
        break;
    }
    case W3D_PRIMITIVE_POINTS:
        for (i = 0; i < count; i++) {
            struct ovtx q[4];
            LONG h = w3d_f2l((x->point_size < 1.0f ? 1.0f : x->point_size) * 32768.0f);
            array_vertex(x, base + index_at(ix, type, i), t, &q[0]);
            q[1] = q[2] = q[3] = q[0];
            q[0].x -= h; q[0].y -= h; q[1].x += h; q[1].y -= h; q[2].x -= h; q[2].y += h; q[3].x += h; q[3].y += h;
            if (quad(x, tex, q) != W3D_SUCCESS) return W3D_QUEUEFAILED;
        }
        break;
    case W3D_PRIMITIVE_LINES: case W3D_PRIMITIVE_LINESTRIP: case W3D_PRIMITIVE_LINELOOP: {
        ULONG n = prim == W3D_PRIMITIVE_LINES ? count / 2 : prim == W3D_PRIMITIVE_LINESTRIP ? (count ? count - 1 : 0) : count;
        for (i = 0; i < n; i++) {
            struct ovtx a, b;
            ULONG i0 = prim == W3D_PRIMITIVE_LINES ? i * 2 : i, i1 = prim == W3D_PRIMITIVE_LINES ? i * 2 + 1 : (i + 1) % count;
            array_vertex(x, base + index_at(ix, type, i0), t, &a);
            array_vertex(x, base + index_at(ix, type, i1), t, &b);
            if (segment(x, tex, &a, &b, x->line_width < 1.0f ? 1.0f : x->line_width) != W3D_SUCCESS) return W3D_QUEUEFAILED;
        }
        break;
    }
    default:
        return W3D_ILLEGALINPUT;
    }
    w3d_maybe_flush(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_DrawArray(REG(a0, W3D_Context *c), REG(d0, ULONG prim), REG(d1, ULONG base), REG(d2, ULONG count),
                                REG(a6, struct W3DBase *libbase))
{
    return arrays(CTX(c), prim, base, count, 0, 0);
}

LIBCALL ULONG LIB_W3D_DrawElements(REG(a0, W3D_Context *c), REG(d0, ULONG prim), REG(d1, ULONG type), REG(d2, ULONG count),
                                   REG(a1, void *indices), REG(a6, struct W3DBase *libbase))
{
    if (!indices || type > W3D_INDEX_ULONG) return W3D_ILLEGALINPUT;
    return arrays(CTX(c), prim, 0, count, type, indices);
}

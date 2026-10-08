/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: vertices. glBegin/glEnd and the current values, vertex
 * arrays (glDrawArrays, glDrawElements, glArrayElement, interleaved and
 * locked arrays), and the assembly of points, lines, triangles, strips,
 * fans, quads and polygons from processed vertices (mgl_pipe.c draws them).
 *
 * Between glBegin and glEnd vertices are processed as they come and kept
 * (up to MGL_VB); the primitive is assembled at glEnd, or in part when the
 * buffer fills, keeping what the rest of it needs. An array draw processes
 * each vertex it uses once and assembles from those. */
#include "mgl_internal.h"

/* ---- assembly -------------------------------------------------------------------------------- */

#define V(k) (b + (ix ? ix[k] : (ULONG)(k)))

static void quad4(GLcontext c, struct mgl_pv *p0, struct mgl_pv *p1, struct mgl_pv *p2, struct mgl_pv *p3,
                  struct mgl_pv *prov, int strip)
{
    struct mgl_pv *v[4];
    UBYTE e[4];
    v[0] = p0; v[1] = p1; v[2] = p2; v[3] = p3;
    if (strip) { e[0] = 1; e[1] = 1; e[2] = 1; e[3] = 1; }
    else { e[0] = p0->edge; e[1] = p1->edge; e[2] = p2->edge; e[3] = p3->edge; }
    mgl_polygon(c, v, 4, e, prov);
}

/* n vertices of a primitive: base b, through ix when there is one. `odd`:
 * a strip continued from an earlier part starts on an odd triangle. */
static void assemble(GLcontext c, GLenum mode, struct mgl_pv *b, const ULONG *ix, ULONG n, int odd)
{
    int flat = c->shade_model == GL_FLAT;
    ULONG k;
    switch (mode) {
    case GL_POINTS:
        for (k = 0; k < n; k++) mgl_point(c, V(k));
        break;
    case GL_LINES:
        for (k = 0; k + 1 < n; k += 2) mgl_line(c, V(k), V(k + 1), flat ? V(k + 1) : 0);
        break;
    case GL_LINE_STRIP: case GL_LINE_LOOP:
        for (k = 1; k < n; k++) mgl_line(c, V(k - 1), V(k), flat ? V(k) : 0);
        break;
    case GL_TRIANGLES:
        for (k = 0; k + 2 < n; k += 3) mgl_triangle(c, V(k), V(k + 1), V(k + 2), flat ? V(k + 2) : 0);
        break;
    case GL_TRIANGLE_STRIP:
        for (k = 0; k + 2 < n; k++) {
            if ((k + (ULONG)odd) & 1) mgl_triangle(c, V(k + 1), V(k), V(k + 2), flat ? V(k + 2) : 0);
            else mgl_triangle(c, V(k), V(k + 1), V(k + 2), flat ? V(k + 2) : 0);
        }
        break;
    case GL_TRIANGLE_FAN:
        for (k = 1; k + 1 < n; k++) mgl_triangle(c, V(0), V(k), V(k + 1), flat ? V(k + 1) : 0);
        break;
    case GL_QUADS:
        for (k = 0; k + 3 < n; k += 4) quad4(c, V(k), V(k + 1), V(k + 2), V(k + 3), flat ? V(k + 3) : 0, 0);
        break;
    case GL_QUAD_STRIP:
        for (k = 0; k + 3 < n; k += 2) quad4(c, V(k), V(k + 1), V(k + 3), V(k + 2), flat ? V(k + 3) : 0, 1);
        break;
    case GL_POLYGON:
        if (n > MGL_VB) { assemble(c, GL_TRIANGLE_FAN, b, ix, n, 0); return; }
        if (n >= 3) {
            struct mgl_pv *v[MGL_VB + 4];
            UBYTE e[MGL_VB + 4];
            ULONG m = n > MGL_VB ? MGL_VB : n;
            for (k = 0; k < m; k++) { v[k] = V(k); e[k] = v[k]->edge; }
            mgl_polygon(c, v, (int)m, e, flat ? V(0) : 0);
        }
        break;
    }
    mgl_pipe_flush(c);
}
#undef V

/* ---- glBegin / glEnd --------------------------------------------------------------------------- */

static int valid_mode(GLenum m) { return m <= GL_POLYGON; }

void GLBegin(GLcontext c, GLenum mode)
{
    ULONG a = mode;
    MGL_REC(c, OP_BEGIN, 1, &a);
    if (c->prim != PRIM_NONE) { mgl_error(c, GL_INVALID_OPERATION); return; }
    if (!valid_mode(mode)) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->prim = mode;
    c->nv = 0;
    c->prim_count = 0;
}

/* The buffer is full in the middle of a primitive: draw what is complete
 * and keep what the rest needs. */
static void part(GLcontext c)
{
    int n = c->nv, keep = 0, k;
    switch (c->prim) {
    case GL_POINTS: keep = 0; break;
    case GL_LINES: keep = n & 1; n -= keep; break;
    case GL_TRIANGLES: keep = n % 3; n -= keep; break;
    case GL_QUADS: keep = n & 3; n -= keep; break;
    case GL_LINE_STRIP: case GL_LINE_LOOP: keep = 1; break;
    case GL_QUAD_STRIP: keep = 2 + (n & 1); n -= n & 1; break;
    case GL_TRIANGLE_STRIP: keep = 2; break;
    case GL_TRIANGLE_FAN: case GL_POLYGON: keep = 2; break;
    }
    if (c->prim_count == c->nv && (c->prim == GL_LINE_LOOP)) c->first = c->vb[0];
    assemble(c, c->prim == GL_POLYGON ? GL_TRIANGLE_FAN : c->prim, c->vb, 0, (ULONG)n, c->prim_count - c->nv > 0 ? c->strip_odd : 0);
    if (c->prim == GL_TRIANGLE_STRIP) c->strip_odd ^= (n - 2) & 1;
    if (c->prim == GL_TRIANGLE_FAN || c->prim == GL_POLYGON) {
        c->vb[1] = c->vb[c->nv - 1];
        c->vb[1].edge = 0;                      /* the inner edge, in line mode */
        c->nv = 2;
        return;
    }
    for (k = 0; k < keep; k++) c->vb[k] = c->vb[c->nv - keep + k];
    c->nv = keep;
}

void mgl_vertex(GLcontext c, float x, float y, float z, float w)
{
    float o[4];
    if (c->prim == PRIM_NONE) return;
    o[0] = x; o[1] = y; o[2] = z; o[3] = w;
    if (c->nv >= MGL_VB) part(c);
    if (c->prim_count == 0) c->strip_odd = 0;
    mgl_process(c, &c->vb[c->nv], o, c->color, c->normal, (const float (*)[4])c->tc);
    c->nv++;
    c->prim_count++;
}

void mgl_end_prim(GLcontext c)
{
    GLenum p = c->prim;
    int whole = c->prim_count == c->nv;
    if (p == PRIM_NONE) return;
    if (c->nv) {
        if (p == GL_POLYGON && !whole) assemble(c, GL_TRIANGLE_FAN, c->vb, 0, (ULONG)c->nv, 0);
        else assemble(c, p, c->vb, 0, (ULONG)c->nv, whole ? 0 : c->strip_odd);
    }
    if (p == GL_LINE_LOOP && c->prim_count >= 2) {
        struct mgl_pv *first = whole ? &c->vb[0] : &c->first;
        if (c->nv) mgl_line(c, &c->vb[c->nv - 1], first, c->shade_model == GL_FLAT ? first : 0);
    }
    c->prim = PRIM_NONE;
    c->nv = 0;
}

void GLEnd(GLcontext c)
{
    MGL_REC(c, OP_ENDPRIM, 0, 0);
    if (c->prim == PRIM_NONE) { mgl_error(c, GL_INVALID_OPERATION); return; }
    mgl_end_prim(c);
}

/* ---- current values ---------------------------------------------------------------------------- */

void GLVertex4f(GLcontext c, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    ULONG a[4];
    a[0] = mgl_fu(x); a[1] = mgl_fu(y); a[2] = mgl_fu(z); a[3] = mgl_fu(w);
    MGL_REC(c, OP_VERTEX, 4, a);
    mgl_vertex(c, x, y, z, w);
}

void GLVertex2fv(GLcontext c, GLfloat *v) { if (v) GLVertex4f(c, v[0], v[1], 0.0f, 1.0f); }
void GLVertex3fv(GLcontext c, GLfloat *v) { if (v) GLVertex4f(c, v[0], v[1], v[2], 1.0f); }
void GLVertex4fv(GLcontext c, GLfloat *v) { if (v) GLVertex4f(c, v[0], v[1], v[2], v[3]); }

void GLColor4f(GLcontext c, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    ULONG a[4];
    a[0] = mgl_fu(red); a[1] = mgl_fu(green); a[2] = mgl_fu(blue); a[3] = mgl_fu(alpha);
    MGL_REC(c, OP_COLOR, 4, a);
    c->color[0] = red; c->color[1] = green; c->color[2] = blue; c->color[3] = alpha;
}

void GLColor3fv(GLcontext c, GLfloat *v) { if (v) GLColor4f(c, v[0], v[1], v[2], 1.0f); }
void GLColor4fv(GLcontext c, GLfloat *v) { if (v) GLColor4f(c, v[0], v[1], v[2], v[3]); }

void GLColor4ub(GLcontext c, GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    GLColor4f(c, red * (1.0f / 255.0f), green * (1.0f / 255.0f), blue * (1.0f / 255.0f), alpha * (1.0f / 255.0f));
}

void GLColor3ubv(GLcontext c, GLubyte *v) { if (v) GLColor4ub(c, v[0], v[1], v[2], 255); }
void GLColor4ubv(GLcontext c, GLubyte *v) { if (v) GLColor4ub(c, v[0], v[1], v[2], v[3]); }

void GLNormal3f(GLcontext c, GLfloat x, GLfloat y, GLfloat z)
{
    ULONG a[3];
    a[0] = mgl_fu(x); a[1] = mgl_fu(y); a[2] = mgl_fu(z);
    MGL_REC(c, OP_NORMAL, 3, a);
    c->normal[0] = x; c->normal[1] = y; c->normal[2] = z;
}

void GLNormal3fv(GLcontext c, GLfloat *n) { if (n) GLNormal3f(c, n[0], n[1], n[2]); }

void GLTexCoord4f(GLcontext c, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    ULONG a[4];
    a[0] = mgl_fu(s); a[1] = mgl_fu(t); a[2] = mgl_fu(r); a[3] = mgl_fu(q);
    MGL_REC(c, OP_TEXCOORD, 4, a);
    c->tc[0][0] = s; c->tc[0][1] = t; c->tc[0][2] = r; c->tc[0][3] = q;
}

void GLTexCoord2f(GLcontext c, GLfloat s, GLfloat t) { GLTexCoord4f(c, s, t, 0.0f, 1.0f); }
void GLTexCoord2fv(GLcontext c, GLfloat *v) { if (v) GLTexCoord4f(c, v[0], v[1], 0.0f, 1.0f); }
void GLTexCoord4fv(GLcontext c, GLfloat *v) { if (v) GLTexCoord4f(c, v[0], v[1], v[2], v[3]); }

void GLMultiTexCoord2fARB(GLcontext c, GLenum unit, GLfloat s, GLfloat t)
{
    ULONG a[3];
    int u;
    a[0] = unit; a[1] = mgl_fu(s); a[2] = mgl_fu(t);
    MGL_REC(c, OP_MTEXCOORD, 3, a);
    u = (int)(unit - GL_TEXTURE0_ARB);
    if (u < 0 || u >= MGL_TEX_UNITS) { if (u < 0 || u > 31) mgl_error(c, GL_INVALID_ENUM); return; }
    c->tc[u][0] = s; c->tc[u][1] = t; c->tc[u][2] = 0.0f; c->tc[u][3] = 1.0f;
}

void GLMultiTexCoord2fvARB(GLcontext c, GLenum unit, GLfloat *v) { if (v) GLMultiTexCoord2fARB(c, unit, v[0], v[1]); }

void GLActiveTextureARB(GLcontext c, GLenum unit)
{
    ULONG a = unit;
    int u;
    MGL_REC(c, OP_ACTIVETEX, 1, &a);
    u = (int)(unit - GL_TEXTURE0_ARB);
    if (u < 0 || u >= MGL_TEX_UNITS) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->active = u;
}

void GLClientActiveTextureARB(GLcontext c, GLenum unit)
{
    int u;
    if (!c) return;
    u = (int)(unit - GL_TEXTURE0_ARB);
    if (u < 0 || u >= MGL_TEX_UNITS) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->client_active = u;
}

/* ---- vertex arrays ------------------------------------------------------------------------------ */

static int type_size(GLenum t)
{
    switch (t) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: case MGL_UBYTE_ARGB: case MGL_UBYTE_BGRA: return 1;
    case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
    case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: return 4;
    case GL_DOUBLE: return 8;
    }
    return 0;
}

static void set_array(GLcontext c, struct mgl_array *a, GLint size, GLenum type, GLsizei stride, const GLvoid *p)
{
    if (!c) return;
    if (stride < 0 || !type_size(type)) { mgl_error(c, stride < 0 ? GL_INVALID_VALUE : GL_INVALID_ENUM); return; }
    a->size = size; a->type = type; a->stride = stride; a->ptr = p;
}

void GLVertexPointer(GLcontext c, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    if (size < 2 || size > 4) { mgl_error(c, GL_INVALID_VALUE); return; }
    set_array(c, &c->va, size, type, stride, pointer);
}

void GLColorPointer(GLcontext c, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    if (size < 3 || size > 4) { mgl_error(c, GL_INVALID_VALUE); return; }
    set_array(c, &c->ca, size, type, stride, pointer);
}

void GLNormalPointer(GLcontext c, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    set_array(c, &c->na, 3, type, stride, pointer);
}

void GLTexCoordPointer(GLcontext c, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    if (size < 1 || size > 4) { mgl_error(c, GL_INVALID_VALUE); return; }
    set_array(c, &c->ta[c->client_active], size, type, stride, pointer);
}

/* Colour index and edge flag arrays: accepted, not drawn from. */
void GLIndexPointer(GLcontext c, GLenum type, GLsizei stride, const GLvoid *pointer) { (void)c; (void)type; (void)stride; (void)pointer; }
void GLEdgeFlagPointer(GLcontext c, GLsizei stride, const GLvoid *pointer) { (void)c; (void)stride; (void)pointer; }

static ULONG client_bit(GLcontext c, GLenum cap)
{
    switch (cap) {
    case GL_VERTEX_ARRAY: return CL_VERTEX;
    case GL_COLOR_ARRAY: return CL_COLOR;
    case GL_NORMAL_ARRAY: return CL_NORMAL;
    case GL_TEXTURE_COORD_ARRAY: return c->client_active ? CL_TEX1 : CL_TEX0;
    case GL_INDEX_ARRAY: case GL_EDGE_FLAG_ARRAY: return 0x80000000UL;
    }
    return 0;
}

void GLEnableClientState(GLcontext c, GLenum cap)
{
    ULONG bit;
    if (!c) return;
    bit = client_bit(c, cap);
    if (!bit) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->client |= bit & ~0x80000000UL;
}

void GLDisableClientState(GLcontext c, GLenum cap)
{
    ULONG bit;
    if (!c) return;
    bit = client_bit(c, cap);
    if (!bit) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->client &= ~bit;
}

void GLInterleavedArrays(GLcontext c, GLenum format, GLsizei stride, const GLvoid *pointer)
{
    const UBYTE *p = pointer;
    int tsz = 0, csz = 0, nsz = 0, vsz = 3, tc = 0, cub = 0, off = 0, size;
    if (!c) return;
    switch (format) {
    case GL_V2F: vsz = 2; break;
    case GL_V3F: break;
    case GL_C4UB_V2F: csz = 4; cub = 1; vsz = 2; break;
    case GL_C4UB_V3F: csz = 4; cub = 1; break;
    case GL_C3F_V3F: csz = 3; break;
    case GL_N3F_V3F: nsz = 3; break;
    case GL_C4F_N3F_V3F: csz = 4; nsz = 3; break;
    case GL_T2F_V3F: tsz = 2; break;
    case GL_T4F_V4F: tsz = 4; vsz = 4; break;
    case GL_T2F_C4UB_V3F: tsz = 2; csz = 4; cub = 1; break;
    case GL_T2F_C3F_V3F: tsz = 2; csz = 3; break;
    case GL_T2F_N3F_V3F: tsz = 2; nsz = 3; break;
    case GL_T2F_C4F_N3F_V3F: tsz = 2; csz = 4; nsz = 3; break;
    case GL_T4F_C4F_N3F_V4F: tsz = 4; csz = 4; nsz = 3; vsz = 4; break;
    default: mgl_error(c, GL_INVALID_ENUM); return;
    }
    size = tsz * 4 + (cub ? 4 : csz * 4) + nsz * 4 + vsz * 4;
    if (!stride) stride = size;
    c->client &= ~(CL_COLOR | CL_NORMAL | CL_TEX0 | CL_TEX1);
    c->client |= CL_VERTEX;
    tc = c->client_active;
    if (tsz) { set_array(c, &c->ta[tc], tsz, GL_FLOAT, stride, p + off); c->client |= tc ? CL_TEX1 : CL_TEX0; off += tsz * 4; }
    if (csz) { set_array(c, &c->ca, csz, cub ? GL_UNSIGNED_BYTE : GL_FLOAT, stride, p + off); c->client |= CL_COLOR; off += cub ? 4 : csz * 4; }
    if (nsz) { set_array(c, &c->na, 3, GL_FLOAT, stride, p + off); c->client |= CL_NORMAL; off += 12; }
    set_array(c, &c->va, vsz, GL_FLOAT, stride, p + off);
}

void GLLockArrays(GLcontext c, GLuint first, GLsizei count)
{
    if (!c) return;
    if (count < 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    c->lock_first = (GLint)first;
    c->lock_count = count;
}

void GLUnlockArrays(GLcontext c)
{
    if (!c) return;
    c->lock_count = 0;
}

/* One element of an array, as floats. */
static void fetch(const struct mgl_array *a, ULONG i, float *out, int n, int normalise)
{
    int k, sz = type_size(a->type);
    const UBYTE *p = a->ptr + i * (ULONG)(a->stride ? a->stride : a->size * sz);
    for (k = 0; k < n && k < a->size; k++) {
        switch (a->type) {
        case GL_FLOAT: { union { ULONG u; float f; } x; x.u = *(const ULONG *)(p + k * 4); out[k] = x.f; break; }
        case GL_DOUBLE: out[k] = (float)((const double *)p)[k]; break;
        case GL_INT: out[k] = normalise ? (float)((const LONG *)p)[k] * (1.0f / 2147483647.0f) : (float)((const LONG *)p)[k]; break;
        case GL_UNSIGNED_INT: out[k] = normalise ? (float)((const ULONG *)p)[k] * (1.0f / 4294967295.0f) : (float)((const ULONG *)p)[k]; break;
        case GL_SHORT: out[k] = normalise ? (float)((const WORD *)p)[k] * (1.0f / 32767.0f) : (float)((const WORD *)p)[k]; break;
        case GL_UNSIGNED_SHORT: out[k] = normalise ? (float)((const UWORD *)p)[k] * (1.0f / 65535.0f) : (float)((const UWORD *)p)[k]; break;
        case GL_BYTE: out[k] = normalise ? (float)((const BYTE *)p)[k] * (1.0f / 127.0f) : (float)((const BYTE *)p)[k]; break;
        default: out[k] = normalise ? (float)p[k] * (1.0f / 255.0f) : (float)p[k]; break;
        }
    }
    if (a->type == MGL_UBYTE_ARGB && n >= 4) {         /* A, R, G, B in memory */
        float t = out[0]; out[0] = out[1]; out[1] = out[2]; out[2] = out[3]; out[3] = t;
    } else if (a->type == MGL_UBYTE_BGRA && n >= 3) {   /* B, G, R (, A) */
        float t = out[0]; out[0] = out[2]; out[2] = t;
    }
}

/* The vertex at index i of the enabled arrays (the current values for the rest). */
static void array_values(GLcontext c, ULONG i, float *obj, float *col, float *nrm, float (*tc)[4])
{
    int u;
    obj[0] = obj[1] = obj[2] = 0.0f; obj[3] = 1.0f;
    fetch(&c->va, i, obj, 4, 0);
    if (c->client & CL_COLOR) { col[3] = 1.0f; fetch(&c->ca, i, col, 4, 1); }
    else { col[0] = c->color[0]; col[1] = c->color[1]; col[2] = c->color[2]; col[3] = c->color[3]; }
    if (c->client & CL_NORMAL) fetch(&c->na, i, nrm, 3, 1);
    else { nrm[0] = c->normal[0]; nrm[1] = c->normal[1]; nrm[2] = c->normal[2]; }
    for (u = 0; u < MGL_TEX_UNITS; u++) {
        if (c->client & (u ? CL_TEX1 : CL_TEX0)) { tc[u][0] = tc[u][1] = tc[u][2] = 0.0f; tc[u][3] = 1.0f; fetch(&c->ta[u], i, tc[u], 4, 0); }
        else { tc[u][0] = c->tc[u][0]; tc[u][1] = c->tc[u][1]; tc[u][2] = c->tc[u][2]; tc[u][3] = c->tc[u][3]; }
    }
}

void mgl_array_element(GLcontext c, GLint i)
{
    float obj[4], col[4], nrm[3], tc[MGL_TEX_UNITS][4];
    int u;
    if (!(c->client & CL_VERTEX)) return;
    array_values(c, (ULONG)i, obj, col, nrm, tc);
    /* the arrays' values become the current ones, as GL has it */
    if (c->client & CL_COLOR) GLColor4f(c, col[0], col[1], col[2], col[3]);
    if (c->client & CL_NORMAL) GLNormal3f(c, nrm[0], nrm[1], nrm[2]);
    for (u = 0; u < MGL_TEX_UNITS; u++)
        if (c->client & (u ? CL_TEX1 : CL_TEX0)) {
            if (u == 0) GLTexCoord4f(c, tc[0][0], tc[0][1], tc[0][2], tc[0][3]);
            else GLMultiTexCoord2fARB(c, GL_TEXTURE1_ARB, tc[1][0], tc[1][1]);
        }
    GLVertex4f(c, obj[0], obj[1], obj[2], obj[3]);
}

void GLArrayElement(GLcontext c, GLint i)
{
    if (!c) return;
    mgl_array_element(c, i);
}

/* Room for n processed vertices and n indices. */
static int cache_room(GLcontext c, ULONG n)
{
    if (n <= c->cache_cap) return 1;
    mgl_free(c->cache);
    mgl_free(c->cache_ix);
    c->cache_cap = (n + 255) & ~255UL;
    c->cache = mgl_alloc(c->cache_cap * sizeof(struct mgl_pv));
    c->cache_ix = mgl_alloc(c->cache_cap * sizeof(ULONG));
    if (!c->cache || !c->cache_ix) {
        mgl_free(c->cache); mgl_free(c->cache_ix);
        c->cache = 0; c->cache_ix = 0; c->cache_cap = 0;
        mgl_error(c, GL_OUT_OF_MEMORY);
        return 0;
    }
    return 1;
}

static void process_range(GLcontext c, ULONG first, ULONG n)
{
    float obj[4], col[4], nrm[3], tc[MGL_TEX_UNITS][4];
    ULONG k;
    for (k = 0; k < n; k++) {
        array_values(c, first + k, obj, col, nrm, tc);
        mgl_process(c, &c->cache[k], obj, col, nrm, (const float (*)[4])tc);
    }
}

/* Drawn through glBegin/glEnd: while a list is compiled. */
static void draw_immediate(GLcontext c, GLenum mode, const ULONG *ix, ULONG first, ULONG n)
{
    ULONG k;
    GLBegin(c, mode);
    for (k = 0; k < n; k++) mgl_array_element(c, (GLint)(ix ? ix[k] : first + k));
    GLEnd(c);
}

void GLDrawArrays(GLcontext c, GLenum mode, GLint first, GLsizei count)
{
    if (!c) return;
    if (!valid_mode(mode)) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (count < 0 || first < 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    if (c->prim != PRIM_NONE) { mgl_error(c, GL_INVALID_OPERATION); return; }
    if (!(c->client & CL_VERTEX) || !count) return;
    if (c->compiling) { draw_immediate(c, mode, 0, (ULONG)first, (ULONG)count); return; }
    while (count > 0) {
        /* in pieces of the cache's size, keeping what strips and fans need */
        ULONG n = (ULONG)count > 8192 ? 8192 : (ULONG)count;
        if (!cache_room(c, n)) return;
        process_range(c, (ULONG)first, n);
        assemble(c, mode, c->cache, 0, n, 0);
        if ((ULONG)count == n) break;
        if (mode == GL_TRIANGLE_FAN || mode == GL_POLYGON || mode == GL_LINE_LOOP) {
            /* rare: the rest through glBegin, so the first vertex stays */
            draw_immediate(c, mode, 0, (ULONG)first, (ULONG)count);
            return;
        }
        n -= mode == GL_TRIANGLE_STRIP || mode == GL_QUAD_STRIP ? 2 : mode == GL_LINE_STRIP ? 1 : 0;
        n -= mode == GL_TRIANGLE_STRIP ? n & 1 : 0;    /* keep the strip's winding */
        first += (GLint)n;
        count -= (GLsizei)n;
    }
}

void GLMultiDrawArrays(GLcontext c, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    GLsizei i;
    if (!first || !count) return;
    for (i = 0; i < primcount; i++) GLDrawArrays(c, mode, first[i], count[i]);
}

void GLDrawElements(GLcontext c, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    ULONG k, lo = 0xFFFFFFFFUL, hi = 0, n = (ULONG)count, *ix;
    if (!c) return;
    if (!valid_mode(mode)) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (count < 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (c->prim != PRIM_NONE) { mgl_error(c, GL_INVALID_OPERATION); return; }
    if (!(c->client & CL_VERTEX) || !count || !indices) return;
    if (!cache_room(c, n)) return;
    ix = c->cache_ix;
    for (k = 0; k < n; k++) {
        ULONG v = type == GL_UNSIGNED_BYTE ? ((const UBYTE *)indices)[k]
                : type == GL_UNSIGNED_SHORT ? ((const UWORD *)indices)[k] : ((const ULONG *)indices)[k];
        ix[k] = v;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (c->compiling) { draw_immediate(c, mode, ix, 0, n); return; }
    if (hi - lo + 1 <= 2 * n + 64) {
        /* each vertex once: the range the indices use */
        ULONG range = hi - lo + 1;
        if (range > c->cache_cap) {
            ULONG *keep = mgl_alloc(n * sizeof(ULONG));
            if (!keep) { mgl_error(c, GL_OUT_OF_MEMORY); return; }
            mgl_copy(keep, ix, n * sizeof(ULONG));
            if (!cache_room(c, range)) { mgl_free(keep); return; }
            mgl_copy(c->cache_ix, keep, n * sizeof(ULONG));
            mgl_free(keep);
            ix = c->cache_ix;
        }
        process_range(c, lo, range);
        for (k = 0; k < n; k++) ix[k] -= lo;
        assemble(c, mode, c->cache, ix, n, 0);
    } else {
        /* scattered indices: each element as it comes */
        float obj[4], col[4], nrm[3], tc[MGL_TEX_UNITS][4];
        for (k = 0; k < n; k++) {
            array_values(c, ix[k], obj, col, nrm, tc);
            mgl_process(c, &c->cache[k], obj, col, nrm, (const float (*)[4])tc);
        }
        assemble(c, mode, c->cache, 0, n, 0);
    }
}

/* ---- finishing ----------------------------------------------------------------------------------- */

void GLFlush(GLcontext c)
{
    if (!c) return;
    mgl_pipe_flush(c);
    mgl_flush(c);
}

void GLFinish(GLcontext c) { GLFlush(c); }

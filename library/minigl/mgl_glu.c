/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: the GLU part. gluBuild2DMipmaps (any size, scaled to
 * powers of two, levels by averaging), gluErrorString, and the quadrics
 * (spheres, cylinders and disks with normals and texture coordinates, in
 * each draw style). gluPerspective and gluLookAt are in mgl_matrix.c.
 * GLU calls without a context argument use the current context. */
#include "mgl_internal.h"

struct GLUquadricObj_t {
    GLenum normals;                 /* GLU_NONE, GLU_FLAT, GLU_SMOOTH */
    GLboolean texture;
    GLenum style;                   /* GLU_FILL, GLU_LINE, GLU_SILHOUETTE, GLU_POINT */
    GLenum orientation;             /* GLU_OUTSIDE, GLU_INSIDE */
    MGLUfuncptr error;
};

/* ---- errors ------------------------------------------------------------------------------------ */

const GLubyte *GLUErrorString(GLenum e)
{
    switch (e) {
    case GL_NO_ERROR: return (const GLubyte *)"no error";
    case GL_INVALID_ENUM: return (const GLubyte *)"invalid enumerant";
    case GL_INVALID_VALUE: return (const GLubyte *)"invalid value";
    case GL_INVALID_OPERATION: return (const GLubyte *)"invalid operation";
    case GL_STACK_OVERFLOW: return (const GLubyte *)"stack overflow";
    case GL_STACK_UNDERFLOW: return (const GLubyte *)"stack underflow";
    case GL_OUT_OF_MEMORY: return (const GLubyte *)"out of memory";
    case GL_TABLE_TOO_LARGE: return (const GLubyte *)"table too large";
    case GLU_INVALID_ENUM: return (const GLubyte *)"invalid enumerant";
    case GLU_INVALID_VALUE: return (const GLubyte *)"invalid value";
    case GLU_OUT_OF_MEMORY: return (const GLubyte *)"out of memory";
    case GLU_INCOMPATIBLE_GL_VERSION: return (const GLubyte *)"incompatible gl version";
    case GLU_INVALID_OPERATION: return (const GLubyte *)"invalid operation";
    }
    return 0;
}

/* ---- mipmaps ------------------------------------------------------------------------------------ */

static int floor_pow2(int v)
{
    int p = 1;
    while (p * 2 <= v && p < 2048) p *= 2;
    return p;
}

GLint GLUBuild2DMipmaps(GLcontext c, GLenum target, GLint internalFormat, GLsizei width, GLsizei height,
                        GLenum format, GLenum type, const GLvoid *data)
{
    UBYTE *src, *img, *next;
    int w, h, level = 0, x, y;
    GLint keep_row, keep_rows, keep_px, keep_al;
    if (!c) return GLU_INVALID_OPERATION;
    if (target != GL_TEXTURE_2D) return GLU_INVALID_ENUM;
    if (width < 1 || height < 1 || !data) return GLU_INVALID_VALUE;
    src = mgl_unpack_rgba(c, width, height, format, type, data);
    if (!src) return GLU_OUT_OF_MEMORY;
    w = floor_pow2(width);
    h = floor_pow2(height);
    img = mgl_alloc((ULONG)w * h * 4);
    if (!img) { mgl_free(src); return GLU_OUT_OF_MEMORY; }
    /* to a power of two: each texel the average of the source pixels it covers */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int x0 = x * width / w, x1 = (x + 1) * width / w, y0 = y * height / h, y1 = (y + 1) * height / h, i, j, k;
            ULONG sum[4] = { 0, 0, 0, 0 }, n = 0;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            for (j = y0; j < y1; j++)
                for (i = x0; i < x1; i++, n++)
                    for (k = 0; k < 4; k++) sum[k] += src[((ULONG)j * width + i) * 4 + k];
            for (k = 0; k < 4; k++) img[((ULONG)y * w + x) * 4 + k] = (UBYTE)(sum[k] / n);
        }
    mgl_free(src);
    keep_row = c->unpack_row; keep_rows = c->unpack_skip_rows; keep_px = c->unpack_skip_px; keep_al = c->unpack_align;
    c->unpack_row = 0; c->unpack_skip_rows = 0; c->unpack_skip_px = 0; c->unpack_align = 1;
    for (;;) {
        GLTexImage2D(c, target, level, internalFormat, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
        if (w == 1 && h == 1) break;
        {
            int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1, k;
            next = mgl_alloc((ULONG)nw * nh * 4);
            if (!next) break;
            for (y = 0; y < nh; y++)
                for (x = 0; x < nw; x++)
                    for (k = 0; k < 4; k++) {
                        int x0 = x * 2, y0 = y * 2, x1 = w > 1 ? x0 + 1 : x0, y1 = h > 1 ? y0 + 1 : y0;
                        ULONG s = (ULONG)img[((ULONG)y0 * w + x0) * 4 + k] + img[((ULONG)y0 * w + x1) * 4 + k]
                                + img[((ULONG)y1 * w + x0) * 4 + k] + img[((ULONG)y1 * w + x1) * 4 + k];
                        next[((ULONG)y * nw + x) * 4 + k] = (UBYTE)((s + 2) >> 2);
                    }
            mgl_free(img);
            img = next;
            w = nw; h = nh;
            level++;
        }
    }
    c->unpack_row = keep_row; c->unpack_skip_rows = keep_rows; c->unpack_skip_px = keep_px; c->unpack_align = keep_al;
    mgl_free(img);
    return 0;
}

/* ---- quadrics ---------------------------------------------------------------------------------- */

GLUquadricObj *GLUNewQuadric(void)
{
    GLUquadricObj *q = mgl_alloc(sizeof *q);
    if (!q) return 0;
    q->normals = GLU_SMOOTH;
    q->texture = GL_FALSE;
    q->style = GLU_FILL;
    q->orientation = GLU_OUTSIDE;
    return q;
}

void GLUDeleteQuadric(GLUquadricObj *q) { mgl_free(q); }

static void quad_error(GLUquadricObj *q, GLenum e)
{
    if (q && q->error) ((void (*)(GLenum))q->error)(e);
}

void GLUQuadricNormals(GLUquadricObj *q, GLenum normals)
{
    if (!q) return;
    if (normals != GLU_NONE && normals != GLU_FLAT && normals != GLU_SMOOTH) { quad_error(q, GLU_INVALID_ENUM); return; }
    q->normals = normals;
}

void GLUQuadricTexture(GLUquadricObj *q, GLboolean textureCoords) { if (q) q->texture = textureCoords ? GL_TRUE : GL_FALSE; }

void GLUQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle)
{
    if (!q) return;
    if (drawStyle != GLU_FILL && drawStyle != GLU_LINE && drawStyle != GLU_SILHOUETTE && drawStyle != GLU_POINT) {
        quad_error(q, GLU_INVALID_ENUM); return;
    }
    q->style = drawStyle;
}

void GLUQuadricOrientation(GLUquadricObj *q, GLenum orientation)
{
    if (!q) return;
    if (orientation != GLU_OUTSIDE && orientation != GLU_INSIDE) { quad_error(q, GLU_INVALID_ENUM); return; }
    q->orientation = orientation;
}

void GLUQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn)
{
    if (!q) return;
    if (which == GLU_ERROR) q->error = fn;
}

#define MAXSL 128

static void table(int n, float *s, float *co)
{
    int i;
    for (i = 0; i <= n; i++) {
        float a = 2.0f * MGL_PI * (float)i / (float)n;
        s[i] = mgl_sin(a);
        co[i] = mgl_cos(a);
    }
    s[n] = s[0]; co[n] = co[0];
}

static void vertex(GLcontext c, GLUquadricObj *q, float x, float y, float z, float nx, float ny, float nz, float s, float t)
{
    float sign = q->orientation == GLU_INSIDE ? -1.0f : 1.0f;
    if (q->normals != GLU_NONE) GLNormal3f(c, nx * sign, ny * sign, nz * sign);
    if (q->texture) GLTexCoord2f(c, s, t);
    GLVertex4f(c, x, y, z, 1.0f);
}

void GLUCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks)
{
    GLcontext c = mgl_current;
    float sn[MAXSL + 1], cs[MAXSL + 1], nz, nl, b = (float)base, t = (float)top, hgt = (float)height;
    int i, j;
    if (!c || !q) return;
    if (base < 0 || top < 0 || height < 0 || slices < 2 || stacks < 1) { quad_error(q, GLU_INVALID_VALUE); return; }
    if (slices > MAXSL) slices = MAXSL;
    table(slices, sn, cs);
    nz = (b - t) / (hgt != 0.0f ? hgt : 1.0f);
    nl = mgl_rsqrt(1.0f + nz * nz);
    if (q->style == GLU_POINT) {
        GLBegin(c, GL_POINTS);
        for (j = 0; j <= stacks; j++) {
            float z = hgt * j / stacks, r = b + (t - b) * j / stacks;
            for (i = 0; i < slices; i++)
                vertex(c, q, r * sn[i], r * cs[i], z, sn[i] * nl, cs[i] * nl, nz * nl, (float)i / slices, (float)j / stacks);
        }
        GLEnd(c);
        return;
    }
    if (q->style == GLU_LINE || q->style == GLU_SILHOUETTE) {
        for (j = 0; j <= stacks; j++) {
            float z = hgt * j / stacks, r = b + (t - b) * j / stacks;
            if (q->style == GLU_SILHOUETTE && j != 0 && j != stacks) continue;
            GLBegin(c, GL_LINE_LOOP);
            for (i = 0; i < slices; i++)
                vertex(c, q, r * sn[i], r * cs[i], z, sn[i] * nl, cs[i] * nl, nz * nl, (float)i / slices, (float)j / stacks);
            GLEnd(c);
        }
        GLBegin(c, GL_LINES);
        for (i = 0; i < slices; i++) {
            vertex(c, q, b * sn[i], b * cs[i], 0.0f, sn[i] * nl, cs[i] * nl, nz * nl, (float)i / slices, 0.0f);
            vertex(c, q, t * sn[i], t * cs[i], hgt, sn[i] * nl, cs[i] * nl, nz * nl, (float)i / slices, 1.0f);
        }
        GLEnd(c);
        return;
    }
    for (j = 0; j < stacks; j++) {
        float z0 = hgt * j / stacks, z1 = hgt * (j + 1) / stacks;
        float r0 = b + (t - b) * j / stacks, r1 = b + (t - b) * (j + 1) / stacks;
        GLBegin(c, GL_QUAD_STRIP);
        for (i = 0; i <= slices; i++) {
            int k = q->orientation == GLU_INSIDE ? slices - i : i;
            float s = (float)k / slices;
            vertex(c, q, r1 * sn[k], r1 * cs[k], z1, sn[k] * nl, cs[k] * nl, nz * nl, s, (float)(j + 1) / stacks);
            vertex(c, q, r0 * sn[k], r0 * cs[k], z0, sn[k] * nl, cs[k] * nl, nz * nl, s, (float)j / stacks);
        }
        GLEnd(c);
    }
}

void GLUSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks)
{
    GLcontext c = mgl_current;
    float sn[MAXSL + 1], cs[MAXSL + 1], r = (float)radius;
    int i, j;
    if (!c || !q) return;
    if (radius < 0 || slices < 2 || stacks < 1) { quad_error(q, GLU_INVALID_VALUE); return; }
    if (slices > MAXSL) slices = MAXSL;
    table(slices, sn, cs);
    for (j = 0; j < stacks; j++) {
        float a0 = MGL_PI * j / stacks, a1 = MGL_PI * (j + 1) / stacks;
        float z0 = mgl_cos(a0), z1 = mgl_cos(a1), s0 = mgl_sin(a0), s1 = mgl_sin(a1);
        if (q->style == GLU_POINT || q->style == GLU_LINE || q->style == GLU_SILHOUETTE) {
            GLBegin(c, q->style == GLU_POINT ? GL_POINTS : GL_LINE_LOOP);
            for (i = 0; i < slices; i++)
                vertex(c, q, -sn[i] * s0 * r, cs[i] * s0 * r, z0 * r, -sn[i] * s0, cs[i] * s0, z0,
                       (float)i / slices, 1.0f - (float)j / stacks);
            GLEnd(c);
            continue;
        }
        GLBegin(c, GL_QUAD_STRIP);
        for (i = 0; i <= slices; i++) {
            int k = q->orientation == GLU_INSIDE ? slices - i : i;
            float s = (float)k / slices;
            vertex(c, q, -sn[k] * s0 * r, cs[k] * s0 * r, z0 * r, -sn[k] * s0, cs[k] * s0, z0, s, 1.0f - (float)j / stacks);
            vertex(c, q, -sn[k] * s1 * r, cs[k] * s1 * r, z1 * r, -sn[k] * s1, cs[k] * s1, z1, s, 1.0f - (float)(j + 1) / stacks);
        }
        GLEnd(c);
    }
}

void GLUDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
    GLcontext c = mgl_current;
    float sn[MAXSL + 1], cs[MAXSL + 1], ri = (float)inner, ro = (float)outer;
    int i, j;
    if (!c || !q) return;
    if (inner < 0 || outer <= 0 || inner >= outer || slices < 2 || loops < 1) { quad_error(q, GLU_INVALID_VALUE); return; }
    if (slices > MAXSL) slices = MAXSL;
    table(slices, sn, cs);
    for (j = 0; j < loops; j++) {
        float r0 = ri + (ro - ri) * j / loops, r1 = ri + (ro - ri) * (j + 1) / loops;
        if (q->style != GLU_FILL) {
            GLBegin(c, q->style == GLU_POINT ? GL_POINTS : GL_LINE_LOOP);
            for (i = 0; i < slices; i++)
                vertex(c, q, r1 * sn[i], r1 * cs[i], 0.0f, 0.0f, 0.0f, 1.0f, 0.5f + sn[i] * r1 / (2 * ro), 0.5f + cs[i] * r1 / (2 * ro));
            GLEnd(c);
            continue;
        }
        GLBegin(c, GL_QUAD_STRIP);
        for (i = 0; i <= slices; i++) {
            int k = q->orientation == GLU_INSIDE ? slices - i : i;
            vertex(c, q, r1 * sn[k], r1 * cs[k], 0.0f, 0.0f, 0.0f, 1.0f, 0.5f + sn[k] * r1 / (2 * ro), 0.5f + cs[k] * r1 / (2 * ro));
            vertex(c, q, r0 * sn[k], r0 * cs[k], 0.0f, 0.0f, 0.0f, 1.0f, 0.5f + sn[k] * r0 / (2 * ro), 0.5f + cs[k] * r0 / (2 * ro));
        }
        GLEnd(c);
    }
}

/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: the three matrix stacks (modelview, projection, texture
 * for each unit), the calls that change them, gluPerspective and gluLookAt,
 * and the matrices the pipeline derives: projection times modelview, and
 * the normal matrix. Column-major, as OpenGL. */
#include "mgl_internal.h"

#ifndef MGL_HOST
#include <proto/dos.h>
#endif

void mgl_mat_identity(float *m)
{
    int i;
    for (i = 0; i < 16; i++) m[i] = (i % 5) == 0 ? 1.0f : 0.0f;
}

void mgl_mat_mul(float *r, const float *a, const float *b)
{
    float t[16];
    int i, j;
    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++)
            t[j * 4 + i] = a[i] * b[j * 4] + a[4 + i] * b[j * 4 + 1] + a[8 + i] * b[j * 4 + 2] + a[12 + i] * b[j * 4 + 3];
    for (i = 0; i < 16; i++) r[i] = t[i];
}

float *mgl_top(GLcontext c)
{
    switch (c->matrix_mode) {
    case GL_PROJECTION: return c->proj[c->proj_top];
    case GL_TEXTURE: return c->texm[c->active][c->tex_top[c->active]];
    default: return c->mv[c->mv_top];
    }
}

static int is_identity(const float *m)
{
    int i;
    for (i = 0; i < 16; i++) if (m[i] != ((i % 5) == 0 ? 1.0f : 0.0f)) return 0;
    return 1;
}

static void changed(GLcontext c)
{
    if (c->matrix_mode == GL_TEXTURE) c->tex_identity[c->active] = is_identity(mgl_top(c));
    else {
        c->mvp_dirty = 1;
        if (c->matrix_mode == GL_MODELVIEW) c->nm_dirty = 1;
    }
}

void mgl_update_mvp(GLcontext c)
{
    const float *p = c->proj[c->proj_top];
    mgl_mat_mul(c->mvp, p, c->mv[c->mv_top]);
    /* glFrustum's shape needs 7 products of 16 */
    c->proj_kind = p[1] == 0 && p[2] == 0 && p[3] == 0 && p[4] == 0 && p[6] == 0 && p[7] == 0 && p[11] == -1.0f
                && p[12] == 0 && p[13] == 0 && p[15] == 0;
    c->mvp_dirty = 0;
}

/* The inverse transpose of the modelview's upper 3x3, by cofactors. */
void mgl_update_nm(GLcontext c)
{
    const float *m = c->mv[c->mv_top];
    float a = m[0], b = m[4], d = m[8], e = m[1], f = m[5], g = m[9], h = m[2], i = m[6], k = m[10];
    float c00 = f * k - g * i, c01 = -(e * k - g * h), c02 = e * i - f * h;
    float c10 = -(b * k - d * i), c11 = a * k - d * h, c12 = -(a * i - b * h);
    float c20 = b * g - d * f, c21 = -(a * g - d * e), c22 = a * f - b * e;
    float det = a * c00 + b * c01 + d * c02, r = det != 0.0f ? 1.0f / det : 0.0f;
    /* nm transforms a normal: n' = nm * n, rows as stored here (row-major 3x3) */
    c->nm[0] = c00 * r; c->nm[1] = c01 * r; c->nm[2] = c02 * r;
    c->nm[3] = c10 * r; c->nm[4] = c11 * r; c->nm[5] = c12 * r;
    c->nm[6] = c20 * r; c->nm[7] = c21 * r; c->nm[8] = c22 * r;
    c->nm_dirty = 0;
}

/* Window coordinates from the viewport: x right, y down from the top of
 * the drawing area (GL's y is up from the bottom), and how far outside the
 * viewport a vertex can lie before the triangle has to be clipped. */
void mgl_viewport_changed(GLcontext c)
{
    float hw = (float)c->vp_w * 0.5f, hh = (float)c->vp_h * 0.5f, m;
    c->vsx = hw;
    c->vtx = (float)c->vp_x + hw;
    c->vsy = -hh;
    c->vty = (float)(c->height - c->vp_y) - hh;
    /* the stream takes coordinates within +-16383 */
    m = hw > hh ? hw : hh;
    if (m < 1.0f) m = 1.0f;
    c->gb = (16000.0f - 4096.0f) / m;
    if (c->gb < 1.0f) c->gb = 1.0f;
}

/* ---- the calls ------------------------------------------------------------------------------ */

void GLMatrixMode(GLcontext c, GLenum mode)
{
    ULONG a = mode;
    MGL_REC(c, OP_MATRIXMODE, 1, &a);
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->matrix_mode = mode;
}

void GLLoadIdentity(GLcontext c)
{
    MGL_REC(c, OP_LOADIDENTITY, 0, 0);
    mgl_mat_identity(mgl_top(c));
    changed(c);
}

void GLLoadMatrixf(GLcontext c, const GLfloat *m)
{
    float *t;
    int i;
    if (!m) return;
    MGL_REC(c, OP_LOADMATRIX, 16, m);
    t = mgl_top(c);
    for (i = 0; i < 16; i++) t[i] = m[i];
    changed(c);
}

void GLLoadMatrixd(GLcontext c, const GLdouble *m)
{
    float f[16];
    int i;
    if (!m) return;
    for (i = 0; i < 16; i++) f[i] = (float)m[i];
    GLLoadMatrixf(c, f);
}

void GLMultMatrixf(GLcontext c, const GLfloat *m)
{
    float *t;
    if (!m) return;
    MGL_REC(c, OP_MULTMATRIX, 16, m);
    t = mgl_top(c);
    mgl_mat_mul(t, t, m);
    changed(c);
}

void GLMultMatrixd(GLcontext c, const GLdouble *m)
{
    float f[16];
    int i;
    if (!m) return;
    for (i = 0; i < 16; i++) f[i] = (float)m[i];
    GLMultMatrixf(c, f);
}

void GLPushMatrix(GLcontext c)
{
    float *from;
    int i;
    MGL_REC(c, OP_PUSH, 0, 0);
    from = mgl_top(c);
    switch (c->matrix_mode) {
    case GL_PROJECTION:
        if (c->proj_top + 1 >= PROJ_DEPTH) { mgl_error(c, GL_STACK_OVERFLOW); return; }
        c->proj_top++;
        break;
    case GL_TEXTURE:
        if (c->tex_top[c->active] + 1 >= TEX_DEPTH) { mgl_error(c, GL_STACK_OVERFLOW); return; }
        c->tex_top[c->active]++;
        break;
    default:
        if (c->mv_top + 1 >= MV_DEPTH) { mgl_error(c, GL_STACK_OVERFLOW); return; }
        c->mv_top++;
    }
    for (i = 0; i < 16; i++) mgl_top(c)[i] = from[i];
}

void GLPopMatrix(GLcontext c)
{
    MGL_REC(c, OP_POP, 0, 0);
    switch (c->matrix_mode) {
    case GL_PROJECTION:
        if (!c->proj_top) { mgl_error(c, GL_STACK_UNDERFLOW); return; }
        c->proj_top--;
        break;
    case GL_TEXTURE:
        if (!c->tex_top[c->active]) { mgl_error(c, GL_STACK_UNDERFLOW); return; }
        c->tex_top[c->active]--;
        break;
    default:
        if (!c->mv_top) { mgl_error(c, GL_STACK_UNDERFLOW); return; }
        c->mv_top--;
    }
    changed(c);
}

/* The top matrix times a rotation of the given sine and cosine about the
 * axis (x, y, z), which is normalised. */
static void rotate_sc(GLcontext c, float s, float co, float x, float y, float z)
{
    float m[16], l = x * x + y * y + z * z, t, *top;
    if (l == 0.0f) return;
    if (l != 1.0f) { l = mgl_rsqrt(l); x *= l; y *= l; z *= l; }
    t = 1.0f - co;
    m[0] = x * x * t + co;     m[4] = x * y * t - z * s;  m[8] = x * z * t + y * s;   m[12] = 0;
    m[1] = y * x * t + z * s;  m[5] = y * y * t + co;     m[9] = y * z * t - x * s;   m[13] = 0;
    m[2] = x * z * t - y * s;  m[6] = y * z * t + x * s;  m[10] = z * z * t + co;     m[14] = 0;
    m[3] = 0;                  m[7] = 0;                  m[11] = 0;                  m[15] = 1;
    top = mgl_top(c);
    mgl_mat_mul(top, top, m);
    changed(c);
}

void GLRotatef(GLcontext c, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float r;
    ULONG a[4];
    a[0] = mgl_fu(angle); a[1] = mgl_fu(x); a[2] = mgl_fu(y); a[3] = mgl_fu(z);
    MGL_REC(c, OP_ROTATE, 4, a);
    r = angle * (MGL_PI / 180.0f);
    rotate_sc(c, mgl_sin(r), mgl_cos(r), x, y, z);
}

void GLRotated(GLcontext c, GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    GLRotatef(c, (float)angle, (float)x, (float)y, (float)z);
}

static void axis_of(GLint xyz, float *x, float *y, float *z)
{
    *x = (xyz & GLROT_100) ? 1.0f : 0.0f;
    *y = (xyz & GLROT_010) ? 1.0f : 0.0f;
    *z = (xyz & GLROT_001) ? 1.0f : 0.0f;
    if (xyz == GLROT_011) { *x = 0; *y = 1; *z = 1; }
    else if (xyz == GLROT_101) { *x = 1; *y = 0; *z = 1; }
    else if (xyz == GLROT_110) { *x = 1; *y = 1; *z = 0; }
    else if (xyz == GLROT_111) { *x = 1; *y = 1; *z = 1; }
    if (*x == 0 && *y == 0 && *z == 0) *x = 1;
}

void GLRotatefEXT(GLcontext c, GLfloat angle, const GLint xyz)
{
    float x, y, z;
    axis_of(xyz, &x, &y, &z);
    GLRotatef(c, angle, x, y, z);
}

void GLRotatefEXTs(GLcontext c, GLfloat sin_an, GLfloat cos_an, const GLint xyz)
{
    float x, y, z;
    if (!c) return;
    axis_of(xyz, &x, &y, &z);
    if (c->compiling) {
        /* a list keeps the angle */
        float deg = 0.0f, s = sin_an, k = cos_an;
        /* atan2 by halving: good enough for a recorded call */
        int q;
        float lo = -180.0f, hi = 180.0f;
        for (q = 0; q < 40; q++) {
            float mid = (lo + hi) * 0.5f, r = mid * (MGL_PI / 180.0f);
            /* the angle whose (sin, cos) turns furthest anticlockwise from (s, k) is below */
            if (mgl_sin(r) * k - mgl_cos(r) * s > 0.0f) hi = mid; else lo = mid;
        }
        deg = (lo + hi) * 0.5f;
        GLRotatef(c, deg, x, y, z);
        return;
    }
    rotate_sc(c, sin_an, cos_an, x, y, z);
}

void GLScalef(GLcontext c, GLfloat x, GLfloat y, GLfloat z)
{
    float *t;
    int i;
    ULONG a[3];
    a[0] = mgl_fu(x); a[1] = mgl_fu(y); a[2] = mgl_fu(z);
    MGL_REC(c, OP_SCALE, 3, a);
    t = mgl_top(c);
    for (i = 0; i < 4; i++) { t[i] *= x; t[4 + i] *= y; t[8 + i] *= z; }
    changed(c);
}

void GLScaled(GLcontext c, GLdouble x, GLdouble y, GLdouble z) { GLScalef(c, (float)x, (float)y, (float)z); }

void GLTranslatef(GLcontext c, GLfloat x, GLfloat y, GLfloat z)
{
    float *t;
    int i;
    ULONG a[3];
    a[0] = mgl_fu(x); a[1] = mgl_fu(y); a[2] = mgl_fu(z);
    MGL_REC(c, OP_TRANSLATE, 3, a);
    t = mgl_top(c);
    for (i = 0; i < 4; i++) t[12 + i] += t[i] * x + t[4 + i] * y + t[8 + i] * z;
    changed(c);
}

void GLTranslated(GLcontext c, GLdouble x, GLdouble y, GLdouble z) { GLTranslatef(c, (float)x, (float)y, (float)z); }

void GLFrustum(GLcontext c, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    float m[16], *t;
    ULONG a[6];
    a[0] = mgl_fu((float)left); a[1] = mgl_fu((float)right); a[2] = mgl_fu((float)bottom);
    a[3] = mgl_fu((float)top); a[4] = mgl_fu((float)zNear); a[5] = mgl_fu((float)zFar);
    MGL_REC(c, OP_FRUSTUM, 6, a);
    if (zNear <= 0 || zFar <= 0 || left == right || bottom == top || zNear == zFar) { mgl_error(c, GL_INVALID_VALUE); return; }
    mgl_zero(m, sizeof m);
    m[0] = (float)(2.0 * zNear / (right - left));
    m[5] = (float)(2.0 * zNear / (top - bottom));
    m[8] = (float)((right + left) / (right - left));
    m[9] = (float)((top + bottom) / (top - bottom));
    m[10] = (float)(-(zFar + zNear) / (zFar - zNear));
    m[11] = -1.0f;
    m[14] = (float)(-2.0 * zFar * zNear / (zFar - zNear));
    t = mgl_top(c);
    mgl_mat_mul(t, t, m);
    changed(c);
}

void GLOrtho(GLcontext c, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    float m[16], *t;
    ULONG a[6];
    a[0] = mgl_fu((float)left); a[1] = mgl_fu((float)right); a[2] = mgl_fu((float)bottom);
    a[3] = mgl_fu((float)top); a[4] = mgl_fu((float)zNear); a[5] = mgl_fu((float)zFar);
    MGL_REC(c, OP_ORTHO, 6, a);
    if (left == right || bottom == top || zNear == zFar) { mgl_error(c, GL_INVALID_VALUE); return; }
    mgl_mat_identity(m);
    m[0] = (float)(2.0 / (right - left));
    m[5] = (float)(2.0 / (top - bottom));
    m[10] = (float)(-2.0 / (zFar - zNear));
    m[12] = (float)(-(right + left) / (right - left));
    m[13] = (float)(-(top + bottom) / (top - bottom));
    m[14] = (float)(-(zFar + zNear) / (zFar - zNear));
    t = mgl_top(c);
    mgl_mat_mul(t, t, m);
    changed(c);
}

/* ---- GLU ----------------------------------------------------------------------------------- */

void GLUPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
    GLcontext c = mgl_current;
    float f;
    if (!c) return;
    f = mgl_tan(fovy * (MGL_PI / 360.0f));
    if (f == 0.0f) return;
    GLFrustum(c, -znear * f * aspect, znear * f * aspect, -znear * f, znear * f, znear, zfar);
}

void GLULookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz)
{
    GLcontext c = mgl_current;
    float f[3], s[3], u[3], m[16], l;
    if (!c) return;
    f[0] = cx - ex; f[1] = cy - ey; f[2] = cz - ez;
    l = mgl_rsqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f[0] *= l; f[1] *= l; f[2] *= l;
    s[0] = f[1] * uz - f[2] * uy; s[1] = f[2] * ux - f[0] * uz; s[2] = f[0] * uy - f[1] * ux;
    l = mgl_rsqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s[0] *= l; s[1] *= l; s[2] *= l;
    u[0] = s[1] * f[2] - s[2] * f[1]; u[1] = s[2] * f[0] - s[0] * f[2]; u[2] = s[0] * f[1] - s[1] * f[0];
    mgl_mat_identity(m);
    m[0] = s[0]; m[4] = s[1]; m[8] = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9] = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    GLMultMatrixf(c, m);
    GLTranslatef(c, -ex, -ey, -ez);
}

/* ---- printing (for finding faults) ---------------------------------------------------------- */

static void print_matrix(const float *m)
{
#ifndef MGL_HOST
    int r;
    for (r = 0; r < 4; r++) {
        LONG v[4];
        int k;
        for (k = 0; k < 4; k++) v[k] = mgl_f2l(m[k * 4 + r] * 1000.0f);
        Printf((STRPTR)"  %8ld %8ld %8ld %8ld  (x1000)\n", v[0], v[1], v[2], v[3]);
    }
#else
    (void)m;
#endif
}

static int depth_of(GLcontext c, GLenum mode)
{
    return mode == GL_PROJECTION ? c->proj_top : mode == GL_TEXTURE ? c->tex_top[c->active] : c->mv_top;
}

static float *matrix_at(GLcontext c, GLenum mode, int i)
{
    return mode == GL_PROJECTION ? c->proj[i] : mode == GL_TEXTURE ? c->texm[c->active][i] : c->mv[i];
}

void MGLPrintMatrix(GLcontext c, int mode)
{
    if (!c) return;
    print_matrix(matrix_at(c, (GLenum)mode, depth_of(c, (GLenum)mode)));
}

void MGLPrintMatrixStack(GLcontext c, int mode)
{
    int i;
    if (!c) return;
    for (i = depth_of(c, (GLenum)mode); i >= 0; i--) print_matrix(matrix_at(c, (GLenum)mode, i));
}

/* ---- the inverse, for texture generation's eye planes ---------------------------------------- */

int mgl_mat_inverse(float *r, const float *m)
{
    float inv[16], det;
    int i;
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f) return 0;
    det = 1.0f / det;
    for (i = 0; i < 16; i++) r[i] = inv[i] * det;
    return 1;
}

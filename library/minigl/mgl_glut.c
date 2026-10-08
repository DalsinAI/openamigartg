/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: the part of GLUT it offers. One window (or a game mode
 * screen) made with the mgl* calls, GLUT's main loop over them, the
 * callbacks GLUT programs use most, glutGet's sizes and its clock, and the
 * solid shapes. */
#include "mgl_internal.h"

#ifndef MGL_HOST
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <dos/dos.h>
#endif

int mgl_events(GLcontext c, void (*glut_key)(int key), void (*glut_special)(int key));
ULONG mgl_event_signal(GLcontext c);

static struct {
    unsigned int mode;
    int x, y, w, h;
    void (*display)(void);
    void (*idle)(void);
    void (*keyboard)(unsigned char key, int x, int y);
    void (*reshape)(int w, int h);
    int redisplay;
    ULONG start;
    int game_w, game_h, game_depth, game_active;
    int shown_w, shown_h;
} glut;

void GLUTInit(int *argcp, char **argv)
{
    (void)argcp; (void)argv;
    glut.start = mgl_millis();
    if (!glut.w) glut.w = 300;
    if (!glut.h) glut.h = 300;
}

void GLUTInitDisplayMode(unsigned int mode) { glut.mode = mode; }
void GLUTInitWindowSize(int width, int height) { glut.w = width; glut.h = height; }
void GLUTInitWindowPosition(int x, int y) { glut.x = x; glut.y = y; }
void GLUTDisplayFunc(void (*func)(void)) { glut.display = func; glut.redisplay = 1; }
void GLUTIdleFunc(void (*func)(void)) { glut.idle = func; }
void GLUTKeyboardFunc(void (*func)(unsigned char key, int x, int y)) { glut.keyboard = func; }
void GLUTReshapeFunc(void (*func)(int width, int height)) { glut.reshape = func; }
void GLUTPostRedisplay(void) { glut.redisplay = 1; }

int GLUTCreateWindow(const char *title)
{
    GLcontext c;
    int keep = mgl_prefs.window;
    mgl_prefs.window = 1;
    if (glut.mode & GLUT_DEPTH) mgl_prefs.zbits = mgl_prefs.zbits ? mgl_prefs.zbits : 16;
    c = MGLCreateContext(glut.x, glut.y, glut.w > 0 ? glut.w : 300, glut.h > 0 ? glut.h : 300);
    mgl_prefs.window = keep;
    if (!c) return 0;
#ifndef MGL_HOST
    if (title && c->disp && MGLGetWindowHandle(c)) SetWindowTitles((struct Window *)MGLGetWindowHandle(c), (STRPTR)title, (STRPTR)-1);
#else
    (void)title;
#endif
    glut.redisplay = 1;
    return 1;
}

void GLUTSwapBuffers(void)
{
    if (mgl_current) MGLSwitchDisplay(mgl_current);
}

static void glut_key(int key)
{
    if (glut.keyboard) glut.keyboard((unsigned char)key, 0, 0);
}

void GLUTMainLoop(void)
{
    GLcontext c = mgl_current;
    if (!c) return;
    c->quit = 0;
    while (!c->quit && mgl_current == c) {
        if (glut.reshape && (glut.shown_w != c->width || glut.shown_h != c->height)) {
            glut.shown_w = c->width; glut.shown_h = c->height;
            glut.reshape(c->width, c->height);
            glut.redisplay = 1;
        } else if (!glut.reshape && (glut.shown_w != c->width || glut.shown_h != c->height)) {
            glut.shown_w = c->width; glut.shown_h = c->height;
            GLViewport(c, 0, 0, c->width, c->height);
        }
        if (!mgl_events(c, glut_key, 0)) break;
        if (c->quit || mgl_current != c) break;
        if (glut.redisplay && glut.display) { glut.redisplay = 0; glut.display(); }
        else if (glut.idle) glut.idle();
#ifndef MGL_HOST
        else {
            ULONG s = mgl_event_signal(c) | SIGBREAKF_CTRL_C;
            if (Wait(s) & SIGBREAKF_CTRL_C) break;
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) break;
#else
        else break;
#endif
    }
}

int GLUTGet(GLenum state)
{
    GLcontext c = mgl_current;
    switch (state) {
    case GLUT_ELAPSED_TIME: return (int)(mgl_millis() - glut.start);
    case GLUT_WINDOW_X: return glut.x;
    case GLUT_WINDOW_Y: return glut.y;
    case GLUT_WINDOW_WIDTH: return c ? c->width : 0;
    case GLUT_WINDOW_HEIGHT: return c ? c->height : 0;
    case GLUT_WINDOW_DOUBLEBUFFER: case GLUT_WINDOW_RGBA: case GLUT_DISPLAY_MODE_POSSIBLE: return 1;
    case GLUT_WINDOW_DEPTH_SIZE: return c ? (c->zfmt == OGPU_FMT_Z32 ? 32 : 16) : 0;
    case GLUT_WINDOW_RED_SIZE: case GLUT_WINDOW_BLUE_SIZE: return c && c->fmt == OGPU_FMT_RGB565 ? 5 : 8;
    case GLUT_WINDOW_GREEN_SIZE: return c && c->fmt == OGPU_FMT_RGB565 ? 6 : 8;
    case GLUT_WINDOW_BUFFER_SIZE: return c && c->fmt == OGPU_FMT_RGB565 ? 16 : 32;
#ifndef MGL_HOST
    case GLUT_SCREEN_WIDTH: {
        struct Screen *s = LockPubScreen(0); int w = s ? s->Width : 0;
        if (s) UnlockPubScreen(0, s);
        return w;
    }
    case GLUT_SCREEN_HEIGHT: {
        struct Screen *s = LockPubScreen(0); int h = s ? s->Height : 0;
        if (s) UnlockPubScreen(0, s);
        return h;
    }
#endif
    case GLUT_INIT_WINDOW_X: return glut.x;
    case GLUT_INIT_WINDOW_Y: return glut.y;
    case GLUT_INIT_WINDOW_WIDTH: return glut.w;
    case GLUT_INIT_WINDOW_HEIGHT: return glut.h;
    case GLUT_INIT_DISPLAY_MODE: return (int)glut.mode;
    case GLUT_WINDOW_CURSOR: return GLUT_CURSOR_INHERIT;
    }
    return 0;
}

/* "640x480:16@60", "640x480", ":32": the parts given. */
void GLUTGameModeString(const char *s)
{
    int v = 0, part = 0;
    if (!s) return;
    glut.game_w = 640; glut.game_h = 480; glut.game_depth = 16;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); continue; }
        if (v) {
            if (part == 0) glut.game_w = v;
            else if (part == 1) glut.game_h = v;
            else if (part == 2) glut.game_depth = v;
        }
        v = 0;
        if (*s == 'x') part = 1;
        else if (*s == ':') part = 2;
        else if (*s == '@') part = 3;
        if (!*s) break;
    }
}

int GLUTEnterGameMode(void)
{
    GLcontext c;
    int keep = mgl_prefs.window, keepd = mgl_prefs.depth;
    if (!glut.game_w) GLUTGameModeString("640x480:16");
    if (mgl_current) { MGLDeleteContext(mgl_current); }
    mgl_prefs.window = 0;
    mgl_prefs.depth = glut.game_depth;
    c = MGLCreateContext(0, 0, glut.game_w, glut.game_h);
    mgl_prefs.window = keep;
    mgl_prefs.depth = keepd;
    glut.game_active = c != 0;
    glut.redisplay = 1;
    return c ? 1 : 0;
}

void GLUTLeaveGameMode(void)
{
    if (glut.game_active && mgl_current) MGLDeleteContext(mgl_current);
    glut.game_active = 0;
}

int GLUTGameModeGet(GLenum query)
{
    switch (query) {
    case GLUT_GAME_MODE_ACTIVE: return glut.game_active;
    case GLUT_GAME_MODE_POSSIBLE: return 1;
    case GLUT_GAME_MODE_WIDTH: return glut.game_w;
    case GLUT_GAME_MODE_HEIGHT: return glut.game_h;
    case GLUT_GAME_MODE_PIXEL_DEPTH: return glut.game_depth;
    case GLUT_GAME_MODE_REFRESH_RATE: return 60;
    case GLUT_GAME_MODE_DISPLAY_CHANGED: return glut.game_active;
    }
    return 0;
}

/* ---- the solid shapes ----------------------------------------------------------------------- */

void GLUTSolidCube(GLdouble size)
{
    static const float n[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    static const signed char v[6][4][3] = {
        { { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } },
        { { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 } },
        { { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 } },
        { { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } },
        { { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
        { { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } },
    };
    GLcontext c = mgl_current;
    float h = (float)size * 0.5f;
    int f, k;
    if (!c) return;
    GLBegin(c, GL_QUADS);
    for (f = 0; f < 6; f++) {
        GLNormal3f(c, n[f][0], n[f][1], n[f][2]);
        for (k = 0; k < 4; k++) GLVertex4f(c, v[f][k][0] * h, v[f][k][1] * h, v[f][k][2] * h, 1.0f);
    }
    GLEnd(c);
}

static GLUquadricObj *shape_quadric(void)
{
    static GLUquadricObj *q;
    if (!q) q = GLUNewQuadric();
    return q;
}

void GLUTSolidSphere(GLdouble radius, GLint slices, GLint stacks)
{
    GLUquadricObj *q = shape_quadric();
    if (q) GLUSphere(q, radius, slices, stacks);
}

void GLUTSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks)
{
    GLUquadricObj *q = shape_quadric();
    if (!q || !mgl_current) return;
    GLUCylinder(q, base, 0.0, height, slices, stacks);
    /* the base, facing down */
    GLUQuadricOrientation(q, GLU_INSIDE);
    GLUDisk(q, 0.0, base, slices, 1);
    GLUQuadricOrientation(q, GLU_OUTSIDE);
}

void GLUTSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings)
{
    GLcontext c = mgl_current;
    float ri = (float)innerRadius, ro = (float)outerRadius;
    int i, j;
    if (!c || sides < 3 || rings < 3) return;
    for (i = 0; i < rings; i++) {
        float a0 = 2.0f * MGL_PI * i / rings, a1 = 2.0f * MGL_PI * (i + 1) / rings;
        float c0 = mgl_cos(a0), s0 = mgl_sin(a0), c1 = mgl_cos(a1), s1 = mgl_sin(a1);
        GLBegin(c, GL_QUAD_STRIP);
        for (j = 0; j <= sides; j++) {
            float b = 2.0f * MGL_PI * j / sides, cb = mgl_cos(b), sb = mgl_sin(b), r = ro + ri * cb;
            GLNormal3f(c, c1 * cb, s1 * cb, sb);
            GLVertex4f(c, c1 * r, s1 * r, ri * sb, 1.0f);
            GLNormal3f(c, c0 * cb, s0 * cb, sb);
            GLVertex4f(c, c0 * r, s0 * r, ri * sb, 1.0f);
        }
        GLEnd(c);
    }
}

/* The dodecahedron of radius sqrt(3): its 20 corners, and each face as the
 * five corners nearest the direction of one of the icosahedron's corners. */
void GLUTSolidDodecahedron(void)
{
    GLcontext c = mgl_current;
    const float p = 1.6180340f, ip = 0.6180340f;
    float v[20][3], n[12][3];
    int i, k, f;
    if (!c) return;
    for (i = 0; i < 8; i++) { v[i][0] = (i & 1) ? 1.0f : -1.0f; v[i][1] = (i & 2) ? 1.0f : -1.0f; v[i][2] = (i & 4) ? 1.0f : -1.0f; }
    for (i = 0; i < 4; i++) {
        float a = (i & 1) ? ip : -ip, b = (i & 2) ? p : -p;
        v[8 + i][0] = 0; v[8 + i][1] = a; v[8 + i][2] = b;
        v[12 + i][0] = a; v[12 + i][1] = b; v[12 + i][2] = 0;
        v[16 + i][0] = b; v[16 + i][1] = 0; v[16 + i][2] = a;
    }
    for (i = 0; i < 4; i++) {
        float a = (i & 1) ? 1.0f : -1.0f, b = (i & 2) ? p : -p, l = mgl_rsqrt(1.0f + p * p);
        n[i][0] = 0; n[i][1] = a * l; n[i][2] = b * l;
        n[4 + i][0] = a * l; n[4 + i][1] = b * l; n[4 + i][2] = 0;
        n[8 + i][0] = b * l; n[8 + i][1] = 0; n[8 + i][2] = a * l;
    }
    for (f = 0; f < 12; f++) {
        int idx[5], m = 0;
        float best = -10.0f, ang[5], ux, uy, uz, wx, wy, wz, l;
        for (i = 0; i < 20; i++) {
            float d = v[i][0] * n[f][0] + v[i][1] * n[f][1] + v[i][2] * n[f][2];
            if (d > best) best = d;
        }
        for (i = 0; i < 20 && m < 5; i++) {
            float d = v[i][0] * n[f][0] + v[i][1] * n[f][1] + v[i][2] * n[f][2];
            if (d > best - 0.01f) idx[m++] = i;
        }
        if (m < 5) continue;
        /* order the corners around the face's normal */
        ux = v[idx[0]][0] - n[f][0] * best; uy = v[idx[0]][1] - n[f][1] * best; uz = v[idx[0]][2] - n[f][2] * best;
        l = mgl_rsqrt(ux * ux + uy * uy + uz * uz); ux *= l; uy *= l; uz *= l;
        wx = n[f][1] * uz - n[f][2] * uy; wy = n[f][2] * ux - n[f][0] * uz; wz = n[f][0] * uy - n[f][1] * ux;
        for (k = 0; k < 5; k++) {
            float x = v[idx[k]][0], y = v[idx[k]][1], z = v[idx[k]][2];
            float cu = x * ux + y * uy + z * uz, cw = x * wx + y * wy + z * wz;
            /* the "diamond angle": 0..4 once round, in the angle's order */
            float a;
            if (cw >= 0) a = cu >= 0 ? cw / (cu + cw) : 1.0f - cu / (-cu + cw);
            else a = cu < 0 ? 2.0f - cw / (-cu - cw) : 3.0f + cu / (cu - cw);
            ang[k] = a;
        }
        for (i = 0; i < 5; i++)
            for (k = i + 1; k < 5; k++)
                if (ang[k] < ang[i]) { float t = ang[i]; int ti = idx[i]; ang[i] = ang[k]; idx[i] = idx[k]; ang[k] = t; idx[k] = ti; }
        GLBegin(c, GL_POLYGON);
        GLNormal3f(c, n[f][0], n[f][1], n[f][2]);
        for (k = 0; k < 5; k++) GLVertex4f(c, v[idx[k]][0], v[idx[k]][1], v[idx[k]][2], 1.0f);
        GLEnd(c);
    }
}

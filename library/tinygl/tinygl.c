/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libtinygl.a: TinyGL's context calls on OpenGPU's GL module. A TinyGL
 * program gets a GLContext from GLInit(), gives it a window, screen or
 * bitmap with GLAInitializeContext*, draws with GL's own calls and shows
 * each frame with GLASwapBuffers. Here each context is a GLA display,
 * context and buffer (gla/gla_core.h, in libGL.a): virgl on the GPU where
 * OpenGPU's back end has it, else Mesa's softpipe on the 68k, and its
 * frames go into the target's RastPort.
 *
 * ENV:TinyGL/Driver picks the route: CPU (softpipe), OpenGPU (the GPU, or
 * fail), or Auto (the default: the GPU when there is one).
 *
 * The Team wrote this from TinyGL's published interface (names and
 * arguments); no TinyGL code is in it. */
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/var.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include <tgl/gl.h>
#include <tgl/glu.h>
#include <gla/gla_core.h>
#include <gla/os3/gla_present_os3.h>
#include <gla/os3/gla_virgl_os3.h>

enum { TGL_NONE, TGL_WINDOW, TGL_SCREEN, TGL_BITMAP };

struct _GLContext {
    int kind;                       /* TGL_ */
    struct Window *window;
    struct Screen *screen;
    struct BitMap *bitmap;
    struct RastPort bm_rp;          /* a bitmap target's own RastPort */
    unsigned w, h;
    int stencil, sync;
    struct gla_config cfg;
    struct gla_os3_target target;
    struct gla_present hook;
    struct gla_display *d;
    struct gla_context *c;
    struct gla_buffer *b;
};

static GLContext *current;

GLContext *GLInit(void)
{
    return (GLContext *)calloc(1, sizeof(GLContext));
}

/* The route: 0 auto, 1 CPU, 2 OpenGPU only. */
static int driver(void)
{
    char v[16];
    if (GetVar((CONST_STRPTR) "TinyGL/Driver", (STRPTR)v, sizeof(v), 0) <= 0)
        return 0;
    if (!strcasecmp(v, "CPU"))
        return 1;
    if (!strcasecmp(v, "OpenGPU"))
        return 2;
    return 0;
}

/* Where the target's pixels go, and its size, from the target itself. */
static void measure(GLContext *g)
{
    if (g->kind == TGL_WINDOW) {
        struct Window *w = g->window;
        if (w->Flags & WFLG_GIMMEZEROZERO) {
            g->target.rp = w->RPort;        /* the inner RastPort */
            g->target.left = g->target.top = 0;
            g->w = w->GZZWidth;
            g->h = w->GZZHeight;
        } else {
            g->target.rp = w->RPort;
            g->target.left = w->BorderLeft;
            g->target.top = w->BorderTop;
            g->w = w->Width - w->BorderLeft - w->BorderRight;
            g->h = w->Height - w->BorderTop - w->BorderBottom;
        }
    } else if (g->kind == TGL_SCREEN) {
        g->target.rp = &g->screen->RastPort;
        g->target.left = g->target.top = 0;
        g->w = g->screen->Width;
        g->h = g->screen->Height;
    } else if (g->kind == TGL_BITMAP) {
        InitRastPort(&g->bm_rp);
        g->bm_rp.BitMap = g->bitmap;
        g->target.rp = &g->bm_rp;
        g->target.left = g->target.top = 0;
        g->w = GetBitMapAttr(g->bitmap, BMA_WIDTH);
        g->h = GetBitMapAttr(g->bitmap, BMA_HEIGHT);
    }
    g->target.bitmap = NULL;            /* through the RastPort: layers respected */
    if ((int)g->w < 1)
        g->w = 1;
    if ((int)g->h < 1)
        g->h = 1;
}

static void stop(GLContext *g)
{
    if (!g)
        return;
    if (current == g) {
        gla_make_current(NULL, NULL);
        current = NULL;
    }
    if (g->b)
        gla_buffer_destroy(g->b);
    if (g->c)
        gla_context_destroy(g->c);
    if (g->d)
        gla_display_destroy(g->d);
    g->b = NULL;
    g->c = NULL;
    g->d = NULL;
    g->kind = TGL_NONE;
}

static int start(GLContext *g)
{
    int route = driver();
    measure(g);
    gla_os3_present_init(&g->hook, &g->target);
    if (route != 1) {
        struct gla_virgl_transport t;
        if (gla_os3_virgl_transport(&t))
            g->d = gla_display_create_virgl(&t);
    }
    if (!g->d && route != 2)
        g->d = gla_display_create();
    g->cfg.profile = GLA_COMPAT;
    g->cfg.major = g->cfg.minor = 0;
    g->cfg.doublebuf = 1;
    g->cfg.depth = 24;
    g->cfg.stencil = g->stencil ? 8 : 0;
    g->cfg.alpha = 0;
    g->c = g->d ? gla_context_create(g->d, &g->cfg, NULL) : NULL;
    g->b = g->c ? gla_buffer_create(g->d, &g->cfg, g->w, g->h, &g->hook) : NULL;
    if (!g->b || !gla_make_current(g->c, g->b)) {
        stop(g);
        return 0;
    }
    current = g;
    return 1;
}

int GLAInitializeContext(GLContext *g, struct TagItem *tags)
{
    struct Screen *s;
    struct Window *w;
    struct BitMap *bm;
    if (!g)
        return 0;
    stop(g);
    s = (struct Screen *)GetTagData(TGL_CONTEXT_SCREEN, 0, tags);
    w = (struct Window *)GetTagData(TGL_CONTEXT_WINDOW, 0, tags);
    bm = (struct BitMap *)GetTagData(TGL_CONTEXT_BITMAP, 0, tags);
    g->stencil = GetTagData(TGL_CONTEXT_STENCIL, g->stencil, tags) != 0;
    g->screen = s;
    g->window = w;
    g->bitmap = bm;
    if (s)
        g->kind = TGL_SCREEN;
    else if (w)
        g->kind = TGL_WINDOW;
    else if (bm)
        g->kind = TGL_BITMAP;
    else
        return 0;
    return start(g);
}

static int init_one(GLContext *g, Tag tag, void *target)
{
    struct TagItem tags[2];
    tags[0].ti_Tag = tag;
    tags[0].ti_Data = (ULONG)target;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    return GLAInitializeContext(g, tags);
}

int GLAInitializeContextWindowed(GLContext *g, void *window)
{
    return init_one(g, TGL_CONTEXT_WINDOW, window);
}

int GLAInitializeContextScreen(GLContext *g, void *screen)
{
    return init_one(g, TGL_CONTEXT_SCREEN, screen);
}

int GLAInitializeContextBitMap(GLContext *g, void *bitmap)
{
    return init_one(g, TGL_CONTEXT_BITMAP, bitmap);
}

/* Keeps the GL state: only the buffer follows the window's size. */
int GLAReinitializeContextWindowed(GLContext *g, void *window)
{
    unsigned w, h;
    if (!g || !window)
        return 0;
    if (!g->b)
        return GLAInitializeContextWindowed(g, window);
    w = g->w;
    h = g->h;
    g->kind = TGL_WINDOW;
    g->window = (struct Window *)window;
    measure(g);
    if (g->w != w || g->h != h)
        gla_buffer_resize(g->b, g->w, g->h);
    return 1;
}

void GLADestroyContext(GLContext *g) { stop(g); }
void GLADestroyContextWindowed(GLContext *g) { stop(g); }
void GLADestroyContextScreen(GLContext *g) { stop(g); }
void GLADestroyContextBitMap(GLContext *g) { stop(g); }

void GLClose(GLContext *g)
{
    if (!g)
        return;
    stop(g);
    free(g);
}

void GLASwapBuffers(GLContext *g)
{
    if (!g || !g->b)
        return;
    if (current != g) {
        gla_make_current(g->c, g->b);
        current = g;
    }
    if (g->kind == TGL_WINDOW) {
        unsigned w = g->w, h = g->h;
        measure(g);
        if (g->w != w || g->h != h)
            gla_buffer_resize(g->b, g->w, g->h);
    }
    if (g->sync)
        WaitTOF();
    gla_swap(g->c, g->b);
}

void GLASetSync(GLContext *g, int enable)
{
    if (g)
        g->sync = enable != 0;
}

void GLASetAttr(GLContext *g, unsigned int attr, unsigned int value)
{
    (void)g;
    (void)attr;
    (void)value;
}

void *GLAGetProcAddress(GLContext *g, const char *name)
{
    (void)g;
    return gla_get_proc_address(name);
}

/* GLU: the few calls TinyGL programs use most. */

/* tan(x) for 0 <= x < pi/2 from sin and cos series: the 68040's FPU has no
 * ftan, fsin or fcos (they trap to software), so none is used. */
static GLdouble tan_half(GLdouble x)
{
    GLdouble x2 = x * x, s = x, c = 1, ts = x, tc = 1;
    int i;
    for (i = 1; i < 12; i++) {
        ts *= -x2 / ((2 * i) * (2 * i + 1));
        tc *= -x2 / ((2 * i - 1) * (2 * i));
        s += ts;
        c += tc;
    }
    return c != 0 ? s / c : 1e9;
}

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
    GLdouble half = fovy * 3.14159265358979323846 / 360.0;
    GLdouble top = zNear * tan_half(half);
    glFrustum(-top * aspect, top * aspect, -top, top, zNear, zFar);
}

static void normalize(GLdouble v[3])
{
    GLdouble l = __builtin_sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

static void cross(const GLdouble a[3], const GLdouble b[3], GLdouble r[3])
{
    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
}

void gluLookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz,
               GLdouble ux, GLdouble uy, GLdouble uz)
{
    GLdouble f[3], up[3], s[3], u[3], m[16];
    f[0] = cx - ex;
    f[1] = cy - ey;
    f[2] = cz - ez;
    normalize(f);
    up[0] = ux;
    up[1] = uy;
    up[2] = uz;
    cross(f, up, s);
    normalize(s);
    cross(s, f, u);
    memset(m, 0, sizeof(m));
    m[0] = s[0];  m[4] = s[1];  m[8] = s[2];
    m[1] = u[0];  m[5] = u[1];  m[9] = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    m[15] = 1;
    glMultMatrixd(m);
    glTranslated(-ex, -ey, -ez);
}

void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
    glOrtho(left, right, bottom, top, -1, 1);
}

GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height,
                        GLenum format, GLenum type, const void *data)
{
    GLenum e;
    while (glGetError() != GL_NO_ERROR)
        ;
    glTexParameteri(target, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(target, 0, internalFormat, width, height, 0, format, type, data);
    e = glGetError();
    return e == GL_NO_ERROR ? 0 : (GLint)e;
}

const GLubyte *gluErrorString(GLenum error)
{
    switch (error) {
    case GL_NO_ERROR: return (const GLubyte *)"no error";
    case GL_INVALID_ENUM: return (const GLubyte *)"invalid enumerant";
    case GL_INVALID_VALUE: return (const GLubyte *)"invalid value";
    case GL_INVALID_OPERATION: return (const GLubyte *)"invalid operation";
    case GL_STACK_OVERFLOW: return (const GLubyte *)"stack overflow";
    case GL_STACK_UNDERFLOW: return (const GLubyte *)"stack underflow";
    case GL_OUT_OF_MEMORY: return (const GLubyte *)"out of memory";
    }
    return NULL;
}

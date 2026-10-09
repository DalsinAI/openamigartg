/* Copyright (c) 2026 Dalsin Limited. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GLArrays: GL's vertex and index data in every width a program can give it,
 * checked by the picture. Each case fills the whole 64x64 buffer with one
 * colour, from client arrays (Mesa uploads them itself), from buffer
 * objects, with 8, 16 and 32-bit indices, unsigned-byte colours (RGBA and
 * BGRA), short positions and a texture; the presented frame's middle and a
 * corner must be that colour, within 8 on each channel.
 * On a big-endian Amiga, virgl's buffers reach the graphics chip as 32-bit
 * words, so data narrower than that has to be widened first; softpipe reads
 * the Amiga's own memory. Fixed-function GL, as games of Neverball's age
 * use it.
 *   GLArrays [CPU] [DEPTH 16|24] [STENCIL]
 *     CPU: softpipe (else virgl where there is one); DEPTH and STENCIL ask
 *     for those buffers too, as games do (Neverball: 16-bit depth and a
 *     stencil, for its reflections).
 * Prints one line per case and "GLArrays: n of m cases right on <driver>";
 * returns 0 when all are right, 5 when one isn't. Built by
 * library/modules/gl/module/build.sh against libGL.a, with both stoves. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <gla/gla_core.h>
#ifdef __amigaos__
#include <gla/os3/gla_virgl_os3.h>
#endif
#define GL_GLEXT_PROTOTYPES 0
#include <GL/gl.h>
#include <GL/glext.h>

#define S 64

static unsigned char shot[S * S * 3];      /* the presented frame: R, G, B */

static void present(void *user, const struct gla_frame *f)
{
    int r = f->format == GLA_ARGB ? 1 : 2, g = f->format == GLA_ARGB ? 2 : 1, b = f->format == GLA_ARGB ? 3 : 0;
    unsigned x, y;
    (void)user;
    for (y = 0; y < f->h && y < S; y++) {
        const unsigned char *row = (const unsigned char *)f->pixels + y * f->stride;
        for (x = 0; x < f->w && x < S; x++) {
            shot[(y * S + x) * 3 + 0] = row[x * 4 + r];
            shot[(y * S + x) * 3 + 1] = row[x * 4 + g];
            shot[(y * S + x) * 3 + 2] = row[x * 4 + b];
        }
    }
}

static const unsigned char *at(int x, int y) { return &shot[(y * S + x) * 3]; }

static PFNGLGENBUFFERSPROC pGenBuffers;
static PFNGLBINDBUFFERPROC pBindBuffer;
static PFNGLBUFFERDATAPROC pBufferData;
static PFNGLDELETEBUFFERSPROC pDeleteBuffers;
static PFNGLBUFFERSUBDATAPROC pBufferSubData;

/* A square over the whole buffer, as a fan of two triangles. In shorts
 * (with glOrtho(0, S, 0, S)), a band across the middle instead, full width
 * and half the height, so x and y the wrong way round show: the band would
 * stand upright. */
static const GLfloat pos_f[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
static const GLdouble pos_d[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
static const GLshort pos_s[] = { 0, S / 4, S, S / 4, S, 3 * S / 4, 0, 3 * S / 4 };
static const GLfloat uv[] = { 0, 0, 1, 0, 1, 1, 0, 1 };
static const GLubyte rgba[] = { 200, 100, 50, 255, 200, 100, 50, 255, 200, 100, 50, 255, 200, 100, 50, 255 };
static const GLubyte bgra[] = { 50, 100, 200, 255, 50, 100, 200, 255, 50, 100, 200, 255, 50, 100, 200, 255 };
static const GLfloat rgba_f[] = { 0.2f, 0.6f, 0.9f, 1, 0.2f, 0.6f, 0.9f, 1, 0.2f, 0.6f, 0.9f, 1, 0.2f, 0.6f, 0.9f, 1 };
static const GLushort idx16[] = { 0, 1, 2, 0, 2, 3 };
static const GLubyte idx8[] = { 0, 1, 2, 0, 2, 3 };
static const GLuint idx32[] = { 0, 1, 2, 0, 2, 3 };

enum { C_RGBA, C_BGRA, C_FLOAT, C_TEX, C_GUI };
enum { D_ARRAYS, D_U8, D_U16, D_U32 };

struct test {
    const char *name;
    int colour, draw, shorts, vbo;     /* shorts: 1 short positions, 2 double ones */
    unsigned char want[3];
};

static const struct test tests[] = {
    { "client arrays, float colours, glDrawArrays",          C_FLOAT, D_ARRAYS, 0, 0, { 51, 153, 230 } },
    { "client arrays, ubyte RGBA colours, glDrawArrays",     C_RGBA, D_ARRAYS, 0, 0, { 200, 100, 50 } },
    { "client arrays, ubyte RGBA colours, 16-bit indices",   C_RGBA, D_U16, 0, 0, { 200, 100, 50 } },
    { "client arrays, ubyte RGBA colours, 8-bit indices",    C_RGBA, D_U8, 0, 0, { 200, 100, 50 } },
    { "client arrays, ubyte RGBA colours, 32-bit indices",   C_RGBA, D_U32, 0, 0, { 200, 100, 50 } },
    { "client arrays, ubyte BGRA colours, 16-bit indices",   C_BGRA, D_U16, 0, 0, { 200, 100, 50 } },
    { "client arrays, short positions, 16-bit indices",      C_RGBA, D_U16, 1, 0, { 200, 100, 50 } },
    { "client arrays, double positions, glDrawArrays",       C_FLOAT, D_ARRAYS, 2, 0, { 51, 153, 230 } },
    { "client arrays, texture (RGBA8), 16-bit indices",      C_TEX, D_U16, 0, 0, { 40, 180, 90 } },
    { "buffer objects, ubyte RGBA colours, 16-bit indices",  C_RGBA, D_U16, 0, 1, { 200, 100, 50 } },
    { "buffer objects, short positions, 8-bit indices",      C_RGBA, D_U8, 1, 1, { 200, 100, 50 } },
    { "buffer objects, texture (RGBA8), glDrawArrays",       C_TEX, D_ARRAYS, 0, 1, { 40, 180, 90 } },
    /* texture (40, 180, 90) times colour (200, 100, 50) / 255 */
    { "Neverball's GUI: glBufferSubData, glDrawElements at an offset", C_GUI, D_U16, 1, 1, { 31, 71, 18 } },
    { "Neverball's GUI: glBufferSubData, glDrawArrays from a start",   C_GUI, D_ARRAYS, 1, 1, { 31, 71, 18 } },
};

/* Neverball's GUI (share/gui.c): one interleaved vertex buffer and one of
 * 16-bit indices for every widget, filled piece by piece with
 * glBufferSubData; a widget is drawn as a triangle strip of its indices
 * (glDrawElements at an offset into the buffer), its text with glDrawArrays
 * from a start. Here the third of four widgets is drawn, in both ways. */
struct gui_vert {
    GLubyte c[4];
    GLfloat u[2];
    GLshort p[2];
};
#define GUI_WIDGETS 4

static void draw_gui(int arrays, GLuint tex)
{
    struct gui_vert v[4];
    GLushort e[4];
    GLuint vbo, ebo;
    static const GLubyte grey[4] = { 30, 30, 30, 255 };
    int w, i;
    pGenBuffers(1, &vbo);
    pGenBuffers(1, &ebo);
    pBindBuffer(GL_ARRAY_BUFFER, vbo);
    pBufferData(GL_ARRAY_BUFFER, GUI_WIDGETS * sizeof v, NULL, GL_STATIC_DRAW);
    pBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    pBufferData(GL_ELEMENT_ARRAY_BUFFER, GUI_WIDGETS * sizeof e, NULL, GL_STATIC_DRAW);
    for (w = 0; w < GUI_WIDGETS; w++) {
        const GLubyte *c = w == 2 ? rgba : grey;
        for (i = 0; i < 4; i++) {
            memcpy(v[i].c, c, 4);
            v[i].u[0] = (GLfloat)(i & 1);
            v[i].u[1] = (GLfloat)(i >> 1);
            v[i].p[0] = (GLshort)((i & 1) * S);
            v[i].p[1] = (GLshort)(S / 4 + (i >> 1) * S / 2);    /* the band, as in the short cases */
            e[i] = (GLushort)(w * 4 + i);
        }
        pBufferSubData(GL_ARRAY_BUFFER, w * sizeof v, sizeof v, v);
        pBufferSubData(GL_ELEMENT_ARRAY_BUFFER, w * sizeof e, sizeof e, e);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, S, 0, S, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof (struct gui_vert), (GLvoid *)0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof (struct gui_vert), (GLvoid *)4);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_SHORT, sizeof (struct gui_vert), (GLvoid *)12);
    if (arrays)
        glDrawArrays(GL_TRIANGLE_STRIP, 2 * 4, 4);
    else
        glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, (const GLvoid *)(2 * sizeof e));
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    pBindBuffer(GL_ARRAY_BUFFER, 0);
    pBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    pDeleteBuffers(1, &vbo);
    pDeleteBuffers(1, &ebo);
}

static GLuint texture(void)
{
    static const GLubyte t[2 * 2 * 4] = { 40, 180, 90, 255, 40, 180, 90, 255, 40, 180, 90, 255, 40, 180, 90, 255 };
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);
    return id;
}

/* A buffer object holding data, or 0 with the data used as it is. */
static const void *array(int vbo, GLenum target, GLuint *ids, int *n, const void *data, GLsizeiptr size)
{
    if (!vbo) return data;
    pGenBuffers(1, &ids[*n]);
    pBindBuffer(target, ids[*n]);
    pBufferData(target, size, data, GL_STATIC_DRAW);
    (*n)++;
    return NULL;                    /* an offset of 0 into the bound buffer */
}

static void draw(const struct test *t, GLuint tex)
{
    GLuint ids[4];
    int n = 0;
    const void *p;
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (t->shorts == 1) glOrtho(0, S, 0, S, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnableClientState(GL_VERTEX_ARRAY);
    p = t->shorts == 1 ? array(t->vbo, GL_ARRAY_BUFFER, ids, &n, pos_s, sizeof pos_s)
        : t->shorts == 2 ? array(t->vbo, GL_ARRAY_BUFFER, ids, &n, pos_d, sizeof pos_d)
        : array(t->vbo, GL_ARRAY_BUFFER, ids, &n, pos_f, sizeof pos_f);
    glVertexPointer(2, t->shorts == 1 ? GL_SHORT : t->shorts == 2 ? GL_DOUBLE : GL_FLOAT, 0, p);
    if (t->colour == C_TEX) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, array(t->vbo, GL_ARRAY_BUFFER, ids, &n, uv, sizeof uv));
    } else {
        glEnableClientState(GL_COLOR_ARRAY);
        if (t->colour == C_FLOAT)
            glColorPointer(4, GL_FLOAT, 0, array(t->vbo, GL_ARRAY_BUFFER, ids, &n, rgba_f, sizeof rgba_f));
        else if (t->colour == C_BGRA)
            glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, 0, array(t->vbo, GL_ARRAY_BUFFER, ids, &n, bgra, sizeof bgra));
        else
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, array(t->vbo, GL_ARRAY_BUFFER, ids, &n, rgba, sizeof rgba));
    }
    if (t->draw == D_ARRAYS)
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    else if (t->draw == D_U8)
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, array(t->vbo, GL_ELEMENT_ARRAY_BUFFER, ids, &n, idx8, sizeof idx8));
    else if (t->draw == D_U16)
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, array(t->vbo, GL_ELEMENT_ARRAY_BUFFER, ids, &n, idx16, sizeof idx16));
    else
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, array(t->vbo, GL_ELEMENT_ARRAY_BUFFER, ids, &n, idx32, sizeof idx32));
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_TEXTURE_2D);
    if (t->vbo) {
        pBindBuffer(GL_ARRAY_BUFFER, 0);
        pBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        pDeleteBuffers(n, ids);
    }
}

static const unsigned char black[3] = { 0, 0, 0 };

static int near(const unsigned char *a, const unsigned char *b)
{
    int i;
    for (i = 0; i < 3; i++)
        if (a[i] > b[i] + 8 || b[i] > a[i] + 8) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    struct gla_config cfg = { GLA_COMPAT, 0, 0, 1, 0, 0, 0 };
    struct gla_present hook = { present, NULL };
    struct gla_display *d = NULL;
    struct gla_context *c = NULL;
    struct gla_buffer *b = NULL;
    int i, cpu = 0, right = 0, n = (int)(sizeof tests / sizeof tests[0]);
    GLuint tex;

    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "CPU")) cpu = 1;
        else if (!strcasecmp(argv[i], "DEPTH") && i + 1 < argc) cfg.depth = atoi(argv[++i]);
        else if (!strcasecmp(argv[i], "STENCIL")) cfg.stencil = 8;
    }

#ifdef __amigaos__
    if (!cpu) {
        struct gla_virgl_transport t;
        if (gla_os3_virgl_transport(&t)) d = gla_display_create_virgl(&t);
    }
#endif
    (void)cpu;
    if (!d) d = gla_display_create();
    c = d ? gla_context_create(d, &cfg, NULL) : NULL;
    b = c ? gla_buffer_create(d, &cfg, S, S, &hook) : NULL;
    if (!b || !gla_make_current(c, b)) {
        printf("GLArrays: GL couldn't start\n");
        return 20;
    }
    pGenBuffers = (PFNGLGENBUFFERSPROC)gla_get_proc_address("glGenBuffers");
    pBindBuffer = (PFNGLBINDBUFFERPROC)gla_get_proc_address("glBindBuffer");
    pBufferData = (PFNGLBUFFERDATAPROC)gla_get_proc_address("glBufferData");
    pDeleteBuffers = (PFNGLDELETEBUFFERSPROC)gla_get_proc_address("glDeleteBuffers");
    pBufferSubData = (PFNGLBUFFERSUBDATAPROC)gla_get_proc_address("glBufferSubData");
    {
        GLint db = 0, sb = 0;
        glGetIntegerv(GL_DEPTH_BITS, &db);
        glGetIntegerv(GL_STENCIL_BITS, &sb);
        printf("GLArrays: %s (%s), depth %d and stencil %d asked, %d and %d bits given\n", (const char *)glGetString(GL_RENDERER),
               gla_display_driver(d), cfg.depth, cfg.stencil, (int)db, (int)sb);
    }
    glViewport(0, 0, S, S);
    glClearColor(0, 0, 0, 1);
    tex = texture();
    for (i = 0; i < n; i++) {
        const struct test *t = &tests[i];
        int ok;
        if (t->vbo && (!pGenBuffers || !pBindBuffer || !pBufferData || !pDeleteBuffers)) {
            printf("--    %s: no buffer objects\n", t->name);
            continue;
        }
        const unsigned char *p, *q;
        memset(shot, 0, sizeof shot);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (t->colour == C_GUI) {
            if (!pBufferSubData) { printf("--    %s: no glBufferSubData\n", t->name); continue; }
            draw_gui(t->draw == D_ARRAYS, tex);
        } else
            draw(t, tex);
        gla_swap(c, b);
        if (t->shorts == 1) {
            /* inside the band at its left end; above it in the middle */
            p = at(S / 16, S / 2);
            q = at(S / 2, S / 16);
            ok = near(p, t->want) && near(q, black);
        } else {
            p = at(S / 2, S / 2);
            q = at(3, 3);
            ok = near(p, t->want) && near(q, t->want);
        }
        right += ok;
        printf("%s %s: %u,%u,%u and %u,%u,%u (want %u,%u,%u%s)\n", ok ? "ok:  " : "FAIL:", t->name,
               p[0], p[1], p[2], q[0], q[1], q[2], t->want[0], t->want[1], t->want[2], t->shorts == 1 ? ", then black" : "");
    }
    printf("GLArrays: %d of %d cases right on %s\n", right, n, gla_display_driver(d));
    glDeleteTextures(1, &tex);
    gla_make_current(NULL, NULL);
    gla_buffer_destroy(b);
    gla_context_destroy(c);
    gla_display_destroy(d);
    return right == n ? 0 : 5;
}

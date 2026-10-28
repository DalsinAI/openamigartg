/* Copyright (c) 2026 AmigaChrome Team. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GLA core draws four scenes with softpipe (GL 1.x immediate mode,
 * GLES 2 shaders, GL core with a vertex array, and GL core with a buffer and
 * a texture written again between many draws), each presented through the
 * hook, and their pictures' hashes are checked against a stored list. Then,
 * built with GLA_TEST_VIRGL and with ACVIRGL_LIB naming virglrenderer, the
 * same scenes through virgl on this machine's graphics chip (ACVirgl's host
 * side called directly, with "Amiga memory" an arena), each compared with
 * softpipe's picture: a graphics chip rounds a little differently, so a
 * picture matches when 99% of its pixels are within 8 of softpipe's on
 * every channel.
 * On this machine virgl runs twice: one command a block (GLA_VIRGL_BATCH=1,
 * as before batching) and batched. Both must give the same pictures, byte
 * for byte, and the same submits and puts on the host; batched, no block may
 * hold more than 64 commands or 64 KB of data unless it holds one command.
 *   test_gla [golden.txt] [dir]    dir: also write each picture as a .ppm */
#if !defined(__amigaos__)
#define _POSIX_C_SOURCE 200112L     /* setenv: virgl runs with and without batching */
#endif
#include "../gla/gla_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_GLEXT_PROTOTYPES 0
#include <GL/gl.h>
#include <GL/glext.h>

#define W 160
#define H 120

#define NSCENES 4

static unsigned char shot[W * H * 3];   /* R, G, B */
static unsigned char soft[NSCENES][W * H * 3];   /* softpipe's pictures */
static int presents;

static void present(void *user, const struct gla_frame *f)
{
    unsigned x, y;
    int r = f->format == GLA_ARGB ? 1 : 2, g = f->format == GLA_ARGB ? 2 : 1, b = f->format == GLA_ARGB ? 3 : 0;
    (void)user;
    for (y = 0; y < f->h && y < H; y++) {
        const unsigned char *row = (const unsigned char *)f->pixels + y * f->stride;
        for (x = 0; x < f->w && x < W; x++) {
            shot[(y * W + x) * 3 + 0] = row[x * 4 + r];
            shot[(y * W + x) * 3 + 1] = row[x * 4 + g];
            shot[(y * W + x) * 3 + 2] = row[x * 4 + b];
        }
    }
    presents++;
}

static unsigned long hash_shot(void)
{
    unsigned long h = 2166136261u;
    int i;
    for (i = 0; i < W * H * 3; i++)
        h = ((h ^ shot[i]) * 16777619u) & 0xffffffffu;
    return h;
}

static void save_ppm(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(shot, 1, sizeof shot, f);
    fclose(f);
}

static PFNGLCREATESHADERPROC pCreateShader;
static PFNGLSHADERSOURCEPROC pShaderSource;
static PFNGLCOMPILESHADERPROC pCompileShader;
static PFNGLGETSHADERIVPROC pGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC pGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC pCreateProgram;
static PFNGLATTACHSHADERPROC pAttachShader;
static PFNGLBINDATTRIBLOCATIONPROC pBindAttribLocation;
static PFNGLLINKPROGRAMPROC pLinkProgram;
static PFNGLGETPROGRAMIVPROC pGetProgramiv;
static PFNGLUSEPROGRAMPROC pUseProgram;
static PFNGLVERTEXATTRIBPOINTERPROC pVertexAttribPointer;
static PFNGLENABLEVERTEXATTRIBARRAYPROC pEnableVertexAttribArray;
static PFNGLGENBUFFERSPROC pGenBuffers;
static PFNGLBINDBUFFERPROC pBindBuffer;
static PFNGLBUFFERDATAPROC pBufferData;
static PFNGLBUFFERSUBDATAPROC pBufferSubData;
static PFNGLGENVERTEXARRAYSPROC pGenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC pBindVertexArray;
static PFNGLGETUNIFORMLOCATIONPROC pGetUniformLocation;
static PFNGLUNIFORM1FPROC pUniform1f;

#define GET(p, n) do { *(void **)&p = gla_get_proc_address(n); if (!p) { printf("no %s\n", n); return 1; } } while (0)

static int get_procs(void)
{
    GET(pCreateShader, "glCreateShader");
    GET(pShaderSource, "glShaderSource");
    GET(pCompileShader, "glCompileShader");
    GET(pGetShaderiv, "glGetShaderiv");
    GET(pGetShaderInfoLog, "glGetShaderInfoLog");
    GET(pCreateProgram, "glCreateProgram");
    GET(pAttachShader, "glAttachShader");
    GET(pBindAttribLocation, "glBindAttribLocation");
    GET(pLinkProgram, "glLinkProgram");
    GET(pGetProgramiv, "glGetProgramiv");
    GET(pUseProgram, "glUseProgram");
    GET(pVertexAttribPointer, "glVertexAttribPointer");
    GET(pEnableVertexAttribArray, "glEnableVertexAttribArray");
    GET(pGenBuffers, "glGenBuffers");
    GET(pBindBuffer, "glBindBuffer");
    GET(pBufferData, "glBufferData");
    GET(pBufferSubData, "glBufferSubData");
    GET(pGenVertexArrays, "glGenVertexArrays");
    GET(pBindVertexArray, "glBindVertexArray");
    GET(pGetUniformLocation, "glGetUniformLocation");
    GET(pUniform1f, "glUniform1f");
    return 0;
}

static GLuint program(const char *vs, const char *fs)
{
    const char *src[2] = { vs, fs };
    GLenum kind[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint p = pCreateProgram();
    GLint ok;
    int i;
    for (i = 0; i < 2; i++) {
        GLuint s = pCreateShader(kind[i]);
        pShaderSource(s, 1, &src[i], NULL);
        pCompileShader(s);
        pGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            pGetShaderInfoLog(s, sizeof log, NULL, log);
            printf("shader: %s\n", log);
            return 0;
        }
        pAttachShader(p, s);
    }
    pBindAttribLocation(p, 0, "pos");
    pBindAttribLocation(p, 1, "col");
    pLinkProgram(p);
    pGetProgramiv(p, GL_LINK_STATUS, &ok);
    return ok ? p : 0;
}

static const GLfloat tri[] = {
    -0.8f, -0.7f,  1, 0, 0,
     0.8f, -0.5f,  0, 1, 0,
     0.1f,  0.8f,  0, 0, 1,
};

/* 1: GL 1.x, depth test, a smooth triangle under a flat quad. */
static void scene_fixed(void)
{
    glViewport(0, 0, W, H);
    glClearColor(0.1f, 0.2f, 0.3f, 1);
    glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex3f(-0.8f, -0.7f, 0.5f);
    glColor3f(0, 1, 0); glVertex3f(0.8f, -0.5f, 0.5f);
    glColor3f(0, 0, 1); glVertex3f(0.1f, 0.8f, 0.5f);
    glEnd();
    glColor3f(1, 1, 0);
    glBegin(GL_QUADS);
    glVertex3f(-0.3f, -0.3f, 0.2f); glVertex3f(0.3f, -0.3f, 0.2f);
    glVertex3f(0.3f, 0.3f, 0.8f); glVertex3f(-0.3f, 0.3f, 0.8f);
    glEnd();
}

/* 2: GLES 2 shaders from a client-side array. */
static int scene_es2(void)
{
    GLuint p = program(
        "attribute vec2 pos; attribute vec3 col; varying vec3 v;\n"
        "void main() { v = col; gl_Position = vec4(pos, 0.0, 1.0); }\n",
        "precision mediump float; varying vec3 v; uniform float k;\n"
        "void main() { gl_FragColor = vec4(v * k, 1.0); }\n");
    if (!p)
        return 1;
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    pUseProgram(p);
    pUniform1f(pGetUniformLocation(p, "k"), 0.75f);
    pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri);
    pVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri + 2);
    pEnableVertexAttribArray(0);
    pEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return 0;
}

/* 3: GL 3.3 core: a vertex array object and a buffer. */
static int scene_core(void)
{
    GLuint vao, vbo, p = program(
        "#version 330 core\nin vec2 pos; in vec3 col; out vec3 v;\n"
        "void main() { v = col; gl_Position = vec4(pos.y, -pos.x, 0.0, 1.0); }\n",
        "#version 330 core\nin vec3 v; out vec4 o;\n"
        "void main() { o = vec4(v.bgr, 1.0); }\n");
    if (!p)
        return 1;
    pGenVertexArrays(1, &vao);
    pBindVertexArray(vao);
    pGenBuffers(1, &vbo);
    pBindBuffer(GL_ARRAY_BUFFER, vbo);
    pBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
    pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void *)0);
    pVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
    pEnableVertexAttribArray(0);
    pEnableVertexAttribArray(1);
    glViewport(0, 0, W, H);
    glClearColor(0.5f, 0.5f, 0.5f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    pUseProgram(p);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return 0;
}

/* A square from x0, y0 to x1, y1 (triangle strip), its texture coordinates
 * u0, v0 to u1, v1: pos and col, two floats each. */
static void square(GLfloat *q, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
{
    GLfloat v[16] = { x0, y0, u0, v0,  x1, y0, u1, v0,  x0, y1, u0, v1,  x1, y1, u1, v1 };
    memcpy(q, v, sizeof v);
}

/* 4: GL 3.3 core, one buffer and one texture written again between draws
 * (glBufferSubData, glTexSubImage2D), 120 draws in one frame, and a texture
 * bigger than a block carries (256 KB). Each draw must see the data as they
 * were when it was drawn: virgl queues the writes and the draws. */
static int scene_rewrites(void)
{
    static GLubyte big[256 * 256 * 4];
    GLubyte dot[4 * 4 * 4];
    GLfloat q[16];
    GLuint vao, vbo, tex, p = program(
        "#version 330 core\nin vec2 pos; in vec2 col; out vec2 t;\n"
        "void main() { t = col; gl_Position = vec4(pos, 0.0, 1.0); }\n",
        "#version 330 core\nin vec2 t; uniform sampler2D s; out vec4 o;\n"
        "void main() { o = texture(s, t); }\n");
    int i, x, y;
    const float u0 = 64.5f / 256, u1 = 67.5f / 256;     /* the 4x4 dot at texel 64, 64 */
    if (!p)
        return 1;
    for (y = 0; y < 256; y++)
        for (x = 0; x < 256; x++) {
            GLubyte *px = big + (y * 256 + x) * 4;
            px[0] = (GLubyte)x; px[1] = (GLubyte)y; px[2] = (GLubyte)((x ^ y) * 4); px[3] = 255;
        }
    pGenVertexArrays(1, &vao);
    pBindVertexArray(vao);
    pGenBuffers(1, &vbo);
    pBindBuffer(GL_ARRAY_BUFFER, vbo);
    pBufferData(GL_ARRAY_BUFFER, sizeof q, NULL, GL_DYNAMIC_DRAW);
    pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
    pVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
    pEnableVertexAttribArray(0);
    pEnableVertexAttribArray(1);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, big);
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    pUseProgram(p);
    /* the whole texture behind, then 120 squares of the dot, each drawn from
     * the buffer written again; every eighth, the dot gets a new colour */
    square(q, -1, -1, 1, 1, 0, 0, 1, 1);
    pBufferSubData(GL_ARRAY_BUFFER, 0, sizeof q, q);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    for (i = 0; i < 120; i++) {
        float x0 = -1 + (float)(i % 12) * (2.0f / 12), y0 = -1 + (float)(i / 12) * (2.0f / 10);
        if (i % 8 == 0) {
            for (x = 0; x < 16; x++) {
                dot[x * 4 + 0] = (GLubyte)(i * 2); dot[x * 4 + 1] = (GLubyte)(255 - i * 2);
                dot[x * 4 + 2] = (GLubyte)(i * 37); dot[x * 4 + 3] = 255;
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 64, 64, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, dot);
        }
        square(q, x0 + 0.02f, y0 + 0.03f, x0 + 0.14f, y0 + 0.17f, u0, u0, u1, u1);
        pBufferSubData(GL_ARRAY_BUFFER, 0, sizeof q, q);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    return 0;
}

struct scene {
    const char *name;
    struct gla_config cfg;
    int (*draw)(void);
};

static int draw_fixed(void) { scene_fixed(); return 0; }

static const struct scene scenes[] = {
    { "gl1-fixed", { GLA_COMPAT, 0, 0, 1, 24, 0, 0 }, draw_fixed },
    { "es2-shaders", { GLA_ES2, 2, 0, 1, 0, 0, 0 }, scene_es2 },
    { "gl33-core", { GLA_CORE, 3, 3, 0, 0, 0, 0 }, scene_core },
    { "gl33-rewrites", { GLA_CORE, 3, 3, 0, 0, 0, 0 }, scene_rewrites },
};

#if defined(__amigaos__)
/* libnix gives main a stack this big, whatever the Shell's Stack is. */
unsigned long __stack = 1024 * 1024;
#endif

/* How far the test got, flushed at once, so a crash still shows it. */
static void step(const char *what)
{
    printf("gla: %s\n", what);
    fflush(stdout);
}

#if defined(__amigaos__)
#include "../gla/os3/gla_virgl_os3.h"
#define GLA_TEST_VIRGL 1
#elif defined(GLA_TEST_VIRGL)
#include "host/virgl/acvirgl.h"
/* "Amiga memory" for the host side: an arena, addresses from 16 MiB. */
#define ARENA (64u << 20)
#define BASE 0x01000000u
static unsigned char *arena;
static unsigned long arena_used;
static struct acvirgl *host;
static void *t_alloc(void *u, unsigned long n)
{
    unsigned long at = (arena_used + 63) & ~63ul;
    (void)u;
    if (at + n > ARENA) return NULL;
    arena_used = at + n;
    memset(arena + at, 0, n);
    return arena + at;
}
static void t_free(void *u, void *p) { (void)u; (void)p; }
static unsigned long t_addr(void *u, const void *p) { (void)u; return BASE + (unsigned long)((const unsigned char *)p - arena); }
static uint8_t *t_map(void *u, uint32_t a, uint32_t len) { (void)u; return a >= BASE && a - BASE <= ARENA && len <= ARENA - (a - BASE) ? arena + (a - BASE) : NULL; }
/* Each block counted, and checked against the caps: at most 64 commands and
 * 64 KB of data (PUT bytes, SUBMIT words) unless it holds one command. */
static unsigned long t_blocks, t_cmds, t_over;
static unsigned t_most;
static uint32_t t_be(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int t_run(void *u, void *block, unsigned long bytes)
{
    const unsigned char *b = block;
    unsigned long at = 0, n = 0, data = 0;
    (void)u;
    while (at + 8 <= bytes) {
        uint32_t hdr = t_be(b + at), len = hdr & 0xFFFFu;
        if (len < 2 || at + len * 4 > bytes) break;
        if (hdr >> 16 == 11 && len >= 16) data += t_be(b + at + 56);            /* ACV_PUT: its bytes */
        if (hdr >> 16 == 10 && len >= 5) data += 4ul * t_be(b + at + 16);       /* ACV_SUBMIT: its words */
        n++;
        at += len * 4;
    }
    t_blocks++;
    t_cmds += n;
    if (n > t_most) t_most = (unsigned)n;
    if (n > 1 && (n > 64 || data > 65536)) t_over++;
    return acvirgl_run(host, t_map, NULL, (uint32_t)t_addr(NULL, block), (uint32_t)bytes);
}

/* The direct present (ACV_PRESENT) as openrtg.library sends it, into an
 * XRGB32 "screen" in the arena: one rectangle, the frame's changed part. */
#include "include/opengpu/virgl.h"
#include "include/opengpu/stream.h"
static unsigned char *screen;
static uint32_t be(uint32_t v)
{
    const unsigned char b[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
    uint32_t r;
    memcpy(&r, b, 4);
    return r;
}
static long present_direct(void *user, const struct gla_virgl_frame *f)
{
    static uint32_t *w;
    unsigned x, y;
    int32_t r;
    (void)user;
    if (!w && !(w = t_alloc(NULL, 16 * 4))) return -1;
    w[0] = be(ACV_HDR(ACV_PRESENT, 14)); w[1] = 0;
    w[2] = be((uint32_t)f->context); w[3] = be((uint32_t)f->handle); w[4] = 0;
    w[5] = 0;                                       /* the frame at the screen's 0, 0 */
    w[6] = be((uint32_t)t_addr(NULL, screen)); w[7] = be(W * 4); w[8] = be(OGPU_FMT_XRGB32);
    w[9] = be((W << 16) | H); w[10] = 0; w[11] = be(1);
    w[12] = be(((uint32_t)f->x0 << 16) | (uint32_t)f->y0);
    w[13] = be(((uint32_t)(f->x1 - f->x0) << 16) | (uint32_t)(f->y1 - f->y0));
    if (t_run(NULL, w, 14 * 4)) return -1;
    r = (int32_t)be(w[1]);
    if (r < 0) return -1;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            const unsigned char *px = screen + (y * W + x) * 4;
            if (px[0]) return -1;                   /* X is written 0 */
            shot[(y * W + x) * 3 + 0] = px[1];
            shot[(y * W + x) * 3 + 1] = px[2];
            shot[(y * W + x) * 3 + 2] = px[3];
        }
    presents++;
    return r;
}
#endif
#if defined(GLA_TEST_VIRGL)
/* The transport: OpenGPU's ring on the Amiga, the host side called directly here. */
static int virgl_transport(struct gla_virgl_transport *t, char *why, int whylen)
{
#if defined(__amigaos__)
    if (gla_os3_virgl_transport(t)) return 1;
    snprintf(why, whylen, "opengpu.library has no OGPU_OP_VIRGL here");
    return 0;
#else
    if (!(host = acvirgl_create(NULL, why, whylen))) return 0;
    if (!(arena = calloc(1, ARENA))) { snprintf(why, whylen, "no memory"); return 0; }
    t->alloc = t_alloc; t->free = t_free; t->addr = t_addr; t->run = t_run; t->user = NULL;
    return 1;
#endif
}
#endif

/* The three scenes on a display; their hashes in got, pictures in keep if given. */
static int run_scenes(struct gla_display *d, unsigned long *got, unsigned char (*keep)[W * H * 3], const char *dir, const char *tag,
                      long (*direct)(void *, const struct gla_virgl_frame *))
{
    struct gla_present hook = { present, NULL };
    int i, fails = 0;
    gla_present_set_virgl(present, direct);     /* NULL: no direct present */
    printf("gla: %s gives GL %d (compat), %d (core), GLES %d\n", gla_display_driver(d),
           gla_display_version(d, GLA_COMPAT), gla_display_version(d, GLA_CORE),
           gla_display_version(d, GLA_ES2));
    for (i = 0; i < NSCENES; i++) {
        const struct scene *s = &scenes[i];
        struct gla_context *c;
        struct gla_buffer *b;
        int before = presents;
        char nm[96];
        step(s->name);
        c = gla_context_create(d, &s->cfg, NULL);
        b = gla_buffer_create(d, &s->cfg, W, H, &hook);
        if (!c || !b || !gla_make_current(c, b)) {
            printf("%s: no context\n", s->name);
            return -1;
        }
        if (get_procs())
            return -1;
        if (s->draw())
            return -1;
        if (glGetError() != GL_NO_ERROR) {
            printf("%s: GL error\n", s->name);
            fails++;
        }
        if (s->cfg.doublebuf)
            gla_swap(c, b);
        else
            glFinish();
        if (presents == before) {
            printf("%s: nothing presented\n", s->name);
            fails++;
        }
        got[i] = hash_shot();
        if (keep) memcpy(keep[i], shot, sizeof shot);
        printf("%s (%s): %s, picture %08lx\n", s->name, tag, (const char *)glGetString(GL_VERSION), got[i]);
        snprintf(nm, sizeof nm, "%s-%s", s->name, tag);
        save_ppm(dir, nm);
        gla_make_current(NULL, NULL);
        gla_buffer_destroy(b);
        gla_context_destroy(c);
    }
    return fails;
}

/* How close a picture is to softpipe's: the share of pixels within 8 on
 * every channel, in tenths of a percent. */
static int closeness(const unsigned char *a, const unsigned char *b)
{
    int i, near = 0;
    for (i = 0; i < W * H; i++) {
        int k, ok = 1;
        for (k = 0; k < 3; k++) { int dd = a[i * 3 + k] - b[i * 3 + k]; if (dd < -8 || dd > 8) ok = 0; }
        near += ok;
    }
    return near * 1000 / (W * H);
}

int main(int argc, char **argv)
{
    const char *golden = argc > 1 ? argv[1] : NULL;
    const char *dir = argc > 2 ? argv[2] : NULL;
    struct gla_display *d;
    unsigned long got[NSCENES];
    int i, fails = 0, r;

    step("start");
    d = gla_display_create();
    step("display made");
    if (!d) {
        printf("gla: no display\n");
        return 1;
    }
    if ((r = run_scenes(d, got, soft, dir, "softpipe", NULL)) < 0) return 1;
    fails += r;
    gla_display_destroy(d);

    if (golden) {
        FILE *f = fopen(golden, "r");
        char name[64];
        unsigned long want;
        int seen = 0;
        if (!f) {
            printf("no %s\n", golden);
            return 1;
        }
        while (fscanf(f, "%63s %lx", name, &want) == 2)
            for (i = 0; i < NSCENES; i++)
                if (!strcmp(name, scenes[i].name)) {
                    seen++;
                    if (got[i] != want) {
                        printf("%s: picture %08lx, stored %08lx\n", name, got[i], want);
                        fails++;
                    }
                }
        fclose(f);
        if (seen != NSCENES) {
            printf("%s has %d of the %d scenes\n", golden, seen, NSCENES);
            fails++;
        }
    }

#ifdef GLA_TEST_VIRGL
    {
        char err[256];
        static unsigned char gpu[NSCENES][W * H * 3];
        struct gla_virgl_transport t;
        int pass, passes = 1;
#if !defined(__amigaos__)
        static unsigned char gpu1[NSCENES][W * H * 3];
        struct acvirgl_stats hs[2], he[2];
        unsigned long blocks[2] = { 0, 0 }, cmds[2] = { 0, 0 };
        passes = 2;
#endif
        if (!virgl_transport(&t, err, sizeof err)) {
            printf("gla: virgl skipped (%s)\n", err);
            passes = 0;
        }
        /* on this machine: one command a block first (GLA_VIRGL_BATCH=1, as
         * before batching), then batched, as GL.module runs */
        for (pass = 0; pass < passes; pass++) {
            const char *tag = passes == 2 && !pass ? "virgl-1" : "virgl";
#if !defined(__amigaos__)
            if (passes == 2 && !pass) setenv("GLA_VIRGL_BATCH", "1", 1);
            else unsetenv("GLA_VIRGL_BATCH");
            acvirgl_stats(host, &hs[pass]);
            t_blocks = t_cmds = t_over = 0;
            t_most = 0;
#endif
            if (!(d = gla_display_create_virgl(&t))) {
                printf("gla: virgl: no display\n");
                fails++;
                break;
            } else {
                struct gla_stats st;
                gla_get_stats(NULL, 1);
                if ((r = run_scenes(d, got, gpu, dir, tag, NULL)) < 0) return 1;
                fails += r;
                for (i = 0; i < NSCENES; i++) {
                    int c = closeness(gpu[i], soft[i]);
                    printf("%s: %s's picture is %d.%d%% softpipe's\n", scenes[i].name, tag, c / 10, c % 10);
                    if (c < 990) fails++;
                }
                gla_get_stats(&st, 1);
                if (!st.frames || st.direct || st.readback != st.frames || st.direct_state != GLA_DIRECT_NO_HOOK) {
                    printf("gla: virgl read back %lu of %lu frames\n", st.readback, st.frames);
                    fails++;
                }
#if !defined(__amigaos__)
                blocks[pass] = t_blocks;
                cmds[pass] = t_cmds;
                acvirgl_stats(host, &he[pass]);
                printf("gla: %s: %lu blocks for %lu commands, at most %u a block, %lu over the caps\n",
                       tag, t_blocks, t_cmds, t_most, t_over);
                if (t_over) fails++;
                if (!pass) {
                    memcpy(gpu1, gpu, sizeof gpu);
                    if (t_most > 2) fails++;            /* HELLO and CTX_CREATE share one */
                } else {
                    for (i = 0; i < NSCENES; i++)
                        if (memcmp(gpu[i], gpu1[i], sizeof gpu[i])) {
                            printf("%s: batched, virgl's picture isn't the one of one command a block\n", scenes[i].name);
                            fails++;
                        }
                    /* the same submits and puts reach the host, in fewer blocks */
#define HD(k, f) ((unsigned long long)(he[k].f - hs[k].f))
                    printf("gla: on the host, one a block: %llu submits (%llu words), %llu puts (%llu bytes); batched: %llu (%llu), %llu (%llu)\n",
                           HD(0, submits), HD(0, submit_words), HD(0, puts), HD(0, put_bytes),
                           HD(1, submits), HD(1, submit_words), HD(1, puts), HD(1, put_bytes));
                    if (HD(0, submits) != HD(1, submits) || HD(0, submit_words) != HD(1, submit_words) ||
                        HD(0, puts) != HD(1, puts) || HD(0, put_bytes) != HD(1, put_bytes)) {
                        printf("gla: batched, the host didn't get the same submits and puts\n");
                        fails++;
                    }
                    if (he[1].errors != hs[0].errors) {
                        printf("gla: virgl: %llu errors on the host\n", (unsigned long long)(he[1].errors - hs[0].errors));
                        fails++;
                    }
#undef HD
                    if (blocks[1] * 2 > blocks[0] || cmds[1] != cmds[0]) {
                        printf("gla: batched, %lu blocks for %lu commands, against %lu for %lu\n", blocks[1], cmds[1], blocks[0], cmds[0]);
                        fails++;
                    }
                    /* The same scenes again, the host writing each frame straight
                     * into the screen (ACV_PRESENT): the pictures byte for byte the
                     * read-back ones, and nothing read back. */
                    {
                        unsigned long direct[NSCENES];
                        if (!(screen = t_alloc(NULL, W * H * 4))) return 1;
                        if ((r = run_scenes(d, direct, NULL, dir, "virgl-direct", present_direct)) < 0) return 1;
                        fails += r;
                        for (i = 0; i < NSCENES; i++)
                            if (direct[i] != got[i]) {
                                printf("%s: the direct present's picture %08lx, read back %08lx\n", scenes[i].name, direct[i], got[i]);
                                fails++;
                            }
                        gla_get_stats(&st, 1);
                        printf("gla: virgl direct: %lu frames, %lu straight in (%lu pixels), %lu read back\n",
                               st.frames, st.direct, st.direct_pixels, st.readback);
                        if (!st.frames || st.direct != st.frames || st.readback || st.direct_state != GLA_DIRECT_ON) fails++;
                    }
                }
#endif
                gla_display_destroy(d);
            }
        }
    }
#endif
    printf(fails ? "gla: %d failed\n" : "gla: all tests passed\n", fails);
    return fails != 0;
}

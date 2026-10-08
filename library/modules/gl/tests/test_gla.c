/* Copyright (c) 2026 Dalsin Limited. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GLA core draws three scenes with softpipe (GL 1.x immediate mode,
 * GLES 2 shaders, GL core with a vertex array), each presented through the
 * hook, and their pictures' hashes are checked against a stored list. Then,
 * built with GLA_TEST_VIRGL and with ACVIRGL_LIB naming virglrenderer, the
 * same scenes through virgl on this machine's graphics chip (ACVirgl's host
 * side called directly, with "Amiga memory" an arena), each compared with
 * softpipe's picture: a graphics chip rounds a little differently, so a
 * picture matches when 99% of its pixels are within 8 of softpipe's on
 * every channel.
 *   test_gla [golden.txt] [dir]    dir: also write each picture as a .ppm */
#include "../gla/gla_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_GLEXT_PROTOTYPES 0
#include <GL/gl.h>
#include <GL/glext.h>

#define W 160
#define H 120

static unsigned char shot[W * H * 3];   /* R, G, B */
static unsigned char soft[3][W * H * 3];   /* softpipe's pictures */
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
static int t_run(void *u, void *block, unsigned long bytes) { (void)u; return acvirgl_run(host, t_map, NULL, (uint32_t)t_addr(NULL, block), (uint32_t)bytes); }
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
static int run_scenes(struct gla_display *d, unsigned long *got, unsigned char (*keep)[W * H * 3], const char *dir, const char *tag)
{
    struct gla_present hook = { present, NULL };
    int i, fails = 0;
    printf("gla: %s gives GL %d (compat), %d (core), GLES %d\n", gla_display_driver(d),
           gla_display_version(d, GLA_COMPAT), gla_display_version(d, GLA_CORE),
           gla_display_version(d, GLA_ES2));
    for (i = 0; i < 3; i++) {
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
    unsigned long got[3];
    int i, fails = 0, r;

    step("start");
    d = gla_display_create();
    step("display made");
    if (!d) {
        printf("gla: no display\n");
        return 1;
    }
    if ((r = run_scenes(d, got, soft, dir, "softpipe")) < 0) return 1;
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
            for (i = 0; i < 3; i++)
                if (!strcmp(name, scenes[i].name)) {
                    seen++;
                    if (got[i] != want) {
                        printf("%s: picture %08lx, stored %08lx\n", name, got[i], want);
                        fails++;
                    }
                }
        fclose(f);
        if (seen != 3) {
            printf("%s has %d of the 3 scenes\n", golden, seen);
            fails++;
        }
    }

#ifdef GLA_TEST_VIRGL
    {
        char err[256];
        static unsigned char gpu[3][W * H * 3];
        struct gla_virgl_transport t;
        if (!virgl_transport(&t, err, sizeof err))
            printf("gla: virgl skipped (%s)\n", err);
        else if (!(d = gla_display_create_virgl(&t))) {
            printf("gla: virgl: no display\n");
            fails++;
        } else {
            if ((r = run_scenes(d, got, gpu, dir, "virgl")) < 0) return 1;
            fails += r;
            gla_display_destroy(d);
            for (i = 0; i < 3; i++) {
                int c = closeness(gpu[i], soft[i]);
                printf("%s: virgl's picture is %d.%d%% softpipe's\n", scenes[i].name, c / 10, c % 10);
                if (c < 990) fails++;
            }
        }
    }
#endif
    printf(fails ? "gla: %d failed\n" : "gla: all tests passed\n", fails);
    return fails != 0;
}

/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's tests on the PC: the library's own files (all but the
 * AmigaOS parts, mgl_lib.c and mgl_display.c) built 32-bit with MGL_HOST,
 * a drawing area in memory standing in for the display, and the calls made
 * through the table (mgl_static.c over mgl_table.c), as a program makes
 * them. Checks OpenGL's behaviour piece by piece, then draws the test scene
 * and compares its checksum with the golden one.
 *   test_minigl [golden-file] [-o picture.ppm] [-update]
 */
#define MINIGL_LIBRARY_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <proto/minigl.h>
#include "../library/minigl/mgl_internal.h"

/* ---- what mgl_lib.c and mgl_display.c give on the Amiga -------------------------------------- */

GLcontext mgl_current;
struct mgl_prefs mgl_prefs = { 16, 2, 0, 16, 0, 0, 0, 0, ROUTE_CPU, 0, 0, 0 };
long mgl_trace;
struct Library *MiniGLBase;
const MGLDispatchTable *MiniGLDispatch;

void *mgl_alloc(ULONG bytes) { return calloc(1, bytes ? bytes : 4); }
void mgl_free(void *p) { free(p); }
void mgl_copy(void *d, const void *s, ULONG n) { memmove(d, s, n); }
void mgl_zero(void *d, ULONG n) { memset(d, 0, n); }
void mgl_read_prefs(void) { }
void mgl_log(const char *fmt, ...) { (void)fmt; }
ULONG mgl_millis(void) { return 0; }

BOOL MiniGLOpen(void) { MiniGLDispatch = &mgl_table; return TRUE; }
void MiniGLClose(void) { MiniGLDispatch = 0; }

struct mgl_display { UBYTE *pix; };

int mgl_disp_lock(GLcontext c) { c->base = c->disp->pix; c->bpr = (ULONG)c->width * (c->fmt == OGPU_FMT_RGB565 ? 2 : 4); return 1; }
void mgl_disp_unlock(GLcontext c) { (void)c; }
void mgl_disp_free(GLcontext c) { (void)c; }
int mgl_events(GLcontext c, void (*k)(int), void (*s)(int)) { (void)c; (void)k; (void)s; return 1; }
ULONG mgl_event_signal(GLcontext c) { (void)c; return 0; }

void *MGLCreateContext(int offx, int offy, int w, int h)
{
    GLcontext c = mgl_alloc(sizeof(struct GLcontext_t));
    (void)offx; (void)offy;
    c->disp = mgl_alloc(sizeof(struct mgl_display));
    c->width = w; c->height = h;
    c->fmt = mgl_prefs.depth <= 16 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32;
    c->disp->pix = mgl_alloc((ULONG)w * h * 4);
    if (!mgl_out_open(c)) return 0;
    c->route = ROUTE_CPU;
    mgl_init_state(c);
    mgl_current = c;
    return c;
}
void *MGLCreateContextFromID(GLint id, GLint *w, GLint *h) { (void)id; return MGLCreateContext(0, 0, w ? *w : 320, h ? *h : 240); }
void *MGLCreateContextFromWindow(struct Window *window) { (void)window; return 0; }
void *MGLCreateContextFromBitMap(struct BitMap *bitmap) { (void)bitmap; return 0; }
void MGLDeleteContext(GLcontext c)
{
    if (!c) return;
    mgl_pipe_flush(c); mgl_flush(c);
    mgl_free_lists(c); mgl_free_textures(c);
    mgl_free(c->cache); mgl_free(c->cache_ix);
    mgl_out_close(c);
    mgl_free(c->disp->pix); mgl_free(c->disp);
    if (mgl_current == c) mgl_current = 0;
    mgl_free(c);
}
GLboolean MGLLockDisplay(GLcontext c) { (void)c; return GL_TRUE; }
void MGLUnlockDisplay(GLcontext c) { (void)c; }
void MGLLockMode(GLcontext c, GLenum m) { (void)c; (void)m; }
void MGLEnableSync(GLcontext c, GLboolean e) { (void)c; (void)e; }
GLboolean MGLLockBack(GLcontext c, MGLLockInfo *info) { (void)c; (void)info; return GL_FALSE; }
void MGLSwitchDisplay(GLcontext c) { if (c) { mgl_pipe_flush(c); mgl_flush(c); c->frames++; } }
GLboolean MGLResizeContext(GLcontext c, GLsizei w, GLsizei h) { (void)c; (void)w; (void)h; return GL_FALSE; }
void *MGLGetWindowHandle(GLcontext c) { (void)c; return 0; }
void *MGLGetInputWindowHandle(GLcontext c) { (void)c; return 0; }
void MGLSetPointer(GLcontext c) { (void)c; }
void MGLClearPointer(GLcontext c) { (void)c; }
void MGLIdleFunc(GLcontext c, IdleFn i) { if (c) c->idle = i; }
void MGLKeyFunc(GLcontext c, KeyHandlerFn k) { if (c) c->key = k; }
void MGLMouseFunc(GLcontext c, MouseHandlerFn m) { if (c) c->mouse = m; }
void MGLSpecialFunc(GLcontext c, SpecialHandlerFn s) { if (c) c->special = s; }
void MGLExit(GLcontext c) { if (c) c->quit = 1; }
void MGLMainLoop(GLcontext c) { (void)c; }
void LIB_mglChoosePixelDepth(int depth) { mgl_prefs.depth = depth; }
void LIB_mglChooseNumberOfBuffers(int n) { (void)n; }
void LIB_mglChooseWindowMode(GLboolean f) { (void)f; }
void LIB_mglChooseZBufferDepth(int bits) { mgl_prefs.zbits = bits >= 24 ? 32 : 16; }
void LIB_mglChooseVertexBufferSize(int s) { (void)s; }
void LIB_mglChooseTextureBufferSize(int s) { (void)s; }
void LIB_mglChooseMtexBufferSize(int s) { (void)s; }
void LIB_mglChooseGuardBand(GLboolean f) { (void)f; }
void LIB_mglProhibitAlphaFallback(GLboolean f) { (void)f; }
void LIB_mglProhibitMipMapping(GLboolean f) { mgl_prefs.no_mip = f; }
void LIB_mglProposeCloseDesktop(GLboolean f) { (void)f; }
void MGLDrawMultitexBuffer(GLcontext c, GLenum a, GLenum b, GLenum e) { (void)c; (void)a; (void)b; (void)e; }
GLint LIB_mglGetSupportedScreenModes(MGLScreenModeCallback cb) { (void)cb; return (GLint)MGL_SM_BESTMODE; }
void MGLWriteShotPPM(GLcontext c, char *f) { (void)c; (void)f; }

/* ---- checks ------------------------------------------------------------------------------------- */

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void pixel(int x, int y, unsigned char *rgb)
{
    glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
}

static int near(int a, int b, int tol) { return a - b <= tol && b - a <= tol; }

static void ortho(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void test_basics(void)
{
    unsigned char p[3];
    GLint v[4];
    GLfloat f[16];
    mglChoosePixelDepth(32);
    CHECK(mglCreateContext(0, 0, 64, 64) != 0, "context");
    glGetIntegerv(GL_VIEWPORT, v);
    CHECK(v[0] == 0 && v[1] == 0 && v[2] == 64 && v[3] == 64, "viewport %d %d %d %d", v[0], v[1], v[2], v[3]);
    CHECK(glGetError() == GL_NO_ERROR, "no error at the start");
    glEnable(0x1234);
    CHECK(glGetError() == GL_INVALID_ENUM, "unknown enable");
    CHECK(glGetError() == GL_NO_ERROR, "error cleared");
    glClearColor(1.0f, 0.5f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    pixel(10, 10, p);
    CHECK(p[0] == 255 && near(p[1], 128, 1) && p[2] == 0, "clear colour %d %d %d", p[0], p[1], p[2]);
    /* a flat square in the window's lower left (OpenGL's origin) */
    ortho(64, 64);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f(16, 0); glVertex2f(16, 16); glVertex2f(0, 16);
    glEnd();
    pixel(4, 4, p);
    CHECK(p[0] == 0 && p[1] == 255 && p[2] == 0, "square at the bottom left %d %d %d", p[0], p[1], p[2]);
    pixel(4, 60, p);
    CHECK(p[1] == 0, "nothing at the top left %d", p[1]);
    pixel(16, 4, p);
    CHECK(p[1] == 0, "the square's right edge is not drawn (pixel 16) %d", p[1]);
    pixel(15, 15, p);
    CHECK(p[1] == 255, "its last pixel is (15, 15) %d", p[1]);
    /* matrices */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(1, 2, 3);
    glPushMatrix();
    glScalef(2, 2, 2);
    glPopMatrix();
    glGetFloatv(GL_MODELVIEW_MATRIX, f);
    CHECK(f[12] == 1 && f[13] == 2 && f[14] == 3 && f[0] == 1, "translate and pop");
    glPopMatrix();
    CHECK(glGetError() == GL_STACK_UNDERFLOW, "pop below the bottom");
    glDisable(GL_TEXTURE_2D);
    mglDeleteContext();
}

static void test_depth_blend(void)
{
    unsigned char p[3];
    mglChoosePixelDepth(32);
    mglCreateContext(0, 0, 64, 64);
    ortho(64, 64);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -10, 10);
    glMatrixMode(GL_MODELVIEW);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    /* red near (z = 5 is nearer with this ortho: -z is depth), then blue far: red stays */
    glColor3f(1, 0, 0);
    glBegin(GL_TRIANGLES); glVertex3f(0, 0, 5); glVertex3f(64, 0, 5); glVertex3f(0, 64, 5); glEnd();
    glColor3f(0, 0, 1);
    glBegin(GL_TRIANGLES); glVertex3f(0, 0, -5); glVertex3f(64, 0, -5); glVertex3f(0, 64, -5); glEnd();
    pixel(5, 5, p);
    CHECK(p[0] == 255 && p[2] == 0, "depth test keeps the near one %d %d %d", p[0], p[1], p[2]);
    glDepthFunc(GL_ALWAYS);
    glBegin(GL_TRIANGLES); glVertex3f(0, 0, -5); glVertex3f(64, 0, -5); glVertex3f(0, 64, -5); glEnd();
    pixel(5, 5, p);
    CHECK(p[2] == 255, "GL_ALWAYS draws over %d %d %d", p[0], p[1], p[2]);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 0.5f);
    glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(64, 0); glVertex2f(0, 64); glEnd();
    pixel(5, 5, p);
    CHECK(near(p[0], 128, 2) && near(p[2], 255, 1), "half white over blue %d %d %d", p[0], p[1], p[2]);
    glDisable(GL_BLEND);
    /* culling: a clockwise triangle is a back face */
    glEnable(GL_CULL_FACE);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0, 1, 0);
    glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(0, 64); glVertex2f(64, 0); glEnd();
    pixel(5, 5, p);
    CHECK(p[1] == 0, "a back face is culled %d", p[1]);
    glFrontFace(GL_CW);
    glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(0, 64); glVertex2f(64, 0); glEnd();
    pixel(5, 5, p);
    CHECK(p[1] == 255, "with GL_CW it is the front %d", p[1]);
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    /* scissor */
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 8, 8);
    glColor3f(1, 1, 0);
    glBegin(GL_QUADS); glVertex2f(0, 0); glVertex2f(64, 0); glVertex2f(64, 64); glVertex2f(0, 64); glEnd();
    pixel(4, 4, p);
    CHECK(p[0] == 255, "inside the scissor %d", p[0]);
    pixel(20, 20, p);
    CHECK(p[0] == 0, "outside the scissor %d", p[0]);
    glDisable(GL_SCISSOR_TEST);
    mglDeleteContext();
}

static void test_clip_light_fog(void)
{
    unsigned char p[3];
    static const GLfloat pos[4] = { 0, 0, 1, 0 }, white[4] = { 1, 1, 1, 1 }, black[4] = { 0, 0, 0, 1 };
    mglChoosePixelDepth(32);
    mglCreateContext(0, 0, 64, 64);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 1, 10);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    /* a quad reaching behind the eye: clipped at the near plane, drawn in front */
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glVertex3f(-5, -5, 5); glVertex3f(5, -5, 5); glVertex3f(5, -5, -20); glVertex3f(-5, -5, -20);
    glEnd();
    pixel(32, 5, p);
    CHECK(p[0] == 255, "a floor crossing the near plane is drawn near the bottom %d", p[0]);
    pixel(32, 60, p);
    CHECK(p[0] == 0, "and not at the top %d", p[0]);
    CHECK(glGetError() == GL_NO_ERROR, "no error");
    /* lighting: a face toward a light straight on gets its diffuse colour */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, black);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, white);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glNormal3f(0, 0, 1);
    glBegin(GL_QUADS);
    glVertex3f(-1, -1, -2); glVertex3f(1, -1, -2); glVertex3f(1, 1, -2); glVertex3f(-1, 1, -2);
    glEnd();
    pixel(32, 32, p);
    CHECK(p[0] >= 253, "lit face on %d", p[0]);
    glNormal3f(0.0f, 0.70710678f, 0.70710678f);
    glBegin(GL_QUADS);
    glVertex3f(-1, -1, -2); glVertex3f(1, -1, -2); glVertex3f(1, 1, -2); glVertex3f(-1, 1, -2);
    glEnd();
    pixel(32, 32, p);
    CHECK(near(p[0], 180, 2), "lit at 45 degrees %d", p[0]);
    glDisable(GL_LIGHTING);
    /* linear fog, half way */
    {
        static const GLfloat fc[4] = { 0, 0, 1, 1 };
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, 1.0f);
        glFogf(GL_FOG_END, 3.0f);
        glFogfv(GL_FOG_COLOR, (GLfloat *)fc);
        glEnable(GL_FOG);
        glColor3f(1, 0, 0);
        glBegin(GL_QUADS);
        glVertex3f(-1, -1, -2); glVertex3f(1, -1, -2); glVertex3f(1, 1, -2); glVertex3f(-1, 1, -2);
        glEnd();
        pixel(32, 32, p);
        CHECK(near(p[0], 128, 3) && near(p[2], 128, 3), "fog half way %d %d %d", p[0], p[1], p[2]);
        glDisable(GL_FOG);
    }
    mglDeleteContext();
}

static void test_textures_lists_arrays(void)
{
    unsigned char p[3], img[4 * 4 * 3];
    GLuint t, l;
    int i;
    static const GLfloat va[] = { 0, 0, 32, 0, 32, 32, 0, 32 };
    static const GLubyte ca[] = { 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255 };
    static const GLushort ix[] = { 0, 1, 2, 0, 2, 3 };
    mglChoosePixelDepth(32);
    mglCreateContext(0, 0, 64, 64);
    ortho(64, 64);
    for (i = 0; i < 16; i++) { img[i * 3] = (unsigned char)(i * 16); img[i * 3 + 1] = 0; img[i * 3 + 2] = (unsigned char)(255 - i * 16); }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(1, 0); glVertex2f(64, 0);
    glTexCoord2f(1, 1); glVertex2f(64, 64);
    glTexCoord2f(0, 1); glVertex2f(0, 64);
    glEnd();
    pixel(2, 2, p);
    CHECK(p[0] == 0 && p[2] == 255, "texel (0,0) at the bottom left %d %d %d", p[0], p[1], p[2]);
    pixel(62, 62, p);
    CHECK(p[0] == 240 && p[2] == 15, "texel (3,3) at the top right %d %d %d", p[0], p[1], p[2]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(1, 0); glVertex2f(64, 0);
    glTexCoord2f(1, 1); glVertex2f(64, 64);
    glTexCoord2f(0, 1); glVertex2f(0, 64);
    glEnd();
    pixel(62, 62, p);
    CHECK(near(p[0], 120, 2), "modulated by half %d", p[0]);
    glDisable(GL_TEXTURE_2D);
    /* a display list */
    l = glGenLists(1);
    glNewList(l, GL_COMPILE);
    glColor3f(0, 1, 0);
    glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(64, 0); glVertex2f(0, 64); glEnd();
    glEndList();
    glClear(GL_COLOR_BUFFER_BIT);
    pixel(5, 5, p);
    CHECK(p[1] == 0, "a compiled list draws nothing yet %d", p[1]);
    glCallList(l);
    pixel(5, 5, p);
    CHECK(p[1] == 255, "glCallList draws it %d", p[1]);
    /* vertex arrays */
    glClear(GL_COLOR_BUFFER_BIT);
    glVertexPointer(2, GL_FLOAT, 0, va);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, ca);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, ix);
    pixel(5, 5, p);
    CHECK(p[0] == 255 && p[1] == 0, "glDrawElements %d %d %d", p[0], p[1], p[2]);
    pixel(40, 40, p);
    CHECK(p[0] == 0, "and only where it was asked %d", p[0]);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    pixel(30, 30, p);
    CHECK(p[0] == 255, "glDrawArrays %d", p[0]);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDeleteLists(l, 1);
    glDeleteTextures(1, &t);
    CHECK(glGetError() == GL_NO_ERROR, "no error");
    mglDeleteContext();
}

/* ---- the scene ------------------------------------------------------------------------------------- */

#define MGLTEST_GL_HEADER <mgl/gl.h>
#include "../tools/mgltest_scene.c"

static unsigned long scene_sum(int w, int h, int depth, float t, const char *ppm)
{
    unsigned char *px = malloc((size_t)w * h * 3);
    unsigned long sum = 2166136261UL;
    int i;
    mglChoosePixelDepth(depth);
    mglCreateContext(0, 0, w, h);
    scene_init(w, h);
    scene_frame(t);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    for (i = 0; i < w * h * 3; i++) sum = ((sum ^ px[i]) * 16777619UL) & 0xFFFFFFFFUL;
    CHECK(glGetError() == GL_NO_ERROR, "the scene makes no error");
    if (ppm) {
        FILE *f = fopen(ppm, "wb");
        int y;
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (y = h - 1; y >= 0; y--) fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f);
            fclose(f);
        }
    }
    scene_done();
    mglDeleteContext();
    free(px);
    return sum;
}

int main(int argc, char **argv)
{
    const char *golden = 0, *ppm = 0;
    int update = 0, i;
    unsigned long s32, s16;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "-update")) update = 1;
        else golden = argv[i];
    }
    MiniGLOpen();
    test_basics();
    test_depth_blend();
    test_clip_light_fog();
    test_textures_lists_arrays();
    s32 = scene_sum(320, 240, 32, 0.0f, ppm);
    if (getenv("MGL_BIG")) scene_sum(960, 720, 32, 0.0f, getenv("MGL_BIG"));
    s16 = scene_sum(320, 240, 16, 0.0f, 0);
    CHECK(scene_sum(320, 240, 32, 0.0f, 0) == s32, "the scene draws the same twice");
    printf("minigl scene: ARGB32 %08lx, RGB565 %08lx\n", s32, s16);
    if (golden) {
        FILE *f = fopen(golden, update ? "w" : "r");
        if (f && update) { fprintf(f, "%08lx %08lx\n", s32, s16); printf("golden written\n"); }
        else if (f) {
            unsigned long g32 = 0, g16 = 0;
            if (fscanf(f, "%lx %lx", &g32, &g16) == 2) {
                CHECK(g32 == s32, "the ARGB32 scene is the golden one (%08lx)", g32);
                CHECK(g16 == s16, "the RGB565 scene is the golden one (%08lx)", g16);
            }
        }
        if (f) fclose(f);
    }
    MiniGLClose();
    printf("minigl: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

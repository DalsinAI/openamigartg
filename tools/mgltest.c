/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MGLTest: the MiniGL test scene (mgltest_scene.c) through minigl.library,
 * for comparing drivers and libraries. Built against the minigl.library 29
 * table, so it runs on OpenRTG's minigl.library and on the classic one.
 *   MGLTest [CPU|GPU|AUTO] [W] [H] [DEPTH] [FRAMES] [SNAP]
 *           (FRAMES n, SECONDS n and SNAP file also work by name)
 * Started plainly (a Shell name, or the Workbench icon) it shows the scene in
 * a Workbench window, until its close gadget, Esc or Ctrl-C ends it, then
 * frees everything and exits. Giving FRAMES or SECONDS (or SNAP) makes it the
 * measuring run, as it always was, full screen, printing the frame rate.
 * CPU or GPU sets ENV:MiniGL/Driver for this run (OpenRTG's library reads
 * it; the classic library draws through Warp3D whatever it says). The first
 * frame (t = 0) is read back and its checksum printed (and written as a PPM
 * picture to SNAP); then FRAMES frames are drawn and timed. MGLTestStatic
 * is the same program built the linked-in MiniGL's way (libmgl.a). */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>
#ifdef MGLTEST_STATIC
/* Built as a program written for the linked-in MiniGL: <mgl/gl.h>, MGLInit
 * and MGLTerm, linked with libmgl.a. */
#include <mgl/gl.h>
#include <libraries/minigl.h>
#define MiniGLOpen MGLInit
#define MiniGLClose MGLTerm
#else
#include <proto/minigl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MGLTEST_GL_HEADER <mgl/gl.h>
#define MGLTEST_NO_LIBM
#include "mgltest_scene.c"

static const char version[] __attribute__((used)) = "$VER: MGLTest 1.0 (8.10.2026) OpenRTG MiniGL, Dalsin Limited";

/* sin and cos without the maths library (whose 68040 code traps). */
static float reduce(float x, int *k)
{
    double q = (double)x * 0.63661977236758134;
    int n = (int)(q < 0 ? q - 0.5 : q + 0.5);
    *k = n;
    return (float)((double)x - (double)n * 1.57079632679489662);
}
static float ks(float x) { float x2 = x * x; return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72)))); }
static float kc(float x) { float x2 = x * x; return 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56))); }
float mgltest_sin(float x) { int k; float r = reduce(x, &k); switch (k & 3) { case 0: return ks(r); case 1: return kc(r); case 2: return -ks(r); } return -kc(r); }
float mgltest_cos(float x) { int k; float r = reduce(x, &k); switch (k & 3) { case 0: return kc(r); case 1: return -ks(r); case 2: return -kc(r); } return ks(r); }

static ULONG ticks(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 4320000UL + (ULONG)ds.ds_Minute * 3000UL + (ULONG)ds.ds_Tick;
}

static int framecount, quiet;

/* Started from Workbench (argc 0) the C library's output window would open on the
 * first line and stay (WAIT) after the program had gone, so the report is only
 * printed from a Shell; failures still print, and show that window. */
#define INFO(...) do { if (!quiet) printf(__VA_ARGS__); } while (0)

/* mglMainLoop's callbacks: one frame each time round, and Esc leaves. */
static void idle(void)
{
    scene_frame(++framecount / 30.0f);
    mglSwitchDisplay();
}
static void key(char k)
{
    if (k == 27) mglExit();
}

int main(int argc, char **argv)
{
    const char *drv = "AUTO", *snap = 0;
    int w = 320, h = 240, depth = 16, frames = 0, secs = 0, timed = 0, i, had = 0, pos = 0;
    char old[32];
    unsigned char *px;
    unsigned long sum = 2166136261UL;
    ULONG t0, t1;
    quiet = argc == 0;
    for (i = 1; i < argc; i++) {
        if (!stricmp(argv[i], "FRAMES") && i + 1 < argc) { frames = atoi(argv[++i]); timed = 1; }
        else if (!stricmp(argv[i], "SECONDS") && i + 1 < argc) { secs = atoi(argv[++i]); timed = 1; }
        else if (!stricmp(argv[i], "SNAP") && i + 1 < argc) snap = argv[++i];
        else switch (pos++) {                   /* the old order: driver, width, height, depth, frames, snap */
        case 0: drv = argv[i]; break;
        case 1: w = atoi(argv[i]); break;
        case 2: h = atoi(argv[i]); break;
        case 3: depth = atoi(argv[i]); break;
        case 4: frames = atoi(argv[i]); timed = 1; break;
        case 5: snap = argv[i]; break;
        }
    }
    if (snap && !timed) { frames = 100; timed = 1; }    /* SNAP alone: as before */
    if (w < 16) w = 320;
    if (h < 16) h = 240;
    had = GetVar((STRPTR)"MiniGL/Driver", (STRPTR)old, sizeof old, GVF_GLOBAL_ONLY) > 0;
    if (!stricmp(drv, "CPU")) SetVar((STRPTR)"MiniGL/Driver", (STRPTR)"CPU", -1, GVF_GLOBAL_ONLY);
    else if (!stricmp(drv, "GPU")) SetVar((STRPTR)"MiniGL/Driver", (STRPTR)"OpenGPU", -1, GVF_GLOBAL_ONLY);
    if (!MiniGLOpen()) { printf("MGLTest: no minigl.library 29\n"); return 10; }
    INFO("MGLTest: %s %ld.%ld, %dx%d, %d bits\n", MiniGLBase->lib_Node.ln_Name, (long)MiniGLBase->lib_Version,
           (long)MiniGLBase->lib_Revision, w, h, depth);
    mglChoosePixelDepth(depth);
    if (!timed) mglChooseWindowMode(GL_TRUE);       /* the demo is a window, with a close gadget */
    if (!mglCreateContext(0, 0, w, h)) { printf("MGLTest: no context\n"); MiniGLClose(); return 10; }
    if (!stricmp(drv, "CPU") || !stricmp(drv, "GPU")) {
        if (had) SetVar((STRPTR)"MiniGL/Driver", (STRPTR)old, -1, GVF_GLOBAL_ONLY);
        else DeleteVar((STRPTR)"MiniGL/Driver", GVF_GLOBAL_ONLY);
    }
    mglEnableSync(GL_FALSE);
    INFO("  renderer: %s\n", (const char *)glGetString(GL_RENDERER));
    scene_init(w, h);
    scene_frame(0.0f);
    px = malloc((size_t)w * h * 3);
    if (px) {
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
        for (i = 0; i < w * h * 3; i++) sum = ((sum ^ px[i]) * 16777619UL) & 0xFFFFFFFFUL;
        INFO("  frame 0: checksum %08lx\n", sum);
        if (snap) {
            BPTR f = Open((STRPTR)snap, MODE_NEWFILE);
            if (f) {
                char head[32];
                int y;
                sprintf(head, "P6\n%d %d\n255\n", w, h);
                Write(f, head, (LONG)strlen(head));
                for (y = h - 1; y >= 0; y--) Write(f, px + (size_t)y * w * 3, w * 3);
                Close(f);
            }
        }
        free(px);
    }
    INFO("  error: %ld\n", (long)glGetError());
    mglSwitchDisplay();
    t0 = ticks();
    if (timed) {
        if (secs) {
            for (i = 0; ticks() - t0 < (ULONG)secs * 50UL; ) { scene_frame(++i / 30.0f); mglSwitchDisplay(); }
            frames = i;
        } else {
            for (i = 1; i <= frames; i++) {
                scene_frame(i / 30.0f);
                mglSwitchDisplay();
            }
        }
    } else {
        /* until the close gadget, Esc or Ctrl-C: mglMainLoop returns for each */
        mglKeyFunc(key);
        mglIdleFunc(idle);
        mglMainLoop();
        frames = framecount;
    }
    t1 = ticks();
    if (t1 > t0) {
        unsigned long hund = (unsigned long)frames * 5000UL / (t1 - t0);
        INFO("  %d frames in %lu.%02lu s: %lu.%02lu frames a second\n", frames, (unsigned long)(t1 - t0) / 50,
               (unsigned long)((t1 - t0) % 50) * 2, hund / 100, hund % 100);
    }
    scene_done();
    mglDeleteContext();
    MiniGLClose();
    return 0;
}

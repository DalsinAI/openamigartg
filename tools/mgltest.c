/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MGLTest: the MiniGL test scene (mgltest_scene.c) through minigl.library,
 * for comparing drivers and libraries. Built against the minigl.library 29
 * table, so it runs on OpenRTG's minigl.library and on the classic one.
 *   MGLTest [CPU|GPU|AUTO] [W] [H] [DEPTH] [FRAMES] [SNAP]
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

int main(int argc, char **argv)
{
    const char *drv = argc > 1 ? argv[1] : "AUTO", *snap = argc > 6 ? argv[6] : 0;
    int w = argc > 2 ? atoi(argv[2]) : 320, h = argc > 3 ? atoi(argv[3]) : 240;
    int depth = argc > 4 ? atoi(argv[4]) : 16, frames = argc > 5 ? atoi(argv[5]) : 100, i, had = 0;
    char old[32];
    unsigned char *px;
    unsigned long sum = 2166136261UL;
    ULONG t0, t1;
    if (w < 16) w = 320;
    if (h < 16) h = 240;
    had = GetVar((STRPTR)"MiniGL/Driver", (STRPTR)old, sizeof old, GVF_GLOBAL_ONLY) > 0;
    if (!stricmp(drv, "CPU")) SetVar((STRPTR)"MiniGL/Driver", (STRPTR)"CPU", -1, GVF_GLOBAL_ONLY);
    else if (!stricmp(drv, "GPU")) SetVar((STRPTR)"MiniGL/Driver", (STRPTR)"OpenGPU", -1, GVF_GLOBAL_ONLY);
    if (!MiniGLOpen()) { printf("MGLTest: no minigl.library 29\n"); return 10; }
    printf("MGLTest: %s %ld.%ld, %dx%d, %d bits\n", MiniGLBase->lib_Node.ln_Name, (long)MiniGLBase->lib_Version,
           (long)MiniGLBase->lib_Revision, w, h, depth);
    mglChoosePixelDepth(depth);
    if (!mglCreateContext(0, 0, w, h)) { printf("MGLTest: no context\n"); MiniGLClose(); return 10; }
    if (!stricmp(drv, "CPU") || !stricmp(drv, "GPU")) {
        if (had) SetVar((STRPTR)"MiniGL/Driver", (STRPTR)old, -1, GVF_GLOBAL_ONLY);
        else DeleteVar((STRPTR)"MiniGL/Driver", GVF_GLOBAL_ONLY);
    }
    mglEnableSync(GL_FALSE);
    printf("  renderer: %s\n", (const char *)glGetString(GL_RENDERER));
    scene_init(w, h);
    scene_frame(0.0f);
    px = malloc((size_t)w * h * 3);
    if (px) {
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
        for (i = 0; i < w * h * 3; i++) sum = ((sum ^ px[i]) * 16777619UL) & 0xFFFFFFFFUL;
        printf("  frame 0: checksum %08lx\n", sum);
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
    printf("  error: %ld\n", (long)glGetError());
    mglSwitchDisplay();
    t0 = ticks();
    for (i = 1; i <= frames; i++) {
        scene_frame(i / 30.0f);
        mglSwitchDisplay();
    }
    t1 = ticks();
    if (t1 > t0) {
        unsigned long hund = (unsigned long)frames * 5000UL / (t1 - t0);
        printf("  %d frames in %lu.%02lu s: %lu.%02lu frames a second\n", frames, (unsigned long)(t1 - t0) / 50,
               (unsigned long)((t1 - t0) % 50) * 2, hund / 100, hund % 100);
    }
    scene_done();
    mglDeleteContext();
    MiniGLClose();
    return 0;
}

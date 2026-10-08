/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MGLGlutTest: the MiniGL test scene as a GLUT program, in a Workbench
 * window: glutInit, glutCreateWindow, display, idle, reshape and keyboard
 * functions, glutSwapBuffers and glutGet(GLUT_ELAPSED_TIME). Draws FRAMES
 * frames and prints the frame rate
 * (Escape or the close gadget stop it sooner).
 *   MGLGlutTest [W] [H] [FRAMES] */
#include <proto/minigl.h>
#include <stdio.h>
#include <stdlib.h>

#define MGLTEST_GL_HEADER <mgl/gl.h>
#define MGLTEST_NO_LIBM
#include "mgltest_scene.c"

static const char version[] __attribute__((used)) = "$VER: MGLGlutTest 1.0 (8.10.2026) OpenRTG MiniGL, Dalsin Limited";

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

static int frames, wanted = 100, start, shown_w, shown_h;

static void finish(void)
{
    int ms = glutGet(GLUT_ELAPSED_TIME) - start;
    if (ms > 0) printf("MGLGlutTest: %d frames in %d ms: %d.%d frames a second, %dx%d\n", frames, ms,
                       frames * 1000 / ms, (frames * 10000 / ms) % 10, shown_w, shown_h);
    scene_done();
    mglDeleteContext();
    MiniGLClose();
    exit(0);
}

static void display(void)
{
    if (!frames) start = glutGet(GLUT_ELAPSED_TIME);
    scene_frame(frames / 30.0f);
    glutSwapBuffers();
    if (++frames > wanted) finish();
}

static void idle(void) { glutPostRedisplay(); }

static void reshape(int w, int h)
{
    shown_w = w; shown_h = h;
    scene_init(w, h);
}

static void keyboard(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 27) finish();
}

int main(int argc, char **argv)
{
    int w = argc > 1 ? atoi(argv[1]) : 320, h = argc > 2 ? atoi(argv[2]) : 240;
    if (argc > 3) wanted = atoi(argv[3]);
    if (!MiniGLOpen()) { printf("MGLGlutTest: no minigl.library 29\n"); return 10; }
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(w, h);
    glutInitWindowPosition(40, 40);
    if (!glutCreateWindow("MGLGlutTest")) { printf("MGLGlutTest: no window\n"); MiniGLClose(); return 10; }
    printf("MGLGlutTest: %s\n", (const char *)glGetString(GL_RENDERER));
    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutMainLoop();
    finish();
    return 0;
}

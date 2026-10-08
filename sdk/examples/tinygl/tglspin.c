/* Copyright (c) 2026 Dalsin Limited. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * TGLSpin: a TinyGL program, written as one is on other systems with
 * TinyGL: it opens tinygl.library, gets its context from GLInit(), starts
 * GL in an Intuition window with glAInitializeContextWindowed, and shows
 * each frame with glASwapBuffers. On OpenGPU it runs on the GL module
 * (Mesa: the GPU through virgl on AmigaChrome, else softpipe on the 68k;
 * SetEnv TinyGL/Driver CPU picks softpipe). A lit, spinning cube; Esc or
 * the close gadget quits.
 *   TGLSpin [SECONDS n]
 * Build:
 *   m68k-amigaos-gcc -O2 tglspin.c -o TGLSpin $(pkg-config --cflags --libs tinygl)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/tinygl.h>
#include <tgl/glu.h>

/* GL needs a big stack: libGL.a runs GL calls on one of its own when the
 * program's is small (a Shell's default is 4 KB), so nothing is needed here. */

struct Library *TinyGLBase;
GLContext *__tglContext;

static double now(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Days * 86400.0 + ds.ds_Minute * 60.0 + ds.ds_Tick / (double)TICKS_PER_SECOND;
}

static void cube(void)
{
    static const float n[6][3] = { {0,0,1}, {0,0,-1}, {0,1,0}, {0,-1,0}, {1,0,0}, {-1,0,0} };
    static const float c[6][3] = { {1,.3f,.3f}, {.3f,1,.3f}, {.3f,.3f,1}, {1,1,.3f}, {1,.3f,1}, {.3f,1,1} };
    static const float v[6][4][3] = {
        { {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1} },
        { { 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1} },
        { {-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1,-1} },
        { {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1} },
        { { 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1} },
        { {-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1} },
    };
    int f, i;
    glBegin(GL_QUADS);
    for (f = 0; f < 6; f++) {
        glNormal3fv(n[f]);
        glColor3fv(c[f]);
        for (i = 0; i < 4; i++)
            glVertex3fv(v[f][i]);
    }
    glEnd();
}

int main(int argc, char **argv)
{
    const int w = 320, h = 240;
    static const float light[4] = { 0.4f, 0.7f, 1.0f, 0.0f };
    int i, seconds = 0, quit = 0, frames = 0, rc = RETURN_FAIL;
    struct Window *win = NULL;
    double start;

    for (i = 1; i < argc; i++)
        if (!strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            seconds = atoi(argv[++i]);

    TinyGLBase = OpenLibrary((CONST_STRPTR) "tinygl.library", 50);
    if (!TinyGLBase) {
        printf("TGLSpin: can't open tinygl.library 50\n");
        return RETURN_FAIL;
    }
    __tglContext = GLInit();
    if (!__tglContext) {
        printf("TGLSpin: GLInit failed\n");
        goto out;
    }
    win = OpenWindowTags(NULL, WA_Title, (ULONG) "TGLSpin", WA_InnerWidth, w, WA_InnerHeight, h,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SimpleRefresh, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW,
                         TAG_END);
    if (!win) {
        printf("TGLSpin: no window\n");
        goto out;
    }
    if (!glAInitializeContextWindowed(win)) {
        printf("TGLSpin: GL couldn't start\n");
        goto out;
    }
    printf("TGLSpin: %s, OpenGL %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));

    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, (double)w / h, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glClearColor(0.1f, 0.1f, 0.2f, 1);

    start = now();
    while (!quit) {
        struct IntuiMessage *m;
        double t = now() - start;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        gluLookAt(0, 0, 6, 0, 0, 0, 0, 1, 0);
        glLightfv(GL_LIGHT0, GL_POSITION, light);
        glRotatef((float)(t * 60), 1, 0, 0);
        glRotatef((float)(t * 90), 0, 1, 0);
        cube();
        glASwapBuffers();
        frames++;
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            if (m->Class == IDCMP_CLOSEWINDOW || (m->Class == IDCMP_VANILLAKEY && m->Code == 27))
                quit = 1;
            else if (m->Class == IDCMP_REFRESHWINDOW) {
                BeginRefresh(win);
                EndRefresh(win, TRUE);
            }
            ReplyMsg((struct Message *)m);
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            quit = 1;
        if (seconds && t >= seconds)
            quit = 1;
    }
    printf("TGLSpin: %d frames in %.1f s, %.1f fps\n", frames, now() - start, frames / (now() - start + 0.001));
    rc = RETURN_OK;
out:
    if (__tglContext) {
        glADestroyContextWindowed();
        GLClose(__tglContext);
    }
    if (win)
        CloseWindow(win);
    CloseLibrary(TinyGLBase);
    return rc;
}

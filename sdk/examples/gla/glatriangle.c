/* Copyright (c) 2026 Dalsin Limited. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * GLATriangle: OpenGL with GLA, OpenGPU's own GL interface, in an
 * Intuition window, with no SDL. GLA gives a display (virgl on the PC's
 * graphics chip, else softpipe on the 68k), a context, and a buffer whose
 * frames gla_swap shows in the window. A spinning triangle; Esc or the
 * close gadget quits.
 *   GLATriangle [CPU] [SECONDS n]
 * Build:
 *   m68k-amigaos-gcc -O2 glatriangle.c -o GLATriangle $(pkg-config --cflags --libs gl)
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

#include <GL/gl.h>
#include <gla/gla_core.h>
#include <gla/os3/gla_present_os3.h>
#include <gla/os3/gla_virgl_os3.h>

/* GL runs on the program's stack: libnix gives main one this big. */
unsigned long __stack = 1024 * 1024;

static double now(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Days * 86400.0 + ds.ds_Minute * 60.0 + ds.ds_Tick / (double)TICKS_PER_SECOND;
}

int main(int argc, char **argv)
{
    const int w = 320, h = 240;
    int i, cpu = 0, seconds = 0, quit = 0, frames = 0;
    struct gla_config cfg = { GLA_COMPAT, 0, 0, 1, 0, 0, 0 };
    struct gla_os3_target target;
    struct gla_present hook;
    struct gla_display *d = NULL;
    struct gla_context *c = NULL;
    struct gla_buffer *b = NULL;
    struct Window *win;
    double start;

    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "CPU"))
            cpu = 1;
        else if (!strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            seconds = atoi(argv[++i]);
    }

    win = OpenWindowTags(NULL, WA_Title, (ULONG) "GLATriangle", WA_InnerWidth, w, WA_InnerHeight, h,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SimpleRefresh, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW,
                         TAG_END);
    if (!win) {
        printf("GLATriangle: no window\n");
        return RETURN_FAIL;
    }
    /* Frames go into the window's RastPort, inside its borders. */
    target.rp = win->RPort;
    target.bitmap = NULL;
    target.left = win->BorderLeft;
    target.top = win->BorderTop;
    gla_os3_present_init(&hook, &target);

    if (!cpu) {
        struct gla_virgl_transport t;
        if (gla_os3_virgl_transport(&t))
            d = gla_display_create_virgl(&t);
    }
    if (!d)
        d = gla_display_create();
    c = d ? gla_context_create(d, &cfg, NULL) : NULL;
    b = c ? gla_buffer_create(d, &cfg, w, h, &hook) : NULL;
    if (!b || !gla_make_current(c, b)) {
        printf("GLATriangle: GL couldn't start\n");
        goto out;
    }
    printf("GLATriangle: %s (%s), OpenGL %s\n", (const char *)glGetString(GL_RENDERER), gla_display_driver(d),
           (const char *)glGetString(GL_VERSION));

    glClearColor(0.1f, 0.1f, 0.2f, 1);
    start = now();
    while (!quit) {
        struct IntuiMessage *m;
        double t = now() - start;
        glClear(GL_COLOR_BUFFER_BIT);
        glLoadIdentity();
        glRotatef((float)(t * 90), 0, 0, 1);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0, 0); glVertex2f(0, 0.8f);
        glColor3f(0, 1, 0); glVertex2f(-0.7f, -0.6f);
        glColor3f(0, 0, 1); glVertex2f(0.7f, -0.6f);
        glEnd();
        gla_swap(c, b);
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
    printf("GLATriangle: %d frames in %.1f s, %.1f fps\n", frames, now() - start, frames / (now() - start + 0.001));
out:
    if (c)
        gla_make_current(NULL, NULL);
    if (b)
        gla_buffer_destroy(b);
    if (c)
        gla_context_destroy(c);
    if (d)
        gla_display_destroy(d);
    CloseWindow(win);
    return RETURN_OK;
}

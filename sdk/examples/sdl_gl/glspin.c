/* Copyright (c) 2026 Dalsin Limited. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * GLSpin: OpenGL through SDL 2 (SDL_WINDOW_OPENGL, SDL_GL_CreateContext,
 * SDL_GL_SwapWindow) on OpenGPU's GL module. A lit, spinning cube. GL is
 * Mesa on the PC's graphics chip (virgl) on AmigaChrome, else softpipe on
 * the 68k; SetEnv SDL_OPENGPU_GL cpu picks softpipe. The title shows the
 * frame rate and the renderer. Esc or the close gadget quits.
 *   GLSpin [WIDTH n] [HEIGHT n] [SECONDS n]
 * Build:
 *   m68k-amigaos-gcc -O2 glspin.c -o GLSpin $(sdl2-config --cflags --libs)
 */
#include "SDL.h"
#include "SDL_opengl.h"
#include <stdio.h>
#include <stdlib.h>

/* GL runs on the program's stack: libnix gives main one this big. */
unsigned long __stack = 1024 * 1024;

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

int main(int argc, char *argv[])
{
    int w = 320, h = 240, seconds = 0, quit = 0, i, frames = 0, all = 0;
    static const GLfloat light[] = { 2, 3, 4, 0 };
    char title[128], renderer[64];
    SDL_Window *win;
    SDL_GLContext ctx;
    Uint32 start, mark;

    for (i = 1; i < argc; i++) {
        if (!SDL_strcasecmp(argv[i], "WIDTH") && i + 1 < argc)
            w = atoi(argv[++i]);
        else if (!SDL_strcasecmp(argv[i], "HEIGHT") && i + 1 < argc)
            h = atoi(argv[++i]);
        else if (!SDL_strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            seconds = atoi(argv[++i]);
    }
    if (w < 32 || h < 32)
        w = 320, h = 240;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("GLSpin: %s\n", SDL_GetError());
        return 20;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    win = SDL_CreateWindow("GLSpin", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_OPENGL);
    ctx = win ? SDL_GL_CreateContext(win) : NULL;
    if (!ctx) {
        printf("GLSpin: %s\n", SDL_GetError());
        SDL_Quit();
        return 20;
    }
    SDL_snprintf(renderer, sizeof renderer, "%s", (const char *)glGetString(GL_RENDERER));
    printf("GLSpin: %s, OpenGL %s\n", renderer, (const char *)glGetString(GL_VERSION));

    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0 * w / h, 1.0 * w / h, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glLightfv(GL_LIGHT0, GL_POSITION, light);
    glClearColor(0.06f, 0.08f, 0.16f, 1);

    start = mark = SDL_GetTicks();
    while (!quit) {
        SDL_Event e;
        Uint32 now = SDL_GetTicks();
        float t = (now - start) / 1000.0f;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                quit = 1;
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        glTranslatef(0, 0, -6);
        glRotatef(t * 50, 1, 0, 0);
        glRotatef(t * 70, 0, 1, 0);
        cube();
        SDL_GL_SwapWindow(win);
        frames++;
        all++;
        if (now - mark >= 1000) {
            SDL_snprintf(title, sizeof title, "GLSpin  %.1f fps (%s)", frames * 1000.0 / (now - mark), renderer);
            SDL_SetWindowTitle(win, title);
            frames = 0;
            mark = now;
        }
        if (seconds && now - start >= (Uint32)seconds * 1000)
            quit = 1;
    }
    printf("GLSpin: %dx%d, %d frames in %.1f s, %.1f fps\n", w, h, all,
           (SDL_GetTicks() - start) / 1000.0, all * 1000.0 / (SDL_GetTicks() - start + 1));
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

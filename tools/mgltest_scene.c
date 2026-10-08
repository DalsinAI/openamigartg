/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The MiniGL test scene, in plain OpenGL 1.1 so the same file draws through
 * minigl.library, through the classic minigl.library on Warp3D, and through
 * Mesa: a fogged, textured floor, a lit and textured cube, a lit sphere with
 * a highlight, a see-through pane, all Z-buffered. Textures are made here;
 * nothing is read from disk. scene_frame(t) draws the picture for t seconds
 * in, the same picture for the same t everywhere.
 *
 * The including file provides the GL header (MGLTEST_GL_HEADER) and, where
 * the C library has none, sinf and cosf (MGLTEST_NO_LIBM). */
#ifdef MGLTEST_GL_HEADER
#include MGLTEST_GL_HEADER
#else
#include <GL/gl.h>
#endif

#ifdef MGLTEST_NO_LIBM
float mgltest_sin(float x);
float mgltest_cos(float x);
#define SIN mgltest_sin
#define COS mgltest_cos
#else
#include <math.h>
#define SIN sinf
#define COS cosf
#endif

#define TEXN 3
static GLuint tex[TEXN];
static int sw, sh;

/* 64 x 64 RGBA: a checked floor, bricks, and a soft round spot (alpha). */
static void make_textures(void)
{
    static unsigned char img[64 * 64 * 4];
    int x, y, i;
    glGenTextures(TEXN, tex);
    for (i = 0; i < TEXN; i++) {
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++) {
                unsigned char *p = &img[(y * 64 + x) * 4];
                if (i == 0) {
                    int k = ((x >> 3) ^ (y >> 3)) & 1;
                    p[0] = k ? 200 : 60; p[1] = k ? 190 : 70; p[2] = k ? 160 : 90; p[3] = 255;
                } else if (i == 1) {
                    int row = y >> 3, off = (row & 1) ? 8 : 0, mortar = (y & 7) == 0 || ((x + off) & 15) == 0;
                    p[0] = mortar ? 210 : (unsigned char)(150 + ((x * 7 + y * 3) & 31));
                    p[1] = mortar ? 205 : (unsigned char)(60 + ((x * 3) & 15));
                    p[2] = mortar ? 190 : 40;
                    p[3] = 255;
                } else {
                    int dx = x - 32, dy = y - 32, d = dx * dx + dy * dy, a = 255 - d / 4;
                    p[0] = 120; p[1] = 200; p[2] = 255; p[3] = (unsigned char)(a < 40 ? 40 : a > 255 ? 255 : a);
                }
            }
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, i == 2 ? GL_RGBA : GL_RGB, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
}

int scene_init(int w, int h)
{
    static const GLfloat fogc[4] = { 0.55f, 0.62f, 0.75f, 1.0f };
    static const GLfloat l0pos[4] = { -0.4f, 1.0f, 0.6f, 0.0f };
    static const GLfloat l0dif[4] = { 0.9f, 0.9f, 0.85f, 1.0f };
    static const GLfloat l0spe[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const GLfloat l1dif[4] = { 0.8f, 0.3f, 0.2f, 1.0f };
    static const GLfloat amb[4] = { 0.25f, 0.25f, 0.3f, 1.0f };
    sw = w; sh = h;
    glViewport(0, 0, w, h);
    glClearColor(fogc[0], fogc[1], fogc[2], 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 4.0f);
    glFogf(GL_FOG_END, 22.0f);
    glFogfv(GL_FOG_COLOR, (GLfloat *)fogc);
    glEnable(GL_FOG);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, l0dif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, l0spe);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, l1dif);
    glLightf(GL_LIGHT1, GL_LINEAR_ATTENUATION, 0.15f);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.1 * w / h, 0.1 * w / h, -0.1, 0.1, 0.2, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, l0pos);
    make_textures();
    return 0;
}

static void floor_grid(void)
{
    int i, j;
    glNormal3f(0.0f, 1.0f, 0.0f);
    for (j = -8; j < 8; j++) {
        glBegin(GL_QUAD_STRIP);
        for (i = -8; i <= 8; i++) {
            glTexCoord2f(i * 0.5f, j * 0.5f);
            glVertex3f(i * 1.5f, 0.0f, j * 1.5f);
            glTexCoord2f(i * 0.5f, (j + 1) * 0.5f);
            glVertex3f(i * 1.5f, 0.0f, (j + 1) * 1.5f);
        }
        glEnd();
    }
}

static void cube(void)
{
    static const signed char f[6][4][3] = {
        { { 1, -1, 1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } },
        { { -1, -1, -1 }, { -1, -1, 1 }, { -1, 1, 1 }, { -1, 1, -1 } },
        { { -1, 1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { -1, 1, -1 } },
        { { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 } },
        { { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
        { { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 } },
    };
    static const signed char n[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    static const float st[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    int i, k;
    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++) {
        glNormal3f(n[i][0], n[i][1], n[i][2]);
        for (k = 0; k < 4; k++) {
            glTexCoord2f(st[k][0], st[k][1]);
            glVertex3f(f[i][k][0], f[i][k][1], f[i][k][2]);
        }
    }
    glEnd();
}

static void sphere(float r, int sl, int stk)
{
    int i, j;
    for (j = 0; j < stk; j++) {
        float a0 = 3.14159265f * j / stk, a1 = 3.14159265f * (j + 1) / stk;
        float z0 = COS(a0), z1 = COS(a1), s0 = SIN(a0), s1 = SIN(a1);
        glBegin(GL_TRIANGLE_STRIP);
        for (i = 0; i <= sl; i++) {
            float b = 6.2831853f * i / sl, cb = COS(b), sb = SIN(b);
            glNormal3f(cb * s1, z1, sb * s1);
            glVertex3f(cb * s1 * r, z1 * r, sb * s1 * r);
            glNormal3f(cb * s0, z0, sb * s0);
            glVertex3f(cb * s0 * r, z0 * r, sb * s0 * r);
        }
        glEnd();
    }
}

void scene_frame(float t)
{
    static const GLfloat white[4] = { 1, 1, 1, 1 }, grey[4] = { 0.2f, 0.2f, 0.2f, 1 }, none[4] = { 0, 0, 0, 1 };
    static const GLfloat red[4] = { 0.9f, 0.25f, 0.2f, 1 }, spec[4] = { 0.9f, 0.9f, 0.9f, 1 };
    GLfloat l1pos[4];
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, -1.6f, -9.0f);
    glRotatef(14.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(t * 20.0f, 0.0f, 1.0f, 0.0f);
    l1pos[0] = 3.0f * COS(t); l1pos[1] = 1.5f; l1pos[2] = 3.0f * SIN(t); l1pos[3] = 1.0f;
    glLightfv(GL_LIGHT1, GL_POSITION, l1pos);
    glEnable(GL_LIGHTING);

    /* the floor: textured and lit, fading into the fog */
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, white);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, none);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    floor_grid();

    /* the cube: bricks, lit, turning */
    glPushMatrix();
    glTranslatef(-1.6f, 1.1f, 0.0f);
    glRotatef(t * 50.0f, 0.3f, 1.0f, 0.2f);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    cube();
    glPopMatrix();

    /* the sphere: no texture, a red body with a white highlight */
    glDisable(GL_TEXTURE_2D);
    glPushMatrix();
    glTranslatef(1.6f, 1.2f + 0.3f * SIN(t * 2.0f), 0.4f);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, red);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32.0f);
    sphere(1.1f, 20, 12);
    glPopMatrix();
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, none);

    /* the pane: blended over what is behind it, unlit */
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex[2]);
    glColor4f(1.0f, 1.0f, 1.0f, 0.8f);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-1.0f, 0.4f, 2.2f);
    glTexCoord2f(1, 0); glVertex3f(1.0f, 0.4f, 2.2f);
    glTexCoord2f(1, 1); glVertex3f(1.0f, 2.4f, 2.2f);
    glTexCoord2f(0, 1); glVertex3f(-1.0f, 2.4f, 2.2f);
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, grey);
}

void scene_done(void)
{
    glDeleteTextures(TEXN, tex);
}

/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * W3DTest: a Warp3D program that draws a textured, Z-buffered, fogged
 * scene (a floor into the fog, two turning cubes, a see-through pane) and
 * reports frames a second, so drivers can be compared on the same pictures:
 *   W3DTest [CPU|GPU|AUTO] [W n] [H n] [DEPTH 16|32] [FRAMES n] [SNAP file]
 * CPU asks for W3D_DRIVER_CPU, GPU for W3D_DRIVER_3DHW (OpenGPU), AUTO lets
 * Warp3D choose. SNAP writes the first frame as raw ARGB (8-byte header:
 * width, height) and prints its checksum: the same frame from two drivers
 * should match within the 3D tolerance. Uses only the Warp3D API, as a
 * game would, and only FPU instructions a 68040 has. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/modeid.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/Warp3D.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *Warp3DBase, *CyberGfxBase;

static float fsin(float x)
{
    float r = 0, t;
    int i;
    while (x > 3.14159265f) x -= 6.2831853f;
    while (x < -3.14159265f) x += 6.2831853f;
    t = x;
    for (i = 1; i < 10; i++) { r += t; t *= -x * x / (float)((2 * i) * (2 * i + 1)); }
    return r;
}
static float fcos(float x) { return fsin(x + 1.5707963f); }

static int W = 320, H = 240;
static W3D_Context *ctx;
static W3D_Texture *brick, *checker;

/* A camera-space point to a Warp3D vertex: x, y screen; z 0..1 over 1..20; w = 1/z. */
static void project(W3D_Vertex *v, float x, float y, float z, float u, float tv, float r, float g, float b, float a)
{
    float f = (float)H * 0.9f;
    if (z < 0.5f) z = 0.5f;
    v->x = (float)W * 0.5f + x * f / z;
    v->y = (float)H * 0.5f - y * f / z;
    v->z = (z - 1.0f) / 19.0f;
    if (v->z < 0) v->z = 0;
    if (v->z > 1) v->z = 1;
    v->w = 1.0f / z;
    v->u = u; v->v = tv; v->tex3d = 0;
    v->color.r = r; v->color.g = g; v->color.b = b; v->color.a = a;
    v->spec.r = v->spec.g = v->spec.b = 0;
    v->l = 0;
}

static void floor_(void)
{
    int i, j;
    for (j = 0; j < 9; j++) {
        W3D_Vertex v[20];
        W3D_Triangles t;
        for (i = 0; i <= 8; i++) {
            float x = -8.0f + i * 2.0f, z0 = 1.5f + j * 2.0f, z1 = z0 + 2.0f;
            float sh = 0.6f + 0.05f * (float)((i + j) & 3);
            project(&v[i * 2], x, -1.2f, z0, i * 64.0f, j * 64.0f, sh, sh, 1.0f, 1.0f);
            project(&v[i * 2 + 1], x, -1.2f, z1, i * 64.0f, (j + 1) * 64.0f, 1.0f, sh, sh, 1.0f);
        }
        t.vertexcount = 18; t.v = v; t.tex = checker; t.st_pattern = 0;
        W3D_DrawTriStrip(ctx, &t);
    }
}

static void cube(float cx, float cy, float cz, float s, float a1, float a2)
{
    static const signed char corner[8][3] = { {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}, {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} };
    static const unsigned char face[6][4] = { {0,1,2,3}, {5,4,7,6}, {4,0,3,7}, {1,5,6,2}, {3,2,6,7}, {4,5,1,0} };
    float p[8][3], c1 = fcos(a1), s1 = fsin(a1), c2 = fcos(a2), s2 = fsin(a2);
    int i, f;
    for (i = 0; i < 8; i++) {
        float x = corner[i][0] * s, y = corner[i][1] * s, z = corner[i][2] * s, t;
        t = x * c1 - z * s1; z = x * s1 + z * c1; x = t;
        t = y * c2 - z * s2; z = y * s2 + z * c2; y = t;
        p[i][0] = cx + x; p[i][1] = cy + y; p[i][2] = cz + z;
    }
    for (f = 0; f < 6; f++) {
        W3D_Vertex v[4];
        W3D_Triangles t;
        static const float uv[4][2] = { {0,0}, {128,0}, {128,128}, {0,128} };
        float lum = 0.55f + 0.08f * f;
        for (i = 0; i < 4; i++) {
            const float *q = p[face[f][i]];
            project(&v[i], q[0], q[1], q[2], uv[i][0], uv[i][1], lum, lum, lum, 1.0f);
        }
        t.vertexcount = 4; t.v = v; t.tex = brick; t.st_pattern = 0;
        W3D_DrawTriFan(ctx, &t);
    }
}

static void pane(float a)
{
    W3D_Triangle t;
    W3D_SetState(ctx, W3D_BLENDING, W3D_ENABLE);
    W3D_SetState(ctx, W3D_TEXMAPPING, W3D_DISABLE);
    W3D_SetState(ctx, W3D_ZBUFFERUPDATE, W3D_DISABLE);
    project(&t.v1, -1.6f + 0.3f * fsin(a), 1.0f, 3.0f, 0, 0, 1.0f, 0.3f, 0.2f, 0.7f);
    project(&t.v2, 1.6f, 0.6f, 3.5f, 0, 0, 0.2f, 1.0f, 0.3f, 0.4f);
    project(&t.v3, -0.4f, -1.0f, 3.2f, 0, 0, 0.3f, 0.4f, 1.0f, 0.6f);
    t.tex = 0; t.st_pattern = 0;
    W3D_DrawTriangle(ctx, &t);
    W3D_SetState(ctx, W3D_BLENDING, W3D_DISABLE);
    W3D_SetState(ctx, W3D_TEXMAPPING, W3D_ENABLE);
    W3D_SetState(ctx, W3D_ZBUFFERUPDATE, W3D_ENABLE);
}

static void frame(int n)
{
    W3D_Double far = 1.0;
    float a = n * 0.05f;
    W3D_LockHardware(ctx);
    W3D_ClearDrawRegion(ctx, 0xFF243040UL);
    W3D_ClearZBuffer(ctx, &far);
    floor_();
    cube(0.0f, 0.0f, 5.0f, 1.0f, a, a * 0.7f);
    cube(-2.4f, 0.6f, 9.0f, 1.2f, -a * 0.8f, 0.4f);
    cube(3.0f, -0.2f, 13.0f, 1.5f, a * 0.5f, a);
    pane(a);
    W3D_UnLockHardware(ctx);
}

static long ticks(void)
{
    struct DateStamp d;
    DateStamp(&d);
    return (d.ds_Days % 1000) * 4320000L + d.ds_Minute * 3000L + d.ds_Tick;
}

static W3D_Texture *texture(int w, int h, ULONG fmt, void *img, ULONG mips)
{
    ULONG err = 0;
    W3D_Texture *t = W3D_AllocTexObjTags(ctx, &err, W3D_ATO_IMAGE, (ULONG)img, W3D_ATO_FORMAT, fmt, W3D_ATO_WIDTH, w,
                                         W3D_ATO_HEIGHT, h, mips ? W3D_ATO_MIPMAP : TAG_IGNORE, mips, TAG_DONE);
    if (!t) printf("W3DTest: texture %dx%d format %lu: error %ld\n", w, h, (unsigned long)fmt, (long)err);
    return t;
}

int main(int argc, char **argv)
{
    struct Screen *scr;
    ULONG type = W3D_DRIVER_BEST, err = 0, mode, depth = 16;
    int frames = 100, i, rc = 0;
    const char *snap = 0, *tname = "AUTO";
    static ULONG brickimg[128 * 128];
    static UWORD checkimg[64 * 64];
    W3D_Fog fog;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "CPU")) { type = W3D_DRIVER_CPU; tname = "CPU"; }
        else if (!strcmp(argv[i], "GPU")) { type = W3D_DRIVER_3DHW; tname = "GPU"; }
        else if (!strcmp(argv[i], "AUTO")) type = W3D_DRIVER_BEST;
        else if (!strcmp(argv[i], "W") && i + 1 < argc) W = atoi(argv[++i]);
        else if (!strcmp(argv[i], "H") && i + 1 < argc) H = atoi(argv[++i]);
        else if (!strcmp(argv[i], "DEPTH") && i + 1 < argc) depth = (ULONG)atoi(argv[++i]);
        else if (!strcmp(argv[i], "FRAMES") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "SNAP") && i + 1 < argc) snap = argv[++i];
    }
    Warp3DBase = OpenLibrary("Warp3D.library", 4);
    CyberGfxBase = OpenLibrary("cybergraphics.library", 40);
    if (!Warp3DBase || !CyberGfxBase) { printf("W3DTest: needs Warp3D.library 4 and cybergraphics.library\n"); return 20; }
    printf("W3DTest: %s %d.%d, driver %s, %dx%d, %lu bits\n", Warp3DBase->lib_Node.ln_Name, Warp3DBase->lib_Version,
           Warp3DBase->lib_Revision, tname, W, H, (unsigned long)depth);
    {
        W3D_Driver **d = W3D_GetDrivers();
        while (d && *d) { printf("  driver: %s%s\n", (*d)->name, (*d)->swdriver ? " (CPU)" : ""); d++; }
    }
    mode = W3D_BestModeIDTags(W3D_BMI_WIDTH, W, W3D_BMI_HEIGHT, H, W3D_BMI_DEPTH, depth, TAG_DONE);
    if (mode == (ULONG)INVALID_ID) { printf("W3DTest: no %dx%d %lu-bit mode\n", W, H, (unsigned long)depth); rc = 20; goto out0; }
    scr = OpenScreenTags(0, SA_DisplayID, mode, SA_Width, W, SA_Height, H, SA_Depth, depth, SA_Quiet, TRUE,
                         SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, SA_Title, (ULONG)"W3DTest", TAG_DONE);
    if (!scr) { printf("W3DTest: no screen\n"); rc = 20; goto out0; }
    ctx = W3D_CreateContextTags(&err, W3D_CC_BITMAP, (ULONG)scr->RastPort.BitMap, W3D_CC_YOFFSET, 0,
                                W3D_CC_DRIVERTYPE, type, TAG_DONE);
    if (!ctx) { printf("W3DTest: W3D_CreateContext: error %ld\n", (long)err); rc = 20; goto out1; }
    printf("  context: %s driver, %dx%d, format %08lx\n", ctx->drivertype == W3D_DRIVER_CPU ? "CPU" : "3D hardware",
           ctx->width, ctx->height, (unsigned long)ctx->format);
    if (W3D_AllocZBuffer(ctx) != W3D_SUCCESS) printf("W3DTest: no Z buffer\n");
    /* a brick texture with mip levels (ARGB32) and a checker (R5G6B5) */
    for (i = 0; i < 128 * 128; i++) {
        int x = i & 127, y = i >> 7, row = y / 16, mortar = (y % 16) < 2 || ((x + (row & 1) * 16) % 32) < 2;
        ULONG r = mortar ? 200 : 150 + ((x * 7 + y * 13) & 31), g = mortar ? 190 : 60 + ((x * 3) & 15), b = mortar ? 170 : 40;
        brickimg[i] = 0xFF000000UL | (r << 16) | (g << 8) | b;
    }
    for (i = 0; i < 64 * 64; i++) {
        int x = i & 63, y = i >> 6;
        checkimg[i] = ((x / 8 + y / 8) & 1) ? 0xFFFF : 0x4A69;
    }
    brick = texture(128, 128, W3D_A8R8G8B8, brickimg, 0xFFFF);
    checker = texture(64, 64, W3D_R5G6B5, checkimg, 0);
    if (!brick || !checker) { rc = 20; goto out2; }
    W3D_SetFilter(ctx, brick, W3D_LINEAR_MIP_NEAREST, W3D_LINEAR);
    W3D_SetFilter(ctx, checker, W3D_LINEAR, W3D_LINEAR);
    W3D_SetTexEnv(ctx, brick, W3D_MODULATE, 0);
    W3D_SetTexEnv(ctx, checker, W3D_MODULATE, 0);
    W3D_SetWrapMode(ctx, checker, W3D_REPEAT, W3D_REPEAT, 0);
    W3D_SetState(ctx, W3D_TEXMAPPING, W3D_ENABLE);
    W3D_SetState(ctx, W3D_PERSPECTIVE, W3D_ENABLE);
    W3D_SetState(ctx, W3D_GOURAUD, W3D_ENABLE);
    W3D_SetState(ctx, W3D_ZBUFFER, W3D_ENABLE);
    W3D_SetState(ctx, W3D_ZBUFFERUPDATE, W3D_ENABLE);
    W3D_SetZCompareMode(ctx, W3D_Z_LESS);
    W3D_SetBlendMode(ctx, W3D_SRC_ALPHA, W3D_ONE_MINUS_SRC_ALPHA);
    fog.fog_start = 1.0f / 4.0f; fog.fog_end = 1.0f / 19.0f; fog.fog_density = 1.0f;
    fog.fog_color.r = 0x24 / 255.0f; fog.fog_color.g = 0x30 / 255.0f; fog.fog_color.b = 0x40 / 255.0f;
    W3D_SetFogParams(ctx, &fog, W3D_FOG_LINEAR);
    W3D_SetState(ctx, W3D_FOGGING, W3D_ENABLE);

    frame(0);
    if (snap) {
        ULONG *buf = AllocVec((ULONG)W * H * 4 + 8, MEMF_ANY), h = 2166136261UL;
        if (buf) {
            BPTR f;
            struct RastPort rp;
            UBYTE *p;
            InitRastPort(&rp);
            rp.BitMap = scr->RastPort.BitMap;
            buf[0] = (ULONG)W; buf[1] = (ULONG)H;
            ReadPixelArray(buf + 2, 0, 0, (UWORD)(W * 4), &rp, 0, 0, (UWORD)W, (UWORD)H, RECTFMT_ARGB);
            for (p = (UBYTE *)(buf + 2), i = 0; i < W * H * 4; i++) h = (h ^ p[i]) * 16777619UL;
            if ((f = Open((STRPTR)snap, MODE_NEWFILE))) { Write(f, buf, (LONG)W * H * 4 + 8); Close(f); }
            printf("  first frame: checksum %08lx, written to %s\n", (unsigned long)h, snap);
            FreeVec(buf);
        }
    }
    {
        long t0 = ticks(), t1;
        for (i = 1; i <= frames; i++) frame(i);
        t1 = ticks();
        if (t1 > t0) {
            long fps100 = (long)frames * 5000L / (t1 - t0);     /* frames a second, times 100 */
            printf("  %d frames in %ld.%02ld s: %ld.%02ld frames a second\n", frames, (t1 - t0) / 50, ((t1 - t0) % 50) * 2,
                   fps100 / 100, fps100 % 100);
        } else printf("  %d frames in under a tick\n", frames);
    }
out2:
    if (brick) W3D_FreeTexObj(ctx, brick);
    if (checker) W3D_FreeTexObj(ctx, checker);
    W3D_DestroyContext(ctx);
out1:
    CloseScreen(scr);
out0:
    CloseLibrary(CyberGfxBase);
    CloseLibrary(Warp3DBase);
    return rc;
}

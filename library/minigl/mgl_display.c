/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library on AmigaOS: where a context draws, and the mgl* calls.
 *   - A screen of its own (the default): an RTG screen with two or more
 *     buffers (ScreenBuffers); a frame is drawn into the hidden one and
 *     shown by mglSwitchDisplay. A borderless backdrop window takes the
 *     input.
 *   - A window (mglChooseWindowMode, mglCreateContextFromWindow): frames
 *     are drawn off screen and copied into the window.
 *   - A bitmap the program owns (mglCreateContextFromBitMap).
 * R5G6B5 and A8R8G8B8 are drawn in place; other formats through an ARGB32
 * copy written back with WritePixelArray. mglMainLoop gives the program's
 * key, mouse and idle functions their turn. */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>

#include "mgl_internal.h"

extern struct Library *CyberGfxBase;

/* Warp3D's names for drawing formats, which MGLLockInfo has always carried. */
#define W3D_FMT_R5G6B5   (1 << 5)
#define W3D_FMT_A8R8G8B8 (1 << 11)

enum { MODE_SCREEN, MODE_WINDOW, MODE_APPWINDOW, MODE_BITMAP };

struct mgl_display {
    int mode;
    struct Screen *screen;
    struct Window *window;
    struct ScreenBuffer *sb[3];
    int nbuf, back;                 /* buffers, the one drawn into */
    struct BitMap *off;             /* window modes: the frame, drawn off screen */
    ULONG *argb;                    /* formats the core can't draw into: an ARGB32 copy */
    ULONG argbbpr;
    int direct;
    struct BitMap *appbm;
    APTR lock;                      /* LockBitMapTags while a batch runs */
    APTR backlock;                  /* mglLockBack's */
    UWORD *blank;                   /* the empty pointer (chip memory) */
    int ox, oy;                     /* the frame's place in the window */
    ULONG buttons;
};

/* The bitmap the next frame is drawn into. */
static struct BitMap *back_bitmap(struct mgl_display *d)
{
    switch (d->mode) {
    case MODE_SCREEN: return d->off ? d->off : d->nbuf ? d->sb[d->back]->sb_BitMap : d->screen->RastPort.BitMap;
    case MODE_BITMAP: return d->appbm;
    }
    return d->off;
}

static int ogpu_format_of(struct BitMap *bm, int *direct)
{
    ULONG pf = GetCyberMapAttr(bm, CYBRMATTR_PIXFMT);
    *direct = pf == PIXFMT_RGB16 || pf == PIXFMT_ARGB32;
    return pf == PIXFMT_RGB16 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32;
}

int mgl_disp_lock(GLcontext c)
{
    struct mgl_display *d = c->disp;
    struct BitMap *bm = back_bitmap(d);
    if (!bm) return 0;
    if (!d->direct) {
        struct RastPort rp;
        InitRastPort(&rp);
        rp.BitMap = bm;
        ReadPixelArray(d->argb, 0, 0, d->argbbpr, &rp, 0, 0, c->width, c->height, RECTFMT_ARGB);
        c->base = (UBYTE *)d->argb;
        c->bpr = d->argbbpr;
        return 1;
    }
    if (d->backlock) return 1;                  /* mglLockBack has it */
    {
        ULONG base = 0, bpr = 0;
        d->lock = LockBitMapTags(bm, LBMI_BASEADDRESS, (ULONG)&base, LBMI_BYTESPERROW, (ULONG)&bpr, TAG_DONE);
        if (!d->lock) return 0;
        c->base = (UBYTE *)base;
        c->bpr = bpr;
    }
    return 1;
}

void mgl_disp_unlock(GLcontext c)
{
    struct mgl_display *d = c->disp;
    if (d->lock) { UnLockBitMap(d->lock); d->lock = 0; }
    if (!d->direct) {
        struct RastPort rp;
        InitRastPort(&rp);
        rp.BitMap = back_bitmap(d);
        WritePixelArray(d->argb, 0, 0, d->argbbpr, &rp, 0, 0, c->width, c->height, RECTFMT_ARGB);
    }
}

/* ---- opening -------------------------------------------------------------------------------- */

static void close_display(struct mgl_display *d)
{
    int i;
    if (!d) return;
    if (d->backlock) { UnLockBitMap(d->backlock); d->backlock = 0; }
    if (d->window && d->mode != MODE_APPWINDOW) {
        ClearPointer(d->window);
        CloseWindow(d->window);
    }
    if (d->screen && d->mode == MODE_SCREEN) {
        for (i = d->nbuf - 1; i >= 0; i--) if (d->sb[i]) FreeScreenBuffer(d->screen, d->sb[i]);
        CloseScreen(d->screen);
    }
    if (d->off) { WaitBlit(); FreeBitMap(d->off); }
    mgl_free(d->argb);
    if (d->blank) FreeVec(d->blank);
    mgl_free(d);
}

/* The area's format: drawn in place, or through an ARGB32 copy. */
static int setup_area(GLcontext c)
{
    struct mgl_display *d = c->disp;
    struct BitMap *bm = back_bitmap(d);
    if (!bm || !GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX)) { mgl_log("mglCreateContext: not an RTG bitmap (%lx)", (unsigned long)bm); return 0; }
    c->fmt = ogpu_format_of(bm, &d->direct);
    if (!d->direct) {
        d->argbbpr = (ULONG)c->width * 4;
        d->argb = mgl_alloc(d->argbbpr * (ULONG)c->height);
        if (!d->argb) return 0;
    }
    return 1;
}

static ULONG idcmp_flags(void)
{
    return IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_CLOSEWINDOW;
}

static int open_screen(GLcontext c, struct mgl_display *d, ULONG id, int w, int h)
{
    int depth = mgl_prefs.depth <= 16 ? 16 : 32, i;
    if (id == (ULONG)INVALID_ID || !id || id == MGL_SM_BESTMODE)
        id = BestCModeIDTags(CYBRBIDTG_NominalWidth, w, CYBRBIDTG_NominalHeight, h, CYBRBIDTG_Depth, depth, TAG_DONE);
    if (id == (ULONG)INVALID_ID) { mgl_log("mglCreateContext: no %ldx%ld %ld-bit mode", (long)w, (long)h, (long)depth); return 0; }
    if (IsCyberModeID(id)) depth = (int)GetCyberIDAttr(CYBRIDATTR_DEPTH, id);
    if (depth > 24) depth = 24;                 /* RTG screens: 24 opens the 32-bit modes */
    {
        ULONG err = 0;
        d->screen = OpenScreenTags(0, SA_DisplayID, id, SA_Width, w, SA_Height, h, SA_Depth, depth,
                                   SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN,
                                   SA_Title, (ULONG)"MiniGL", SA_ErrorCode, (ULONG)&err, TAG_DONE);
        if (!d->screen) {
            mgl_log("mglCreateContext: no screen (mode %lx, depth %ld, error %ld)", (unsigned long)id, (long)depth, (long)err);
            return 0;
        }
    }
    d->window = OpenWindowTags(0, WA_CustomScreen, (ULONG)d->screen, WA_Left, 0, WA_Top, 0, WA_Width, w, WA_Height, h,
                               WA_Backdrop, TRUE, WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
                               WA_ReportMouse, TRUE, WA_IDCMP, idcmp_flags(), WA_SimpleRefresh, TRUE,
                               WA_NoCareRefresh, TRUE, TAG_DONE);
    if (!d->window) { mgl_log("mglCreateContext: no window"); return 0; }
    c->width = w; c->height = h;
    /* A frame is drawn off screen and copied onto the screen in one blit:
     * on RTG that is quicker than changing screen buffers, which waits for
     * the display (ENV:MiniGL/Flip = 1 asks for the buffers instead). */
    if (!mgl_prefs.flip) {
        d->off = AllocBitMap(w, h, depth, BMF_MINPLANES | BMF_CLEAR, d->screen->RastPort.BitMap);
        if (d->off) return 1;
    }
    d->nbuf = mgl_prefs.buffers < 2 ? 2 : mgl_prefs.buffers > 3 ? 3 : mgl_prefs.buffers;
    for (i = 0; i < d->nbuf; i++) {
        d->sb[i] = AllocScreenBuffer(d->screen, 0, i ? 0 : SB_SCREEN_BITMAP);
        if (!d->sb[i]) break;
        d->sb[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = 0;
        d->sb[i]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = 0;
    }
    if (i < 2) {                                /* no buffers: draw into the screen itself */
        while (i > 0) FreeScreenBuffer(d->screen, d->sb[--i]);
        d->nbuf = 0;
    } else d->nbuf = i;
    d->back = d->nbuf ? 1 : 0;
    c->width = w; c->height = h;
    return 1;
}

static int open_window(GLcontext c, struct mgl_display *d, int x, int y, int w, int h, struct Window *app)
{
    struct Screen *pub = 0;
    if (app) {
        d->window = app;
        d->mode = MODE_APPWINDOW;
        w = app->Width - app->BorderLeft - app->BorderRight;
        h = app->Height - app->BorderTop - app->BorderBottom;
    } else {
        pub = LockPubScreen(0);
        if (!pub) return 0;
        d->window = OpenWindowTags(0, WA_PubScreen, (ULONG)pub, WA_Left, x, WA_Top, y, WA_InnerWidth, w, WA_InnerHeight, h,
                                   WA_Title, (ULONG)"MiniGL", WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                                   WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_ReportMouse, TRUE, WA_IDCMP, idcmp_flags(),
                                   WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE, TAG_DONE);
        UnlockPubScreen(0, pub);
        if (!d->window) return 0;
        d->mode = MODE_WINDOW;
    }
    d->ox = d->window->BorderLeft;
    d->oy = d->window->BorderTop;
    d->off = AllocBitMap(w, h, 16, BMF_MINPLANES | BMF_CLEAR, d->window->RPort->BitMap);
    if (!d->off) return 0;
    c->width = w; c->height = h;
    return 1;
}

static GLcontext new_context(void)
{
    GLcontext c = mgl_alloc(sizeof(struct GLcontext_t));
    if (!c) return 0;
    c->disp = mgl_alloc(sizeof(struct mgl_display));
    if (!c->disp) { mgl_free(c); return 0; }
    mgl_read_prefs();
    return c;
}

static GLcontext finish(GLcontext c)
{
    if (!setup_area(c) || !mgl_out_open(c)) {
        mgl_log("mglCreateContext: no drawing area or no memory");
        mgl_out_close(c);
        close_display(c->disp);
        mgl_free(c);
        return 0;
    }
    c->no_mip = mgl_prefs.no_mip;
    mgl_init_state(c);
    mgl_current = c;
    mgl_log("mglCreateContext %ldx%ld route %ld fmt %ld direct %ld", (long)c->width, (long)c->height, (long)c->route,
            (long)c->fmt, (long)c->disp->direct);
    return c;
}

static GLcontext create(ULONG id, int x, int y, int w, int h, int window)
{
    GLcontext c = new_context();
    int ok;
    if (!c) return 0;
    if (w < 1) w = 640;
    if (h < 1) h = 480;
    if (window) ok = open_window(c, c->disp, x, y, w, h, 0);
    else { c->disp->mode = MODE_SCREEN; ok = open_screen(c, c->disp, id, w, h); }
    if (!ok) { close_display(c->disp); mgl_free(c); return 0; }
    return finish(c);
}

void *MGLCreateContext(int offx, int offy, int w, int h)
{
    return create((ULONG)INVALID_ID, offx, offy, w, h, mgl_prefs.window);
}

void *MGLCreateContextFromID(GLint ID, GLint *w, GLint *h)
{
    GLcontext c;
    int ww = w && *w > 0 ? *w : 640, hh = h && *h > 0 ? *h : 480;
    if ((ULONG)ID == MGL_SM_WINDOWMODE) c = create((ULONG)INVALID_ID, 0, 0, ww, hh, 1);
    else {
        if ((ULONG)ID != MGL_SM_BESTMODE && IsCyberModeID((ULONG)ID)) {
            ww = (int)GetCyberIDAttr(CYBRIDATTR_WIDTH, (ULONG)ID);
            hh = (int)GetCyberIDAttr(CYBRIDATTR_HEIGHT, (ULONG)ID);
        }
        c = create((ULONG)ID, 0, 0, ww, hh, 0);
    }
    if (c) { if (w) *w = c->width; if (h) *h = c->height; }
    return c;
}

void *MGLCreateContextFromWindow(struct Window *window)
{
    GLcontext c;
    if (!window) return 0;
    c = new_context();
    if (!c) return 0;
    if (!open_window(c, c->disp, 0, 0, 0, 0, window)) { close_display(c->disp); mgl_free(c); return 0; }
    return finish(c);
}

void *MGLCreateContextFromBitMap(struct BitMap *bitmap)
{
    GLcontext c;
    if (!bitmap || !GetCyberMapAttr(bitmap, CYBRMATTR_ISCYBERGFX)) return 0;
    c = new_context();
    if (!c) return 0;
    c->disp->mode = MODE_BITMAP;
    c->disp->appbm = bitmap;
    c->width = (int)GetCyberMapAttr(bitmap, CYBRMATTR_WIDTH);
    c->height = (int)GetCyberMapAttr(bitmap, CYBRMATTR_HEIGHT);
    return finish(c);
}

void MGLDeleteContext(GLcontext c)
{
    if (!c) return;
    mgl_pipe_flush(c);
    mgl_flush(c);
    mgl_free_lists(c);
    mgl_free_textures(c);
    mgl_free(c->cache);
    mgl_free(c->cache_ix);
    mgl_out_close(c);
    close_display(c->disp);
    if (mgl_current == c) mgl_current = 0;
    mgl_free(c);
}

void mgl_disp_free(GLcontext c) { close_display(c->disp); c->disp = 0; }

/* ---- frames ----------------------------------------------------------------------------------- */

GLboolean MGLLockDisplay(GLcontext c) { return c ? GL_TRUE : GL_FALSE; }

void MGLUnlockDisplay(GLcontext c)
{
    if (!c) return;
    if (c->disp->backlock) { UnLockBitMap(c->disp->backlock); c->disp->backlock = 0; }
}

void MGLLockMode(GLcontext c, GLenum lockMode) { if (c) c->lock_mode = lockMode; }
void MGLEnableSync(GLcontext c, GLboolean enable) { if (c) c->sync = enable; }

GLboolean MGLLockBack(GLcontext c, MGLLockInfo *info)
{
    struct mgl_display *d;
    ULONG base = 0, bpr = 0;
    if (!c) return GL_FALSE;
    d = c->disp;
    mgl_pipe_flush(c);
    mgl_flush(c);
    if (!d->backlock) {
        d->backlock = LockBitMapTags(back_bitmap(d), LBMI_BASEADDRESS, (ULONG)&base, LBMI_BYTESPERROW, (ULONG)&bpr, TAG_DONE);
        if (!d->backlock) return GL_FALSE;
        c->base = (UBYTE *)base;
        c->bpr = bpr;
    }
    if (info) {
        info->width = (ULONG)c->width;
        info->height = (ULONG)c->height;
        info->depth = c->fmt == OGPU_FMT_RGB565 ? 16 : 32;
        info->pixel_format = c->fmt == OGPU_FMT_RGB565 ? W3D_FMT_R5G6B5 : W3D_FMT_A8R8G8B8;
        info->base_address = c->base;
        info->pitch = c->bpr;
    }
    return GL_TRUE;
}

void MGLSwitchDisplay(GLcontext c)
{
    struct mgl_display *d;
    if (!c) return;
    d = c->disp;
    mgl_log("switch %ld", (long)c->frames);
    mgl_pipe_flush(c);
    mgl_flush(c);
    MGLUnlockDisplay(c);
    c->frames++;
    switch (d->mode) {
    case MODE_SCREEN:
        if (d->off) {
            if (c->sync) WaitTOF();
            BltBitMapRastPort(d->off, 0, 0, &d->screen->RastPort, 0, 0, c->width, c->height, 0xC0);
        } else if (d->nbuf) {
            /* shown at the next display change; when the last change is still
             * waiting, this frame's buffer stays the one drawn into */
            ULONG t0 = mgl_trace > 0 ? mgl_millis() : 0;
            if (c->sync) WaitTOF();
            if (ChangeScreenBuffer(d->screen, d->sb[d->back])) d->back = (d->back + 1) % d->nbuf;
            if (mgl_trace > 0) mgl_log("change: %ld ms", (long)(mgl_millis() - t0));
            else mgl_log("mglSwitchDisplay: the last change is still waiting");
        }
        break;
    case MODE_WINDOW: case MODE_APPWINDOW:
        if (c->sync) WaitTOF();
        BltBitMapRastPort(d->off, 0, 0, d->window->RPort, d->ox, d->oy, c->width, c->height, 0xC0);
        break;
    default:
        break;
    }
}

GLboolean MGLResizeContext(GLcontext c, GLsizei width, GLsizei height)
{
    struct mgl_display *d;
    struct BitMap *nb;
    UBYTE *nz;
    ULONG *na = 0;
    if (!c || width < 1 || height < 1) return GL_FALSE;
    d = c->disp;
    if (d->mode != MODE_WINDOW && d->mode != MODE_APPWINDOW) return GL_FALSE;
    mgl_pipe_flush(c);
    mgl_flush(c);
    nb = AllocBitMap(width, height, 16, BMF_MINPLANES | BMF_CLEAR, d->window->RPort->BitMap);
    nz = mgl_alloc((ULONG)width * height * (c->zfmt == OGPU_FMT_Z32 ? 4 : 2));
    if (!d->direct) na = mgl_alloc((ULONG)width * height * 4);
    if (!nb || !nz || (!d->direct && !na)) {
        if (nb) FreeBitMap(nb);
        mgl_free(nz); mgl_free(na);
        return GL_FALSE;
    }
    if (d->mode == MODE_WINDOW)
        ChangeWindowBox(d->window, d->window->LeftEdge, d->window->TopEdge,
                        width + d->window->BorderLeft + d->window->BorderRight,
                        height + d->window->BorderTop + d->window->BorderBottom);
    WaitBlit();
    FreeBitMap(d->off);
    d->off = nb;
    mgl_free(c->zbuf);
    c->zbuf = nz;
    c->zbpr = (ULONG)width * (c->zfmt == OGPU_FMT_Z32 ? 4 : 2);
    if (!d->direct) { mgl_free(d->argb); d->argb = na; d->argbbpr = (ULONG)width * 4; }
    c->width = width; c->height = height;
    if (!(c->en & EN_SCISSOR)) { c->sc_w = width; c->sc_h = height; }
    mgl_viewport_changed(c);
    c->clip_dirty = 1;
    return GL_TRUE;
}

void *MGLGetWindowHandle(GLcontext c) { return c ? c->disp->window : 0; }
void *MGLGetInputWindowHandle(GLcontext c) { return c ? c->disp->window : 0; }

/* mglSetPointer hides the pointer, mglClearPointer brings it back, as MiniGL has them. */
void MGLSetPointer(GLcontext c)
{
    struct mgl_display *d;
    if (!c) return;
    d = c->disp;
    if (!d->blank) d->blank = AllocVec(16, MEMF_CHIP | MEMF_CLEAR);
    if (d->window && d->blank) SetPointer(d->window, d->blank, 1, 1, 0, 0);
}

void MGLClearPointer(GLcontext c)
{
    if (c && c->disp->window) ClearPointer(c->disp->window);
}

/* ---- the main loop ---------------------------------------------------------------------------- */

void MGLIdleFunc(GLcontext c, IdleFn i) { if (c) c->idle = i; }
void MGLKeyFunc(GLcontext c, KeyHandlerFn k) { if (c) c->key = k; }
void MGLMouseFunc(GLcontext c, MouseHandlerFn m) { if (c) c->mouse = m; }
void MGLSpecialFunc(GLcontext c, SpecialHandlerFn s) { if (c) c->special = s; }
void MGLExit(GLcontext c) { if (c) c->quit = 1; }

/* One round of the window's messages; 0 when the window was closed. */
int mgl_events(GLcontext c, void (*glut_key)(int key), void (*glut_special)(int key))
{
    struct mgl_display *d = c->disp;
    struct IntuiMessage *m;
    int alive = 1;
    if (!d->window) return 1;
    while ((m = (struct IntuiMessage *)GetMsg(d->window->UserPort)) != 0) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        WORD mx = m->MouseX - d->ox, my = m->MouseY - d->oy;
        ReplyMsg(&m->ExecMessage);
        switch (cls) {
        case IDCMP_VANILLAKEY:
            if (glut_key) glut_key(code);
            else if (c->key) c->key((char)code);
            break;
        case IDCMP_RAWKEY: {
            int sp = -1;
            if (code >= 0x50 && code <= 0x59) sp = MGLKEY_F1 + (code - 0x50);
            else if (code == 0x4C) sp = MGLKEY_CUP;
            else if (code == 0x4D) sp = MGLKEY_CDOWN;
            else if (code == 0x4E) sp = MGLKEY_CRIGHT;
            else if (code == 0x4F) sp = MGLKEY_CLEFT;
            if (sp >= 0) {
                if (glut_special) glut_special(sp);
                else if (c->special) c->special((MGLspecial)sp);
            }
            break;
        }
        case IDCMP_MOUSEBUTTONS:
            switch (code) {
            case SELECTDOWN: d->buttons |= MGL_BUTTON_LEFT; break;
            case SELECTUP: d->buttons &= ~MGL_BUTTON_LEFT; break;
            case MENUDOWN: d->buttons |= MGL_BUTTON_RIGHT; break;
            case MENUUP: d->buttons &= ~MGL_BUTTON_RIGHT; break;
            case MIDDLEDOWN: d->buttons |= MGL_BUTTON_MID; break;
            case MIDDLEUP: d->buttons &= ~MGL_BUTTON_MID; break;
            }
            /* fall through */
        case IDCMP_MOUSEMOVE:
            if (c->mouse) c->mouse(mx, my, d->buttons);
            break;
        case IDCMP_CLOSEWINDOW:
            alive = 0;
            break;
        }
    }
    return alive;
}

ULONG mgl_event_signal(GLcontext c)
{
    return c->disp->window ? 1UL << c->disp->window->UserPort->mp_SigBit : 0;
}

void MGLMainLoop(GLcontext c)
{
    if (!c) return;
    c->quit = 0;
    while (!c->quit) {
        if (!mgl_events(c, 0, 0)) break;
        if (c->quit) break;
        if (c->idle) c->idle();
        else {
            ULONG s = mgl_event_signal(c) | SIGBREAKF_CTRL_C;
            if (Wait(s) & SIGBREAKF_CTRL_C) break;
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) break;
    }
}

/* ---- choices before a context ---------------------------------------------------------------- */

void LIB_mglChoosePixelDepth(int depth) { mgl_prefs.depth = depth; }
void LIB_mglChooseNumberOfBuffers(int number) { mgl_prefs.buffers = number; }
void LIB_mglChooseWindowMode(GLboolean flag) { mgl_prefs.window = flag ? 1 : 0; }
void LIB_mglChooseZBufferDepth(int bits) { mgl_prefs.zbits = bits >= 24 ? 32 : 16; }
void LIB_mglChooseVertexBufferSize(int size) { (void)size; }
void LIB_mglChooseTextureBufferSize(int size) { (void)size; }
void LIB_mglChooseMtexBufferSize(int size) { (void)size; }
void LIB_mglChooseGuardBand(GLboolean flag) { mgl_prefs.guard = flag; }
void LIB_mglProhibitAlphaFallback(GLboolean flag) { mgl_prefs.alpha_fallback = !flag; }
void LIB_mglProhibitMipMapping(GLboolean flag) { mgl_prefs.no_mip = flag ? 1 : 0; if (mgl_current) mgl_current->no_mip = mgl_prefs.no_mip; }
void LIB_mglProposeCloseDesktop(GLboolean closeme) { mgl_prefs.close_desktop = closeme; }

/* A second texture unit is drawn as each triangle comes: there is no buffer to draw. */
void MGLDrawMultitexBuffer(GLcontext c, GLenum BSrc, GLenum BDst, GLenum TexEnv) { (void)c; (void)BSrc; (void)BDst; (void)TexEnv; }

GLint LIB_mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn)
{
    ULONG id = INVALID_ID;
    if (!CallbackFn) return (GLint)MGL_SM_BESTMODE;
    while ((id = NextDisplayInfo(id)) != (ULONG)INVALID_ID) {
        MGLScreenMode sm;
        struct NameInfo ni;
        int i;
        if (!IsCyberModeID(id)) continue;
        sm.bit_depth = (GLint)GetCyberIDAttr(CYBRIDATTR_DEPTH, id);
        if (sm.bit_depth < 15) continue;
        sm.id = (GLint)id;
        sm.width = (GLint)GetCyberIDAttr(CYBRIDATTR_WIDTH, id);
        sm.height = (GLint)GetCyberIDAttr(CYBRIDATTR_HEIGHT, id);
        sm.mode_name[0] = 0;
        if (GetDisplayInfoData(0, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id) > 0)
            for (i = 0; i < MGL_MAX_MODE - 1 && ni.Name[i]; i++) sm.mode_name[i] = (char)ni.Name[i], sm.mode_name[i + 1] = 0;
        if (CallbackFn(&sm) == GL_TRUE) return sm.id;
    }
    return (GLint)MGL_SM_BESTMODE;
}

/* ---- a picture of the frame -------------------------------------------------------------------- */

void MGLWriteShotPPM(GLcontext c, char *filename)
{
    BPTR f;
    ULONG *row;
    UBYTE *out;
    char head[32];
    int y, x, n = 0;
    LONG v;
    if (!c || !filename) return;
    row = mgl_alloc((ULONG)c->width * 4);
    out = mgl_alloc((ULONG)c->width * 3);
    f = Open((STRPTR)filename, MODE_NEWFILE);
    if (!row || !out || !f) { if (f) Close(f); mgl_free(row); mgl_free(out); return; }
    /* "P6\n<w> <h>\n255\n" */
    head[n++] = 'P'; head[n++] = '6'; head[n++] = '\n';
    for (v = c->width, x = 1; v >= 10; v /= 10) x *= 10;
    for (v = c->width; x; x /= 10) head[n++] = (char)('0' + (v / x) % 10);
    head[n++] = ' ';
    for (v = c->height, x = 1; v >= 10; v /= 10) x *= 10;
    for (v = c->height; x; x /= 10) head[n++] = (char)('0' + (v / x) % 10);
    head[n++] = '\n'; head[n++] = '2'; head[n++] = '5'; head[n++] = '5'; head[n++] = '\n';
    Write(f, head, n);
    for (y = 0; y < c->height; y++) {
        mgl_readback(c, 0, y, c->width, 1, row, c->width);
        for (x = 0; x < c->width; x++) {
            out[x * 3] = (UBYTE)(row[x] >> 16); out[x * 3 + 1] = (UBYTE)(row[x] >> 8); out[x * 3 + 2] = (UBYTE)row[x];
        }
        Write(f, out, c->width * 3);
    }
    Close(f);
    mgl_free(row);
    mgl_free(out);
}

/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library: contexts, their state, locking, the Z and stencil
 * buffers. State changes only mark what the next batch must carry; the
 * batch code (w3d_batch.c) sends it before the next primitive. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "w3d_internal.h"
#include "w3d_calls.h"

/* The states this library can turn on: everything but antialiasing, volume
 * textures and multitexturing (as Wazp3D). */
#define CAN_ENABLE (W3D_AUTOTEXMANAGEMENT | W3D_SYNCHRON | W3D_INDIRECT | W3D_GLOBALTEXENV | W3D_DOUBLEHEIGHT \
    | W3D_FAST | W3D_AUTOCLIP | W3D_TEXMAPPING | W3D_PERSPECTIVE | W3D_GOURAUD | W3D_ZBUFFER | W3D_ZBUFFERUPDATE \
    | W3D_BLENDING | W3D_FOGGING | W3D_DITHERING | W3D_LOGICOP | W3D_STENCILBUFFER | W3D_ALPHATEST | W3D_SPECULAR \
    | W3D_SCISSOR | W3D_CHROMATEST | W3D_CULLFACE | W3D_FOG_COORD | W3D_LINE_STIPPLE | W3D_POLYGON_STIPPLE)

static void dirty(struct w3dctx *x) { x->r3d_dirty = 1; x->list_at = -1; }

static void newlist(struct MinList *l)
{
    l->mlh_Head = (struct MinNode *)&l->mlh_Tail;
    l->mlh_Tail = 0;
    l->mlh_TailPred = (struct MinNode *)&l->mlh_Head;
}

/* ---- contexts ------------------------------------------------------------------------ */

LIBCALL W3D_Context *LIB_W3D_CreateContext(REG(a0, ULONG *error), REG(a1, struct TagItem *tags), REG(a6, struct W3DBase *libbase))
{
    W3D_Context *c;
    struct w3dctx *x;
    struct BitMap *bm = (struct BitMap *)GetTagData(W3D_CC_BITMAP, 0, tags);
    ULONG type = GetTagData(W3D_CC_DRIVERTYPE, W3D_DRIVER_BEST, tags);
    ULONG e;
    if (error) *error = W3D_SUCCESS;
    w3d_read_prefs(libbase);
    c = w3d_alloc(sizeof *c);
    x = w3d_alloc(sizeof *x);
    if (x) {
        x->cmdmem = w3d_alloc(BATCH_WORDS * 4);
        x->vmem = w3d_alloc(VERT_BYTES);
    }
    if (!c || !x || !x->cmdmem || !x->vmem) {
        if (x) { w3d_free(x->cmdmem); w3d_free(x->vmem); }
        w3d_free(x); w3d_free(c);
        if (error) *error = W3D_NOMEMORY;
        return 0;
    }
    c->driver = x;
    x->ctx = c;
    if (type & W3D_DRIVER_CPU) x->route = ROUTE_CPU;
    else if (type & W3D_DRIVER_3DHW) {
        if (!libbase->gpu_3d) { e = W3D_NODRIVER; goto fail; }
        x->route = ROUTE_GPU;
    } else x->route = libbase->gpu_3d && !libbase->prefer_cpu && (libbase->gpu_host || libbase->prefer_gpu) ? ROUTE_GPU : ROUTE_CPU;
    c->drivertype = x->route == ROUTE_CPU ? W3D_DRIVER_CPU : W3D_DRIVER_3DHW;
    c->supportedfmt = W3D_GetDestFmt_all();
    c->state = W3D_AUTOTEXMANAGEMENT | W3D_TEXMAPPING | W3D_GOURAUD | W3D_ZBUFFERUPDATE;
    if (GetTagData(W3D_CC_INDIRECT, FALSE, tags)) c->state |= W3D_INDIRECT;
    if (GetTagData(W3D_CC_GLOBALTEXENV, FALSE, tags)) c->state |= W3D_GLOBALTEXENV;
    if (GetTagData(W3D_CC_DOUBLEHEIGHT, FALSE, tags)) c->state |= W3D_DOUBLEHEIGHT;
    if (GetTagData(W3D_CC_FAST, FALSE, tags)) c->state |= W3D_FAST;
    c->EnableMask = CAN_ENABLE;
    c->DisableMask = 0xFFFFFFFFUL;
    c->CurrentChip = W3D_CHIP_UNKNOWN;
    c->DriverVersion = W3D_LIB_VERSION;
    c->maxtexwidth = c->maxtexheight = c->maxtexwidthp = c->maxtexheightp = 2048;
    c->envsupmask = (1 << W3D_REPLACE) | (1 << W3D_DECAL) | (1 << W3D_MODULATE) | (1 << W3D_BLEND)
                  | (1 << W3D_ADD) | (1 << W3D_SUB) | (1 << W3D_OFF);
    c->globaltexenvmode = W3D_MODULATE;
    c->FrontFaceOrder = W3D_CCW;
    c->fog.fog_start = 1.0f; c->fog.fog_end = 0.0f; c->fog.fog_density = 1.0f;
    newlist(&c->restex);
    newlist(&c->tex);
    x->list_at = -1;
    x->zfunc = W3D_Z_LESS; x->afunc = W3D_A_ALWAYS; x->bsrc = W3D_ONE; x->bdst = W3D_ZERO;
    x->logicop = W3D_LO_COPY; x->fogmode = W3D_FOG_LINEAR; x->fogmode_v5 = -1;
    x->sfunc = W3D_ST_ALWAYS; x->smask = 255; x->sfail = x->szfail = x->szpass = W3D_ST_KEEP; x->swmask = 255;
    x->cmask = 15; x->penmask = 255;
    x->current.r = x->current.g = x->current.b = x->current.a = 1.0f;
    x->point_size = x->line_width = 1.0f;
    x->line_pattern = 0xFFFF; x->line_factor = 1;
    x->r3d_dirty = x->clip_dirty = 1;
    if (GetTagData(W3D_CC_W3DBM, FALSE, tags)) e = w3d_open_area(x, 0, (W3D_Bitmap *)bm, (int)GetTagData(W3D_CC_YOFFSET, 0, tags));
    else e = w3d_open_area(x, bm, 0, (int)GetTagData(W3D_CC_YOFFSET, 0, tags));
    if (e != W3D_SUCCESS) goto fail;
    return c;
fail:
    w3d_close_area(x);
    w3d_free(x->cmdmem); w3d_free(x->vmem); w3d_free(x); w3d_free(c);
    if (error) *error = e;
    return 0;
}

LIBCALL void LIB_W3D_DestroyContext(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x;
    if (!c) return;
    x = CTX(c);
    w3d_flush(x);
    LIB_W3D_FreeAllTexObj(c, libbase);
    w3d_free(x->zbuf); w3d_free(x->sbuf); w3d_free(x->stipple);
    w3d_close_area(x);
    w3d_free(x->cmdmem); w3d_free(x->vmem);
    w3d_free(x); w3d_free(c);
}

LIBCALL ULONG LIB_W3D_GetState(REG(a0, W3D_Context *c), REG(d1, ULONG state), REG(a6, struct W3DBase *libbase))
{
    return (c->state & state) ? W3D_ENABLED : W3D_DISABLED;
}

LIBCALL ULONG LIB_W3D_SetState(REG(a0, W3D_Context *c), REG(d0, ULONG state), REG(d1, ULONG action), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG old = c->state;
    if (action == W3D_ENABLE) {
        if (state & ~CAN_ENABLE) return W3D_UNSUPPORTEDSTATE;
        c->state |= state;
    } else c->state &= ~state;
    if ((old ^ c->state) & W3D_INDIRECT && !(c->state & W3D_INDIRECT)) w3d_flush(x);
    if ((old ^ c->state) & W3D_DOUBLEHEIGHT) w3d_open_area(x, c->w3dbitmap ? 0 : c->drawregion,
                                                          c->w3dbitmap ? (W3D_Bitmap *)c->drawregion : 0, c->yoffset);
    if ((old ^ c->state) & W3D_GLOBALTEXENV) x->tex_dirty = 1;
    dirty(x);
    return W3D_SUCCESS;
}

/* ---- locking, flushing ------------------------------------------------------------------- */

LIBCALL ULONG LIB_W3D_CheckDriver(REG(a6, struct W3DBase *libbase))
{
    return W3D_DRIVER_CPU | (libbase->gpu_3d ? W3D_DRIVER_3DHW : 0);
}

LIBCALL ULONG LIB_W3D_LockHardware(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase))
{
    c->HWlocked = W3D_TRUE;
    return W3D_SUCCESS;
}

LIBCALL void LIB_W3D_UnLockHardware(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase))
{
    w3d_flush(CTX(c));
    c->HWlocked = W3D_FALSE;
}

LIBCALL void LIB_W3D_WaitIdle(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase)) { w3d_flush(CTX(c)); }
LIBCALL ULONG LIB_W3D_CheckIdle(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase)) { return W3D_SUCCESS; }
LIBCALL ULONG LIB_W3D_Flush(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase)) { return w3d_flush(CTX(c)); }
LIBCALL void LIB_W3D_FlushFrame(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase)) { w3d_flush(CTX(c)); }
LIBCALL ULONG LIB_W3D_GetDriverState(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase)) { return W3D_SUCCESS; }

/* ---- the drawing area ---------------------------------------------------------------- */

LIBCALL ULONG LIB_W3D_SetDrawRegion(REG(a0, W3D_Context *c), REG(a1, struct BitMap *bm), REG(d1, int yoffset),
                                    REG(a2, W3D_Scissor *scissor), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG e;
    if (bm == c->drawregion && !c->w3dbitmap && x->direct) {
        w3d_flush(x);                           /* double buffering in one bitmap: only the offset moves */
        c->yoffset = yoffset;
    } else if ((e = w3d_open_area(x, bm, 0, yoffset)) != W3D_SUCCESS) return e;
    if (scissor) { c->scissor = *scissor; x->clip_dirty = 1; }
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetDrawRegionWBM(REG(a0, W3D_Context *c), REG(a1, W3D_Bitmap *bm), REG(a2, W3D_Scissor *scissor),
                                       REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG e = w3d_open_area(x, 0, bm, 0);
    if (e != W3D_SUCCESS) return e;
    if (scissor) { c->scissor = *scissor; x->clip_dirty = 1; }
    return W3D_SUCCESS;
}

LIBCALL void LIB_W3D_SetScissor(REG(a0, W3D_Context *c), REG(a1, W3D_Scissor *s), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!s) return;
    c->scissor = *s;
    x->clip_dirty = 1;
    x->list_at = -1;
}

LIBCALL ULONG LIB_W3D_ClearDrawRegion(REG(a0, W3D_Context *c), REG(d0, ULONG colour), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG r = w3d_fill(x, colour);
    w3d_maybe_flush(x);
    return r;
}

/* ---- effects ------------------------------------------------------------------------------- */

LIBCALL ULONG LIB_W3D_SetAlphaMode(REG(a0, W3D_Context *c), REG(d1, ULONG mode), REG(a1, W3D_Float *refval), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (mode < W3D_A_NEVER || mode > W3D_A_ALWAYS) return W3D_UNSUPPORTEDATEST;
    x->afunc = mode;
    if (refval) x->aref = *refval;
    dirty(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetBlendMode(REG(a0, W3D_Context *c), REG(d0, ULONG src), REG(d1, ULONG dst), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (src < W3D_ZERO || src > W3D_ONE_MINUS_CONSTANT_ALPHA || dst < W3D_ZERO || dst > W3D_ONE_MINUS_CONSTANT_ALPHA)
        return W3D_UNSUPPORTEDBLEND;
    /* as Warp3D lists them: some factors are for one side only */
    if (src == W3D_SRC_COLOR || src == W3D_ONE_MINUS_SRC_COLOR) return W3D_UNSUPPORTEDBLEND;
    if (dst == W3D_DST_COLOR || dst == W3D_ONE_MINUS_DST_COLOR || dst == W3D_SRC_ALPHA_SATURATE) return W3D_UNSUPPORTEDBLEND;
    x->bsrc = src; x->bdst = dst;
    dirty(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetFogParams(REG(a0, W3D_Context *c), REG(a1, W3D_Fog *fog), REG(d1, ULONG mode), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (mode < W3D_FOG_LINEAR || mode > W3D_FOG_INTERPOLATED) return W3D_UNSUPPORTEDFOG;
    if (fog) c->fog = *fog;
    x->fogmode = mode;
    x->fogmode_v5 = -1;
    dirty(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetColorMask(REG(a0, W3D_Context *c), REG(d0, ULONG r), REG(d1, ULONG g), REG(d2, ULONG b),
                                   REG(d3, ULONG a), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    x->cmask = ((UWORD)r ? 1 : 0) | ((UWORD)g ? 2 : 0) | ((UWORD)b ? 4 : 0) | ((UWORD)a ? 8 : 0);
    dirty(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetPenMask(REG(a0, W3D_Context *c), REG(d1, ULONG pen), REG(a6, struct W3DBase *libbase))
{
    CTX(c)->penmask = pen;
    dirty(CTX(c));
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetStencilFunc(REG(a0, W3D_Context *c), REG(d0, ULONG func), REG(d1, ULONG ref), REG(d2, ULONG mask),
                                     REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (func < W3D_ST_NEVER || func > W3D_ST_NOTEQUAL) return W3D_UNSUPPORTEDSTTEST;
    x->sfunc = func; x->sref = ref; x->smask = mask;
    dirty(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetStencilOp(REG(a0, W3D_Context *c), REG(d0, ULONG sfail), REG(d1, ULONG dpfail), REG(d2, ULONG dppass),
                                   REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (sfail < 1 || sfail > 8 || dpfail < 1 || dpfail > 8 || dppass < 1 || dppass > 8) return W3D_ILLEGALINPUT;
    x->sfail = sfail; x->szfail = dpfail; x->szpass = dppass;
    dirty(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetWriteMask(REG(a0, W3D_Context *c), REG(d1, ULONG mask), REG(a6, struct W3DBase *libbase))
{
    CTX(c)->swmask = mask;
    dirty(CTX(c));
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetZCompareMode(REG(a0, W3D_Context *c), REG(d1, ULONG mode), REG(a6, struct W3DBase *libbase))
{
    if (mode < W3D_Z_NEVER || mode > W3D_Z_ALWAYS) return W3D_UNSUPPORTEDZCMP;
    CTX(c)->zfunc = mode;
    dirty(CTX(c));
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetLogicOp(REG(a0, W3D_Context *c), REG(d1, ULONG op), REG(a6, struct W3DBase *libbase))
{
    if (op < W3D_LO_CLEAR || op > W3D_LO_SET) return W3D_UNSUPPORTEDLOGICOP;
    CTX(c)->logicop = op;
    dirty(CTX(c));
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_Hint(REG(a0, W3D_Context *c), REG(d0, ULONG mode), REG(d1, ULONG quality), REG(a6, struct W3DBase *libbase))
{
    if (mode < 16) CTX(c)->hints[mode] = quality;
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetCurrentColor(REG(a0, W3D_Context *c), REG(a1, W3D_Color *colour), REG(a6, struct W3DBase *libbase))
{
    if (colour) CTX(c)->current = *colour;
    dirty(CTX(c));
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetCurrentPen(REG(a0, W3D_Context *c), REG(d1, ULONG pen), REG(a6, struct W3DBase *libbase))
{
    CTX(c)->currentpen = pen;
    return W3D_SUCCESS;
}

LIBCALL void LIB_W3D_SetFrontFace(REG(a0, W3D_Context *c), REG(d0, ULONG direction), REG(a6, struct W3DBase *libbase))
{
    c->FrontFaceOrder = direction;
}

/* V5: one value by its target. */
LIBCALL ULONG LIB_W3D_SetParameter(REG(a0, W3D_Context *c), REG(d0, ULONG target), REG(a1, void *p), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!p) return W3D_ILLEGALINPUT;
    switch (target) {
    case W3D_STIPPLE_LINE: x->line_pattern = *(UWORD *)p; break;
    case W3D_STIPPLE_LINE_FACTOR: x->line_factor = (int)*(ULONG *)p; break;
    case W3D_STIPPLE_POLYGON:
        if (!x->stipple && !(x->stipple = w3d_alloc(128))) return W3D_NOMEMORY;
        w3d_flush(x);                           /* the pending batch may point at the old pattern */
        w3d_copy(x->stipple, p, 128);
        break;
    case W3D_POINT_SIZE: x->point_size = *(float *)p; break;
    case W3D_LINE_WIDTH: x->line_width = *(float *)p; break;
    case W3D_ZFOG_START: x->zfog_start = *(float *)p; break;
    case W3D_ZFOG_END: x->zfog_end = *(float *)p; break;
    case W3D_ZFOG_DENSITY: x->zfog_density = *(float *)p; break;
    case W3D_WFOG_START: c->fog.fog_start = *(float *)p; break;
    case W3D_WFOG_END: c->fog.fog_end = *(float *)p; break;
    case W3D_WFOG_DENSITY: c->fog.fog_density = *(float *)p; break;
    case W3D_FOG_MODE: {
        ULONG m = *(ULONG *)p;
        if (m > W3D_FOG_W_EXP_2) return W3D_UNSUPPORTEDFOG;
        x->fogmode_v5 = (int)m;
        break;
    }
    case W3D_FOG_COLOR: {
        W3D_Color *col = p;
        c->fog.fog_color.r = col->r; c->fog.fog_color.g = col->g; c->fog.fog_color.b = col->b;
        break;
    }
    default: return W3D_UNSUPPORTED;
    }
    dirty(x);
    return W3D_SUCCESS;
}

/* ---- Z buffer ----------------------------------------------------------------------------- */

LIBCALL ULONG LIB_W3D_AllocZBuffer(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    int bpp = libbase->z32 ? 4 : 2;
    if (x->zbuf) return W3D_SUCCESS;
    w3d_flush(x);
    x->zbpr = (ULONG)c->width * bpp;
    x->zbuf = w3d_alloc(x->zbpr * (ULONG)c->height);
    if (!x->zbuf) return W3D_NOGFXMEM;
    x->zfmt = bpp == 4 ? OGPU_FMT_Z32 : OGPU_FMT_Z16;
    c->zbuffer = x->zbuf;
    c->zbufferalloc = W3D_TRUE;
    c->zbufferlost = W3D_FALSE;
    dirty(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_FreeZBuffer(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!x->zbuf) return W3D_NOZBUFFER;
    w3d_flush(x);
    w3d_free(x->zbuf);
    x->zbuf = 0;
    c->zbuffer = 0;
    c->zbufferalloc = W3D_FALSE;
    dirty(x);
    return W3D_SUCCESS;
}

static ULONG z_to_u32(double z)
{
    if (z <= 0.0) return 0;
    if (z >= 1.0) return 0xFFFFFFFFUL;
    return (ULONG)w3d_f2l((float)(z * 1073741823.0)) << 2;
}

LIBCALL ULONG LIB_W3D_ClearZBuffer(REG(a0, W3D_Context *c), REG(a1, W3D_Double *value), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!x->zbuf) return W3D_NOZBUFFER;
    w3d_clear_buffer(x, OGPU_KIND_DEPTH, z_to_u32(value ? *value : 1.0));
    w3d_maybe_flush(x);
    return W3D_SUCCESS;
}

static double zat(struct w3dctx *x, ULONG px, ULONG py)
{
    UBYTE *p = x->zbuf + py * x->zbpr;
    if (x->zfmt == OGPU_FMT_Z32) return (double)*(ULONG *)(p + px * 4) / 4294967295.0;
    return (double)*(UWORD *)(p + px * 2) / 65535.0;
}

static void zput(struct w3dctx *x, ULONG px, ULONG py, double z)
{
    UBYTE *p = x->zbuf + py * x->zbpr;
    ULONG v = z_to_u32(z);
    if (x->zfmt == OGPU_FMT_Z32) *(ULONG *)(p + px * 4) = v;
    else *(UWORD *)(p + px * 2) = (UWORD)(v >> 16);
}

LIBCALL ULONG LIB_W3D_ReadZPixel(REG(a0, W3D_Context *c), REG(d0, ULONG px), REG(d1, ULONG py), REG(a1, W3D_Double *z),
                                 REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!x->zbuf) return W3D_NOZBUFFER;
    if (px >= (ULONG)c->width || py >= (ULONG)c->height) return W3D_ILLEGALINPUT;
    w3d_flush(x);
    *z = zat(x, px, py);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_ReadZSpan(REG(a0, W3D_Context *c), REG(d0, ULONG px), REG(d1, ULONG py), REG(d2, ULONG n),
                                REG(a1, W3D_Double *z), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG i;
    if (!x->zbuf) return W3D_NOZBUFFER;
    if (py >= (ULONG)c->height || px + n > (ULONG)c->width) return W3D_ILLEGALINPUT;
    w3d_flush(x);
    for (i = 0; i < n; i++) z[i] = zat(x, px + i, py);
    return W3D_SUCCESS;
}

LIBCALL void LIB_W3D_WriteZPixel(REG(a0, W3D_Context *c), REG(d0, ULONG px), REG(d1, ULONG py), REG(a1, W3D_Double *z),
                                 REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!x->zbuf || px >= (ULONG)c->width || py >= (ULONG)c->height) return;
    w3d_flush(x);
    zput(x, px, py, *z);
}

LIBCALL void LIB_W3D_WriteZSpan(REG(a0, W3D_Context *c), REG(d0, ULONG px), REG(d1, ULONG py), REG(d2, ULONG n),
                                REG(a1, W3D_Double *z), REG(a2, UBYTE *mask), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG i;
    if (!x->zbuf || py >= (ULONG)c->height || px + n > (ULONG)c->width) return;
    w3d_flush(x);
    for (i = 0; i < n; i++) if (!mask || mask[i]) zput(x, px + i, py, z[i]);
}

/* ---- stencil buffer ------------------------------------------------------------------------ */

LIBCALL ULONG LIB_W3D_AllocStencilBuffer(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (x->sbuf) return W3D_SUCCESS;
    w3d_flush(x);
    x->sbuf = w3d_alloc((ULONG)c->width * (ULONG)c->height);
    if (!x->sbuf) return W3D_NOGFXMEM;
    c->stencilbuffer = x->sbuf;
    c->stbufferalloc = W3D_TRUE;
    dirty(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_FreeStencilBuffer(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!x->sbuf) return W3D_NOSTENCILBUFFER;
    w3d_flush(x);
    w3d_free(x->sbuf);
    x->sbuf = 0;
    c->stencilbuffer = 0;
    c->stbufferalloc = W3D_FALSE;
    dirty(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_ClearStencilBuffer(REG(a0, W3D_Context *c), REG(a1, ULONG *value), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!x->sbuf) return W3D_NOSTENCILBUFFER;
    w3d_clear_buffer(x, OGPU_KIND_STENCIL, value ? *value : 0);
    w3d_maybe_flush(x);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_FillStencilBuffer(REG(a0, W3D_Context *c), REG(d0, ULONG px), REG(d1, ULONG py), REG(d2, ULONG w),
                                        REG(d3, ULONG h), REG(d4, ULONG depth), REG(a1, void *data), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG i, j;
    if (!x->sbuf) return W3D_NOSTENCILBUFFER;
    if (px + w > (ULONG)c->width || py + h > (ULONG)c->height || !data) return W3D_ILLEGALINPUT;
    w3d_flush(x);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            ULONG k = j * w + i, v;
            if (depth <= 8) v = ((UBYTE *)data)[k];
            else if (depth <= 16) v = ((UWORD *)data)[k];
            else v = ((ULONG *)data)[k];
            x->sbuf[(py + j) * (ULONG)c->width + px + i] = (UBYTE)v;
        }
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_ReadStencilPixel(REG(a0, W3D_Context *c), REG(d0, ULONG px), REG(d1, ULONG py), REG(a1, ULONG *st),
                                       REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!x->sbuf) return W3D_NOSTENCILBUFFER;
    if (px >= (ULONG)c->width || py >= (ULONG)c->height) return W3D_ILLEGALINPUT;
    w3d_flush(x);
    *st = x->sbuf[py * (ULONG)c->width + px];
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_ReadStencilSpan(REG(a0, W3D_Context *c), REG(d0, ULONG px), REG(d1, ULONG py), REG(d2, ULONG n),
                                      REG(a1, ULONG *st), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG i;
    if (!x->sbuf) return W3D_NOSTENCILBUFFER;
    if (py >= (ULONG)c->height || px + n > (ULONG)c->width) return W3D_ILLEGALINPUT;
    w3d_flush(x);
    for (i = 0; i < n; i++) st[i] = x->sbuf[py * (ULONG)c->width + px + i];
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_WriteStencilPixel(REG(a0, W3D_Context *c), REG(d0, ULONG px), REG(d1, ULONG py), REG(d2, ULONG st),
                                        REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (!x->sbuf) return W3D_NOSTENCILBUFFER;
    if (px >= (ULONG)c->width || py >= (ULONG)c->height) return W3D_ILLEGALINPUT;
    w3d_flush(x);
    x->sbuf[py * (ULONG)c->width + px] = (UBYTE)st;
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_WriteStencilSpan(REG(a0, W3D_Context *c), REG(d0, ULONG px), REG(d1, ULONG py), REG(d2, ULONG n),
                                       REG(a1, ULONG *st), REG(a2, UBYTE *mask), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG i;
    if (!x->sbuf) return W3D_NOSTENCILBUFFER;
    if (py >= (ULONG)c->height || px + n > (ULONG)c->width) return W3D_ILLEGALINPUT;
    w3d_flush(x);
    for (i = 0; i < n; i++) if (!mask || mask[i]) x->sbuf[py * (ULONG)c->width + px + i] = (UBYTE)st[i];
    return W3D_SUCCESS;
}

/* V5: colour, depth and stencil in one call; NULL leaves one alone. */
LIBCALL ULONG LIB_W3D_ClearBuffers(REG(a0, W3D_Context *c), REG(a1, W3D_Color *colour), REG(a2, W3D_Double *depth),
                                   REG(a3, ULONG *stencil), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    if (colour) w3d_fill(x, w3d_colour(colour));
    if (depth && x->zbuf) w3d_clear_buffer(x, OGPU_KIND_DEPTH, z_to_u32(*depth));
    if (stencil && x->sbuf) w3d_clear_buffer(x, OGPU_KIND_STENCIL, *stencil);
    w3d_maybe_flush(x);
    return W3D_SUCCESS;
}

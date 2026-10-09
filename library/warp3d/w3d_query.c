/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library: what the drivers do (W3D_Query and the texture format
 * questions), the driver list, and screen modes (the RTG modes of 15 bits
 * and more, through CyberGraphX and the display database). Both drivers
 * draw the same things, so they answer alike. */
#include <exec/memory.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <libraries/asl.h>
#include <utility/hooks.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/asl.h>
#include <proto/cybergraphics.h>

#include "w3d_internal.h"
#include "w3d_calls.h"

ULONG w3d_query(int route, ULONG query, ULONG destfmt)
{
    (void)route;
    if (destfmt && !(destfmt & W3D_GetDestFmt_all())) return W3D_NOT_SUPPORTED;
    switch (query) {
    case W3D_Q_MAXTEXWIDTH: case W3D_Q_MAXTEXHEIGHT: case W3D_Q_MAXTEXWIDTH_P: case W3D_Q_MAXTEXHEIGHT_P:
        return 2048;
    case W3D_Q_NUM_TMU: case W3D_Q_NUM_BLEND:
        return 1;
    case W3D_Q_TEXMAPPING3D: case W3D_Q_ANTIALIASING: case W3D_Q_ANTI_POINT: case W3D_Q_ANTI_LINE:
    case W3D_Q_ANTI_POLYGON: case W3D_Q_ANTI_FULLSCREEN: case W3D_Q_DITHERING: case W3D_Q_ENV_COMBINE:
    case W3D_Q_ENV_CROSSBAR:
        return W3D_NOT_SUPPORTED;
    case W3D_Q_MIPMAPPING: case W3D_Q_MMFILTER:
        return W3D_PARTIALLY_SUPPORTED;     /* the level is chosen once a triangle on the CPU */
    }
    if ((query >= W3D_Q_DRAW_POINT && query <= W3D_Q_DRAW_LINE_FX) || (query >= W3D_Q_TEXMAPPING && query <= W3D_Q_CHROMATEST)
        || query == W3D_Q_FLATSHADING || query == W3D_Q_GOURAUDSHADING
        || (query >= W3D_Q_ZBUFFER && query <= W3D_Q_ZCOMPAREMODES) || query == W3D_Q_ALPHATEST || query == W3D_Q_ALPHATESTMODES
        || (query >= W3D_Q_BLENDING && query <= W3D_Q_ONE_ONE) || (query >= W3D_Q_FOGGING && query <= W3D_Q_INTERPOLATED)
        || query == W3D_Q_PALETTECONV || query == W3D_Q_SCISSOR || query == W3D_Q_RECTTEXTURES || query == W3D_Q_LOGICOP
        || query == W3D_Q_MASKING || (query >= W3D_Q_STENCILBUFFER && query <= W3D_Q_STENCIL_WRMASK)
        || query == W3D_Q_DRAW_POINT_TEX || query == W3D_Q_DRAW_LINE_TEX || query == W3D_Q_CULLFACE
        || query == W3D_Q_ENV_ADD || query == W3D_Q_ENV_SUB || query == W3D_Q_STIPPLE_LINE || query == W3D_Q_STIPPLE_POLYGON)
        return W3D_FULLY_SUPPORTED;
    return W3D_NOT_SUPPORTED;
}

ULONG w3d_texfmt_info(int route, ULONG format, ULONG destfmt)
{
    (void)route;
    if (destfmt && !(destfmt & W3D_GetDestFmt_all())) return W3D_TEXFMT_UNSUPPORTED;
    if (format >= W3D_CHUNKY && format <= W3D_R8G8B8A8)
        return W3D_TEXFMT_SUPPORTED | (format == W3D_A8R8G8B8 ? W3D_TEXFMT_FAST | W3D_TEXFMT_ARGBFAST : 0);
    return W3D_TEXFMT_UNSUPPORTED;
}

static int route_of(struct W3DBase *base, W3D_Driver *d)
{
    return d == &base->drv[ROUTE_GPU] ? ROUTE_GPU : ROUTE_CPU;
}

LIBCALL ULONG LIB_W3D_Query(REG(a0, W3D_Context *c), REG(d0, ULONG query), REG(d1, ULONG destfmt), REG(a6, struct W3DBase *libbase))
{
    return w3d_query(c ? CTX(c)->route : ROUTE_CPU, query, destfmt);
}

LIBCALL ULONG LIB_W3D_QueryDriver(REG(a0, W3D_Driver *d), REG(d0, ULONG query), REG(d1, ULONG destfmt), REG(a6, struct W3DBase *libbase))
{
    return w3d_query(route_of(libbase, d), query, destfmt);
}

LIBCALL ULONG LIB_W3D_GetTexFmtInfo(REG(a0, W3D_Context *c), REG(d0, ULONG format), REG(d1, ULONG destfmt), REG(a6, struct W3DBase *libbase))
{
    return w3d_texfmt_info(c ? CTX(c)->route : ROUTE_CPU, format, destfmt);
}

LIBCALL ULONG LIB_W3D_GetDriverTexFmtInfo(REG(a0, W3D_Driver *d), REG(d0, ULONG format), REG(d1, ULONG destfmt),
                                          REG(a6, struct W3DBase *libbase))
{
    return w3d_texfmt_info(route_of(libbase, d), format, destfmt);
}

LIBCALL ULONG LIB_W3D_GetDestFmt(REG(a6, struct W3DBase *libbase)) { return W3D_GetDestFmt_all(); }

LIBCALL W3D_Driver **LIB_W3D_GetDrivers(REG(a6, struct W3DBase *libbase)) { return libbase->drivers; }

/* ---- screen modes ------------------------------------------------------------------------- */

/* An RTG mode Warp3D can draw on: 15 bits or more. */
static int usable(ULONG id)
{
    if (!CyberGfxBase || id == (ULONG)INVALID_ID || !IsCyberModeID(id)) return 0;
    return GetCyberIDAttr(CYBRIDATTR_DEPTH, id) >= 15;
}

LIBCALL W3D_Driver *LIB_W3D_TestMode(REG(d0, ULONG id), REG(a6, struct W3DBase *libbase))
{
    return usable(id) ? libbase->drivers[0] : 0;
}

LIBCALL W3D_ScreenMode *LIB_W3D_GetScreenmodeList(REG(a6, struct W3DBase *libbase))
{
    W3D_ScreenMode *head = 0, **tail = &head;
    ULONG id = (ULONG)INVALID_ID;
    while ((id = NextDisplayInfo(id)) != (ULONG)INVALID_ID) {
        struct NameInfo ni;
        W3D_ScreenMode *m;
        int i;
        if (!usable(id)) continue;
        m = w3d_alloc(sizeof *m);
        if (!m) break;
        m->ModeID = id;
        m->Width = GetCyberIDAttr(CYBRIDATTR_WIDTH, id);
        m->Height = GetCyberIDAttr(CYBRIDATTR_HEIGHT, id);
        m->Depth = GetCyberIDAttr(CYBRIDATTR_DEPTH, id);
        m->Driver = libbase->drivers[0];
        if (GetDisplayInfoData(0, (UBYTE *)&ni, sizeof ni, DTAG_NAME, id))
            for (i = 0; i < DISPLAYNAMELEN - 1 && ni.Name[i]; i++) m->DisplayName[i] = ni.Name[i];
        *tail = m;
        tail = (W3D_ScreenMode **)&m->Next;
    }
    return head;
}

LIBCALL void LIB_W3D_FreeScreenmodeList(REG(a0, W3D_ScreenMode *list), REG(a6, struct W3DBase *libbase))
{
    while (list) {
        W3D_ScreenMode *next = list->Next;
        w3d_free(list);
        list = next;
    }
}

LIBCALL ULONG LIB_W3D_BestModeID(REG(a0, struct TagItem *tags), REG(a6, struct W3DBase *libbase))
{
    ULONG w = GetTagData(W3D_BMI_WIDTH, 640, tags), h = GetTagData(W3D_BMI_HEIGHT, 480, tags);
    ULONG d = GetTagData(W3D_BMI_DEPTH, 16, tags), id;
    if (!CyberGfxBase) return (ULONG)INVALID_ID;
    if (d <= 16) d = 16;                        /* 15 too: R5G6B5 is drawn in place, 15-bit modes through a copy */
    id = BestCModeIDTags(CYBRBIDTG_NominalWidth, w, CYBRBIDTG_NominalHeight, h, CYBRBIDTG_Depth, d, TAG_DONE);
    return usable(id) ? id : (ULONG)INVALID_ID;
}

static ULONG filter(REG(a0, struct Hook *hook), REG(a2, APTR req), REG(a1, ULONG id))
{
    (void)hook; (void)req;
    return (ULONG)usable(id);
}

LIBCALL ULONG LIB_W3D_RequestMode(REG(a0, struct TagItem *tags), REG(a6, struct W3DBase *libbase))
{
    struct Hook hook;
    struct ScreenModeRequester *r;
    ULONG id = (ULONG)INVALID_ID;
    if (!AslBase) AslBase = OpenLibrary("asl.library", 39);
    if (!AslBase) return (ULONG)INVALID_ID;
    /* h_Entry is the NDK's generic ULONG (*)(): ASL calls it with the hook in
     * A0, the requester in A2 and the mode in A1, as filter() takes them. The
     * cast goes through void (*)(void), C's "any function" type, because the
     * types differ by design (GCC 16, C23: -Wcast-function-type). */
    hook.h_Entry = (ULONG (*)())(void (*)(void))filter;
    hook.h_SubEntry = 0;
    hook.h_Data = 0;
    r = AllocAslRequestTags(ASL_ScreenModeRequest, ASLSM_FilterFunc, (ULONG)&hook, ASLSM_TitleText, (ULONG)"Warp3D screen mode",
                            TAG_DONE);
    if (!r) return (ULONG)INVALID_ID;
    if (AslRequestTags(r, TAG_DONE)) id = r->sm_DisplayID;
    FreeAslRequest(r);
    (void)tags;
    return id;
}

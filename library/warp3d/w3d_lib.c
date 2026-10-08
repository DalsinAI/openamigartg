/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library 5.0: the library itself, its 92-entry function table (the
 * order of version 5's 68k table, a superset of version 4's), the libraries
 * it opens, the drivers it offers and small helpers. Built bare by
 * library/warp3d/build.sh: no startup code, no C library.
 *
 * The Team wrote this library from the published Warp3D documentation.
 * Wazp3D and AROS's Warp3D were references for behaviour only. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "w3d_internal.h"
#include "w3d_calls.h"
#include "../../include/opengpu/opengpu.h"
#include "../../include/proto/opengpu.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase;
struct Library *CyberGfxBase;
struct Library *OpenGPUBase;
struct IntuitionBase *IntuitionBase;
struct Library *AslBase;
struct W3DBase *W3DBase;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "Warp3D.library";
static const char lib_id[] = "Warp3D.library 5.0 (8.10.2026) OpenRTG Warp3D, Dalsin Limited\r\n";
static const char lib_ver[] __attribute__((used)) = "\0$VER: Warp3D.library 5.0 (8.10.2026) OpenRTG Warp3D, Dalsin Limited";
static char name_gpu[64] = "OpenGPU";
static const char name_cpu[] = "CPU (OpenRTG's 3D core on this 68k)";

static struct Library *lib_init(REG(d0, struct W3DBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct W3DBase *base));
static BPTR lib_close(REG(a6, struct W3DBase *base));
static BPTR lib_expunge(REG(a6, struct W3DBase *base));
static ULONG lib_null(void) { return 0; }

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)LIB_W3D_CreateContext, (APTR)LIB_W3D_DestroyContext, (APTR)LIB_W3D_GetState, (APTR)LIB_W3D_SetState,
    (APTR)LIB_W3D_CheckDriver, (APTR)LIB_W3D_LockHardware, (APTR)LIB_W3D_UnLockHardware, (APTR)LIB_W3D_WaitIdle,
    (APTR)LIB_W3D_CheckIdle, (APTR)LIB_W3D_Query, (APTR)LIB_W3D_GetTexFmtInfo,
    (APTR)LIB_W3D_AllocTexObj, (APTR)LIB_W3D_FreeTexObj, (APTR)LIB_W3D_ReleaseTexture, (APTR)LIB_W3D_FlushTextures,
    (APTR)LIB_W3D_SetFilter, (APTR)LIB_W3D_SetTexEnv, (APTR)LIB_W3D_SetWrapMode, (APTR)LIB_W3D_UpdateTexImage,
    (APTR)LIB_W3D_UploadTexture,
    (APTR)LIB_W3D_DrawLine, (APTR)LIB_W3D_DrawPoint, (APTR)LIB_W3D_DrawTriangle, (APTR)LIB_W3D_DrawTriFan,
    (APTR)LIB_W3D_DrawTriStrip,
    (APTR)LIB_W3D_SetAlphaMode, (APTR)LIB_W3D_SetBlendMode, (APTR)LIB_W3D_SetDrawRegion, (APTR)LIB_W3D_SetFogParams,
    (APTR)LIB_W3D_SetColorMask, (APTR)LIB_W3D_SetStencilFunc,
    (APTR)LIB_W3D_AllocZBuffer, (APTR)LIB_W3D_FreeZBuffer, (APTR)LIB_W3D_ClearZBuffer, (APTR)LIB_W3D_ReadZPixel,
    (APTR)LIB_W3D_ReadZSpan, (APTR)LIB_W3D_SetZCompareMode,
    (APTR)LIB_W3D_AllocStencilBuffer, (APTR)LIB_W3D_ClearStencilBuffer, (APTR)LIB_W3D_FillStencilBuffer,
    (APTR)LIB_W3D_FreeStencilBuffer, (APTR)LIB_W3D_ReadStencilPixel, (APTR)LIB_W3D_ReadStencilSpan,
    (APTR)LIB_W3D_SetLogicOp, (APTR)LIB_W3D_Hint, (APTR)LIB_W3D_SetDrawRegionWBM, (APTR)LIB_W3D_GetDriverState,
    (APTR)LIB_W3D_Flush, (APTR)LIB_W3D_SetPenMask, (APTR)LIB_W3D_SetStencilOp, (APTR)LIB_W3D_SetWriteMask,
    (APTR)LIB_W3D_WriteStencilPixel, (APTR)LIB_W3D_WriteStencilSpan, (APTR)LIB_W3D_WriteZPixel, (APTR)LIB_W3D_WriteZSpan,
    (APTR)LIB_W3D_SetCurrentColor, (APTR)LIB_W3D_SetCurrentPen, (APTR)LIB_W3D_UpdateTexSubImage,
    (APTR)LIB_W3D_FreeAllTexObj, (APTR)LIB_W3D_GetDestFmt,
    (APTR)LIB_W3D_DrawLineStrip, (APTR)LIB_W3D_DrawLineLoop, (APTR)LIB_W3D_GetDrivers, (APTR)LIB_W3D_QueryDriver,
    (APTR)LIB_W3D_GetDriverTexFmtInfo, (APTR)LIB_W3D_RequestMode, (APTR)LIB_W3D_SetScissor, (APTR)LIB_W3D_FlushFrame,
    (APTR)LIB_W3D_TestMode, (APTR)LIB_W3D_SetChromaTestBounds, (APTR)LIB_W3D_ClearDrawRegion,
    (APTR)LIB_W3D_DrawTriangleV, (APTR)LIB_W3D_DrawTriFanV, (APTR)LIB_W3D_DrawTriStripV,
    (APTR)LIB_W3D_GetScreenmodeList, (APTR)LIB_W3D_FreeScreenmodeList, (APTR)LIB_W3D_BestModeID,
    (APTR)LIB_W3D_VertexPointer, (APTR)LIB_W3D_TexCoordPointer, (APTR)LIB_W3D_ColorPointer, (APTR)LIB_W3D_BindTexture,
    (APTR)LIB_W3D_DrawArray, (APTR)LIB_W3D_DrawElements, (APTR)LIB_W3D_SetFrontFace,
    (APTR)LIB_W3D_SetTextureBlend, (APTR)LIB_W3D_SecondaryColorPointer, (APTR)LIB_W3D_FogCoordPointer,
    (APTR)LIB_W3D_InterleavedArray, (APTR)LIB_W3D_ClearBuffers, (APTR)LIB_W3D_SetParameter, (APTR)LIB_W3D_PinTexture,
    (APTR)LIB_W3D_SetDrawRegionTexture,
    (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct W3DBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, W3D_LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

/* ---- the library -------------------------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct W3DBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    W3DBase = base;
    base->seglist = seglist;
    base->lib.lib_Revision = W3D_LIB_REVISION;
    InitSemaphore(&base->lock);
    base->drv[ROUTE_GPU].ChipID = W3D_CHIP_UNKNOWN;
    base->drv[ROUTE_GPU].name = name_gpu;
    base->drv[ROUTE_GPU].swdriver = W3D_FALSE;
    base->drv[ROUTE_CPU].ChipID = W3D_CHIP_UNKNOWN;
    base->drv[ROUTE_CPU].name = (char *)name_cpu;
    base->drv[ROUTE_CPU].swdriver = W3D_TRUE;
    base->drv[ROUTE_GPU].formats = base->drv[ROUTE_CPU].formats = W3D_GetDestFmt_all();
    return &base->lib;
}

/* The libraries Warp3D uses: opened on the first open (exec's own call, so
 * disk libraries load in the opener's task). */
static int open_libs(struct W3DBase *base)
{
    if (!DOSBase) DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 39);
    if (!GfxBase) GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!UtilityBase) UtilityBase = OpenLibrary("utility.library", 39);
    if (!IntuitionBase) IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (!CyberGfxBase) CyberGfxBase = OpenLibrary("cybergraphics.library", 40);
    if (!DOSBase || !GfxBase || !UtilityBase || !IntuitionBase) return 0;
    if (!OpenGPUBase) {
        OpenGPUBase = OpenLibrary("opengpu.library", 0);
        base->gpu_3d = 0;
        if (OpenGPUBase) {
            ULONG a = OGPU_Query(OGPU_OP_TRIANGLES, OGPU_FMT_ARGB32);
            if (OGPU_ANSWER(a) != OGPU_NONE) {
                STRPTR n = OGPU_BackEndName((ULONG)OGPU_BACKEND(a));
                const char *p = "OpenGPU: ";
                int i = 0;
                base->gpu_3d = 1;
                base->gpu_host = OGPU_BACKEND(a) != OGPU_BACKEND_CPU;
                while (*p && i < 60) name_gpu[i++] = *p++;
                while (n && *n && i < 62) name_gpu[i++] = *n++;
                name_gpu[i] = 0;
            }
        }
    }
    w3d_read_prefs(base);
    base->drivers[0] = base->gpu_3d ? &base->drv[ROUTE_GPU] : &base->drv[ROUTE_CPU];
    base->drivers[1] = base->gpu_3d ? &base->drv[ROUTE_CPU] : 0;
    base->drivers[2] = 0;
    return 1;
}

static struct Library *lib_open(REG(a6, struct W3DBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    if (!open_libs(base)) { base->lib.lib_OpenCnt--; return 0; }
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct W3DBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct W3DBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) { base->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    if (OpenGPUBase) CloseLibrary(OpenGPUBase);
    if (AslBase) CloseLibrary(AslBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (UtilityBase) CloseLibrary(UtilityBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);
    OpenGPUBase = AslBase = CyberGfxBase = UtilityBase = 0;
    IntuitionBase = 0; GfxBase = 0; DOSBase = 0;
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

/* The preferences, in ENV:Warp3D/ (OpenPrefs' Acceleration page writes them;
 * C:Warp3DPrefs FROMWAZP3D carries Wazp3D's settings over):
 *   Driver      Auto, CPU or OpenGPU   which driver W3D_DRIVER_BEST gets
 *   ZBuffer     16 or 32               bits of depth
 *   Fog, Perspective, Filtering, Lighting   "0" leaves that out, for speed
 * Read again for each new context, so a change applies to the next program. */
static int envflag(const char *name, int dflt)
{
    char v[16];
    if (GetVar((STRPTR)name, v, sizeof v, GVF_GLOBAL_ONLY) <= 0) return dflt;
    return v[0] == '1' || v[0] == 'Y' || v[0] == 'y' || ((v[0] == 'O' || v[0] == 'o') && (v[1] == 'N' || v[1] == 'n'));
}

void w3d_read_prefs(struct W3DBase *base)
{
    char v[16];
    base->prefer_cpu = base->prefer_gpu = 0;
    if (GetVar("Warp3D/Driver", v, sizeof v, GVF_GLOBAL_ONLY) > 0) {
        base->prefer_cpu = v[0] == 'C' || v[0] == 'c';
        base->prefer_gpu = v[0] == 'O' || v[0] == 'o' || v[0] == 'G' || v[0] == 'g';
    }
    base->z32 = GetVar("Warp3D/ZBuffer", v, sizeof v, GVF_GLOBAL_ONLY) > 0 && v[0] == '3';
    base->no_fog = !envflag("Warp3D/Fog", 1);
    base->no_persp = !envflag("Warp3D/Perspective", 1);
    base->no_filter = !envflag("Warp3D/Filtering", 1);
    base->no_light = !envflag("Warp3D/Lighting", 1);
}

/* ---- helpers ------------------------------------------------------------------------------ */

void *w3d_alloc(ULONG bytes) { return AllocVec(bytes ? bytes : 4, MEMF_ANY | MEMF_CLEAR); }
void w3d_free(void *p) { if (p) FreeVec(p); }

void w3d_copy(void *d, const void *s, ULONG n)
{
    UBYTE *dd = d;
    const UBYTE *ss = s;
    if (!(((ULONG)dd | (ULONG)ss | n) & 3)) {
        ULONG *dl = d;
        const ULONG *sl = s;
        n >>= 2;
        while (n--) *dl++ = *sl++;
        return;
    }
    while (n--) *dd++ = *ss++;
}

void w3d_clear(void *d, ULONG n)
{
    UBYTE *dd = d;
    while (n--) *dd++ = 0;
}

/* Rounded to the nearest (the FPU's own mode); no FPCR changes, which cost
 * time on emulated FPUs. */
LONG w3d_f2l(float f)
{
    LONG r;
    __asm__("fmove.l %1,%0" : "=d"(r) : "f"(f));
    return r;
}

ULONG w3d_fbits(float f)
{
    union { float f; ULONG u; } x;
    x.f = f;
    return x.u;
}

static ULONG ch(float v)
{
    LONG c = w3d_f2l(v * 255.0f);
    return c < 0 ? 0 : c > 255 ? 255 : (ULONG)c;
}

ULONG w3d_colour(const W3D_Color *c)
{
    return (ch(c->a) << 24) | (ch(c->r) << 16) | (ch(c->g) << 8) | ch(c->b);
}

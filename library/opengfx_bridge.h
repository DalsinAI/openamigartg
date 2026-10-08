/* SPDX-License-Identifier: MIT */
/*
 * Private copy of opengfx.library's provider ABI v1.
 *
 * Source of truth: DalsinAI/amigachrome-guest
 *   libraries/opengfx/include/libraries/opengfx.h
 *   libraries/opengfx/include/inline/opengfx.h
 *
 * Kept private so OpenRTG still builds standalone when the OpenGfx SDK is
 * not installed. ABI v1 is frozen; change this only with the OpenGfx ABI.
 */
#ifndef ORTG_OPENGFX_BRIDGE_H
#define ORTG_OPENGFX_BRIDGE_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <inline/macros.h>

#define ORTG_OPENGFXLIB_NAME "opengfx.library"
#define ORTG_OPENGFXLIB_VERSION 1
#define ORTG_OGFX_PROVIDER_ABI_V1 1

struct ortg_ogfx_rectfill {
    struct RastPort *rp;
    LONG x0, y0, x1, y1;
};

struct ortg_ogfx_text {
    struct RastPort *rp;
    STRPTR text;
    ULONG length;
    LONG result;
};

struct ortg_ogfx_bltbitmap {
    struct BitMap *src;
    LONG sx, sy;
    struct BitMap *dst;
    LONG dx, dy;
    LONG width, height;
    ULONG minterm;
    ULONG mask;
    PLANEPTR temp;
    LONG result;
};

struct ortg_ogfx_scroll {
    struct RastPort *rp;
    LONG dx, dy;
    LONG x0, y0, x1, y1;
};

struct ortg_ogfx_provider_v1 {
    ULONG size;
    ULONG abi;
    APTR owner;
    APTR userdata;
    LONG (*rectfill)(APTR, struct ortg_ogfx_rectfill *);
    LONG (*text)(APTR, struct ortg_ogfx_text *);
    LONG (*bltbitmap)(APTR, struct ortg_ogfx_bltbitmap *);
    LONG (*scrollraster)(APTR, struct ortg_ogfx_scroll *);
};

#define ORTG_OGFX_InstallPatches(base)     LP0(0x24, LONG, OGFX_InstallPatches, , base)
#define ORTG_OGFX_RegisterProvider(base,provider)     LP1(0x36, LONG, OGFX_RegisterProvider, struct ortg_ogfx_provider_v1 *, provider, a0, , base)
#define ORTG_OGFX_UnregisterProvider(base,owner)     LP1(0x3c, LONG, OGFX_UnregisterProvider, APTR, owner, a0, , base)

#endif

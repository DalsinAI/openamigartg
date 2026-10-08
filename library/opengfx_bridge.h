/* SPDX-License-Identifier: MIT */
/*
 * Private copy of OpenGfx's provider ABI v1.
 *
 * Source of truth: include/opengpu/gfx.h. OpenGfx lives in opengpu.library
 * from 0.6 (its calls are LVOs from 66); before that it was opengfx.library
 * (amigachrome-guest libraries/opengfx), which is now a stub forwarding to
 * opengpu.library. OpenRTG registers with opengpu.library when it carries
 * OpenGfx, else with opengfx.library.
 *
 * Kept private so OpenRTG still builds standalone. ABI v1 is frozen; change
 * this only with the OpenGfx ABI. The 1.4 record extends it (fourteen more
 * calls after v1's eight).
 */
#ifndef ORTG_OPENGFX_BRIDGE_H
#define ORTG_OPENGFX_BRIDGE_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
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

struct ortg_ogfx_textlength {
    struct RastPort *rp;
    STRPTR text;
    ULONG length;
    LONG result;
};

struct ortg_ogfx_textextent {
    struct RastPort *rp;
    STRPTR text;
    ULONG length;
    struct TextExtent *extent;
};

struct ortg_ogfx_textfit {
    struct RastPort *rp;
    STRPTR text;
    ULONG length;
    struct TextExtent *extent;
    struct TextExtent *constraining;
    LONG direction;
    ULONG bit_width, bit_height;
    ULONG result;
};

struct ortg_ogfx_blttemplate {
    PLANEPTR source;
    LONG sx, source_modulo;
    struct RastPort *rp;
    LONG dx, dy, width, height;
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
    LONG (*textlength)(APTR, struct ortg_ogfx_textlength *);
    LONG (*textextent)(APTR, struct ortg_ogfx_textextent *);
    LONG (*textfit)(APTR, struct ortg_ogfx_textfit *);
    LONG (*blttemplate)(APTR, struct ortg_ogfx_blttemplate *);
};

/* OpenGfx 1.4 (opengpu.library 0.8): the fourteen other drawing calls, in
 * a longer record that starts with the v1 one (gfx.h, struct OGFXProviderAll). */
struct ortg_ogfx_bltpattern { struct RastPort *rp; PLANEPTR mask; LONG x0, y0, x1, y1; LONG mask_bpr; };
struct ortg_ogfx_setrast { struct RastPort *rp; ULONG pen; };
struct ortg_ogfx_draw { struct RastPort *rp; LONG x, y; };
struct ortg_ogfx_polydraw { struct RastPort *rp; LONG count; WORD *array; };
struct ortg_ogfx_pixel { struct RastPort *rp; LONG x, y; LONG result; };
struct ortg_ogfx_bltbmrp { struct BitMap *src; LONG sx, sy; struct RastPort *rp; LONG dx, dy, width, height; ULONG minterm; PLANEPTR mask; };
struct ortg_ogfx_clipblit { struct RastPort *src_rp; LONG sx, sy; struct RastPort *rp; LONG dx, dy, width, height; ULONG minterm; };
struct ortg_ogfx_array {
    struct RastPort *rp;
    LONG x0, y0, x1, y1;
    ULONG width;
    UBYTE *array;
    LONG bytes_per_row;
    struct RastPort *temp_rp;
    LONG result;
};

struct ortg_ogfx_provider_all {
    struct ortg_ogfx_provider_v1 v1;
    LONG (*bltpattern)(APTR, struct ortg_ogfx_bltpattern *);
    LONG (*setrast)(APTR, struct ortg_ogfx_setrast *);
    LONG (*draw)(APTR, struct ortg_ogfx_draw *);
    LONG (*polydraw)(APTR, struct ortg_ogfx_polydraw *);
    LONG (*writepixel)(APTR, struct ortg_ogfx_pixel *);
    LONG (*readpixel)(APTR, struct ortg_ogfx_pixel *);
    LONG (*bltbitmaprastport)(APTR, struct ortg_ogfx_bltbmrp *);
    LONG (*bltmaskbitmaprastport)(APTR, struct ortg_ogfx_bltbmrp *);
    LONG (*clipblit)(APTR, struct ortg_ogfx_clipblit *);
    LONG (*writechunkypixels)(APTR, struct ortg_ogfx_array *);
    LONG (*writepixelarray8)(APTR, struct ortg_ogfx_array *);
    LONG (*writepixelline8)(APTR, struct ortg_ogfx_array *);
    LONG (*readpixelline8)(APTR, struct ortg_ogfx_array *);
    LONG (*readpixelarray8)(APTR, struct ortg_ogfx_array *);
};
#define ORTG_OGFX_ALL_INTERFACE 0x00010004UL     /* OGFX_Version() 1.4: OpenGfx patches all 22 */

#define ORTG_OGFX_InstallPatches(base)     LP0(0x24, LONG, OGFX_InstallPatches, , base)
#define ORTG_OGFX_RegisterProvider(base,provider)     LP1(0x36, LONG, OGFX_RegisterProvider, struct ortg_ogfx_provider_v1 *, provider, a0, , base)
#define ORTG_OGFX_UnregisterProvider(base,owner)     LP1(0x3c, LONG, OGFX_UnregisterProvider, APTR, owner, a0, , base)

/* The same three calls in opengpu.library 0.6 and later (LVOs 72, 90, 96). */
#define ORTG_OPENGPU_OGFX_REVISION 6
#define ORTG_OGPU_OGFX_Version(base)     LP0(0x42, ULONG, OGFX_Version, , base)
#define ORTG_OGPU_OGFX_InstallPatches(base)     LP0(0x48, LONG, OGFX_InstallPatches, , base)
#define ORTG_OGPU_OGFX_RegisterProvider(base,provider)     LP1(0x5a, LONG, OGFX_RegisterProvider, struct ortg_ogfx_provider_v1 *, provider, a0, , base)
#define ORTG_OGPU_OGFX_UnregisterProvider(base,owner)     LP1(0x60, LONG, OGFX_UnregisterProvider, APTR, owner, a0, , base)

#endif

/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenGfx inside opengpu.library (0.6 and later): the public calls.
 *
 * opengfx.library 1.1 (amigachrome-guest libraries/opengfx, commit 90aa66f)
 * was brought into opengpu.library on 8 October 2026, so one library does
 * all the drawing. Its six calls are opengpu.library's LVOs from 66, in
 * opengfx.library's order, and the eight graphics.library calls OpenGfx owns
 * follow them as LVOs of their own:
 *
 *    66 OGFX_Version          102 OGFX_Text         126 OGFX_RectFill
 *    72 OGFX_InstallPatches   108 OGFX_TextLength   132 OGFX_BltBitMap
 *    78 OGFX_SetEnabled       114 OGFX_TextExtent   138 OGFX_BltTemplate
 *    84 OGFX_Status           120 OGFX_TextFit      144 OGFX_ScrollRaster
 *    90 OGFX_RegisterProvider
 *    96 OGFX_UnregisterProvider
 *
 * 0.7 adds the look hook, for a program that draws the system's look on
 * these calls (OpenLook's window frames) without patching them itself:
 *
 *   150 OGFX_RegisterLook        156 OGFX_UnregisterLook
 *
 * The drawing LVOs take graphics.library's arguments in graphics.library's
 * registers and run the same code as the patches: for RectFill and Text a
 * registered look first (0.7), then a registered provider
 * (OpenRTG), then OpenGfx's native planar paths, then graphics.library's
 * own code. OGFX_InstallPatches() points graphics.library's eight vectors at
 * that code, so every program reaches it. Opening the library changes no
 * vector by itself.
 *
 * The provider record and the status bits are opengfx.library 1.1's, unchanged
 * (ABI v1). A program built against <libraries/opengfx.h> keeps working
 * through the opengfx.library stub, which forwards to these LVOs.
 *
 * OpenGPU's revision says whether OpenGfx is there: opengpu.library is
 * version 0, so open it with version 0 and check lib_Revision >= 6
 * (OPENGPU_OGFX_REVISION), or ask OGFX_Version(). The look hook needs
 * lib_Revision >= 7 (OPENGPU_OGFX_LOOK_REVISION), or OGFX_Version() 1.3. */
#ifndef OPENGPU_GFX_H
#define OPENGPU_GFX_H

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>

#define OPENGPU_OGFX_REVISION 6          /* opengpu.library 0.6: OpenGfx inside */
#define OPENGPU_OGFX_LOOK_REVISION 7     /* opengpu.library 0.7: the look hook */

/* OGFX_Version(): (version << 16) | revision of the OpenGfx interface.
 * 1.2 is opengfx.library 1.1's interface inside opengpu.library, with the
 * eight drawing LVOs added; 1.3 adds the look hook (opengpu.library 0.7). */
#define OGFX_INTERFACE_VERSION  1
#define OGFX_INTERFACE_REVISION 3

/* The rest is opengfx.library 1.1's public header (libraries/opengfx.h), the
 * same names and layouts, so either header may come first. */
#ifndef LIBRARIES_OPENGFX_H
#define LIBRARIES_OPENGFX_H

#define OPENGFXLIB_NAME "opengfx.library"
#define OPENGFXLIB_VERSION 1
#define OPENGFXLIB_REVISION 1

#define OGFX_PROVIDER_ABI_V1 1

#define OGFX_STATUS_PATCHED  (1UL << 0)
#define OGFX_STATUS_ENABLED  (1UL << 1)
#define OGFX_STATUS_PROVIDER (1UL << 2)
#define OGFX_STATUS_AMIGACHROME (1UL << 3)   /* Dalsin boards found; Chip RAM drawing goes to the leaves */
#define OGFX_PATCHES 8                       /* the graphics.library calls OpenGfx owns */

struct OGFXRectFillRequest {
    struct RastPort *rp;
    LONG x0, y0, x1, y1;
};

struct OGFXTextRequest {
    struct RastPort *rp;
    STRPTR text;
    ULONG length;
    LONG result;
};

struct OGFXBltBitMapRequest {
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

struct OGFXScrollRasterRequest {
    struct RastPort *rp;
    LONG dx, dy;
    LONG x0, y0, x1, y1;
};

struct OGFXTextLengthRequest {
    struct RastPort *rp;
    STRPTR text;
    ULONG length;
    LONG result;                       /* TextLength's WORD */
};

struct OGFXTextExtentRequest {
    struct RastPort *rp;
    STRPTR text;
    ULONG length;
    struct TextExtent *extent;
};

struct OGFXTextFitRequest {
    struct RastPort *rp;
    STRPTR text;
    ULONG length;
    struct TextExtent *extent;
    struct TextExtent *constraining;
    LONG direction;
    ULONG bit_width, bit_height;
    ULONG result;
};

struct OGFXBltTemplateRequest {
    PLANEPTR source;
    LONG sx, source_modulo;
    struct RastPort *rp;
    LONG dx, dy, width, height;
};

/* A provider returns non-zero only when it has reproduced graphics.library's
 * semantics exactly and OpenGfx must not call the original vector. The record
 * is copied, so the caller may keep it on the stack; owner must stay a stable
 * non-NULL token until it unregisters. size is sizeof the record the provider
 * was built with: one built with opengfx.library 1.0's header (the first four
 * calls) is taken, and the calls it has no room for go to graphics.library.
 * A NULL entry also means "not mine". */
struct OGFXProviderV1 {
    ULONG size;
    ULONG abi;
    APTR owner;
    APTR userdata;

    LONG (*rectfill)(APTR userdata, struct OGFXRectFillRequest *request);
    LONG (*text)(APTR userdata, struct OGFXTextRequest *request);
    LONG (*bltbitmap)(APTR userdata, struct OGFXBltBitMapRequest *request);
    LONG (*scrollraster)(APTR userdata, struct OGFXScrollRasterRequest *request);
    /* opengfx.library 1.1 */
    LONG (*textlength)(APTR userdata, struct OGFXTextLengthRequest *request);
    LONG (*textextent)(APTR userdata, struct OGFXTextExtentRequest *request);
    LONG (*textfit)(APTR userdata, struct OGFXTextFitRequest *request);
    LONG (*blttemplate)(APTR userdata, struct OGFXBltTemplateRequest *request);
};

/* sizeof a 1.0 provider record: the first four calls only. */
#define OGFX_PROVIDER_V1_0_SIZE ((ULONG)&((struct OGFXProviderV1 *)0)->textlength)

#endif /* LIBRARIES_OPENGFX_H */

/* ---- the look hook (opengpu.library 0.7, OpenGfx interface 1.3) -------------

   OpenGfx owns graphics.library's drawing and text patches, so a program
   that draws the system's look on RectFill and Text (OpenLook: window frames,
   title bars, the screen's bar, border scrollers) registers a look instead
   of patching them. There is one look at a time.

   - OpenGfx asks the look first, before a provider and before its own paths,
     on every RectFill and Text, whichever way it came (the patch or the LVO).
     The request holds the arguments as graphics.library reads them (16-bit
     coordinates and counts, sign-extended).
   - The look returns non-zero when it has drawn the call itself, and the call
     ends there (for Text it sets request->result); zero, and the call goes on
     as if there were no look. A NULL entry is "not mine".
   - The look may draw with graphics.library, RectFill and Text included: those
     calls come back through OpenGfx and to the look again, so it must decline
     its own (OpenLook keeps a flag while it draws).
   - It is called on the drawing task, any task, from C (the arguments on the
     stack, the result in D0; D0, D1, A0 and A1 are the look's to change). It
     must not wait for long: Intuition may hold its locks.
   - The record is copied, so it may be on the stack; owner must stay a stable
     non-NULL token until the look is taken out.
   - OGFX_RegisterLook answers 0 when another look is in, or while calls into a
     look taken out are still running; 1 when this look is in.
   - OGFX_UnregisterLook takes the look out (1) and calls stop reaching it at
     once. Calls already inside it run on: the owner waits until OGFX_Status()
     clears OGFX_STATUS_LOOK_BUSY before its code goes.
   - Registering a look changes no vector. A look reaches graphics.library's
     calls once OGFX_InstallPatches() has put OpenGfx's patches in (OpenRTG
     does; a look may ask for them itself). */

#define OGFX_LOOK_ABI_V1 1

#define OGFX_STATUS_LOOK      (1UL << 4)   /* a look is in */
#define OGFX_STATUS_LOOK_BUSY (1UL << 5)   /* calls are inside a look, in or just taken out */

struct OGFXLookV1 {
    ULONG size;                        /* sizeof(struct OGFXLookV1) */
    ULONG abi;                         /* OGFX_LOOK_ABI_V1 */
    APTR owner;
    APTR userdata;

    LONG (*rectfill)(APTR userdata, struct OGFXRectFillRequest *request);
    LONG (*text)(APTR userdata, struct OGFXTextRequest *request);
};

#endif

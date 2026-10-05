/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * cybergraphics.library 43.1: the CyberGraphX API over openrtg.library
 * (DESIGN.md sections 1 and 10, phase 4), so programs written for
 * CyberGraphX draw on OpenRTG's screens, and picture.datatype draws
 * Workbench's backdrop through it rather than poking planes. It lives in
 * LIBS:OpenRTG/ and C:OpenRTG opens it there, which puts it in the library
 * list under its own name while OpenRTG is the RTG system.
 *
 * The calls take CyberGraphX's registers (cybergraphics_lib.fd), the pixel
 * work is openrtg.library's (ORTG_WritePixels and the rest), and the
 * numbers (attributes, tags, formats) are the CyberGraphX interface's.
 * Built bare by library/build.sh.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include "../include/openrtg/openrtg.h"

#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */
#define NO_ID ((ULONG)INVALID_ID)
#define LIB_VERSION 43
#define LIB_REVISION 1

/* ---- the CyberGraphX interface's numbers ---- */
#define CYBRMATTR_XMOD          0x80000001UL
#define CYBRMATTR_BPPIX         0x80000002UL
#define CYBRMATTR_DISPADR       0x80000003UL
#define CYBRMATTR_PIXFMT        0x80000004UL
#define CYBRMATTR_WIDTH         0x80000005UL
#define CYBRMATTR_HEIGHT        0x80000006UL
#define CYBRMATTR_DEPTH         0x80000007UL
#define CYBRMATTR_ISCYBERGFX    0x80000008UL
#define CYBRMATTR_ISLINEARMEM   0x80000009UL
#define CYBRIDATTR_PIXFMT       0x80000001UL
#define CYBRIDATTR_WIDTH        0x80000002UL
#define CYBRIDATTR_HEIGHT       0x80000003UL
#define CYBRIDATTR_DEPTH        0x80000004UL
#define CYBRIDATTR_BPPIX        0x80000005UL
#define CYBRMREQ_TB             (TAG_USER + 0x40000)
#define CYBRMREQ_MinDepth       (CYBRMREQ_TB + 0)
#define CYBRMREQ_MaxDepth       (CYBRMREQ_TB + 1)
#define CYBRMREQ_MinWidth       (CYBRMREQ_TB + 2)
#define CYBRMREQ_MaxWidth       (CYBRMREQ_TB + 3)
#define CYBRMREQ_MinHeight      (CYBRMREQ_TB + 4)
#define CYBRMREQ_MaxHeight      (CYBRMREQ_TB + 5)
#define CYBRBIDTG_TB            (TAG_USER + 0x50000)
#define CYBRBIDTG_Depth         (CYBRBIDTG_TB + 0)
#define CYBRBIDTG_NominalWidth  (CYBRBIDTG_TB + 1)
#define CYBRBIDTG_NominalHeight (CYBRBIDTG_TB + 2)
#define CYBRBIDTG_MonitorID     (CYBRBIDTG_TB + 3)
#define PIXFMT_LUT8             0UL
#define PIXFMT_RGB16            5UL
#define PIXFMT_ARGB32           11UL
#define RECTFMT_ARGB            2UL
#define LBMI_WIDTH              0x84001001UL
#define LBMI_HEIGHT             0x84001002UL
#define LBMI_DEPTH              0x84001003UL
#define LBMI_PIXFMT             0x84001004UL
#define LBMI_BYTESPERPIX        0x84001005UL
#define LBMI_BYTESPERROW        0x84001006UL
#define LBMI_BASEADDRESS        0x84001007UL

struct CyberModeNode {
    struct Node Node;
    char ModeText[DISPLAYNAMELEN];
    ULONG DisplayID;
    UWORD Width, Height, Depth;
    struct TagItem *DisplayTagList;
};

struct CDrawMsg {
    APTR cdm_MemPtr;
    ULONG cdm_offx, cdm_offy, cdm_xsize, cdm_ysize;
    UWORD cdm_BytesPerRow, cdm_BytesPerPix, cdm_ColorModel;
};

struct CGXBase {
    struct Library lib;
    BPTR seglist;
};

struct ExecBase *SysBase;
struct GfxBase *GfxBase;
struct Library *UtilityBase;
struct Library *OpenRTGBase;

/* openrtg.library's calls (openrtg_lib.sfd), with its registers */
static inline BOOL ortg_get_mode(ULONG id, struct OpenRTGMode *m)
{
    register ULONG d0 __asm("d0") = id;
    register struct OpenRTGMode *a0 __asm("a0") = m;
    register struct Library *a6 __asm("a6") = OpenRTGBase;
    __asm volatile ("jsr -42(a6)" : "+r"(d0), "+r"(a0), "+r"(a6) : : "d1", "a1", "cc", "memory");
    return (BOOL)d0;
}
static inline ULONG ortg_next_mode(ULONG monitor, ULONG previous)
{
    register ULONG d0 __asm("d0") = monitor;
    register ULONG d1 __asm("d1") = previous;
    register struct Library *a6 __asm("a6") = OpenRTGBase;
    __asm volatile ("jsr -36(a6)" : "+r"(d0), "+r"(d1), "+r"(a6) : : "a0", "a1", "cc", "memory");
    return d0;
}
static inline ULONG ortg_best_mode(ULONG monitor, ULONG w, ULONG h, ULONG depth)
{
    register ULONG d0 __asm("d0") = monitor;
    register ULONG d1 __asm("d1") = w;
    register ULONG d2 __asm("d2") = h;
    register ULONG d3 __asm("d3") = depth;
    register struct Library *a6 __asm("a6") = OpenRTGBase;
    __asm volatile ("jsr -48(a6)" : "+r"(d0), "+r"(d1), "+r"(d2), "+r"(d3), "+r"(a6) : : "a0", "a1", "cc", "memory");
    return d0;
}
static inline LONG ortg_pixels(LONG lvo, struct RastPort *rp, LONG x, LONG y, struct OpenRTGPixels *px)
{
    register struct RastPort *a1 __asm("a1") = rp;
    register LONG d0 __asm("d0") = x;
    register LONG d1 __asm("d1") = y;
    register struct OpenRTGPixels *a0 __asm("a0") = px;
    register struct Library *a6 __asm("a6") = OpenRTGBase;
    if (lvo == -78) __asm volatile ("jsr -78(a6)" : "+r"(a1), "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a6) : : "cc", "memory");
    else __asm volatile ("jsr -84(a6)" : "+r"(a1), "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a6) : : "cc", "memory");
    return d0;
}
static inline LONG ortg_fill(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h, ULONG argb)
{
    register struct RastPort *a1 __asm("a1") = rp;
    register LONG d0 __asm("d0") = x;
    register LONG d1 __asm("d1") = y;
    register LONG d2 __asm("d2") = w;
    register LONG d3 __asm("d3") = h;
    register ULONG d4 __asm("d4") = argb;
    register struct Library *a6 __asm("a6") = OpenRTGBase;
    __asm volatile ("jsr -90(a6)" : "+r"(a1), "+r"(d0), "+r"(d1), "+r"(d2), "+r"(d3), "+r"(d4), "+r"(a6) : : "a0", "cc", "memory");
    return d0;
}
static inline LONG ortg_invert(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h)
{
    register struct RastPort *a1 __asm("a1") = rp;
    register LONG d0 __asm("d0") = x;
    register LONG d1 __asm("d1") = y;
    register LONG d2 __asm("d2") = w;
    register LONG d3 __asm("d3") = h;
    register struct Library *a6 __asm("a6") = OpenRTGBase;
    __asm volatile ("jsr -96(a6)" : "+r"(a1), "+r"(d0), "+r"(d1), "+r"(d2), "+r"(d3), "+r"(a6) : : "a0", "cc", "memory");
    return d0;
}
static inline BOOL ortg_info(struct BitMap *bm, struct OpenRTGBitMapInfo *info)
{
    register struct BitMap *a0 __asm("a0") = bm;
    register struct OpenRTGBitMapInfo *a1 __asm("a1") = info;
    register struct Library *a6 __asm("a6") = OpenRTGBase;
    register ULONG d0 __asm("d0");
    __asm volatile ("jsr -102(a6)" : "=r"(d0), "+r"(a0), "+r"(a1), "+r"(a6) : : "d1", "cc", "memory");
    return (BOOL)d0;
}

/* ---- tracing to the serial port, while OpenRTG is being brought up ---- */
#define CGX_TRACE 0
#if CGX_TRACE
static void trace(const char *t, ULONG v)
{
    static int n;
    char b[16];
    if (n++ >= 80) return;
    for (int k = 0; k < 2; k++) {
        const char *q = k ? b : t;
        if (k) { for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15]; b[8] = '\r'; b[9] = '\n'; b[10] = 0; }
        while (*q) {
            register UBYTE c __asm("d0") = (UBYTE)*q++;
            __asm volatile ("move.l a6,-(sp)\n\tmove.l 4.w,a6\n\tjsr -516(a6)\n\tmove.l (sp)+,a6" : "+d"(c) : : "d1", "a0", "a1", "cc", "memory");
        }
    }
}
#else
#define trace(t, v) ((void)0)
#endif

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "cybergraphics.library";
static const char lib_id[] = "cybergraphics.library 43.1 (5.10.2026) OpenRTG's CyberGraphX API, Dalsin Limited\r\n";

static struct Library *lib_init(REG(d0, struct CGXBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct CGXBase *base));
static BPTR lib_close(REG(a6, struct CGXBase *base));
static BPTR lib_expunge(REG(a6, struct CGXBase *base));
static ULONG lib_null(void);
static BOOL IsCyberModeID(REG(d0, ULONG id));
static ULONG BestCModeIDTagList(REG(a0, struct TagItem *tags));
static ULONG CModeRequestTagList(REG(a0, APTR req), REG(a1, struct TagItem *tags));
static struct List *AllocCModeListTagList(REG(a1, struct TagItem *tags));
static void FreeCModeList(REG(a0, struct List *list));
static ULONG ScalePixelArray(REG(a0, APTR src), REG(d0, UWORD sw), REG(d1, UWORD sh), REG(d2, UWORD smod), REG(a1, struct RastPort *rp),
                             REG(d3, UWORD dx), REG(d4, UWORD dy), REG(d5, UWORD dw), REG(d6, UWORD dh), REG(d7, UBYTE fmt));
static ULONG GetCyberMapAttr(REG(a0, struct BitMap *bm), REG(d0, ULONG attr));
static ULONG GetCyberIDAttr(REG(d0, ULONG attr), REG(d1, ULONG id));
static ULONG ReadRGBPixel(REG(a1, struct RastPort *rp), REG(d0, UWORD x), REG(d1, UWORD y));
static LONG WriteRGBPixel(REG(a1, struct RastPort *rp), REG(d0, UWORD x), REG(d1, UWORD y), REG(d2, ULONG pixel));
static ULONG ReadPixelArray(REG(a0, APTR dst), REG(d0, UWORD dx), REG(d1, UWORD dy), REG(d2, UWORD dmod), REG(a1, struct RastPort *rp),
                            REG(d3, UWORD sx), REG(d4, UWORD sy), REG(d5, UWORD w), REG(d6, UWORD h), REG(d7, UBYTE fmt));
static ULONG WritePixelArray(REG(a0, APTR src), REG(d0, UWORD sx), REG(d1, UWORD sy), REG(d2, UWORD smod), REG(a1, struct RastPort *rp),
                             REG(d3, UWORD dx), REG(d4, UWORD dy), REG(d5, UWORD w), REG(d6, UWORD h), REG(d7, UBYTE fmt));
static ULONG MovePixelArray(REG(d0, UWORD sx), REG(d1, UWORD sy), REG(a1, struct RastPort *rp), REG(d2, UWORD dx), REG(d3, UWORD dy), REG(d4, UWORD w), REG(d5, UWORD h));
static ULONG InvertPixelArray(REG(a1, struct RastPort *rp), REG(d0, UWORD x), REG(d1, UWORD y), REG(d2, UWORD w), REG(d3, UWORD h));
static ULONG FillPixelArray(REG(a1, struct RastPort *rp), REG(d0, UWORD x), REG(d1, UWORD y), REG(d2, UWORD w), REG(d3, UWORD h), REG(d4, ULONG pixel));
static void DoCDrawMethodTagList(REG(a0, struct Hook *hook), REG(a1, struct RastPort *rp), REG(a2, struct TagItem *tags));
static void CVideoCtrlTagList(REG(a0, APTR vp), REG(a1, struct TagItem *tags));
static APTR LockBitMapTagList(REG(a0, struct BitMap *bm), REG(a1, struct TagItem *tags));
static void UnLockBitMap(REG(a0, APTR handle));
static void UnLockBitMapTagList(REG(a0, APTR handle), REG(a1, struct TagItem *tags));
static ULONG ExtractColor(REG(a0, struct RastPort *rp), REG(a1, struct BitMap *bm), REG(d0, ULONG colour), REG(d1, ULONG sx), REG(d2, ULONG sy), REG(d3, ULONG w), REG(d4, ULONG h));
static ULONG WriteLUTPixelArray(REG(a0, APTR src), REG(d0, UWORD sx), REG(d1, UWORD sy), REG(d2, UWORD smod), REG(a1, struct RastPort *rp), REG(a2, ULONG *ctab),
                                REG(d3, UWORD dx), REG(d4, UWORD dy), REG(d5, UWORD w), REG(d6, UWORD h), REG(d7, UBYTE ctabfmt));
static ULONG WritePixelArrayAlpha(REG(a0, APTR src), REG(d0, UWORD sx), REG(d1, UWORD sy), REG(d2, UWORD smod), REG(a1, struct RastPort *rp),
                                  REG(d3, UWORD dx), REG(d4, UWORD dy), REG(d5, UWORD w), REG(d6, UWORD h), REG(d7, ULONG alpha));

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)lib_null, (APTR)lib_null, (APTR)lib_null, (APTR)lib_null,           /* -30 to -48: CyberGraphX's private */
    (APTR)IsCyberModeID, (APTR)BestCModeIDTagList, (APTR)CModeRequestTagList,  /* -54, -60, -66 */
    (APTR)AllocCModeListTagList, (APTR)FreeCModeList, (APTR)lib_null,         /* -72, -78, -84 */
    (APTR)ScalePixelArray, (APTR)GetCyberMapAttr, (APTR)GetCyberIDAttr,       /* -90, -96, -102 */
    (APTR)ReadRGBPixel, (APTR)WriteRGBPixel, (APTR)ReadPixelArray,            /* -108, -114, -120 */
    (APTR)WritePixelArray, (APTR)MovePixelArray, (APTR)lib_null,              /* -126, -132, -138 */
    (APTR)InvertPixelArray, (APTR)FillPixelArray, (APTR)DoCDrawMethodTagList, /* -144, -150, -156 */
    (APTR)CVideoCtrlTagList, (APTR)LockBitMapTagList, (APTR)UnLockBitMap,     /* -162, -168, -174 */
    (APTR)UnLockBitMapTagList, (APTR)ExtractColor, (APTR)lib_null,            /* -180, -186, -192 */
    (APTR)WriteLUTPixelArray, (APTR)lib_null, (APTR)lib_null,                 /* -198, -204, -210 */
    (APTR)WritePixelArrayAlpha, (APTR)lib_null, (APTR)lib_null,               /* -216; BltTemplateAlpha, ProcessPixelArray not yet */
    (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct CGXBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

/* ---- the library ---------------------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct CGXBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = LIB_REVISION;
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    UtilityBase = OpenLibrary("utility.library", 39);
    OpenRTGBase = OpenLibrary(OPENRTG_NAME, 0);
    if (!GfxBase || !UtilityBase || !OpenRTGBase || OpenRTGBase->lib_Revision < 4) {
        /* nothing to answer with: give the memory back */
        if (OpenRTGBase) CloseLibrary(OpenRTGBase);
        if (UtilityBase) CloseLibrary(UtilityBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
        return NULL;
    }
    return &base->lib;
}
static struct Library *lib_open(REG(a6, struct CGXBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}
static BPTR lib_close(REG(a6, struct CGXBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}
static BPTR lib_expunge(REG(a6, struct CGXBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) { base->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    CloseLibrary(OpenRTGBase);
    CloseLibrary(UtilityBase);
    CloseLibrary((struct Library *)GfxBase);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}
static ULONG lib_null(void) { return 0; }

/* ---- modes ---------------------------------------------------------------------- */

static ULONG pixfmt_of(UBYTE format) { return format == 0 ? PIXFMT_LUT8 : format == 1 ? PIXFMT_RGB16 : PIXFMT_ARGB32; }

static BOOL IsCyberModeID(REG(d0, ULONG id))
{
    trace("cgx: IsCyberModeID ", id);
    struct OpenRTGMode m;
    return ortg_get_mode(id, &m);
}

static ULONG GetCyberIDAttr(REG(d0, ULONG attr), REG(d1, ULONG id))
{
    trace("cgx: GetCyberIDAttr ", attr);
    struct OpenRTGMode m;
    if (!ortg_get_mode(id, &m)) return 0;
    switch (attr) {
    case CYBRIDATTR_PIXFMT: return pixfmt_of(m.format);
    case CYBRIDATTR_WIDTH: return m.width;
    case CYBRIDATTR_HEIGHT: return m.height;
    case CYBRIDATTR_DEPTH: return m.depth == 32 ? 24 : m.depth;
    case CYBRIDATTR_BPPIX: return m.depth / 8;
    default: return 0;
    }
}

static ULONG BestCModeIDTagList(REG(a0, struct TagItem *tags))
{
    trace("cgx: BestCModeIDTagList ", 0);
    ULONG depth = GetTagData(CYBRBIDTG_Depth, 8, tags);
    ULONG w = GetTagData(CYBRBIDTG_NominalWidth, 640, tags), h = GetTagData(CYBRBIDTG_NominalHeight, 480, tags);
    ULONG monitor = GetTagData(CYBRBIDTG_MonitorID, 0, tags);
    for (ULONG n = monitor ? monitor : 1; n <= (monitor ? monitor : 4); n++) {
        ULONG id = ortg_best_mode(n, w, h, depth > 16 ? 32 : depth > 8 ? 16 : 8);
        if (id && id != NO_ID) return id;
    }
    return NO_ID;
}

static ULONG CModeRequestTagList(REG(a0, APTR req), REG(a1, struct TagItem *tags))
{
    return 0;   /* no requester yet: as if cancelled */
}

static struct List *AllocCModeListTagList(REG(a1, struct TagItem *tags))
{
    ULONG mind = GetTagData(CYBRMREQ_MinDepth, 8, tags), maxd = GetTagData(CYBRMREQ_MaxDepth, 32, tags);
    ULONG minw = GetTagData(CYBRMREQ_MinWidth, 320, tags), maxw = GetTagData(CYBRMREQ_MaxWidth, 1600, tags);
    ULONG minh = GetTagData(CYBRMREQ_MinHeight, 240, tags), maxh = GetTagData(CYBRMREQ_MaxHeight, 1200, tags);
    struct List *l = AllocVec(sizeof *l, MEMF_PUBLIC | MEMF_CLEAR);
    if (!l) return NULL;
    l->lh_Head = (struct Node *)&l->lh_Tail; l->lh_Tail = NULL; l->lh_TailPred = (struct Node *)&l->lh_Head;
    for (ULONG n = 1; n <= 4; n++) {
        ULONG id = 0;
        while ((id = ortg_next_mode(n, id)) != 0) {
            struct OpenRTGMode m;
            struct CyberModeNode *c;
            ULONG d;
            if (!ortg_get_mode(id, &m)) continue;
            d = m.depth == 32 ? 24 : m.depth;
            if (d < mind || d > maxd || m.width < minw || m.width > maxw || m.height < minh || m.height > maxh) continue;
            if (!(c = AllocVec(sizeof *c, MEMF_PUBLIC | MEMF_CLEAR))) continue;
            for (int i = 0; i < DISPLAYNAMELEN - 1 && m.name[i]; i++) c->ModeText[i] = m.name[i];
            c->Node.ln_Name = c->ModeText;
            c->DisplayID = id; c->Width = m.width; c->Height = m.height; c->Depth = (UWORD)d;
            AddTail(l, &c->Node);
        }
    }
    return l;
}

static void FreeCModeList(REG(a0, struct List *list))
{
    struct Node *n;
    if (!list) return;
    while ((n = RemHead(list))) FreeVec(n);
    FreeVec(list);
}

/* ---- bitmaps -------------------------------------------------------------------- */

static ULONG GetCyberMapAttr(REG(a0, struct BitMap *bm), REG(d0, ULONG attr))
{
    struct OpenRTGBitMapInfo i;
    trace("cgx: GetCyberMapAttr ", attr);
    trace("  bitmap ", (ULONG)bm);
    trace("  ours ", bm ? ortg_info(bm, &i) : 0);
    trace("  bpr/rows ", bm ? (ULONG)bm->BytesPerRow << 16 | bm->Rows : 0);
    trace("  depth/flags/pad ", bm ? (ULONG)bm->Depth << 24 | (ULONG)bm->Flags << 16 | bm->pad : 0);
    if (!bm) return 0;
    if (!ortg_info(bm, &i)) {
        switch (attr) {
        case CYBRMATTR_WIDTH: return (ULONG)bm->BytesPerRow * 8;
        case CYBRMATTR_HEIGHT: return bm->Rows;
        case CYBRMATTR_DEPTH: return bm->Depth;
        default: return 0;          /* ISCYBERGFX: FALSE */
        }
    }
    switch (attr) {
    case CYBRMATTR_XMOD: return i.bytes_per_row;
    case CYBRMATTR_BPPIX: return 1;
    case CYBRMATTR_DISPADR: return (ULONG)i.memory;
    case CYBRMATTR_PIXFMT: return PIXFMT_LUT8;
    case CYBRMATTR_WIDTH: return i.width;
    case CYBRMATTR_HEIGHT: return i.height;
    case CYBRMATTR_DEPTH: return 8;
    case CYBRMATTR_ISCYBERGFX: return TRUE;
    case CYBRMATTR_ISLINEARMEM: return TRUE;
    default: return 0;
    }
}

static APTR LockBitMapTagList(REG(a0, struct BitMap *bm), REG(a1, struct TagItem *tags))
{
    trace("cgx: LockBitMapTagList ", (ULONG)bm);
    struct OpenRTGBitMapInfo i;
    struct TagItem *t, *state = tags;
    if (!bm || !ortg_info(bm, &i)) return NULL;
    while ((t = NextTagItem(&state))) {
        ULONG *p = (ULONG *)t->ti_Data;
        if (!p) continue;
        switch (t->ti_Tag) {
        case LBMI_WIDTH: *p = i.width; break;
        case LBMI_HEIGHT: *p = i.height; break;
        case LBMI_DEPTH: *p = 8; break;
        case LBMI_PIXFMT: *p = PIXFMT_LUT8; break;
        case LBMI_BYTESPERPIX: *p = 1; break;
        case LBMI_BYTESPERROW: *p = i.bytes_per_row; break;
        case LBMI_BASEADDRESS: *p = (ULONG)i.memory; break;
        }
    }
    return (APTR)bm;            /* the handle: nothing to undo yet (no blitter queue) */
}

static void UnLockBitMap(REG(a0, APTR handle)) { }
static void UnLockBitMapTagList(REG(a0, APTR handle), REG(a1, struct TagItem *tags)) { }

/* ---- pixels ---------------------------------------------------------------------- */

static ULONG pixels(LONG lvo, APTR mem, LONG x, LONG y, LONG mod, ULONG fmt, ULONG *ctab, struct RastPort *rp, LONG rx, LONG ry, LONG w, LONG h, LONG dw, LONG dh)
{
    trace(lvo == -78 ? "cgx: write pixels, format/w/h " : "cgx: read pixels, format/w/h ", fmt << 24 | (ULONG)w << 12 | (ULONG)h);
    struct OpenRTGPixels px;
    if (!rp || !mem || w <= 0 || h <= 0) return 0;
    px.data = mem; px.x = x; px.y = y; px.modulo = mod; px.format = fmt; px.ctable = (const unsigned long *)ctab;
    px.width = w; px.height = h; px.dest_width = dw; px.dest_height = dh;
    return (ULONG)ortg_pixels(lvo, rp, rx, ry, &px);
}

static ULONG WritePixelArray(REG(a0, APTR src), REG(d0, UWORD sx), REG(d1, UWORD sy), REG(d2, UWORD smod), REG(a1, struct RastPort *rp),
                             REG(d3, UWORD dx), REG(d4, UWORD dy), REG(d5, UWORD w), REG(d6, UWORD h), REG(d7, UBYTE fmt))
{
    return pixels(-78, src, sx, sy, smod, fmt, NULL, rp, (WORD)dx, (WORD)dy, w, h, 0, 0);
}

static ULONG ReadPixelArray(REG(a0, APTR dst), REG(d0, UWORD dx), REG(d1, UWORD dy), REG(d2, UWORD dmod), REG(a1, struct RastPort *rp),
                            REG(d3, UWORD sx), REG(d4, UWORD sy), REG(d5, UWORD w), REG(d6, UWORD h), REG(d7, UBYTE fmt))
{
    return pixels(-84, dst, dx, dy, dmod, fmt, NULL, rp, (WORD)sx, (WORD)sy, w, h, 0, 0);
}

static ULONG ScalePixelArray(REG(a0, APTR src), REG(d0, UWORD sw), REG(d1, UWORD sh), REG(d2, UWORD smod), REG(a1, struct RastPort *rp),
                             REG(d3, UWORD dx), REG(d4, UWORD dy), REG(d5, UWORD dw), REG(d6, UWORD dh), REG(d7, UBYTE fmt))
{
    return pixels(-78, src, 0, 0, smod, fmt, NULL, rp, (WORD)dx, (WORD)dy, sw, sh, dw, dh);
}

static ULONG WriteLUTPixelArray(REG(a0, APTR src), REG(d0, UWORD sx), REG(d1, UWORD sy), REG(d2, UWORD smod), REG(a1, struct RastPort *rp), REG(a2, ULONG *ctab),
                                REG(d3, UWORD dx), REG(d4, UWORD dy), REG(d5, UWORD w), REG(d6, UWORD h), REG(d7, UBYTE ctabfmt))
{
    return pixels(-78, src, sx, sy, smod, ORTG_PIX_INDEX, ctab, rp, (WORD)dx, (WORD)dy, w, h, 0, 0);
}

/* Alpha: 8-bit screens have no blending yet, so a pixel is drawn where it is
 * at least half opaque (and global alpha too). */
static ULONG WritePixelArrayAlpha(REG(a0, APTR src), REG(d0, UWORD sx), REG(d1, UWORD sy), REG(d2, UWORD smod), REG(a1, struct RastPort *rp),
                                  REG(d3, UWORD dx), REG(d4, UWORD dy), REG(d5, UWORD w), REG(d6, UWORD h), REG(d7, ULONG alpha))
{
    ULONG done = 0;
    for (UWORD j = 0; j < h; j++) {
        const ULONG *row = (const ULONG *)((const UBYTE *)src + (ULONG)(sy + j) * smod) + sx;
        UWORD i = 0;
        while (i < w) {
            UWORD run;
            while (i < w && (row[i] >> 24) < 0x80) i++;
            for (run = 0; i + run < w && (row[i + run] >> 24) >= 0x80; run++) ;
            if (run) done += pixels(-78, (APTR)row, i, 0, smod, RECTFMT_ARGB, NULL, rp, (WORD)dx + i, (WORD)dy + j, run, 1, 0, 0);
            i += run;
        }
    }
    return done;
}

static ULONG ReadRGBPixel(REG(a1, struct RastPort *rp), REG(d0, UWORD x), REG(d1, UWORD y))
{
    ULONG argb = 0;
    pixels(-84, &argb, 0, 0, 4, RECTFMT_ARGB, NULL, rp, (WORD)x, (WORD)y, 1, 1, 0, 0);
    return argb & 0xFFFFFF;
}

static LONG WriteRGBPixel(REG(a1, struct RastPort *rp), REG(d0, UWORD x), REG(d1, UWORD y), REG(d2, ULONG pixel))
{
    ULONG argb = pixel;
    return pixels(-78, &argb, 0, 0, 4, RECTFMT_ARGB, NULL, rp, (WORD)x, (WORD)y, 1, 1, 0, 0) ? 0 : -1;
}

static ULONG FillPixelArray(REG(a1, struct RastPort *rp), REG(d0, UWORD x), REG(d1, UWORD y), REG(d2, UWORD w), REG(d3, UWORD h), REG(d4, ULONG pixel))
{
    return rp ? (ULONG)ortg_fill(rp, (WORD)x, (WORD)y, w, h, pixel) : 0;
}

static ULONG InvertPixelArray(REG(a1, struct RastPort *rp), REG(d0, UWORD x), REG(d1, UWORD y), REG(d2, UWORD w), REG(d3, UWORD h))
{
    return rp ? (ULONG)ortg_invert(rp, (WORD)x, (WORD)y, w, h) : 0;
}

static ULONG MovePixelArray(REG(d0, UWORD sx), REG(d1, UWORD sy), REG(a1, struct RastPort *rp), REG(d2, UWORD dx), REG(d3, UWORD dy), REG(d4, UWORD w), REG(d5, UWORD h))
{
    if (!rp) return 0;
    ClipBlit(rp, (WORD)sx, (WORD)sy, rp, (WORD)dx, (WORD)dy, w, h, 0xC0);
    return (ULONG)w * h;
}

/* The hook gets each visible piece of the RastPort's bitmap: the memory of
 * the piece's first pixel and where the piece is in the RastPort. */
static void DoCDrawMethodTagList(REG(a0, struct Hook *hook), REG(a1, struct RastPort *rp), REG(a2, struct TagItem *tags))
{
    trace("cgx: DoCDrawMethodTagList ", (ULONG)rp);
    struct OpenRTGBitMapInfo i;
    struct Layer *l;
    if (!hook || !rp || !ortg_info(rp->BitMap, &i)) return;
    l = rp->Layer;
    if (!l) {
        struct CDrawMsg m = { i.memory, 0, 0, i.width, i.height, (UWORD)i.bytes_per_row, 1, PIXFMT_LUT8 };
        CallHookPkt(hook, rp, &m);
        return;
    }
    ObtainSemaphore(&l->Lock);
    {
        LONG ox = l->bounds.MinX - l->Scroll_X, oy = l->bounds.MinY - l->Scroll_Y;
        for (struct ClipRect *cr = l->ClipRect; cr; cr = cr->Next) {
            LONG a0 = cr->bounds.MinX, b0 = cr->bounds.MinY, a1 = cr->bounds.MaxX, b1 = cr->bounds.MaxY;
            struct CDrawMsg m;
            if (cr->obscured) continue;
            if (a0 < 0) a0 = 0;
            if (b0 < 0) b0 = 0;
            if (a1 >= i.width) a1 = i.width - 1;
            if (b1 >= i.height) b1 = i.height - 1;
            if (a0 > a1 || b0 > b1) continue;
            m.cdm_MemPtr = (UBYTE *)i.memory + b0 * i.bytes_per_row + a0;
            m.cdm_offx = (ULONG)(a0 - ox); m.cdm_offy = (ULONG)(b0 - oy);
            m.cdm_xsize = (ULONG)(a1 - a0 + 1); m.cdm_ysize = (ULONG)(b1 - b0 + 1);
            m.cdm_BytesPerRow = (UWORD)i.bytes_per_row; m.cdm_BytesPerPix = 1; m.cdm_ColorModel = PIXFMT_LUT8;
            CallHookPkt(hook, rp, &m);
        }
    }
    ReleaseSemaphore(&l->Lock);
}

static void CVideoCtrlTagList(REG(a0, APTR vp), REG(a1, struct TagItem *tags)) { }

static ULONG ExtractColor(REG(a0, struct RastPort *rp), REG(a1, struct BitMap *bm), REG(d0, ULONG colour), REG(d1, ULONG sx), REG(d2, ULONG sy), REG(d3, ULONG w), REG(d4, ULONG h))
{
    return FALSE;
}

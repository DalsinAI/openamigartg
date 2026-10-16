/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The display database (DESIGN.md, section 4): graphics.library's
 * NextDisplayInfo, FindDisplayInfo, GetDisplayInfoData and ModeNotAvailable
 * answer for OpenRTG's ModeIDs from the mode table (modes.c), so ScreenMode
 * prefs, the ASL screen mode requester and programs see the monitors; and
 * BestModeIDA picks an OpenRTG mode when the request is for one (0.8).
 *
 * OS-friendly (DESIGN.md, section 4): SetFunction() under Forbid() with the
 * caches cleared; every call that isn't ours goes to the vector SetFunction()
 * returned; the patches are never taken out, and when OpenRTG is off they
 * pass every call straight through. They run on the caller's stack and task,
 * are re-entrant, and never call DOS or Wait().
 */
#include <exec/types.h>
#include "ortg_state.h"
#include <exec/memory.h>
#include <exec/execbase.h>
#include <utility/tagitem.h>
#include <graphics/displayinfo.h>
#include <graphics/monitor.h>
#include <graphics/modeid.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "modes.h"
#include "displaydb.h"
#include "screens.h"

#define REG(r, decl) register decl __asm(#r)
#define NO_ID ((ULONG)INVALID_ID)
/* An RTG pixel in the display database's ticks, as Picasso96 gives it
 * (measured 5 Oct 2026); the monitor's ratio follows from it. */
#define ORTG_TICKS 18
/* Intuition keeps the mouse in a 16-bit range: the pointer stops at 30000
 * ticks. A picture wider than 30000 / ORTG_TICKS pixels (1667 at 18) could
 * not be reached in its right-hand part, the screen bar's icons, a window's
 * right edge and its scroll bar among it (OpenRTG 0.13.1, 10 Oct 2026: 1920 x
 * 1080 stopped the pointer at 1666; with 15 ticks it reaches the edge).
 * So a mode gets as many ticks as keep its larger side under the limit, up
 * to ORTG_TICKS. */
#define ORTG_MOUSE_LIMIT 29900

typedef ULONG (*next_fn)(REG(d0, ULONG), REG(a6, struct Library *));
typedef APTR (*find_fn)(REG(d0, ULONG), REG(a6, struct Library *));
typedef ULONG (*data_fn)(REG(a0, APTR), REG(a1, UBYTE *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(a6, struct Library *));
typedef ULONG (*avail_fn)(REG(d0, ULONG), REG(a6, struct Library *));
typedef ULONG (*best_fn)(REG(a0, struct TagItem *), REG(a6, struct Library *));

/* A DisplayInfoHandle is a pointer to graphics' own DisplayInfoRecord, and
 * graphics and intuition read some of its fields straight from the handle
 * (5 Oct 2026: with a handle to OpenRTG's mode table instead, intuition's
 * mouse ran to its limits). So OpenRTG's handles are records laid out as
 * graphics' are (V37 onwards): the node, the keys, the clip rectangle. */
struct record_node { struct record_node *succ, *pred, *child, *parent; };
struct ortg_record {
    struct record_node node;
    UWORD major, minor;             /* the ModeID's halves */
    struct TagItem tag;
    ULONG control;
    APTR get_data, set_data;
    struct Rectangle clip_oscan;    /* the whole picture */
    ULONG reserved[2];
    /* the record's data, right after it as graphics keeps it: rec_Tag is a
     * TAG_MORE to here, and each chunk is a tag list of its own, since a
     * QueryHeader is {StructID, DisplayID} {TAG_SKIP, Length} and Length
     * counts the 8-byte pairs that follow (measured on the chipset's and
     * Picasso96's records, 5 Oct 2026): DISP, DIMS, MNTR, NAME, TAG_DONE */
    ULONG data[80];
    const struct ortg_mode *mode;   /* OpenRTG's own, after graphics' fields */
    ULONG gen;                      /* a Native mode's: native_gen[] when its data was filled (0.17) */
};
/* What the display database writes (0.18): its part of the library's state (ortg_state.h). */
struct db_g {
    next_fn old_next;
    find_fn old_find;
    data_fn old_data;
    avail_fn old_avail;
    best_fn old_best;
    struct ortg_mode_table **tables;        /* [1..4], the library's */
    volatile int db_on, patched;
    struct MonitorSpec mspec[ORTG_MAX_MONITORS + 1];
    struct SpecialMonitor mspecial[ORTG_MAX_MONITORS + 1];
    char mspec_name[ORTG_MAX_MONITORS + 1][20];
    /* Each monitor's records, one per slot (ortg_record_slot): a mode's place in the table, the Native
     * modes' the last three (0.17), so a record stays where it is in graphics' tree while the Native size
     * comes, changes and goes. */
    struct ortg_record *records[ORTG_MAX_MONITORS + 1];
    struct ortg_record *monitor_records[ORTG_MAX_MONITORS + 1];
    ULONG native_gen[ORTG_MAX_MONITORS + 1];   /* up by one each time a monitor's Native modes are made again */
};
ULONG ortg_displaydb_size(void) { return sizeof(struct db_g); }
#define DB ((struct db_g *)ortg_S->db)
#define old_next (DB->old_next)
#define old_find (DB->old_find)
#define old_data (DB->old_data)
#define old_avail (DB->old_avail)
#define old_best (DB->old_best)
#define tables (DB->tables)
#define db_on (DB->db_on)
#define patched (DB->patched)
#define mspec (DB->mspec)
#define mspecial (DB->mspecial)
#define mspec_name (DB->mspec_name)
#define records (DB->records)
#define monitor_records (DB->monitor_records)
#define native_gen (DB->native_gen)

/* A board's monitor has nothing of the chipset's to program. Called by graphics with its own A4: it
 * reads nothing of the library's. */
static LONG __STDARGS__ do_monitor(struct MonitorSpec *ms) { (void)ms; return 0; }   /* the field is __stdargs */

static struct ortg_mode_table *table_for(ULONG id)
{
    ULONG n = (id >> 24) - 0x60;
    if ((id & 0x1000) == 0 || (id >> 28) != 6 || n < 1 || n > ORTG_MAX_MONITORS) return NULL;
    return tables ? tables[n] : NULL;
}

static const struct ortg_mode *ours(ULONG id)
{
    struct ortg_mode_table *t = table_for(id);
    return t ? ortg_find_mode(t, id) : NULL;
}

static ULONG fill(const struct ortg_mode *m, UBYTE *buf, ULONG size, ULONG tag);

/* graphics' private AddDisplayInfo (LVO -738), as monitor drivers use it */
static void add_info(APTR record, struct Library *gfx)
{
    register APTR a0 __asm("a0") = record;
    register struct Library *a6 __asm("a6") = gfx;
    __asm volatile ("jsr -738(a6)" : "+r"(a0), "+r"(a6) : : "d0", "d1", "a1", "cc", "memory");
}

static struct ortg_record *record_of(const struct ortg_mode *m)
{
    int n = (int)((m->mode_id >> 24) - 0x60);
    struct ortg_mode_table *t = (tables && n >= 1 && n <= ORTG_MAX_MONITORS) ? tables[n] : NULL;
    int i = t ? ortg_record_slot(t, m) : -1;
    struct ortg_record *r;
    if (i < 0 || !records[n]) return NULL;
    r = &records[n][i];
    r->major = (UWORD)(m->mode_id >> 16); r->minor = (UWORD)m->mode_id;
    /* filled once; a Native mode's again when its size has changed (0.17) */
    if (r->mode != m || (m->native && r->gen != native_gen[n])) {
        static const ULONG kinds[] = { DTAG_DISP, DTAG_DIMS, DTAG_MNTR, DTAG_NAME };
        UBYTE *at = (UBYTE *)r->data;
        for (int k = 0; k < 4; k++) {
            struct QueryHeader *q = (struct QueryHeader *)at;
            fill(m, at, sizeof r->data - 8 - (ULONG)(at - (UBYTE *)r->data), kinds[k]);
            at += 16 + q->Length * 8;   /* the chunk as its header says */
        }
        ((struct TagItem *)at)->ti_Tag = TAG_DONE;
    }
    r->tag.ti_Tag = TAG_MORE; r->tag.ti_Data = (ULONG)r->data;
    r->clip_oscan.MinX = 0; r->clip_oscan.MinY = 0;
    r->clip_oscan.MaxX = (WORD)(m->width - 1); r->clip_oscan.MaxY = (WORD)(m->height - 1);
    r->mode = m;
    r->gen = native_gen[n];
    return r;
}

/* A Native slot's record with no Native mode in it (no size asked for, or a format the board lacks): in
 * graphics' tree under its ModeID all the same, with no data, so graphics finds nothing in it, and
 * ModeNotAvailable says so (0.17). */
static void native_record_empty(int n, int format)
{
    struct ortg_record *r = &records[n][ORTG_MAX_MODES - ORTG_NATIVE_MODES + format];
    ULONG id = ortg_native_id(n, format);
    r->major = (UWORD)(id >> 16); r->minor = (UWORD)id;
    r->data[0] = TAG_DONE;
    r->tag.ti_Tag = TAG_MORE; r->tag.ti_Data = (ULONG)r->data;
    r->clip_oscan.MinX = r->clip_oscan.MinY = r->clip_oscan.MaxX = r->clip_oscan.MaxY = 0;
    r->mode = NULL;
}

void ortg_displaydb_native(const struct ortg_mode_table *t)
{
    int n = t ? t->monitor : 0, have[ORTG_FORMATS] = { 0, 0, 0 };
    if (n < 1 || n > ORTG_MAX_MONITORS || !records[n] || !t->native) return;
    native_gen[n]++;
    for (int i = 0; i < t->full_count; i++)
        if (t->full[i].native) { have[t->full[i].format] = 1; record_of(&t->full[i]); }
    for (int f = 0; f < ORTG_FORMATS; f++)
        if (!have[f]) native_record_empty(n, f);
}

static const struct ortg_mode *our_handle(APTR h)
{
    for (int n = 1; tables && n <= ORTG_MAX_MONITORS; n++) {
        struct ortg_mode_table *t = tables[n];
        if (t && records[n] && (struct ortg_record *)h >= records[n] && (struct ortg_record *)h < records[n] + ORTG_MAX_MODES) {
            const struct ortg_mode *m = ((struct ortg_record *)h)->mode;
            /* still the mode in that slot (a Native slot may be empty now) */
            return m && ortg_record_slot(t, m) == (struct ortg_record *)h - records[n] ? m : NULL;
        }
    }
    return NULL;
}

static ULONG first_from(int n)
{
    for (; tables && n <= ORTG_MAX_MONITORS; n++)
        if (tables[n] && tables[n]->count) return tables[n]->modes[0].mode_id;
    return NO_ID;
}

static ULONG next_patch(REG(d0, ULONG id), REG(a6, struct Library *gfx))
{
    if (db_on) {
        struct ortg_mode_table *t = table_for(id);
        if (t) {
            for (int i = 0; i + 1 < t->count; i++)
                if (t->modes[i].mode_id == id) return t->modes[i + 1].mode_id;
            return first_from(t->monitor + 1);
        }
    }
    {
        /* graphics' own walk passes OpenRTG's records too, since they are in
         * its tree (0.4): those are listed in our order below, Standard or
         * All, so the walk steps over them (6 Oct 2026: ScreenMode and
         * OpenRTG LISTDB showed only the first, 320x200) */
        ULONG r = old_next(id, gfx);
        while (db_on && r != NO_ID && table_for(r)) r = old_next(r, gfx);
        if (r == NO_ID && db_on) {
            ortg_native_check(0);           /* OpenRTG's list next: its Native modes at the size asked for now (0.17) */
            return first_from(1);
        }
        return r;
    }
}

/* A Native ModeID asked about: its mode made again first when the host has asked for a new size (0.17). */
static void native_first(ULONG id)
{
    if (db_on && ORTG_IS_NATIVE(id) && table_for(id)) ortg_native_check((int)((id >> 24) - 0x60));
}

static APTR find_patch(REG(d0, ULONG id), REG(a6, struct Library *gfx))
{
    const struct ortg_mode *m;
    native_first(id);
    m = db_on ? ours(id) : NULL;
    struct ortg_record *r = m ? record_of(m) : NULL;
    return r ? (APTR)r : old_find(id, gfx);
}

static void header(struct QueryHeader *q, ULONG tag, ULONG id, ULONG size)
{
    q->StructID = tag;
    q->DisplayID = id;
    q->SkipID = TAG_SKIP;
    q->Length = (size - sizeof *q + 7) / 8;
}

/* A Native mode's ticks are those of the board's largest picture, whatever its size of the moment
 * (0.17): a screen keeps the size it opened at while the host asks for another, and its pointer's
 * scale must not change under it. */
static int ticks_of(const struct ortg_mode *m)
{
    int side = m->width > m->height ? m->width : m->height, t;
    if (m->native) {
        int n = (int)((m->mode_id >> 24) - 0x60);
        const struct ortg_mode_table *tb = (tables && n >= 1 && n <= ORTG_MAX_MONITORS) ? tables[n] : NULL;
        if (tb && tb->max_width && tb->max_height) side = tb->max_width > tb->max_height ? tb->max_width : tb->max_height;
    }
    t = side > 0 ? ORTG_MOUSE_LIMIT / side : ORTG_TICKS;
    return t > ORTG_TICKS ? ORTG_TICKS : t < 1 ? 1 : t;
}

static ULONG fill(const struct ortg_mode *m, UBYTE *buf, ULONG size, ULONG tag)
{
    union { struct DisplayInfo d; struct DimensionInfo dm; struct MonitorInfo mi; struct NameInfo n; } r;
    ULONG len = 0, i;
    UBYTE *z = (UBYTE *)&r;
    int n = (int)((m->mode_id >> 24) - 0x60), ticks = ticks_of(m);
    for (i = 0; i < sizeof r; i++) z[i] = 0;
    switch (tag) {
    case DTAG_DISP:
        len = sizeof r.d;
        /* as Picasso96's modes: OS 3.2's intuition treats 0x02000000 as a
         * board's mode (its mouse and pointer), measured 5 Oct 2026 */
        r.d.PropertyFlags = 0x02000000UL | DIPF_IS_DBUFFER | DIPF_IS_SPRITES_CHNG_RES | DIPF_IS_DRAGGABLE | DIPF_IS_WB | DIPF_IS_GENLOCK;
        r.d.Resolution.x = r.d.Resolution.y = ticks;
        r.d.PixelSpeed = 25;
        r.d.NumStdSprites = 0;
        r.d.PaletteRange = 512;
        r.d.SpriteResolution.x = r.d.SpriteResolution.y = ticks;
        r.d.RedBits = m->format == ORTG_RGB16 ? 5 : 8;
        r.d.GreenBits = m->format == ORTG_RGB16 ? 6 : 8;
        r.d.BlueBits = m->format == ORTG_RGB16 ? 5 : 8;
        break;
    case DTAG_DIMS:
        len = sizeof r.dm;
        r.dm.MaxDepth = m->depth;      /* 32 for ARGB32, as CyberGraphX and Picasso96 accept it (0.8; SDL asks for 32) */
        r.dm.MinRasterWidth = 16; r.dm.MinRasterHeight = 16;
        r.dm.MaxRasterWidth = 4096; r.dm.MaxRasterHeight = 4096;
        r.dm.Nominal.MaxX = r.dm.MaxOScan.MaxX = r.dm.VideoOScan.MaxX = r.dm.TxtOScan.MaxX = r.dm.StdOScan.MaxX = (WORD)(m->width - 1);
        r.dm.Nominal.MaxY = r.dm.MaxOScan.MaxY = r.dm.VideoOScan.MaxY = r.dm.TxtOScan.MaxY = r.dm.StdOScan.MaxY = (WORD)(m->height - 1);
        break;
    case DTAG_MNTR:
        len = sizeof r.mi;
        r.mi.Mspc = (n >= 1 && n <= ORTG_MAX_MONITORS) ? &mspec[n] : NULL;
        r.mi.ViewResolution.x = r.mi.ViewResolution.y = ticks;
        r.mi.Compatibility = MCOMPAT_NOBODY;
        r.mi.MouseTicks.x = r.mi.MouseTicks.y = ticks;
        r.mi.TotalRows = (UWORD)(m->height + 28);
        r.mi.TotalColorClocks = 94;
        {
            /* OS 3.2 reads two rectangles of the whole picture, in ticks,
             * from MonitorInfo's pad (0, 0, w*ticks-1, h*ticks-1), as
             * Picasso96 fills them (measured 5 Oct 2026); without them the
             * mouse has no room to move on the screen */
            ULONG *pad = (ULONG *)r.mi.pad;
            for (int k = 0; k < 2; k++) {
                pad[4 * k] = 0; pad[4 * k + 1] = 0;
                pad[4 * k + 2] = (ULONG)m->width * ticks - 1;
                pad[4 * k + 3] = (ULONG)m->height * ticks - 1;
            }
        }   /* as the resolution: intuition scales the mouse by them */
        /* the monitor's preferred mode: the one at the board's display size, in this mode's format
         * (ACRTG v6's DISPLAY); before, or with no such mode, the mode itself, as OpenRTG always gave */
        {
            const struct ortg_mode_table *tb = (tables && n >= 1 && n <= ORTG_MAX_MONITORS) ? tables[n] : NULL;
            ULONG pref = tb ? ortg_preferred_mode(tb, m->format) : 0;
            r.mi.PreferredModeID = pref ? pref : m->mode_id;
        }
        break;
    case DTAG_NAME:
        len = sizeof r.n;
        for (i = 0; i < DISPLAYNAMELEN - 1 && m->name[i]; i++) r.n.Name[i] = (UBYTE)m->name[i];
        break;
    default:
        return 0;
    }
    header((struct QueryHeader *)&r, tag, m->mode_id, len);
    if (len > size) len = size;
    for (i = 0; i < len; i++) buf[i] = z[i];
    return len;
}

static ULONG data_patch(REG(a0, APTR h), REG(a1, UBYTE *buf), REG(d0, ULONG size), REG(d1, ULONG tag), REG(d2, ULONG id), REG(a6, struct Library *gfx))
{
    if (db_on) {
        const struct ortg_mode *m;
        if (!h) native_first(id);
        m = h ? our_handle(h) : ours(id);
        if (m) return fill(m, buf, size, tag);
    }
    return old_data(h, buf, size, tag, id, gfx);
}

static ULONG avail_patch(REG(d0, ULONG id), REG(a6, struct Library *gfx))
{
    native_first(id);
    if (db_on && table_for(id)) return ours(id) ? 0 : DI_AVAIL_NOMONITOR;
    return old_avail(id, gfx);
}

/* The property flags OpenRTG's modes have (fill(), DTAG_DISP). */
#define ORTG_PROPS (0x02000000UL | DIPF_IS_DBUFFER | DIPF_IS_SPRITES_CHNG_RES | DIPF_IS_DRAGGABLE | DIPF_IS_WB | DIPF_IS_GENLOCK)

/* BestModeIDA. Graphics' own answer stands unless the request is for a
 * board's mode: the RTG property (0x02000000) is a must-have, more than 8
 * bits are asked for, the monitor or the source mode is OpenRTG's, graphics
 * found nothing, or an 8-bit mode is asked for while an OpenRTG screen is
 * showing (6 Oct 2026: SDL's 320x200x8 got PAL Low Res). A must-have flag
 * OpenRTG's modes lack, or the RTG flag as a must-not-have, keeps graphics'
 * answer. The size is the desired one, else the nominal one, else the
 * source mode's, else 640x480. */
static ULONG best_patch(REG(a0, struct TagItem *tags), REG(a6, struct Library *gfx))
{
    ULONG r = old_best(tags, gfx);
    ULONG must = 0, mustnot = 0, monitor = NO_ID, source = NO_ID, depth = 1;
    LONG nw = 0, nh = 0, dw = 0, dh = 0;
    struct TagItem *t = tags;
    int want = 0, from = 1, to = ORTG_MAX_MONITORS;
    if (!db_on || !tables) return r;
    while (t) {
        switch (t->ti_Tag) {
        case TAG_DONE: t = NULL; continue;
        case TAG_MORE: t = (struct TagItem *)t->ti_Data; continue;
        case TAG_SKIP: t += t->ti_Data + 1; continue;
        case BIDTAG_DIPFMustHave: must = t->ti_Data; break;
        case BIDTAG_DIPFMustNotHave: mustnot = t->ti_Data; break;
        case BIDTAG_ViewPort:
            if (t->ti_Data && source == NO_ID) source = GetVPModeID((struct ViewPort *)t->ti_Data);
            break;
        case BIDTAG_NominalWidth: nw = (LONG)t->ti_Data; break;
        case BIDTAG_NominalHeight: nh = (LONG)t->ti_Data; break;
        case BIDTAG_DesiredWidth: dw = (LONG)t->ti_Data; break;
        case BIDTAG_DesiredHeight: dh = (LONG)t->ti_Data; break;
        case BIDTAG_Depth: depth = t->ti_Data; break;
        case BIDTAG_MonitorID: monitor = t->ti_Data; break;
        case BIDTAG_SourceID: source = t->ti_Data; break;
        }
        t++;
    }
    if ((must & ~ORTG_PROPS) || (mustnot & ORTG_PROPS & 0x02000000UL)) return r;
    if (monitor != NO_ID) {
        ULONG n = (monitor >> 24) - 0x60;
        if ((monitor >> 28) != 6 || n < 1 || n > ORTG_MAX_MONITORS) return r;   /* a chipset monitor */
        from = to = (int)n; want = 1;
    }
    if (must & 0x02000000UL) want = 1;
    if (depth > 8) want = 1;
    if (r == NO_ID) want = 1;
    if (source != NO_ID && ours(source)) want = 1;
    if (depth == 8 && ortg_any_shown()) want = 1;
    if (!want) return r;
    if (!dw) dw = nw;
    if (!dh) dh = nh;
    if ((!dw || !dh) && source != NO_ID && ours(source)) { const struct ortg_mode *m = ours(source); if (!dw) dw = m->width; if (!dh) dh = m->height; }
    if (!dw) dw = 640;
    if (!dh) dh = 480;
    for (int n = from; n <= to; n++)
        if (tables[n] && tables[n]->count) {
            ULONG id = ortg_best_mode(tables[n], (int)dw, (int)dh, (int)depth);
            if (id) return id;
        }
    return r;
}

int ortg_displaydb(struct Library *gfx, struct ortg_mode_table **t, int on)
{
    tables = t;
    if (!patched) {
        if (!on) return 1;
        /* Each monitor's MonitorSpec starts as a copy of the default
         * monitor's: graphics and intuition call its functions (ms_transform,
         * ms_translate, ms_scale and the overscan ones) for any mode. */
        /* graphics' calls go through ortg_S->gfx (ortg_state.h), which screens.c sets only when
         * OpenRTG's screens go on: gfx here, for these two calls */
        struct GfxBase *gb = (struct GfxBase *)gfx, *was = ortg_S->gfx;
        struct MonitorSpec *def;
        ortg_S->gfx = gb;
        def = OpenMonitor(NULL, 0);
        ortg_S->gfx = was;
        for (int n = 1; n <= ORTG_MAX_MONITORS; n++) {
            char *s = mspec_name[n];
            if (def) mspec[n] = *def;
            mspec[n].ms_Node.xln_Succ = mspec[n].ms_Node.xln_Pred = NULL;
            /* not the chipset's: its view is the board's, at RTG ticks */
            mspec[n].ratioh = mspec[n].ratiov = (44 << RATIO_FIXEDPART) / ORTG_TICKS;
            mspec[n].ms_transform = NULL; mspec[n].ms_translate = NULL; mspec[n].ms_scale = NULL;
            /* nothing of the chipset's beam: as Picasso96's monitors have it */
            /* as Picasso96's: a special monitor, whose do_monitor programs nothing */
            mspec[n].ms_Flags = MSF_REQUEST_SPECIAL;
            mspecial[n].spm_Node.xln_Type = NT_GRAPHICS;
            mspecial[n].do_monitor = do_monitor;
            mspec[n].DeniseMaxDisplayColumn = mspec[n].BeamCon0 = mspec[n].min_row = 0;
            mspec[n].DeniseMinDisplayColumn = 0;
            mspec[n].ms_Special = &mspecial[n];
            mspec[n].DisplayCompatible = 0;
            mspec[n].ms_xoffset = 9;                     /* as Picasso96's */
            /* its own (empty) list of display records: a copy of the
             * default's list header would lead into the default monitor's */
            {
                UBYTE *z = (UBYTE *)&mspec[n].DisplayInfoDataBase;
                for (ULONG i = 0; i < sizeof mspec[n].DisplayInfoDataBase + sizeof mspec[n].DisplayInfoDataBaseSemaphore; i++) z[i] = 0;
            }
            mspec[n].ms_xoffset = mspec[n].ms_yoffset = 0;
            mspec[n].ms_LegalView.MinX = mspec[n].ms_LegalView.MinY = mspec[n].ms_LegalView.MaxX = mspec[n].ms_LegalView.MaxY = 0;
            mspec[n].ms_maxoscan = NULL; mspec[n].ms_videoscan = NULL;
            mspec[n].total_rows = 628; mspec[n].total_colorclocks = 94;
            const char *p = "OpenRTG.";
            int i = 0;
            while (*p) s[i++] = *p++;
            s[i++] = (char)('0' + n);
            s[i] = 0;
            mspec[n].ms_Node.xln_Name = s;
        }
        if (def) { ortg_S->gfx = gb; CloseMonitor(def); ortg_S->gfx = was; }
        for (int n = 1; n <= ORTG_MAX_MONITORS; n++)
            if (tables && tables[n] && !records[n]) {
                records[n] = AllocVec(sizeof(struct ortg_record) * ORTG_MAX_MODES, MEMF_PUBLIC | MEMF_CLEAR);
                monitor_records[n] = AllocVec(sizeof(struct ortg_record) * ORTG_MAX_MODES, MEMF_PUBLIC | MEMF_CLEAR);
            }
        for (int n = 1; n <= ORTG_MAX_MONITORS; n++)
            if (tables && tables[n] && records[n] && monitor_records[n]) {
                for (int i = 0; i < tables[n]->full_count; i++) record_of(&tables[n]->full[i]);
                /* a board that can be asked for a size: its three Native slots, filled or empty (0.17) */
                if (tables[n]->native) ortg_displaydb_native(tables[n]);
            }
        Forbid();
        /* in graphics' own display database, as Picasso96's modes are: a
         * record at the top for each upper word of the ModeIDs (AddDisplayInfo
         * keys it by its minor key) and the modes as its children. Graphics looks modes up
         * itself, not only through FindDisplayInfo, and finds nothing for a
         * ModeID it has no record of (5 Oct 2026: intuition's mouse stayed
         * at 0,0 until the records were in the tree) */
        for (int n = 1; n <= ORTG_MAX_MONITORS; n++) {
            int tops = 0;
            int others;
            if (!tables || !tables[n] || !monitor_records[n] || !records[n]) continue;
            /* the slots in use: every mode but the Native ones, then the Native slots on a board that can be
             * asked for a size (0.17), filled or not, so they are in the tree before any size comes */
            for (others = 0; others < tables[n]->full_count && !tables[n]->full[others].native; others++) ;
            for (int i = 0; i < others + (tables[n]->native ? ORTG_NATIVE_MODES : 0); i++) {
                int slot = i < others ? i : ORTG_MAX_MODES - ORTG_NATIVE_MODES + (i - others);
                struct ortg_record *r = &records[n][slot], *top = NULL, *last;
                int k;
                for (k = 0; k < tops; k++)
                    if (monitor_records[n][k].minor == r->major) top = &monitor_records[n][k];
                if (!top) {
                    top = &monitor_records[n][tops++];
                    top->minor = r->major;
                    top->tag.ti_Tag = TAG_DONE;
                    add_info(top, gfx);
                }
                r->node.parent = &top->node;
                for (last = (struct ortg_record *)top->node.child; last && last->node.succ; last = (struct ortg_record *)last->node.succ) ;
                r->node.pred = last ? &last->node : NULL;
                if (last) last->node.succ = &r->node; else top->node.child = &r->node;
            }
        }
        /* in graphics' list of monitors, as Picasso96's are */
        for (int n = 1; n <= ORTG_MAX_MONITORS; n++)
            if (tables && tables[n]) AddTail(&gb->MonitorList, (struct Node *)&mspec[n].ms_Node);
        /* graphics calls the patches with its own A4: each through an entry stub (ortg_state.h) */
        old_next = (next_fn)SetFunction(gfx, -732, ortg_entry(next_patch));
        old_find = (find_fn)SetFunction(gfx, -726, ortg_entry(find_patch));
        old_data = (data_fn)SetFunction(gfx, -756, ortg_entry(data_patch));
        old_avail = (avail_fn)SetFunction(gfx, -798, ortg_entry(avail_patch));
        old_best = (best_fn)SetFunction(gfx, -1050, ortg_entry(best_patch));
        patched = 1;
        CacheClearU();
        Permit();
    }
    db_on = on;
    return 1;
}

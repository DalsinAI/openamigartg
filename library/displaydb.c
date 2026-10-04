/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The display database (DESIGN.md, section 4): graphics.library's
 * NextDisplayInfo, FindDisplayInfo, GetDisplayInfoData and ModeNotAvailable
 * answer for OpenRTG's ModeIDs from the mode table (modes.c), so ScreenMode
 * prefs, the ASL screen mode requester and programs see the monitors.
 *
 * OS-friendly (DESIGN.md, section 4): SetFunction() under Forbid() with the
 * caches cleared; every call that isn't ours goes to the vector SetFunction()
 * returned; the patches are never taken out, and when OpenRTG is off they
 * pass every call straight through. They run on the caller's stack and task,
 * are re-entrant, and never call DOS or Wait().
 */
#include <exec/types.h>
#include <exec/execbase.h>
#include <utility/tagitem.h>
#include <graphics/displayinfo.h>
#include <graphics/monitor.h>
#include <graphics/modeid.h>
#include <proto/exec.h>

#include "modes.h"
#include "displaydb.h"

#define REG(r, decl) register decl __asm(#r)
#define NO_ID ((ULONG)INVALID_ID)

typedef ULONG (*next_fn)(REG(d0, ULONG), REG(a6, struct Library *));
typedef APTR (*find_fn)(REG(d0, ULONG), REG(a6, struct Library *));
typedef ULONG (*data_fn)(REG(a0, APTR), REG(a1, UBYTE *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(a6, struct Library *));
typedef ULONG (*avail_fn)(REG(d0, ULONG), REG(a6, struct Library *));

static next_fn old_next;
static find_fn old_find;
static data_fn old_data;
static avail_fn old_avail;
static struct ortg_mode_table **tables;        /* [1..4], the library's */
static volatile int db_on, patched;
static struct MonitorSpec mspec[ORTG_MAX_MONITORS + 1];
static char mspec_name[ORTG_MAX_MONITORS + 1][20];

static struct ortg_mode_table *table_for(ULONG id)
{
    ULONG high = id >> 16;
    if ((id & 0x1000) == 0 || high < 0x5001 || high > 0x5000 + ORTG_MAX_MONITORS) return NULL;
    return tables ? tables[high - 0x5000] : NULL;
}

static const struct ortg_mode *ours(ULONG id)
{
    struct ortg_mode_table *t = table_for(id);
    return t ? ortg_find_mode(t, id) : NULL;
}

static const struct ortg_mode *our_handle(APTR h)
{
    for (int n = 1; tables && n <= ORTG_MAX_MONITORS; n++) {
        struct ortg_mode_table *t = tables[n];
        if (t && (const struct ortg_mode *)h >= t->full && (const struct ortg_mode *)h < t->full + t->full_count)
            return (const struct ortg_mode *)h;
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
    ULONG r = old_next(id, gfx);
    return (r == NO_ID && db_on) ? first_from(1) : r;
}

static APTR find_patch(REG(d0, ULONG id), REG(a6, struct Library *gfx))
{
    const struct ortg_mode *m = db_on ? ours(id) : NULL;
    return m ? (APTR)m : old_find(id, gfx);
}

static void header(struct QueryHeader *q, ULONG tag, ULONG id, ULONG size)
{
    q->StructID = tag;
    q->DisplayID = id;
    q->SkipID = TAG_SKIP;
    q->Length = (size - sizeof *q + 7) / 8;
}

static ULONG fill(const struct ortg_mode *m, UBYTE *buf, ULONG size, ULONG tag)
{
    union { struct DisplayInfo d; struct DimensionInfo dm; struct MonitorInfo mi; struct NameInfo n; } r;
    ULONG len = 0, i;
    UBYTE *z = (UBYTE *)&r;
    int n = (int)((m->mode_id >> 16) - 0x5000);
    for (i = 0; i < sizeof r; i++) z[i] = 0;
    switch (tag) {
    case DTAG_DISP:
        len = sizeof r.d;
        r.d.PropertyFlags = DIPF_IS_WB | DIPF_IS_SPRITES | DIPF_IS_FOREIGN;
        r.d.Resolution.x = r.d.Resolution.y = 22;
        r.d.PixelSpeed = 1;
        r.d.NumStdSprites = 1;
        r.d.PaletteRange = 4096;
        r.d.SpriteResolution.x = r.d.SpriteResolution.y = 22;
        r.d.RedBits = m->format == ORTG_RGB16 ? 5 : 8;
        r.d.GreenBits = m->format == ORTG_RGB16 ? 6 : 8;
        r.d.BlueBits = m->format == ORTG_RGB16 ? 5 : 8;
        break;
    case DTAG_DIMS:
        len = sizeof r.dm;
        r.dm.MaxDepth = m->depth == 32 ? 24 : m->depth;
        r.dm.MinRasterWidth = 16; r.dm.MinRasterHeight = 16;
        r.dm.MaxRasterWidth = 4096; r.dm.MaxRasterHeight = 4096;
        r.dm.Nominal.MaxX = r.dm.MaxOScan.MaxX = r.dm.VideoOScan.MaxX = r.dm.TxtOScan.MaxX = r.dm.StdOScan.MaxX = (WORD)(m->width - 1);
        r.dm.Nominal.MaxY = r.dm.MaxOScan.MaxY = r.dm.VideoOScan.MaxY = r.dm.TxtOScan.MaxY = r.dm.StdOScan.MaxY = (WORD)(m->height - 1);
        break;
    case DTAG_MNTR:
        len = sizeof r.mi;
        r.mi.Mspc = (n >= 1 && n <= ORTG_MAX_MONITORS) ? &mspec[n] : NULL;
        r.mi.ViewResolution.x = r.mi.ViewResolution.y = 22;
        r.mi.TotalRows = m->height;
        r.mi.TotalColorClocks = (UWORD)(m->width / 4);
        r.mi.Compatibility = MCOMPAT_NOBODY;
        r.mi.MouseTicks.x = r.mi.MouseTicks.y = 1;
        r.mi.PreferredModeID = m->mode_id;
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
        const struct ortg_mode *m = h ? our_handle(h) : ours(id);
        if (m) return fill(m, buf, size, tag);
    }
    return old_data(h, buf, size, tag, id, gfx);
}

static ULONG avail_patch(REG(d0, ULONG id), REG(a6, struct Library *gfx))
{
    if (db_on && table_for(id)) return ours(id) ? 0 : DI_AVAIL_NOMONITOR;
    return old_avail(id, gfx);
}

int ortg_displaydb(struct Library *gfx, struct ortg_mode_table **t, int on)
{
    tables = t;
    if (!patched) {
        if (!on) return 1;
        for (int n = 1; n <= ORTG_MAX_MONITORS; n++) {
            char *s = mspec_name[n];
            const char *p = "OpenRTG.";
            int i = 0;
            while (*p) s[i++] = *p++;
            s[i++] = (char)('0' + n);
            s[i] = 0;
            mspec[n].ms_Node.xln_Name = s;
        }
        Forbid();
        old_next = (next_fn)SetFunction(gfx, -732, (APTR)next_patch);
        old_find = (find_fn)SetFunction(gfx, -726, (APTR)find_patch);
        old_data = (data_fn)SetFunction(gfx, -756, (APTR)data_patch);
        old_avail = (avail_fn)SetFunction(gfx, -798, (APTR)avail_patch);
        patched = 1;
        CacheClearU();
        Permit();
    }
    db_on = on;
    return 1;
}

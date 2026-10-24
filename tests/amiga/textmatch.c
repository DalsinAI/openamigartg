/* TextMatch: does Text on Workbench's screen (an RTG bitmap, drawn by OpenRTG) draw what
 * TextLength and TextExtent say, and the same pixels as OpenGfx's own text core draws on a
 * planar bitmap? (Team 4, 24 Oct 2026; lab only.)
 *   TextMatch
 * For every font in the system's list (up to 10) and topaz 8, five soft styles and seven
 * strings, JAM2 then JAM1:
 *   - a strip of the window is filled with pen A, then Text in pen F on pen B at the pen;
 *   - the same on a planar bitmap of the same size (the reference: OpenGfx's core);
 *   - every pixel of the strip is classed A, F, B or other, on the screen by its RGB against
 *     the three pens' RGB, on the planar bitmap by its pen;
 *   - the classes must be the same everywhere, cp_x must move by TextLength on both, and with
 *     JAM2 the changed columns must be TextExtent's MinX..MaxX.
 * Prints one line per mismatch (the first 40, the first six with their rows: # the text's pen, o the
 * background, . the strip) and "TextMatch: N cases, M differ".
 * Build: m68k-amigaos-gcc -O2 -m68020 -Wall -Wno-format -o TextMatch textmatch.c (the os32-gcc16 stove).
 * Run it on a Workbench screen on an ACRTG board, from a Shell. Colour and right-to-left fonts are
 * left out: OpenRTG doesn't draw those as OS 3.2 does yet.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT (LICENSE). */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <stdio.h>
#include <string.h>

struct Library *CyberGfxBase;
extern struct GfxBase *GfxBase;

#define SW 620
#define SH 48
#define MAXF 7

static const char *const strs[] = {
    "drawn after the writes", "PokePull frame 00001", "Ram Disk", "Workbench AmigaChrome 3.2",
    "iiiWWW|j_gy,Qq", "11111111", "\x7f\xff\x80 A"
};
static const UBYTE styles[] = { 0, FSF_BOLD, FSF_ITALIC, FSF_UNDERLINED, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED };

static UBYTE scr_class[SH][SW], ref_class[SH][SW];
static int shown;

static int classify_rgb(ULONG v, const ULONG *rgb)
{
    int i;
    v &= 0xFFFFFF;
    for (i = 0; i < 3; i++) if ((rgb[i] & 0xFFFFFF) == v) return i;
    return 3;
}

int main(void)
{
    struct Screen *scr;
    struct Window *win;
    struct BitMap *pbm;
    struct RastPort prp, *rp;
    struct TextAttr ta[MAXF];
    char names[MAXF][40];
    struct TextFont *fonts[MAXF];
    int nf = 0, i, cases = 0, bad = 0;
    ULONG rgb[3];
    const UBYTE PA = 3, PF = 1, PB = 2;   /* the strip's pen, the text's, its background's */

    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
    if (!CyberGfxBase) { printf("TextMatch: no cybergraphics.library\n"); return 20; }
    scr = LockPubScreen(NULL);
    if (!scr) { printf("TextMatch: no Workbench screen\n"); return 20; }
    win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, 10, WA_Top, scr->BarHeight + 20,
                         WA_Width, SW + 8, WA_Height, SH + 8, WA_Borderless, TRUE, WA_Activate, FALSE,
                         WA_RMBTrap, TRUE, TAG_DONE);
    UnlockPubScreen(NULL, scr);
    if (!win) { printf("TextMatch: no window\n"); return 20; }
    rp = win->RPort;
    pbm = AllocBitMap(SW, SH, 2, BMF_CLEAR, NULL);
    if (!pbm) { printf("TextMatch: no planar bitmap\n"); CloseWindow(win); return 20; }
    InitRastPort(&prp);
    prp.BitMap = pbm;
    printf("TextMatch: window bitmap %s, planar bitmap %s\n",
           GetCyberMapAttr(rp->BitMap, CYBRMATTR_ISCYBERGFX) ? "RTG" : "not RTG",
           GetCyberMapAttr(pbm, CYBRMATTR_ISCYBERGFX) ? "RTG (!)" : "planar");

    /* the three pens' RGB on the screen */
    for (i = 0; i < 3; i++) {
        UBYTE pen = i == 0 ? PA : i == 1 ? PF : PB;
        SetAPen(rp, pen);
        SetDrMd(rp, JAM1);
        RectFill(rp, 4, 4, 7, 7);
        rgb[i] = ReadRGBPixel(rp, 5, 5);
    }
    printf("TextMatch: pens A %lu %06lx, F %lu %06lx, B %lu %06lx\n", (unsigned long)PA, (unsigned long)(rgb[0] & 0xFFFFFF),
           (unsigned long)PF, (unsigned long)(rgb[1] & 0xFFFFFF), (unsigned long)PB, (unsigned long)(rgb[2] & 0xFFFFFF));
    if ((rgb[0] & 0xFFFFFF) == (rgb[1] & 0xFFFFFF) || (rgb[0] & 0xFFFFFF) == (rgb[2] & 0xFFFFFF) ||
        (rgb[1] & 0xFFFFFF) == (rgb[2] & 0xFFFFFF)) {
        printf("TextMatch: two pens share a colour, can't class pixels\n");
        goto out;
    }

    /* the fonts: topaz 8, then those in the system's list */
    strcpy(names[0], "topaz.font"); ta[0].ta_Name = (STRPTR)names[0]; ta[0].ta_YSize = 8; ta[0].ta_Style = 0; ta[0].ta_Flags = 0;
    nf = 1;
    Forbid();
    {
        struct Node *n;
        for (n = GfxBase->TextFonts.lh_Head; n->ln_Succ && nf < MAXF; n = n->ln_Succ) {
            struct TextFont *tf = (struct TextFont *)n;
            if (!tf->tf_Message.mn_Node.ln_Name) continue;
            strncpy(names[nf], tf->tf_Message.mn_Node.ln_Name, 39); names[nf][39] = 0;
            ta[nf].ta_Name = (STRPTR)names[nf]; ta[nf].ta_YSize = tf->tf_YSize;
            ta[nf].ta_Style = tf->tf_Style; ta[nf].ta_Flags = tf->tf_Flags & ~FPF_REMOVED;
            nf++;
        }
    }
    Permit();
    for (i = 0; i < nf; i++) fonts[i] = OpenFont(&ta[i]);

    for (i = 0; i < nf; i++) {
        struct TextFont *f = fonts[i];
        int si, ti, mi;
        if (!f || f->tf_YSize > SH - 8) continue;
        if (f->tf_Style & FSF_COLORFONT || f->tf_Flags & FPF_REVPATH) {
            printf("font %s/%ld: colour or right to left, left out\n", names[i], (long)f->tf_YSize);
            continue;
        }
        printf("font %s/%ld: X %ld base %ld smear %ld flags $%02lx space %s kern %s\n", names[i], (long)f->tf_YSize,
               (long)f->tf_XSize, (long)f->tf_Baseline, (long)f->tf_BoldSmear, (unsigned long)f->tf_Flags,
               f->tf_CharSpace ? "yes" : "no", f->tf_CharKern ? "yes" : "no");
        for (si = 0; si < (int)(sizeof styles); si++)
            for (ti = 0; ti < (int)(sizeof strs / sizeof strs[0]); ti++)
                for (mi = 0; mi < 2; mi++) {
                    const char *s = strs[ti];
                    LONG n = strlen(s), x0 = 24, y = 6 + f->tf_Baseline, len, x, yy, diff = 0, cmin = SW, cmax = -1;
                    LONG cpx_s, cpx_p;
                    struct TextExtent te;
                    UBYTE mode = mi ? JAM1 : JAM2;
                    struct RastPort *r2[2];
                    int k;
                    r2[0] = rp; r2[1] = &prp;
                    LONG cw, ch = f->tf_YSize + 12;
                    SetFont(rp, f);
                    SetSoftStyle(rp, styles[si], 0xFF);
                    cw = x0 + TextLength(rp, (STRPTR)s, n) + 48;
                    if (cw > SW) cw = SW;
                    if (ch > SH) ch = SH;
                    for (k = 0; k < 2; k++) {
                        struct RastPort *q = r2[k];
                        SetFont(q, f);
                        SetSoftStyle(q, styles[si], 0xFF);
                        SetDrMd(q, JAM1);
                        SetAPen(q, PA);
                        RectFill(q, 0, 0, cw - 1, ch - 1);
                        SetAPen(q, PF); SetBPen(q, PB); SetDrMd(q, mode);
                        Move(q, x0, y);
                        Text(q, (STRPTR)s, n);
                    }
                    cpx_s = rp->cp_x; cpx_p = prp.cp_x;
                    len = TextLength(rp, (STRPTR)s, n);
                    TextExtent(rp, (STRPTR)s, n, &te);
                    WaitBlit();
                    for (yy = 0; yy < ch; yy++)
                        for (x = 0; x < cw; x++) {
                            scr_class[yy][x] = classify_rgb(ReadRGBPixel(rp, x, yy), rgb);
                            {
                                LONG p = ReadPixel(&prp, x, yy);
                                ref_class[yy][x] = p == PA ? 0 : p == PF ? 1 : p == PB ? 2 : 3;
                            }
                            if (scr_class[yy][x] != ref_class[yy][x]) diff++;
                            if (scr_class[yy][x] != 0) { if (x < cmin) cmin = x; if (x > cmax) cmax = x; }
                        }
                    cases++;
                    {
                        int ok = !diff && cpx_s == x0 + len && cpx_p == x0 + len;
                        if (mode == JAM2 && n) ok = ok && cmin == x0 + te.te_Extent.MinX && cmax == x0 + te.te_Extent.MaxX;
                        if (!ok) {
                            bad++;
                            if (shown++ < 40) {
                                printf("DIFF %s/%ld style %ld %s \"%s\": TextLength %ld extent %ld..%ld; screen cols %ld..%ld cp +%ld, planar cp +%ld; %ld pixels differ",
                                       names[i], (long)f->tf_YSize, (long)styles[si], mode == JAM2 ? "JAM2" : "JAM1", s, len,
                                       (long)te.te_Extent.MinX, (long)te.te_Extent.MaxX, cmin - x0, cmax - x0,
                                       cpx_s - x0, cpx_p - x0, diff);
                                for (yy = 0; yy < ch && diff; yy++)
                                    for (x = 0; x < cw; x++)
                                        if (scr_class[yy][x] != ref_class[yy][x]) {
                                            printf(" (first at %ld,%ld: screen %ld, planar %ld)", x - x0, yy - y,
                                                   (long)scr_class[yy][x], (long)ref_class[yy][x]);
                                            yy = ch; break;
                                        }
                                printf("\n");
                                if (shown <= 6 && diff) {   /* the rows around the first difference, screen | planar */
                                    LONG fx = -1, fy = -1, r0, c0;
                                    for (yy = 0; yy < ch && fx < 0; yy++)
                                        for (x = 0; x < cw; x++)
                                            if (scr_class[yy][x] != ref_class[yy][x]) { fx = x; fy = yy; break; }
                                    c0 = fx - 12 < 0 ? 0 : fx - 12;
                                    for (r0 = 6; r0 < 6 + f->tf_YSize && r0 < ch; r0++) {
                                        char a[32], b[32];
                                        int q;
                                        for (q = 0; q < 28 && c0 + q < cw; q++) {
                                            a[q] = ".#o?"[scr_class[r0][c0 + q]];
                                            b[q] = ".#o?"[ref_class[r0][c0 + q]];
                                        }
                                        a[q] = b[q] = 0;
                                        printf("  row %2ld%s %s | %s\n", r0 - y, r0 == fy ? "*" : " ", a, b);
                                    }
                                    printf("  (columns from %ld)\n", c0 - x0);
                                }
                            }
                        }
                    }
                }
    }
    printf("TextMatch: %ld cases, %ld differ\n", (long)cases, (long)bad);
    for (i = 0; i < nf; i++) if (fonts[i]) CloseFont(fonts[i]);
out:
    FreeBitMap(pbm);
    CloseWindow(win);
    CloseLibrary(CyberGfxBase);
    return bad ? 5 : 0;
}

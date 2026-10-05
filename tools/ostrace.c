/* OSTrace: logs what reaches the ROM's OpenScreenTagList (installed before
 * Picasso96, so its patch calls through this), and the screen that comes
 * back. Not shipped. MIT, Dalsin Limited. */
#include <exec/types.h>
#include <utility/tagitem.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

APTR old_os;

static void ser(const char *t)
{
    while (*t) {
        register UBYTE c __asm("d0") = (UBYTE)*t++;
        __asm volatile ("move.l a6,-(sp)\n\tmove.l 4.w,a6\n\tjsr -516(a6)\n\tmove.l (sp)+,a6" : "+d"(c) : : "d1", "a0", "a1", "cc", "memory");
    }
}
static void hex(ULONG v)
{
    char b[10];
    for (int i = 0; i < 8; i++) b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
    b[8] = ' '; b[9] = 0;
    ser(b);
}

void __attribute__((used)) before(struct NewScreen *ns, struct TagItem *tags)
{
    ser("OS ns="); hex((ULONG)ns);
    if (ns) { ser("w/h/d "); hex(ns->Width); hex(ns->Height); hex(ns->Depth); ser("modes "); hex(ns->ViewModes); ser("type "); hex(ns->Type); }
    ser("\r\n");
    for (int i = 0; tags && i < 60 && tags[i].ti_Tag != TAG_DONE; i++) {
        if (tags[i].ti_Tag == TAG_IGNORE) continue;
        if (tags[i].ti_Tag == TAG_MORE) { tags = (struct TagItem *)tags[i].ti_Data; i = -1; ser("   (more)\r\n"); continue; }
        ser("   "); hex(tags[i].ti_Tag); hex(tags[i].ti_Data); ser("\r\n");
    }
}

void __attribute__((used)) after(struct Screen *s)
{
    ser("OS -> "); hex((ULONG)s);
    if (s) { ser("vp "); hex(s->ViewPort.DxOffset); hex(s->ViewPort.DyOffset); hex(s->ViewPort.DWidth); hex(s->ViewPort.DHeight); hex(s->ViewPort.Modes); }
    ser("\r\n");
}

__asm(
"_os_entry:\n"
"   movem.l a0-a1,-(sp)\n"
"   movem.l d0-d1/a0-a1,-(sp)\n"
"   move.l a1,-(sp)\n"
"   move.l a0,-(sp)\n"
"   jsr _before\n"
"   addq.l #8,sp\n"
"   movem.l (sp)+,d0-d1/a0-a1\n"
"   movem.l (sp)+,a0-a1\n"
"   jsr ([_old_os])\n"
"   move.l d0,-(sp)\n"
"   move.l d0,-(sp)\n"
"   jsr _after\n"
"   addq.l #4,sp\n"
"   move.l (sp)+,d0\n"
"   rts\n");
void os_entry(void);

int main(void)
{
    Forbid();
    old_os = SetFunction((struct Library *)IntuitionBase, -612, (APTR)os_entry);
    Permit();
    ser("OSTrace: on\r\n");
    Wait(0);
    return 0;
}

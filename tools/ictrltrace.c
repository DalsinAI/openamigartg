/* ICtrlTrace: logs every IntuitionControlA() call (object, tags, result) to
 * the serial port, to learn how an RTG system registers with OS 3.2's
 * intuition. Run it before LoadMonDrvs; it stays. Not shipped.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

static APTR old_ictrl;

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

void __attribute__((used)) note(APTR obj, struct TagItem *tags, ULONG ret, ULONG caller)
{
    struct Task *me = FindTask(NULL);
    ser("ictrl obj="); hex((ULONG)obj); ser("ret="); hex(ret); ser("from="); hex(caller);
    ser("task="); ser(me->tc_Node.ln_Name ? me->tc_Node.ln_Name : "?"); ser("\r\n");
    for (int i = 0; tags && i < 40 && tags[i].ti_Tag != TAG_DONE; i++) {
        ser("   tag "); hex(tags[i].ti_Tag); hex(tags[i].ti_Data);
        if (tags[i].ti_Tag == TAG_MORE) { tags = (struct TagItem *)tags[i].ti_Data; i = -1; ser("(more)"); }
        ser("\r\n");
    }
}

/* IntuitionControlA(object a0, taglist a1): call through, then note */
__asm(
"_ictrl_entry:\n"
"   movem.l a0-a1,-(sp)\n"
"   jsr ([_old_ictrl])\n"
"   movem.l (sp)+,a0-a1\n"
"   move.l d0,-(sp)\n"
"   move.l 4(sp),-(sp)\n"
"   move.l d0,-(sp)\n"
"   move.l a1,-(sp)\n"
"   move.l a0,-(sp)\n"
"   jsr _note\n"
"   lea 16(sp),sp\n"
"   move.l (sp)+,d0\n"
"   rts\n");
void ictrl_entry(void);

int main(void)
{
    Forbid();
    old_ictrl = SetFunction((struct Library *)IntuitionBase, -1212, (APTR)ictrl_entry);
    Permit();
    ser("ICtrlTrace: on\r\n");
    Wait(0);                                  /* stays: the patch is in this program */
    return 0;
}

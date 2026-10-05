/* Priv1Trace: logs calls of intuition's private function at -576 (which
 * Picasso96 patches), with every register, to the serial port; the first
 * N calls, then every 50th. Not shipped. MIT, Dalsin Limited. */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

APTR old_priv;
static ULONG calls;

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

/* regs: d0-d7, a0-a6 as pushed, then the caller's return address */
void __attribute__((used)) note(ULONG *r)
{
    calls++;
    if (calls > 60 && calls % 50) return;
    ser("priv1 #"); hex(calls); ser("from "); hex(r[15]); ser("\r\n  d:");
    for (int i = 0; i < 8; i++) hex(r[i]);
    ser("\r\n  a:");
    for (int i = 8; i < 15; i++) hex(r[i]);
    ser("\r\n");
}

__asm(
"_priv_entry:\n"
"   movem.l d0-d7/a0-a6,-(sp)\n"
"   move.l sp,-(sp)\n"
"   jsr _note\n"
"   addq.l #4,sp\n"
"   movem.l (sp)+,d0-d7/a0-a6\n"
"   move.l _old_priv,-(sp)\n"
"   rts\n");
void priv_entry(void);

int main(void)
{
    Forbid();
    old_priv = SetFunction((struct Library *)IntuitionBase, -576, (APTR)priv_entry);
    Permit();
    ser("Priv1Trace: on, old ");
    hex((ULONG)old_priv);
    ser("\r\n");
    Wait(0);
    return 0;
}

/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * AlertSpy: patches exec's Alert so that a software failure prints the
 * task, its registers and its stack to the serial port. Not shipped.
 *   Run >NIL: AlertSpy */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <proto/dos.h>

extern struct ExecBase *SysBase;
static APTR old_alert;
static ULONG regs[16];

static void put(char c)
{
    register char d0 __asm("d0") = c;
    register struct ExecBase *a6 __asm("a6") = SysBase;
    __asm volatile ("jsr -516(a6)" : "+r"(d0), "+r"(a6) : : "d1", "a0", "a1", "cc", "memory");
}
static void str(const char *s) { while (*s) put(*s++); }
static void hex(ULONG v) { for (int i = 28; i >= 0; i -= 4) put("0123456789abcdef"[(v >> i) & 15]); }

static void report(ULONG code)
{
    struct Task *t = FindTask(NULL);
    ULONG *sp = (ULONG *)regs[15];
    str("\nalertspy: alert "); hex(code); str(" task "); hex((ULONG)t);
    str(" "); str(t && t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "?");
    str("\n regs d0-d7 a0-a7:");
    for (int i = 0; i < 16; i++) { if (i % 8 == 0) str("\n  "); hex(regs[i]); put(' '); }
    str("\n stack:");
    for (int i = 0; i < 96; i++) { if (i % 8 == 0) str("\n  "); hex(sp[i]); put(' '); }
    str("\n tc_SPReg "); hex((ULONG)t->tc_SPReg); str(" upper "); hex((ULONG)t->tc_SPUpper); str("\n");
}

/* Alert(d7 = code), a6 = SysBase */
static void __attribute__((used)) alert_c(ULONG code) { report(code); }
asm(
"_alert_patch:\n"
"    movem.l d0-d7/a0-a7,_regs\n"
"    movem.l d0-d1/a0-a1,-(sp)\n"
"    move.l  d7,-(sp)\n"
"    jsr     _alert_c\n"
"    addq.l  #4,sp\n"
"    movem.l (sp)+,d0-d1/a0-a1\n"
"    move.l  _old_alert,-(sp)\n"
"    rts\n");
extern void alert_patch(void);

int main(void)
{
    Forbid();
    old_alert = SetFunction((struct Library *)SysBase, -108, (APTR)alert_patch);
    CacheClearU();
    Permit();
    PutStr((STRPTR)"AlertSpy on\n");
    Wait(0);
    return 0;
}

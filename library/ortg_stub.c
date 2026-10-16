/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Entry stubs (ortg_stub.h). Plain C, no Amiga calls: the 68k words are
 * written out here.
 */
#include <exec/types.h>
#include <exec/libraries.h>

#include "ortg_stub.h"

static UWORD *take(struct ortg_stubs *p, ULONG bytes)
{
    UBYTE *at = p->next;
    if (!at || at + bytes > p->end) return NULL;
    p->next = at + bytes;
    return (UWORD *)at;
}

static UWORD *put_long(UWORD *w, ULONG v)
{
    *w++ = (UWORD)(v >> 16);
    *w++ = (UWORD)v;
    return w;
}

APTR ortg_stub_reg(struct ortg_stubs *p, APTR a4, APTR fn)
{
    UWORD *s = take(p, ORTG_STUB_REG_BYTES), *w = s;
    if (!s) return NULL;
    *w++ = 0x2F0C;                      /* move.l  a4,-(sp)          */
    *w++ = 0x287C;                      /* movea.l #a4,a4            */
    w = put_long(w, (ULONG)a4);
    *w++ = 0x4EB9;                      /* jsr     fn                */
    w = put_long(w, (ULONG)fn);
    *w++ = 0x285F;                      /* movea.l (sp)+,a4          */
    *w = 0x4E75;                        /* rts                       */
    return s;
}

APTR ortg_stub_c2(struct ortg_stubs *p, APTR a4, APTR fn)
{
    UWORD *s = take(p, ORTG_STUB_C2_BYTES), *w = s;
    if (!s) return NULL;
    *w++ = 0x2F0C;                      /* move.l  a4,-(sp)          */
    *w++ = 0x287C;                      /* movea.l #a4,a4            */
    w = put_long(w, (ULONG)a4);
    *w++ = 0x2F2F; *w++ = 0x000C;       /* move.l  12(sp),-(sp): the second argument */
    *w++ = 0x2F2F; *w++ = 0x000C;       /* move.l  12(sp),-(sp): the first           */
    *w++ = 0x4EB9;                      /* jsr     fn                */
    w = put_long(w, (ULONG)fn);
    *w++ = 0x508F;                      /* addq.l  #8,sp             */
    *w++ = 0x285F;                      /* movea.l (sp)+,a4          */
    *w = 0x4E75;                        /* rts                       */
    return s;
}

LONG ortg_stub_vectors(struct ortg_stubs *p, APTR a4, struct Library *lib, int first)
{
    LONG n = 0;
    for (int k = first; (ULONG)k * 6 <= lib->lib_NegSize; k++) {
        UWORD *jmp = (UWORD *)((UBYTE *)lib - 6 * k);
        APTR stub;
        if (jmp[0] != 0x4EF9) break;    /* jmp abs.l, as MakeLibrary writes each */
        if (!(stub = ortg_stub_reg(p, a4, (APTR)((ULONG)jmp[1] << 16 | jmp[2])))) return -1;
        jmp[1] = (UWORD)((ULONG)stub >> 16);
        jmp[2] = (UWORD)(ULONG)stub;
        n++;
    }
    return n;
}

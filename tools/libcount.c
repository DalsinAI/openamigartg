/* Copyright (c) 2026 Dalsin Limited. Open RTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT */
/* LibCount: how often each function of some libraries is called, for Open
 * RTG's phase 0 (docs/architecture/OPEN_RTG_DESIGN.md): which graphics,
 * intuition, layers and RTG calls Workbench and programs really make, so
 * openrtg.library takes over the busy ones first.
 *
 *   LibCount LIBS/M/A, SECONDS/N, TO/K
 *   LibCount graphics.library intuition.library layers.library SECONDS 30 TO RAM:counts
 *
 * Each function from -30 on gets a 12-byte stub in public memory that counts
 * and jumps on to the original: ADDQ.L #1,counter then JMP original. The
 * stubs go in with SetFunction() under Forbid() and the caches are cleared,
 * the OS-friendly way. At the end a stub comes out only if it is still the
 * vector; one that someone patched after us stays (with its memory) and goes
 * on passing calls through. Ctrl-C ends the count early. The report is one
 * line a function: library, offset, count (the host names the offsets from
 * the NDK's FD files: tools/libcount_report.py). Works on any Amiga with
 * OS 2.04 or later. */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

static const char ver[] __attribute__((used)) = "$VER: LibCount 1.0 (4.10.2026) Open RTG";

#define MAXLIBS 12
#define FIRST_LVO 30                 /* after Open, Close, Expunge and the reserved vector */

struct Counted {
    struct Library *lib;
    const char *name;
    ULONG n;                         /* functions counted: -30, -36, ... */
    UWORD *stubs;                    /* 6 words each */
    ULONG *counts;
    APTR *orig;
    ULONG bytes;
};

static void stub(UWORD *s, ULONG *count, APTR orig)
{
    s[0] = 0x52B9;                   /* ADDQ.L #1,(abs).L */
    s[1] = (UWORD)((ULONG)count >> 16); s[2] = (UWORD)(ULONG)count;
    s[3] = 0x4EF9;                   /* JMP (abs).L */
    s[4] = (UWORD)((ULONG)orig >> 16); s[5] = (UWORD)(ULONG)orig;
}

int main(void)
{
    static struct Counted c[MAXLIBS];
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"LIBS/M/A,SECONDS/N,TO/K", args, NULL);
    STRPTR *names;
    LONG seconds, i, nlibs = 0, kept = 0;
    BPTR out;
    if (!rd) { PrintFault(IoErr(), (STRPTR)"LibCount"); return RETURN_FAIL; }
    names = (STRPTR *)args[0];
    seconds = args[1] ? *(LONG *)args[1] : 30;

    for (i = 0; names[i] && nlibs < MAXLIBS; i++) {
        struct Counted *k = &c[nlibs];
        ULONG f;
        k->lib = OpenLibrary(names[i], 0);
        if (!k->lib) { Printf((STRPTR)"LibCount: %s isn't there, left out\n", (LONG)names[i]); continue; }
        k->name = (const char *)names[i];
        k->n = k->lib->lib_NegSize / 6 > FIRST_LVO / 6 ? k->lib->lib_NegSize / 6 - FIRST_LVO / 6 + 1 : 0;
        k->bytes = k->n * (12 + 4 + 4);
        k->stubs = k->bytes ? AllocMem(k->bytes, MEMF_PUBLIC | MEMF_CLEAR) : NULL;
        if (!k->stubs) { CloseLibrary(k->lib); k->lib = NULL; continue; }
        k->counts = (ULONG *)(k->stubs + 6 * k->n);
        k->orig = (APTR *)(k->counts + k->n);
        Forbid();
        for (f = 0; f < k->n; f++) {
            LONG off = -(LONG)(FIRST_LVO + 6 * f);
            UBYTE *vec = (UBYTE *)k->lib + off;
            /* a vector is JMP (abs).L; anything else is left alone, uncounted */
            k->orig[f] = *(UWORD *)vec == 0x4EF9 ? *(APTR *)(vec + 2) : NULL;
            if (k->orig[f]) stub(k->stubs + 6 * f, &k->counts[f], k->orig[f]);
        }
        CacheClearU();
        for (f = 0; f < k->n; f++)
            if (k->orig[f]) SetFunction(k->lib, -(LONG)(FIRST_LVO + 6 * f), (APTR)(k->stubs + 6 * f));
        Permit();
        Printf((STRPTR)"LibCount: counting %ld functions of %s\n", (LONG)k->n, (LONG)k->name);
        nlibs++;
    }

    for (i = 0; i < seconds * 5; i++) {                   /* Ctrl-C ends it early */
        Delay(10);
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) { SetSignal(0, SIGBREAKF_CTRL_C); break; }
    }

    out = args[2] ? Open((STRPTR)args[2], MODE_NEWFILE) : Output();
    for (i = 0; i < nlibs; i++) {
        struct Counted *k = &c[i];
        ULONG f, still = 0;
        Forbid();
        for (f = 0; f < k->n; f++) {
            APTR mine = (APTR)(k->stubs + 6 * f);
            APTR now;
            if (!k->orig[f]) continue;
            now = SetFunction(k->lib, -(LONG)(FIRST_LVO + 6 * f), k->orig[f]);
            if (now != mine) { SetFunction(k->lib, -(LONG)(FIRST_LVO + 6 * f), now); still++; }   /* patched after us: ours stays */
        }
        Permit();
        if (out)
            for (f = 0; f < k->n; f++)
                if (k->counts[f]) FPrintf(out, (STRPTR)"%s %ld %lu\n", (LONG)k->name, -(LONG)(FIRST_LVO + 6 * f), k->counts[f]);
        if (still) kept++;
        else FreeMem(k->stubs, k->bytes);
        CloseLibrary(k->lib);
    }
    if (out && args[2]) Close(out);
    if (kept) Printf((STRPTR)"LibCount: %ld librar%s had a function patched after ours; those stubs stay in memory\n", kept, (LONG)(kept == 1 ? "y" : "ies"));
    FreeArgs(rd);
    return RETURN_OK;
}

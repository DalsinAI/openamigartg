/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ModuleCheck: opengpu.library's OGPU_ModuleOpen and OGPU_ModuleClose
 * (include/opengpu/module.h) on an Amiga, with Test.module (shared, built
 * -fbaserel32) and TestEach.module (a copy for each program) beside it:
 *   - a module opens, its table works, libnix started inside it (a
 *     constructor, printf, getenv through GetVar);
 *   - a shared module's two opens share one copy of the code, each with
 *     its own data; a module for each program is two copies;
 *   - A4: a callback into the program runs with the program's A4 and the
 *     module has its own back afterwards; a thread the module starts works
 *     on the copy that started it;
 *   - a stub's own LoadSeg (an opengpu.library older than 0.5) still runs
 *     a shared module, as that program's copy;
 *   - refusals (too new, too old an ABI, missing) give IoErr's reason;
 *   - memory: what the first and second opens take, and twenty opens and
 *     closes give it all back (with the module loaded meanwhile, and not);
 *   - SDL2.module and GL.module, when LIBS:OpenGPU/ has them: the time and
 *     memory of a first, second and third open.
 * Prints one line per check and "ModuleCheck: n of m checks passed".
 *   ModuleCheck [DIR path]       (DIR: where the test modules are; default PROGDIR:)
 *   ModuleCheck HOLD secs [DIR path]
 *       a second program: holds Test.module open for secs, bumping its
 *       counter, and says where its code and data are (ENV:OGPUModuleHold),
 *       so a ModuleCheck run meanwhile checks the two programs share code.
 * Built with -ffixed-a4 (tests/modules/build.sh): calls into a module's
 * table set A4 with OGPU_A4. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/opengpu.h>

#include "test_module.h"

struct Library *OpenGPUBase;
struct Device *TimerBase;

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); } else printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } while (0)

#define PROGRAM_A4 ((APTR)0x41344134)           /* caller_a4 for the callback check */
#define HOLD_VAR "OGPUModuleHold"

static double now_ms(void)
{
    struct EClockVal e;
    ULONG hz = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + e.ev_lo) * 1000.0 / hz;
}

static ULONG avail(void)
{
    return AvailMem(MEMF_ANY);
}

/* ---- calls into a table, with its A4 ----------------------------------------------- */

static long t_add(struct TestModuleTable *t, long a, long b) { OGPU_A4(t->head.a4); return t->add(a, b); }
static long t_bump(struct TestModuleTable *t) { OGPU_A4(t->head.a4); return t->bump(); }
static int t_ctor_ran(struct TestModuleTable *t) { OGPU_A4(t->head.a4); return t->ctor_ran(); }
static const char *t_getenv(struct TestModuleTable *t, const char *n) { OGPU_A4(t->head.a4); return t->getenv(n); }
static int t_say(struct TestModuleTable *t, const char *w) { OGPU_A4(t->head.a4); return t->say(w); }
static struct Library *t_opengpu(struct TestModuleTable *t) { OGPU_A4(t->head.a4); return t->opengpu(); }
static int t_shared(struct TestModuleTable *t) { OGPU_A4(t->head.a4); return t->shared(); }
static long t_call_back(struct TestModuleTable *t, long (*fn)(long), long x) { OGPU_A4(t->head.a4); return t->call_back(fn, x); }
static long t_thread_bump(struct TestModuleTable *t) { OGPU_A4(t->head.a4); return t->thread_bump(); }
static APTR t_data(struct TestModuleTable *t) { OGPU_A4(t->head.a4); return t->data(); }
static void t_close(struct TestModuleTable *t) { OGPU_A4(t->head.a4); t->close(); }

/* The program's side of the callback: 1000 more when A4 is the program's. */
static long seen_a4;
static long callback(long x)
{
    seen_a4 = (long)ogpu_a4_get();
    return x * 2 + (ogpu_a4_get() == PROGRAM_A4 ? 1000 : 0);
}

static struct TestModuleTable *open_test(const char *path, APTR *h)
{
    struct TestModuleTable *t = NULL;
    *h = OGPU_ModuleOpen((CONST_STRPTR)path, TEST_MODULE_VERSION, (APTR *)&t);
    return *h ? t : NULL;
}

static void close_test(struct TestModuleTable *t, APTR h)
{
    if (t) t_close(t);
    if (h) OGPU_ModuleClose(h);
}

/* ---- the installed modules ------------------------------------------------------- */

struct sdl_table { struct OGPUModuleTable head; LONG (*dynapi_entry)(ULONG, void *, ULONG); void (*close)(void); };
struct gl_table { struct OGPUModuleTable head; void (*close)(void); ULONG count; const struct OGPUModuleExport *exports; };

static void module_close(const char *name, struct OGPUModuleTable *t, APTR h)
{
    OGPU_A4(t->a4);
    if (!strcmp(name, "SDL2")) ((struct sdl_table *)t)->close();
    else ((struct gl_table *)t)->close();
    OGPU_ModuleClose(h);
}

/* Three opens at once, then all closed: the time and memory each open
 * takes. Twice: the first round also loads the libraries the module opens
 * from disk (they stay in memory), so the second round's numbers are the
 * module's own, and it must give back all it took. */
static int open_three(const char *name, ULONG version, APTR *h, struct OGPUModuleTable **t, double *ms, ULONG *mem)
{
    int i;
    for (i = 0; i < 3; i++) {
        ULONG m0 = avail();
        double t0 = now_ms();
        h[i] = OGPU_ModuleOpen((CONST_STRPTR)name, version, (APTR *)&t[i]);
        ms[i] = now_ms() - t0;
        mem[i] = m0 - avail();
        if (!h[i]) break;
    }
    return i;
}

static void close_three(const char *name, APTR *h, struct OGPUModuleTable **t)
{
    int i;
    for (i = 2; i >= 0; i--)
        if (h[i]) module_close(name, t[i], h[i]);
}

static void installed(const char *name, ULONG version)
{
    char file[64];
    BPTR lock;
    APTR h[3] = { 0 };
    struct OGPUModuleTable *t[3] = { 0 };
    double ms[3], first_ms;
    ULONG before, mem[3];
    snprintf(file, sizeof file, OGPU_MODULE_DIR "%s" OGPU_MODULE_EXT, name);
    if (!(lock = Lock((CONST_STRPTR)file, ACCESS_READ))) {
        printf("--    %s isn't installed\n", file);
        return;
    }
    UnLock(lock);
    /* the first round */
    CHECK(open_three(name, version, h, t, ms, mem) == 3, "%s opens three times (IoErr %ld)", name, h[2] ? 0L : (long)IoErr());
    first_ms = ms[0];
    if (h[0] && h[1] && !strcmp(name, "SDL2")) {
        void *jt[2][16];
        LONG r0, r1;
        struct sdl_table *a = (struct sdl_table *)t[0], *b = (struct sdl_table *)t[1];
        memset(jt, 0, sizeof jt);
        { OGPU_A4(a->head.a4); r0 = a->dynapi_entry(1, jt[0], sizeof jt[0]); }
        { OGPU_A4(b->head.a4); r1 = b->dynapi_entry(1, jt[1], sizeof jt[1]); }
        CHECK(r0 == 0 && r1 == 0 && jt[0][0] && jt[0][15] && jt[0][0] == jt[1][0],
              "SDL2's dynapi fills a table, the same code for both programs");
    } else if (h[0] && h[1] && !strcmp(name, "GL")) {
        struct gl_table *a = (struct gl_table *)t[0], *b = (struct gl_table *)t[1];
        CHECK(a->count > 1000 && a->exports && strcmp(a->exports[0].name, a->exports[1].name) < 0 &&
              a->exports[0].func == b->exports[0].func && a != b,
              "GL hands out %lu calls by name, the same code for both programs", (unsigned long)a->count);
    }
    close_three(name, h, t);
    /* the second round: the module's own numbers */
    memset(h, 0, sizeof h);
    before = avail();
    if (open_three(name, version, h, t, ms, mem) == 3) {
        printf("      %s: a first open takes %.0f ms (%.0f ms the first time) and %lu KB; a second %.0f ms and %lu KB; a third %.0f ms and %lu KB\n",
               name, ms[0], first_ms, (unsigned long)mem[0] / 1024, ms[1], (unsigned long)mem[1] / 1024, ms[2], (unsigned long)mem[2] / 1024);
        CHECK(mem[1] < mem[0] / 4 && mem[2] < mem[0] / 4, "%s's code is loaded once (a second or third open takes %lu KB of the first's %lu KB)",
              name, (unsigned long)mem[1] / 1024, (unsigned long)mem[0] / 1024);
    }
    close_three(name, h, t);
    CHECK((LONG)(before - avail()) < 4096, "%s: closing them gives the memory back (%ld bytes kept)", name, (long)(before - avail()));
}

/* ---- a second program -------------------------------------------------------------- */

static int hold(const char *path, int secs)
{
    APTR h;
    struct TestModuleTable *t = open_test(path, &h);
    char v[64];
    long n = 0, got = 0;
    int i;
    if (!t) {
        printf("ModuleCheck HOLD: Test.module doesn't open (IoErr %ld)\n", (long)IoErr());
        return RETURN_FAIL;
    }
    snprintf(v, sizeof v, "%lx %lx", (unsigned long)t->add, (unsigned long)t_data(t));
    SetVar((CONST_STRPTR)HOLD_VAR, (CONST_STRPTR)v, -1, GVF_GLOBAL_ONLY);
    printf("ModuleCheck HOLD: Test.module's code at %lx, this program's counter at %lx\n",
           (unsigned long)t->add, (unsigned long)t_data(t));
    for (i = 0; i < secs * 10; i++) {
        got = t_bump(t);
        n++;
        Delay(5);
    }
    DeleteVar((CONST_STRPTR)HOLD_VAR, GVF_GLOBAL_ONLY);
    printf("ModuleCheck HOLD: %s: %ld bumps, counter %ld (the other program's bumps aren't in it)\n",
           got == n ? "ok" : "FAIL", n, got);
    close_test(t, h);
    return got == n ? RETURN_OK : RETURN_ERROR;
}

int main(int argc, char **argv)
{
    char path[256], each[256];
    const char *dir = "PROGDIR:";
    struct TestModuleTable *a = NULL, *b = NULL, *x;
    APTR ha, hb, hx;
    const char *v;
    ULONG before, m1, m2;
    struct timerequest tr;
    int i, secs = 0, rc;

    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "DIR")) dir = argv[i + 1];
        else if (!strcmp(argv[i], "HOLD")) secs = atoi(argv[i + 1]);
    }
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &tr.tr_node, 0)) return RETURN_FAIL;
    TimerBase = tr.tr_node.io_Device;
    if (!(OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, 0))) {
        printf("ModuleCheck: no opengpu.library\n");
        CloseDevice(&tr.tr_node);
        return RETURN_FAIL;
    }
    if (OpenGPUBase->lib_Version == 0 && OpenGPUBase->lib_Revision < OGPU_MODULE_LIB_REVISION) {
        printf("ModuleCheck: this opengpu.library has no OGPU_ModuleOpen\n");
        CloseLibrary(OpenGPUBase);
        CloseDevice(&tr.tr_node);
        return RETURN_FAIL;
    }
    snprintf(path, sizeof path, "%s%sTest.module", dir, dir[strlen(dir) - 1] == ':' || dir[strlen(dir) - 1] == '/' ? "" : "/");
    snprintf(each, sizeof each, "%s%sTestEach.module", dir, dir[strlen(dir) - 1] == ':' || dir[strlen(dir) - 1] == '/' ? "" : "/");
    if (secs) {
        rc = hold(path, secs);
        CloseLibrary(OpenGPUBase);
        CloseDevice(&tr.tr_node);
        return rc;
    }
    printf("ModuleCheck: opengpu.library %u.%u\n", OpenGPUBase->lib_Version, OpenGPUBase->lib_Revision);

    /* one copy, its table */
    before = avail();
    a = open_test(path, &ha);
    m1 = before - avail();
    CHECK(a && a->head.version == TEST_MODULE_VERSION, "Test.module opens (IoErr %ld)", ha ? 0L : (long)IoErr());
    if (!a) goto out;
    CHECK(t_shared(a), "it is a shared module (built -fbaserel32)");
    CHECK(t_add(a, 2, 40) == 42, "its calls work (2 + 40 = %ld)", t_add(a, 2, 40));
    CHECK(t_ctor_ran(a), "its constructor ran");
    CHECK(t_say(a, "hello") > 0, "its printf writes to this program's output");
    CHECK(t_opengpu(a) == OpenGPUBase, "it was given opengpu.library");
    SetVar((CONST_STRPTR)"OGPUModuleTest", (CONST_STRPTR)"one", -1, GVF_LOCAL_ONLY);
    v = t_getenv(a, "OGPUModuleTest");
    CHECK(v && !strcmp(v, "one"), "its getenv reads a variable (%s)", v ? v : "none");
    SetVar((CONST_STRPTR)"OGPUModuleTest", (CONST_STRPTR)"a longer value", -1, GVF_LOCAL_ONLY);
    v = t_getenv(a, "OGPUModuleTest");
    CHECK(v && !strcmp(v, "a longer value"), "and sees it change (%s)", v ? v : "none");
    DeleteVar((CONST_STRPTR)"OGPUModuleTest", GVF_LOCAL_ONLY);
    CHECK(!t_getenv(a, "OGPUModuleTest"), "and sees it go");

    /* a second copy: the same code, data of its own */
    before = avail();
    b = open_test(path, &hb);
    m2 = before - avail();
    CHECK(b && b != a, "a second open is a second copy of the data");
    if (b) {
        long a1 = t_bump(a), a2 = t_bump(a), b1 = t_bump(b);
        CHECK(a1 == 1 && a2 == 2 && b1 == 1, "each copy has its own globals (%ld %ld, %ld)", a1, a2, b1);
        CHECK(a->add == b->add && t_data(a) != t_data(b), "both run the same code (%lx), each on its own data (%lx, %lx)",
              (unsigned long)a->add, (unsigned long)t_data(a), (unsigned long)t_data(b));
        printf("      memory: the first open took %lu bytes, the second %lu (code loaded once)\n",
               (unsigned long)m1, (unsigned long)m2);
        a->head.caller_a4 = PROGRAM_A4;
        {
            long r = t_call_back(a, callback, 21);
            CHECK(r == 42 + 1000 + 2, "a callback runs with the program's A4 (%lx), and the module's own is back after (%ld)",
                  (unsigned long)seen_a4, r);
        }
        {
            long ra = t_thread_bump(a), rb = t_bump(b);
            CHECK(ra == 102 && rb == 2, "a thread the module starts works on the copy that started it (%ld, other %ld)", ra, rb);
        }
        /* the second program, when ModuleCheck HOLD runs beside this one */
        {
            char hv[64];
            unsigned long code = 0, data = 0;
            if (GetVar((CONST_STRPTR)HOLD_VAR, (STRPTR)hv, sizeof hv, GVF_GLOBAL_ONLY) > 0 &&
                sscanf(hv, "%lx %lx", &code, &data) == 2)
                CHECK(code == (unsigned long)a->add && data != (unsigned long)t_data(a) && data != (unsigned long)t_data(b),
                      "another program running Test.module now shares this code (%lx), with its data at %lx", code, data);
            else
                printf("--    no other program holds Test.module (run ModuleCheck HOLD 30 beside this one)\n");
        }
        close_test(b, hb);
    }
    close_test(a, ha);

    /* a module for each program */
    a = open_test(each, &ha);
    b = open_test(each, &hb);
    CHECK(a && b && !t_shared(a) && a->add != b->add && t_bump(a) == 1 && t_bump(b) == 1,
          "TestEach.module is a copy for each program (code at %lx and %lx)", a ? (unsigned long)a->add : 0UL,
          b ? (unsigned long)b->add : 0UL);
    if (a && b) {
        long r = t_call_back(a, callback, 21);
        CHECK(r == 42 + 1 && t_thread_bump(a) == 101, "and its callback and thread work there too (%ld)", r);
    }
    close_test(b, hb);
    close_test(a, ha);

    /* a stub's own LoadSeg, on an opengpu.library older than 0.5 */
    {
        BPTR seg = LoadSeg((CONST_STRPTR)path);
        struct OGPUModuleArgs args;
        args.SysBase = SysBase;
        args.DOSBase = (struct Library *)DOSBase;
        args.OpenGPUBase = OpenGPUBase;
        args.version = TEST_MODULE_VERSION;
        x = seg ? (struct TestModuleTable *)OGPU_MODULE_ENTRY(seg)(&args) : NULL;
        CHECK(x && t_add(x, 2, 40) == 42 && t_bump(x) == 1 && t_ctor_ran(x),
              "a stub's own LoadSeg runs the shared module, on the data it loaded");
        if (x) t_close(x);
        if (seg) UnLoadSeg(seg);
    }

    /* refusals */
    hx = OGPU_ModuleOpen((CONST_STRPTR)path, TEST_MODULE_VERSION + 1, (APTR *)&x);
    CHECK(!hx && !x && IoErr() == ERROR_OBJECT_WRONG_TYPE, "a newer version than the module is refused (IoErr %ld)", (long)IoErr());
    if (hx) OGPU_ModuleClose(hx);
    hx = OGPU_ModuleOpen((CONST_STRPTR)path, 1, (APTR *)&x);
    CHECK(!hx && IoErr() == ERROR_OBJECT_WRONG_TYPE, "a caller from before the A4 calls (version 1) is refused (IoErr %ld)", (long)IoErr());
    if (hx) OGPU_ModuleClose(hx);
    hx = OGPU_ModuleOpen((CONST_STRPTR)"PROGDIR:NoSuch.module", 1, (APTR *)&x);
    CHECK(!hx && IoErr() == ERROR_OBJECT_NOT_FOUND, "a missing module is not found (IoErr %ld)", (long)IoErr());
    hx = OGPU_ModuleOpen((CONST_STRPTR)"NoSuch", 1, (APTR *)&x);
    CHECK(!hx && IoErr() == ERROR_OBJECT_NOT_FOUND, "nor one missing from " OGPU_MODULE_DIR " (IoErr %ld)", (long)IoErr());
    OGPU_ModuleClose(NULL);
    OGPU_ModuleClose((APTR)path);               /* not a handle: ignored */

    /* memory: twenty opens and closes, loading the module each time; then
     * twenty with one copy held open, so only the data comes and goes; then
     * the module for each program */
    for (rc = 0; rc < 3; rc++) {
        const char *p = rc == 2 ? each : path;
        APTR hh = NULL;
        struct TestModuleTable *held = rc == 1 ? open_test(path, &hh) : NULL;
        before = avail();
        for (i = 0; i < 20; i++) {
            x = open_test(p, &hx);
            if (!x) break;
            t_getenv(x, "Workbench");
            t_bump(x);
            close_test(x, hx);
        }
        m1 = before - avail();
        CHECK(i == 20 && (LONG)m1 < 4096, "20 opens and closes keep memory (%ld bytes; %s)", (long)m1,
              rc == 0 ? "shared, loaded each time" : rc == 1 ? "shared, held open meanwhile" : "a copy for each program");
        close_test(held, hh);
    }

    installed("SDL2", 2);
    installed("GL", 2);
out:
    printf("ModuleCheck: %d of %d checks passed\n", checks - fails, checks);
    CloseLibrary(OpenGPUBase);
    CloseDevice(&tr.tr_node);
    return fails ? RETURN_ERROR : RETURN_OK;
}

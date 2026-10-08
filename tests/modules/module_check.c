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
 *   - residency (opengpu.library 0.9): a shared module stays loaded after
 *     its last close, and the next open runs the same code without loading
 *     it; a low-memory flush (as C:Avail FLUSH makes) leaves a module a
 *     program has open, and unloads one nobody has; a replaced module file
 *     is loaded anew while programs on the old one keep it;
 *   - memory: what the first and second opens take, and twenty opens and
 *     closes keep nothing (with another copy open meanwhile, and not);
 *   - SDL2.module and GL.module, when LIBS:OpenGPU/ has them: the time and
 *     memory of a first, second and third open, of an open after all have
 *     closed, and of the load after a flush.
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

/* What C:Avail FLUSH does: an allocation that can't succeed, so exec runs
 * its low-memory handlers, which expunge every library they can. */
static void flush(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        APTR p = AllocMem(0x7ffffff0UL, MEMF_PUBLIC);
        if (p) FreeMem(p, 0x7ffffff0UL);
    }
}

/* opengpu.library 0.9 keeps shared modules loaded after their last close. */
static int resident;

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

/* Three opens at once: the time and memory each takes. */
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

/* Where an open's code is: SDL2's first dynapi call, GL's first export. */
static APTR code_of(const char *name, struct OGPUModuleTable *t)
{
    if (!strcmp(name, "SDL2")) {
        void *jt[16];
        LONG r;
        struct sdl_table *a = (struct sdl_table *)t;
        memset(jt, 0, sizeof jt);
        { OGPU_A4(a->head.a4); r = a->dynapi_entry(1, jt, sizeof jt); }
        return r == 0 ? jt[0] : NULL;
    }
    return ((struct gl_table *)t)->count ? (APTR)((struct gl_table *)t)->exports[0].func : NULL;
}

/* One open, timed. */
static APTR open_one(const char *name, ULONG version, struct OGPUModuleTable **t, double *ms, ULONG *mem)
{
    ULONG m0 = avail();
    double t0 = now_ms();
    APTR h = OGPU_ModuleOpen((CONST_STRPTR)name, version, (APTR *)t);
    *ms = now_ms() - t0;
    *mem = m0 - avail();
    return h;
}

static void installed(const char *name, ULONG version)
{
    char file[64];
    BPTR lock;
    APTR h[3] = { 0 }, hr, hh;
    struct OGPUModuleTable *t[3] = { 0 }, *tr, *th;
    double ms[3], ms_r, ms_l;
    ULONG idle, closed, mem[3], mem_r, mem_l;
    APTR code = NULL;
    snprintf(file, sizeof file, OGPU_MODULE_DIR "%s" OGPU_MODULE_EXT, name);
    if (!(lock = Lock((CONST_STRPTR)file, ACCESS_READ))) {
        printf("--    %s isn't installed\n", file);
        return;
    }
    UnLock(lock);
    /* From nothing loaded: three programs' opens at once. */
    flush();
    idle = avail();
    CHECK(open_three(name, version, h, t, ms, mem) == 3, "%s opens three times (IoErr %ld)", name, h[2] ? 0L : (long)IoErr());
    if (h[0] && h[1] && h[2]) {
        printf("      %s: a first open takes %.0f ms and %lu KB; a second %.0f ms and %lu KB; a third %.0f ms and %lu KB\n",
               name, ms[0], (unsigned long)mem[0] / 1024, ms[1], (unsigned long)mem[1] / 1024, ms[2], (unsigned long)mem[2] / 1024);
        code = code_of(name, t[0]);
        CHECK(code && code == code_of(name, t[1]) && code == code_of(name, t[2]) && mem[1] < mem[0] / 4 && mem[2] < mem[0] / 4,
              "%s's code is loaded once, for all three (%lx)", name, (unsigned long)code);
    }
    close_three(name, h, t);
    closed = avail();
    if (!code) return;
    if (!resident) {
        /* An opengpu.library older than 0.9: the last close unloads it. */
        hr = open_one(name, version, &tr, &ms_r, &mem_r);
        printf("      %s: all three closed, %lu KB kept; the next program's open takes %.0f ms and %lu KB (it loads again)\n",
               name, (unsigned long)(idle - closed) / 1024, ms_r, (unsigned long)mem_r / 1024);
        if (hr) module_close(name, tr, hr);
        return;
    }
    /* All closed: the code stays, and the next program doesn't load it. */
    CHECK(idle - closed > mem[0] / 2, "%s stays loaded when all three have closed (%lu KB kept)", name, (unsigned long)(idle - closed) / 1024);
    hr = open_one(name, version, &tr, &ms_r, &mem_r);
    CHECK(hr && code_of(name, tr) == code && mem_r < mem[0] / 4,
          "%s: the next program's open runs the loaded code: %.0f ms and %lu KB (the first took %.0f ms)",
          name, ms_r, (unsigned long)mem_r / 1024, ms[0]);
    if (hr) module_close(name, tr, hr);
    CHECK((LONG)(closed - avail()) < 4096 && (LONG)(avail() - closed) < 4096, "%s: an open and close after that keeps no memory (%ld bytes)",
          name, (long)(closed - avail()));
    /* A low-memory flush: nothing goes while a program has it open ... */
    hh = OGPU_ModuleOpen((CONST_STRPTR)name, version, (APTR *)&th);
    flush();
    hr = hh ? open_one(name, version, &tr, &ms_r, &mem_r) : NULL;
    CHECK(hh && hr && code_of(name, th) == code && code_of(name, tr) == code && mem_r < mem[0] / 4,
          "%s: a low-memory flush leaves it while a program has it open (another open: %.0f ms, %lu KB)",
          name, ms_r, (unsigned long)mem_r / 1024);
    if (hr) module_close(name, tr, hr);
    if (hh) module_close(name, th, hh);
    /* ... and it unloads it once nobody has. */
    closed = avail();
    flush();
    printf("      %s: the flush gave back %lu KB\n", name, (unsigned long)(avail() - closed) / 1024);
    hr = open_one(name, version, &tr, &ms_l, &mem_l);
    CHECK(hr && mem_l > mem[0] / 2, "%s: after a low-memory flush with no program on it, the next open loads it again: %.0f ms and %lu KB",
          name, ms_l, (unsigned long)mem_l / 1024);
    if (hr) module_close(name, tr, hr);
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

/* A module file replaced while it is loaded (an installer's update): the
 * next open loads the new file, and a program on the old one keeps it. */
static void replaced(const char *path)
{
    static const char tmp[] = "T:OGPUReplaced.module";
    char buf[4096];
    FILE *in = fopen(path, "rb"), *out = fopen(tmp, "wb");
    size_t n;
    struct DateStamp ds;
    struct TestModuleTable *a, *b, *c;
    APTR ha, hb, hc;
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        printf("--    no copy of Test.module in T: (the replaced-file check is skipped)\n");
        return;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    a = open_test(tmp, &ha);
    DateStamp(&ds);
    ds.ds_Days++;                               /* the new file: another date */
    SetFileDate((CONST_STRPTR)tmp, &ds);
    b = open_test(tmp, &hb);
    CHECK(a && b && a->add != b->add && t_add(a, 2, 40) == 42 && t_add(b, 2, 40) == 42,
          "a replaced module file is loaded anew (code at %lx), and a program on the old one keeps it (%lx)",
          b ? (unsigned long)b->add : 0UL, a ? (unsigned long)a->add : 0UL);
    close_test(a, ha);
    c = open_test(tmp, &hc);
    CHECK(c && b && c->add == b->add, "and the next open runs the new one");
    close_test(c, hc);
    close_test(b, hb);
    flush();
    DeleteFile((CONST_STRPTR)tmp);              /* the check's own copy */
}

int main(int argc, char **argv)
{
    char path[256], each[256];
    const char *dir = "PROGDIR:";
    struct TestModuleTable *a = NULL, *b = NULL, *x;
    APTR ha, hb, hx;
    const char *v;
    ULONG before, m1, m2, data_m;
    APTR code;
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
    resident = OpenGPUBase->lib_Version > 0 || OpenGPUBase->lib_Revision >= 9;

    /* one copy, its table (from nothing loaded) */
    flush();
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
    code = (APTR)a->add;
    data_m = b ? m2 : m1;                       /* what an open takes besides the code */
    close_test(a, ha);

    /* residency: kept after the last close, until memory is short */
    if (resident) {
        before = avail();
        x = open_test(path, &hx);
        m2 = before - avail();
        CHECK(x && (APTR)x->add == code && m2 <= data_m + 4096,
              "Test.module stays loaded after its last close: the next open runs the same code (%lx) and takes %lu bytes",
              x ? (unsigned long)x->add : 0UL, (unsigned long)m2);
        flush();
        CHECK(x && (APTR)x->add == code && t_add(x, 2, 40) == 42 && t_bump(x) == 1,
              "a low-memory flush leaves a module a program has open");
        close_test(x, hx);
        flush();
        before = avail();
        x = open_test(path, &hx);
        m2 = before - avail();
        CHECK(x && m2 >= data_m + 16384, "after a low-memory flush with no program on it, the next open loads it again (%lu bytes, %lu more)",
              (unsigned long)m2, (unsigned long)(m2 - data_m));
        close_test(x, hx);
        replaced(path);
    }

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
              rc == 0 ? (resident ? "shared, kept loaded" : "shared, loaded each time") : rc == 1 ? "shared, held open meanwhile" : "a copy for each program");
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

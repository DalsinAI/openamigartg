/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ModuleCheck: opengpu.library's OGPU_ModuleOpen and OGPU_ModuleClose
 * (include/opengpu/module.h) on an Amiga, with Test.module beside it:
 *   - a module opens, its table works, libnix started inside it (a
 *     constructor, printf, getenv through GetVar);
 *   - two opens are two copies, each with its own globals;
 *   - a module that refuses (too old) and a missing one fail with IoErr's
 *     reason;
 *   - opening and closing gives all memory back;
 *   - SDL2.module and GL.module, when LIBS:OpenGPU/ has them, open and close
 *     (with the time each open takes).
 * Prints one line per check and "ModuleCheck: n of m checks passed".
 *   ModuleCheck [DIR path]      (DIR: where Test.module is; default PROGDIR:) */
#include <stdio.h>
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

static double now_ms(void)
{
    struct EClockVal e;
    ULONG hz = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + e.ev_lo) * 1000.0 / hz;
}

/* An installed module: open, its table's version, the time, close. */
struct sdl_table { ULONG version; LONG (*dynapi_entry)(ULONG, void *, ULONG); void (*close)(void); };
struct gl_table { ULONG version; void (*close)(void); ULONG count; const struct OGPUModuleExport *exports; };

static void installed(const char *name)
{
    char file[64];
    BPTR lock;
    APTR h, t = NULL;
    double t0;
    snprintf(file, sizeof file, OGPU_MODULE_DIR "%s" OGPU_MODULE_EXT, name);
    if (!(lock = Lock((CONST_STRPTR)file, ACCESS_READ))) {
        printf("--    %s isn't installed\n", file);
        return;
    }
    UnLock(lock);
    t0 = now_ms();
    h = OGPU_ModuleOpen((CONST_STRPTR)name, 1, &t);
    CHECK(h && t, "%s opens (%.0f ms, IoErr %ld)", name, now_ms() - t0, h ? 0L : (long)IoErr());
    if (!h) return;
    if (!strcmp(name, "SDL2")) {
        struct sdl_table *s = t;
        void *jt[16];
        memset(jt, 0, sizeof jt);
        CHECK(s->dynapi_entry(1, jt, sizeof jt) == 0 && jt[0] && jt[15], "SDL2's dynapi fills a table");
        s->close();
    } else if (!strcmp(name, "GL")) {
        struct gl_table *g = t;
        CHECK(g->count > 1000 && g->exports && strcmp(g->exports[0].name, g->exports[1].name) < 0,
              "GL hands out %lu calls by name", (unsigned long)g->count);
        g->close();
    }
    OGPU_ModuleClose(h);
}

int main(int argc, char **argv)
{
    char path[256];
    const char *dir = "PROGDIR:";
    struct TestModuleTable *a = NULL, *b = NULL;
    APTR ha, hb, hx;
    const char *v;
    ULONG before, after;
    struct timerequest tr;
    int i;

    if (argc == 3 && !strcmp(argv[1], "DIR")) dir = argv[2];
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &tr.tr_node, 0)) return RETURN_FAIL;
    TimerBase = tr.tr_node.io_Device;
    if (!(OpenGPUBase = OpenLibrary((CONST_STRPTR)OPENGPU_NAME, 0))) {
        printf("ModuleCheck: no opengpu.library\n");
        CloseDevice(&tr.tr_node);
        return RETURN_FAIL;
    }
    printf("ModuleCheck: opengpu.library %u.%u\n", OpenGPUBase->lib_Version, OpenGPUBase->lib_Revision);
    if (OpenGPUBase->lib_Version == 0 && OpenGPUBase->lib_Revision < OGPU_MODULE_LIB_REVISION) {
        printf("ModuleCheck: this opengpu.library has no OGPU_ModuleOpen\n");
        CloseLibrary(OpenGPUBase);
        CloseDevice(&tr.tr_node);
        return RETURN_FAIL;
    }
    snprintf(path, sizeof path, "%s%sTest.module", dir, dir[strlen(dir) - 1] == ':' || dir[strlen(dir) - 1] == '/' ? "" : "/");

    /* one copy, its table */
    ha = OGPU_ModuleOpen((CONST_STRPTR)path, TEST_MODULE_VERSION, (APTR *)&a);
    CHECK(ha && a && a->head.version == TEST_MODULE_VERSION, "Test.module opens (IoErr %ld)", ha ? 0L : (long)IoErr());
    if (!ha) goto out;
    CHECK(a->add(2, 40) == 42, "its calls work (2 + 40 = %ld)", a->add(2, 40));
    CHECK(a->ctor_ran(), "its constructor ran");
    CHECK(a->say("hello") > 0, "its printf writes to this program's output");
    CHECK(a->opengpu() == OpenGPUBase, "it was given opengpu.library");
    SetVar((CONST_STRPTR)"OGPUModuleTest", (CONST_STRPTR)"one", -1, GVF_LOCAL_ONLY);
    v = a->getenv("OGPUModuleTest");
    CHECK(v && !strcmp(v, "one"), "its getenv reads a variable (%s)", v ? v : "none");
    SetVar((CONST_STRPTR)"OGPUModuleTest", (CONST_STRPTR)"a longer value", -1, GVF_LOCAL_ONLY);
    v = a->getenv("OGPUModuleTest");
    CHECK(v && !strcmp(v, "a longer value"), "and sees it change (%s)", v ? v : "none");
    DeleteVar((CONST_STRPTR)"OGPUModuleTest", GVF_LOCAL_ONLY);
    CHECK(!a->getenv("OGPUModuleTest"), "and sees it go");

    /* a second copy: globals of its own */
    hb = OGPU_ModuleOpen((CONST_STRPTR)path, TEST_MODULE_VERSION, (APTR *)&b);
    CHECK(hb && b && b != a, "a second open is a second copy");
    if (hb) {
        long a1 = a->bump(), a2 = a->bump(), b1 = b->bump();
        CHECK(a1 == 1 && a2 == 2 && b1 == 1, "each copy has its own globals (%ld %ld, %ld)", a1, a2, b1);
        b->close();
        OGPU_ModuleClose(hb);
    }
    a->close();
    OGPU_ModuleClose(ha);

    /* refusals */
    hx = OGPU_ModuleOpen((CONST_STRPTR)path, TEST_MODULE_VERSION + 1, (APTR *)&a);
    CHECK(!hx && !a && IoErr() == ERROR_OBJECT_WRONG_TYPE, "a newer version than the module is refused (IoErr %ld)", (long)IoErr());
    if (hx) OGPU_ModuleClose(hx);
    hx = OGPU_ModuleOpen((CONST_STRPTR)"PROGDIR:NoSuch.module", 1, (APTR *)&a);
    CHECK(!hx && IoErr() == ERROR_OBJECT_NOT_FOUND, "a missing module is not found (IoErr %ld)", (long)IoErr());
    hx = OGPU_ModuleOpen((CONST_STRPTR)"NoSuch", 1, (APTR *)&a);
    CHECK(!hx && IoErr() == ERROR_OBJECT_NOT_FOUND, "nor one missing from " OGPU_MODULE_DIR " (IoErr %ld)", (long)IoErr());
    OGPU_ModuleClose(NULL);
    OGPU_ModuleClose((APTR)path);               /* not a handle: ignored */

    /* memory: twenty opens and closes */
    before = AvailMem(MEMF_ANY);
    for (i = 0; i < 20; i++) {
        hx = OGPU_ModuleOpen((CONST_STRPTR)path, TEST_MODULE_VERSION, (APTR *)&a);
        if (!hx) break;
        a->getenv("Workbench");
        a->close();
        OGPU_ModuleClose(hx);
    }
    after = AvailMem(MEMF_ANY);
    CHECK(i == 20 && (LONG)(before - after) < 4096, "20 opens and closes keep memory (%ld bytes)", (long)(before - after));

    installed("SDL2");
    installed("GL");
out:
    printf("ModuleCheck: %d of %d checks passed\n", checks - fails, checks);
    CloseLibrary(OpenGPUBase);
    CloseDevice(&tr.tr_node);
    return fails ? RETURN_ERROR : RETURN_OK;
}

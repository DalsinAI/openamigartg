/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ACVirgl's host side against virglrenderer: request blocks built here, in
 * the Amiga's byte order, in an arena that stands for Amiga memory. Hello,
 * the caps (turned into the Amiga's order), a context, a texture, pixels
 * up and back with their units turned round, and a command stream (a
 * surface, a framebuffer, a clear) whose colour comes back as the 68k
 * reads it: A, R, G, B. Skipped when ACVIRGL_LIB isn't set.
 *   tests/run.sh builds it with virglrenderer's protocol header (fetched). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../host/virgl/acvirgl.h"
#include "../include/opengpu/stream.h"
#include "../include/opengpu/virgl.h"
#include "virgl_protocol.h"     /* virglrenderer's, from its pinned source */

#define ARENA (1u << 20)
static uint8_t arena[ARENA];
static uint8_t *map(void *u, uint32_t a, uint32_t len) { (void)u; return a < ARENA && len <= ARENA - a ? arena + a : NULL; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* A block at BLK, built word by word. */
#define BLK 0x1000u
static uint32_t at;
static void be(uint32_t a, uint32_t v) { arena[a] = (uint8_t)(v >> 24); arena[a + 1] = (uint8_t)(v >> 16); arena[a + 2] = (uint8_t)(v >> 8); arena[a + 3] = (uint8_t)v; }
static uint32_t rbe(uint32_t a) { return (uint32_t)arena[a] << 24 | (uint32_t)arena[a + 1] << 16 | (uint32_t)arena[a + 2] << 8 | arena[a + 3]; }
static void begin(void) { at = BLK; }
static uint32_t cmd(unsigned c, int nargs, const uint32_t *args)
{
    uint32_t here = at;
    int i;
    be(at, ACV_HDR(c, nargs + 2)); be(at + 4, 0xDEADBEEFu);
    for (i = 0; i < nargs; i++) be(at + 8 + i * 4, args[i]);
    at += (uint32_t)(nargs + 2) * 4;
    return here;               /* where its result word is, less 4 */
}
static int32_t result(uint32_t c) { return (int32_t)rbe(c + 4); }

/* A command stream in little-endian words, as Mesa's encoder writes it. */
#define CMDS 0x8000u
static uint32_t cn;
static void le(uint32_t v) { uint32_t a = CMDS + cn * 4; arena[a] = (uint8_t)v; arena[a + 1] = (uint8_t)(v >> 8); arena[a + 2] = (uint8_t)(v >> 16); arena[a + 3] = (uint8_t)(v >> 24); cn++; }
static uint32_t f32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    char err[256];
    struct acvirgl *v = acvirgl_create(NULL, err, sizeof err);
    uint32_t c, i;
    if (!v) { printf("acvirgl: skipped (%s)\n", err); return 0; }

    begin();
    uint32_t a_hello[1] = { 0 }, a_info[3] = { 2, 0, 0 }, a_caps[4] = { 2, 2, 0x20000, 0x8000 };
    uint32_t ch = cmd(ACV_HELLO, 1, a_hello), ci = cmd(ACV_CAPSET_INFO, 3, a_info), cc = cmd(ACV_CAPS, 4, a_caps);
    memcpy(arena + 0x30000, "acvirgl test", 12);
    uint32_t a_ctx[3] = { 1, 0x30000, 12 };
    uint32_t cx = cmd(ACV_CTX_CREATE, 3, a_ctx);
    /* a 64x64 B8G8R8A8 render target and texture, attached to context 1 */
    uint32_t a_res[12] = { 1, 2 /* PIPE_TEXTURE_2D */, 1 /* B8G8R8A8_UNORM */, 2 | 8, 64, 64, 1, 1, 0, 0, 0, 1 };
    uint32_t cr = cmd(ACV_RES_CREATE, 12, a_res);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(ch) == ACV_PROTOCOL, "hello: %d", result(ch));
    CHECK(rbe(ci + 12) >= 1 && rbe(ci + 16) > 0, "capset 2: version %u, %u bytes", rbe(ci + 12), rbe(ci + 16));
    CHECK(result(cc) > 0 && rbe(0x20000) >= 1, "caps: %d bytes, max_version %u (the first word, in the Amiga's order)", result(cc), rbe(0x20000));
    CHECK(result(cx) == 0 && result(cr) == 0, "context %d, resource %d", result(cx), result(cr));
    printf("acvirgl: capset 2 version %u, %u bytes; caps v1 max_version %u\n", rbe(ci + 12), rbe(ci + 16), rbe(0x20000));

    /* pixels up, turned round in 32-bit units, and back the same way */
    for (i = 0; i < 64 * 64 * 4; i++) arena[0x40000 + i] = (uint8_t)(i * 7 + (i >> 8));
    begin();
    uint32_t a_put[14] = { 1, 1, 0, 256, 0, 0, 0, 0, 64, 64, 1, 0x40000, 64 * 64 * 4, ACV_SWAP_32 };
    uint32_t a_get[14] = { 1, 1, 0, 256, 0, 0, 0, 0, 64, 64, 1, 0x50000, 64 * 64 * 4, ACV_SWAP_32 };
    uint32_t cp = cmd(ACV_PUT, 14, a_put), cg = cmd(ACV_GET, 14, a_get);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(cp) == 0 && result(cg) == 0 && !memcmp(arena + 0x40000, arena + 0x50000, 64 * 64 * 4), "pixels up and back (%d, %d)", result(cp), result(cg));
    /* up as they are, back turned round: each pixel's bytes reversed */
    a_put[13] = ACV_SWAP_NONE;
    begin(); cp = cmd(ACV_PUT, 14, a_put); cg = cmd(ACV_GET, 14, a_get);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(arena[0x50000] == arena[0x40003] && arena[0x50003] == arena[0x40000], "a 32-bit unit turned round once");

    /* a surface on the texture, the framebuffer, a clear to 0.25, 0.5, 0.75, 1 */
    cn = 0;
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_SUB_CTX, 0, 1)); le(1);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_SUB_CTX, 0, 1)); le(1);
    le(VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, VIRGL_OBJ_SURFACE_SIZE)); le(10); le(1); le(1); le(0); le(0);
    le(VIRGL_CMD0(VIRGL_CCMD_SET_FRAMEBUFFER_STATE, 0, VIRGL_SET_FRAMEBUFFER_STATE_SIZE(1))); le(1); le(0); le(10);
    le(VIRGL_CMD0(VIRGL_CCMD_CLEAR, 0, VIRGL_OBJ_CLEAR_SIZE)); le(1 << 2);
    le(f32(0.25f)); le(f32(0.5f)); le(f32(0.75f)); le(f32(1.0f));
    { double d = 1.0; uint32_t w2[2]; memcpy(w2, &d, 8); le(w2[0]); le(w2[1]); }
    le(0);
    begin();
    uint32_t a_sub[3] = { 1, CMDS, cn };
    uint32_t cs = cmd(ACV_SUBMIT, 3, a_sub);
    cg = cmd(ACV_GET, 14, a_get);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    c = 0x50000 + (10 * 64 + 10) * 4;
    printf("acvirgl: the cleared pixel reads %02x %02x %02x %02x\n", arena[c], arena[c + 1], arena[c + 2], arena[c + 3]);
    CHECK(result(cs) == 0 && result(cg) == 0, "submit %d, get %d", result(cs), result(cg));
    CHECK(arena[c] == 0xFF && arena[c + 1] >= 0x3F && arena[c + 1] <= 0x40 && arena[c + 2] >= 0x7F && arena[c + 2] <= 0x80
          && arena[c + 3] >= 0xBF && arena[c + 3] <= 0xC0, "the clear comes back as A, R, G, B");

    /* a wrong length: BADCMD, and nothing after it runs */
    begin();
    be(at, ACV_HDR(ACV_RES_UNREF, 2)); be(at + 4, 0); at += 8;
    cg = cmd(ACV_HELLO, 1, a_hello);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(BLK) == ACV_ERR_BADCMD && rbe(cg + 4) == 0xDEADBEEFu, "a short command stops the block");

    {
        struct acvirgl_stats s;
        acvirgl_stats(v, &s);
        printf("acvirgl: %llu blocks, %llu submits, %llu puts, %llu gets, %llu errors\n", (unsigned long long)s.blocks,
               (unsigned long long)s.submits, (unsigned long long)s.puts, (unsigned long long)s.gets, (unsigned long long)s.errors);
    }
    acvirgl_destroy(v);
    printf("acvirgl: %d of %d checks passed\n", checks - fails, checks);
    return fails != 0;
}

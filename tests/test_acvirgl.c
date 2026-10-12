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
    CHECK(rbe(ch + 8) & ACV_HOST_PRESENT, "hello says the host presents (flags %08x)", rbe(ch + 8));
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


    /* ACV_PRESENT: a known picture into the resource (bytes as they are,
     * so B, G, R, A as B8G8R8A8 keeps them), then straight into "video RAM". */
    for (i = 0; i < 64 * 64 * 4; i++) arena[0x40000 + i] = (uint8_t)(i * 13 + (i >> 9) + 5);
    a_put[13] = ACV_SWAP_NONE;
    begin(); cp = cmd(ACV_PUT, 14, a_put);
    acvirgl_run(v, map, NULL, BLK, at - BLK);
    CHECK(result(cp) == 0, "the picture went up (%d)", result(cp));
    {
        /* a 100x80 XRGB32 target at 0x60000, the resource at (10, 5) in it;
         * rectangles: one inside, one that runs off the resource and the
         * target, one wholly outside the resource */
        enum { TW = 100, TH = 80, TB = TW * 4, T = 0x60000, T16 = 0x70000 };
        uint32_t pa[16] = { 1, 1, 0, (10u << 16) | 5u, T, TB, OGPU_FMT_XRGB32, ((uint32_t)TW << 16) | TH, 0, 3,
                            (12u << 16) | 7u, (20u << 16) | 10u,
                            (60u << 16) | 60u, (40u << 16) | 40u,
                            (80u << 16) | 0u, (20u << 16) | 4u };
        uint32_t x, y, ok = 1, outside_ok = 1, pixels;
        memset(arena + T, 0xEE, TB * TH);
        begin(); c = cmd(ACV_PRESENT, 16, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        /* 20x10 + (74-60)x(69-60) = 200 + 126 */
        pixels = 20 * 10 + 14 * 9;
        CHECK(result(c) == (int32_t)pixels, "present wrote %d pixels, want %u", result(c), pixels);
        for (y = 0; y < TH; y++)
            for (x = 0; x < TW; x++) {
                const uint8_t *d = arena + T + y * TB + x * 4;
                int in = (x >= 12 && x < 32 && y >= 7 && y < 17) || (x >= 60 && x < 74 && y >= 60 && y < 69);
                if (in) {
                    const uint8_t *p = arena + 0x40000 + ((y - 5) * 64 + (x - 10)) * 4;
                    if (d[0] != 0 || d[1] != p[2] || d[2] != p[1] || d[3] != p[0]) ok = 0;
                } else if (d[0] != 0xEE || d[1] != 0xEE || d[2] != 0xEE || d[3] != 0xEE) outside_ok = 0;
            }
        CHECK(ok, "present: X 0, R, G, B from the resource");
        CHECK(outside_ok, "present: nothing outside the rectangles changed");

        /* the same into RGB565, flipped */
        pa[4] = T16; pa[5] = TW * 2; pa[6] = OGPU_FMT_RGB565; pa[8] = ACV_PRESENT_FLIP_Y; pa[9] = 1;
        memset(arena + T16, 0, TW * 2 * TH);
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        ok = result(c) == 200;
        for (y = 7; y < 17; y++)
            for (x = 12; x < 32; x++) {
                const uint8_t *d = arena + T16 + y * TW * 2 + x * 2;
                const uint8_t *p = arena + 0x40000 + ((63 - (y - 5)) * 64 + (x - 10)) * 4;
                if (d[0] != ((p[2] & 0xF8) | (p[1] >> 5)) || d[1] != (((p[1] & 0x1C) << 3) | (p[0] >> 3))) ok = 0;
            }
        CHECK(ok, "present: RGB565, flipped (%d)", result(c));

        /* refusals: out of reach, an unknown resource, a bad format, bad flags */
        pa[4] = ARENA - 16; pa[5] = TB; pa[6] = OGPU_FMT_XRGB32; pa[8] = 0;
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        pa[4] = T; pa[1] = 99; x = cmd(ACV_PRESENT, 12, pa);
        pa[1] = 1; pa[6] = OGPU_FMT_CLUT8; y = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == ACV_ERR_NOMAP && result(x) == ACV_ERR_RENDERER && result(y) == ACV_ERR_BADCMD,
              "present refusals: %d %d %d", result(c), result(x), result(y));
        pa[6] = OGPU_FMT_XRGB32; pa[8] = 2;
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == ACV_ERR_BADCMD, "present: unknown flags (%d)", result(c));
        /* a count longer than the command */
        pa[8] = 0; pa[9] = 3;
        begin(); c = cmd(ACV_PRESENT, 12, pa);
        acvirgl_run(v, map, NULL, BLK, at - BLK);
        CHECK(result(c) == ACV_ERR_BADCMD, "present: a short rectangle list (%d)", result(c));
    }

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
        printf("acvirgl: %llu presents, %llu rectangles, %llu pixels; %llu frame bytes read back\n", (unsigned long long)s.presents,
               (unsigned long long)s.present_rects, (unsigned long long)s.present_pixels, (unsigned long long)s.frame_get_bytes);
        CHECK(s.presents == 2 && s.present_rects == 3 && s.present_pixels == 526 && s.frame_get_bytes == 0, "present counters");
    }
    acvirgl_destroy(v);
    printf("acvirgl: %d of %d checks passed\n", checks - fails, checks);
    return fails != 0;
}

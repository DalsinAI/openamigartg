/* SPDX-License-Identifier: MIT */
/* OpenGfx's leaves (library/ogfx/ogfx_leaves.c) against a reference
 * that works a pixel at a time: random rectangles on random bitmaps,
 * planar (1 to 8 planes, masks, the 0 and -1 planes, set and invert) and
 * chunky (1 to 4 bytes a pixel, store and XOR), copies within one bitmap in
 * every direction and between two, and blocks that must draw nothing. The
 * leaves run through their tags' entries, as callers call them. Built for
 * m68k (the leaves are 32-bit Amiga code: addresses are longs) and run
 * under qemu-m68k by test_ogfx_leaves.sh. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../library/ogfx/ogfx_leaves.h"

static uint32_t rng = 2463534242u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

#define W 512                       /* pixels across the biggest bitmap */
#define H 64
static uint8_t planes_a[8][W / 8 * H + 8], planes_b[8][W / 8 * H + 8];
static uint8_t planes_c[8][W / 8 * H + 8], planes_d[8][W / 8 * H + 8];
static uint8_t chunky_a[W * 4 * H + 64], chunky_b[W * 4 * H + 64];
static uint8_t blit_bits[8][W * H];
static uint32_t ptrs[8], src_ptrs[8], dst_ptrs[8];

static void ref_planar(uint8_t (*pl)[W / 8 * H + 8], const struct ogfx_planar_rect *r, const uint32_t *ptr, uint32_t *n)
{
    *n = 0;
    if (r->depth > 8 || r->x0 < 0 || r->y0 < 0 || r->x1 < r->x0 || r->y1 < r->y0) return;
    for (uint32_t p = 0; p < r->depth; p++) {
        if (!(r->mask >> p & 1) || ptr[p] == 0 || ptr[p] == 0xffffffffu) continue;
        (*n)++;
        for (int32_t y = r->y0; y <= r->y1; y++)
            for (int32_t x = r->x0; x <= r->x1; x++) {
                uint8_t *b = &pl[p][(uint32_t)y * r->bytes_per_row + (uint32_t)x / 8], bit = (uint8_t)(0x80 >> (x & 7));
                if (r->mode & 1) *b ^= bit; else if (r->pen >> p & 1) *b |= bit; else *b &= (uint8_t)~bit;
            }
    }
}
static uint32_t ref_planar_blit(uint8_t (*src)[W / 8 * H + 8],
                                uint8_t (*dst)[W / 8 * H + 8],
                                const struct ogfx_planar_blit *r,
                                const uint32_t *sptr, const uint32_t *dptr)
{
    uint32_t planes = 0;
    if (!r->depth || r->depth > 8 || r->sx < 0 || r->sy < 0 ||
        r->dx < 0 || r->dy < 0 || r->width <= 0 || r->height <= 0 ||
        !r->mask)
        return 0;

    for (uint32_t p = 0; p < r->depth; ++p) {
        if (!(r->mask >> p & 1u) || dptr[p] == 0 || dptr[p] == 0xffffffffu)
            continue;
        planes++;
        for (int32_t y = 0; y < r->height; ++y)
            for (int32_t x = 0; x < r->width; ++x) {
                uint32_t sx = (uint32_t)(r->sx + x);
                uint32_t sbit;
                if (sptr[p] == 0) sbit = 0;
                else if (sptr[p] == 0xffffffffu) sbit = 1;
                else {
                    uint8_t b = src[p][(uint32_t)(r->sy + y) * r->src_bytes_per_row + (sx >> 3)];
                    sbit = (b >> (7 - (sx & 7))) & 1u;
                }
                blit_bits[p][(uint32_t)y * (uint32_t)r->width + (uint32_t)x] = (uint8_t)sbit;
            }
    }

    for (uint32_t p = 0; p < r->depth; ++p) {
        if (!(r->mask >> p & 1u) || dptr[p] == 0 || dptr[p] == 0xffffffffu)
            continue;
        for (int32_t y = 0; y < r->height; ++y)
            for (int32_t x = 0; x < r->width; ++x) {
                uint32_t dx = (uint32_t)(r->dx + x);
                uint8_t *db = &dst[p][(uint32_t)(r->dy + y) * r->dst_bytes_per_row + (dx >> 3)];
                uint8_t dm = (uint8_t)(0x80u >> (dx & 7u));
                uint32_t sbit = blit_bits[p][(uint32_t)y * (uint32_t)r->width + (uint32_t)x];
                uint32_t dbit = (*db & dm) != 0;
                uint32_t bit = 4u + (sbit ? 2u : 0u) + (dbit ? 1u : 0u);
                if ((r->minterm >> bit) & 1u) *db |= dm;
                else *db &= (uint8_t)~dm;
            }
    }
    return planes;
}

static uint32_t ref_chunky(uint8_t *base, const struct ogfx_chunky_rect *r)
{
    uint32_t bpp = r->bytes_per_pixel;
    if (bpp < 1 || bpp > 4 || r->x0 < 0 || r->y0 < 0 || r->x1 < r->x0 || r->y1 < r->y0) return 0;
    for (int32_t y = r->y0; y <= r->y1; y++)
        for (int32_t x = r->x0; x <= r->x1; x++)
            for (uint32_t k = 0; k < bpp; k++) {
                uint8_t *q = base + (uint32_t)y * r->bytes_per_row + (uint32_t)x * bpp + k, v = (uint8_t)(r->color >> (8 * (bpp - 1 - k)));
                if (r->mode & 1) *q ^= v; else *q = v;
            }
    return (uint32_t)(r->x1 - r->x0 + 1) * (uint32_t)(r->y1 - r->y0 + 1);
}
/* a copy as if through a buffer: right for one bitmap and one stride, which is what the cases use */
static uint32_t ref_copy(uint8_t *mem0, const struct ogfx_chunky_copy *r, uint32_t base_addr)
{
    static uint8_t buf[W * 4 * H];
    uint32_t bpp = r->bytes_per_pixel;
    if (bpp < 1 || bpp > 4 || r->sx < 0 || r->sy < 0 || r->dx < 0 || r->dy < 0 || r->width <= 0 || r->height <= 0) return 0;
    uint32_t rb = (uint32_t)r->width * bpp;
    for (int32_t y = 0; y < r->height; y++)
        memcpy(buf + (uint32_t)y * rb, mem0 + (r->src - base_addr) + (uint32_t)(r->sy + y) * r->src_bytes_per_row + (uint32_t)r->sx * bpp, rb);
    for (int32_t y = 0; y < r->height; y++)
        memcpy(mem0 + (r->dst - base_addr) + (uint32_t)(r->dy + y) * r->dst_bytes_per_row + (uint32_t)r->dx * bpp, buf + (uint32_t)y * rb, rb);
    return (uint32_t)r->width * (uint32_t)r->height;
}

/* ogfx_scale_copy: a 64 x 16 source scaled into a 256 x 64 destination */
#define SW 64
#define SH 16
#define DW 256
#define DH 64
static uint8_t sc_src[SW * 4 * SH], sc_a[DW * 4 * DH], sc_b[DW * 4 * DH];
static uint8_t sp_src[8][SW / 8 * SH], sp_a[8][DW / 8 * DH], sp_b[8][DW / 8 * DH];
static uint32_t sp_sptr[8], sp_aptr[8];

static uint32_t ref_scale(const struct ogfx_scale_copy *r, const uint32_t *sptr)
{
    uint32_t n = 0, f = r->factor, bpp = r->bytes_per_pixel;
    for (int32_t y = 0; y < DH; y++)
        for (int32_t x = 0; x < DW; x++) {
            if (x < r->dx || y < r->dy || x >= r->dx + r->width * (int32_t)f || y >= r->dy + r->height * (int32_t)f) continue;
            if (x < r->cx0 || y < r->cy0 || x > r->cx1 || y > r->cy1) continue;
            int32_t sx = r->sx + (x - r->dx) / (int32_t)f, sy = r->sy + (y - r->dy) / (int32_t)f;
            n++;
            if (bpp)
                memcpy(&sc_b[(uint32_t)y * r->dst_bytes_per_row + (uint32_t)x * bpp], &sc_src[(uint32_t)sy * r->src_bytes_per_row + (uint32_t)sx * bpp], bpp);
            else
                for (uint32_t p = 0; p < r->depth; p++) {
                    if (sp_aptr[p] == 0 || sp_aptr[p] == 0xffffffffu) continue;
                    uint32_t b = sptr[p] == 0 ? 0 : sptr[p] == 0xffffffffu ? 1 : sp_src[p][(uint32_t)sy * (SW / 8) + (uint32_t)sx / 8] >> (7 - (sx & 7)) & 1;
                    uint8_t *d = &sp_b[p][(uint32_t)y * (DW / 8) + (uint32_t)x / 8], bit = (uint8_t)(0x80 >> (x & 7));
                    if (b) *d |= bit; else *d &= (uint8_t)~bit;
                }
        }
    return n;
}

static int failures, checks;
static void check(int ok, const char *what, int i)
{
    checks++;
    if (!ok && failures++ < 10) printf("FAIL %s (case %d)\n", what, i);
}

int main(void)
{
    for (int i = 0; i < 4000; i++) {          /* planar */
        uint32_t bpr = 2 * (1 + rnd() % (W / 16)), depth = 1 + rnd() % 8, hgt = 1 + rnd() % H, wid = bpr * 8;
        struct ogfx_planar_rect r;
        for (int p = 0; p < 8; p++) for (uint32_t k = 0; k < sizeof planes_a[0]; k++) planes_a[p][k] = planes_b[p][k] = (uint8_t)rnd();
        for (int p = 0; p < 8; p++) ptrs[p] = (uint32_t)(uintptr_t)planes_a[p];
        if (rnd() % 8 == 0) ptrs[rnd() % 8] = 0;
        if (rnd() % 8 == 0) ptrs[rnd() % 8] = 0xffffffffu;
        r.planes = (uint32_t)(uintptr_t)ptrs; r.bytes_per_row = bpr; r.depth = depth;
        r.x0 = (int32_t)(rnd() % wid); r.x1 = r.x0 + (int32_t)(rnd() % (wid - (uint32_t)r.x0));
        r.y0 = (int32_t)(rnd() % hgt); r.y1 = r.y0 + (int32_t)(rnd() % (hgt - (uint32_t)r.y0));
        if (rnd() % 16 == 0) r.x1 = r.x0 - 1;   /* nothing */
        if (rnd() % 32 == 0) r.depth = 9;
        r.pen = rnd(); r.mask = rnd() % 4 ? rnd() : 0xff; r.mode = rnd() % 3 == 0;
        uint32_t want, got;
        ref_planar(planes_b, &r, ptrs, &want);
        got = ogfx_planar_rect(&r);
        check(got == want, "planar: planes drawn", i);
        check(!memcmp(planes_a, planes_b, sizeof planes_a), "planar: the planes' bytes", i);
    }
    for (int i = 0; i < 4000; i++) {          /* planar BltBitMap minterms and overlap */
        uint32_t bpr = 2 * (2 + rnd() % (W / 16 - 1)), depth = 1 + rnd() % 8;
        uint32_t wid = bpr * 8, hgt = 2 + rnd() % (H - 1);
        int same = (rnd() & 1) != 0;
        struct ogfx_planar_blit r;

        for (int p = 0; p < 8; ++p)
            for (uint32_t k = 0; k < sizeof planes_a[0]; ++k) {
                uint8_t a = (uint8_t)rnd(), b = (uint8_t)rnd();
                planes_a[p][k] = planes_b[p][k] = a;
                planes_c[p][k] = planes_d[p][k] = same ? a : b;
            }

        for (int p = 0; p < 8; ++p) {
            src_ptrs[p] = (uint32_t)(uintptr_t)planes_a[p];
            dst_ptrs[p] = (uint32_t)(uintptr_t)(same ? planes_a[p] : planes_c[p]);
        }
        if (rnd() % 10 == 0) src_ptrs[rnd() % depth] = 0;
        if (rnd() % 10 == 0) src_ptrs[rnd() % depth] = 0xffffffffu;
        if (rnd() % 12 == 0) dst_ptrs[rnd() % depth] = 0;
        if (rnd() % 12 == 0) dst_ptrs[rnd() % depth] = 0xffffffffu;

        r.src_planes = (uint32_t)(uintptr_t)src_ptrs;
        r.src_bytes_per_row = bpr;
        r.dst_planes = (uint32_t)(uintptr_t)dst_ptrs;
        r.dst_bytes_per_row = bpr;
        r.depth = depth;
        r.width = 1 + (int32_t)(rnd() % (wid / 2));
        r.height = 1 + (int32_t)(rnd() % (hgt / 2));
        r.sx = (int32_t)(rnd() % (wid - (uint32_t)r.width + 1));
        r.sy = (int32_t)(rnd() % (hgt - (uint32_t)r.height + 1));
        r.dx = (int32_t)(rnd() % (wid - (uint32_t)r.width + 1));
        r.dy = (int32_t)(rnd() % (hgt - (uint32_t)r.height + 1));
        r.minterm = (rnd() & 0x0fu) << 4;
        r.mask = rnd() % 5 ? rnd() : 0xff;
        r.same_bitmap = same;

        /* Reference pointer values describe constants only; normal planes
         * are read from the reference arrays above. */
        uint32_t rsp[8], rdp[8];
        for (int p = 0; p < 8; ++p) {
            rsp[p] = src_ptrs[p] == 0 || src_ptrs[p] == 0xffffffffu ? src_ptrs[p] : 1;
            rdp[p] = dst_ptrs[p] == 0 || dst_ptrs[p] == 0xffffffffu ? dst_ptrs[p] : 1;
        }

        uint32_t want = ref_planar_blit(planes_b, same ? planes_b : planes_d,
                                        &r, rsp, rdp);
        uint32_t got = ogfx_planar_blit(&r);
        check(got == want, "planar blit: planes involved", i);
        if (same)
            check(!memcmp(planes_a, planes_b, sizeof planes_a), "planar blit: overlapping bytes", i);
        else {
            check(!memcmp(planes_a, planes_b, sizeof planes_a), "planar blit: source unchanged", i);
            check(!memcmp(planes_c, planes_d, sizeof planes_c), "planar blit: destination bytes", i);
        }
    }
    for (int i = 0; i < 4000; i++) {          /* chunky fills */
        uint32_t bpp = 1 + rnd() % 4, wid = 1 + rnd() % W, hgt = 1 + rnd() % H, bpr = wid * bpp + rnd() % 8;
        struct ogfx_chunky_rect r;
        for (uint32_t k = 0; k < sizeof chunky_a; k++) chunky_a[k] = chunky_b[k] = (uint8_t)rnd();
        uint32_t off = rnd() % 7;
        r.base = (uint32_t)(uintptr_t)(chunky_a + off); r.bytes_per_row = bpr; r.bytes_per_pixel = rnd() % 64 ? bpp : 5;
        r.x0 = (int32_t)(rnd() % wid); r.x1 = r.x0 + (int32_t)(rnd() % (wid - (uint32_t)r.x0));
        r.y0 = (int32_t)(rnd() % hgt); r.y1 = r.y0 + (int32_t)(rnd() % (hgt - (uint32_t)r.y0));
        if (rnd() % 16 == 0) r.y1 = r.y0 - 1;
        r.color = rnd(); r.mode = rnd() % 3 == 0;
        uint32_t want = ref_chunky(chunky_b + off, &r), got = ogfx_chunky_rect(&r);
        check(got == want, "chunky fill: pixels drawn", i);
        check(!memcmp(chunky_a, chunky_b, sizeof chunky_a), "chunky fill: the bytes", i);
    }
    for (int i = 0; i < 4000; i++) {          /* copies within one bitmap, every direction, and from another */
        uint32_t bpp = 1 + rnd() % 4, wid = 2 + rnd() % (W / 2), hgt = 2 + rnd() % (H / 2), bpr = wid * bpp + rnd() % 8;
        struct ogfx_chunky_copy r;
        for (uint32_t k = 0; k < sizeof chunky_a; k++) chunky_a[k] = chunky_b[k] = (uint8_t)rnd();
        uint32_t base = (uint32_t)(uintptr_t)chunky_a;
        int other = rnd() % 4 == 0;
        r.src = other ? base + (uint32_t)(sizeof chunky_a / 2) : base; r.dst = base;
        r.src_bytes_per_row = r.dst_bytes_per_row = bpr; r.bytes_per_pixel = bpp;
        r.width = 1 + (int32_t)(rnd() % (wid - 1)); r.height = 1 + (int32_t)(rnd() % (hgt - 1));
        r.sx = (int32_t)(rnd() % (wid - (uint32_t)r.width + 1)); r.sy = (int32_t)(rnd() % (hgt - (uint32_t)r.height + 1));
        r.dx = (int32_t)(rnd() % (wid - (uint32_t)r.width + 1)); r.dy = (int32_t)(rnd() % (hgt - (uint32_t)r.height + 1));
        if (other && (uint32_t)(r.sy + r.height) * bpr > sizeof chunky_a / 2) r.sy = 0;
        if (rnd() % 32 == 0) r.width = 0;
        uint32_t want = ref_copy(chunky_b, &r, base), got = ogfx_chunky_copy(&r);
        check(got == want, "chunky copy: pixels copied", i);
        check(!memcmp(chunky_a, chunky_b, sizeof chunky_a), "chunky copy: the bytes", i);
    }
    for (int i = 0; i < 3000; i++) {          /* scaled copies, chunky and planar */
        struct ogfx_scale_copy r;
        int planar = rnd() % 3 == 0;
        uint32_t bpp = planar ? 0 : 1 + rnd() % 4, f = 1 + rnd() % 4;
        for (uint32_t k = 0; k < sizeof sc_src; k++) sc_src[k] = (uint8_t)rnd();
        for (uint32_t k = 0; k < sizeof sc_a; k++) sc_a[k] = sc_b[k] = (uint8_t)rnd();
        for (int p = 0; p < 8; p++) {
            for (uint32_t k = 0; k < sizeof sp_src[0]; k++) sp_src[p][k] = (uint8_t)rnd();
            for (uint32_t k = 0; k < sizeof sp_a[0]; k++) sp_a[p][k] = sp_b[p][k] = (uint8_t)rnd();
            sp_sptr[p] = (uint32_t)(uintptr_t)sp_src[p];
            sp_aptr[p] = (uint32_t)(uintptr_t)sp_a[p];
        }
        if (rnd() % 6 == 0) sp_sptr[rnd() % 8] = 0;
        if (rnd() % 6 == 0) sp_sptr[rnd() % 8] = 0xffffffffu;
        if (rnd() % 6 == 0) sp_aptr[rnd() % 8] = 0;
        if (rnd() % 6 == 0) sp_aptr[rnd() % 8] = 0xffffffffu;
        r.bytes_per_pixel = bpp; r.depth = 1 + rnd() % 8; r.factor = f;
        r.src = planar ? (uint32_t)(uintptr_t)sp_sptr : (uint32_t)(uintptr_t)sc_src;
        r.dst = planar ? (uint32_t)(uintptr_t)sp_aptr : (uint32_t)(uintptr_t)sc_a;
        r.src_bytes_per_row = planar ? SW / 8 : SW * bpp; r.dst_bytes_per_row = planar ? DW / 8 : DW * bpp;
        r.width = 1 + (int32_t)(rnd() % SW); if (r.width * (int32_t)f > DW) r.width = DW / (int32_t)f;
        r.height = 1 + (int32_t)(rnd() % SH);
        r.sx = (int32_t)(rnd() % (SW - (uint32_t)r.width + 1)); r.sy = (int32_t)(rnd() % (SH - (uint32_t)r.height + 1));
        r.dx = (int32_t)(rnd() % (DW - (uint32_t)r.width * f + 1)); r.dy = (int32_t)(rnd() % (DH - (uint32_t)r.height * f + 1));
        r.cx0 = (int32_t)(rnd() % DW); r.cx1 = r.cx0 + (int32_t)(rnd() % (DW - (uint32_t)r.cx0));
        r.cy0 = (int32_t)(rnd() % DH); r.cy1 = r.cy0 + (int32_t)(rnd() % (DH - (uint32_t)r.cy0));
        if (rnd() % 4 == 0) { r.cx0 = r.cy0 = 0; r.cx1 = DW - 1; r.cy1 = DH - 1; }
        uint32_t want = ref_scale(&r, sp_sptr), got = ogfx_scale_copy(&r);
        check(got == want, "scale copy: pixels written", i);
        check(planar ? !memcmp(sp_a, sp_b, sizeof sp_a) : !memcmp(sc_a, sc_b, sizeof sc_a), "scale copy: the bytes", i);
    }
    {                                          /* blocks that draw nothing */
        struct ogfx_scale_copy r = { (uint32_t)(uintptr_t)sc_src, SW, (uint32_t)(uintptr_t)sc_a, DW, 1, 0, 0, 0, 4, 4, 0, 0, 5, 0, 0, DW - 1, DH - 1 };
        check(ogfx_scale_copy(&r) == 0, "scale copy: factor 5", 0);
        r.factor = 2; r.cx1 = -1;
        check(ogfx_scale_copy(&r) == 0, "scale copy: an empty clip", 0);
        r.cx1 = DW - 1; r.width = 0;
        check(ogfx_scale_copy(&r) == 0, "scale copy: no width", 0);
    }
    printf("%s %d of %d checks\n", failures ? "FAIL" : "ok  ", checks - failures, checks);
    return failures != 0;
}

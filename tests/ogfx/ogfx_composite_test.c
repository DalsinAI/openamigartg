/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../library/ogfx/ogfx_composite.h"

#define W 97
#define H 41
#define MAXSTRIDE (W * 4 + 13)

static uint8_t src[MAXSTRIDE * H];
static uint8_t dst_a[MAXSTRIDE * H];
static uint8_t dst_b[MAXSTRIDE * H];
static uint8_t mask[(W + 7) * H];
static uint32_t rng = 0x6d2b79f5u;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint8_t ref_mul255(uint8_t a, uint8_t b)
{
    return (uint8_t)(((uint32_t)a * (uint32_t)b + 127u) / 255u);
}

static uint8_t ref_sat(uint32_t x)
{
    return (uint8_t)(x > 255u ? 255u : x);
}

static void ref_over(uint8_t *d, const uint8_t *s)
{
    uint8_t inv = (uint8_t)(255u - s[0]);
    d[0] = ref_sat((uint32_t)s[0] + ref_mul255(d[0], inv));
    d[1] = ref_sat((uint32_t)s[1] + ref_mul255(d[1], inv));
    d[2] = ref_sat((uint32_t)s[2] + ref_mul255(d[2], inv));
    d[3] = ref_sat((uint32_t)s[3] + ref_mul255(d[3], inv));
}

static void make_premul(uint8_t *p)
{
    uint8_t a = (uint8_t)rnd();
    p[0] = a;
    p[1] = ref_mul255((uint8_t)rnd(), a);
    p[2] = ref_mul255((uint8_t)rnd(), a);
    p[3] = ref_mul255((uint8_t)rnd(), a);
}

static void fill_case(uint32_t stride, uint32_t mstride, uint32_t h)
{
    uint32_t x, y;
    for (y = 0; y < h; ++y) {
        for (x = 0; x < stride / 4u; ++x) {
            make_premul(src + y * stride + x * 4u);
            make_premul(dst_a + y * stride + x * 4u);
        }
        memcpy(dst_b + y * stride, dst_a + y * stride, stride);
        for (x = 0; x < mstride; ++x)
            mask[y * mstride + x] = (uint8_t)rnd();
    }
}

static int check_plain(int id)
{
    uint32_t w = 1u + rnd() % W, h = 1u + rnd() % H;
    uint32_t stride = w * 4u + rnd() % 12u;
    uint32_t y, x, got;
    fill_case(stride, w, h);
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            ref_over(dst_b + y * stride + x * 4u,
                     src + y * stride + x * 4u);
    got = ogfx_argb32_over(dst_a, stride, src, stride, w, h);
    if (got != w * h || memcmp(dst_a, dst_b, stride * h)) {
        printf("FAIL plain %d %ux%u stride=%u got=%u\n", id, w, h, stride, got);
        return 0;
    }
    return 1;
}

static int check_mask(int id)
{
    uint32_t w = 1u + rnd() % W, h = 1u + rnd() % H;
    uint32_t stride = w * 4u + rnd() % 12u;
    uint32_t mstride = w + rnd() % 7u;
    uint32_t y, x, got;
    fill_case(stride, mstride, h);
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            const uint8_t *s = src + y * stride + x * 4u;
            uint8_t q[4], a = mask[y * mstride + x];
            q[0] = ref_mul255(s[0], a);
            q[1] = ref_mul255(s[1], a);
            q[2] = ref_mul255(s[2], a);
            q[3] = ref_mul255(s[3], a);
            ref_over(dst_b + y * stride + x * 4u, q);
        }
    got = ogfx_argb32_over_a8(dst_a, stride, src, stride,
                              mask, mstride, w, h);
    if (got != w * h || memcmp(dst_a, dst_b, stride * h)) {
        printf("FAIL mask %d %ux%u stride=%u mstride=%u got=%u\n",
               id, w, h, stride, mstride, got);
        return 0;
    }
    return 1;
}

static int check_solid(int id)
{
    uint32_t w = 1u + rnd() % W, h = 1u + rnd() % H;
    uint32_t stride = w * 4u + rnd() % 12u;
    uint32_t mstride = w + rnd() % 7u;
    uint32_t y, x, got, argb;
    uint8_t colour[4];
    fill_case(stride, mstride, h);
    make_premul(colour);
    argb = ((uint32_t)colour[0] << 24) | ((uint32_t)colour[1] << 16) |
           ((uint32_t)colour[2] << 8) | colour[3];
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            uint8_t q[4], a = mask[y * mstride + x];
            q[0] = ref_mul255(colour[0], a);
            q[1] = ref_mul255(colour[1], a);
            q[2] = ref_mul255(colour[2], a);
            q[3] = ref_mul255(colour[3], a);
            ref_over(dst_b + y * stride + x * 4u, q);
        }
    got = ogfx_argb32_solid_over_a8(dst_a, stride, argb,
                                    mask, mstride, w, h);
    if (got != w * h || memcmp(dst_a, dst_b, stride * h)) {
        printf("FAIL solid %d %ux%u stride=%u mstride=%u got=%u\n",
               id, w, h, stride, mstride, got);
        return 0;
    }
    return 1;
}

int main(void)
{
    int i, ok = 1;
    uint8_t opaque[4] = {255, 17, 31, 63};
    uint8_t d[4] = {200, 10, 20, 30};
    uint8_t m = 255;

    if (ogfx_argb32_over(d, 4, opaque, 4, 1, 1) != 1 ||
        memcmp(d, opaque, 4)) {
        puts("FAIL opaque source");
        ok = 0;
    }

    memcpy(d, opaque, 4);
    if (ogfx_argb32_over_a8(d, 4, opaque, 4, &m, 1, 0, 1) != 0) {
        puts("FAIL zero width");
        ok = 0;
    }

    for (i = 0; i < 5000 && ok; ++i) ok = check_plain(i);
    for (i = 0; i < 5000 && ok; ++i) ok = check_mask(i);
    for (i = 0; i < 5000 && ok; ++i) ok = check_solid(i);

    printf("%s OpenGfx composite: 15000 random cases\n", ok ? "ok  " : "FAIL");
    return ok ? 0 : 1;
}

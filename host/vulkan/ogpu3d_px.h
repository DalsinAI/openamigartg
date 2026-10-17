/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The 3D range's pixels on the GPU: library/opengpu/ogpu_3d.c's general span
 * (o3_span), one pixel at a time, in the same integer steps, so the GPU's
 * pictures are the core's pixel for pixel. Triangle setup, coverage (the
 * spans) and the choice of mip level stay on the CPU, in ogpu_3d.c itself
 * (its sink): this file only colours the pixels.
 *
 * Written in the part of GLSL that is also C, so the same text builds as the
 * shader (ogpu3d.comp) and as C for the host test that runs it on the CPU
 * (tests/test_ogpu3d_emul.c). The includer defines:
 *   TOU(x), TOI(x)     conversions to uint and int
 *   INOUT              GLSL's inout (empty in C, where arrays are pointers)
 *   VRAM(i), STAGE(i), TEXB(i), DATA(i)        the buffers' words
 *   XOR_VRAM(i, v), XOR_STAGE(i, v)            an atomic exclusive or
 *   PC(n)              push constant n (ogpu3d_layout.h's P3_)
 *   uint, bool, true, false, and struct o3x_level as a type name (C)
 * Integer arithmetic wraps modulo 2^32 (C builds with -fwrapv), shifts of
 * signed values are arithmetic, and no division has a negative operand. */

#include "ogpu3d_layout.h"

#define K3_CLUT8        1u
#define K3_RGB565       2u
#define K3_ARGB32       3u
#define K3_A8           5u
#define K3_Z32          17u
#define K3_REPEAT       1u
#define K3_BORDER       3u

/* ---- memory ---------------------------------------------------------------------- */

uint o3x_ldw(uint b, uint w) {
    if (b == B3_VRAM) return VRAM(w);
    if (b == B3_STAGE) return STAGE(w);
    if (b == B3_TEX) return TEXB(w);
    return DATA(w);
}

uint o3x_ld8(uint b, uint a) {
    return (o3x_ldw(b, a >> 2u) >> ((a & 3u) << 3u)) & 255u;
}

uint o3x_bswap(uint v) {
    return (v >> 24u) | ((v >> 8u) & 0xFF00u) | ((v << 8u) & 0xFF0000u) | (v << 24u);
}

/* n bytes at a, the first the highest */
uint o3x_get(uint b, uint a, uint n) {
    uint v = 0u, k;
    if (n == 4u && (a & 3u) == 0u) return o3x_bswap(o3x_ldw(b, a >> 2u));
    for (k = 0u; k < n; k++) v = (v << 8u) | o3x_ld8(b, a + k);
    return v;
}

/* The target, Z and stencil buffers are in video RAM or staging. A pixel's
 * bytes belong to this invocation alone, but a word may hold other pixels:
 * those words change by an atomic exclusive or of this pixel's bytes only. */
void o3x_xorw(uint b, uint w, uint v) {
    if (b == B3_VRAM) XOR_VRAM(w, v);
    else XOR_STAGE(w, v);
}

void o3x_put(uint b, uint a, uint n, uint v) {
    uint wi, old, delta, k;
    if (n == 4u && (a & 3u) == 0u) {
        if (b == B3_VRAM) VRAM(a >> 2u) = o3x_bswap(v);
        else STAGE(a >> 2u) = o3x_bswap(v);
        return;
    }
    wi = a >> 2u;
    old = o3x_ldw(b, wi);
    delta = 0u;
    for (k = 0u; k < n; k++) {
        uint ak = a + k, sh, byte;
        if ((ak >> 2u) != wi) {
            if (delta != 0u) o3x_xorw(b, wi, delta);
            wi = ak >> 2u;
            old = o3x_ldw(b, wi);
            delta = 0u;
        }
        sh = (ak & 3u) << 3u;
        byte = (v >> ((n - 1u - k) << 3u)) & 255u;
        delta |= (((old >> sh) & 255u) ^ byte) << sh;
    }
    if (delta != 0u) o3x_xorw(b, wi, delta);
}

/* ---- small helpers (ogpu_3d.c's o3_) ------------------------------------------------- */

int o3x_clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

int o3x_m255(int x, int y) {
    uint t = (TOU(x) & 0xFFFFu) * (TOU(y) & 0xFFFFu) + 128u;
    return TOI((t + (t >> 8u)) >> 8u);
}

void o3x_unpack(uint v, INOUT int c[4]) {
    c[3] = TOI(v >> 24u); c[0] = TOI((v >> 16u) & 255u); c[1] = TOI((v >> 8u) & 255u); c[2] = TOI(v & 255u);
}

void o3x_from565(uint hi, uint lo, INOUT int c[4]) {
    int r5 = TOI(hi >> 3u), g6 = TOI(((hi & 7u) << 3u) | (lo >> 5u)), b5 = TOI(lo & 31u);
    c[0] = (r5 << 3) | (r5 >> 2);
    c[1] = (g6 << 2) | (g6 >> 4);
    c[2] = (b5 << 3) | (b5 >> 2);
    c[3] = 255;
}

/* v * 2^28 / q (q > 0), rounded toward zero and saturating: o3_div_shl28. */
int o3x_div28(int v, int q) {
    bool neg = v < 0;
    uint a = neg ? 0u - TOU(v) : TOU(v), uq = TOU(q), qh, r, ql;
    int i, res;
    qh = a / uq;
    r = a - qh * uq;
    if (qh >= 8u) return neg ? -0x7FFFFFFF : 0x7FFFFFFF;
    ql = 0u;
    for (i = 0; i < 28; i++) {
        r <<= 1u; ql <<= 1u;
        if (r >= uq) { r -= uq; ql |= 1u; }
    }
    res = TOI((qh << 28u) | ql);
    return neg ? -res : res;
}

/* ---- textures ---------------------------------------------------------------------- */

struct o3x_level {
    uint base, pitch;
    int w, h, wmask, hmask;
};

o3x_level o3x_level_of(uint S, int lv) {
    o3x_level L;
    int w = TOI(DATA(S + ST_TEXW) >> TOU(lv)), h = TOI(DATA(S + ST_TEXH) >> TOU(lv));
    L.w = w < 1 ? 1 : w; L.h = h < 1 ? 1 : h;
    L.wmask = (L.w & (L.w - 1)) != 0 ? 0 : L.w - 1;
    L.hmask = (L.h & (L.h - 1)) != 0 ? 0 : L.h - 1;
    L.base = DATA(S + ST_LEVEL + TOU(lv));
    L.pitch = DATA(S + ST_PITCH + TOU(lv));
    return L;
}

int o3x_wrapc(int c, int size, int mask, uint mode) {
    if (mode == K3_REPEAT) {
        uint m;
        if (mask != 0 || size == 1) return c & mask;
        if (c >= 0) return TOI(TOU(c) % TOU(size));
        m = (0u - TOU(c)) % TOU(size);
        return m == 0u ? 0 : size - TOI(m);
    }
    if (mode == K3_BORDER && (c < 0 || c >= size)) return -1;
    return c < 0 ? 0 : c >= size ? size - 1 : c;
}

void o3x_texel(uint S, o3x_level L, int x, int y, INOUT int c[4]) {
    uint p, fmt, i, table;
    x = o3x_wrapc(x, L.w, L.wmask, DATA(S + ST_WRAPS));
    y = o3x_wrapc(y, L.h, L.hmask, DATA(S + ST_WRAPT));
    if (x < 0 || y < 0) { o3x_unpack(DATA(S + ST_BORDER), c); return; }
    p = L.base + L.pitch * TOU(y);
    fmt = DATA(S + ST_TEXFMT);
    if (fmt == K3_ARGB32) { o3x_unpack(o3x_get(B3_TEX, p + TOU(x) * 4u, 4u), c); return; }
    if (fmt == K3_RGB565) { o3x_from565(o3x_ld8(B3_TEX, p + TOU(x) * 2u), o3x_ld8(B3_TEX, p + TOU(x) * 2u + 1u), c); return; }
    i = o3x_ld8(B3_TEX, p + TOU(x));
    if (fmt == K3_A8) { c[0] = 255; c[1] = 255; c[2] = 255; c[3] = TOI(i); return; }
    table = DATA(S + ST_TABLE);                         /* INDEX8, CLUT8 */
    if (table != OGPU3D_NONE) o3x_unpack(o3x_get(B3_TEX, table + i * 4u, 4u), c);
    else { c[0] = TOI(i); c[1] = TOI(i); c[2] = TOI(i); c[3] = 255; }
}

/* The level at U, V (texels at this level, 24.8), nearest or bilinear: o3_sample. */
void o3x_sample(uint S, o3x_level L, int U, int V, bool bilinear, INOUT int c[4]) {
    int t00[4], t10[4], t01[4], t11[4], k, x, y;
    uint fx, fy;
    if (!bilinear) { o3x_texel(S, L, U >> 8, V >> 8, c); return; }
    U -= 128; V -= 128;
    x = U >> 8; y = V >> 8;
    fx = TOU(U) & 255u; fy = TOU(V) & 255u;
    o3x_texel(S, L, x, y, t00); o3x_texel(S, L, x + 1, y, t10);
    o3x_texel(S, L, x, y + 1, t01); o3x_texel(S, L, x + 1, y + 1, t11);
    for (k = 0; k < 4; k++) {
        uint top = TOU(t00[k]) * (256u - fx) + TOU(t10[k]) * fx;
        uint bot = TOU(t01[k]) * (256u - fx) + TOU(t11[k]) * fx;
        top = (top + 128u) >> 8u; bot = (bot + 128u) >> 8u;
        c[k] = TOI((top * (256u - fy) + bot * fy + 128u) >> 8u);
    }
}

/* The texture environment: o3_environment. */
void o3x_environment(int mode, int base, INOUT int T[4], INOUT int cc[4], INOUT int f[4]) {
    bool has_c = true, has_a = true;
    if (base == 1) has_a = false;                                  /* RGB */
    else if (base == 2) has_c = false;                             /* ALPHA */
    else if (base == 3) { T[1] = T[0]; T[2] = T[0]; has_a = false; }    /* LUMINANCE */
    else if (base == 4) { T[1] = T[0]; T[2] = T[0]; }              /* LUM_ALPHA */
    else if (base == 5) { T[1] = T[0]; T[2] = T[0]; T[3] = T[0]; } /* INTENSITY */
    if (mode == 1) {                                               /* REPLACE */
        if (has_c) { f[0] = T[0]; f[1] = T[1]; f[2] = T[2]; }
        if (has_a) f[3] = T[3];
    } else if (mode == 2) {                                        /* DECAL */
        if (base == 1) { f[0] = T[0]; f[1] = T[1]; f[2] = T[2]; }
        else if (base == 0) {
            f[0] = o3x_m255(f[0], 255 - T[3]) + o3x_m255(T[0], T[3]);
            f[1] = o3x_m255(f[1], 255 - T[3]) + o3x_m255(T[1], T[3]);
            f[2] = o3x_m255(f[2], 255 - T[3]) + o3x_m255(T[2], T[3]);
        }
    } else if (mode == 3) {                                        /* MODULATE */
        if (has_c) { f[0] = o3x_m255(f[0], T[0]); f[1] = o3x_m255(f[1], T[1]); f[2] = o3x_m255(f[2], T[2]); }
        if (has_a) f[3] = o3x_m255(f[3], T[3]);
    } else if (mode == 4) {                                        /* BLEND */
        if (has_c) {
            f[0] = o3x_m255(f[0], 255 - T[0]) + o3x_m255(cc[0], T[0]);
            f[1] = o3x_m255(f[1], 255 - T[1]) + o3x_m255(cc[1], T[1]);
            f[2] = o3x_m255(f[2], 255 - T[2]) + o3x_m255(cc[2], T[2]);
        }
        if (base == 5) f[3] = o3x_m255(f[3], 255 - T[3]) + o3x_m255(cc[3], T[3]);
        else if (has_a) f[3] = o3x_m255(f[3], T[3]);
    } else if (mode == 5) {                                        /* ADD */
        if (has_c) { f[0] = o3x_clamp255(f[0] + T[0]); f[1] = o3x_clamp255(f[1] + T[1]); f[2] = o3x_clamp255(f[2] + T[2]); }
        if (has_a) f[3] = o3x_m255(f[3], T[3]);
    } else if (mode == 6) {                                        /* SUB */
        if (has_c) { f[0] = o3x_clamp255(f[0] - T[0]); f[1] = o3x_clamp255(f[1] - T[1]); f[2] = o3x_clamp255(f[2] - T[2]); }
        if (has_a) f[3] = o3x_m255(f[3], T[3]);
    }
}

/* ---- the fragment tests and the blend --------------------------------------------- */

bool o3x_cmp(uint func, uint a, uint b) {      /* W3D_Z_ / W3D_A_: is a (incoming) OP b? */
    if (func == 1u) return false;
    if (func == 2u) return a < b;
    if (func == 3u) return a >= b;
    if (func == 4u) return a <= b;
    if (func == 5u) return a > b;
    if (func == 6u) return a != b;
    if (func == 7u) return a == b;
    return true;
}

bool o3x_stencil_test(uint S, int s) {
    int m = TOI(DATA(S + ST_SMASK)), r = TOI(DATA(S + ST_SREF)) & m, v = s & m;
    uint f = DATA(S + ST_SFUNC);
    if (f == 1u) return false;
    if (f == 2u) return true;
    if (f == 3u) return r < v;
    if (f == 4u) return r <= v;
    if (f == 5u) return r == v;
    if (f == 6u) return r >= v;
    if (f == 7u) return r > v;
    if (f == 8u) return r != v;
    return true;
}

int o3x_stencil_op(uint S, uint op, int s) {
    int n = s, wm = TOI(DATA(S + ST_SWMASK));
    if (op == 2u) n = 0;
    else if (op == 3u) n = TOI(DATA(S + ST_SREF));
    else if (op == 4u) n = s < 255 ? s + 1 : 255;
    else if (op == 5u) n = s > 0 ? s - 1 : 0;
    else if (op == 6u) n = ~s & 255;
    else if (op == 7u) n = (s + 1) & 255;
    else if (op == 8u) n = (s - 1) & 255;
    return (s & ~wm) | (n & wm);
}

void o3x_factor(uint fac, INOUT int s[4], INOUT int d[4], INOUT int k[4], INOUT int o[4]) {
    int i, v;
    if (fac == 1u) { o[0] = 0; o[1] = 0; o[2] = 0; o[3] = 0; return; }
    if (fac == 3u) { for (i = 0; i < 4; i++) o[i] = s[i]; return; }
    if (fac == 4u) { for (i = 0; i < 4; i++) o[i] = d[i]; return; }
    if (fac == 5u) { for (i = 0; i < 4; i++) o[i] = 255 - s[i]; return; }
    if (fac == 6u) { for (i = 0; i < 4; i++) o[i] = 255 - d[i]; return; }
    if (fac == 11u) { v = s[3] < 255 - d[3] ? s[3] : 255 - d[3]; o[0] = v; o[1] = v; o[2] = v; o[3] = 255; return; }
    if (fac == 12u) { for (i = 0; i < 4; i++) o[i] = k[i]; return; }
    if (fac == 13u) { for (i = 0; i < 4; i++) o[i] = 255 - k[i]; return; }
    if (fac == 7u) v = s[3];
    else if (fac == 8u) v = 255 - s[3];
    else if (fac == 9u) v = d[3];
    else if (fac == 10u) v = 255 - d[3];
    else if (fac == 14u) v = k[3];
    else if (fac == 15u) v = 255 - k[3];
    else v = 255;                                       /* ONE */
    o[0] = v; o[1] = v; o[2] = v; o[3] = v;
}

uint o3x_logic(uint op, uint s, uint d) {
    if (op == 1u) return 0u;
    if (op == 2u) return s & d;
    if (op == 3u) return s & ~d;
    if (op == 5u) return ~s & d;
    if (op == 6u) return d;
    if (op == 7u) return s ^ d;
    if (op == 8u) return s | d;
    if (op == 9u) return ~(s | d);
    if (op == 10u) return ~(s ^ d);
    if (op == 11u) return ~d;
    if (op == 12u) return s | ~d;
    if (op == 13u) return ~s;
    if (op == 14u) return ~s | d;
    if (op == 15u) return ~(s & d);
    if (op == 16u) return 0xFFFFFFFFu;
    return s;                                           /* COPY */
}

/* ---- the target ----------------------------------------------------------------------- */

void o3x_read_dst(uint S, uint b, uint a, uint fmt, INOUT int c[4]) {
    uint pal, p0;
    if (fmt == K3_ARGB32) { o3x_unpack(o3x_get(b, a, 4u), c); return; }
    if (fmt == K3_RGB565) { o3x_from565(o3x_ld8(b, a), o3x_ld8(b, a + 1u), c); return; }
    p0 = o3x_ld8(b, a);
    pal = DATA(S + ST_PAL);
    if (pal != OGPU3D_NONE) o3x_unpack(o3x_get(B3_DATA, pal + p0 * 4u, 4u), c);
    else { c[0] = TOI(p0); c[1] = TOI(p0); c[2] = TOI(p0); c[3] = 255; }
}

uint o3x_to_raw(uint S, uint fmt, INOUT int c[4]) {
    uint pen;
    if (fmt == K3_ARGB32) return (TOU(c[3]) << 24u) | (TOU(c[0]) << 16u) | (TOU(c[1]) << 8u) | TOU(c[2]);
    if (fmt == K3_RGB565)
        return (TOU((c[0] & 0xF8) | (c[1] >> 5)) << 8u) | TOU(((c[1] & 0x1C) << 3) | (c[2] >> 3));
    pen = DATA(S + ST_PEN);
    if (pen != OGPU3D_NONE)
        return o3x_ld8(B3_DATA, pen + (TOU(((c[0] & 0xF8) << 7) | ((c[1] & 0xF8) << 2) | (c[2] >> 3)) & 0x7FFFu));
    return TOU((c[0] * 77 + c[1] * 151 + c[2] * 28) >> 8);
}

uint o3x_bytes(uint fmt) { return fmt == K3_ARGB32 ? 4u : fmt == K3_RGB565 ? 2u : 1u; }

uint o3x_raw_dst(uint b, uint a, uint fmt) { return o3x_get(b, a, o3x_bytes(fmt)); }

void o3x_write_raw(uint b, uint a, uint fmt, uint v) {
    uint n = o3x_bytes(fmt);
    o3x_put(b, a, n, n == 4u ? v : n == 2u ? v & 0xFFFFu : v & 255u);
}

void o3x_write_px(uint S, uint b, uint a, uint fmt, INOUT int c[4]) {
    if (fmt == K3_ARGB32) {
        o3x_put(b, a, 4u, ((TOU(c[3]) & 255u) << 24u) | ((TOU(c[0]) & 255u) << 16u) | ((TOU(c[1]) & 255u) << 8u)
                          | (TOU(c[2]) & 255u));
        return;
    }
    if (fmt == K3_RGB565) {
        uint p0 = TOU((c[0] & 0xF8) | (c[1] >> 5)) & 255u, p1 = TOU(((c[1] & 0x1C) << 3) | (c[2] >> 3)) & 255u;
        o3x_put(b, a, 2u, (p0 << 8u) | p1);
        return;
    }
    o3x_put(b, a, 1u, o3x_to_raw(S, fmt, c) & 255u);
}

/* ---- one pixel of one triangle: o3_span's loop body ------------------------------------- */

/* Value k of triangle T at the pixel dy rows below its first and dx columns
 * right of its rx: exact modulo 2^32, as the core's sums. */
uint o3x_val(uint T, uint k, int dy, int dx) {
    return DATA(T + TR_ROW + k) + DATA(T + TR_GY + k) * TOU(dy) + DATA(T + TR_GX + k) * TOU(dx);
}

void o3x_pixel(uint T, int x, int y) {
    int ys = TOI(DATA(T + TR_YS)), x0, x1, dy, dx, rx, f[4], Uc, Vc;
    uint spw, S, mode, fl, fmt, tb, ta;
    int sv;
    uint sb, sa;
    if (y < ys || y >= TOI(DATA(T + TR_YE))) return;
    spw = DATA(DATA(T + TR_SPANS) + TOU(y - ys));
    x0 = TOI(spw & 0xFFFFu); x1 = TOI(spw >> 16u);
    if (x < x0 || x >= x1) return;
    S = DATA(T + TR_STATE);
    mode = DATA(S + ST_MODE);
    fl = DATA(T + TR_FLAGS);
    rx = TOI(DATA(T + TR_RX));
    dy = y - ys; dx = x - rx;

    /* texture coordinates: under perspective, exact every 8 pixels from the
     * span's start and stepped linearly between */
    Uc = 0; Vc = 0;
    if ((mode & M_TEX) != 0u) {
        if ((fl & TRF_PERSP) != 0u) {
            int s = x0 + ((x - x0) & ~7), q0, q1, U0, V0, U1, V1;
            uint vU = o3x_val(T, AT3_U, dy, s - rx), vV = o3x_val(T, AT3_V, dy, s - rx), vQ = o3x_val(T, AT3_Q, dy, s - rx);
            q0 = TOI(vQ) > 0 ? TOI(vQ) : 1;
            q1 = TOI(vQ + (DATA(T + TR_GX + AT3_Q) << 3u));
            if (q1 <= 0) q1 = 1;
            U0 = o3x_div28(TOI(vU), q0);
            V0 = o3x_div28(TOI(vV), q0);
            U1 = o3x_div28(TOI(vU + (DATA(T + TR_GX + AT3_U) << 3u)), q1);
            V1 = o3x_div28(TOI(vV + (DATA(T + TR_GX + AT3_V) << 3u)), q1);
            Uc = U0 + ((U1 - U0) >> 3) * (x - s);
            Vc = V0 + ((V1 - V0) >> 3) * (x - s);
        } else {
            Uc = TOI(o3x_val(T, AT3_U, dy, dx));
            Vc = TOI(o3x_val(T, AT3_V, dy, dx));
        }
    }
    if ((mode & M_STIPPLE) != 0u) {
        uint st = DATA(S + ST_STIPPLE) + TOU((y & 31) << 2);
        if (((o3x_ld8(B3_DATA, st + TOU((x & 31) >> 3)) >> TOU(7 - (x & 7))) & 1u) == 0u) return;
    }
    /* colour */
    if ((mode & M_GOURAUD) != 0u) {
        f[0] = o3x_clamp255(TOI(o3x_val(T, AT3_R, dy, dx)) >> 16); f[1] = o3x_clamp255(TOI(o3x_val(T, AT3_G, dy, dx)) >> 16);
        f[2] = o3x_clamp255(TOI(o3x_val(T, AT3_B, dy, dx)) >> 16); f[3] = o3x_clamp255(TOI(o3x_val(T, AT3_A, dy, dx)) >> 16);
    } else {
        o3x_unpack(DATA(S + ST_CUR), f);
    }
    if ((mode & M_TEX) != 0u) {
        int Tx[4], cc[4], lv = TOI((fl >> 16u) & 255u), lfrac = TOI((fl >> 8u) & 255u);
        bool bil = (fl & TRF_BILINEAR) != 0u;
        o3x_sample(S, o3x_level_of(S, lv), Uc, Vc, bil, Tx);
        if (lfrac != 0) {
            int T1[4], j;
            o3x_sample(S, o3x_level_of(S, lv + 1), Uc >> 1, Vc >> 1, bil, T1);
            for (j = 0; j < 4; j++) Tx[j] += ((T1[j] - Tx[j]) * lfrac) >> 8;
        }
        if ((mode & M_CHROMA) != 0u) {
            int lo[4], hi[4];
            bool inside;
            o3x_unpack(DATA(S + ST_CHLO), lo); o3x_unpack(DATA(S + ST_CHHI), hi);
            inside = Tx[0] >= lo[0] && Tx[0] <= hi[0] && Tx[1] >= lo[1] && Tx[1] <= hi[1] && Tx[2] >= lo[2] && Tx[2] <= hi[2];
            if (inside != ((mode & M_CHROMA_IN) != 0u)) return;
        }
        o3x_unpack(DATA(S + ST_ENVC), cc);
        o3x_environment(TOI(DATA(S + ST_ENV)), TOI(DATA(S + ST_BASE)), Tx, cc, f);
    }
    if ((mode & M_SPEC) != 0u) {
        f[0] = o3x_clamp255(f[0] + (TOI(o3x_val(T, AT3_SR, dy, dx)) >> 16));
        f[1] = o3x_clamp255(f[1] + (TOI(o3x_val(T, AT3_SG, dy, dx)) >> 16));
        f[2] = o3x_clamp255(f[2] + (TOI(o3x_val(T, AT3_SB, dy, dx)) >> 16));
    }
    if ((mode & (M_FOGV | M_FOGT)) != 0u) {
        int ff;
        if ((mode & M_FOGV) != 0u) ff = o3x_clamp255(TOI(o3x_val(T, AT3_F, dy, dx)) >> 16);
        else {
            int idx = TOI(o3x_val(T, AT3_FI, dy, dx)) >> 16;
            ff = TOI(o3x_ld8(B3_DATA, DATA(S + ST_FOGTAB) + TOU(idx < 0 ? 0 : idx >= 1024 ? 1023 : idx)));
        }
        if (ff < 255) {
            int fc[4];
            o3x_unpack(DATA(S + ST_FOGC), fc);
            f[0] = o3x_m255(f[0], ff) + o3x_m255(fc[0], 255 - ff);
            f[1] = o3x_m255(f[1], ff) + o3x_m255(fc[1], 255 - ff);
            f[2] = o3x_m255(f[2], ff) + o3x_m255(fc[2], 255 - ff);
        }
    }
    if ((mode & M_ATEST) != 0u && !o3x_cmp(DATA(S + ST_AFUNC), TOU(f[3]), DATA(S + ST_AREF))) return;
    sv = 0; sb = PC(P3_SBUF); sa = 0u;
    if ((mode & M_STENCIL) != 0u) {
        sa = PC(P3_SOFF) + PC(P3_SBPR) * TOU(y) + TOU(x);
        sv = TOI(o3x_ld8(sb, sa));
        if (!o3x_stencil_test(S, sv)) { o3x_put(sb, sa, 1u, TOU(o3x_stencil_op(S, DATA(S + ST_SFAIL), sv))); return; }
    }
    if ((mode & M_ZTEST) != 0u) {
        int z = TOI(o3x_val(T, AT3_Z, dy, dx));
        uint zu = z < 0 ? 0u : z > 0x3FFFFFFF ? 0x3FFFFFFFu : TOU(z), zb = PC(P3_ZBUF), za, zval;
        bool pass;
        zu <<= 2u;
        if (PC(P3_ZFMT) == K3_Z32) {
            za = PC(P3_ZOFF) + PC(P3_ZBPR) * TOU(y) + TOU(x) * 4u;
            zval = zu;
            pass = o3x_cmp(DATA(S + ST_ZFUNC), zval, o3x_get(zb, za, 4u));
            if (pass && (mode & M_ZWRITE) != 0u) o3x_put(zb, za, 4u, zval);
        } else {
            za = PC(P3_ZOFF) + PC(P3_ZBPR) * TOU(y) + TOU(x) * 2u;
            zval = zu >> 16u;
            pass = o3x_cmp(DATA(S + ST_ZFUNC), zval, o3x_get(zb, za, 2u));
            if (pass && (mode & M_ZWRITE) != 0u) o3x_put(zb, za, 2u, zval);
        }
        if ((mode & M_STENCIL) != 0u) o3x_put(sb, sa, 1u, TOU(o3x_stencil_op(S, pass ? DATA(S + ST_SZPASS) : DATA(S + ST_SZFAIL), sv)));
        if (!pass) return;
    } else if ((mode & M_STENCIL) != 0u) {
        o3x_put(sb, sa, 1u, TOU(o3x_stencil_op(S, DATA(S + ST_SZPASS), sv)));
    }
    /* blend or logic op, then the colour mask */
    fmt = PC(P3_TFMT); tb = PC(P3_TBUF);
    ta = PC(P3_TOFF) + PC(P3_TBPR) * TOU(y) + TOU(x) * o3x_bytes(fmt);
    if ((mode & M_LOGIC) != 0u) {
        uint old = o3x_raw_dst(tb, ta, fmt), wm = DATA(S + ST_WMASK);
        uint res = o3x_logic(DATA(S + ST_LOGICOP), o3x_to_raw(S, fmt, f), old);
        o3x_write_raw(tb, ta, fmt, (res & wm) | (old & ~wm));
        return;
    }
    if ((mode & M_BLEND) != 0u) {
        int dc[4], j;
        uint bk = DATA(S + ST_BLEND);
        o3x_read_dst(S, tb, ta, fmt, dc);
        if (fmt != K3_ARGB32) dc[3] = 255;
        if (bk == 1u) {
            int sa8 = f[3], da8 = 255 - f[3];
            for (j = 0; j < 4; j++) f[j] = o3x_m255(f[j], sa8) + o3x_m255(dc[j], da8);
        } else if (bk == 2u) {
            for (j = 0; j < 4; j++) f[j] = o3x_clamp255(f[j] + dc[j]);
        } else {
            int fs[4], fd[4], kc[4];
            o3x_unpack(DATA(S + ST_KC), kc);
            o3x_factor(DATA(S + ST_BSRC), f, dc, kc, fs);
            o3x_factor(DATA(S + ST_BDST), f, dc, kc, fd);
            for (j = 0; j < 4; j++) f[j] = o3x_clamp255(o3x_m255(f[j], fs[j]) + o3x_m255(dc[j], fd[j]));
        }
    }
    if ((mode & M_MASKALL) != 0u) o3x_write_px(S, tb, ta, fmt, f);
    else {
        uint old = o3x_raw_dst(tb, ta, fmt), wm = DATA(S + ST_WMASK);
        o3x_write_raw(tb, ta, fmt, (o3x_to_raw(S, fmt, f) & wm) | (old & ~wm));
    }
}

/* ---- one invocation: one pixel of a tile, every triangle on the tile's list ------------ */

void o3x_tile(uint wgx, uint wgy, uint lx, uint ly) {
    int x = TOI(PC(P3_GX0)) + TOI(wgx) * OGPU3D_TILE + TOI(lx);
    int y = TOI(PC(P3_GY0)) + TOI(wgy) * OGPU3D_TILE + TOI(ly);
    uint b = PC(P3_BINS) + (wgy * PC(P3_GTW) + wgx) * 2u, first = DATA(b), n = DATA(b + 1u), i;
    for (i = 0u; i < n; i++) o3x_pixel(DATA(first + i), x, y);
}

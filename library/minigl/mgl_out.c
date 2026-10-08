/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: the batch. Triangles go into the context's batch as OGPU
 * stream v1.2 commands (include/opengpu/stream3d.h), with the state they
 * are drawn with in front (RENDER3D, TEXENV, TEXTURE, DEPTH, CLIP), and
 * vertices in the batch's data area. A flush puts the drawing area's
 * SURFACE and TARGET in front and runs it:
 *  - on OpenGPU: OGPU_Submit to opengpu.library, which hands it to its back
 *    end (ACRTG.gpu: the PC on AmigaChrome), and OGPU_Wait;
 *  - on the 68k: the 3D core linked in here.
 * A batch carries all its state, as OpenGPU's back ends keep nothing
 * between batches. A frame is one batch unless it outgrows it. */
#include "mgl_internal.h"

#ifndef MGL_HOST
#include <proto/exec.h>
#include "../../include/opengpu/opengpu.h"
#include "../../include/proto/opengpu.h"
extern struct Library *OpenGPUBase;
#endif

/* The stream numbers its functions, factors and filters as Warp3D does. */
#define W3D_Z_NEVER 1
#define W3D_Z_LESS 2
#define W3D_Z_GEQUAL 3
#define W3D_Z_LEQUAL 4
#define W3D_Z_GREATER 5
#define W3D_Z_NOTEQUAL 6
#define W3D_Z_EQUAL 7
#define W3D_Z_ALWAYS 8
#define W3D_ZERO 1
#define W3D_ONE 2
#define W3D_SRC_COLOR 3
#define W3D_DST_COLOR 4
#define W3D_ONE_MINUS_SRC_COLOR 5
#define W3D_ONE_MINUS_DST_COLOR 6
#define W3D_SRC_ALPHA 7
#define W3D_ONE_MINUS_SRC_ALPHA 8
#define W3D_DST_ALPHA 9
#define W3D_ONE_MINUS_DST_ALPHA 10
#define W3D_SRC_ALPHA_SATURATE 11
#define W3D_CONSTANT_COLOR 12
#define W3D_ONE_MINUS_CONSTANT_COLOR 13
#define W3D_CONSTANT_ALPHA 14
#define W3D_ONE_MINUS_CONSTANT_ALPHA 15
#define W3D_NEAREST 1
#define W3D_LINEAR 2
#define W3D_NEAREST_MIP_NEAREST 3
#define W3D_NEAREST_MIP_LINEAR 4
#define W3D_LINEAR_MIP_NEAREST 5
#define W3D_LINEAR_MIP_LINEAR 6
#define W3D_LO_COPY 4
#define W3D_ST_ALWAYS 2
#define W3D_ST_KEEP 1
#define W3D_CHROMATEST_NONE 1

/* ---- OpenGL's numbers to the stream's (Warp3D's) --------------------------------------------- */

static ULONG compare_of(GLenum f)
{
    switch (f) {
    case GL_NEVER: return W3D_Z_NEVER;
    case GL_LESS: return W3D_Z_LESS;
    case GL_EQUAL: return W3D_Z_EQUAL;
    case GL_LEQUAL: return W3D_Z_LEQUAL;
    case GL_GREATER: return W3D_Z_GREATER;
    case GL_NOTEQUAL: return W3D_Z_NOTEQUAL;
    case GL_GEQUAL: return W3D_Z_GEQUAL;
    }
    return W3D_Z_ALWAYS;
}

static ULONG factor_of(GLenum f)
{
    switch (f) {
    case GL_ZERO: return W3D_ZERO;
    case GL_ONE: return W3D_ONE;
    case GL_SRC_COLOR: return W3D_SRC_COLOR;
    case GL_DST_COLOR: return W3D_DST_COLOR;
    case GL_ONE_MINUS_SRC_COLOR: return W3D_ONE_MINUS_SRC_COLOR;
    case GL_ONE_MINUS_DST_COLOR: return W3D_ONE_MINUS_DST_COLOR;
    case GL_SRC_ALPHA: return W3D_SRC_ALPHA;
    case GL_ONE_MINUS_SRC_ALPHA: return W3D_ONE_MINUS_SRC_ALPHA;
    case GL_DST_ALPHA: return W3D_DST_ALPHA;
    case GL_ONE_MINUS_DST_ALPHA: return W3D_ONE_MINUS_DST_ALPHA;
    case GL_SRC_ALPHA_SATURATE: return W3D_SRC_ALPHA_SATURATE;
    case GL_CONSTANT_COLOR: return W3D_CONSTANT_COLOR;
    case GL_ONE_MINUS_CONSTANT_COLOR: return W3D_ONE_MINUS_CONSTANT_COLOR;
    case GL_CONSTANT_ALPHA: return W3D_CONSTANT_ALPHA;
    case GL_ONE_MINUS_CONSTANT_ALPHA: return W3D_ONE_MINUS_CONSTANT_ALPHA;
    }
    return W3D_ONE;
}

static int env_of(GLenum m)
{
    switch (m) {
    case GL_REPLACE: return OGPU_ENV_REPLACE;
    case GL_DECAL: return OGPU_ENV_DECAL;
    case GL_BLEND: return OGPU_ENV_BLEND;
    case GL_ADD: return OGPU_ENV_ADD;
    }
    return OGPU_ENV_MODULATE;
}

static int filter_of(GLenum f, int no_mip)
{
    switch (f) {
    case GL_NEAREST: return W3D_NEAREST;
    case GL_LINEAR: return W3D_LINEAR;
    case GL_NEAREST_MIPMAP_NEAREST: return no_mip ? W3D_NEAREST : W3D_NEAREST_MIP_NEAREST;
    case GL_NEAREST_MIPMAP_LINEAR: return no_mip ? W3D_NEAREST : W3D_NEAREST_MIP_LINEAR;
    case GL_LINEAR_MIPMAP_NEAREST: return no_mip ? W3D_LINEAR : W3D_LINEAR_MIP_NEAREST;
    case GL_LINEAR_MIPMAP_LINEAR: return no_mip ? W3D_LINEAR : W3D_LINEAR_MIP_LINEAR;
    }
    return W3D_LINEAR;
}

/* ---- opening and closing ---------------------------------------------------------------------- */

int mgl_out_open(GLcontext c)
{
    c->cmdmem = mgl_alloc(BATCH_WORDS * 4);
    c->vmem = mgl_alloc(VERT_BYTES);
    c->zfmt = mgl_prefs.zbits == 32 ? OGPU_FMT_Z32 : OGPU_FMT_Z16;
    c->zbpr = (ULONG)c->width * (c->zfmt == OGPU_FMT_Z32 ? 4 : 2);
    c->zbuf = mgl_alloc(c->zbpr * (ULONG)c->height);
    if (!c->cmdmem || !c->vmem || !c->zbuf) { mgl_out_close(c); return 0; }
    c->list_at = -1;
    c->sent_pass = -1;
    if (mgl_prefs.route >= 0) c->route = mgl_prefs.route == ROUTE_GPU && mgl_prefs.gpu_3d ? ROUTE_GPU : ROUTE_CPU;
    else c->route = mgl_prefs.gpu_3d && mgl_prefs.gpu_host ? ROUTE_GPU : ROUTE_CPU;
    return 1;
}

void mgl_out_close(GLcontext c)
{
    mgl_free(c->cmdmem); c->cmdmem = 0;
    mgl_free(c->vmem); c->vmem = 0;
    mgl_free(c->zbuf); c->zbuf = 0;
}

/* ---- running a batch ----------------------------------------------------------------------------- */

static ogpu_u8 *cpu_map(void *user, ogpu_u32 address, ogpu_u32 length)
{
    (void)user; (void)length;
    return (ogpu_u8 *)address;
}

void mgl_flush(GLcontext c)
{
    struct OGPUBatch pre;
    ULONG prefix[PREFIX_WORDS];
    long words;
    UBYTE *stream;
    if (!c->body) return;
    c->body = 0;
    c->list_at = -1;
    c->sent_tex = 0;
    c->sent_pass = -1;
    c->batch_no++;
    if (!mgl_disp_lock(c)) { c->b.words = 0; c->vused = 0; return; }
    ogpu_batch_init(&pre, prefix, PREFIX_WORDS);
    ogpu_surface(&pre, 0, ADDR(c->base), c->bpr, c->width, c->height, c->fmt);
    ogpu_target(&pre, 0);
    stream = (UBYTE *)(c->cmdmem + PREFIX_WORDS - pre.words);
    mgl_copy(stream, prefix, (ULONG)pre.words * 4);
    words = pre.words + c->b.words;
#ifndef MGL_HOST
    if (c->route == ROUTE_GPU && OpenGPUBase) {
        ULONG fence = 0, t0 = mgl_trace > 0 ? mgl_millis() : 0;
        LONG e = OGPU_Submit(stream, (ULONG)words, &fence);
        if (e == OGPU_OK) e = OGPU_Wait(fence);
        if (mgl_trace > 0) mgl_log("flush %ld words %ld bytes: %ld ms (%ld)", (long)words, (long)c->vused, (long)(mgl_millis() - t0), (long)e);
    } else
#endif
    {
        ogpu_core_init(&c->core);
        c->core.map = cpu_map;
        c->core.fence = 0;
        c->core.ext = 0;
        c->core.user = 0;
        ogpu3d_init(&c->d3, &c->core);
        ogpu3d_run(&c->d3, stream, words);
        if (mgl_trace > 0) mgl_log("flush %ld words %ld bytes on the 68k", (long)words, (long)c->vused);
    }
    mgl_disp_unlock(c);
    c->b.words = 0;
    c->vused = 0;
}

/* ---- the state the stream carries ------------------------------------------------------------- */

static void begin(GLcontext c)
{
    if (!c->body) {
        ogpu_batch_init(&c->b, c->cmdmem + PREFIX_WORDS, BATCH_WORDS - PREFIX_WORDS);
        c->body = 1;
        c->list_at = -1;
        c->sent_tex = 0;
        c->sent_pass = -1;
        c->clip_dirty = c->r3d_dirty = 1;
        ogpu_depth(&c->b, OGPU_KIND_DEPTH, ADDR(c->zbuf), c->zbpr, c->width, c->height, c->zfmt);
    }
}

static void send_clip(GLcontext c)
{
    int l = 0, t = 0, r = c->width, b = c->height;
    if (c->en & EN_SCISSOR) {
        l = c->sc_x; r = c->sc_x + c->sc_w;
        t = c->height - (c->sc_y + c->sc_h); b = c->height - c->sc_y;
        if (l < 0) l = 0;
        if (t < 0) t = 0;
        if (r > c->width) r = c->width;
        if (b > c->height) b = c->height;
        if (r <= l || b <= t) { l = t = 0; r = b = 1; c->scissor_empty = 1; } else c->scissor_empty = 0;
    } else c->scissor_empty = 0;
    ogpu_clip(&c->b, l, t, r - l, b - t);
    c->clip_dirty = 0;
    c->list_at = -1;
}

static void send_render3d(GLcontext c, int pass)
{
    ULONG en = OGPU_R3D_GOURAUD, a[OGPU_R3D_ARGS], fc;
    LONG aref = mgl_f2l(c->alpha_ref * 255.0f);
    int i;
    ULONG bsrc = factor_of(c->blend_src), bdst = factor_of(c->blend_dst), zf = compare_of(c->depth_func);
    if (c->hint_persp != GL_FASTEST) en |= OGPU_R3D_PERSPECTIVE;
    if ((c->en & EN_DEPTH) && c->zbuf) {
        en |= OGPU_R3D_ZTEST;
        if (c->depth_mask && !pass) en |= OGPU_R3D_ZWRITE;
    }
    if (c->en & EN_BLEND) en |= OGPU_R3D_BLEND;
    if ((c->en & EN_FOG) && !pass) en |= OGPU_R3D_FOG;
    if (c->en & EN_ALPHA) en |= OGPU_R3D_ALPHATEST;
    if (mgl_tex_unit(c, pass)) en |= OGPU_R3D_TEXTURE;
    if (pass) {
        /* unit 1 blended onto unit 0's picture, where it lies */
        en |= OGPU_R3D_BLEND;
        if (en & OGPU_R3D_ZTEST) zf = W3D_Z_EQUAL;
        switch (c->env_mode[1]) {
        case GL_REPLACE: en &= ~OGPU_R3D_BLEND; break;
        case GL_DECAL: bsrc = W3D_SRC_ALPHA; bdst = W3D_ONE_MINUS_SRC_ALPHA; break;
        case GL_ADD: bsrc = W3D_ONE; bdst = W3D_ONE; break;
        default: bsrc = W3D_DST_COLOR; bdst = W3D_ZERO; break;
        }
    }
    for (i = 0; i < OGPU_R3D_ARGS; i++) a[i] = 0;
    a[0] = en;
    a[1] = zf;
    a[2] = compare_of(c->alpha_func) | ((ULONG)(aref < 0 ? 0 : aref > 255 ? 255 : aref) << 16);
    a[3] = bsrc | (bdst << 16);
    a[4] = 0;
    a[5] = c->cmask | (255 << 8);
    a[6] = W3D_LO_COPY;
    a[7] = OGPU_FOG_VERTEX;
    fc = mgl_argb(c->fog_color);
    a[8] = fc | 0xFF000000UL;
    a[9] = mgl_fbits(1.0f);
    a[10] = mgl_fbits(0.0f);
    a[11] = mgl_fbits(1.0f);
    a[12] = W3D_ST_ALWAYS | (255UL << 16);
    a[13] = W3D_ST_KEEP | (W3D_ST_KEEP << 8) | (W3D_ST_KEEP << 16) | (255UL << 24);
    a[14] = W3D_CHROMATEST_NONE;
    a[17] = mgl_argb(c->color);
    ogpu_render3d(&c->b, (const unsigned long *)a);
    c->r3d_dirty = 0;
    c->sent_pass = pass;
}

static int levels_of(const struct mgl_tex *t)
{
    int n = 1, w = t->w, h = t->h;
    while (n < MGL_MAX_LEVELS) {
        w = w > 1 ? w >> 1 : 1;
        h = h > 1 ? h >> 1 : 1;
        if (!t->level[n] || t->lw[n] != w || t->lh[n] != h) break;
        n++;
        if (w == 1 && h == 1) break;
    }
    return n;
}

static void send_texture(GLcontext c, struct mgl_tex *t, int pass)
{
    if (!t) {
        ogpu_texture(&c->b, 0, 0, 0, 1, 1, OGPU_FMT_ARGB32, 0, 0, 1, 1, 1, 1, 0, 1, 0);
        c->sent_env = 0;
    } else {
        unsigned long lv[MGL_MAX_LEVELS];
        int i, n = levels_of(t), env = pass ? OGPU_ENV_REPLACE : env_of(c->env_mode[0]);
        ULONG envc = mgl_argb(c->env_color[pass]);
        for (i = 1; i < n; i++) lv[i - 1] = ADDR(t->level[i]);
        ogpu_texenv(&c->b, 0, env, envc);
        ogpu_texture(&c->b, 0, ADDR(t->level[0]), (ULONG)t->w * 4, t->w, t->h,
                     OGPU_TEX_FORMAT(OGPU_FMT_ARGB32, t->base), 0, t->generation,
                     filter_of(t->min, c->no_mip || mgl_prefs.no_mip || n == 1), filter_of(t->mag, 1),
                     t->wrap_s == GL_REPEAT ? OGPU_WRAP_REPEAT : OGPU_WRAP_CLAMP,
                     t->wrap_t == GL_REPEAT ? OGPU_WRAP_REPEAT : OGPU_WRAP_CLAMP, t->border, n, lv);
        c->sent_gen = t->generation;
        c->sent_env = env;
        c->sent_envc = envc;
        t->used_in = c->batch_no;
    }
    c->sent_tex = t;
    c->list_at = -1;
}

/* The state the next triangles are drawn with, sent when it changed. */
static void select_state(GLcontext c)
{
    int pass = c->pass;
    struct mgl_tex *t = mgl_tex_unit(c, pass);
    begin(c);
    if (c->clip_dirty) send_clip(c);
    if (t != c->sent_tex || (t && (t->generation != c->sent_gen
        || (pass ? OGPU_ENV_REPLACE : env_of(c->env_mode[0])) != c->sent_env
        || mgl_argb(c->env_color[pass]) != c->sent_envc)) || c->sent_pass != pass) {
        send_texture(c, t, pass);
        c->r3d_dirty = 1;
    }
    if (c->r3d_dirty || c->sent_pass != pass) { send_render3d(c, pass); c->list_at = -1; }
}

/* Room for n vertices in the batch's data area, the state sent; 0 when there is none. */
struct ovtx *mgl_vspace(GLcontext c, ULONG n)
{
    ULONG bytes = n * sizeof(struct ovtx);
    long have = c->body ? c->b.words : 0, cap = BATCH_WORDS - PREFIX_WORDS;
    UBYTE *p;
    if (have + 160 > cap || c->vused + bytes > VERT_BYTES) {
        mgl_flush(c);
        if (bytes > VERT_BYTES) return 0;
    }
    select_state(c);
    if (c->scissor_empty) return 0;
    p = c->vmem + c->vused;
    c->vused += (bytes + 3) & ~3UL;
    return (struct ovtx *)p;
}

/* n vertices from mgl_vspace as a list, strip or fan (OGPU_LAY_). A list
 * right after the last one, with the same state, makes it longer. */
void mgl_emit(GLcontext c, struct ovtx *d, ULONG n, int prim)
{
    ULONG bytes = n * sizeof(struct ovtx);
    int tex = c->sent_tex ? OGPU_TRI_UNIT0 : OGPU_TRI_NOTEX;
    if (!n) return;
    if (prim == OGPU_LAY_LIST && c->list_at >= 0 && ADDR(d) == c->list_vend) {
        UBYTE *cnt = (UBYTE *)(c->cmdmem + PREFIX_WORDS) + (c->list_at + 2) * 4;
        PUTW(cnt, GETW(cnt) + n);
        c->list_vend = ADDR(d) + bytes;
        return;
    }
    if (prim == OGPU_LAY_LIST) { c->list_at = c->b.words; c->list_vend = ADDR(d) + bytes; }
    else c->list_at = -1;
    ogpu_triangles3d(&c->b, ADDR(d), n, sizeof(struct ovtx), 0, 0, 0, tex, 0, OVTX_LAYOUT | (ULONG)prim);
}

/* ---- glClear ------------------------------------------------------------------------------------- */

void mgl_clear(GLcontext c, GLbitfield mask)
{
    long have = c->body ? c->b.words : 0;
    if (have + 64 > BATCH_WORDS - PREFIX_WORDS) mgl_flush(c);
    begin(c);
    if (c->clip_dirty) send_clip(c);
    if (c->scissor_empty) return;
    if (mask & GL_COLOR_BUFFER_BIT) {
        ULONG argb = mgl_argb(c->clear_color), raw = argb;
        if (c->fmt == OGPU_FMT_RGB565) raw = (((argb >> 19) & 31) << 11) | (((argb >> 10) & 63) << 5) | ((argb >> 3) & 31);
        ogpu_fill(&c->b, 0, 0, c->width, c->height, raw);
    }
    if ((mask & GL_DEPTH_BUFFER_BIT) && c->depth_mask) {
        ULONG v = c->clear_depth >= 1.0 ? 0xFFFFFFFFUL : c->clear_depth <= 0.0 ? 0
                : (ULONG)mgl_f2l((float)(c->clear_depth * 1073741823.0)) << 2;
        ogpu_clear3d(&c->b, OGPU_KIND_DEPTH, 0, 0, c->width, c->height, v);
    }
    c->list_at = -1;
}

void GLClear(GLcontext c, GLbitfield mask)
{
    ULONG a = mask;
    MGL_REC(c, OP_CLEAR, 1, &a);
    if (c->prim != PRIM_NONE) { mgl_error(c, GL_INVALID_OPERATION); return; }
    mgl_pipe_flush(c);
    mgl_clear(c, mask);
}

/* ---- reading the picture ------------------------------------------------------------------------- */

/* w x h pixels at x, y (top-left origin) as ARGB32 (native words), after
 * everything drawn so far. */
void mgl_readback(GLcontext c, int x, int y, int w, int h, ULONG *argb, int stride)
{
    int i, j;
    if (c->route == ROUTE_GPU) {
        begin(c);
        ogpu_clear3d(&c->b, OGPU_KIND_COLOUR | OGPU_KIND_READBACK, 0, 0, c->width, c->height, 0);
    }
    mgl_flush(c);
    if (!mgl_disp_lock(c)) return;
    for (j = 0; j < h; j++) {
        int sy = y + j;
        ULONG *out = argb + (ULONG)j * stride;
        for (i = 0; i < w; i++) {
            int sx = x + i;
            ULONG v = 0;
            if (sx >= 0 && sy >= 0 && sx < c->width && sy < c->height) {
                const UBYTE *p = c->base + (ULONG)sy * c->bpr;
                if (c->fmt == OGPU_FMT_RGB565) {
                    ULONG q = ((ULONG)p[sx * 2] << 8) | p[sx * 2 + 1];
                    ULONG r = (q >> 11) & 31, g = (q >> 5) & 63, b = q & 31;
                    v = 0xFF000000UL | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
                } else {
                    v = ((ULONG)p[sx * 4] << 24) | ((ULONG)p[sx * 4 + 1] << 16) | ((ULONG)p[sx * 4 + 2] << 8) | p[sx * 4 + 3];
                    v |= 0xFF000000UL;
                }
            }
            out[i] = v;
        }
    }
    mgl_disp_unlock(c);
}

void GLReadPixels(GLcontext c, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    ULONG *buf, rowbytes;
    int j, i, comps;
    if (!c || !pixels) return;
    if (width < 0 || height < 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    comps = format == GL_RGB || format == GL_BGR ? 3 : format == GL_RGBA || format == GL_BGRA ? 4
          : format == GL_LUMINANCE || format == GL_ALPHA || format == GL_RED || format == GL_GREEN || format == GL_BLUE ? 1
          : format == GL_LUMINANCE_ALPHA ? 2 : 0;
    if (!comps || type != GL_UNSIGNED_BYTE) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (!width || !height) return;
    buf = mgl_alloc((ULONG)width * height * 4);
    if (!buf) { mgl_error(c, GL_OUT_OF_MEMORY); return; }
    mgl_pipe_flush(c);
    /* OpenGL's rows y .. y + height - 1 count up from the bottom */
    mgl_readback(c, x, c->height - y - height, width, height, buf, width);
    rowbytes = ((ULONG)width * comps + (ULONG)c->pack_align - 1) & ~((ULONG)c->pack_align - 1);
    for (j = 0; j < height; j++) {
        UBYTE *out = (UBYTE *)pixels + (ULONG)j * rowbytes;
        const ULONG *row = buf + (ULONG)(height - 1 - j) * width;
        for (i = 0; i < width; i++) {
            ULONG v = row[i];
            UBYTE r = (UBYTE)(v >> 16), g = (UBYTE)(v >> 8), b = (UBYTE)v, a = (UBYTE)(v >> 24);
            switch (format) {
            case GL_RGB: *out++ = r; *out++ = g; *out++ = b; break;
            case GL_BGR: *out++ = b; *out++ = g; *out++ = r; break;
            case GL_RGBA: *out++ = r; *out++ = g; *out++ = b; *out++ = a; break;
            case GL_BGRA: *out++ = b; *out++ = g; *out++ = r; *out++ = a; break;
            case GL_LUMINANCE: *out++ = (UBYTE)((r * 77 + g * 151 + b * 28) >> 8); break;
            case GL_LUMINANCE_ALPHA: *out++ = (UBYTE)((r * 77 + g * 151 + b * 28) >> 8); *out++ = a; break;
            case GL_ALPHA: *out++ = a; break;
            case GL_RED: *out++ = r; break;
            case GL_GREEN: *out++ = g; break;
            default: *out++ = b; break;
            }
        }
    }
    mgl_free(buf);
}

/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library: textures. Every texture is converted once, when it is
 * made or updated, to ARGB32 with all its mip levels, and its meaning
 * (alpha only, luminance, intensity ...) goes with it as an OGPU texture
 * base. The 3D core and a GPU then read one format, and a GPU back end keeps
 * its copy until the texture's generation changes. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "w3d_internal.h"
#include "w3d_calls.h"

static int log2i(int v) { int n = 0; while ((1 << n) < v) n++; return n; }

/* One texel of a Warp3D format as ARGB32 (the base says what it means). */
static ULONG texel_of(ULONG fmt, const UBYTE *p, const ULONG *pal)
{
    ULONG v, r, g, b, a;
    switch (fmt) {
    case W3D_CHUNKY: return pal ? pal[p[0]] : 0xFF000000UL | p[0] * 0x010101UL;
    case W3D_A1R5G5B5:
        v = (p[0] << 8) | p[1];
        r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31;
        return ((v & 0x8000) ? 0xFF000000UL : 0) | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
    case W3D_R5G6B5:
        v = (p[0] << 8) | p[1];
        r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31;
        return 0xFF000000UL | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
    case W3D_R8G8B8: return 0xFF000000UL | (p[0] << 16) | (p[1] << 8) | p[2];
    case W3D_A4R4G4B4:
        a = p[0] >> 4; r = p[0] & 15; g = p[1] >> 4; b = p[1] & 15;
        return (a * 17 << 24) | (r * 17 << 16) | (g * 17 << 8) | b * 17;
    case W3D_A8R8G8B8: return ((ULONG)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
    case W3D_A8: return ((ULONG)p[0] << 24) | 0xFFFFFF;
    case W3D_L8: return 0xFF000000UL | p[0] * 0x010101UL;
    case W3D_L8A8: return ((ULONG)p[1] << 24) | p[0] * 0x010101UL;
    case W3D_I8: return p[0] * 0x01010101UL;
    case W3D_R8G8B8A8: return ((ULONG)p[3] << 24) | (p[0] << 16) | (p[1] << 8) | p[2];
    }
    return 0;
}

static int bytes_of(ULONG fmt)
{
    switch (fmt) {
    case W3D_CHUNKY: case W3D_A8: case W3D_L8: case W3D_I8: return 1;
    case W3D_A1R5G5B5: case W3D_R5G6B5: case W3D_A4R4G4B4: case W3D_L8A8: return 2;
    case W3D_R8G8B8: return 3;
    case W3D_A8R8G8B8: case W3D_R8G8B8A8: return 4;
    }
    return 0;
}

static int base_of(ULONG fmt)
{
    switch (fmt) {
    case W3D_R5G6B5: case W3D_R8G8B8: return OGPU_TEXBASE_RGB;
    case W3D_A8: return OGPU_TEXBASE_ALPHA;
    case W3D_L8: return OGPU_TEXBASE_LUMINANCE;
    case W3D_L8A8: return OGPU_TEXBASE_LUM_ALPHA;
    case W3D_I8: return OGPU_TEXBASE_INTENSITY;
    }
    return OGPU_TEXBASE_RGBA;
}

/* A rectangle of src (in the texture's format, srcbpr bytes a row) into level lv. */
static void convert(W3D_Texture *tex, int lv, const void *src, ULONG srcbpr, int x0, int y0, int w, int h)
{
    struct w3dtex *t = TEX(tex);
    int lw = t->w >> lv, bpp = bytes_of(tex->texfmtsrc), i, j;
    if (lw < 1) lw = 1;
    for (j = 0; j < h; j++) {
        const UBYTE *p = (const UBYTE *)src + j * srcbpr;
        ULONG *q = t->level[lv] + (y0 + j) * lw + x0;
        for (i = 0; i < w; i++, p += bpp) q[i] = texel_of(tex->texfmtsrc, p, t->srcpal);
    }
}

/* Level lv from lv-1, each texel the mean of up to four. */
static void reduce(struct w3dtex *t, int lv)
{
    int pw = t->w >> (lv - 1), ph = t->h >> (lv - 1), w = t->w >> lv, h = t->h >> lv, i, j, k;
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            int x0 = i * 2 < pw ? i * 2 : pw - 1, x1 = i * 2 + 1 < pw ? i * 2 + 1 : x0;
            int y0 = j * 2 < ph ? j * 2 : ph - 1, y1 = j * 2 + 1 < ph ? j * 2 + 1 : y0;
            ULONG a = t->level[lv - 1][y0 * pw + x0], b = t->level[lv - 1][y0 * pw + x1];
            ULONG c = t->level[lv - 1][y1 * pw + x0], d = t->level[lv - 1][y1 * pw + x1], r = 0;
            for (k = 0; k < 32; k += 8)
                r |= ((((a >> k) & 255) + ((b >> k) & 255) + ((c >> k) & 255) + ((d >> k) & 255) + 2) >> 2) << k;
            t->level[lv][j * w + i] = r;
        }
}

/* The levels the library makes (mipmapmask) from level 0, after level 0 changed. */
static void remake_levels(W3D_Texture *tex)
{
    struct w3dtex *t = TEX(tex);
    int lv;
    for (lv = 1; lv < t->levels; lv++)
        if (tex->mipmapmask & (1UL << (lv - 1))) reduce(t, lv);
}

LIBCALL W3D_Texture *LIB_W3D_AllocTexObj(REG(a0, W3D_Context *c), REG(a1, ULONG *error), REG(a2, struct TagItem *tags),
                                         REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    W3D_Texture *tex;
    struct w3dtex *t;
    void *image = (void *)GetTagData(W3D_ATO_IMAGE, 0, tags);
    ULONG fmt = GetTagData(W3D_ATO_FORMAT, 0, tags);
    int w = (int)GetTagData(W3D_ATO_WIDTH, 0, tags), h = (int)GetTagData(W3D_ATO_HEIGHT, 0, tags), lv;
    struct TagItem *mm = FindTagItem(W3D_ATO_MIPMAP, tags);
    void **ptrs = (void **)GetTagData(W3D_ATO_MIPMAPPTRS, 0, tags);
    ULONG e = W3D_SUCCESS;
    (void)x;
    if (error) *error = W3D_SUCCESS;
    if (!image || !bytes_of(fmt)) { e = !image ? W3D_ILLEGALINPUT : W3D_UNSUPPORTEDTEXFMT; goto fail0; }
    if (w < 1 || h < 1 || w > 2048 || h > 2048) { e = W3D_UNSUPPORTEDTEXSIZE; goto fail0; }
    if (fmt == W3D_CHUNKY && !GetTagData(W3D_ATO_PALETTE, 0, tags)) { e = W3D_NOPALETTE; goto fail0; }
    tex = w3d_alloc(sizeof *tex);
    t = w3d_alloc(sizeof *t);
    if (!tex || !t) { w3d_free(tex); w3d_free(t); e = W3D_NOMEMORY; goto fail0; }
    tex->driver = t;
    tex->texsource = image;
    tex->texfmtsrc = (int)fmt;
    tex->palette = (ULONG *)GetTagData(W3D_ATO_PALETTE, 0, tags);
    tex->texwidth = w; tex->texheight = h;
    tex->texwidthexp = log2i(w); tex->texheightexp = log2i(h);
    tex->bytesperpix = 4; tex->bytesperrow = w * 4;
    tex->matchfmt = fmt == W3D_A8R8G8B8;
    tex->resident = W3D_TRUE;
    t->w = w; t->h = h;
    t->base = base_of(fmt);
    t->generation = 1;
    t->min = t->mag = W3D_NEAREST;
    t->wrap_s = t->wrap_t = OGPU_WRAP_REPEAT;
    t->env = W3D_MODULATE;
    t->chroma_mode = W3D_CHROMATEST_NONE;
    if (fmt == W3D_CHUNKY) {
        t->srcpal = w3d_alloc(1024);
        if (!t->srcpal) { e = W3D_NOMEMORY; goto fail; }
        w3d_copy(t->srcpal, tex->palette, 1024);
    }
    t->levels = 1;
    if (mm) {
        int n = log2i(w > h ? w : h) + 1;
        tex->mipmap = W3D_TRUE;
        tex->mipmapmask = mm->ti_Data;
        t->levels = n > OGPU_TEX_LEVELS ? OGPU_TEX_LEVELS : n;
    }
    for (lv = 0; lv < t->levels; lv++) {
        int lw = w >> lv, lh = h >> lv;
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        t->level[lv] = w3d_alloc((ULONG)lw * (ULONG)lh * 4);
        if (!t->level[lv]) { e = W3D_NOMEMORY; goto fail; }
    }
    tex->texdata = t->level[0];
    convert(tex, 0, image, (ULONG)(w * bytes_of(fmt)), 0, 0, w, h);
    if (mm) {
        int k = 0;
        for (lv = 1; lv < t->levels; lv++) {
            if (tex->mipmapmask & (1UL << (lv - 1))) reduce(t, lv);
            else if (ptrs && ptrs[k]) {
                int lw = w >> lv, lh = h >> lv;
                if (lw < 1) lw = 1;
                if (lh < 1) lh = 1;
                tex->mipmaps[lv - 1 < 16 ? lv - 1 : 15] = ptrs[k];
                convert(tex, lv, ptrs[k++], (ULONG)(lw * bytes_of(fmt)), 0, 0, lw, lh);
            } else reduce(t, lv);
        }
    }
    AddTail((struct List *)&c->tex, &tex->link);
    return tex;
fail:
    for (lv = 0; lv < OGPU_TEX_LEVELS; lv++) w3d_free(t->level[lv]);
    w3d_free(t->srcpal);
    w3d_free(t); w3d_free(tex);
fail0:
    if (error) *error = e;
    return 0;
}

void w3d_free_texture(struct w3dctx *x, W3D_Texture *tex)
{
    struct w3dtex *t = TEX(tex);
    int lv;
    if (x->body) w3d_flush(x);                  /* the batch may point at it */
    if (x->sent_tex == tex) x->sent_tex = 0;
    Remove(&tex->link);
    for (lv = 0; lv < OGPU_TEX_LEVELS; lv++) w3d_free(t->level[lv]);
    w3d_free(t->srcpal);
    w3d_free(t);
    w3d_free(tex);
}

LIBCALL void LIB_W3D_FreeTexObj(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(a6, struct W3DBase *libbase))
{
    int i;
    if (!tex) return;
    for (i = 0; i < W3D_MAX_TMU; i++) if (c->CurrentTex[i] == tex) c->CurrentTex[i] = 0;
    w3d_free_texture(CTX(c), tex);
}

LIBCALL ULONG LIB_W3D_FreeAllTexObj(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase))
{
    struct List *l = (struct List *)&c->tex;
    while (l->lh_Head->ln_Succ) LIB_W3D_FreeTexObj(c, (W3D_Texture *)l->lh_Head, libbase);
    return W3D_SUCCESS;
}

/* Textures live in memory every back end reads: nothing to load or release. */
LIBCALL void LIB_W3D_ReleaseTexture(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(a6, struct W3DBase *libbase)) { }
LIBCALL void LIB_W3D_FlushTextures(REG(a0, W3D_Context *c), REG(a6, struct W3DBase *libbase)) { }
LIBCALL ULONG LIB_W3D_UploadTexture(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(a6, struct W3DBase *libbase))
{
    return tex ? W3D_SUCCESS : W3D_NOTEXTURE;
}

LIBCALL ULONG LIB_W3D_PinTexture(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(d0, ULONG pinning), REG(a6, struct W3DBase *libbase))
{
    if (!tex) return W3D_NOTEXTURE;
    tex->pinned = (UWORD)pinning ? W3D_TRUE : W3D_FALSE;
    return W3D_SUCCESS;
}

static void changed(struct w3dctx *x, W3D_Texture *tex)
{
    if (x->sent_tex == tex) { x->tex_dirty = 1; x->list_at = -1; }
}

LIBCALL ULONG LIB_W3D_SetFilter(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(d0, ULONG min), REG(d1, ULONG mag),
                                REG(a6, struct W3DBase *libbase))
{
    struct w3dtex *t;
    if (!tex) return W3D_NOTEXTURE;
    if (min < W3D_NEAREST || min > W3D_LINEAR_MIP_LINEAR || (mag != W3D_NEAREST && mag != W3D_LINEAR))
        return W3D_UNSUPPORTEDFILTER;
    t = TEX(tex);
    if (min > W3D_LINEAR && t->levels < 2) return W3D_NOMIPMAPS;
    t->min = (int)min; t->mag = (int)mag;
    changed(CTX(c), tex);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetTexEnv(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(d1, ULONG env), REG(a2, W3D_Color *colour),
                                REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    ULONG col = colour ? w3d_colour(colour) : 0;
    if (env < W3D_REPLACE || env > W3D_BLEND) return W3D_UNSUPPORTEDTEXENV;
    if ((c->state & W3D_GLOBALTEXENV) || !tex) {
        x->genv = (int)env; x->genvcolour = col;
        c->globaltexenvmode = env;
        if (colour) {
            c->globaltexenvcolor[0] = colour->r; c->globaltexenvcolor[1] = colour->g;
            c->globaltexenvcolor[2] = colour->b; c->globaltexenvcolor[3] = colour->a;
        }
        x->tex_dirty = 1; x->list_at = -1;
        return W3D_SUCCESS;
    }
    TEX(tex)->env = (int)env;
    TEX(tex)->envcolour = col;
    changed(x, tex);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_SetWrapMode(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(d0, ULONG s), REG(d1, ULONG t_),
                                  REG(a2, W3D_Color *border), REG(a6, struct W3DBase *libbase))
{
    struct w3dtex *t;
    if (!tex) return W3D_NOTEXTURE;
    if ((s != W3D_REPEAT && s != W3D_CLAMP) || (t_ != W3D_REPEAT && t_ != W3D_CLAMP)) return W3D_UNSUPPORTEDWRAPMODE;
    t = TEX(tex);
    t->wrap_s = s == W3D_REPEAT ? OGPU_WRAP_REPEAT : OGPU_WRAP_CLAMP;
    t->wrap_t = t_ == W3D_REPEAT ? OGPU_WRAP_REPEAT : OGPU_WRAP_CLAMP;
    t->border = border ? w3d_colour(border) : 0;
    changed(CTX(c), tex);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_UpdateTexSubImage(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(a2, void *image), REG(d1, ULONG level),
                                        REG(a3, ULONG *palette), REG(a4, W3D_Scissor *sc), REG(d0, ULONG srcbpr),
                                        REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    struct w3dtex *t;
    int lw, lh, x0 = 0, y0 = 0, w, h;
    if (!tex) return W3D_NOTEXTURE;
    t = TEX(tex);
    if ((int)level >= t->levels) return level ? W3D_NOMIPMAPS : W3D_ILLEGALINPUT;
    if (x->body) w3d_flush(x);                  /* the pending batch reads the old texels */
    if (palette && t->srcpal) { w3d_copy(t->srcpal, palette, 1024); tex->palette = palette; }
    lw = t->w >> level; lh = t->h >> level;
    if (lw < 1) lw = 1;
    if (lh < 1) lh = 1;
    w = lw; h = lh;
    if (sc) { x0 = sc->left; y0 = sc->top; w = sc->width; h = sc->height; }
    if (x0 < 0 || y0 < 0 || w < 0 || h < 0 || x0 + w > lw || y0 + h > lh) return W3D_ILLEGALINPUT;
    if (!srcbpr) srcbpr = (ULONG)(w * bytes_of(tex->texfmtsrc));
    if (image) convert(tex, (int)level, image, srcbpr, x0, y0, w, h);
    if (!level) { tex->texsource = image; remake_levels(tex); }
    t->generation++;
    changed(x, tex);
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_UpdateTexImage(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(a2, void *image), REG(d1, int level),
                                     REG(a3, ULONG *palette), REG(a6, struct W3DBase *libbase))
{
    return LIB_W3D_UpdateTexSubImage(c, tex, image, (ULONG)level, palette, 0, 0, libbase);
}

LIBCALL ULONG LIB_W3D_SetChromaTestBounds(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(d0, ULONG lower),
                                          REG(d1, ULONG upper), REG(d2, ULONG mode), REG(a6, struct W3DBase *libbase))
{
    struct w3dtex *t;
    if (!tex) return W3D_NOTEXTURE;
    if (mode < W3D_CHROMATEST_NONE || mode > W3D_CHROMATEST_EXCLUSIVE) return W3D_ILLEGALINPUT;
    t = TEX(tex);
    t->chroma_lo = (lower >> 8) | (lower << 24);    /* RGBA to ARGB */
    t->chroma_hi = (upper >> 8) | (upper << 24);
    t->chroma_mode = (int)mode;
    CTX(c)->r3d_dirty = 1;
    CTX(c)->list_at = -1;
    return W3D_SUCCESS;
}

LIBCALL ULONG LIB_W3D_BindTexture(REG(a0, W3D_Context *c), REG(d0, ULONG tmu), REG(a1, W3D_Texture *tex), REG(a6, struct W3DBase *libbase))
{
    if (tmu >= W3D_MAX_TMU) return W3D_ILLEGALINPUT;
    c->CurrentTex[tmu] = tex;
    return W3D_SUCCESS;
}

/* V5: texture combiners. One unit: the environment mode of stage 0 and the
 * blend factor (its colour); combiner arguments ask for more units than the
 * library has (W3D_Q_NUM_BLEND is 1). */
LIBCALL ULONG LIB_W3D_SetTextureBlend(REG(a0, W3D_Context *c), REG(a1, struct TagItem *tags), REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    struct TagItem *ti, *tstate = tags;
    ULONG stage = GetTagData(W3D_BLEND_STAGE, 0, tags), r = W3D_SUCCESS;
    if (stage) return W3D_UNSUPPORTED;
    while ((ti = NextTagItem(&tstate))) {
        switch (ti->ti_Tag) {
        case W3D_BLEND_STAGE: break;
        case W3D_ENV_MODE:
            if (ti->ti_Data < W3D_REPLACE || ti->ti_Data > W3D_OFF) return W3D_UNSUPPORTEDTEXENV;
            x->genv = (int)ti->ti_Data;
            break;
        case W3D_BLEND_FACTOR:
            if (ti->ti_Data) x->genvcolour = w3d_colour((W3D_Color *)ti->ti_Data);
            break;
        default: r = W3D_UNSUPPORTED;
        }
    }
    x->tex_dirty = 1; x->list_at = -1;
    return r;
}

/* V5: draw into a texture. Its ARGB32 level 0 becomes the drawing area. */
LIBCALL ULONG LIB_W3D_SetDrawRegionTexture(REG(a0, W3D_Context *c), REG(a1, W3D_Texture *tex), REG(a2, W3D_Scissor *sc),
                                           REG(a6, struct W3DBase *libbase))
{
    struct w3dctx *x = CTX(c);
    W3D_Bitmap *tb = &x->texbm;
    ULONG e;
    if (!tex) {
        if (!c->orig_drawregion) return W3D_ILLEGALINPUT;
        e = LIB_W3D_SetDrawRegion(c, c->orig_drawregion, (int)c->orig_yoffset, &c->orig_scissor, libbase);
        c->orig_drawregion = 0;
        return e;
    }
    if (!c->orig_drawregion && !c->w3dbitmap) {
        c->orig_drawregion = c->drawregion; c->orig_yoffset = (ULONG)c->yoffset; c->orig_scissor = c->scissor;
        c->orig_width = (ULONG)c->width; c->orig_height = (ULONG)c->height; c->orig_bprow = (ULONG)c->bprow;
    }
    tb->bprow = TEX(tex)->w * 4; tb->width = TEX(tex)->w; tb->height = TEX(tex)->h;
    tb->format = W3D_FMT_A8R8G8B8; tb->dest = TEX(tex)->level[0];
    e = w3d_open_area(x, 0, tb, 0);
    if (e == W3D_SUCCESS && sc) { c->scissor = *sc; x->clip_dirty = 1; }
    TEX(tex)->generation++;
    return e;
}

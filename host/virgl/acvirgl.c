/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ACVirgl's host side (acvirgl.h): OGPU_OP_VIRGL's request blocks carried
 * out with virglrenderer. The calls used are virglrenderer's public API
 * (virglrenderer.h, MIT), declared here as it gives them, and found with
 * dlsym. */
#define _GNU_SOURCE
#include "acvirgl.h"
#include "../../include/opengpu/stream.h"
#include "../../include/opengpu/virgl.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

/* ---- virglrenderer's API, as virglrenderer.h declares it --------------------------- */

#define VR_USE_EGL          1
#define VR_THREAD_SYNC      2
#define VR_USE_SURFACELESS  (1 << 3)
#define VR_USE_GLES         (1 << 4)

struct vr_callbacks {
    int version;
    void (*write_fence)(void *cookie, uint32_t fence);
    void *(*create_gl_context)(void *cookie, int scanout_idx, void *param);
    void (*destroy_gl_context)(void *cookie, void *ctx);
    int (*make_current)(void *cookie, int scanout_idx, void *ctx);
};

struct vr_resource_create_args {
    uint32_t handle, target, format, bind, width, height, depth, array_size, last_level, nr_samples, flags;
};

struct vr_box { uint32_t x, y, z, w, h, d; };

struct vr_api {
    int (*init)(void *cookie, int flags, struct vr_callbacks *cb);
    void (*cleanup)(void *cookie);
    void (*get_cap_set)(uint32_t set, uint32_t *max_ver, uint32_t *max_size);
    void (*fill_caps)(uint32_t set, uint32_t version, void *caps);
    int (*context_create)(uint32_t handle, uint32_t nlen, const char *name);
    void (*context_destroy)(uint32_t handle);
    int (*resource_create)(struct vr_resource_create_args *args, struct iovec *iov, uint32_t num_iovs);
    void (*resource_unref)(uint32_t handle);
    void (*ctx_attach_resource)(int ctx_id, int res_handle);
    void (*ctx_detach_resource)(int ctx_id, int res_handle);
    int (*submit_cmd)(void *buffer, int ctx_id, int ndw);
    int (*transfer_read_iov)(uint32_t handle, uint32_t ctx_id, uint32_t level, uint32_t stride, uint32_t layer_stride,
                             struct vr_box *box, uint64_t offset, struct iovec *iov, int iovec_cnt);
    int (*transfer_write_iov)(uint32_t handle, uint32_t ctx_id, int level, uint32_t stride, uint32_t layer_stride,
                              struct vr_box *box, uint64_t offset, struct iovec *iovec, unsigned int iovec_cnt);
};

/* What ACV_PRESENT and the counters need of a resource, by handle. On the
 * Amiga a handle is the address of GLA's resource (gla_virgl.c), so the
 * handles are sparse 32-bit numbers: a hash table, open addressing,
 * handle 0 an empty slot. */
struct av_res { uint32_t handle, format, bind, width, height; };
#define AV_MAX_RES (1u << 20)               /* resources known at once */

struct acvirgl {
    void *dl;
    struct vr_api vr;
    struct vr_callbacks cb;
    uint8_t *tmp;               /* commands and pixels, in the host's order */
    size_t tmp_size;
    struct av_res *res;         /* the table: res_cap slots, a power of 2 */
    uint32_t res_cap, res_used;
    struct acvirgl_stats st;
    const char *api;            /* "GL" or "GLES": the context virglrenderer got */
    char renderer[64];          /* and the GPU it draws on, from capset 2 */
};

static void av_no_fence(void *cookie, uint32_t fence) { (void)cookie; (void)fence; }

static int av_need(struct acvirgl *v, size_t n)
{
    uint8_t *p;
    if (n <= v->tmp_size) return 1;
    if (!(p = realloc(v->tmp, n + 65536))) return 0;
    v->tmp = p;
    v->tmp_size = n + 65536;
    return 1;
}

/* The host's renderer as virglrenderer tells the guest: capset 2 (struct
 * virgl_caps_v2 in virglrenderer's virgl_hw.h, an ABI shared with Mesa)
 * carries the host GL_RENDERER at byte 696, 64 bytes, e.g. "V3D 7.1". */
#define AV_CAPS_RENDERER 696
static void av_renderer(struct acvirgl *v)
{
    uint32_t mv = 0, ms = 0;
    v->vr.get_cap_set(2, &mv, &ms);
    if (ms < AV_CAPS_RENDERER + sizeof v->renderer || !av_need(v, ms + 4096)) return;
    memset(v->tmp, 0, ms + 4096);
    v->vr.fill_caps(2, mv, v->tmp);
    memcpy(v->renderer, v->tmp + AV_CAPS_RENDERER, sizeof v->renderer - 1);
}

struct acvirgl *acvirgl_create(const char *lib, char *err, int errlen)
{
    struct acvirgl *v = calloc(1, sizeof *v);
    const char *path = lib ? lib : getenv("ACVIRGL_LIB"), *drv, *gles;
    if (!v) { snprintf(err, (size_t)errlen, "no memory"); return NULL; }
    if (!path) { snprintf(err, (size_t)errlen, "no virglrenderer (ACVIRGL_LIB not set)"); free(v); return NULL; }
    if (!(v->dl = dlopen(path, RTLD_NOW | RTLD_LOCAL))) { snprintf(err, (size_t)errlen, "%s", dlerror()); free(v); return NULL; }
#define SYM(field, name) if (!(*(void **)&v->vr.field = dlsym(v->dl, name))) { snprintf(err, (size_t)errlen, "%s: no %s", path, name); goto fail; }
    SYM(init, "virgl_renderer_init")
    SYM(cleanup, "virgl_renderer_cleanup")
    SYM(get_cap_set, "virgl_renderer_get_cap_set")
    SYM(fill_caps, "virgl_renderer_fill_caps")
    SYM(context_create, "virgl_renderer_context_create")
    SYM(context_destroy, "virgl_renderer_context_destroy")
    SYM(resource_create, "virgl_renderer_resource_create")
    SYM(resource_unref, "virgl_renderer_resource_unref")
    SYM(ctx_attach_resource, "virgl_renderer_ctx_attach_resource")
    SYM(ctx_detach_resource, "virgl_renderer_ctx_detach_resource")
    SYM(submit_cmd, "virgl_renderer_submit_cmd")
    SYM(transfer_read_iov, "virgl_renderer_transfer_read_iov")
    SYM(transfer_write_iov, "virgl_renderer_transfer_write_iov")
#undef SYM
    v->cb.version = 1;
    v->cb.write_fence = av_no_fence;
    /* AC_VIRGL_DRIVER names Mesa's driver for the context, e.g. zink (GL
     * over the host's Vulkan, as on a Pi 5's V3DV); unset: the GPU's own. */
    if ((drv = getenv("AC_VIRGL_DRIVER")) && *drv) setenv("MESA_LOADER_DRIVER_OVERRIDE", drv, 1);
    /* Desktop GL, else GLES: a Pi's V3D gives GLES 3.1 where its desktop GL
     * may not make the context virglrenderer asks for. AC_VIRGL_GLES=1
     * asks for GLES only, AC_VIRGL_GLES=0 for desktop GL only. */
    gles = getenv("AC_VIRGL_GLES");
    if ((!gles || strcmp(gles, "1")) && !v->vr.init(v, VR_USE_EGL | VR_USE_SURFACELESS, &v->cb)) v->api = "GL";
    else if ((!gles || strcmp(gles, "0")) && !v->vr.init(v, VR_USE_EGL | VR_USE_SURFACELESS | VR_USE_GLES, &v->cb)) v->api = "GLES";
    else {
        snprintf(err, (size_t)errlen, "virgl_renderer_init failed (no EGL with surfaceless %s contexts?)",
                 !gles ? "GL or GLES" : !strcmp(gles, "1") ? "GLES" : "GL");
        goto fail;
    }
    av_renderer(v);
    return v;
fail:
    dlclose(v->dl);
    free(v);
    return NULL;
}

void acvirgl_destroy(struct acvirgl *v)
{
    if (!v) return;
    v->vr.cleanup(v);
    /* virglrenderer keeps state at file scope; it stays loaded */
    free(v->tmp);
    free(v->res);
    free(v);
}

void acvirgl_stats(const struct acvirgl *v, struct acvirgl_stats *s) { *s = v->st; }

const char *acvirgl_api(const struct acvirgl *v) { return v->api; }
const char *acvirgl_renderer(const struct acvirgl *v) { return v->renderer[0] ? v->renderer : "unknown"; }

/* ---- the block ------------------------------------------------------------------------ */

static uint32_t av_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void av_put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* n bytes from s to d, each unit (1, 2, 4 or 8 bytes) turned round. */
static void av_swap_copy(uint8_t *d, const uint8_t *s, size_t n, uint32_t unit)
{
    size_t i;
    switch (unit) {
    case ACV_SWAP_16:
        for (i = 0; i + 1 < n; i += 2) { d[i] = s[i + 1]; d[i + 1] = s[i]; }
        break;
    case ACV_SWAP_32:
        for (i = 0; i + 3 < n; i += 4) { d[i] = s[i + 3]; d[i + 1] = s[i + 2]; d[i + 2] = s[i + 1]; d[i + 3] = s[i]; }
        break;
    case ACV_SWAP_64:
        for (i = 0; i + 7 < n; i += 8) { int k; for (k = 0; k < 8; k++) d[i + k] = s[i + 7 - k]; }
        break;
    default:
        i = 0;
        break;
    }
    if (i < n) memcpy(d + i, s + i, n - i);   /* units 1 (or what is left over) as they are */
}

/* virgl_hw.h's numbers that ACV_PRESENT and the counters use. */
#define AV_BIND_DISPLAY_TARGET (1u << 7)
#define AV_BIND_SCANOUT        (1u << 18)

static uint32_t av_res_slot(const struct acvirgl *v, uint32_t handle)
{
    return (handle * 0x9E3779B1u) >> 7 & (v->res_cap - 1);
}

/* The resource with this handle, or NULL. */
static const struct av_res *av_res_find(const struct acvirgl *v, uint32_t handle)
{
    uint32_t i;
    if (!handle || !v->res_cap) return NULL;
    for (i = av_res_slot(v, handle); v->res[i].handle; i = (i + 1) & (v->res_cap - 1))
        if (v->res[i].handle == handle) return &v->res[i];
    return NULL;
}

static void av_res_put(struct acvirgl *v, const struct av_res *r)
{
    uint32_t i = av_res_slot(v, r->handle);
    while (v->res[i].handle && v->res[i].handle != r->handle) i = (i + 1) & (v->res_cap - 1);
    if (!v->res[i].handle) v->res_used++;
    v->res[i] = *r;
}

static void av_res_set(struct acvirgl *v, uint32_t handle, const struct av_res *r)
{
    struct av_res e = *r;
    if (!handle) return;                            /* ACV_PRESENT then says no for it */
    if ((v->res_used + 1) * 2 > v->res_cap) {       /* at most half full */
        struct av_res *old = v->res, *p;
        uint32_t n = v->res_cap ? v->res_cap * 2 : 256, i, oc = v->res_cap;
        if (n > 2 * AV_MAX_RES || !(p = calloc(n, sizeof *p))) return;
        v->res = p; v->res_cap = n; v->res_used = 0;
        for (i = 0; i < oc; i++) if (old[i].handle) av_res_put(v, &old[i]);
        free(old);
    }
    e.handle = handle;
    av_res_put(v, &e);
}

/* Takes the handle out, moving back the entries after it that its slot
 * displaced (linear probing's deletion, no tombstones). */
static void av_res_drop(struct acvirgl *v, uint32_t handle)
{
    uint32_t i, j, k, m = v->res_cap - 1;
    const struct av_res *f = av_res_find(v, handle);
    if (!f) return;
    i = (uint32_t)(f - v->res);
    v->res[i].handle = 0;
    v->res_used--;
    for (j = (i + 1) & m; v->res[j].handle; j = (j + 1) & m) {
        k = av_res_slot(v, v->res[j].handle);
        /* j's entry can fill the hole at i unless its home k lies cyclically in (i, j] */
        if ((j > i && (k <= i || k > j)) || (j < i && k <= i && k > j)) {
            v->res[i] = v->res[j];
            v->res[j].handle = 0;
            i = j;
        }
    }
}

/* Where R, G, B and A are in a pixel of a 32-bit colour format, as the
 * host reads it back (4 for A: none, opaque); 0 for formats ACV_PRESENT
 * doesn't take. */
static int av_layout(uint32_t format, int *r, int *g, int *b, int *a)
{
    static const struct { uint8_t f, r, g, b, a; } t[] = {
        { 1, 2, 1, 0, 3 },      /* B8G8R8A8 */
        { 2, 2, 1, 0, 4 },      /* B8G8R8X8 */
        { 3, 1, 2, 3, 0 },      /* A8R8G8B8 */
        { 4, 1, 2, 3, 4 },      /* X8R8G8B8 */
        { 67, 0, 1, 2, 3 },     /* R8G8B8A8 */
        { 68, 3, 2, 1, 4 },     /* X8B8G8R8 */
        { 121, 3, 2, 1, 0 },    /* A8B8G8R8 */
        { 134, 0, 1, 2, 4 },    /* R8G8B8X8 */
    };
    unsigned i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++)
        if (t[i].f == format) { *r = t[i].r; *g = t[i].g; *b = t[i].b; *a = t[i].a; return 1; }
    return 0;
}

static int av_s16(uint32_t v) { v &= 0xFFFFu; return v >= 0x8000u ? (int)v - 0x10000 : (int)v; }

/* ACV_PRESENT: the resource read back once, then written into the target's
 * rectangles in its own format. */
static int32_t av_present(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, const uint8_t *a, unsigned nargs)
{
    uint32_t ctx = av_be32(a), handle = av_be32(a + 4), layer = av_be32(a + 8), so = av_be32(a + 12);
    uint32_t addr = av_be32(a + 16), bpr = av_be32(a + 20), fmt = av_be32(a + 24), wh = av_be32(a + 28);
    uint32_t flags = av_be32(a + 32), n = av_be32(a + 36), i, bpp;
    int sx = av_s16(so >> 16), sy = av_s16(so), tw = (int)(wh >> 16), th = (int)(wh & 0xFFFFu);
    int lr, lg, lb, la, rw, rh, bx0 = 0x7FFFFFFF, by0 = 0x7FFFFFFF, bx1 = -1, by1 = -1, flip = (flags & ACV_PRESENT_FLIP_Y) != 0;
    int rect[ACV_PRESENT_MAX][4], nr = 0;
    const struct av_res *res;
    struct vr_box box;
    struct iovec iov;
    uint8_t *dst;
    uint64_t pixels = 0;
    if (n > ACV_PRESENT_MAX || nargs < 10 + 2 * n || (flags & ~(uint32_t)ACV_PRESENT_FLIP_Y)) return ACV_ERR_BADCMD;
    bpp = fmt == OGPU_FMT_XRGB32 || fmt == OGPU_FMT_ARGB32 ? 4 : fmt == OGPU_FMT_RGB565 ? 2 : 0;
    if (!bpp) return ACV_ERR_BADCMD;
    if (!n || !tw || !th) return 0;
    if (bpr < (uint32_t)tw * bpp || bpr > 0x01000000u) return ACV_ERR_BADCMD;
    if (!(res = av_res_find(v, handle)) || !av_layout(res->format, &lr, &lg, &lb, &la))
        return ACV_ERR_RENDERER;
    rw = (int)res->width; rh = (int)res->height;
    if (!(dst = map(mu, addr, bpr * (uint32_t)(th - 1) + (uint32_t)tw * bpp))) return ACV_ERR_NOMAP;
    /* Each rectangle inside the target and over the resource; their box in the resource. */
    for (i = 0; i < n; i++) {
        uint32_t xy = av_be32(a + 40 + i * 8), rwh = av_be32(a + 44 + i * 8);
        int x0 = av_s16(xy >> 16), y0 = av_s16(xy), x1 = x0 + (int)(rwh >> 16), y1 = y0 + (int)(rwh & 0xFFFFu);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > tw) x1 = tw;
        if (y1 > th) y1 = th;
        if (x0 < sx) x0 = sx;
        if (y0 < sy) y0 = sy;
        if (x1 > sx + rw) x1 = sx + rw;
        if (y1 > sy + rh) y1 = sy + rh;
        if (x0 >= x1 || y0 >= y1) continue;
        rect[nr][0] = x0; rect[nr][1] = y0; rect[nr][2] = x1; rect[nr][3] = y1; nr++;
        if (x0 - sx < bx0) bx0 = x0 - sx;
        if (x1 - sx > bx1) bx1 = x1 - sx;
        if (flip) {
            if (rh - (y1 - sy) < by0) by0 = rh - (y1 - sy);
            if (rh - (y0 - sy) > by1) by1 = rh - (y0 - sy);
        } else {
            if (y0 - sy < by0) by0 = y0 - sy;
            if (y1 - sy > by1) by1 = y1 - sy;
        }
    }
    if (!nr) return 0;
    box.x = (uint32_t)bx0; box.y = (uint32_t)by0; box.z = layer;
    box.w = (uint32_t)(bx1 - bx0); box.h = (uint32_t)(by1 - by0); box.d = 1;
    if (!av_need(v, (size_t)box.w * box.h * 4)) return ACV_ERR_RENDERER;
    iov.iov_base = v->tmp;
    iov.iov_len = (size_t)box.w * box.h * 4;
    if (v->vr.transfer_read_iov(handle, ctx, 0, box.w * 4, 0, &box, 0, &iov, 1)) return ACV_ERR_RENDERER;
    for (i = 0; i < (uint32_t)nr; i++) {
        int x0 = rect[i][0], y0 = rect[i][1], x1 = rect[i][2], y1 = rect[i][3], x, y;
        for (y = y0; y < y1; y++) {
            int ry = flip ? rh - 1 - (y - sy) : y - sy;
            const uint8_t *sp = v->tmp + ((size_t)(ry - by0) * box.w + (size_t)(x0 - sx - bx0)) * 4;
            uint8_t *dp = dst + (size_t)y * bpr + (size_t)x0 * bpp;
            if (fmt == OGPU_FMT_RGB565)
                for (x = x0; x < x1; x++, sp += 4, dp += 2) {
                    dp[0] = (uint8_t)((sp[lr] & 0xF8) | (sp[lg] >> 5));
                    dp[1] = (uint8_t)(((sp[lg] & 0x1C) << 3) | (sp[lb] >> 3));
                }
            else
                for (x = x0; x < x1; x++, sp += 4, dp += 4) {
                    dp[0] = fmt == OGPU_FMT_XRGB32 ? 0 : la == 4 ? 0xFF : sp[la];
                    dp[1] = sp[lr]; dp[2] = sp[lg]; dp[3] = sp[lb];
                }
        }
        pixels += (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
    }
    v->st.presents++; v->st.present_rects += (uint64_t)nr; v->st.present_pixels += pixels;
    return pixels > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)pixels;
}

/* The words a command has after its header and result word. */
static int av_args_of(unsigned cmd)
{
    switch (cmd) {
    case ACV_HELLO: return 1;
    case ACV_CAPSET_INFO: return 3;
    case ACV_CAPS: return 4;
    case ACV_CTX_CREATE: return 3;
    case ACV_CTX_DESTROY: return 1;
    case ACV_RES_CREATE: return 12;
    case ACV_RES_UNREF: return 1;
    case ACV_ATTACH: case ACV_DETACH: return 2;
    case ACV_SUBMIT: return 3;
    case ACV_PUT: case ACV_GET: return 14;
    case ACV_PRESENT: return 10;            /* and two a rectangle */
    }
    return -1;
}

static int32_t av_transfer(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, const uint8_t *a, int get)
{
    uint32_t handle = av_be32(a), ctx = av_be32(a + 4), level = av_be32(a + 8), stride = av_be32(a + 12), layer_stride = av_be32(a + 16);
    struct vr_box box;
    uint32_t addr = av_be32(a + 44), bytes = av_be32(a + 48), unit = av_be32(a + 52);
    uint8_t *mem;
    struct iovec iov;
    box.x = av_be32(a + 20); box.y = av_be32(a + 24); box.z = av_be32(a + 28);
    box.w = av_be32(a + 32); box.h = av_be32(a + 36); box.d = av_be32(a + 40);
    if (!bytes) return ACV_OK;
    if (!(mem = map(mu, addr, bytes))) return ACV_ERR_NOMAP;
    if (!av_need(v, bytes)) return ACV_ERR_RENDERER;
    iov.iov_base = v->tmp;
    iov.iov_len = bytes;
    if (get) {
        if (v->vr.transfer_read_iov(handle, ctx, level, stride, layer_stride, &box, 0, &iov, 1)) return ACV_ERR_RENDERER;
        av_swap_copy(mem, v->tmp, bytes, unit);
        v->st.gets++; v->st.get_bytes += bytes;
        {
            const struct av_res *res = av_res_find(v, handle);
            if (res && (res->bind & (AV_BIND_DISPLAY_TARGET | AV_BIND_SCANOUT))) v->st.frame_get_bytes += bytes;
        }
    } else {
        av_swap_copy(v->tmp, mem, bytes, unit);
        if (v->vr.transfer_write_iov(handle, ctx, (int)level, stride, layer_stride, &box, 0, &iov, 1)) return ACV_ERR_RENDERER;
        v->st.puts++; v->st.put_bytes += bytes;
    }
    return ACV_OK;
}

int acvirgl_run(struct acvirgl *v, uint8_t *(*map)(void *, uint32_t, uint32_t), void *mu, uint32_t addr, uint32_t bytes)
{
    uint8_t *blk;
    uint32_t at = 0, words = bytes / 4;
    if (!v) return OGPU_ERR_BADOP;
    if (!words || !(blk = map(mu, addr, words * 4))) return OGPU_ERR_NOMAP;
    v->st.blocks++;
    while (at + 2 <= words) {
        uint8_t *c = blk + at * 4, *a = c + 8;
        uint32_t hdr = av_be32(c);
        unsigned cmd = ACV_HDR_CMD(hdr), len = ACV_HDR_WORDS(hdr);
        int n = av_args_of(cmd);
        int32_t r = ACV_OK;
        if (len < 2 || at + len > words || n < 0 || len - 2 < (unsigned)n) {
            av_put_be32(c + 4, (uint32_t)ACV_ERR_BADCMD);
            v->st.errors++;
            return OGPU_OK;                     /* the rest can't be trusted */
        }
        switch (cmd) {
        case ACV_HELLO:
            av_put_be32(a, ACV_HOST_PRESENT);
            r = ACV_PROTOCOL;
            break;
        case ACV_CAPSET_INFO: {
            uint32_t mv = 0, ms = 0;
            v->vr.get_cap_set(av_be32(a), &mv, &ms);
            av_put_be32(a + 4, mv); av_put_be32(a + 8, ms);
            break;
        }
        case ACV_CAPS: {
            uint32_t set = av_be32(a), ver = av_be32(a + 4), to = av_be32(a + 8), max = av_be32(a + 12), mv = 0, ms = 0, i;
            uint8_t *mem;
            v->vr.get_cap_set(set, &mv, &ms);
            if (ms > max) ms = max;
            ms &= ~3u;
            if (!ms) break;
            if (!(mem = map(mu, to, ms)) || !av_need(v, ms + 4096)) { r = ACV_ERR_NOMAP; break; }
            memset(v->tmp, 0, ms + 4096);
            v->vr.fill_caps(set, ver, v->tmp);
            for (i = 0; i < ms; i += 4) {           /* each word into the Amiga's order */
                uint32_t w;
                memcpy(&w, v->tmp + i, 4);
                av_put_be32(mem + i, w);
            }
            r = (int32_t)ms;
            break;
        }
        case ACV_CTX_CREATE: {
            uint32_t id = av_be32(a), name = av_be32(a + 4), nlen = av_be32(a + 8);
            const uint8_t *s = nlen ? map(mu, name, nlen) : NULL;
            char buf[64];
            if (nlen > sizeof buf - 1) nlen = sizeof buf - 1;
            if (s) memcpy(buf, s, nlen);
            buf[s ? nlen : 0] = 0;
            if (v->vr.context_create(id, (uint32_t)strlen(buf), buf)) r = ACV_ERR_RENDERER;
            break;
        }
        case ACV_CTX_DESTROY: v->vr.context_destroy(av_be32(a)); break;
        case ACV_RES_CREATE: {
            struct vr_resource_create_args ra;
            uint32_t ctx = av_be32(a + 44);
            ra.handle = av_be32(a); ra.target = av_be32(a + 4); ra.format = av_be32(a + 8); ra.bind = av_be32(a + 12);
            ra.width = av_be32(a + 16); ra.height = av_be32(a + 20); ra.depth = av_be32(a + 24); ra.array_size = av_be32(a + 28);
            ra.last_level = av_be32(a + 32); ra.nr_samples = av_be32(a + 36); ra.flags = av_be32(a + 40);
            if (v->vr.resource_create(&ra, NULL, 0)) r = ACV_ERR_RENDERER;
            else {
                struct av_res e;
                e.format = ra.format; e.bind = ra.bind; e.width = ra.width; e.height = ra.height;
                av_res_set(v, ra.handle, &e);
                if (ctx) v->vr.ctx_attach_resource((int)ctx, (int)ra.handle);
            }
            break;
        }
        case ACV_RES_UNREF: {
            uint32_t h = av_be32(a);
            v->vr.resource_unref(h);
            av_res_drop(v, h);
            break;
        }
        case ACV_ATTACH: v->vr.ctx_attach_resource((int)av_be32(a), (int)av_be32(a + 4)); break;
        case ACV_DETACH: v->vr.ctx_detach_resource((int)av_be32(a), (int)av_be32(a + 4)); break;
        case ACV_SUBMIT: {
            uint32_t ctx = av_be32(a), cmds = av_be32(a + 4), ndw = av_be32(a + 8);
            const uint8_t *s;
            if (!ndw) break;
            if (ndw > 0x01000000u || !(s = map(mu, cmds, ndw * 4))) { r = ACV_ERR_NOMAP; break; }
            if (!av_need(v, (size_t)ndw * 4)) { r = ACV_ERR_RENDERER; break; }
            memcpy(v->tmp, s, (size_t)ndw * 4);     /* already little-endian, as Mesa's encoder wrote it */
            if (v->vr.submit_cmd(v->tmp, (int)ctx, (int)ndw)) r = ACV_ERR_RENDERER;
            v->st.submits++; v->st.submit_words += ndw;
            break;
        }
        case ACV_PUT: r = av_transfer(v, map, mu, a, 0); break;
        case ACV_GET: r = av_transfer(v, map, mu, a, 1); break;
        case ACV_PRESENT: r = av_present(v, map, mu, a, len - 2); break;
        }
        if (r < 0) v->st.errors++;
        av_put_be32(c + 4, (uint32_t)r);
        at += len;
    }
    return OGPU_OK;
}

/* Copyright (c) 2026 Dalsin Limited. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GLA core's virgl winsys: Mesa's virgl driver (Gallium) on OpenGPU.
 * Every call virgl makes of its winsys becomes ACVirgl commands
 * (include/opengpu/virgl.h) in a request block, which the transport hands
 * to the host: on the Amiga one OGPU_OP_VIRGL batch on the ACRTG board's
 * ring, which the Cradle's runtime gives to virglrenderer; in the host
 * tests a direct call of acvirgl_run.
 *
 * The host draws in its own memory; each resource also has a copy in memory
 * the host reaches (Fast RAM on the Amiga), which virgl maps, and PUT and
 * GET move pixels between the two, turning units round on a big-endian
 * Amiga: a texture's by its format's channel or pixel size, a buffer's by
 * 32 bits (floats). Everything is done by the time the block returns, so
 * nothing is ever busy and fences are already signalled.
 *
 * Written for openamigamesa's GLA core (8 October 2026), after virgl's own
 * winsys interface (virgl_winsys.h); Mesa's vtest winsys was the reference
 * for what each call must do. */
#include "gla_virgl.h"

#include <string.h>

#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "frontend/sw_winsys.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_math.h"
#include "c11/threads.h"
#include "util/u_endian.h"
#include "util/box.h"
#include "util/u_surface.h"
#include "virgl/virgl_winsys.h"
#include "virtio-gpu/virgl_hw.h"
#include "virtio-gpu/virgl_protocol.h"
#include "opengpu/virgl.h"

/* Words in the Amiga's order (big-endian), as a request block has them. */
#if UTIL_ARCH_BIG_ENDIAN
#define util_cpu_to_be32(x) (x)
#else
#define util_cpu_to_be32(x) util_bswap32(x)
#endif
#define util_be32_to_cpu(x) util_cpu_to_be32(x)

#define CTX_ID 1
#define BLK_WORDS 64

struct gv_ws {
    struct virgl_winsys base;
    struct sw_winsys *sws;
    struct gla_virgl_transport t;
    uint32_t *blk;              /* the request block, in memory the host reaches */
    unsigned at;                /* words in it */
    uint32_t next_handle;
    mtx_t mutex;
};

struct virgl_hw_res {
    struct pipe_reference reference;
    uint32_t res_handle;
    int num_cs_references;
    uint8_t *ptr;               /* the copy the host reaches */
    uint32_t size;
    enum pipe_format format;
    uint32_t target, bind, width, height, stride;
    struct sw_displaytarget *dt;
    unsigned dt_stride;         /* its rows: the display target's are 64-byte aligned */
};

struct gv_cmd_buf {
    struct virgl_cmd_buf base;
    struct gv_ws *ws;
    struct virgl_hw_res **res;
    unsigned nres, cres;
};

struct gv_fence { struct pipe_reference reference; };

static struct gv_ws *gv(struct virgl_winsys *w) { return (struct gv_ws *)w; }

/* ---- request blocks ---- */

static void blk_begin(struct gv_ws *w) { w->at = 0; }

/* A command; returns where its result word is. */
static unsigned blk_cmd(struct gv_ws *w, unsigned cmd, unsigned n, const uint32_t *args)
{
    unsigned here = w->at, i;
    w->blk[w->at++] = util_cpu_to_be32(ACV_HDR(cmd, n + 2));
    w->blk[w->at++] = 0;
    for (i = 0; i < n; i++) w->blk[w->at++] = util_cpu_to_be32(args[i]);
    return here + 1;
}

static int32_t blk_result(struct gv_ws *w, unsigned at) { return (int32_t)util_be32_to_cpu(w->blk[at]); }
static uint32_t blk_word(struct gv_ws *w, unsigned at) { return util_be32_to_cpu(w->blk[at]); }

static int blk_run(struct gv_ws *w)
{
    int r = w->t.run(w->t.user, w->blk, w->at * 4);
    w->at = 0;
    return r;
}

/* ---- byte order ---- */

/* The units a transfer turns round: none on a little-endian Amiga stand-in;
 * on a big-endian one a buffer's floats, or a texture's channel size (array
 * formats) or pixel size (packed ones). */
static uint32_t swap_unit(const struct virgl_hw_res *res)
{
#if UTIL_ARCH_BIG_ENDIAN
    const struct util_format_description *d;
    if (res->target == PIPE_BUFFER) return ACV_SWAP_32;
    d = util_format_description(res->format);
    if (!d || d->layout != UTIL_FORMAT_LAYOUT_PLAIN) return ACV_SWAP_NONE;
    if (d->is_array) {
        unsigned bits = d->channel[0].size;
        return bits == 16 ? ACV_SWAP_16 : bits == 32 ? ACV_SWAP_32 : bits == 64 ? ACV_SWAP_64 : ACV_SWAP_NONE;
    }
    return d->block.bits == 16 ? ACV_SWAP_16 : d->block.bits == 32 ? ACV_SWAP_32 : d->block.bits == 64 ? ACV_SWAP_32 : ACV_SWAP_NONE;
#else
    (void)res;
    return ACV_SWAP_NONE;
#endif
}

/* ---- resources ---- */

static uint32_t transfer_size(struct virgl_hw_res *res, const struct pipe_box *box, uint32_t stride, uint32_t layer_stride)
{
    uint32_t vs = util_format_get_stride(res->format, box->width), vls;
    if (stride && box->height > 1) vs = stride;
    vls = util_format_get_2d_size(res->format, vs, box->height);
    if (layer_stride && box->depth > 1) vls = layer_stride;
    return vls * box->depth;
}

static int transfer(struct gv_ws *w, struct virgl_hw_res *res, const struct pipe_box *box, uint32_t stride,
                    uint32_t layer_stride, uint32_t buf_offset, uint32_t level, int get, uint32_t unit)
{
    uint32_t a[14];
    unsigned r;
    int rc;
    a[0] = res->res_handle; a[1] = CTX_ID; a[2] = level; a[3] = stride; a[4] = layer_stride;
    a[5] = box->x; a[6] = box->y; a[7] = box->z; a[8] = box->width; a[9] = box->height; a[10] = box->depth;
    a[11] = w->t.addr(w->t.user, res->ptr + buf_offset);
    a[12] = transfer_size(res, box, stride, layer_stride);
    a[13] = unit;
    if (buf_offset + a[12] > res->size) a[12] = res->size > buf_offset ? res->size - buf_offset : 0;
    mtx_lock(&w->mutex);
    blk_begin(w);
    r = blk_cmd(w, get ? ACV_GET : ACV_PUT, 14, a);
    rc = blk_run(w);
    if (!rc) rc = blk_result(w, r);
    mtx_unlock(&w->mutex);
    return rc;
}

static int gv_transfer_put(struct virgl_winsys *vws, struct virgl_hw_res *res, const struct pipe_box *box,
                           uint32_t stride, uint32_t layer_stride, uint32_t buf_offset, uint32_t level)
{
    return transfer(gv(vws), res, box, stride, layer_stride, buf_offset, level, 0, swap_unit(res));
}

static int gv_transfer_get(struct virgl_winsys *vws, struct virgl_hw_res *res, const struct pipe_box *box,
                           uint32_t stride, uint32_t layer_stride, uint32_t buf_offset, uint32_t level)
{
    return transfer(gv(vws), res, box, stride, layer_stride, buf_offset, level, 1, swap_unit(res));
}

static void res_destroy(struct gv_ws *w, struct virgl_hw_res *res)
{
    uint32_t a[1] = { res->res_handle };
    mtx_lock(&w->mutex);
    blk_begin(w);
    blk_cmd(w, ACV_RES_UNREF, 1, a);
    blk_run(w);
    mtx_unlock(&w->mutex);
    if (res->dt) w->sws->displaytarget_destroy(w->sws, res->dt);
    if (res->ptr) w->t.free(w->t.user, res->ptr);
    FREE(res);
}

static struct virgl_hw_res *gv_resource_create(struct virgl_winsys *vws, enum pipe_texture_target target,
                                               const void *map_front_private, uint32_t format, uint32_t bind,
                                               uint32_t width, uint32_t height, uint32_t depth, uint32_t array_size,
                                               uint32_t last_level, uint32_t nr_samples, uint32_t flags, uint32_t size)
{
    struct gv_ws *w = gv(vws);
    struct virgl_hw_res *res = CALLOC_STRUCT(virgl_hw_res);
    uint32_t a[12];
    unsigned r;
    int rc;
    if (!res) return NULL;
    res->format = (enum pipe_format)format;
    res->target = target; res->bind = bind; res->width = width; res->height = height;
    res->size = size;
    res->stride = util_format_get_stride(res->format, width);
    if (bind & (VIRGL_BIND_DISPLAY_TARGET | VIRGL_BIND_SCANOUT)) {
        res->dt = w->sws->displaytarget_create(w->sws, bind, res->format, width, height, 64, map_front_private, &res->dt_stride);
        if (!res->dt) { FREE(res); return NULL; }
    }
    if (size && !(res->ptr = w->t.alloc(w->t.user, size))) {
        if (res->dt) w->sws->displaytarget_destroy(w->sws, res->dt);
        FREE(res);
        return NULL;
    }
    mtx_lock(&w->mutex);
    res->res_handle = w->next_handle++;
    a[0] = res->res_handle; a[1] = target; a[2] = pipe_to_virgl_format(res->format); a[3] = bind;
    a[4] = width; a[5] = height; a[6] = depth; a[7] = array_size; a[8] = last_level; a[9] = nr_samples;
    a[10] = flags; a[11] = CTX_ID;
    blk_begin(w);
    r = blk_cmd(w, ACV_RES_CREATE, 12, a);
    rc = blk_run(w);
    if (!rc) rc = blk_result(w, r);
    mtx_unlock(&w->mutex);
    if (rc) {
        if (res->dt) w->sws->displaytarget_destroy(w->sws, res->dt);
        if (res->ptr) w->t.free(w->t.user, res->ptr);
        FREE(res);
        return NULL;
    }
    pipe_reference_init(&res->reference, 1);
    p_atomic_set(&res->num_cs_references, 0);
    return res;
}

static void gv_resource_reference(struct virgl_winsys *vws, struct virgl_hw_res **dres, struct virgl_hw_res *sres)
{
    struct virgl_hw_res *old = *dres;
    if (pipe_reference(old ? &old->reference : NULL, sres ? &sres->reference : NULL))
        res_destroy(gv(vws), old);
    *dres = sres;
}

static void *gv_resource_map(struct virgl_winsys *vws, struct virgl_hw_res *res) { (void)vws; return res->ptr; }
static void gv_resource_wait(struct virgl_winsys *vws, struct virgl_hw_res *res) { (void)vws; (void)res; }
static bool gv_resource_is_busy(struct virgl_winsys *vws, struct virgl_hw_res *res) { (void)vws; (void)res; return false; }

/* ---- command buffers ---- */

static struct virgl_cmd_buf *gv_cmd_buf_create(struct virgl_winsys *vws, uint32_t size)
{
    struct gv_ws *w = gv(vws);
    struct gv_cmd_buf *cb = CALLOC_STRUCT(gv_cmd_buf);
    if (!cb) return NULL;
    cb->nres = 512;
    cb->res = CALLOC(cb->nres, sizeof *cb->res);
    cb->base.buf = w->t.alloc(w->t.user, (size_t)size * 4);
    if (!cb->res || !cb->base.buf) {
        if (cb->base.buf) w->t.free(w->t.user, cb->base.buf);
        FREE(cb->res);
        FREE(cb);
        return NULL;
    }
    cb->ws = w;
    return &cb->base;
}

static void release_all(struct gv_cmd_buf *cb)
{
    unsigned i;
    for (i = 0; i < cb->cres; i++) {
        p_atomic_dec(&cb->res[i]->num_cs_references);
        gv_resource_reference(&cb->ws->base, &cb->res[i], NULL);
    }
    cb->cres = 0;
}

static void gv_cmd_buf_destroy(struct virgl_cmd_buf *_cb)
{
    struct gv_cmd_buf *cb = (struct gv_cmd_buf *)_cb;
    release_all(cb);
    cb->ws->t.free(cb->ws->t.user, cb->base.buf);
    FREE(cb->res);
    FREE(cb);
}

static void add_res(struct gv_cmd_buf *cb, struct virgl_hw_res *res)
{
    unsigned i;
    for (i = 0; i < cb->cres; i++) if (cb->res[i] == res) return;
    if (cb->cres >= cb->nres) {
        struct virgl_hw_res **n = REALLOC(cb->res, cb->nres * sizeof *n, (cb->nres + 256) * sizeof *n);
        if (!n) return;
        cb->res = n;
        cb->nres += 256;
    }
    cb->res[cb->cres] = NULL;
    gv_resource_reference(&cb->ws->base, &cb->res[cb->cres], res);
    p_atomic_inc(&res->num_cs_references);
    cb->cres++;
}

/* A resource named in the stream: its handle, a little-endian word as the rest. */
static void gv_emit_res(struct virgl_winsys *vws, struct virgl_cmd_buf *_cb, struct virgl_hw_res *res, bool write_buf)
{
    struct gv_cmd_buf *cb = (struct gv_cmd_buf *)_cb;
    (void)vws;
    if (write_buf) cb->base.buf[cb->base.cdw++] = util_cpu_to_le32(res->res_handle);
    add_res(cb, res);
}

static bool gv_res_is_referenced(struct virgl_winsys *vws, struct virgl_cmd_buf *cb, struct virgl_hw_res *res)
{
    (void)vws; (void)cb;
    return p_atomic_read(&res->num_cs_references) != 0;
}

static struct pipe_fence_handle *new_fence(void)
{
    struct gv_fence *f = CALLOC_STRUCT(gv_fence);
    if (f) pipe_reference_init(&f->reference, 1);
    return (struct pipe_fence_handle *)f;
}

static int gv_submit_cmd(struct virgl_winsys *vws, struct virgl_cmd_buf *_cb, struct pipe_fence_handle **fence)
{
    struct gv_ws *w = gv(vws);
    struct gv_cmd_buf *cb = (struct gv_cmd_buf *)_cb;
    int rc = 0;
    if (cb->base.cdw) {
        uint32_t a[3];
        unsigned r;
        a[0] = CTX_ID; a[1] = w->t.addr(w->t.user, cb->base.buf); a[2] = cb->base.cdw;
        mtx_lock(&w->mutex);
        blk_begin(w);
        r = blk_cmd(w, ACV_SUBMIT, 3, a);
        rc = blk_run(w);
        if (!rc) rc = blk_result(w, r);
        mtx_unlock(&w->mutex);
    }
    if (fence && !rc) *fence = new_fence();     /* done already: the block has returned */
    release_all(cb);
    cb->base.cdw = 0;
    return rc;
}

/* ---- caps, fences, the front buffer ---- */

static int gv_get_caps(struct virgl_winsys *vws, struct virgl_drm_caps *caps)
{
    struct gv_ws *w = gv(vws);
    uint32_t a[4], set = 2, *mem, bytes, i, n;
    unsigned ri, rc;
    virgl_ws_fill_new_caps_defaults(caps);
    mtx_lock(&w->mutex);
    blk_begin(w);
    a[0] = 2; a[1] = 0; a[2] = 0;
    ri = blk_cmd(w, ACV_CAPSET_INFO, 3, a);
    blk_run(w);
    if (!blk_word(w, ri + 2)) {
        set = 1;
        blk_begin(w);
        a[0] = 1;
        ri = blk_cmd(w, ACV_CAPSET_INFO, 3, a);
        blk_run(w);
    }
    bytes = blk_word(w, ri + 3);
    if (bytes > sizeof caps->caps) bytes = sizeof caps->caps;
    mem = w->t.alloc(w->t.user, sizeof caps->caps);
    if (!mem || !bytes) { mtx_unlock(&w->mutex); if (mem) w->t.free(w->t.user, mem); return -1; }
    a[0] = set; a[1] = blk_word(w, ri + 2); a[2] = w->t.addr(w->t.user, mem); a[3] = bytes;
    blk_begin(w);
    rc = blk_cmd(w, ACV_CAPS, 4, a);
    blk_run(w);
    n = blk_result(w, rc) > 0 ? (uint32_t)blk_result(w, rc) : 0;
    mtx_unlock(&w->mutex);
    /* the host gives each word in the Amiga's order: read them as words */
    for (i = 0; i < n / 4; i++) ((uint32_t *)&caps->caps)[i] = util_be32_to_cpu(mem[i]);
    w->t.free(w->t.user, mem);
#if UTIL_ARCH_BIG_ENDIAN
    /* the boolean bit-field word: bit k of the host's is field k, which a
     * big-endian compiler puts at bit 31 - k */
    {
        uint32_t *b = (uint32_t *)&caps->caps.v1.bset, v = *b, r = 0;
        for (i = 0; i < 32; i++) if (v & (1u << i)) r |= 1u << (31 - i);
        *b = r;
    }
#endif
    /* copies through staging resources need memory the host maps: not here */
    caps->caps.v2.capability_bits_v2 &= ~VIRGL_CAP_V2_COPY_TRANSFER_BOTH_DIRECTIONS;
    caps->caps.v2.capability_bits &= ~(VIRGL_CAP_COPY_TRANSFER | VIRGL_CAP_TRANSFER);
    return n ? 0 : -1;
}

static struct pipe_fence_handle *gv_cs_create_fence(struct virgl_winsys *vws, int fd) { (void)vws; (void)fd; return new_fence(); }
static bool gv_fence_wait(struct virgl_winsys *vws, struct pipe_fence_handle *f, uint64_t timeout) { (void)vws; (void)f; (void)timeout; return true; }
static void gv_fence_reference(struct virgl_winsys *vws, struct pipe_fence_handle **dst, struct pipe_fence_handle *src)
{
    struct gv_fence *old = (struct gv_fence *)*dst, *s = (struct gv_fence *)src;
    (void)vws;
    if (pipe_reference(old ? &old->reference : NULL, s ? &s->reference : NULL)) FREE(old);
    *dst = src;
}

/* A finished frame: back from the host into the resource's copy, then into
 * the display target, which shows it (the GLA core's present hook). */
static void gv_flush_frontbuffer(struct virgl_winsys *vws, struct virgl_cmd_buf *cb, struct virgl_hw_res *res,
                                 unsigned level, unsigned layer, void *drawable, struct pipe_box *sub_box)
{
    struct gv_ws *w = gv(vws);
    struct pipe_box box;
    uint32_t offset = 0;
    void *map;
    (void)cb;
    if (!res->dt) return;
    if (sub_box) {
        box = *sub_box;
        offset = box.y * res->stride + box.x * util_format_get_blocksize(res->format);
    } else u_box_3d(0, 0, layer, res->width, res->height, 1, &box);
    transfer(w, res, &box, res->stride, 0, offset, level, 1, swap_unit(res));
    map = w->sws->displaytarget_map(w->sws, res->dt, PIPE_MAP_WRITE);
    if (map) {
        util_copy_rect(map, res->format, res->dt_stride, box.x, box.y, box.width, box.height, res->ptr, res->stride, box.x, box.y);
        w->sws->displaytarget_unmap(w->sws, res->dt);
    }
    w->sws->displaytarget_display(w->sws, res->dt, drawable, sub_box ? 1 : 0, sub_box);
}

static void gv_destroy(struct virgl_winsys *vws)
{
    struct gv_ws *w = gv(vws);
    uint32_t a[1] = { CTX_ID };
    blk_begin(w);
    blk_cmd(w, ACV_CTX_DESTROY, 1, a);
    blk_run(w);
    w->t.free(w->t.user, w->blk);
    mtx_destroy(&w->mutex);
    FREE(w);
}

struct virgl_winsys *gla_virgl_winsys_create(struct sw_winsys *sws, const struct gla_virgl_transport *t)
{
    struct gv_ws *w = CALLOC_STRUCT(gv_ws);
    static const char name[] = "GLA";
    uint32_t a[3];
    unsigned rh, rc;
    char *nm;
    if (!w) return NULL;
    w->sws = sws;
    w->t = *t;
    w->next_handle = 1;
    (void)mtx_init(&w->mutex, mtx_plain);
    if (!(w->blk = w->t.alloc(w->t.user, BLK_WORDS * 4 + 64))) { FREE(w); return NULL; }
    nm = (char *)(w->blk + BLK_WORDS);
    memcpy(nm, name, sizeof name);
    blk_begin(w);
    a[0] = 0;
    rh = blk_cmd(w, ACV_HELLO, 1, a);
    a[0] = CTX_ID; a[1] = w->t.addr(w->t.user, nm); a[2] = sizeof name - 1;
    rc = blk_cmd(w, ACV_CTX_CREATE, 3, a);
    if (blk_run(w) || blk_result(w, rh) != ACV_PROTOCOL || blk_result(w, rc) != 0) {
        w->t.free(w->t.user, w->blk);
        FREE(w);
        return NULL;
    }
    w->base.transfer_put = gv_transfer_put;
    w->base.transfer_get = gv_transfer_get;
    w->base.resource_create = gv_resource_create;
    w->base.resource_reference = gv_resource_reference;
    w->base.resource_map = gv_resource_map;
    w->base.resource_wait = gv_resource_wait;
    w->base.resource_is_busy = gv_resource_is_busy;
    w->base.cmd_buf_create = gv_cmd_buf_create;
    w->base.cmd_buf_destroy = gv_cmd_buf_destroy;
    w->base.emit_res = gv_emit_res;
    w->base.submit_cmd = gv_submit_cmd;
    w->base.res_is_referenced = gv_res_is_referenced;
    w->base.get_caps = gv_get_caps;
    w->base.cs_create_fence = gv_cs_create_fence;
    w->base.fence_wait = gv_fence_wait;
    w->base.fence_reference = gv_fence_reference;
    w->base.flush_frontbuffer = gv_flush_frontbuffer;
    w->base.destroy = gv_destroy;
    w->base.supports_fences = 0;
    w->base.supports_encoded_transfers = 0;
    w->base.supports_coherent = 0;
    return &w->base;
}

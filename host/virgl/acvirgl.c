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

struct acvirgl {
    void *dl;
    struct vr_api vr;
    struct vr_callbacks cb;
    uint8_t *tmp;               /* commands and pixels, in the host's order */
    size_t tmp_size;
    struct acvirgl_stats st;
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

struct acvirgl *acvirgl_create(const char *lib, char *err, int errlen)
{
    struct acvirgl *v = calloc(1, sizeof *v);
    const char *path = lib ? lib : getenv("ACVIRGL_LIB");
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
    if (v->vr.init(v, VR_USE_EGL | VR_USE_SURFACELESS, &v->cb)) {
        snprintf(err, (size_t)errlen, "virgl_renderer_init failed (no EGL with surfaceless contexts?)");
        goto fail;
    }
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
    free(v);
}

void acvirgl_stats(const struct acvirgl *v, struct acvirgl_stats *s) { *s = v->st; }

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
            av_put_be32(a, 0);
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
            else if (ctx) v->vr.ctx_attach_resource((int)ctx, (int)ra.handle);
            break;
        }
        case ACV_RES_UNREF: v->vr.resource_unref(av_be32(a)); break;
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
        }
        if (r < 0) v->st.errors++;
        av_put_be32(c + 4, (uint32_t)r);
        at += len;
    }
    return OGPU_OK;
}

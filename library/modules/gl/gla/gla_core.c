/* Copyright (c) 2026 Dalsin Limited. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GLA core (gla_core.h): a Gallium front end and a software winsys for
 * Mesa's state tracker and softpipe, modelled on Mesa's own small front
 * ends (Haiku's hgl): the state tracker asks the core for its framebuffer's
 * textures, and softpipe hands finished frames to the core's winsys, which
 * passes them to the buffer's present hook. Display targets live in ordinary
 * memory (Fast RAM on the Amiga). */
#include "gla_core.h"

#include <string.h>

#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "frontend/api.h"
#include "frontend/sw_winsys.h"
#include "softpipe/sp_public.h"
#include "virgl/virgl_public.h"
#include "gla_virgl.h"
#include "state_tracker/st_context.h"
#include "util/format/u_format.h"
#include "util/u_atomic.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_math.h"
#include "util/u_endian.h"

#include "glapi/glapi.h"

/* ---- the winsys: display targets in memory, shown by a hook ---- */

struct gla_dt {
    enum pipe_format format;
    unsigned w, h, stride;
    void *data;
};

static void ws_destroy(struct sw_winsys *ws) { FREE(ws); }

/* Colour formats by byte order in memory: GLA_ARGB on big-endian machines. */
#if UTIL_ARCH_BIG_ENDIAN
#define GLA_NATIVE      GLA_ARGB
#define FMT_ALPHA       PIPE_FORMAT_A8R8G8B8_UNORM
#define FMT_NOALPHA     PIPE_FORMAT_X8R8G8B8_UNORM
#else
#define GLA_NATIVE      GLA_BGRA
#define FMT_ALPHA       PIPE_FORMAT_B8G8R8A8_UNORM
#define FMT_NOALPHA     PIPE_FORMAT_B8G8R8X8_UNORM
#endif

static bool ws_format_ok(struct sw_winsys *ws, unsigned usage, enum pipe_format f)
{
    (void)ws; (void)usage;
    return f == FMT_ALPHA || f == FMT_NOALPHA;
}

static struct sw_displaytarget *ws_dt_create(struct sw_winsys *ws, unsigned usage,
    enum pipe_format format, unsigned w, unsigned h, unsigned alignment,
    const void *front_private, unsigned *stride)
{
    struct gla_dt *dt = CALLOC_STRUCT(gla_dt);
    (void)ws; (void)usage; (void)front_private;
    if (!dt)
        return NULL;
    dt->format = format;
    dt->w = w;
    dt->h = h;
    dt->stride = align(util_format_get_stride(format, w), alignment);
    dt->data = align_malloc((size_t)dt->stride * util_format_get_nblocksy(format, h), alignment);
    if (!dt->data) {
        FREE(dt);
        return NULL;
    }
    *stride = dt->stride;
    return (struct sw_displaytarget *)dt;
}

static void ws_dt_destroy(struct sw_winsys *ws, struct sw_displaytarget *d)
{
    struct gla_dt *dt = (struct gla_dt *)d;
    (void)ws;
    if (dt) {
        align_free(dt->data);
        FREE(dt);
    }
}

static struct sw_displaytarget *ws_dt_from_handle(struct sw_winsys *ws,
    const struct pipe_resource *t, struct winsys_handle *h, unsigned *stride)
{
    (void)ws; (void)t; (void)h; (void)stride;
    return NULL;
}

static bool ws_dt_get_handle(struct sw_winsys *ws, struct sw_displaytarget *d,
                             struct winsys_handle *h)
{
    (void)ws; (void)d; (void)h;
    return false;
}

static void *ws_dt_map(struct sw_winsys *ws, struct sw_displaytarget *d, unsigned flags)
{
    (void)ws; (void)flags;
    return ((struct gla_dt *)d)->data;
}

static void ws_dt_unmap(struct sw_winsys *ws, struct sw_displaytarget *d)
{
    (void)ws; (void)d;
}

/* softpipe's flush_frontbuffer lands here; `priv` is the buffer's hook. */
static void ws_dt_display(struct sw_winsys *ws, struct sw_displaytarget *d, void *priv,
                          unsigned nboxes, struct pipe_box *box)
{
    const struct gla_present *p = priv;
    struct gla_dt *dt = (struct gla_dt *)d;
    struct gla_frame f;
    (void)ws;
    if (!p || !p->present)
        return;
    f.pixels = dt->data;
    f.format = GLA_NATIVE;
    f.stride = dt->stride;
    f.w = dt->w;
    f.h = dt->h;
    if (nboxes && box) {
        f.x0 = box->x; f.y0 = box->y;
        f.x1 = box->x + box->width; f.y1 = box->y + box->height;
    } else {
        f.x0 = 0; f.y0 = 0; f.x1 = (int)dt->w; f.y1 = (int)dt->h;
    }
    p->present(p->user, &f);
}

static struct sw_winsys *gla_winsys_create(void)
{
    struct sw_winsys *ws = CALLOC_STRUCT(sw_winsys);
    if (!ws)
        return NULL;
    ws->destroy = ws_destroy;
    ws->is_displaytarget_format_supported = ws_format_ok;
    ws->displaytarget_create = ws_dt_create;
    ws->displaytarget_from_handle = ws_dt_from_handle;
    ws->displaytarget_get_handle = ws_dt_get_handle;
    ws->displaytarget_map = ws_dt_map;
    ws->displaytarget_unmap = ws_dt_unmap;
    ws->displaytarget_display = ws_dt_display;
    ws->displaytarget_destroy = ws_dt_destroy;
    return ws;
}

/* ---- the front end ---- */

struct gla_display {
    struct pipe_frontend_screen fscreen;
    struct sw_winsys *ws;
    int virgl;              /* the screen is virgl's (which destroys its own winsys; ws is ours) */
};

struct gla_context {
    struct st_context *st;
    struct gla_display *d;
};

struct gla_buffer {
    struct pipe_frontend_drawable base;   /* first: the state tracker casts */
    struct st_visual visual;
    struct gla_display *d;
    struct gla_present present;
    struct pipe_resource *tex[ST_ATTACHMENT_COUNT];
    unsigned w, h;                        /* the textures' size */
    unsigned new_w, new_h;                /* the size asked for */
    enum pipe_texture_target target;
};

static int fs_get_param(struct pipe_frontend_screen *fs, enum st_manager_param param)
{
    (void)fs;
    return param == ST_MANAGER_BROKEN_INVALIDATE;
}

/* The first of the formats (ending NONE) the screen can make a depth or
 * stencil buffer of: a depth buffer in a format the driver can't make
 * leaves the frame buffer unusable, and nothing is drawn. */
static enum pipe_format depth_format(struct pipe_screen *screen, const enum pipe_format *f)
{
    for (; *f != PIPE_FORMAT_NONE; f++)
        if (!screen || screen->is_format_supported(screen, *f, PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_DEPTH_STENCIL))
            return *f;
    return PIPE_FORMAT_NONE;
}

static void visual_from(struct st_visual *v, const struct gla_config *cfg, struct pipe_screen *screen)
{
    static const enum pipe_format with_stencil[] = {
        PIPE_FORMAT_Z24_UNORM_S8_UINT, PIPE_FORMAT_S8_UINT_Z24_UNORM, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT, PIPE_FORMAT_NONE
    };
    static const enum pipe_format depth24[] = {
        PIPE_FORMAT_Z24X8_UNORM, PIPE_FORMAT_X8Z24_UNORM, PIPE_FORMAT_Z24_UNORM_S8_UINT, PIPE_FORMAT_S8_UINT_Z24_UNORM,
        PIPE_FORMAT_Z32_UNORM, PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_NONE
    };
    static const enum pipe_format depth16[] = {
        PIPE_FORMAT_Z16_UNORM, PIPE_FORMAT_Z24X8_UNORM, PIPE_FORMAT_X8Z24_UNORM, PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_NONE
    };
    memset(v, 0, sizeof *v);
    v->color_format = cfg->alpha ? FMT_ALPHA : FMT_NOALPHA;
    if (cfg->stencil)
        v->depth_stencil_format = depth_format(screen, with_stencil);
    else if (cfg->depth > 16)
        v->depth_stencil_format = depth_format(screen, depth24);
    else if (cfg->depth)
        v->depth_stencil_format = depth_format(screen, depth16);
    else
        v->depth_stencil_format = PIPE_FORMAT_NONE;
    v->accum_format = PIPE_FORMAT_NONE;
    v->buffer_mask = ST_ATTACHMENT_FRONT_LEFT_MASK;
    if (cfg->doublebuf)
        v->buffer_mask |= ST_ATTACHMENT_BACK_LEFT_MASK;
    if (v->depth_stencil_format != PIPE_FORMAT_NONE)
        v->buffer_mask |= ST_ATTACHMENT_DEPTH_STENCIL_MASK;
}

struct gla_display *gla_display_create(void)
{
    struct gla_display *d = CALLOC_STRUCT(gla_display);
    if (!d)
        return NULL;
    d->ws = gla_winsys_create();
    d->fscreen.screen = d->ws ? softpipe_create_screen(d->ws) : NULL;
    if (!d->fscreen.screen) {
        if (d->ws)
            d->ws->destroy(d->ws);
        FREE(d);
        return NULL;
    }
    d->fscreen.get_param = fs_get_param;
    return d;
}

struct gla_display *gla_display_create_virgl(const struct gla_virgl_transport *t)
{
    struct gla_display *d = CALLOC_STRUCT(gla_display);
    struct virgl_winsys *vws;
    if (!d)
        return NULL;
    d->ws = gla_winsys_create();
    vws = d->ws ? gla_virgl_winsys_create(d->ws, t) : NULL;
    d->fscreen.screen = vws ? virgl_create_screen(vws, NULL) : NULL;
    if (!d->fscreen.screen) {
        if (d->ws)
            d->ws->destroy(d->ws);
        FREE(d);
        return NULL;
    }
    d->virgl = 1;
    d->fscreen.get_param = fs_get_param;
    return d;
}

const char *gla_display_driver(struct gla_display *d)
{
    return d && d->virgl ? "virgl" : "softpipe";
}

void gla_display_destroy(struct gla_display *d)
{
    if (!d)
        return;
    st_screen_destroy(&d->fscreen);
    d->fscreen.screen->destroy(d->fscreen.screen);   /* softpipe and virgl destroy their winsys too */
    if (d->virgl)
        d->ws->destroy(d->ws);                       /* virgl's display targets' */
    FREE(d);
}

int gla_display_version(struct gla_display *d, int profile)
{
    struct st_config_options opts;
    int core = 0, compat = 0, es1 = 0, es2 = 0;
    memset(&opts, 0, sizeof opts);
    st_api_query_versions(&d->fscreen, &opts, &core, &compat, &es1, &es2);
    return profile == GLA_CORE ? core : profile == GLA_ES2 ? es2 : compat;
}

struct gla_context *gla_context_create(struct gla_display *d, const struct gla_config *cfg,
                                       struct gla_context *share)
{
    struct st_context_attribs a;
    enum st_context_error err;
    struct gla_context *c = CALLOC_STRUCT(gla_context);
    if (!c)
        return NULL;
    memset(&a, 0, sizeof a);
    a.profile = cfg->profile == GLA_CORE ? API_OPENGL_CORE
              : cfg->profile == GLA_ES2 ? API_OPENGLES2 : API_OPENGL_COMPAT;
    a.major = cfg->major ? cfg->major : (cfg->profile == GLA_ES2 ? 2 : cfg->profile == GLA_CORE ? 3 : 1);
    a.minor = cfg->major ? cfg->minor : (cfg->profile == GLA_CORE ? 2 : 0);
    if (cfg->profile == GLA_CORE)
        a.flags |= ST_CONTEXT_FLAG_FORWARD_COMPATIBLE;
    visual_from(&a.visual, cfg, d->fscreen.screen);
    c->d = d;
    c->st = st_api_create_context(&d->fscreen, &a, &err, share ? share->st : NULL);
    if (!c->st) {
        FREE(c);
        return NULL;
    }
    c->st->frontend_context = c;
    return c;
}

void gla_context_destroy(struct gla_context *c)
{
    if (!c)
        return;
    if (st_api_get_current() == c->st)
        st_api_make_current(NULL, NULL, NULL);
    st_context_flush(c->st, 0, NULL, NULL, NULL);
    st_destroy_context(c->st);
    FREE(c);
}

static bool buf_alloc(struct gla_buffer *b)
{
    struct pipe_screen *screen = b->d->fscreen.screen;
    struct pipe_resource t;
    unsigned i;

    if (b->w != b->new_w || b->h != b->new_h)
        for (i = 0; i < ST_ATTACHMENT_COUNT; i++)
            pipe_resource_reference(&b->tex[i], NULL);
    memset(&t, 0, sizeof t);
    t.target = b->target;
    t.width0 = b->new_w ? b->new_w : 1;
    t.height0 = b->new_h ? b->new_h : 1;
    t.depth0 = 1;
    t.array_size = 1;
    for (i = 0; i < ST_ATTACHMENT_COUNT; i++) {
        if (!(b->visual.buffer_mask & (1u << i)) || b->tex[i])
            continue;
        if (i == ST_ATTACHMENT_DEPTH_STENCIL) {
            t.format = b->visual.depth_stencil_format;
            t.bind = PIPE_BIND_DEPTH_STENCIL;
        } else {
            t.format = b->visual.color_format;
            t.bind = PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_RENDER_TARGET;
        }
        b->tex[i] = screen->resource_create(screen, &t);
        if (!b->tex[i])
            return false;
    }
    b->w = b->new_w;
    b->h = b->new_h;
    return true;
}

static bool buf_validate(struct st_context *st, struct pipe_frontend_drawable *drawable,
                         const enum st_attachment_type *statts, unsigned count,
                         struct pipe_resource **out, struct pipe_resource **resolve)
{
    struct gla_buffer *b = (struct gla_buffer *)drawable;
    unsigned i;
    (void)st; (void)resolve;
    if (!buf_alloc(b))
        return false;
    for (i = 0; i < count; i++)
        pipe_resource_reference(&out[i], b->tex[statts[i]]);
    return true;
}

static void buf_show(struct gla_buffer *b, struct pipe_context *pipe, enum st_attachment_type att)
{
    struct pipe_screen *screen = b->d->fscreen.screen;
    if (b->tex[att])
        screen->flush_frontbuffer(screen, pipe, b->tex[att], 0, 0, &b->present, 0, NULL);
}

/* Single-buffered drawing reaches the window at glFlush/glFinish. */
static bool buf_flush_front(struct st_context *st, struct pipe_frontend_drawable *drawable,
                            enum st_attachment_type statt)
{
    if (statt != ST_ATTACHMENT_FRONT_LEFT)
        return false;
    buf_show((struct gla_buffer *)drawable, st ? st->pipe : NULL, statt);
    return true;
}

struct gla_buffer *gla_buffer_create(struct gla_display *d, const struct gla_config *cfg,
                                     unsigned w, unsigned h, const struct gla_present *p)
{
    static uint32_t next_id;
    struct gla_buffer *b = CALLOC_STRUCT(gla_buffer);
    if (!b)
        return NULL;
    visual_from(&b->visual, cfg, d->fscreen.screen);
    b->d = d;
    if (p)
        b->present = *p;
    b->new_w = w;
    b->new_h = h;
    b->target = d->fscreen.screen->caps.npot_textures ? PIPE_TEXTURE_2D : PIPE_TEXTURE_RECT;
    b->base.visual = &b->visual;
    b->base.fscreen = &d->fscreen;
    b->base.flush_front = buf_flush_front;
    b->base.validate = buf_validate;
    b->base.ID = p_atomic_inc_return(&next_id);
    p_atomic_set(&b->base.stamp, 1);
    return b;
}

void gla_buffer_resize(struct gla_buffer *b, unsigned w, unsigned h)
{
    if (b->new_w == w && b->new_h == h)
        return;
    b->new_w = w;
    b->new_h = h;
    p_atomic_inc(&b->base.stamp);
}

void gla_buffer_destroy(struct gla_buffer *b)
{
    unsigned i;
    if (!b)
        return;
    st_api_destroy_drawable(&b->base);
    for (i = 0; i < ST_ATTACHMENT_COUNT; i++)
        pipe_resource_reference(&b->tex[i], NULL);
    FREE(b);
}

int gla_make_current(struct gla_context *c, struct gla_buffer *b)
{
    if (!c)
        return st_api_make_current(NULL, NULL, NULL);
    return st_api_make_current(c->st, b ? &b->base : NULL, b ? &b->base : NULL);
}

void gla_swap(struct gla_context *c, struct gla_buffer *b)
{
    struct pipe_fence_handle *fence = NULL;
    struct pipe_screen *screen = b->d->fscreen.screen;
    st_context_flush(c->st, ST_FLUSH_FRONT | ST_FLUSH_END_OF_FRAME, &fence, NULL, NULL);
    if (fence) {
        screen->fence_finish(screen, NULL, fence, OS_TIMEOUT_INFINITE);
        screen->fence_reference(screen, &fence, NULL);
    }
    buf_show(b, c->st->pipe, (b->visual.buffer_mask & ST_ATTACHMENT_BACK_LEFT_MASK)
                             ? ST_ATTACHMENT_BACK_LEFT : ST_ATTACHMENT_FRONT_LEFT);
}

void *gla_get_proc_address(const char *name)
{
    return (void *)_mesa_glapi_get_proc_address(name);
}

/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: GL's state. Enables, the per-fragment functions, fog,
 * lights and materials, the viewport, queries and strings. Anything that
 * changes how fragments are drawn marks the stream's state block dirty
 * (R3D_DIRTY); the next triangle sends it. */
#include "mgl_internal.h"

void mgl_error(GLcontext c, GLenum e)
{
    if (c && c->error == GL_NO_ERROR) c->error = e;
}

static void set4(float *d, float a, float b, float e, float f) { d[0] = a; d[1] = b; d[2] = e; d[3] = f; }

void mgl_init_state(GLcontext c)
{
    int i, u;
    c->matrix_mode = GL_MODELVIEW;
    mgl_mat_identity(c->mv[0]);
    mgl_mat_identity(c->proj[0]);
    for (u = 0; u < MGL_TEX_UNITS; u++) { mgl_mat_identity(c->texm[u][0]); c->tex_identity[u] = 1; }
    c->mvp_dirty = c->nm_dirty = 1;
    c->depth_near = 0.0; c->depth_far = 1.0;
    set4(c->color, 1, 1, 1, 1);
    c->normal[0] = 0; c->normal[1] = 0; c->normal[2] = 1;
    for (u = 0; u < MGL_TEX_UNITS; u++) set4(c->tc[u], 0, 0, 0, 1);
    c->edge = GL_TRUE;
    c->en = EN_DITHER;
    c->depth_func = GL_LESS;
    c->depth_mask = GL_TRUE;
    c->blend_src = GL_ONE; c->blend_dst = GL_ZERO; c->blend_eq = GL_FUNC_ADD;
    c->alpha_func = GL_ALWAYS;
    c->cull_mode = GL_BACK; c->front_face = GL_CCW; c->shade_model = GL_SMOOTH;
    c->poly_front = c->poly_back = GL_FILL;
    c->cmask = 15;
    c->clear_depth = 1.0;
    c->fog_mode = GL_EXP;
    c->fog_start = 0.0f; c->fog_end = 1.0f; c->fog_density = 1.0f;
    c->hint_persp = c->hint_fog = c->hint_w_one = GL_DONT_CARE;
    c->point_size = c->line_width = 1.0f;
    c->draw_buffer = c->read_buffer = GL_BACK;
    for (i = 0; i < 8; i++) {
        struct mgl_light *l = &c->light[i];
        set4(l->ambient, 0, 0, 0, 1);
        if (i == 0) { set4(l->diffuse, 1, 1, 1, 1); set4(l->specular, 1, 1, 1, 1); }
        else { set4(l->diffuse, 0, 0, 0, 1); set4(l->specular, 0, 0, 0, 1); }
        set4(l->position, 0, 0, 1, 0);
        l->L[0] = 0; l->L[1] = 0; l->L[2] = 1;
        l->H[0] = 0; l->H[1] = 0; l->H[2] = 1;
        l->direction[0] = 0; l->direction[1] = 0; l->direction[2] = -1;
        l->spot_exp = 0; l->spot_cutoff = 180; l->spot_cos = -1;
        l->att[0] = 1; l->att[1] = 0; l->att[2] = 0;
    }
    set4(c->light_ambient, 0.2f, 0.2f, 0.2f, 1);
    for (i = 0; i < 2; i++) {
        set4(c->mat[i].ambient, 0.2f, 0.2f, 0.2f, 1);
        set4(c->mat[i].diffuse, 0.8f, 0.8f, 0.8f, 1);
        set4(c->mat[i].specular, 0, 0, 0, 1);
        set4(c->mat[i].emission, 0, 0, 0, 1);
        c->mat[i].shininess = 0;
    }
    c->spec_table_for = -1.0f;
    c->colormat_face = GL_FRONT_AND_BACK; c->colormat_mode = GL_AMBIENT_AND_DIFFUSE;
    for (u = 0; u < MGL_TEX_UNITS; u++) {
        for (i = 0; i < 4; i++) c->gen_mode[u][i] = GL_EYE_LINEAR;
        c->gen_obj[u][0][0] = c->gen_eye[u][0][0] = 1;
        c->gen_obj[u][1][1] = c->gen_eye[u][1][1] = 1;
        c->env_mode[u] = GL_MODULATE;
        c->tex0[u].min = GL_NEAREST_MIPMAP_LINEAR; c->tex0[u].mag = GL_LINEAR;
        c->tex0[u].wrap_s = c->tex0[u].wrap_t = GL_REPEAT;
        c->bound[u] = &c->tex0[u];
    }
    c->unpack_align = c->pack_align = 4;
    c->next_name = 1;
    c->next_list = 1;
    c->prim = PRIM_NONE;
    c->lock_mode = MGL_LOCK_AUTOMATIC;
    c->sc_w = c->width; c->sc_h = c->height;
    GLViewport(c, 0, 0, c->width, c->height);
    c->r3d_dirty = c->clip_dirty = 1;
}

/* ---- errors and strings ------------------------------------------------------------------ */

GLenum GLGetError(GLcontext c)
{
    GLenum e;
    if (!c) return GL_INVALID_OPERATION;
    e = c->error;
    c->error = GL_NO_ERROR;
    return e;
}

const GLubyte *GLGetString(GLcontext c, GLenum name)
{
    switch (name) {
    case GL_VENDOR: return (const GLubyte *)"Dalsin Limited (OpenRTG)";
    case GL_RENDERER:
        return (const GLubyte *)(c && c->route == ROUTE_GPU ? "OpenRTG MiniGL on OpenGPU" : "OpenRTG MiniGL on the 68k");
    case GL_VERSION: return (const GLubyte *)"1.1 MiniGL 29.10";
    case GL_EXTENSIONS:
        return (const GLubyte *)"GL_ARB_multitexture GL_EXT_paletted_texture GL_EXT_shared_texture_palette "
                                "GL_EXT_compiled_vertex_array GL_EXT_texture_env_add GL_SGIS_texture_edge_clamp "
                                "GL_EXT_bgra GL_MGL_ARGB";
    }
    mgl_error(c, GL_INVALID_ENUM);
    return 0;
}

/* ---- enables ------------------------------------------------------------------------------ */

static ULONG cap_bit(GLcontext c, GLenum cap)
{
    switch (cap) {
    case GL_DEPTH_TEST: return EN_DEPTH;
    case GL_BLEND: return EN_BLEND;
    case GL_ALPHA_TEST: return EN_ALPHA;
    case GL_FOG: return EN_FOG;
    case GL_CULL_FACE: return EN_CULL;
    case GL_LIGHTING: return EN_LIGHTING;
    case GL_NORMALIZE: return EN_NORMALIZE;
    case GL_COLOR_MATERIAL: return EN_COLORMAT;
    case GL_SCISSOR_TEST: return EN_SCISSOR;
    case GL_POLYGON_OFFSET_FILL: case GL_POLYGON_OFFSET: return EN_OFFSET;
    case GL_DITHER: return EN_DITHER;
    case GL_TEXTURE_2D: return c->active ? EN_TEX1 : EN_TEX0;
    case GL_TEXTURE_GEN_S: return c->active ? EN_GENS1 : EN_GENS0;
    case GL_TEXTURE_GEN_T: return c->active ? EN_GENT1 : EN_GENT0;
    case GL_TEXTURE_GEN_R: return c->active ? 0 : EN_GENR0;
    case GL_TEXTURE_GEN_Q: return c->active ? 0 : EN_GENQ0;
    case GL_SHARED_TEXTURE_PALETTE_EXT: return EN_SHAREDPAL;
    case GL_LINE_SMOOTH: return EN_LINESMOOTH;
    case GL_POINT_SMOOTH: return EN_POINTSMOOTH;
    case GL_POLYGON_SMOOTH: return EN_POLYSMOOTH;
    case MGL_Z_OFFSET: return EN_ZOFFSET;
    }
    if (cap >= GL_LIGHT0 && cap <= GL_LIGHT7) return 1UL << (EN_LIGHT0 + (cap - GL_LIGHT0));
    return 0;
}

/* Capabilities accepted and not drawn: GL says what they do, MiniGL never did it. */
static int cap_known(GLenum cap)
{
    switch (cap) {
    case GL_STENCIL_TEST: case GL_LOGIC_OP: case GL_COLOR_LOGIC_OP: case GL_LINE_STIPPLE:
    case GL_POLYGON_STIPPLE: case GL_AUTO_NORMAL: case GL_TEXTURE_1D: case GL_POLYGON_OFFSET_LINE:
    case GL_POLYGON_OFFSET_POINT: case MGL_PERSPECTIVE_MAPPING: case MGL_ARRAY_TRANSFORMATIONS:
    case GL_CLIP_PLANE0: case GL_CLIP_PLANE1: case GL_CLIP_PLANE2: case GL_CLIP_PLANE3:
    case GL_CLIP_PLANE4: case GL_CLIP_PLANE5:
        return 1;
    }
    return (cap >= GL_MAP1_COLOR_4 && cap <= GL_MAP1_VERTEX_4) || (cap >= GL_MAP2_COLOR_4 && cap <= GL_MAP2_VERTEX_4);
}

void MGLSetState(GLcontext c, GLenum cap, GLboolean state)
{
    ULONG bit, a[2];
    if (!c) return;
    a[0] = cap; a[1] = state;
    MGL_REC(c, OP_SETSTATE, 2, a);
    if (c->prim != PRIM_NONE) { mgl_error(c, GL_INVALID_OPERATION); return; }
    bit = cap_bit(c, cap);
    if (!bit) {
        if (!cap_known(cap)) mgl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (state) c->en |= bit; else c->en &= ~bit;
    if (bit & (EN_DEPTH | EN_BLEND | EN_ALPHA | EN_FOG | EN_TEX0 | EN_TEX1 | EN_OFFSET | EN_ZOFFSET)) R3D_DIRTY(c);
    if (bit & EN_SCISSOR) c->clip_dirty = 1, c->list_at = -1;
}

GLboolean GLIsEnabled(GLcontext c, GLenum cap)
{
    ULONG bit;
    if (!c) return GL_FALSE;
    switch (cap) {
    case GL_VERTEX_ARRAY: return (c->client & CL_VERTEX) != 0;
    case GL_COLOR_ARRAY: return (c->client & CL_COLOR) != 0;
    case GL_NORMAL_ARRAY: return (c->client & CL_NORMAL) != 0;
    case GL_TEXTURE_COORD_ARRAY: return (c->client & (c->client_active ? CL_TEX1 : CL_TEX0)) != 0;
    }
    bit = cap_bit(c, cap);
    if (!bit) {
        if (!cap_known(cap)) mgl_error(c, GL_INVALID_ENUM);
        return GL_FALSE;
    }
    return (c->en & bit) ? GL_TRUE : GL_FALSE;
}

/* ---- fragments ---------------------------------------------------------------------------- */

static int is_compare(GLenum f) { return f >= GL_NEVER && f <= GL_ALWAYS; }

void GLAlphaFunc(GLcontext c, GLenum func, GLclampf ref)
{
    ULONG a[2];
    a[0] = func; a[1] = mgl_fu(ref);
    MGL_REC(c, OP_ALPHAFUNC, 2, a);
    if (!is_compare(func)) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->alpha_func = func;
    c->alpha_ref = ref < 0 ? 0 : ref > 1 ? 1 : ref;
    R3D_DIRTY(c);
}

static int is_factor(GLenum f)
{
    switch (f) {
    case GL_ZERO: case GL_ONE: case GL_SRC_COLOR: case GL_ONE_MINUS_SRC_COLOR: case GL_SRC_ALPHA:
    case GL_ONE_MINUS_SRC_ALPHA: case GL_DST_ALPHA: case GL_ONE_MINUS_DST_ALPHA: case GL_DST_COLOR:
    case GL_ONE_MINUS_DST_COLOR: case GL_SRC_ALPHA_SATURATE: case GL_CONSTANT_COLOR:
    case GL_ONE_MINUS_CONSTANT_COLOR: case GL_CONSTANT_ALPHA: case GL_ONE_MINUS_CONSTANT_ALPHA:
        return 1;
    }
    return 0;
}

void GLBlendFunc(GLcontext c, GLenum sfactor, GLenum dfactor)
{
    ULONG a[2];
    a[0] = sfactor; a[1] = dfactor;
    MGL_REC(c, OP_BLENDFUNC, 2, a);
    if (!is_factor(sfactor) || !is_factor(dfactor)) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->blend_src = sfactor; c->blend_dst = dfactor;
    R3D_DIRTY(c);
}

/* One factor pair for colour and alpha: the core blends both alike, so the
 * colour's pair is taken. */
void GLBlendFuncSeparate(GLcontext c, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    if (!is_factor(srcAlpha) || !is_factor(dstAlpha)) { mgl_error(c, GL_INVALID_ENUM); return; }
    GLBlendFunc(c, srcRGB, dstRGB);
}

/* Only adding: the core has no other equation, so the others are accepted
 * and drawn as GL_FUNC_ADD. */
void GLBlendEquation(GLcontext c, GLenum mode)
{
    if (mode != GL_FUNC_ADD && mode != GL_FUNC_SUBTRACT && mode != GL_FUNC_REVERSE_SUBTRACT
        && mode != GL_MIN && mode != GL_MAX) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->blend_eq = mode;
}

void GLClearColor(GLcontext c, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    ULONG a[4];
    a[0] = mgl_fu(red); a[1] = mgl_fu(green); a[2] = mgl_fu(blue); a[3] = mgl_fu(alpha);
    MGL_REC(c, OP_CLEARCOLOR, 4, a);
    set4(c->clear_color, red, green, blue, alpha);
}

void GLClearDepth(GLcontext c, GLclampd depth)
{
    c->clear_depth = depth < 0 ? 0 : depth > 1 ? 1 : depth;
}

void GLColorMask(GLcontext c, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    ULONG a[4];
    a[0] = red; a[1] = green; a[2] = blue; a[3] = alpha;
    MGL_REC(c, OP_COLORMASK, 4, a);
    c->cmask = (red ? 1 : 0) | (green ? 2 : 0) | (blue ? 4 : 0) | (alpha ? 8 : 0);
    R3D_DIRTY(c);
}

void GLCullFace(GLcontext c, GLenum mode)
{
    ULONG a = mode;
    MGL_REC(c, OP_CULLFACE, 1, &a);
    if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->cull_mode = mode;
}

void GLFrontFace(GLcontext c, GLenum mode)
{
    ULONG a = mode;
    MGL_REC(c, OP_FRONTFACE, 1, &a);
    if (mode != GL_CW && mode != GL_CCW) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->front_face = mode;
}

void GLDepthFunc(GLcontext c, GLenum func)
{
    ULONG a = func;
    MGL_REC(c, OP_DEPTHFUNC, 1, &a);
    if (!is_compare(func)) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->depth_func = func;
    R3D_DIRTY(c);
}

void GLDepthMask(GLcontext c, GLboolean flag)
{
    ULONG a = flag;
    MGL_REC(c, OP_DEPTHMASK, 1, &a);
    c->depth_mask = flag ? GL_TRUE : GL_FALSE;
    R3D_DIRTY(c);
}

void GLDepthRange(GLcontext c, GLclampd n, GLclampd f)
{
    c->depth_near = n < 0 ? 0 : n > 1 ? 1 : n;
    c->depth_far = f < 0 ? 0 : f > 1 ? 1 : f;
}

void GLShadeModel(GLcontext c, GLenum mode)
{
    ULONG a = mode;
    MGL_REC(c, OP_SHADEMODEL, 1, &a);
    if (mode != GL_FLAT && mode != GL_SMOOTH) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->shade_model = mode;
}

void GLPolygonMode(GLcontext c, GLenum face, GLenum mode)
{
    ULONG a[2];
    a[0] = face; a[1] = mode;
    MGL_REC(c, OP_POLYGONMODE, 2, a);
    if (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) c->poly_front = mode;
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) c->poly_back = mode;
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) mgl_error(c, GL_INVALID_ENUM);
}

void GLPolygonOffset(GLcontext c, GLfloat factor, GLfloat units)
{
    ULONG a[2];
    a[0] = mgl_fu(factor); a[1] = mgl_fu(units);
    MGL_REC(c, OP_POLYOFFSET, 2, a);
    c->offset_factor = factor;
    c->offset_units = units;
}

void MGLSetZOffset(GLcontext c, GLfloat offset) { if (c) c->zoffset = offset; }
void MGLMinTriArea(GLcontext c, GLfloat area) { if (c) c->min_tri_area = area; }

void GLPointSize(GLcontext c, GLfloat size)
{
    ULONG a = mgl_fu(size);
    MGL_REC(c, OP_POINTSIZE, 1, &a);
    if (size <= 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    c->point_size = size;
}

void GLLineWidth(GLcontext c, GLfloat width)
{
    ULONG a = mgl_fu(width);
    MGL_REC(c, OP_LINEWIDTH, 1, &a);
    if (width <= 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    c->line_width = width;
}

void GLScissor(GLcontext c, GLint x, GLint y, GLsizei width, GLsizei height)
{
    ULONG a[4];
    a[0] = (ULONG)x; a[1] = (ULONG)y; a[2] = (ULONG)width; a[3] = (ULONG)height;
    MGL_REC(c, OP_SCISSOR, 4, a);
    if (width < 0 || height < 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    c->sc_x = x; c->sc_y = y; c->sc_w = width; c->sc_h = height;
    c->clip_dirty = 1;
    c->list_at = -1;
}

void GLViewport(GLcontext c, GLint x, GLint y, GLsizei width, GLsizei height)
{
    ULONG a[4];
    a[0] = (ULONG)x; a[1] = (ULONG)y; a[2] = (ULONG)width; a[3] = (ULONG)height;
    MGL_REC(c, OP_VIEWPORT, 4, a);
    if (width < 0 || height < 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    if (width > 4096) width = 4096;
    if (height > 4096) height = 4096;
    c->vp_x = x; c->vp_y = y; c->vp_w = width; c->vp_h = height;
    mgl_viewport_changed(c);
}

void GLHint(GLcontext c, GLenum target, GLenum mode)
{
    ULONG a[2];
    a[0] = target; a[1] = mode;
    MGL_REC(c, OP_HINT, 2, a);
    if (mode != GL_FASTEST && mode != GL_NICEST && mode != GL_DONT_CARE) { mgl_error(c, GL_INVALID_ENUM); return; }
    switch (target) {
    case GL_PERSPECTIVE_CORRECTION_HINT: c->hint_persp = mode; R3D_DIRTY(c); break;
    case GL_FOG_HINT: c->hint_fog = mode; break;
    case MGL_W_ONE_HINT: c->hint_w_one = mode; break;
    case GL_POINT_SMOOTH_HINT: case GL_LINE_SMOOTH_HINT: case GL_POLYGON_SMOOTH_HINT:
    case MGL_FIXPOINTTRANS_HINT: break;
    default: mgl_error(c, GL_INVALID_ENUM);
    }
}

void GLPixelStorei(GLcontext c, GLenum pname, GLint param)
{
    switch (pname) {
    case GL_UNPACK_ALIGNMENT:
        if (param != 1 && param != 2 && param != 4 && param != 8) { mgl_error(c, GL_INVALID_VALUE); return; }
        c->unpack_align = param; break;
    case GL_PACK_ALIGNMENT:
        if (param != 1 && param != 2 && param != 4 && param != 8) { mgl_error(c, GL_INVALID_VALUE); return; }
        c->pack_align = param; break;
    case GL_UNPACK_ROW_LENGTH: c->unpack_row = param < 0 ? 0 : param; break;
    case GL_UNPACK_SKIP_ROWS: c->unpack_skip_rows = param < 0 ? 0 : param; break;
    case GL_UNPACK_SKIP_PIXELS: c->unpack_skip_px = param < 0 ? 0 : param; break;
    case GL_UNPACK_SWAP_BYTES: case GL_UNPACK_LSB_FIRST: case GL_PACK_SWAP_BYTES: case GL_PACK_LSB_FIRST:
    case GL_PACK_ROW_LENGTH: case GL_PACK_SKIP_ROWS: case GL_PACK_SKIP_PIXELS: break;
    default: mgl_error(c, GL_INVALID_ENUM);
    }
}

void GLDrawBuffer(GLcontext c, GLenum mode) { if (c) c->draw_buffer = mode; }
void GLReadBuffer(GLcontext c, GLenum mode) { if (c) c->read_buffer = mode; }

void GLEdgeFlag(GLcontext c, GLboolean flag)
{
    ULONG a = flag;
    MGL_REC(c, OP_EDGEFLAG, 1, &a);
    c->edge = flag ? GL_TRUE : GL_FALSE;
}
void GLEdgeFlagv(GLcontext c, const GLboolean *flag) { if (flag) GLEdgeFlag(c, *flag); }
/* Colour index mode is not offered (RGBA only): these are accepted and do nothing. */
void GLIndexi(GLcontext c, GLint i) { (void)c; (void)i; }
void GLIndexiv(GLcontext c, const GLint *i) { (void)c; (void)i; }

/* ---- fog ------------------------------------------------------------------------------------ */

void GLFogfv(GLcontext c, GLenum pname, GLfloat *param)
{
    ULONG a[5];
    int i, n = pname == GL_FOG_COLOR ? 4 : 1;
    if (!param) return;
    a[0] = pname;
    for (i = 0; i < n; i++) a[1 + i] = mgl_fu(param[i]);
    MGL_REC(c, OP_FOG, 1 + n, a);
    switch (pname) {
    case GL_FOG_MODE: {
        GLenum m = (GLenum)mgl_f2l(param[0]);
        if (m != GL_LINEAR && m != GL_EXP && m != GL_EXP2) { mgl_error(c, GL_INVALID_ENUM); return; }
        c->fog_mode = m;
        break;
    }
    case GL_FOG_DENSITY:
        if (param[0] < 0) { mgl_error(c, GL_INVALID_VALUE); return; }
        c->fog_density = param[0];
        break;
    case GL_FOG_START: c->fog_start = param[0]; break;
    case GL_FOG_END: c->fog_end = param[0]; break;
    case GL_FOG_COLOR: set4(c->fog_color, param[0], param[1], param[2], param[3]); R3D_DIRTY(c); break;
    case GL_FOG_INDEX: break;
    default: mgl_error(c, GL_INVALID_ENUM);
    }
}

void GLFogf(GLcontext c, GLenum pname, GLfloat param)
{
    if (pname == GL_FOG_COLOR) { mgl_error(c, GL_INVALID_ENUM); return; }
    GLFogfv(c, pname, &param);
}

/* ---- lights and materials ------------------------------------------------------------------- */

static void xform_point(const float *m, const float *p, float *o)
{
    int i;
    for (i = 0; i < 4; i++) o[i] = m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i] * p[3];
}

void GLLightfv(GLcontext c, GLenum light, GLenum pname, const GLfloat *params)
{
    struct mgl_light *l;
    ULONG a[6];
    int i, n = (pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR || pname == GL_POSITION) ? 4
             : pname == GL_SPOT_DIRECTION ? 3 : 1;
    if (!params) return;
    a[0] = light; a[1] = pname;
    for (i = 0; i < n; i++) a[2 + i] = mgl_fu(params[i]);
    MGL_REC(c, OP_LIGHT, 2 + n, a);
    if (light < GL_LIGHT0 || light > GL_LIGHT7) { mgl_error(c, GL_INVALID_ENUM); return; }
    l = &c->light[light - GL_LIGHT0];
    switch (pname) {
    case GL_AMBIENT: for (i = 0; i < 4; i++) l->ambient[i] = params[i]; break;
    case GL_DIFFUSE: for (i = 0; i < 4; i++) l->diffuse[i] = params[i]; break;
    case GL_SPECULAR: for (i = 0; i < 4; i++) l->specular[i] = params[i]; break;
    case GL_POSITION:
        xform_point(c->mv[c->mv_top], params, l->position);
        if (l->position[3] == 0.0f) {
            float il = mgl_rsqrt(l->position[0] * l->position[0] + l->position[1] * l->position[1]
                                 + l->position[2] * l->position[2]), hl;
            l->L[0] = l->position[0] * il; l->L[1] = l->position[1] * il; l->L[2] = l->position[2] * il;
            l->H[0] = l->L[0]; l->H[1] = l->L[1]; l->H[2] = l->L[2] + 1.0f;
            hl = mgl_rsqrt(l->H[0] * l->H[0] + l->H[1] * l->H[1] + l->H[2] * l->H[2]);
            l->H[0] *= hl; l->H[1] *= hl; l->H[2] *= hl;
        }
        break;
    case GL_SPOT_DIRECTION: {
        const float *m = c->mv[c->mv_top];
        for (i = 0; i < 3; i++) l->direction[i] = m[i] * params[0] + m[4 + i] * params[1] + m[8 + i] * params[2];
        break;
    }
    case GL_SPOT_EXPONENT:
        if (params[0] < 0 || params[0] > 128) { mgl_error(c, GL_INVALID_VALUE); return; }
        l->spot_exp = params[0]; break;
    case GL_SPOT_CUTOFF:
        if ((params[0] < 0 || params[0] > 90) && params[0] != 180) { mgl_error(c, GL_INVALID_VALUE); return; }
        l->spot_cutoff = params[0];
        l->spot_cos = params[0] == 180 ? -1.0f : mgl_cos(params[0] * (MGL_PI / 180.0f));
        break;
    case GL_CONSTANT_ATTENUATION: l->att[0] = params[0]; break;
    case GL_LINEAR_ATTENUATION: l->att[1] = params[0]; break;
    case GL_QUADRATIC_ATTENUATION: l->att[2] = params[0]; break;
    default: mgl_error(c, GL_INVALID_ENUM);
    }
}

void GLLightf(GLcontext c, GLenum light, GLenum pname, GLfloat param)
{
    if (pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR || pname == GL_POSITION
        || pname == GL_SPOT_DIRECTION) { mgl_error(c, GL_INVALID_ENUM); return; }
    GLLightfv(c, light, pname, &param);
}

void GLGetLightfv(GLcontext c, GLenum light, GLenum pname, GLfloat *params)
{
    struct mgl_light *l;
    int i;
    if (!params || light < GL_LIGHT0 || light > GL_LIGHT7) { mgl_error(c, GL_INVALID_ENUM); return; }
    l = &c->light[light - GL_LIGHT0];
    switch (pname) {
    case GL_AMBIENT: for (i = 0; i < 4; i++) params[i] = l->ambient[i]; break;
    case GL_DIFFUSE: for (i = 0; i < 4; i++) params[i] = l->diffuse[i]; break;
    case GL_SPECULAR: for (i = 0; i < 4; i++) params[i] = l->specular[i]; break;
    case GL_POSITION: for (i = 0; i < 4; i++) params[i] = l->position[i]; break;
    case GL_SPOT_DIRECTION: for (i = 0; i < 3; i++) params[i] = l->direction[i]; break;
    case GL_SPOT_EXPONENT: params[0] = l->spot_exp; break;
    case GL_SPOT_CUTOFF: params[0] = l->spot_cutoff; break;
    case GL_CONSTANT_ATTENUATION: params[0] = l->att[0]; break;
    case GL_LINEAR_ATTENUATION: params[0] = l->att[1]; break;
    case GL_QUADRATIC_ATTENUATION: params[0] = l->att[2]; break;
    default: mgl_error(c, GL_INVALID_ENUM);
    }
}

void GLLightModelfv(GLcontext c, GLenum pname, const GLfloat *params)
{
    ULONG a[5];
    int i, n = pname == GL_LIGHT_MODEL_AMBIENT ? 4 : 1;
    if (!params) return;
    a[0] = pname;
    for (i = 0; i < n; i++) a[1 + i] = mgl_fu(params[i]);
    MGL_REC(c, OP_LIGHTMODEL, 1 + n, a);
    switch (pname) {
    case GL_LIGHT_MODEL_AMBIENT: for (i = 0; i < 4; i++) c->light_ambient[i] = params[i]; break;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: c->local_viewer = params[0] != 0.0f; break;
    case GL_LIGHT_MODEL_TWO_SIDE: c->two_side = params[0] != 0.0f; break;
    default: mgl_error(c, GL_INVALID_ENUM);
    }
}

void GLLightModelf(GLcontext c, GLenum pname, GLfloat param)
{
    if (pname == GL_LIGHT_MODEL_AMBIENT) { mgl_error(c, GL_INVALID_ENUM); return; }
    GLLightModelfv(c, pname, &param);
}

void GLMaterialfv(GLcontext c, GLenum face, GLenum pname, const GLfloat *params)
{
    ULONG a[6];
    int i, f, n = pname == GL_SHININESS ? 1 : pname == GL_COLOR_INDEXES ? 3 : 4;
    if (!params) return;
    a[0] = face; a[1] = pname;
    for (i = 0; i < n; i++) a[2 + i] = mgl_fu(params[i]);
    MGL_REC(c, OP_MATERIAL, 2 + n, a);
    if (face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) { mgl_error(c, GL_INVALID_ENUM); return; }
    for (f = 0; f < 2; f++) {
        struct mgl_material *m = &c->mat[f];
        if ((f == 0 && face == GL_BACK) || (f == 1 && face == GL_FRONT)) continue;
        switch (pname) {
        case GL_AMBIENT: for (i = 0; i < 4; i++) m->ambient[i] = params[i]; break;
        case GL_DIFFUSE: for (i = 0; i < 4; i++) m->diffuse[i] = params[i]; break;
        case GL_SPECULAR: for (i = 0; i < 4; i++) m->specular[i] = params[i]; break;
        case GL_EMISSION: for (i = 0; i < 4; i++) m->emission[i] = params[i]; break;
        case GL_AMBIENT_AND_DIFFUSE: for (i = 0; i < 4; i++) m->ambient[i] = m->diffuse[i] = params[i]; break;
        case GL_SHININESS:
            if (params[0] < 0 || params[0] > 128) { mgl_error(c, GL_INVALID_VALUE); return; }
            m->shininess = params[0]; break;
        case GL_COLOR_INDEXES: break;
        default: mgl_error(c, GL_INVALID_ENUM); return;
        }
    }
}

void GLMaterialf(GLcontext c, GLenum face, GLenum pname, GLfloat param)
{
    if (pname != GL_SHININESS) { mgl_error(c, GL_INVALID_ENUM); return; }
    GLMaterialfv(c, face, pname, &param);
}

void GLGetMaterialfv(GLcontext c, GLenum face, GLenum pname, GLfloat *params)
{
    struct mgl_material *m;
    int i;
    if (!params || (face != GL_FRONT && face != GL_BACK)) { mgl_error(c, GL_INVALID_ENUM); return; }
    m = &c->mat[face == GL_BACK];
    switch (pname) {
    case GL_AMBIENT: for (i = 0; i < 4; i++) params[i] = m->ambient[i]; break;
    case GL_DIFFUSE: for (i = 0; i < 4; i++) params[i] = m->diffuse[i]; break;
    case GL_SPECULAR: for (i = 0; i < 4; i++) params[i] = m->specular[i]; break;
    case GL_EMISSION: for (i = 0; i < 4; i++) params[i] = m->emission[i]; break;
    case GL_SHININESS: params[0] = m->shininess; break;
    default: mgl_error(c, GL_INVALID_ENUM);
    }
}

void GLColorMaterial(GLcontext c, GLenum face, GLenum mode)
{
    ULONG a[2];
    a[0] = face; a[1] = mode;
    MGL_REC(c, OP_COLORMATERIAL, 2, a);
    if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK)
        || (mode != GL_AMBIENT && mode != GL_DIFFUSE && mode != GL_SPECULAR && mode != GL_EMISSION
            && mode != GL_AMBIENT_AND_DIFFUSE)) { mgl_error(c, GL_INVALID_ENUM); return; }
    c->colormat_face = face;
    c->colormat_mode = mode;
}

/* ---- queries -------------------------------------------------------------------------------- */

/* Every answer as floats first; the typed getters convert. n is how many. */
static int get_values(GLcontext c, GLenum pname, float *v)
{
    int i;
    ULONG bit;
    switch (pname) {
    case GL_CURRENT_COLOR: for (i = 0; i < 4; i++) v[i] = c->color[i]; return 4;
    case GL_CURRENT_NORMAL: for (i = 0; i < 3; i++) v[i] = c->normal[i]; return 3;
    case GL_CURRENT_TEXTURE_COORDS: for (i = 0; i < 4; i++) v[i] = c->tc[c->active][i]; return 4;
    case GL_MODELVIEW_MATRIX: for (i = 0; i < 16; i++) v[i] = c->mv[c->mv_top][i]; return 16;
    case GL_PROJECTION_MATRIX: for (i = 0; i < 16; i++) v[i] = c->proj[c->proj_top][i]; return 16;
    case GL_TEXTURE_MATRIX: for (i = 0; i < 16; i++) v[i] = c->texm[c->active][c->tex_top[c->active]][i]; return 16;
    case GL_MODELVIEW_STACK_DEPTH: v[0] = (float)(c->mv_top + 1); return 1;
    case GL_PROJECTION_STACK_DEPTH: v[0] = (float)(c->proj_top + 1); return 1;
    case GL_TEXTURE_STACK_DEPTH: v[0] = (float)(c->tex_top[c->active] + 1); return 1;
    case GL_MAX_MODELVIEW_STACK_DEPTH: v[0] = MV_DEPTH; return 1;
    case GL_MAX_PROJECTION_STACK_DEPTH: v[0] = PROJ_DEPTH; return 1;
    case GL_MAX_TEXTURE_STACK_DEPTH: v[0] = TEX_DEPTH; return 1;
    case GL_MATRIX_MODE: v[0] = (float)c->matrix_mode; return 1;
    case GL_VIEWPORT: v[0] = (float)c->vp_x; v[1] = (float)c->vp_y; v[2] = (float)c->vp_w; v[3] = (float)c->vp_h; return 4;
    case GL_SCISSOR_BOX: v[0] = (float)c->sc_x; v[1] = (float)c->sc_y; v[2] = (float)c->sc_w; v[3] = (float)c->sc_h; return 4;
    case GL_DEPTH_RANGE: v[0] = (float)c->depth_near; v[1] = (float)c->depth_far; return 2;
    case GL_COLOR_CLEAR_VALUE: for (i = 0; i < 4; i++) v[i] = c->clear_color[i]; return 4;
    case GL_DEPTH_CLEAR_VALUE: v[0] = (float)c->clear_depth; return 1;
    case GL_DEPTH_FUNC: v[0] = (float)c->depth_func; return 1;
    case GL_DEPTH_WRITEMASK: v[0] = c->depth_mask ? 1.0f : 0.0f; return 1;
    case GL_BLEND_SRC: case GL_BLEND_SRC_RGB: case GL_BLEND_SRC_ALPHA: v[0] = (float)c->blend_src; return 1;
    case GL_BLEND_DST: case GL_BLEND_DST_RGB: case GL_BLEND_DST_ALPHA: v[0] = (float)c->blend_dst; return 1;
    case GL_BLEND_EQUATION: v[0] = (float)c->blend_eq; return 1;
    case GL_ALPHA_TEST_FUNC: v[0] = (float)c->alpha_func; return 1;
    case GL_ALPHA_TEST_REF: v[0] = c->alpha_ref; return 1;
    case GL_CULL_FACE_MODE: v[0] = (float)c->cull_mode; return 1;
    case GL_FRONT_FACE: v[0] = (float)c->front_face; return 1;
    case GL_SHADE_MODEL: v[0] = (float)c->shade_model; return 1;
    case GL_POLYGON_MODE: v[0] = (float)c->poly_front; v[1] = (float)c->poly_back; return 2;
    case GL_COLOR_WRITEMASK: for (i = 0; i < 4; i++) v[i] = (c->cmask >> i) & 1 ? 1.0f : 0.0f; return 4;
    case GL_FOG_MODE: v[0] = (float)c->fog_mode; return 1;
    case GL_FOG_DENSITY: v[0] = c->fog_density; return 1;
    case GL_FOG_START: v[0] = c->fog_start; return 1;
    case GL_FOG_END: v[0] = c->fog_end; return 1;
    case GL_FOG_COLOR: for (i = 0; i < 4; i++) v[i] = c->fog_color[i]; return 4;
    case GL_POINT_SIZE: v[0] = c->point_size; return 1;
    case GL_LINE_WIDTH: v[0] = c->line_width; return 1;
    case GL_POINT_SIZE_RANGE: case GL_LINE_WIDTH_RANGE: v[0] = 1; v[1] = 64; return 2;
    case GL_POINT_SIZE_GRANULARITY: case GL_LINE_WIDTH_GRANULARITY: v[0] = 1.0f / 16.0f; return 1;
    case GL_POLYGON_OFFSET_FACTOR: v[0] = c->offset_factor; return 1;
    case GL_POLYGON_OFFSET_UNITS: v[0] = c->offset_units; return 1;
    case GL_LIGHT_MODEL_AMBIENT: for (i = 0; i < 4; i++) v[i] = c->light_ambient[i]; return 4;
    case GL_LIGHT_MODEL_LOCAL_VIEWER: v[0] = c->local_viewer ? 1.0f : 0.0f; return 1;
    case GL_LIGHT_MODEL_TWO_SIDE: v[0] = c->two_side ? 1.0f : 0.0f; return 1;
    case GL_COLOR_MATERIAL_FACE: v[0] = (float)c->colormat_face; return 1;
    case GL_COLOR_MATERIAL_PARAMETER: v[0] = (float)c->colormat_mode; return 1;
    case GL_PERSPECTIVE_CORRECTION_HINT: v[0] = (float)c->hint_persp; return 1;
    case GL_FOG_HINT: v[0] = (float)c->hint_fog; return 1;
    case GL_MAX_TEXTURE_SIZE: v[0] = 2048; return 1;
    case GL_MAX_LIGHTS: v[0] = 8; return 1;
    case GL_MAX_CLIP_PLANES: v[0] = 0; return 1;
    case GL_MAX_VIEWPORT_DIMS: v[0] = 4096; v[1] = 4096; return 2;
    case GL_MAX_TEXTURE_UNITS_ARB: v[0] = MGL_TEX_UNITS; return 1;
    case GL_MAX_LIST_NESTING: v[0] = 64; return 1;
    case GL_MAX_ATTRIB_STACK_DEPTH: case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH: case GL_MAX_NAME_STACK_DEPTH: v[0] = 0; return 1;
    case GL_ACTIVE_TEXTURE_ARB: v[0] = (float)(GL_TEXTURE0_ARB + c->active); return 1;
    case GL_CLIENT_ACTIVE_TEXTURE_ARB: v[0] = (float)(GL_TEXTURE0_ARB + c->client_active); return 1;
    case GL_TEXTURE_BINDING_2D: v[0] = (float)(c->bound[c->active] ? c->bound[c->active]->name : 0); return 1;
    case GL_TEXTURE_ENV_MODE: v[0] = (float)c->env_mode[c->active]; return 1;
    case GL_SUBPIXEL_BITS: v[0] = 4; return 1;
    case GL_RED_BITS: case GL_BLUE_BITS: v[0] = c->fmt == OGPU_FMT_RGB565 ? 5.0f : 8.0f; return 1;
    case GL_GREEN_BITS: v[0] = c->fmt == OGPU_FMT_RGB565 ? 6.0f : 8.0f; return 1;
    case GL_ALPHA_BITS: v[0] = 0; return 1;
    case GL_DEPTH_BITS: v[0] = c->zfmt == OGPU_FMT_Z32 ? 32.0f : 16.0f; return 1;
    case GL_STENCIL_BITS: case GL_INDEX_BITS: case GL_ACCUM_RED_BITS: case GL_ACCUM_GREEN_BITS:
    case GL_ACCUM_BLUE_BITS: case GL_ACCUM_ALPHA_BITS: case GL_AUX_BUFFERS: v[0] = 0; return 1;
    case GL_DOUBLEBUFFER: case GL_RGBA_MODE: v[0] = 1; return 1;
    case GL_INDEX_MODE: case GL_STEREO: v[0] = 0; return 1;
    case GL_DRAW_BUFFER: v[0] = (float)c->draw_buffer; return 1;
    case GL_READ_BUFFER: v[0] = (float)c->read_buffer; return 1;
    case GL_UNPACK_ALIGNMENT: v[0] = (float)c->unpack_align; return 1;
    case GL_PACK_ALIGNMENT: v[0] = (float)c->pack_align; return 1;
    case GL_UNPACK_ROW_LENGTH: v[0] = (float)c->unpack_row; return 1;
    case GL_UNPACK_SKIP_ROWS: v[0] = (float)c->unpack_skip_rows; return 1;
    case GL_UNPACK_SKIP_PIXELS: v[0] = (float)c->unpack_skip_px; return 1;
    case GL_EDGE_FLAG: v[0] = c->edge ? 1.0f : 0.0f; return 1;
    case GL_LIST_INDEX: v[0] = (float)(c->compiling ? c->compiling->name : 0); return 1;
    case GL_LIST_MODE: v[0] = (float)(c->compiling ? c->compile_mode : 0); return 1;
    case GL_VERTEX_ARRAY_SIZE: v[0] = (float)c->va.size; return 1;
    case GL_VERTEX_ARRAY_TYPE: v[0] = (float)c->va.type; return 1;
    case GL_VERTEX_ARRAY_STRIDE: v[0] = (float)c->va.stride; return 1;
    case GL_COLOR_ARRAY_SIZE: v[0] = (float)c->ca.size; return 1;
    case GL_COLOR_ARRAY_TYPE: v[0] = (float)c->ca.type; return 1;
    case GL_COLOR_ARRAY_STRIDE: v[0] = (float)c->ca.stride; return 1;
    case GL_NORMAL_ARRAY_TYPE: v[0] = (float)c->na.type; return 1;
    case GL_NORMAL_ARRAY_STRIDE: v[0] = (float)c->na.stride; return 1;
    case GL_TEXTURE_COORD_ARRAY_SIZE: v[0] = (float)c->ta[c->client_active].size; return 1;
    case GL_TEXTURE_COORD_ARRAY_TYPE: v[0] = (float)c->ta[c->client_active].type; return 1;
    case GL_TEXTURE_COORD_ARRAY_STRIDE: v[0] = (float)c->ta[c->client_active].stride; return 1;
    case GL_VERTEX_ARRAY: case GL_COLOR_ARRAY: case GL_NORMAL_ARRAY: case GL_TEXTURE_COORD_ARRAY:
        v[0] = GLIsEnabled(c, pname) ? 1.0f : 0.0f; return 1;
    }
    bit = cap_bit(c, pname);
    if (bit) { v[0] = (c->en & bit) ? 1.0f : 0.0f; return 1; }
    if (cap_known(pname)) { v[0] = 0; return 1; }
    return 0;
}

void GLGetFloatv(GLcontext c, GLenum pname, GLfloat *params)
{
    float v[16];
    int i, n;
    if (!params) return;
    n = get_values(c, pname, v);
    if (!n) { mgl_error(c, GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++) params[i] = v[i];
}

void GLGetDoublev(GLcontext c, GLenum pname, GLdouble *params)
{
    float v[16];
    int i, n;
    if (!params) return;
    n = get_values(c, pname, v);
    if (!n) { mgl_error(c, GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++) params[i] = v[i];
}

/* Colours and the like as GL turns them into integers: the full range. */
static int is_colourish(GLenum p)
{
    return p == GL_CURRENT_COLOR || p == GL_COLOR_CLEAR_VALUE || p == GL_FOG_COLOR || p == GL_LIGHT_MODEL_AMBIENT
        || p == GL_DEPTH_CLEAR_VALUE || p == GL_DEPTH_RANGE || p == GL_ALPHA_TEST_REF;
}

void GLGetIntegerv(GLcontext c, GLenum pname, GLint *params)
{
    float v[16];
    int i, n;
    if (!params) return;
    n = get_values(c, pname, v);
    if (!n) { mgl_error(c, GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++) {
        if (is_colourish(pname)) {
            float f = v[i] < -1 ? -1 : v[i] > 1 ? 1 : v[i];
            params[i] = (GLint)((double)f * 2147483647.0);
        } else params[i] = (GLint)mgl_f2l(v[i]);
    }
}

void GLGetBooleanv(GLcontext c, GLenum pname, GLboolean *params)
{
    float v[16];
    int i, n;
    if (!params) return;
    n = get_values(c, pname, v);
    if (!n) { mgl_error(c, GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++) params[i] = v[i] != 0.0f ? GL_TRUE : GL_FALSE;
}

void GLGetPointerv(GLcontext c, GLenum pname, GLvoid **params)
{
    if (!params) return;
    switch (pname) {
    case GL_VERTEX_ARRAY_POINTER: *params = (GLvoid *)c->va.ptr; break;
    case GL_COLOR_ARRAY_POINTER: *params = (GLvoid *)c->ca.ptr; break;
    case GL_NORMAL_ARRAY_POINTER: *params = (GLvoid *)c->na.ptr; break;
    case GL_TEXTURE_COORD_ARRAY_POINTER: *params = (GLvoid *)c->ta[c->client_active].ptr; break;
    case GL_INDEX_ARRAY_POINTER: case GL_EDGE_FLAG_ARRAY_POINTER: *params = 0; break;
    default: mgl_error(c, GL_INVALID_ENUM);
    }
}

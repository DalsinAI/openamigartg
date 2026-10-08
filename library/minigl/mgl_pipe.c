/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's vertex pipeline: each vertex is transformed, lit, given
 * its texture coordinates and its fog factor (mgl_process); triangles, lines
 * and points are clipped, projected to the window, culled and handed to the
 * batch as the stream's vertices (struct ovtx). Lines and points become
 * quads, as the core draws only triangles.
 *
 * Clipping is in clip space. Against near and far always; against the
 * sides only past a guard band, as the core clips to the drawing area
 * itself and only needs coordinates within +-16383.
 *
 * Fog is worked out per vertex from the eye distance and goes to the core
 * as the vertex's fog factor (OGPU_FOG_VERTEX), which is what OpenGL 1.1
 * allows. A second texture unit is drawn as a second pass over the same
 * triangles, blended onto the first. */
#include "mgl_internal.h"

/* ---- one vertex ---------------------------------------------------------------------------- */

/* n.h to the material's shininess, from a table made when the shininess changes. */
static float spec_power(GLcontext c, float shin, float nh)
{
    LONG k;
    if (nh <= 0.0f) return 0.0f;
    if (shin == 0.0f) return 1.0f;
    if (shin != c->spec_table_for) {
        int i;
        for (i = 0; i < 512; i++) c->spec_table[i] = mgl_pow((float)i * (1.0f / 511.0f), shin);
        c->spec_table_for = shin;
    }
    k = mgl_f2l(nh * 511.0f);
    return c->spec_table[k > 511 ? 511 : k];
}

/* OpenGL 1.1's lighting equation for the front face. */
static void light_vertex(GLcontext c, const float *eye, const float *n, const float *color, float *out)
{
    const struct mgl_material *m = &c->mat[0];
    const float *amb = m->ambient, *dif = m->diffuse, *spe = m->specular, *emi = m->emission;
    float r, g, b;
    int i, has_spec;
    if ((c->en & EN_COLORMAT) && c->colormat_face != GL_BACK) {
        switch (c->colormat_mode) {
        case GL_AMBIENT: amb = color; break;
        case GL_DIFFUSE: dif = color; break;
        case GL_SPECULAR: spe = color; break;
        case GL_EMISSION: emi = color; break;
        default: amb = dif = color; break;
        }
    }
    has_spec = spe[0] != 0.0f || spe[1] != 0.0f || spe[2] != 0.0f;
    r = emi[0] + c->light_ambient[0] * amb[0];
    g = emi[1] + c->light_ambient[1] * amb[1];
    b = emi[2] + c->light_ambient[2] * amb[2];
    for (i = 0; i < 8; i++) {
        const struct mgl_light *l = &c->light[i];
        float Lb[3], att = 1.0f, nl, f;
        const float *L;
        if (!(c->en & (1UL << (EN_LIGHT0 + i)))) continue;
        if (l->position[3] != 0.0f) {
            float il, d2;
            Lb[0] = l->position[0] - eye[0]; Lb[1] = l->position[1] - eye[1]; Lb[2] = l->position[2] - eye[2];
            d2 = Lb[0] * Lb[0] + Lb[1] * Lb[1] + Lb[2] * Lb[2];
            il = mgl_rsqrt(d2);
            Lb[0] *= il; Lb[1] *= il; Lb[2] *= il;
            if (l->att[1] != 0.0f || l->att[2] != 0.0f || l->att[0] != 1.0f) {
                float d = d2 * il, den = l->att[0] + l->att[1] * d + l->att[2] * d2;
                att = den > 0.0f ? 1.0f / den : 1.0f;
            }
            if (l->spot_cutoff != 180.0f) {
                float sd = -(Lb[0] * l->direction[0] + Lb[1] * l->direction[1] + Lb[2] * l->direction[2]);
                float dl = mgl_rsqrt(l->direction[0] * l->direction[0] + l->direction[1] * l->direction[1]
                                     + l->direction[2] * l->direction[2]);
                sd *= dl;
                if (sd < l->spot_cos) continue;
                if (l->spot_exp != 0.0f) att *= mgl_pow(sd, l->spot_exp);
            }
            L = Lb;
        } else L = l->L;
        r += att * l->ambient[0] * amb[0];
        g += att * l->ambient[1] * amb[1];
        b += att * l->ambient[2] * amb[2];
        nl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
        if (nl <= 0.0f) continue;
        f = att * nl;
        r += f * l->diffuse[0] * dif[0];
        g += f * l->diffuse[1] * dif[1];
        b += f * l->diffuse[2] * dif[2];
        if (has_spec) {
            float nh, s;
            if (l->position[3] == 0.0f && !c->local_viewer) nh = n[0] * l->H[0] + n[1] * l->H[1] + n[2] * l->H[2];
            else {
                float H[3];
                if (c->local_viewer) {
                    float iv = mgl_rsqrt(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
                    H[0] = L[0] - eye[0] * iv; H[1] = L[1] - eye[1] * iv; H[2] = L[2] - eye[2] * iv;
                } else { H[0] = L[0]; H[1] = L[1]; H[2] = L[2] + 1.0f; }
                nh = (n[0] * H[0] + n[1] * H[1] + n[2] * H[2]) * mgl_rsqrt(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
            }
            s = spec_power(c, m->shininess, nh) * att;
            r += s * l->specular[0] * spe[0];
            g += s * l->specular[1] * spe[1];
            b += s * l->specular[2] * spe[2];
        }
    }
    out[0] = r; out[1] = g; out[2] = b; out[3] = dif[3];
}

static float gen_coord(GLcontext c, int u, int k, const float *obj, const float *eye, const float *sphere)
{
    const float *p;
    switch (c->gen_mode[u][k]) {
    case GL_OBJECT_LINEAR:
        p = c->gen_obj[u][k];
        return p[0] * obj[0] + p[1] * obj[1] + p[2] * obj[2] + p[3] * obj[3];
    case GL_SPHERE_MAP:
        return k < 2 ? sphere[k] : 0.0f;
    default:
        p = c->gen_eye[u][k];
        return p[0] * eye[0] + p[1] * eye[1] + p[2] * eye[2] + p[3] * eye[3];
    }
}

static void tex_coords(GLcontext c, int u, const float *in, const float *obj, const float *eye, const float *n,
                       float *s, float *t, float *q)
{
    float tc[4], sphere[2];
    ULONG gs = u ? EN_GENS1 : EN_GENS0, gt = u ? EN_GENT1 : EN_GENT0;
    tc[0] = in[0]; tc[1] = in[1]; tc[2] = in[2]; tc[3] = in[3];
    if (c->en & (gs | gt)) {
        if (c->gen_mode[u][0] == GL_SPHERE_MAP || c->gen_mode[u][1] == GL_SPHERE_MAP) {
            float iu = mgl_rsqrt(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
            float ux = eye[0] * iu, uy = eye[1] * iu, uz = eye[2] * iu;
            float nu = n[0] * ux + n[1] * uy + n[2] * uz;
            float rx = ux - 2.0f * n[0] * nu, ry = uy - 2.0f * n[1] * nu, rz = uz - 2.0f * n[2] * nu + 1.0f;
            float m = 2.0f * mgl_sqrt(rx * rx + ry * ry + rz * rz);
            float im = m > 0.0f ? 1.0f / m : 0.0f;
            sphere[0] = rx * im + 0.5f;
            sphere[1] = ry * im + 0.5f;
        }
        if (c->en & gs) tc[0] = gen_coord(c, u, 0, obj, eye, sphere);
        if (c->en & gt) tc[1] = gen_coord(c, u, 1, obj, eye, sphere);
    }
    if (u == 0 && (c->en & EN_GENQ0)) tc[3] = gen_coord(c, 0, 3, obj, eye, sphere);
    if (!c->tex_identity[u]) {
        const float *m = c->texm[u][c->tex_top[u]];
        float r[4];
        int i;
        for (i = 0; i < 4; i++) r[i] = m[i] * tc[0] + m[4 + i] * tc[1] + m[8 + i] * tc[2] + m[12 + i] * tc[3];
        tc[0] = r[0]; tc[1] = r[1]; tc[3] = r[3];
    }
    *s = tc[0]; *t = tc[1]; *q = tc[3];
}

static ULONG clip_code(GLcontext c, const struct mgl_pv *v)
{
    ULONG k = 0;
    float g = c->gb * v->w;
    if (v->w <= 1e-7f) k |= CLIP_W;
    if (v->x < -g) k |= CLIP_XN;
    if (v->x > g) k |= CLIP_XP;
    if (v->y < -g) k |= CLIP_YN;
    if (v->y > g) k |= CLIP_YP;
    if (v->z < -v->w) k |= CLIP_ZN;
    if (v->z > v->w) k |= CLIP_ZP;
    return k;
}

void mgl_process(GLcontext c, struct mgl_pv *pv, const float *obj, const float *color, const float *normal,
                 const float (*tc)[4])
{
    ULONG en = c->en;
    int need_eye = (en & (EN_LIGHTING | EN_FOG)) != 0;
    int need_n = (en & EN_LIGHTING) != 0;
    float eye[4], n[3];
    if (en & (EN_GENS0 | EN_GENT0 | EN_GENS1 | EN_GENT1 | EN_GENQ0)) need_eye = need_n = 1;
    if (c->mvp_dirty) mgl_update_mvp(c);
    if (need_eye) {
        const float *m = c->mv[c->mv_top], *p = c->proj[c->proj_top];
        eye[0] = m[0] * obj[0] + m[4] * obj[1] + m[8] * obj[2] + m[12] * obj[3];
        eye[1] = m[1] * obj[0] + m[5] * obj[1] + m[9] * obj[2] + m[13] * obj[3];
        eye[2] = m[2] * obj[0] + m[6] * obj[1] + m[10] * obj[2] + m[14] * obj[3];
        eye[3] = m[3] * obj[0] + m[7] * obj[1] + m[11] * obj[2] + m[15] * obj[3];
        if (c->proj_kind == 1) {                /* glFrustum's shape */
            pv->x = p[0] * eye[0] + p[8] * eye[2];
            pv->y = p[5] * eye[1] + p[9] * eye[2];
            pv->z = p[10] * eye[2] + p[14] * eye[3];
            pv->w = -eye[2];
        } else {
            pv->x = p[0] * eye[0] + p[4] * eye[1] + p[8] * eye[2] + p[12] * eye[3];
            pv->y = p[1] * eye[0] + p[5] * eye[1] + p[9] * eye[2] + p[13] * eye[3];
            pv->z = p[2] * eye[0] + p[6] * eye[1] + p[10] * eye[2] + p[14] * eye[3];
            pv->w = p[3] * eye[0] + p[7] * eye[1] + p[11] * eye[2] + p[15] * eye[3];
        }
    } else {
        const float *m = c->mvp;
        pv->x = m[0] * obj[0] + m[4] * obj[1] + m[8] * obj[2] + m[12] * obj[3];
        pv->y = m[1] * obj[0] + m[5] * obj[1] + m[9] * obj[2] + m[13] * obj[3];
        pv->z = m[2] * obj[0] + m[6] * obj[1] + m[10] * obj[2] + m[14] * obj[3];
        pv->w = m[3] * obj[0] + m[7] * obj[1] + m[11] * obj[2] + m[15] * obj[3];
    }
    if (need_n) {
        float l;
        if (c->nm_dirty) mgl_update_nm(c);
        n[0] = c->nm[0] * normal[0] + c->nm[1] * normal[1] + c->nm[2] * normal[2];
        n[1] = c->nm[3] * normal[0] + c->nm[4] * normal[1] + c->nm[5] * normal[2];
        n[2] = c->nm[6] * normal[0] + c->nm[7] * normal[1] + c->nm[8] * normal[2];
        if (en & EN_NORMALIZE) {
            l = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
            if (l > 0.0f) { l = mgl_rsqrt(l); n[0] *= l; n[1] *= l; n[2] *= l; }
        }
    }
    if (en & EN_LIGHTING) {
        float lit[4];
        light_vertex(c, eye, n, color, lit);
        pv->r = lit[0]; pv->g = lit[1]; pv->b = lit[2]; pv->a = lit[3];
    } else {
        pv->r = color[0]; pv->g = color[1]; pv->b = color[2]; pv->a = color[3];
    }
    if (en & EN_TEX0) tex_coords(c, 0, tc[0], obj, eye, n, &pv->s0, &pv->t0, &pv->q0);
    else { pv->s0 = pv->t0 = 0.0f; pv->q0 = 1.0f; }
    if (en & EN_TEX1) tex_coords(c, 1, tc[1], obj, eye, n, &pv->s1, &pv->t1, &pv->q1);
    else { pv->s1 = pv->t1 = 0.0f; pv->q1 = 1.0f; }
    if (en & EN_FOG) {
        float z = eye[2] < 0.0f ? -eye[2] : eye[2], f;
        switch (c->fog_mode) {
        case GL_LINEAR:
            f = c->fog_end != c->fog_start ? (c->fog_end - z) / (c->fog_end - c->fog_start) : 1.0f;
            break;
        case GL_EXP: f = mgl_exp(-c->fog_density * z); break;
        default: { float dz = c->fog_density * z; f = mgl_exp(-dz * dz); }
        }
        pv->fog = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    } else pv->fog = 1.0f;
    pv->edge = (UBYTE)c->edge;
    pv->proj = 0;
    pv->clip = clip_code(c, pv);
    c->verts++;
}

/* A flat primitive's colour, from its provoking vertex (which may lie outside). */
static void prov_colour(struct mgl_pv *v)
{
    float col[4];
    col[0] = v->r; col[1] = v->g; col[2] = v->b; col[3] = v->a;
    v->argb = mgl_argb(col);
}

static LONG fix16(float f)
{
    if (f > 32000.0f) f = 32000.0f;
    if (f < -32000.0f) f = -32000.0f;
    return mgl_f2l(f * 65536.0f);
}

static void to_ovtx(GLcontext c, const struct mgl_pv *v, struct ovtx *o, ULONG argb, ULONG zadd, int unit)
{
    float q = unit ? v->q1 : v->q0, s = unit ? v->s1 : v->s0, t = unit ? v->t1 : v->t0, iq, w;
    LONG fz;
    (void)c;
    if (q == 0.0f) q = 1e-6f;
    iq = 1.0f / q;
    w = q * v->iw;
    if (!(w > 0.0f)) w = 1e-6f;
    PUTW(&o->x, mgl_f2l(v->wx * 65536.0f));
    PUTW(&o->y, mgl_f2l(v->wy * 65536.0f));
    PUTW(&o->argb, argb);
    PUTW(&o->u, fix16(s * iq));
    PUTW(&o->v, fix16(t * iq));
    {
        ULONG z = v->wz;
        if (zadd) {
            fz = (LONG)zadd;
            if (fz > 0) z = z > 0xFFFFFFFFUL - (ULONG)fz ? 0xFFFFFFFFUL : z + (ULONG)fz;
            else z = z < (ULONG)-fz ? 0 : z - (ULONG)-fz;
        }
        PUTW(&o->z, z);
    }
    PUTW(&o->w, mgl_fbits(w));
    {
        LONG f = mgl_f2l(v->fog * 255.0f);
        PUTW(&o->spec, (ULONG)(f < 0 ? 0 : f > 255 ? 255 : f) << 24);
    }
}

/* ---- to the window ------------------------------------------------------------------------- */

static void project(GLcontext c, struct mgl_pv *v)
{
    float iw = 1.0f / v->w, z;
    float col[4];
    double dz;
    v->iw = iw;
    v->wx = v->x * iw * c->vsx + c->vtx;
    v->wy = v->y * iw * c->vsy + c->vty;
    z = v->z * iw * 0.5f + 0.5f;
    dz = c->depth_near + (double)z * (c->depth_far - c->depth_near);
    if (dz <= 0.0) v->wz = 0;
    else if (dz >= 1.0) v->wz = 0xFFFFFFFFUL;
    else v->wz = (ULONG)mgl_f2l((float)(dz * 1073741823.0)) << 2;
    col[0] = v->r; col[1] = v->g; col[2] = v->b; col[3] = v->a;
    v->argb = mgl_argb(col);
    v->proj = 1;
    to_ovtx(c, v, &v->o, v->argb, 0, 0);
}

/* ---- the second texture unit: a second pass ------------------------------------------------ */

static int two_pass(GLcontext c)
{
    return (c->en & EN_TEX1) && mgl_tex_unit(c, 1) != 0;
}

void mgl_pipe_flush(GLcontext c)
{
    struct ovtx *o;
    ULONG n;
    if (!c->mt_n) return;
    n = (ULONG)c->mt_n * 3;
    c->mt_n = 0;
    c->pass = 0;
    o = mgl_vspace(c, n);
    if (o) { mgl_copy(o, c->mt0, n * sizeof(struct ovtx)); mgl_emit(c, o, n, OGPU_LAY_LIST); }
    c->pass = 1;
    o = mgl_vspace(c, n);
    if (o) { mgl_copy(o, c->mt1, n * sizeof(struct ovtx)); mgl_emit(c, o, n, OGPU_LAY_LIST); }
    c->pass = 0;
}

/* ---- triangles ------------------------------------------------------------------------------ */

static ULONG offset_of(GLcontext c, const struct mgl_pv *a, const struct mgl_pv *b, const struct mgl_pv *d)
{
    float off = 0.0f;
    if ((c->en & EN_OFFSET) && (c->offset_factor != 0.0f || c->offset_units != 0.0f)) {
        /* the steepest slope of z in the window, from the triangle's plane */
        float x1 = b->wx - a->wx, y1 = b->wy - a->wy, x2 = d->wx - a->wx, y2 = d->wy - a->wy;
        float z0 = (float)a->wz * (1.0f / 4294967296.0f);
        float z1 = (float)b->wz * (1.0f / 4294967296.0f) - z0, z2 = (float)d->wz * (1.0f / 4294967296.0f) - z0;
        float det = x1 * y2 - x2 * y1, m = 0.0f;
        if (det != 0.0f) {
            float dzx = (z1 * y2 - z2 * y1) / det, dzy = (x1 * z2 - x2 * z1) / det;
            if (dzx < 0) dzx = -dzx;
            if (dzy < 0) dzy = -dzy;
            m = dzx > dzy ? dzx : dzy;
        }
        off = c->offset_factor * m + c->offset_units * (c->zfmt == OGPU_FMT_Z32 ? 1.0f / 16777216.0f : 1.0f / 65536.0f);
    }
    if (c->en & EN_ZOFFSET) off += c->zoffset;
    if (off == 0.0f) return 0;
    if (off > 0.5f) off = 0.5f;
    if (off < -0.5f) off = -0.5f;
    return (ULONG)mgl_f2l(off * 4294967296.0f * 0.5f) * 2;
}

static void draw_edges(GLcontext c, struct mgl_pv **v, int n, const UBYTE *edges, struct mgl_pv *prov, int points)
{
    int i;
    for (i = 0; i < n; i++) {
        if (points) mgl_point(c, v[i]);
        else if (!edges || edges[i]) mgl_line(c, v[i], v[(i + 1) % n], prov);
    }
}

/* A triangle already inside (projected), filled. */
static void fill_tri(GLcontext c, struct mgl_pv *a, struct mgl_pv *b, struct mgl_pv *d, struct mgl_pv *prov)
{
    struct ovtx *o;
    ULONG zo = offset_of(c, a, b, d);
    ULONG ca = prov ? prov->argb : a->argb, cb = prov ? prov->argb : b->argb, cd = prov ? prov->argb : d->argb;
    c->tris++;
    if (two_pass(c)) {
        if (c->mt_n >= MT_TRIS) mgl_pipe_flush(c);
        o = &c->mt0[c->mt_n * 3];
        to_ovtx(c, a, o, ca, zo, 0); to_ovtx(c, b, o + 1, cb, zo, 0); to_ovtx(c, d, o + 2, cd, zo, 0);
        o = &c->mt1[c->mt_n * 3];
        to_ovtx(c, a, o, ca, zo, 1); to_ovtx(c, b, o + 1, cb, zo, 1); to_ovtx(c, d, o + 2, cd, zo, 1);
        c->mt_n++;
        return;
    }
    o = mgl_vspace(c, 3);
    if (!o) return;
    if (!prov && !zo) { o[0] = a->o; o[1] = b->o; o[2] = d->o; }
    else {
        to_ovtx(c, a, o, ca, zo, 0);
        to_ovtx(c, b, o + 1, cb, zo, 0);
        to_ovtx(c, d, o + 2, cd, zo, 0);
    }
    mgl_emit(c, o, 3, OGPU_LAY_LIST);
}

/* A polygon inside the clip volume: culled, then filled as a fan or drawn as
 * lines or points. edges[i]: whether the edge from v[i] to v[i+1] is drawn
 * in line mode. */
static void polygon(GLcontext c, struct mgl_pv **v, int n, const UBYTE *edges, struct mgl_pv *prov)
{
    float area = 0.0f;
    int i, front;
    GLenum mode;
    for (i = 0; i < n; i++) if (!v[i]->proj) project(c, v[i]);
    if (prov) prov_colour(prov);
    for (i = 1; i + 1 < n; i++)
        area += (v[i]->wx - v[0]->wx) * (v[i + 1]->wy - v[0]->wy) - (v[i + 1]->wx - v[0]->wx) * (v[i]->wy - v[0]->wy);
    /* the window's y runs down: OpenGL's counter-clockwise is clockwise here */
    front = c->front_face == GL_CCW ? area < 0.0f : area > 0.0f;
    if (c->en & EN_CULL) {
        if (c->cull_mode == GL_FRONT_AND_BACK) return;
        if (c->cull_mode == GL_BACK && !front) return;
        if (c->cull_mode == GL_FRONT && front) return;
    }
    if (c->min_tri_area > 0.0f && (area < 0 ? -area : area) * 0.5f < c->min_tri_area) return;
    mode = front ? c->poly_front : c->poly_back;
    if (mode != GL_FILL) { draw_edges(c, v, n, edges, prov, mode == GL_POINT); return; }
    for (i = 1; i + 1 < n; i++) fill_tri(c, v[0], v[i], v[i + 1], prov);
}

/* ---- clipping -------------------------------------------------------------------------------- */

static void lerp(struct mgl_pv *o, const struct mgl_pv *a, const struct mgl_pv *b, float t)
{
    o->x = a->x + (b->x - a->x) * t;
    o->y = a->y + (b->y - a->y) * t;
    o->z = a->z + (b->z - a->z) * t;
    o->w = a->w + (b->w - a->w) * t;
    o->r = a->r + (b->r - a->r) * t;
    o->g = a->g + (b->g - a->g) * t;
    o->b = a->b + (b->b - a->b) * t;
    o->a = a->a + (b->a - a->a) * t;
    o->s0 = a->s0 + (b->s0 - a->s0) * t;
    o->t0 = a->t0 + (b->t0 - a->t0) * t;
    o->q0 = a->q0 + (b->q0 - a->q0) * t;
    o->s1 = a->s1 + (b->s1 - a->s1) * t;
    o->t1 = a->t1 + (b->t1 - a->t1) * t;
    o->q1 = a->q1 + (b->q1 - a->q1) * t;
    o->fog = a->fog + (b->fog - a->fog) * t;
    o->proj = 0;
    o->clip = 0;
}

/* Signed distance to a plane: inside >= 0. */
static float dist(GLcontext c, const struct mgl_pv *v, ULONG plane)
{
    float g = c->gb;
    switch (plane) {
    case CLIP_XN: return v->x + g * v->w;
    case CLIP_XP: return g * v->w - v->x;
    case CLIP_YN: return v->y + g * v->w;
    case CLIP_YP: return g * v->w - v->y;
    case CLIP_ZN: return v->z + v->w;
    case CLIP_ZP: return v->w - v->z;
    default: return v->w - 1e-6f;            /* CLIP_W */
    }
}

/* Clips the polygon in[n] against the planes in `planes` (Sutherland and
 * Hodgman); the result in out, pointing at in's vertices or new ones in
 * c->clipv. ein[i] says whether the edge from in[i] to in[i+1] is drawn in
 * line mode; edges made along a plane are. */
static int clip_poly(GLcontext c, struct mgl_pv **in, UBYTE *ein, int n, ULONG planes, struct mgl_pv **out, UBYTE *eout)
{
    static const ULONG order[7] = { CLIP_W, CLIP_ZN, CLIP_ZP, CLIP_XN, CLIP_XP, CLIP_YN, CLIP_YP };
    struct mgl_pv *bufa[CLIP_MAX], *bufb[CLIP_MAX], **src = in, **dst = bufa;
    UBYTE ea[CLIP_MAX], eb[CLIP_MAX], *es = ein, *ed = ea;
    int p, i, m, used = 0;
    if (planes & (CLIP_ZN | CLIP_XN | CLIP_XP | CLIP_YN | CLIP_YP)) planes |= CLIP_W;
    for (p = 0; p < 7; p++) {
        ULONG pl = order[p];
        if (!(planes & pl)) continue;
        m = 0;
        for (i = 0; i < n && m < CLIP_MAX - 2; i++) {
            struct mgl_pv *a = src[i], *b = src[i + 1 < n ? i + 1 : 0];
            float da = dist(c, a, pl), db = dist(c, b, pl);
            if (da >= 0.0f) { dst[m] = a; ed[m] = es[i]; m++; }
            if ((da >= 0.0f) != (db >= 0.0f) && used < CLIP_MAX * 7) {
                struct mgl_pv *o = &c->clipv[used++];
                lerp(o, a, b, da / (da - db));
                dst[m] = o;
                ed[m] = da >= 0.0f ? 1 : es[i];  /* leaving: the edge along the plane; entering: the rest of edge i */
                m++;
            }
        }
        n = m;
        if (n < 3) return 0;
        src = dst; es = ed;
        dst = dst == bufa ? bufb : bufa;
        ed = ed == ea ? eb : ea;
    }
    for (i = 0; i < n; i++) { out[i] = src[i]; eout[i] = es[i]; }
    return n;
}

void mgl_polygon(GLcontext c, struct mgl_pv **v, int n, const UBYTE *edges, struct mgl_pv *prov)
{
    ULONG or = 0, and = 0xFFFFFFFFUL;
    int i;
    UBYTE e[CLIP_MAX];
    for (i = 0; i < n; i++) { or |= v[i]->clip; and &= v[i]->clip; e[i] = edges ? edges[i] : 1; }
    if (and) return;
    if (!or) { polygon(c, v, n, edges, prov); return; }
    {
        struct mgl_pv *out[CLIP_MAX];
        UBYTE eo[CLIP_MAX];
        int m;
        if (n > 8) {                            /* clip big polygons a fan triangle at a time */
            for (i = 1; i + 1 < n; i++) {
                struct mgl_pv *t[3];
                UBYTE te[3];
                t[0] = v[0]; t[1] = v[i]; t[2] = v[i + 1];
                te[0] = i == 1 ? e[0] : 0; te[1] = e[i]; te[2] = i + 2 == n ? e[n - 1] : 0;
                mgl_polygon(c, t, 3, te, prov);
            }
            return;
        }
        m = clip_poly(c, v, e, n, or, out, eo);
        if (m >= 3) polygon(c, out, m, eo, prov);
    }
}

void mgl_triangle(GLcontext c, struct mgl_pv *a, struct mgl_pv *b, struct mgl_pv *d, struct mgl_pv *prov)
{
    struct mgl_pv *v[3];
    UBYTE e[3];
    if (!((a->clip | b->clip | d->clip))) {
        /* the common case: inside, filled */
        if (c->poly_front == GL_FILL && c->poly_back == GL_FILL) {
            float area;
            if (!a->proj) project(c, a);
            if (!b->proj) project(c, b);
            if (!d->proj) project(c, d);
            if (prov) prov_colour(prov);
            area = (b->wx - a->wx) * (d->wy - a->wy) - (d->wx - a->wx) * (b->wy - a->wy);
            if (c->en & EN_CULL) {
                int front = c->front_face == GL_CCW ? area < 0.0f : area > 0.0f;
                if (c->cull_mode == GL_FRONT_AND_BACK) return;
                if (c->cull_mode == GL_BACK && !front) return;
                if (c->cull_mode == GL_FRONT && front) return;
            }
            if (c->min_tri_area > 0.0f && (area < 0 ? -area : area) * 0.5f < c->min_tri_area) return;
            fill_tri(c, a, b, d, prov);
            return;
        }
    }
    v[0] = a; v[1] = b; v[2] = d;
    e[0] = a->edge; e[1] = b->edge; e[2] = d->edge;
    mgl_polygon(c, v, 3, e, prov);
}

/* ---- lines and points: quads ---------------------------------------------------------------- */

static void quad(GLcontext c, struct mgl_pv *p0, struct mgl_pv *p1, float ox, float oy, float ex, float ey,
                 ULONG argb0, ULONG argb1)
{
    /* corners: p0 - o, p0 + o, p1 - o, p1 + o, each pushed along e (the line's ends) */
    struct mgl_pv q[4];
    struct ovtx *o;
    q[0] = *p0; q[1] = *p0; q[2] = *p1; q[3] = *p1;
    q[0].wx = p0->wx - ox - ex; q[0].wy = p0->wy - oy - ey;
    q[1].wx = p0->wx + ox - ex; q[1].wy = p0->wy + oy - ey;
    q[2].wx = p1->wx - ox + ex; q[2].wy = p1->wy - oy + ey;
    q[3].wx = p1->wx + ox + ex; q[3].wy = p1->wy + oy + ey;
    o = mgl_vspace(c, 6);
    if (!o) return;
    to_ovtx(c, &q[0], o, argb0, 0, 0);
    to_ovtx(c, &q[1], o + 1, argb0, 0, 0);
    to_ovtx(c, &q[2], o + 2, argb1, 0, 0);
    to_ovtx(c, &q[1], o + 3, argb0, 0, 0);
    to_ovtx(c, &q[3], o + 4, argb1, 0, 0);
    to_ovtx(c, &q[2], o + 5, argb1, 0, 0);
    mgl_emit(c, o, 6, OGPU_LAY_LIST);
}

void mgl_line(GLcontext c, struct mgl_pv *a, struct mgl_pv *b, struct mgl_pv *prov)
{
    struct mgl_pv ca, cb, *p0 = a, *p1 = b;
    float dx, dy, l, hw;
    ULONG k = a->clip | b->clip;
    if (a->clip & b->clip) return;
    if (k) {
        /* clip the segment against each plane it crosses */
        static const ULONG order[7] = { CLIP_W, CLIP_ZN, CLIP_ZP, CLIP_XN, CLIP_XP, CLIP_YN, CLIP_YP };
        float t0 = 0.0f, t1 = 1.0f;
        int p;
        for (p = 0; p < 7; p++) {
            float da = dist(c, a, order[p]), db = dist(c, b, order[p]);
            if (da < 0.0f && db < 0.0f) return;
            if (da < 0.0f) { float t = da / (da - db); if (t > t0) t0 = t; }
            else if (db < 0.0f) { float t = da / (da - db); if (t < t1) t1 = t; }
        }
        if (t0 >= t1) return;
        lerp(&ca, a, b, t0); lerp(&cb, a, b, t1);
        p0 = &ca; p1 = &cb;
    }
    if (!p0->proj) project(c, p0);
    if (!p1->proj) project(c, p1);
    if (prov) prov_colour(prov);
    dx = p1->wx - p0->wx; dy = p1->wy - p0->wy;
    l = dx * dx + dy * dy;
    hw = (c->line_width < 1.0f ? 1.0f : c->line_width) * 0.5f;
    if (l < 1e-6f) { dx = 1.0f; dy = 0.0f; } else { l = mgl_rsqrt(l); dx *= l; dy *= l; }
    /* the perpendicular for the width; half a pixel along the line at each end */
    quad(c, p0, p1, -dy * hw, dx * hw, dx * 0.5f, dy * 0.5f,
         prov ? prov->argb : p0->argb, prov ? prov->argb : p1->argb);
}

void mgl_point(GLcontext c, struct mgl_pv *a)
{
    float h = (c->point_size < 1.0f ? 1.0f : c->point_size) * 0.5f;
    if (a->clip) return;
    if (!a->proj) project(c, a);
    quad(c, a, a, 0.0f, h, h, 0.0f, a->argb, a->argb);
}

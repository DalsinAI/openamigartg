/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: display lists. While a list is compiled, the calls a
 * list can hold are recorded (their arguments as words, floats by their
 * bits) instead of being done, or as well as being done with
 * GL_COMPILE_AND_EXECUTE. glCallList plays them back through the same
 * calls. Array draws are recorded as the vertices they give, as OpenGL
 * says. Calls a list cannot hold (queries, textures' texels, client state)
 * are done at once, also as OpenGL says. */
#include "mgl_internal.h"

ULONG mgl_fu(float f)
{
    union { float f; ULONG u; } x;
    x.f = f;
    return x.u;
}

static float uf(ULONG u)
{
    union { float f; ULONG u; } x;
    x.u = u;
    return x.f;
}

static struct mgl_list *find_list(GLcontext c, GLuint name)
{
    struct mgl_list *l;
    for (l = c->list_hash[name & 31]; l; l = l->next) if (l->name == name) return l;
    return 0;
}

static struct mgl_list *new_list(GLcontext c, GLuint name)
{
    struct mgl_list *l = mgl_alloc(sizeof *l);
    if (!l) { mgl_error(c, GL_OUT_OF_MEMORY); return 0; }
    l->name = name;
    l->next = c->list_hash[name & 31];
    c->list_hash[name & 31] = l;
    return l;
}

static void empty_list(struct mgl_list *l)
{
    mgl_free(l->ops);
    l->ops = 0;
    l->used = l->cap = 0;
}

int mgl_record(GLcontext c, int op, int n, const ULONG *args)
{
    struct mgl_list *l = c->compiling;
    int i;
    if (l->used + 1 + (ULONG)n > l->cap) {
        ULONG cap = l->cap ? l->cap * 2 : 256, *ops;
        while (cap < l->used + 1 + (ULONG)n) cap *= 2;
        ops = mgl_alloc(cap * 4);
        if (!ops) { mgl_error(c, GL_OUT_OF_MEMORY); return c->compile_mode == GL_COMPILE_AND_EXECUTE; }
        if (l->ops) { mgl_copy(ops, l->ops, l->used * 4); mgl_free(l->ops); }
        l->ops = ops;
        l->cap = cap;
    }
    l->ops[l->used++] = ((ULONG)op << 16) | (ULONG)n;
    for (i = 0; i < n; i++) l->ops[l->used++] = args[i];
    return c->compile_mode == GL_COMPILE_AND_EXECUTE;
}

void mgl_free_lists(GLcontext c)
{
    int i;
    for (i = 0; i < 32; i++) {
        struct mgl_list *l = c->list_hash[i], *n;
        for (; l; l = n) { n = l->next; empty_list(l); mgl_free(l); }
        c->list_hash[i] = 0;
    }
    c->compiling = 0;
}

GLuint GLGenLists(GLcontext c, GLsizei range)
{
    GLuint first, k;
    GLsizei i;
    if (!c) return 0;
    if (range <= 0) { mgl_error(c, GL_INVALID_VALUE); return 0; }
    /* the first run of `range` unused names */
    for (first = c->next_list ? c->next_list : 1;; first++) {
        for (i = 0; i < range; i++) if (find_list(c, first + (GLuint)i)) break;
        if (i == range) break;
        first += (GLuint)i;
    }
    for (k = 0; k < (GLuint)range; k++) new_list(c, first + k);
    c->next_list = first + (GLuint)range;
    return first;
}

void GLDeleteLists(GLcontext c, GLuint list, GLsizei range)
{
    GLsizei i;
    if (!c) return;
    if (range < 0) { mgl_error(c, GL_INVALID_VALUE); return; }
    for (i = 0; i < range; i++) {
        struct mgl_list **pp, *l;
        for (pp = &c->list_hash[(list + (GLuint)i) & 31]; (l = *pp) != 0; pp = &l->next)
            if (l->name == list + (GLuint)i) break;
        if (!l || l == c->compiling) continue;
        *pp = l->next;
        empty_list(l);
        mgl_free(l);
    }
}

GLboolean GLIsList(GLcontext c, GLuint list)
{
    return c && find_list(c, list) ? GL_TRUE : GL_FALSE;
}

void GLNewList(GLcontext c, GLuint list, GLenum mode)
{
    struct mgl_list *l;
    if (!c) return;
    if (!list) { mgl_error(c, GL_INVALID_VALUE); return; }
    if (mode != GL_COMPILE && mode != GL_COMPILE_AND_EXECUTE) { mgl_error(c, GL_INVALID_ENUM); return; }
    if (c->compiling || c->prim != PRIM_NONE) { mgl_error(c, GL_INVALID_OPERATION); return; }
    l = find_list(c, list);
    if (!l && !(l = new_list(c, list))) return;
    empty_list(l);
    c->compiling = l;
    c->compile_mode = mode;
}

void GLEndList(GLcontext c)
{
    if (!c) return;
    if (!c->compiling) { mgl_error(c, GL_INVALID_OPERATION); return; }
    c->compiling = 0;
}

static void play(GLcontext c, const struct mgl_list *l)
{
    ULONG i = 0;
    while (i < l->used) {
        ULONG h = l->ops[i++], n = h & 0xFFFF;
        const ULONG *a = &l->ops[i];
        i += n;
        switch (h >> 16) {
        case OP_BEGIN: GLBegin(c, a[0]); break;
        case OP_ENDPRIM: GLEnd(c); break;
        case OP_VERTEX: GLVertex4f(c, uf(a[0]), uf(a[1]), uf(a[2]), uf(a[3])); break;
        case OP_COLOR: GLColor4f(c, uf(a[0]), uf(a[1]), uf(a[2]), uf(a[3])); break;
        case OP_NORMAL: GLNormal3f(c, uf(a[0]), uf(a[1]), uf(a[2])); break;
        case OP_TEXCOORD: GLTexCoord4f(c, uf(a[0]), uf(a[1]), uf(a[2]), uf(a[3])); break;
        case OP_MTEXCOORD: GLMultiTexCoord2fARB(c, a[0], uf(a[1]), uf(a[2])); break;
        case OP_SETSTATE: MGLSetState(c, a[0], a[1]); break;
        case OP_BIND: GLBindTexture(c, a[0], a[1]); break;
        case OP_TEXENVI: GLTexEnvi(c, a[0], a[1], (GLint)a[2]); break;
        case OP_TEXENVFV: { float f[4]; int k; for (k = 0; k < 4; k++) f[k] = uf(a[2 + k]); GLTexEnvfv(c, a[0], a[1], f); break; }
        case OP_MATRIXMODE: GLMatrixMode(c, a[0]); break;
        case OP_LOADIDENTITY: GLLoadIdentity(c); break;
        case OP_PUSH: GLPushMatrix(c); break;
        case OP_POP: GLPopMatrix(c); break;
        case OP_LOADMATRIX: GLLoadMatrixf(c, (const GLfloat *)a); break;
        case OP_MULTMATRIX: GLMultMatrixf(c, (const GLfloat *)a); break;
        case OP_TRANSLATE: GLTranslatef(c, uf(a[0]), uf(a[1]), uf(a[2])); break;
        case OP_ROTATE: GLRotatef(c, uf(a[0]), uf(a[1]), uf(a[2]), uf(a[3])); break;
        case OP_SCALE: GLScalef(c, uf(a[0]), uf(a[1]), uf(a[2])); break;
        case OP_FRUSTUM: GLFrustum(c, uf(a[0]), uf(a[1]), uf(a[2]), uf(a[3]), uf(a[4]), uf(a[5])); break;
        case OP_ORTHO: GLOrtho(c, uf(a[0]), uf(a[1]), uf(a[2]), uf(a[3]), uf(a[4]), uf(a[5])); break;
        case OP_MATERIAL: { float f[4]; ULONG k; for (k = 0; k + 2 < n; k++) f[k] = uf(a[2 + k]); GLMaterialfv(c, a[0], a[1], f); break; }
        case OP_LIGHT: { float f[4]; ULONG k; for (k = 0; k + 2 < n; k++) f[k] = uf(a[2 + k]); GLLightfv(c, a[0], a[1], f); break; }
        case OP_LIGHTMODEL: { float f[4]; ULONG k; for (k = 0; k + 1 < n; k++) f[k] = uf(a[1 + k]); GLLightModelfv(c, a[0], f); break; }
        case OP_SHADEMODEL: GLShadeModel(c, a[0]); break;
        case OP_BLENDFUNC: GLBlendFunc(c, a[0], a[1]); break;
        case OP_DEPTHFUNC: GLDepthFunc(c, a[0]); break;
        case OP_DEPTHMASK: GLDepthMask(c, a[0]); break;
        case OP_ALPHAFUNC: GLAlphaFunc(c, a[0], uf(a[1])); break;
        case OP_CALLLIST: GLCallList(c, a[0]); break;
        case OP_COLORMATERIAL: GLColorMaterial(c, a[0], a[1]); break;
        case OP_FOG: { float f[4]; ULONG k; for (k = 0; k + 1 < n; k++) f[k] = uf(a[1 + k]); GLFogfv(c, a[0], f); break; }
        case OP_POINTSIZE: GLPointSize(c, uf(a[0])); break;
        case OP_LINEWIDTH: GLLineWidth(c, uf(a[0])); break;
        case OP_CULLFACE: GLCullFace(c, a[0]); break;
        case OP_FRONTFACE: GLFrontFace(c, a[0]); break;
        case OP_POLYGONMODE: GLPolygonMode(c, a[0], a[1]); break;
        case OP_TEXGENI: GLTexGeni(c, a[0], a[1], a[2]); break;
        case OP_TEXGENFV: { float f[4]; int k; for (k = 0; k < 4; k++) f[k] = uf(a[2 + k]); GLTexGenfv(c, a[0], a[1], f); break; }
        case OP_ACTIVETEX: GLActiveTextureARB(c, a[0]); break;
        case OP_COLORMASK: GLColorMask(c, a[0], a[1], a[2], a[3]); break;
        case OP_CLEAR: GLClear(c, a[0]); break;
        case OP_CLEARCOLOR: GLClearColor(c, uf(a[0]), uf(a[1]), uf(a[2]), uf(a[3])); break;
        case OP_SCISSOR: GLScissor(c, (GLint)a[0], (GLint)a[1], (GLsizei)a[2], (GLsizei)a[3]); break;
        case OP_VIEWPORT: GLViewport(c, (GLint)a[0], (GLint)a[1], (GLsizei)a[2], (GLsizei)a[3]); break;
        case OP_TEXPARAMI: GLTexParameteri(c, a[0], a[1], (GLint)a[2]); break;
        case OP_HINT: GLHint(c, a[0], a[1]); break;
        case OP_POLYOFFSET: GLPolygonOffset(c, uf(a[0]), uf(a[1])); break;
        case OP_EDGEFLAG: GLEdgeFlag(c, a[0]); break;
        }
    }
}

void GLCallList(GLcontext c, GLuint list)
{
    struct mgl_list *l, *keep;
    ULONG a = list;
    MGL_REC(c, OP_CALLLIST, 1, &a);
    l = find_list(c, list);
    if (!l || !l->used) return;
    if (c->call_depth >= 64) return;
    /* a list played while another is compiled is done, not recorded again */
    keep = c->compiling;
    c->compiling = 0;
    c->call_depth++;
    play(c, l);
    c->call_depth--;
    c->compiling = keep;
}

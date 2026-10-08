/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's insides. The library is OpenGL 1.1's fixed-function
 * front end: it keeps GL's state, transforms, lights, fogs and clips the
 * vertices, and sends the triangles to the one library's 3D ops (OGPU
 * stream v1.2, include/opengpu/stream3d.h): to opengpu.library, whose back
 * end draws them (the PC on AmigaChrome), or to the same 3D core linked in
 * here, on the 68k. Both draw the same pixels.
 *
 * The parts:
 *   mgl_lib.c       the library, its table, preferences, small helpers
 *   mgl_math.c      the arithmetic the 68040's FPU leaves to software
 *   mgl_state.c     enables, functions, queries, strings, errors
 *   mgl_matrix.c    matrices, gluPerspective, gluLookAt
 *   mgl_vertex.c    glBegin/glEnd, vertex arrays, primitives
 *   mgl_pipe.c      transform, lighting, fog, texture coordinates, clipping,
 *                   and triangles, lines and points to the stream
 *   mgl_texture.c   texture objects and their texels
 *   mgl_out.c       the batch, the state it carries, flushing it
 *   mgl_list.c      display lists
 *   mgl_glu.c       GLU: quadrics, mipmaps, errors
 *   mgl_glut.c      GLUT
 *   mgl_display.c   screens, windows, buffers, input (AmigaOS)
 *
 * The same files (but mgl_lib.c and mgl_display.c) build on a PC for the
 * tests: MGL_HOST, 32-bit, with the stream's words written big-endian. */
#ifndef MGL_INTERNAL_H
#define MGL_INTERNAL_H

#define MINIGL_LIBRARY_BUILD
#include <exec/types.h>
#include <mgl/gl.h>
#include <mgl/glut.h>
#include <libraries/minigl_dispatch.h>

#include "../opengpu/ogpu_core.h"
#include "../opengpu/ogpu_3d.h"
#include "../../include/opengpu/build3d.h"
#include "mgl_slots.h"

#define MGL_LIB_VERSION   29
#define MGL_LIB_REVISION  10

#ifndef MGL_HOST
#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */
#endif

/* ---- the stream's memory ---------------------------------------------------------------- */

/* Words the core or a back end reads are big-endian; the 68k writes them as
 * they are, a PC test swaps them. */
#ifdef MGL_HOST
#define PUTW(p, v) (*(ULONG *)(p) = __builtin_bswap32((ULONG)(v)))
#define GETW(p) __builtin_bswap32(*(const ULONG *)(p))
#else
#define PUTW(p, v) (*(ULONG *)(p) = (ULONG)(v))
#define GETW(p) (*(const ULONG *)(p))
#endif
#define ADDR(p) ((ULONG)(p))

/* The vertex the stream carries (stream3d.h's 3D layout with Z, W and SPEC). */
struct ovtx {
    LONG x, y;                      /* 16.16 */
    ULONG argb;
    LONG u, v;                      /* 16.16, 0..1 over the texture */
    ULONG z;                        /* 0..0xFFFFFFFF */
    ULONG w;                        /* IEEE754 single */
    ULONG spec;                     /* A: fog factor (255 none) */
};
#define OVTX_LAYOUT (OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC)

#define BATCH_WORDS   16384         /* 64 KB of commands */
#define PREFIX_WORDS  16            /* room for SURFACE and TARGET in front */
#define VERT_BYTES    (256 * 1024)  /* vertices and small data for one batch */

/* ---- textures --------------------------------------------------------------------------- */

#define MGL_TEX_UNITS 2
#define MGL_MAX_LEVELS OGPU_TEX_LEVELS

struct mgl_tex {
    GLuint name;
    struct mgl_tex *next;           /* in the context's hash chain */
    ULONG *level[MGL_MAX_LEVELS];   /* ARGB32 (big-endian words), level 0 first */
    UBYTE *index[MGL_MAX_LEVELS];   /* paletted textures: the indices, kept to follow the palette */
    int lw[MGL_MAX_LEVELS], lh[MGL_MAX_LEVELS];
    int w, h;                       /* level 0 */
    int base;                       /* OGPU_TEXBASE_ */
    GLenum internal;
    ULONG generation;               /* changes whenever the texels do */
    GLenum min, mag, wrap_s, wrap_t;
    ULONG border;
    float priority;
    ULONG bytes;                    /* what its levels take */
    ULONG used_in;                  /* the batch (batch_no) that last drew with it */
    ULONG *pal;                     /* its own palette (glColorTable on GL_TEXTURE_2D), or 0 */
};

/* ---- vertices --------------------------------------------------------------------------- */

/* A vertex after transform and lighting: clip coordinates, colour and all
 * the rest as floats, so the clipper can interpolate them. */
struct mgl_pv {
    float x, y, z, w;               /* clip space */
    float r, g, b, a;               /* the colour (lit, or the current colour) */
    float s0, t0, q0;               /* unit 0 */
    float s1, t1, q1;               /* unit 1 */
    float fog;                      /* fog factor, 1 none */
    ULONG clip;                     /* outside which planes (CLIP_) */
    /* window coordinates, made once (proj set) */
    int proj;
    float wx, wy;                   /* pixels, top-left origin */
    ULONG wz;
    float iw;                       /* 1 / w */
    ULONG argb;
    UBYTE edge;                     /* edge flag */
    struct ovtx o;                  /* as the stream has it, unit 0 (made with the window coordinates) */
};
#define CLIP_XN 1
#define CLIP_XP 2
#define CLIP_YN 4
#define CLIP_YP 8
#define CLIP_ZN 16
#define CLIP_ZP 32
#define CLIP_W  64                  /* w <= 0: behind the eye */

#define CLIP_MAX 16                 /* vertices a clipped polygon can have */
#define MT_TRIS 96                  /* triangles gathered for a second texture pass */
#define MGL_VB 512                  /* vertices kept for one primitive before it is drawn in part */

/* ---- lights ----------------------------------------------------------------------------- */

struct mgl_light {
    float ambient[4], diffuse[4], specular[4];
    float position[4];              /* eye space */
    float direction[3];             /* eye space */
    float spot_exp, spot_cutoff, spot_cos;
    float att[3];                   /* constant, linear, quadratic */
    float L[3], H[3];               /* a directional light: its direction and half vector, normalised */
};

struct mgl_material {
    float ambient[4], diffuse[4], specular[4], emission[4];
    float shininess;
};

/* ---- vertex arrays ---------------------------------------------------------------------- */

struct mgl_array {
    GLint size;
    GLenum type;
    GLsizei stride;                 /* as given */
    const UBYTE *ptr;
};

/* ---- display lists ---------------------------------------------------------------------- */

struct mgl_list {
    GLuint name;
    struct mgl_list *next;
    ULONG *ops;                     /* op words (MGL_OP_) */
    ULONG used, cap;
};

/* ---- the display (mgl_display.c) ------------------------------------------------------- */

struct mgl_display;                 /* private to mgl_display.c */

/* ---- the context ------------------------------------------------------------------------ */

#define MV_DEPTH 32
#define PROJ_DEPTH 8
#define TEX_DEPTH 8

/* Enables (GLcontext.en). */
#define EN_DEPTH        (1UL << 0)
#define EN_BLEND        (1UL << 1)
#define EN_ALPHA        (1UL << 2)
#define EN_FOG          (1UL << 3)
#define EN_CULL         (1UL << 4)
#define EN_LIGHTING     (1UL << 5)
#define EN_NORMALIZE    (1UL << 6)
#define EN_COLORMAT     (1UL << 7)
#define EN_SCISSOR      (1UL << 8)
#define EN_OFFSET       (1UL << 9)  /* polygon offset fill */
#define EN_DITHER       (1UL << 10)
#define EN_TEX0         (1UL << 11)
#define EN_TEX1         (1UL << 12)
#define EN_GENS0        (1UL << 13)
#define EN_GENT0        (1UL << 14)
#define EN_GENS1        (1UL << 15)
#define EN_GENT1        (1UL << 16)
#define EN_SHAREDPAL    (1UL << 17)
#define EN_LINESMOOTH   (1UL << 18)
#define EN_POINTSMOOTH  (1UL << 19)
#define EN_POLYSMOOTH   (1UL << 20)
#define EN_ZOFFSET      (1UL << 21) /* MGL_Z_OFFSET */
#define EN_GENR0        (1UL << 22)
#define EN_GENQ0        (1UL << 23)
#define EN_LIGHT0       24          /* bits 24..31: GL_LIGHT0..7 */

struct GLcontext_t {
    /* where it draws */
    struct mgl_display *disp;
    int width, height;              /* the drawing area */
    int route;                      /* ROUTE_ */
    int fmt;                        /* OGPU_FMT_ the core draws into */
    UBYTE *base;                    /* while a batch runs: the area's first byte */
    ULONG bpr;
    UBYTE *zbuf;
    ULONG zbpr;
    int zfmt;                       /* OGPU_FMT_Z16 or Z32 */

    /* the batch */
    ULONG *cmdmem;
    struct OGPUBatch b;
    UBYTE *vmem;
    ULONG vused;
    int body;
    long list_at;                   /* the last TRIANGLES list (word), or -1 */
    ULONG list_vend;
    int r3d_dirty, clip_dirty;
    struct mgl_tex *sent_tex;       /* the texture TEXTURE last sent, or 0 */
    ULONG sent_gen;
    int sent_env;                   /* OGPU_ENV_ last sent */
    ULONG sent_envc;
    int pass;                       /* 0, or 1 while drawing unit 1's pass */
    int sent_pass;                  /* the pass RENDER3D was last sent for, or -1 */
    int scissor_empty;              /* the scissor box has nothing in it: draw nothing */
    struct ogpu_core core;
    struct ogpu3d d3;
    ULONG frames;                   /* SwitchDisplay count */

    /* errors */
    GLenum error;

    /* matrices (column-major, as OpenGL) */
    GLenum matrix_mode;
    float mv[MV_DEPTH][16];
    float proj[PROJ_DEPTH][16];
    float texm[MGL_TEX_UNITS][TEX_DEPTH][16];
    int mv_top, proj_top, tex_top[MGL_TEX_UNITS];
    int tex_identity[MGL_TEX_UNITS];
    float mvp[16];                  /* proj * mv */
    float nm[9];                    /* the normal matrix: mv's inverse transposed, 3x3 */
    int mvp_dirty, nm_dirty;
    int proj_kind;                  /* 1: the projection has glFrustum's shape */

    /* viewport, depth range, scissor */
    GLint vp_x, vp_y;
    GLsizei vp_w, vp_h;
    float vsx, vsy, vtx, vty;       /* window x = ndc x * vsx + vtx (top-left origin) */
    float gb;                       /* guard band: |x|, |y| up to gb * w need no clipping */
    double depth_near, depth_far;
    GLint sc_x, sc_y;
    GLsizei sc_w, sc_h;

    /* current values */
    float color[4];
    float normal[3];
    float tc[MGL_TEX_UNITS][4];
    GLboolean edge;

    /* state */
    ULONG en;
    GLenum depth_func;
    GLboolean depth_mask;
    GLenum blend_src, blend_dst, blend_eq;
    GLenum alpha_func;
    float alpha_ref;
    GLenum cull_mode, front_face, shade_model;
    GLenum poly_front, poly_back;
    ULONG cmask;                    /* RGBA bits 0-3 */
    float clear_color[4];
    double clear_depth;
    GLenum fog_mode;
    float fog_color[4], fog_start, fog_end, fog_density;
    GLenum hint_persp, hint_fog, hint_w_one;
    float point_size, line_width;
    float offset_factor, offset_units, zoffset;
    float min_tri_area;
    GLenum draw_buffer, read_buffer;

    /* lighting */
    struct mgl_light light[8];
    float light_ambient[4];
    GLboolean local_viewer, two_side;
    struct mgl_material mat[2];     /* front, back */
    GLenum colormat_face, colormat_mode;
    float spec_table[512];          /* n.h to the shininess, by n.h in 1/511ths */
    float spec_table_for;           /* the shininess spec_table is for */

    /* texture coordinate generation */
    GLenum gen_mode[MGL_TEX_UNITS][4];
    float gen_obj[MGL_TEX_UNITS][4][4], gen_eye[MGL_TEX_UNITS][4][4];

    /* textures */
    struct mgl_tex *tex_hash[64];
    struct mgl_tex tex0[MGL_TEX_UNITS];  /* texture object 0 of each unit */
    struct mgl_tex *bound[MGL_TEX_UNITS];
    GLenum env_mode[MGL_TEX_UNITS];
    float env_color[MGL_TEX_UNITS][4];
    int active, client_active;
    GLint unpack_align, unpack_row, unpack_skip_rows, unpack_skip_px;
    GLint pack_align;
    ULONG palette[256];             /* the shared texture palette (ARGB) */
    ULONG tex_bytes, tex_peak;
    GLuint next_name;
    int no_mip;                     /* mglProhibitMipMapping */

    /* vertex arrays */
    struct mgl_array va, ca, na, ta[MGL_TEX_UNITS];
    ULONG client;                   /* CL_ bits */
    GLint lock_first;
    GLsizei lock_count;

    /* glBegin/glEnd */
    GLenum prim;                    /* the primitive, or PRIM_NONE */
    struct mgl_pv vb[MGL_VB + 4];
    int nv;                         /* vertices in vb */
    int prim_count;                 /* vertices the primitive has had in all */
    struct mgl_pv first;            /* a loop's first vertex, once vb moved on */
    int strip_odd;                  /* a strip continued from a part already drawn starts odd */
    struct mgl_pv *cache;           /* an array draw's processed vertices */
    ULONG *cache_ix;                /* and its indices */
    ULONG cache_cap;
    ULONG batch_no;                 /* counts flushes: textures note the batch that uses them */

    /* the clipper's new vertices, and the second unit's pass (mgl_pipe.c) */
    struct mgl_pv clipv[CLIP_MAX * 7];
    struct ovtx mt0[MT_TRIS * 3], mt1[MT_TRIS * 3];
    int mt_n;

    /* display lists */
    struct mgl_list *list_hash[32];
    struct mgl_list *compiling;
    GLenum compile_mode;
    int call_depth;
    GLuint next_list;

    /* the main loop */
    IdleFn idle;
    KeyHandlerFn key;
    SpecialHandlerFn special;
    MouseHandlerFn mouse;
    int quit;
    GLenum lock_mode;
    GLboolean sync;

    /* statistics */
    ULONG tris, verts;
};

#define PRIM_NONE 0xFFFF
#define CL_VERTEX   1
#define CL_COLOR    2
#define CL_NORMAL   4
#define CL_TEX0     8
#define CL_TEX1     16

/* Routes. */
#define ROUTE_GPU 0                 /* opengpu.library */
#define ROUTE_CPU 1                 /* the 3D core on this 68k */

/* ---- the library (mgl_lib.c) ------------------------------------------------------------ */

extern GLcontext mgl_current;
extern const MGLDispatchTable mgl_table;

struct mgl_prefs {                  /* chosen before mglCreateContext */
    int depth;                      /* mglChoosePixelDepth */
    int buffers;                    /* mglChooseNumberOfBuffers */
    int window;                     /* mglChooseWindowMode */
    int zbits;                      /* mglChooseZBufferDepth: 16 or 32 */
    int guard, close_desktop, alpha_fallback, no_mip;
    int route;                      /* ENV:MiniGL/Driver: -1 auto, ROUTE_ */
    int flip;                       /* ENV:MiniGL/Flip: screens change buffers instead of blitting */
    int gpu_3d, gpu_host;           /* opengpu.library draws 3D (and not on this 68k) */
};
extern struct mgl_prefs mgl_prefs;

void *mgl_alloc(ULONG bytes);       /* cleared; 0 when there is no memory */
void mgl_free(void *p);
void mgl_copy(void *d, const void *s, ULONG n);
void mgl_zero(void *d, ULONG n);
void mgl_read_prefs(void);
ULONG mgl_millis(void);            /* milliseconds, for GLUT_ELAPSED_TIME */
void mgl_log(const char *fmt, ...); /* to the serial port when ENV:MiniGL/Trace is set */
extern long mgl_trace;

/* ---- arithmetic (mgl_math.c) ------------------------------------------------------------ */

LONG mgl_f2l(float f);              /* rounded to the nearest */
ULONG mgl_fbits(float f);
float mgl_sqrt(float x);
float mgl_rsqrt(float x);           /* 1 / sqrt */
float mgl_sin(float x);
float mgl_cos(float x);
float mgl_tan(float x);
float mgl_exp(float x);
float mgl_log2(float x);
float mgl_pow(float x, float y);
float mgl_floor(float x);
ULONG mgl_argb(const float *c);     /* clamped RGBA floats to ARGB32 */
#define MGL_PI 3.14159265358979f

/* ---- state (mgl_state.c) ---------------------------------------------------------------- */

void mgl_init_state(GLcontext c);
void mgl_error(GLcontext c, GLenum e);
#define R3D_DIRTY(c) ((c)->r3d_dirty = 1, (c)->list_at = -1)

/* ---- matrices (mgl_matrix.c) ------------------------------------------------------------ */

float *mgl_top(GLcontext c);        /* the current mode's top matrix */
void mgl_update_mvp(GLcontext c);
void mgl_update_nm(GLcontext c);
void mgl_mat_mul(float *r, const float *a, const float *b);     /* r = a * b */
void mgl_mat_identity(float *m);
int mgl_mat_inverse(float *r, const float *m);  /* 0 when m has none */
void mgl_viewport_changed(GLcontext c);

/* ---- vertices (mgl_vertex.c, mgl_pipe.c) ------------------------------------------------ */

void mgl_vertex(GLcontext c, float x, float y, float z, float w);   /* glVertex: through the pipeline */
void mgl_process(GLcontext c, struct mgl_pv *pv, const float *obj, const float *color, const float *normal,
                 const float (*tc)[4]);
void mgl_triangle(GLcontext c, struct mgl_pv *a, struct mgl_pv *b, struct mgl_pv *d, struct mgl_pv *prov);
void mgl_line(GLcontext c, struct mgl_pv *a, struct mgl_pv *b, struct mgl_pv *prov);
void mgl_point(GLcontext c, struct mgl_pv *a);
void mgl_polygon(GLcontext c, struct mgl_pv **v, int n, const UBYTE *edges, struct mgl_pv *prov);
void mgl_end_prim(GLcontext c);     /* glEnd */
void mgl_pipe_flush(GLcontext c);   /* triangles waiting in the pipe to the batch */
void mgl_array_element(GLcontext c, GLint i);

/* ---- textures (mgl_texture.c) ----------------------------------------------------------- */

struct mgl_tex *mgl_tex_unit(GLcontext c, int unit);   /* the texture unit draws with, or 0 */
void mgl_free_textures(GLcontext c);
UBYTE *mgl_unpack_rgba(GLcontext c, int w, int h, GLenum format, GLenum type, const void *pixels);
void mgl_tex_store(GLcontext c, struct mgl_tex *t, int level, int w, int h, GLenum internal, GLenum format,
                   GLenum type, const void *pixels, int x0, int y0, int sub);

/* ---- the batch (mgl_out.c) -------------------------------------------------------------- */

int mgl_out_open(GLcontext c);      /* buffers for the batch and the Z buffer */
void mgl_out_close(GLcontext c);
void mgl_flush(GLcontext c);        /* runs the batch */
struct ovtx *mgl_vspace(GLcontext c, ULONG n);         /* room for n vertices, the state sent */
void mgl_emit(GLcontext c, struct ovtx *d, ULONG n, int prim);
void mgl_clear(GLcontext c, GLbitfield mask);
void mgl_readback(GLcontext c, int x, int y, int w, int h, ULONG *argb, int stride);   /* top-left origin */

/* ---- display lists (mgl_list.c) --------------------------------------------------------- */

/* Calls that a list records. A call checks MGL_REC first: while a list is
 * compiled it is recorded, and with GL_COMPILE not done. */
enum {
    OP_END = 0, OP_BEGIN, OP_ENDPRIM, OP_VERTEX, OP_COLOR, OP_NORMAL, OP_TEXCOORD, OP_MTEXCOORD,
    OP_SETSTATE, OP_BIND, OP_TEXENVI, OP_TEXENVFV, OP_MATRIXMODE, OP_LOADIDENTITY, OP_PUSH, OP_POP,
    OP_LOADMATRIX, OP_MULTMATRIX, OP_TRANSLATE, OP_ROTATE, OP_SCALE, OP_FRUSTUM, OP_ORTHO,
    OP_MATERIAL, OP_LIGHT, OP_LIGHTMODEL, OP_SHADEMODEL, OP_BLENDFUNC, OP_DEPTHFUNC, OP_DEPTHMASK,
    OP_ALPHAFUNC, OP_CALLLIST, OP_COLORMATERIAL, OP_FOG, OP_POINTSIZE, OP_LINEWIDTH, OP_CULLFACE,
    OP_FRONTFACE, OP_POLYGONMODE, OP_TEXGENI, OP_TEXGENFV, OP_ACTIVETEX, OP_COLORMASK, OP_CLEAR,
    OP_CLEARCOLOR, OP_SCISSOR, OP_VIEWPORT, OP_TEXPARAMI, OP_HINT, OP_POLYOFFSET, OP_EDGEFLAG
};
int mgl_record(GLcontext c, int op, int n, const ULONG *args);  /* 1: also do it */
#define MGL_REC(c, op, n, args) if (!(c) || ((c)->compiling && !mgl_record((c), (op), (n), (const ULONG *)(args)))) return
void mgl_free_lists(GLcontext c);
ULONG mgl_fu(float f);              /* a float's bits, for records */

/* ---- the display (mgl_display.c, or a test's) ------------------------------------------- */

int mgl_disp_lock(GLcontext c);     /* c->base, c->bpr for one batch; 0 when it can't */
void mgl_disp_unlock(GLcontext c);
int mgl_disp_readable(GLcontext c); /* the area's pixels as the core left them can be read now */
void mgl_disp_free(GLcontext c);

#endif

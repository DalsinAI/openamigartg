/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.h: the Team's own header for Warp3D, the Amiga's classic 3D API, as
 * OpenRTG's Warp3D.library offers it (version 5's 68k function table, a
 * superset of version 4). Programs written for Warp3D build against it
 * unchanged.
 *
 * Written from the published API documentation (the Warp3D autodocs and
 * programmer's guide). The numbers and the structure layouts below are what
 * existing programs were compiled with, so they must stay exactly as they
 * are; tests/w3d_layout.c checks every offset. No text of the original
 * Warp3D SDK is reproduced here.
 *
 * Coordinates: x and y are window coordinates in pixels; z runs from 0
 * (near) to 1 (far); w is the reciprocal of the eye distance (1.0 at the
 * front plane), used for perspective-correct texturing and for fog; u and v
 * are in texels unless a coordinate array says they are normalised. */
#ifndef WARP3D_WARP3D_H
#define WARP3D_WARP3D_H
#ifndef _WARP3D_WARP3D_H
#define _WARP3D_WARP3D_H        /* the name some programs test for */
#endif

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/libraries.h>
#include <utility/tagitem.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef short  W3D_Bool;
typedef float  W3D_Float;
typedef double W3D_Double;

#ifndef W3D_TRUE
#define W3D_TRUE  1
#endif
#ifndef W3D_FALSE
#define W3D_FALSE 0
#endif

/* ---- Colours and vertices ------------------------------------------------- */

typedef struct { W3D_Float r, g, b, a; } W3D_Color;     /* each 0.0 .. 1.0 */
typedef struct { W3D_Float r, g, b; } W3D_ColorRGB;

typedef struct {
    W3D_Float    x, y;          /* window position */
    W3D_Double   z;             /* depth, 0 near .. 1 far */
    W3D_Float    w;             /* 1 / eye distance */
    W3D_Float    u, v, tex3d;   /* texture coordinates; tex3d for volume textures */
    W3D_Color    color;         /* Gouraud colour */
    W3D_ColorRGB spec;          /* specular colour, added after texturing */
    W3D_Float    l;             /* mipmap level hint (not used by programs) */
} W3D_Vertex;

/* ---- Textures --------------------------------------------------------------- */

/* Programs may read texwidth, texheight and the flags; the rest is the
 * library's. */
typedef struct {
    struct Node link;           /* the library's list of textures */
    W3D_Bool  resident;         /* in the 3D chip's memory */
    W3D_Bool  mipmap;           /* has mipmaps */
    W3D_Bool  dirty;            /* the image changed since it was uploaded */
    W3D_Bool  matchfmt;         /* stored in the format it was given */
    W3D_Bool  pinned;           /* V5: kept resident (W3D_PinTexture) */
    W3D_Bool  reserved2;
    ULONG     mipmapmask;       /* which levels the library makes */
    void     *texsource;        /* the image as given */
    void     *mipmaps[16];      /* program-supplied levels */
    int       texfmtsrc;        /* W3D_ATO_FORMAT */
    ULONG    *palette;          /* CHUNKY textures' colours */
    void     *texdata;          /* the library's converted copy */
    void     *texdest;          /* where the chip keeps it */
    int       texdestsize;
    int       texwidth;
    int       texwidthexp;      /* log2 of the width */
    int       texheight;
    int       texheightexp;
    int       bytesperpix;
    int       bytesperrow;
    void     *driver;           /* the driver's own data */
} W3D_Texture;

/* ---- Primitives ----------------------------------------------------------- */

typedef struct {
    W3D_Vertex   v1;
    W3D_Texture *tex;
    W3D_Float    pointsize;     /* diameter in pixels */
} W3D_Point;

typedef struct {
    W3D_Vertex     v1, v2;
    W3D_Texture   *tex;
    W3D_Float      linewidth;
    W3D_Bool       st_enable;   /* stippled */
    unsigned short st_pattern;  /* 16 bits, low bit first */
    int            st_factor;   /* each bit repeated this often */
} W3D_Line;

typedef struct {                /* V2 */
    int            vertexcount;
    W3D_Vertex    *v;
    W3D_Texture   *tex;
    W3D_Float      linewidth;
    W3D_Bool       st_enable;
    unsigned short st_pattern;
    int            st_factor;
} W3D_Lines;

typedef struct {
    W3D_Vertex     v1, v2, v3;
    W3D_Texture   *tex;
    unsigned char *st_pattern;  /* 32x32 polygon stipple, or NULL */
} W3D_Triangle;

typedef struct {                /* V3: pointers to vertices */
    W3D_Vertex    *v1, *v2, *v3;
    W3D_Texture   *tex;
    unsigned char *st_pattern;
} W3D_TriangleV;

typedef struct {                /* fans and strips */
    int            vertexcount;
    W3D_Vertex    *v;
    W3D_Texture   *tex;
    unsigned char *st_pattern;
} W3D_Triangles;

typedef struct {                /* V3: fans and strips through pointers */
    int            vertexcount;
    W3D_Vertex   **v;
    W3D_Texture   *tex;
    unsigned char *st_pattern;
} W3D_TrianglesV;

/* ---- Fog, scissor, queues ---------------------------------------------------- */

typedef struct {
    W3D_Float    fog_start;     /* w-space: 1.0 is the front plane, 0.0 the back */
    W3D_Float    fog_end;
    W3D_Float    fog_density;   /* exponential modes */
    W3D_ColorRGB fog_color;
} W3D_Fog;

typedef struct { int left, top, width, height; } W3D_Scissor;

typedef struct {
    short type;
    short length;               /* the entry's bytes, this header included */
    ULONG state;
} W3D_QHead;

typedef struct {
    UBYTE *data;
    ULONG  size;
    UBYTE *current;
} W3D_Queue;

#define QEMPTY(x) ((x)->data == (x)->current)

#define W3D_MAX_TMU 16L         /* texture units the context has room for */

/* ---- The context -------------------------------------------------------------- */

/* Private to the library, except that programs may read format, bprow,
 * width, height, depth and drawmem. */
typedef struct {
    void          *driver;
    void          *gfxdriver;
    int            drivertype;
    void          *regbase;
    void          *vmembase;
    void          *zbuffer;
    void          *stencilbuffer;
    ULONG          state;           /* W3D_ state bits that are on */
    struct BitMap *drawregion;
    ULONG          supportedfmt;
    ULONG          format;          /* W3D_FMT_ of the drawing area */
    int            yoffset;
    int            bprow;
    int            width;
    int            height;
    int            depth;
    W3D_Bool       chunky;
    W3D_Bool       destalpha;
    W3D_Bool       zbufferalloc;
    W3D_Bool       stbufferalloc;
    W3D_Bool       HWlocked;
    W3D_Bool       w3dbitmap;       /* drawregion is a W3D_Bitmap */
    W3D_Bool       zbufferlost;
    W3D_Bool       reserved3;
    struct MinList restex;          /* resident textures */
    struct MinList tex;             /* other textures */
    int            maxtexwidth;
    int            maxtexheight;
    int            maxtexwidthp;    /* in perspective mode */
    int            maxtexheightp;
    W3D_Scissor    scissor;
    W3D_Fog        fog;
    ULONG          envsupmask;
    W3D_Queue     *queue;
    void          *drawmem;         /* the drawing area's first byte */
    ULONG          globaltexenvmode;
    W3D_Float      globaltexenvcolor[4];
    struct Library *DriverBase;
    ULONG          EnableMask;
    ULONG          DisableMask;
    ULONG          CurrentChip;
    ULONG          DriverVersion;
    /* V4: vertex arrays */
    void          *VertexPointer;
    int            VPStride;
    ULONG          VPMode;
    ULONG          VPFlags;
    void          *TexCoordPointer[W3D_MAX_TMU];
    int            TPStride[W3D_MAX_TMU];
    W3D_Texture   *CurrentTex[W3D_MAX_TMU];
    int            TPVOffs[W3D_MAX_TMU];
    int            TPWOffs[W3D_MAX_TMU];
    int            TPFlags[W3D_MAX_TMU];
    void          *ColorPointer;
    int            CPStride;
    ULONG          CPMode;
    ULONG          CPFlags;
    ULONG          FrontFaceOrder;  /* W3D_CW or W3D_CCW */
    void          *specialbuffer;
    /* V5 */
    void          *SecondaryColorPointer;
    int            SCPStride;
    ULONG          SCPMode;
    ULONG          SCPFlags;
    void          *FogCoordPointer;
    ULONG          FCPMode;
    ULONG          FCPStride;
    ULONG          FCPFlags;
    struct BitMap *orig_drawregion; /* kept while drawing into a texture */
    void          *orig_zbuffer;
    void          *orig_stencilbuffer;
    ULONG          orig_width, orig_height;
    ULONG          orig_yoffset;
    W3D_Scissor    orig_scissor;
    ULONG          orig_bprow;
} W3D_Context;

/* A drawing area that is not a BitMap (W3D_CC_W3DBM, W3D_SetDrawRegionWBM). */
typedef struct {
    int   bprow;
    int   width;
    int   height;
    ULONG format;               /* W3D_FMT_ */
    void *dest;
} W3D_Bitmap;

typedef struct {                /* V2 */
    ULONG    ChipID;            /* W3D_CHIP_ */
    ULONG    formats;           /* W3D_FMT_ it draws into */
    char    *name;
    W3D_Bool swdriver;          /* W3D_TRUE: drawn by the CPU */
} W3D_Driver;

typedef struct {                /* V3 */
    ULONG       ModeID;
    ULONG       Width, Height;
    ULONG       Depth;
    char        DisplayName[DISPLAYNAMELEN];
    W3D_Driver *Driver;
    void       *Next;
} W3D_ScreenMode;

/* ---- Tags ------------------------------------------------------------------------- */

/* W3D_CreateContext */
#define W3D_CC_TAGS         (TAG_USER + 0x200000)
#define W3D_CC_BITMAP       (W3D_CC_TAGS + 0)   /* struct BitMap * to draw into */
#define W3D_CC_YOFFSET      (W3D_CC_TAGS + 1)
#define W3D_CC_DRIVERTYPE   (W3D_CC_TAGS + 2)   /* W3D_DRIVER_ */
#define W3D_CC_W3DBM        (W3D_CC_TAGS + 3)   /* W3D_CC_BITMAP is a W3D_Bitmap */
#define W3D_CC_INDIRECT     (W3D_CC_TAGS + 4)
#define W3D_CC_GLOBALTEXENV (W3D_CC_TAGS + 5)
#define W3D_CC_DOUBLEHEIGHT (W3D_CC_TAGS + 6)
#define W3D_CC_FAST         (W3D_CC_TAGS + 7)
#define W3D_CC_MODEID       (W3D_CC_TAGS + 8)

/* W3D_AllocTexObj */
#define W3D_ATO_TAGS        (TAG_USER + 0x201000)
#define W3D_ATO_IMAGE       (W3D_ATO_TAGS + 0)
#define W3D_ATO_FORMAT      (W3D_ATO_TAGS + 1)
#define W3D_ATO_WIDTH       (W3D_ATO_TAGS + 2)
#define W3D_ATO_HEIGHT      (W3D_ATO_TAGS + 3)
#define W3D_ATO_MIPMAP      (W3D_ATO_TAGS + 4)  /* mask of levels the library makes */
#define W3D_ATO_PALETTE     (W3D_ATO_TAGS + 5)
#define W3D_ATO_MIPMAPPTRS  (W3D_ATO_TAGS + 6)

/* W3D_RequestMode */
#define W3D_SMR_TAGS        (TAG_USER + 0x202000)
#define W3D_SMR_DRIVER      (W3D_SMR_TAGS + 0)
#define W3D_SMR_DESTFMT     (W3D_SMR_TAGS + 1)
#define W3D_SMR_TYPE        (W3D_SMR_TAGS + 2)
#define W3D_SMR_SIZEFILTER  (W3D_SMR_TAGS + 3)
#define W3D_SMR_MODEMASK    (W3D_SMR_TAGS + 4)

/* W3D_BestModeID */
#define W3D_BMI_TAGS        (TAG_USER + 0x203000)
#define W3D_BMI_DRIVER      (W3D_BMI_TAGS + 0)
#define W3D_BMI_WIDTH       (W3D_BMI_TAGS + 1)
#define W3D_BMI_HEIGHT      (W3D_BMI_TAGS + 2)
#define W3D_BMI_DEPTH       (W3D_BMI_TAGS + 3)

/* W3D_SetTextureBlend (V5) */
#define W3D_STB_TAGS        (TAG_USER + 0x204000)
#define W3D_BLEND_STAGE     (W3D_STB_TAGS + 1)
#define W3D_COLOR_ARG_A     (W3D_STB_TAGS + 2)
#define W3D_COLOR_ARG_B     (W3D_STB_TAGS + 3)
#define W3D_COLOR_ARG_C     (W3D_STB_TAGS + 4)
#define W3D_ALPHA_ARG_A     (W3D_STB_TAGS + 5)
#define W3D_ALPHA_ARG_B     (W3D_STB_TAGS + 6)
#define W3D_ALPHA_ARG_C     (W3D_STB_TAGS + 7)
#define W3D_COLOR_COMBINE   (W3D_STB_TAGS + 8)
#define W3D_ALPHA_COMBINE   (W3D_STB_TAGS + 9)
#define W3D_BLEND_FACTOR    (W3D_STB_TAGS + 10)
#define W3D_COLOR_SCALE     (W3D_STB_TAGS + 11)
#define W3D_ALPHA_SCALE     (W3D_STB_TAGS + 12)
#define W3D_ENV_MODE        (W3D_STB_TAGS + 13)

/* ---- Drivers, chips and formats ------------------------------------------------- */

#define W3D_DRIVER_UNAVAILABLE  (1 << 0)
#define W3D_DRIVER_BEST         (1 << 1)
#define W3D_DRIVER_3DHW         (1 << 2)
#define W3D_DRIVER_CPU          (1 << 3)

/* Chips (W3D_Driver.ChipID): for information only; OpenRTG reports
 * W3D_CHIP_UNKNOWN. Version 5 renumbered the ones above 4: these are its
 * numbers, and the version 4 names that version 5 dropped keep theirs. */
#define W3D_CHIP_UNKNOWN        1
#define W3D_CHIP_VIRGE          2
#define W3D_CHIP_PERMEDIA2      3
#define W3D_CHIP_VOODOO1        4
#define W3D_CHIP_OBSOLETE1      5
#define W3D_CHIP_OBSOLETE2      6
#define W3D_CHIP_RADEON         7
#define W3D_CHIP_AVENGER        8       /* Voodoo 3 */
#define W3D_CHIP_NAPALM         9       /* Voodoo 4 and 5 */
#define W3D_CHIP_RADEON_R200    10
#define W3D_CHIP_RADEON_R300    11
#define W3D_CHIP_AVENGER_LE     5       /* version 4 names */
#define W3D_CHIP_AVENGER_BE     6
#define W3D_CHIP_PERMEDIA3      7
#define W3D_CHIP_RADEON2        9       /* version 4: second generation Radeon */

/* Texture formats (W3D_ATO_FORMAT). */
#define W3D_CHUNKY              1       /* 8-bit index into W3D_ATO_PALETTE */
#define W3D_A1R5G5B5            2
#define W3D_R5G6B5              3
#define W3D_R8G8B8              4
#define W3D_A4R4G4B4            5
#define W3D_A8R8G8B8            6
#define W3D_A8                  7       /* alpha only */
#define W3D_L8                  8       /* luminance */
#define W3D_L8A8                9
#define W3D_I8                  10      /* intensity: colour and alpha alike */
#define W3D_R8G8B8A8            11
#define W3D_COMPRESSED_R5G6B5   12      /* V5 */
#define W3D_A4_COMPRESSED_R5G6B5 13
#define W3D_COMPRESSED_A8R5G6B5 14

/* W3D_GetTexFmtInfo results (bits). */
#define W3D_TEXFMT_SUPPORTED    (1 << 0)
#define W3D_TEXFMT_UNSUPPORTED  (1 << 1)
#define W3D_TEXFMT_FAST         (1 << 16)
#define W3D_TEXFMT_CLUTFAST     (1 << 17)
#define W3D_TEXFMT_ARGBFAST     (1 << 18)

/* Drawing area formats (bits; "PC" is little-endian). */
#define W3D_FMT_CLUT            (1 << 0)
#define W3D_FMT_R5G5B5          (1 << 1)
#define W3D_FMT_B5G5R5          (1 << 2)
#define W3D_FMT_R5G5B5PC        (1 << 3)
#define W3D_FMT_B5G5R5PC        (1 << 4)
#define W3D_FMT_R5G6B5          (1 << 5)
#define W3D_FMT_B5G6R5          (1 << 6)
#define W3D_FMT_R5G6B5PC        (1 << 7)
#define W3D_FMT_B5G6R5PC        (1 << 8)
#define W3D_FMT_R8G8B8          (1 << 9)
#define W3D_FMT_B8G8R8          (1 << 10)
#define W3D_FMT_A8R8G8B8        (1 << 11)
#define W3D_FMT_A8B8G8R8        (1 << 12)
#define W3D_FMT_R8G8B8A8        (1 << 13)
#define W3D_FMT_B8G8R8A8        (1 << 14)

/* ---- States (W3D_SetState, W3D_GetState) ---------------------------------------- */

#define W3D_AUTOTEXMANAGEMENT   (1 << 1)
#define W3D_SYNCHRON            (1 << 2)
#define W3D_INDIRECT            (1 << 3)
#define W3D_GLOBALTEXENV        (1 << 4)
#define W3D_DOUBLEHEIGHT        (1 << 5)
#define W3D_FAST                (1 << 6)
#define W3D_AUTOCLIP            (1 << 7)
#define W3D_TEXMAPPING          (1 << 8)
#define W3D_PERSPECTIVE         (1 << 9)
#define W3D_GOURAUD             (1 << 10)
#define W3D_ZBUFFER             (1 << 11)
#define W3D_ZBUFFERUPDATE       (1 << 12)
#define W3D_BLENDING            (1 << 13)
#define W3D_FOGGING             (1 << 14)
#define W3D_ANTI_POINT          (1 << 15)
#define W3D_ANTI_LINE           (1 << 16)
#define W3D_ANTI_POLYGON        (1 << 17)
#define W3D_ANTI_FULLSCREEN     (1 << 18)
#define W3D_DITHERING           (1 << 19)
#define W3D_LOGICOP             (1 << 20)
#define W3D_STENCILBUFFER       (1 << 21)
#define W3D_ALPHATEST           (1 << 22)
#define W3D_SPECULAR            (1 << 23)
#define W3D_TEXMAPPING3D        (1 << 24)
#define W3D_SCISSOR             (1 << 25)
#define W3D_CHROMATEST          (1 << 26)
#define W3D_CULLFACE            (1 << 27)
#define W3D_MULTITEXTURE        (1 << 28)   /* V5 */
#define W3D_FOG_COORD           (1 << 29)   /* V5: fog from the fog coordinate array */
#define W3D_LINE_STIPPLE        (1 << 30)   /* V5 */
#define W3D_POLYGON_STIPPLE     (1UL << 31) /* V5 */

#define W3D_ENABLE              1
#define W3D_ENABLED             1
#define W3D_DISABLE             2
#define W3D_DISABLED            2

/* ---- W3D_Query ------------------------------------------------------------------------ */

#define W3D_Q_DRAW_POINT        1
#define W3D_Q_DRAW_LINE         2
#define W3D_Q_DRAW_TRIANGLE     3
#define W3D_Q_DRAW_POINT_X      4
#define W3D_Q_DRAW_LINE_X       5
#define W3D_Q_DRAW_LINE_ST      6
#define W3D_Q_DRAW_POLY_ST      7
#define W3D_Q_DRAW_POINT_FX     8
#define W3D_Q_DRAW_LINE_FX      9
#define W3D_Q_TEXMAPPING        11
#define W3D_Q_MIPMAPPING        12
#define W3D_Q_BILINEARFILTER    13
#define W3D_Q_MMFILTER          14
#define W3D_Q_LINEAR_REPEAT     15
#define W3D_Q_LINEAR_CLAMP      16
#define W3D_Q_PERSPECTIVE       17
#define W3D_Q_PERSP_REPEAT      18
#define W3D_Q_PERSP_CLAMP       19
#define W3D_Q_ENV_REPLACE       20
#define W3D_Q_ENV_DECAL         21
#define W3D_Q_ENV_MODULATE      22
#define W3D_Q_ENV_BLEND         23
#define W3D_Q_WRAP_ASYM         24
#define W3D_Q_SPECULAR          25
#define W3D_Q_BLEND_DECAL_FOG   26
#define W3D_Q_TEXMAPPING3D      27
#define W3D_Q_CHROMATEST        28
#define W3D_Q_FLATSHADING       31
#define W3D_Q_GOURAUDSHADING    32
#define W3D_Q_ZBUFFER           41
#define W3D_Q_ZBUFFERUPDATE     42
#define W3D_Q_ZCOMPAREMODES     43
#define W3D_Q_ALPHATEST         51
#define W3D_Q_ALPHATESTMODES    52
#define W3D_Q_BLENDING          61
#define W3D_Q_SRCFACTORS        62
#define W3D_Q_DESTFACTORS       63
#define W3D_Q_ONE_ONE           64
#define W3D_Q_FOGGING           71
#define W3D_Q_LINEAR            72
#define W3D_Q_EXPONENTIAL       73
#define W3D_Q_S_EXPONENTIAL     74
#define W3D_Q_INTERPOLATED      75
#define W3D_Q_ANTIALIASING      81
#define W3D_Q_ANTI_POINT        82
#define W3D_Q_ANTI_LINE         83
#define W3D_Q_ANTI_POLYGON      84
#define W3D_Q_ANTI_FULLSCREEN   85
#define W3D_Q_DITHERING         91
#define W3D_Q_PALETTECONV       92
#define W3D_Q_SCISSOR           101
#define W3D_Q_MAXTEXWIDTH       111
#define W3D_Q_MAXTEXHEIGHT      112
#define W3D_Q_MAXTEXWIDTH_P     113
#define W3D_Q_MAXTEXHEIGHT_P    114
#define W3D_Q_RECTTEXTURES      115
#define W3D_Q_LOGICOP           121
#define W3D_Q_MASKING           131
#define W3D_Q_STENCILBUFFER     141
#define W3D_Q_STENCIL_MASK      142
#define W3D_Q_STENCIL_FUNC      143
#define W3D_Q_STENCIL_SFAIL     144
#define W3D_Q_STENCIL_DPFAIL    145
#define W3D_Q_STENCIL_DPPASS    146
#define W3D_Q_STENCIL_WRMASK    147
#define W3D_Q_DRAW_POINT_TEX    160
#define W3D_Q_DRAW_LINE_TEX     161
#define W3D_Q_CULLFACE          162
#define W3D_Q_NUM_TMU           170     /* V5 */
#define W3D_Q_NUM_BLEND         171
#define W3D_Q_ENV_COMBINE       172
#define W3D_Q_ENV_ADD           173
#define W3D_Q_ENV_SUB           174
#define W3D_Q_ENV_CROSSBAR      175
#define W3D_Q_STIPPLE_LINE      176
#define W3D_Q_STIPPLE_POLYGON   177

#define W3D_FULLY_SUPPORTED     3
#define W3D_PARTIALLY_SUPPORTED 4
#define W3D_NOT_SUPPORTED       5

/* ---- Blending, filters, fog, environments, wrapping ------------------------------- */

#define W3D_ZERO                1
#define W3D_ONE                 2
#define W3D_SRC_COLOR           3
#define W3D_DST_COLOR           4
#define W3D_ONE_MINUS_SRC_COLOR 5
#define W3D_ONE_MINUS_DST_COLOR 6
#define W3D_SRC_ALPHA           7
#define W3D_ONE_MINUS_SRC_ALPHA 8
#define W3D_DST_ALPHA           9
#define W3D_ONE_MINUS_DST_ALPHA 10
#define W3D_SRC_ALPHA_SATURATE  11
#define W3D_CONSTANT_COLOR      12
#define W3D_ONE_MINUS_CONSTANT_COLOR 13
#define W3D_CONSTANT_ALPHA      14
#define W3D_ONE_MINUS_CONSTANT_ALPHA 15

/* Filters, named as OpenGL's: the first word is how texels are sampled,
 * the second how mip levels are; magnification takes W3D_NEAREST or
 * W3D_LINEAR only. */
#define W3D_NEAREST             1       /* no mipmaps, nearest texel */
#define W3D_LINEAR              2       /* no mipmaps, bilinear */
#define W3D_NEAREST_MIP_NEAREST 3       /* nearest level, nearest texel */
#define W3D_NEAREST_MIP_LINEAR  4       /* two levels blended, nearest texels */
#define W3D_LINEAR_MIP_NEAREST  5       /* nearest level, bilinear */
#define W3D_LINEAR_MIP_LINEAR   6       /* trilinear */

#define W3D_FOG_LINEAR          1
#define W3D_FOG_EXP             2
#define W3D_FOG_EXP_2           3
#define W3D_FOG_INTERPOLATED    4

#define W3D_REPLACE             1
#define W3D_DECAL               2
#define W3D_MODULATE            3
#define W3D_BLEND               4
#define W3D_ADD                 5       /* V5 */
#define W3D_SUB                 6       /* V5 */
#define W3D_OFF                 7       /* V5 */

#define W3D_REPEAT              1
#define W3D_CLAMP               2

/* Alpha and Z compare functions: draw if the incoming value is ... */
#define W3D_A_NEVER             1
#define W3D_A_LESS              2
#define W3D_A_GEQUAL            3
#define W3D_A_LEQUAL            4
#define W3D_A_GREATER           5
#define W3D_A_NOTEQUAL          6
#define W3D_A_EQUAL             7
#define W3D_A_ALWAYS            8
#define W3D_Z_NEVER             1
#define W3D_Z_LESS              2
#define W3D_Z_GEQUAL            3
#define W3D_Z_LEQUAL            4
#define W3D_Z_GREATER           5
#define W3D_Z_NOTEQUAL          6
#define W3D_Z_EQUAL             7
#define W3D_Z_ALWAYS            8

#define W3D_LO_CLEAR            1
#define W3D_LO_AND              2
#define W3D_LO_AND_REVERSE      3
#define W3D_LO_COPY             4
#define W3D_LO_AND_INVERTED     5
#define W3D_LO_NOOP             6
#define W3D_LO_XOR              7
#define W3D_LO_OR               8
#define W3D_LO_NOR              9
#define W3D_LO_EQUIV            10
#define W3D_LO_INVERT           11
#define W3D_LO_OR_REVERSE       12
#define W3D_LO_COPY_INVERTED    13
#define W3D_LO_OR_INVERTED      14
#define W3D_LO_NAND             15
#define W3D_LO_SET              16

/* Stencil functions: draw if the reference value is ... the stored one. */
#define W3D_ST_NEVER            1
#define W3D_ST_ALWAYS           2
#define W3D_ST_LESS             3
#define W3D_ST_LEQUAL           4
#define W3D_ST_EQUAL            5
#define W3D_ST_GEQUAL           6
#define W3D_ST_GREATER          7
#define W3D_ST_NOTEQUAL         8
#define W3D_ST_NEVER_BIT        (1L << W3D_ST_NEVER)
#define W3D_ST_ALWAYS_BIT       (1L << W3D_ST_ALWAYS)
#define W3D_ST_LESS_BIT         (1L << W3D_ST_LESS)
#define W3D_ST_LEQUAL_BIT       (1L << W3D_ST_LEQUAL)
#define W3D_ST_EQUAL_BIT        (1L << W3D_ST_EQUAL)
#define W3D_ST_GEQUAL_BIT       (1L << W3D_ST_GEQUAL)
#define W3D_ST_GREATER_BIT      (1L << W3D_ST_GREATER)
#define W3D_ST_NOTEQUAL_BIT     (1L << W3D_ST_NOTEQUAL)

/* Stencil operations. */
#define W3D_ST_KEEP             1
#define W3D_ST_ZERO             2
#define W3D_ST_REPLACE          3
#define W3D_ST_INCR             4
#define W3D_ST_DECR             5
#define W3D_ST_INVERT           6
#define W3D_ST_INCR_WRAP        7       /* V5 */
#define W3D_ST_DECR_WRAP        8       /* V5 */
#define W3D_ST_KEEP_BIT         (1L << W3D_ST_KEEP)
#define W3D_ST_ZERO_BIT         (1L << W3D_ST_ZERO)
#define W3D_ST_REPLACE_BIT      (1L << W3D_ST_REPLACE)
#define W3D_ST_INCR_BIT         (1L << W3D_ST_INCR)
#define W3D_ST_DECR_BIT         (1L << W3D_ST_DECR)
#define W3D_ST_INVERT_BIT       (1L << W3D_ST_INVERT)
#define W3D_ST_INCR_WRAP_BIT    (1L << W3D_ST_INCR_WRAP)
#define W3D_ST_DECR_WRAP_BIT    (1L << W3D_ST_DECR_WRAP)

#define W3D_CHROMATEST_NONE      1
#define W3D_CHROMATEST_INCLUSIVE 2      /* texels inside the range are drawn */
#define W3D_CHROMATEST_EXCLUSIVE 3      /* texels inside the range are dropped */

/* ---- Results -------------------------------------------------------------------------- */

#define W3D_SUCCESS              0
#define W3D_BUSY                -1
#define W3D_ILLEGALINPUT        -2
#define W3D_NOMEMORY            -3
#define W3D_NODRIVER            -4
#define W3D_NOTEXTURE           -5
#define W3D_TEXNOTRESIDENT      -6
#define W3D_NOMIPMAPS           -7
#define W3D_NOGFXMEM            -8
#define W3D_NOTVISIBLE          -9
#define W3D_UNSUPPORTEDFILTER   -10
#define W3D_UNSUPPORTEDTEXENV   -11
#define W3D_UNSUPPORTEDWRAPMODE -12
#define W3D_UNSUPPORTEDZCMP     -13
#define W3D_UNSUPPORTEDATEST    -14
#define W3D_UNSUPPORTEDBLEND    -15
#define W3D_UNSUPPORTEDFOG      -16
#define W3D_UNSUPPORTEDSTATE    -17
#define W3D_UNSUPPORTEDFMT      -18
#define W3D_UNSUPPORTEDTEXSIZE  -19
#define W3D_UNSUPPORTEDLOGICOP  -20
#define W3D_UNSUPPORTEDSTTEST   -21
#define W3D_ILLEGALBITMAP       -22
#define W3D_NOZBUFFER           -23
#define W3D_NOPALETTE           -24
#define W3D_MASKNOTSUPPORTED    -25
#define W3D_NOSTENCILBUFFER     -26
#define W3D_QUEUEFAILED         -27
#define W3D_UNSUPPORTEDTEXFMT   -28
#define W3D_WARNING             -29
#define W3D_UNSUPPORTED         -30

/* ---- Hints ------------------------------------------------------------------------------ */

#define W3D_H_TEXMAPPING        1
#define W3D_H_MIPMAPPING        2
#define W3D_H_BILINEARFILTER    3
#define W3D_H_MMFILTER          4
#define W3D_H_PERSPECTIVE       5
#define W3D_H_BLENDING          6
#define W3D_H_FOGGING           7
#define W3D_H_ANTIALIASING      8
#define W3D_H_DITHERING         9
#define W3D_H_ZBUFFER           10
#define W3D_H_POINTDRAW         11
#define W3D_H_FAST              1
#define W3D_H_AVERAGE           2
#define W3D_H_NICE              3

/* ---- Vertex arrays (V4, V5) -------------------------------------------------------- */

#define W3D_CW                  0       /* W3D_SetFrontFace */
#define W3D_CCW                 1

#define W3D_VERTEX_F_F_F        0       /* x, y, z as floats */
#define W3D_VERTEX_F_F_D        1       /* x, y float, z double */
#define W3D_VERTEX_D_D_D        2

#define W3D_COLOR_FLOAT         (0x01 << 30)
#define W3D_COLOR_UBYTE         (0x02 << 30)
#define W3D_CMODE_RGB           0x01
#define W3D_CMODE_BGR           0x02
#define W3D_CMODE_RGBA          0x04
#define W3D_CMODE_ARGB          0x08
#define W3D_CMODE_BGRA          0x10

#define W3D_PRIMITIVE_TRIANGLES 0
#define W3D_PRIMITIVE_TRIFAN    1
#define W3D_PRIMITIVE_TRISTRIP  2
#define W3D_PRIMITIVE_POINTS    3
#define W3D_PRIMITIVE_LINES     4
#define W3D_PRIMITIVE_LINELOOP  5
#define W3D_PRIMITIVE_LINESTRIP 6

#define W3D_INDEX_UBYTE         0
#define W3D_INDEX_UWORD         1
#define W3D_INDEX_ULONG         2

#define W3D_TEXCOORD_NORMALIZED (1L << 0)   /* coordinates are 0..1, not texels */

#define W3D_FOGCOORD_FLOAT      0       /* V5 */
#define W3D_FOGCOORD_DOUBLE     1

/* W3D_InterleavedArray's vertex formats (V5): which fields follow x, y, z. */
#define W3D_VFORMAT_FOG         (1 << 0)    /* 1 float */
#define W3D_VFORMAT_COLOR       (1 << 1)    /* 4 floats, RGBA */
#define W3D_VFORMAT_PACK_COLOR  (1 << 2)    /* 1 ULONG, RGBA */
#define W3D_VFORMAT_SCOLOR      (1 << 3)
#define W3D_VFORMAT_PACK_SCOLOR (1 << 4)
#define W3D_VFORMAT_TCOORD_0    (1 << 5)    /* 3 floats per set: u, v, w */
#define W3D_VFORMAT_TCOORD_1    (1 << 6)
#define W3D_VFORMAT_TCOORD_2    (1 << 7)
#define W3D_VFORMAT_TCOORD_3    (1 << 8)
#define W3D_VFORMAT_TCOORD_4    (1 << 9)
#define W3D_VFORMAT_TCOORD_5    (1 << 10)
#define W3D_VFORMAT_TCOORD_6    (1 << 11)
#define W3D_VFORMAT_TCOORD_7    (1 << 12)
#define W3D_VFORMAT_TCOORD_8    (1 << 13)
#define W3D_VFORMAT_TCOORD_9    (1 << 14)
#define W3D_VFORMAT_TCOORD_10   (1 << 15)
#define W3D_VFORMAT_TCOORD_11   (1 << 16)
#define W3D_VFORMAT_TCOORD_12   (1 << 17)
#define W3D_VFORMAT_TCOORD_13   (1 << 18)
#define W3D_VFORMAT_TCOORD_14   (1 << 19)
#define W3D_VFORMAT_TCOORD_15   (1 << 20)
#define W3D_VFORMAT_FOG_FACTOR  (1 << 21)

/* W3D_SetParameter targets (V5). */
#define W3D_STIPPLE_LINE        0
#define W3D_STIPPLE_LINE_FACTOR 1
#define W3D_STIPPLE_POLYGON     2
#define W3D_POINT_SIZE          3
#define W3D_LINE_WIDTH          4
#define W3D_ZFOG_START          5
#define W3D_ZFOG_END            6
#define W3D_ZFOG_DENSITY        7
#define W3D_FOG_MODE            8
#define W3D_FOG_COLOR           9
#define W3D_WFOG_START          10
#define W3D_WFOG_END            11
#define W3D_WFOG_DENSITY        12

/* W3D_FOG_MODE values (V5). */
#define W3D_FOG_Z_LINEAR        0
#define W3D_FOG_Z_EXP           1
#define W3D_FOG_Z_EXP_2         2
#define W3D_FOG_W_LINEAR        3
#define W3D_FOG_W_EXP           4
#define W3D_FOG_W_EXP_2         5

/* W3D_SetTextureBlend arguments (V5); OR W3D_ARG_COMPLEMENT for 1 - x. */
enum {
    W3D_ARG_PREVIOUS_COLOR = 0, W3D_ARG_PREVIOUS_ALPHA, W3D_ARG_PREVIOUS,
    W3D_ARG_DIFFUSE_COLOR, W3D_ARG_DIFFUSE_ALPHA, W3D_ARG_DIFFUSE,
    W3D_ARG_TEXTURE_COLOR, W3D_ARG_TEXTURE_ALPHA, W3D_ARG_TEXTURE,
    W3D_ARG_TEXTURE0_COLOR, W3D_ARG_TEXTURE0_ALPHA, W3D_ARG_TEXTURE0,
    W3D_ARG_TEXTURE1_COLOR, W3D_ARG_TEXTURE1_ALPHA, W3D_ARG_TEXTURE1,
    W3D_ARG_TEXTURE2_COLOR, W3D_ARG_TEXTURE2_ALPHA, W3D_ARG_TEXTURE2,
    W3D_ARG_TEXTURE3_COLOR, W3D_ARG_TEXTURE3_ALPHA, W3D_ARG_TEXTURE3,
    W3D_ARG_TEXTURE4_COLOR, W3D_ARG_TEXTURE4_ALPHA, W3D_ARG_TEXTURE4,
    W3D_ARG_TEXTURE5_COLOR, W3D_ARG_TEXTURE5_ALPHA, W3D_ARG_TEXTURE5,
    W3D_ARG_TEXTURE6_COLOR, W3D_ARG_TEXTURE6_ALPHA, W3D_ARG_TEXTURE6,
    W3D_ARG_TEXTURE7_COLOR, W3D_ARG_TEXTURE7_ALPHA, W3D_ARG_TEXTURE7,
    W3D_ARG_TEXTURE8_COLOR, W3D_ARG_TEXTURE8_ALPHA, W3D_ARG_TEXTURE8,
    W3D_ARG_TEXTURE9_COLOR, W3D_ARG_TEXTURE9_ALPHA, W3D_ARG_TEXTURE9,
    W3D_ARG_TEXTURE10_COLOR, W3D_ARG_TEXTURE10_ALPHA, W3D_ARG_TEXTURE10,
    W3D_ARG_TEXTURE11_COLOR, W3D_ARG_TEXTURE11_ALPHA, W3D_ARG_TEXTURE11,
    W3D_ARG_TEXTURE12_COLOR, W3D_ARG_TEXTURE12_ALPHA, W3D_ARG_TEXTURE12,
    W3D_ARG_TEXTURE13_COLOR, W3D_ARG_TEXTURE13_ALPHA, W3D_ARG_TEXTURE13,
    W3D_ARG_TEXTURE14_COLOR, W3D_ARG_TEXTURE14_ALPHA, W3D_ARG_TEXTURE14,
    W3D_ARG_TEXTURE15_COLOR, W3D_ARG_TEXTURE15_ALPHA, W3D_ARG_TEXTURE15,
    W3D_ARG_SPECULAR_COLOR, W3D_ARG_SPECULAR_ALPHA, W3D_ARG_SPECULAR,
    W3D_ARG_FACTOR_COLOR, W3D_ARG_FACTOR_ALPHA, W3D_ARG_FACTOR,
    W3D_ARG_COMPLEMENT = (int)0x80000000
};

/* W3D_SetTextureBlend combiners (V5). */
enum {
    W3D_COMBINE_DISABLED = 0, W3D_COMBINE_SELECT_A, W3D_COMBINE_SELECT_B, W3D_COMBINE_SELECT_C,
    W3D_COMBINE_MODULATE, W3D_COMBINE_ADD, W3D_COMBINE_SUBTRACT, W3D_COMBINE_ADDSIGNED,
    W3D_COMBINE_INTERPOLATE, W3D_COMBINE_ACCUM, W3D_COMBINE_DOT3RGB, W3D_COMBINE_DOT3RGBA
};

#ifdef __cplusplus
}
#endif
#endif

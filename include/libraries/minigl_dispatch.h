/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Made by library/minigl/gen_api.py from library/minigl/minigl_api.txt. */
/*
 * minigl.library's table. A program opens the library (version 29 or later)
 * and calls its one function, at -30, which answers this table; every gl*,
 * glu*, glut* and mgl* call goes through it. MiniGLOpen() in libminigl.a does
 * the opening and checks the table: abiVersion must equal
 * MINIGL_DISPATCH_ABI_VERSION, and structSize must be at least the size this
 * header knows.
 *
 * Including <proto/minigl.h> turns every call name into an inline that goes
 * through the table. A program built with libmgl.a instead calls functions
 * of the same names, which do the same.
 */
#ifndef LIBRARIES_MINIGL_DISPATCH_H
#define LIBRARIES_MINIGL_DISPATCH_H

#include <exec/types.h>
#include <mgl/gl.h>
#include <mgl/glut.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINIGL_DISPATCH_ABI_VERSION   5UL
#define MINIGL_BACKEND_FLAG_STUB      (1UL << 0)
#define MINIGL_BACKEND_FLAG_CLASSIC   (1UL << 1)
#define MINIGL_BACKEND_FLAG_PISTORM3D (1UL << 2)
#define MINIGL_BACKEND_FLAG_OPENRTG   (1UL << 16)   /* OpenRTG's minigl.library: OpenGPU or the 68k */

typedef struct MGLDispatchTable {
    ULONG abiVersion;               /* MINIGL_DISPATCH_ABI_VERSION */
    ULONG structSize;               /* sizeof (MGLDispatchTable) as the library has it */
    ULONG backendFlags;             /* MINIGL_BACKEND_FLAG_ */
    ULONG reserved;
    GLcontext *currentContext;      /* the current context, which the calls below pass */
    void (*GLActiveTextureARB)(GLcontext context, GLenum unit);      /* 1 */
    void (*GLAlphaFunc)(GLcontext context, GLenum func, GLclampf ref);
    void (*GLArrayElement)(GLcontext context, GLint i);
    void (*GLBegin)(GLcontext context, GLenum mode);
    void (*GLBindTexture)(GLcontext context, GLenum target, GLuint texture);
    void (*GLBlendFunc)(GLcontext context, GLenum sfactor, GLenum dfactor);
    void (*GLClear)(GLcontext context, GLbitfield mask);
    void (*GLClearColor)(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
    void (*GLClearDepth)(GLcontext context, GLclampd depth);
    void (*GLColor3fv)(GLcontext context, GLfloat *v);
    void (*GLColor3ubv)(GLcontext context, GLubyte *v);      /* 11 */
    void (*GLColor4f)(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
    void (*GLColor4fv)(GLcontext context, GLfloat *v);
    void (*GLColor4ub)(GLcontext context, GLubyte red, GLubyte green, GLubyte blue, GLubyte alhpa);
    void (*GLColor4ubv)(GLcontext context, GLubyte *v);
    void (*GLColorMask)(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
    void (*GLColorPointer)(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
    void (*GLColorTable)(GLcontext context, GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data);
    void (*GLCullFace)(GLcontext context, GLenum mode);
    void (*GLDeleteTextures)(GLcontext context, GLsizei n, const GLuint *textures);
    void (*GLDepthFunc)(GLcontext context, GLenum func);      /* 21 */
    void (*GLDepthMask)(GLcontext context, GLboolean flag);
    void (*GLDepthRange)(GLcontext context, GLclampd n, GLclampd f);
    void (*GLDisableClientState)(GLcontext context, GLenum cap);
    void (*GLDrawArrays)(GLcontext context, GLenum mode, GLint first, GLsizei count);
    void (*GLDrawBuffer)(GLcontext context, GLenum mode);
    void (*GLDrawElements)(GLcontext context, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
    void (*GLEnableClientState)(GLcontext context, GLenum cap);
    void (*GLEnd)(GLcontext context);
    void (*GLFinish)(GLcontext context);
    void (*GLFlush)(GLcontext context);      /* 31 */
    void (*GLFogf)(GLcontext context, GLenum pname, GLfloat param);
    void (*GLFogfv)(GLcontext context, GLenum pname, GLfloat *param);
    void (*GLFrontFace)(GLcontext context, GLenum mode);
    void (*GLFrustum)(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
    void (*GLGenTextures)(GLcontext context, GLsizei n, GLuint *textures);
    void (*GLGetBooleanv)(GLcontext context, GLenum pname, GLboolean *params);
    GLenum (*GLGetError)(GLcontext context);
    void (*GLGetFloatv)(GLcontext context, GLenum pname, GLfloat *params);
    void (*GLGetIntegerv)(GLcontext context, GLenum pname, GLint *params);
    const GLubyte* (*GLGetString)(GLcontext context, GLenum name);      /* 41 */
    void (*GLHint)(GLcontext context, GLenum target, GLenum mode);
    GLboolean (*GLIsEnabled)(GLcontext context, GLenum cap);
    void (*GLLoadIdentity)(GLcontext context);
    void (*GLLoadMatrixd)(GLcontext context, const GLdouble *m);
    void (*GLLoadMatrixf)(GLcontext context, const GLfloat *m);
    void (*GLLockArrays)(GLcontext context, GLuint first, GLsizei count);
    void (*GLMatrixMode)(GLcontext context, GLenum mode);
    void (*GLMultMatrixd)(GLcontext context, const GLdouble *m);
    void (*GLMultMatrixf)(GLcontext context, const GLfloat *m);
    void (*GLMultiTexCoord2fARB)(GLcontext context, GLenum unit, GLfloat s, GLfloat t);      /* 51 */
    void (*GLMultiTexCoord2fvARB)(GLcontext context, GLenum unit, GLfloat *v);
    void (*GLNormal3f)(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
    void (*GLOrtho)(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
    void (*GLPixelStorei)(GLcontext context, GLenum pname, GLint param);
    void (*GLPointSize)(GLcontext context, GLfloat size);
    void (*GLPolygonMode)(GLcontext context, GLenum face, GLenum mode);
    void (*GLPopMatrix)(GLcontext context);
    void (*GLPushMatrix)(GLcontext context);
    void (*GLReadPixels)(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
    void (*GLRotated)(GLcontext context, GLdouble angle, GLdouble x, GLdouble y, GLdouble z);      /* 61 */
    void (*GLRotatef)(GLcontext context, GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
    void (*GLRotatefEXT)(GLcontext context, GLfloat angle, const GLint xyz);
    void (*GLRotatefEXTs)(GLcontext context, GLfloat sin_an, GLfloat cos_an, const GLint xyz);
    void (*GLScaled)(GLcontext context, GLdouble x, GLdouble y, GLdouble z);
    void (*GLScalef)(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
    void (*GLScissor)(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height);
    void (*GLShadeModel)(GLcontext context, GLenum mode);
    void (*GLTexCoord2f)(GLcontext context, GLfloat s, GLfloat t);
    void (*GLTexCoord2fv)(GLcontext context, GLfloat *v);
    void (*GLTexCoord4f)(GLcontext context, GLfloat s, GLfloat t, GLfloat r, GLfloat q);      /* 71 */
    void (*GLTexCoord4fv)(GLcontext context, GLfloat *v);
    void (*GLTexCoordPointer)(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
    void (*GLTexEnvi)(GLcontext context, GLenum target, GLenum pname, GLint param);
    void (*GLTexGeni)(GLcontext context, GLenum coord, GLenum mode, GLenum map);
    void (*GLTexImage2D)(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
    void (*GLTexParameteri)(GLcontext context, GLenum target, GLenum pname, GLint param);
    void (*GLTexSubImage2D)(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
    void (*GLTranslated)(GLcontext context, GLdouble x, GLdouble y, GLdouble z);
    void (*GLTranslatef)(GLcontext context, GLfloat x, GLfloat y, GLfloat z);
    void (*GLULookAt)(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz);      /* 81 */
    void (*GLUPerspective)(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar);
    void (*GLUnlockArrays)(GLcontext context);
    void (*GLVertex2fv)(GLcontext context, GLfloat *v);
    void (*GLVertex3fv)(GLcontext context, GLfloat *v);
    void (*GLVertex4f)(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
    void (*GLVertex4fv)(GLcontext context, GLfloat *v);
    void (*GLVertexPointer)(GLcontext context, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
    void (*GLViewport)(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height);
    void (*MGLClearPointer)(GLcontext context);
    void * (*MGLCreateContext)(int offx, int offy, int w, int h);      /* 91 */
    void * (*MGLCreateContextFromID)(GLint ID, GLint *w, GLint *h);
    void (*MGLDeleteContext)(GLcontext context);
    void (*MGLDrawMultitexBuffer)(GLcontext context, GLenum BSrc, GLenum BDst, GLenum TexEnv);
    void (*MGLEnableSync)(GLcontext context, GLboolean enable);
    void (*MGLExit)(GLcontext context);
    void * (*MGLGetInputWindowHandle)(GLcontext context);
    void * (*MGLGetWindowHandle)(GLcontext context);
    void (*MGLIdleFunc)(GLcontext context, IdleFn i);
    void (*MGLKeyFunc)(GLcontext context, KeyHandlerFn k);
    GLboolean (*MGLLockBack)(GLcontext context, MGLLockInfo *info);      /* 101 */
    GLboolean (*MGLLockDisplay)(GLcontext context);
    void (*MGLLockMode)(GLcontext context, GLenum lockMode);
    void (*MGLMainLoop)(GLcontext context);
    void (*MGLMinTriArea)(GLcontext context, GLfloat area);
    void (*MGLMouseFunc)(GLcontext context, MouseHandlerFn m);
    void (*MGLPrintMatrix)(GLcontext context, int mode);
    void (*MGLPrintMatrixStack)(GLcontext context, int mode);
    GLboolean (*MGLResizeContext)(GLcontext context, GLsizei width, GLsizei height);
    void (*MGLSetPointer)(GLcontext context);
    void (*MGLSetState)(GLcontext context, GLenum cap, GLboolean state);      /* 111 */
    void (*MGLSetZOffset)(GLcontext context, GLfloat offset);
    void (*MGLSpecialFunc)(GLcontext context, SpecialHandlerFn s);
    void (*MGLSwitchDisplay)(GLcontext context);
    void (*MGLTexMemStat)(GLcontext context, GLint *Current, GLint *Peak);
    void (*MGLUnlockDisplay)(GLcontext context);
    void (*MGLWriteShotPPM)(GLcontext context, char *filename);
    void (*mglChooseGuardBand)(GLboolean flag);
    void (*mglChooseMtexBufferSize)(int size);
    void (*mglChooseNumberOfBuffers)(int number);
    void (*mglChoosePixelDepth)(int depth);      /* 121 */
    void (*mglChooseTextureBufferSize)(int size);
    void (*mglChooseVertexBufferSize)(int size);
    void (*mglChooseWindowMode)(GLboolean flag);
    GLint (*mglGetSupportedScreenModes)(MGLScreenModeCallback CallbackFn);
    void (*mglProhibitAlphaFallback)(GLboolean flag);
    void (*mglProhibitMipMapping)(GLboolean flag);
    void (*mglProposeCloseDesktop)(GLboolean closeme);
    void (*GLPolygonOffset)(GLcontext context, GLfloat factor, GLfloat units);
    void (*GLClientActiveTextureARB)(GLcontext context, GLenum unit);
    void (*GLInterleavedArrays)(GLcontext context, GLenum format, GLsizei stride, const GLvoid *pointer);      /* 131 */
    void (*GLMultiDrawArrays)(GLcontext context, GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount);
    void (*GLBlendEquation)(GLcontext context, GLenum mode);
    void (*GLBlendFuncSeparate)(GLcontext context, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
    GLboolean (*GLIsTexture)(GLcontext context, GLuint texture);
    void (*mglChooseZBufferDepth)(int bits);
    void (*GLLineWidth)(GLcontext context, GLfloat width);
    void (*GLTexGenfv)(GLcontext context, GLenum coord, GLenum pname, const GLfloat *params);
    void (*GLCopyTexImage2D)(GLcontext context, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
    void (*GLCopyTexSubImage2D)(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
    GLboolean (*GLAreTexturesResident)(GLcontext context, GLsizei n, const GLuint *textures, GLboolean *residences);      /* 141 */
    void (*GLEdgeFlag)(GLcontext context, GLboolean flag);
    void (*GLEdgeFlagPointer)(GLcontext context, GLsizei stride, const GLvoid *pointer);
    void (*GLEdgeFlagv)(GLcontext context, const GLboolean *flag);
    void (*GLGetDoublev)(GLcontext context, GLenum pname, GLdouble *params);
    void (*GLGetPointerv)(GLcontext context, GLenum pname, GLvoid **params);
    void (*GLIndexi)(GLcontext context, GLint c);
    void (*GLIndexiv)(GLcontext context, const GLint *c);
    void (*GLIndexPointer)(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer);
    void (*GLPrioritizeTextures)(GLcontext context, GLsizei n, const GLuint *textures, const GLclampf *priorities);
    void (*GLReadBuffer)(GLcontext context, GLenum mode);      /* 151 */
    void * (*MGLCreateContextFromWindow)(struct Window *window);
    void * (*MGLCreateContextFromBitMap)(struct BitMap *bitmap);
    void (*GLLightf)(GLcontext context, GLenum light, GLenum pname, GLfloat param);
    void (*GLLightfv)(GLcontext context, GLenum light, GLenum pname, const GLfloat *params);
    void (*GLMaterialf)(GLcontext context, GLenum face, GLenum pname, GLfloat param);
    void (*GLMaterialfv)(GLcontext context, GLenum face, GLenum pname, const GLfloat *params);
    void (*GLLightModelf)(GLcontext context, GLenum pname, GLfloat param);
    void (*GLLightModelfv)(GLcontext context, GLenum pname, const GLfloat *params);
    void (*GLGetLightfv)(GLcontext context, GLenum light, GLenum pname, GLfloat *params);
    void (*GLGetMaterialfv)(GLcontext context, GLenum face, GLenum pname, GLfloat *params);      /* 161 */
    GLuint (*GLGenLists)(GLcontext context, GLsizei range);
    void (*GLDeleteLists)(GLcontext context, GLuint list, GLsizei range);
    GLboolean (*GLIsList)(GLcontext context, GLuint list);
    void (*GLNewList)(GLcontext context, GLuint list, GLenum mode);
    void (*GLEndList)(GLcontext context);
    void (*GLCallList)(GLcontext context, GLuint list);
    GLint (*GLUBuild2DMipmaps)(GLcontext context, GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data);
    const GLubyte * (*GLUErrorString)(GLenum errCode);
    GLUquadricObj * (*GLUNewQuadric)(void);
    void (*GLUDeleteQuadric)(GLUquadricObj *q);      /* 171 */
    void (*GLUQuadricNormals)(GLUquadricObj *q, GLenum normals);
    void (*GLUQuadricTexture)(GLUquadricObj *q, GLboolean textureCoords);
    void (*GLUQuadricDrawStyle)(GLUquadricObj *q, GLenum drawStyle);
    void (*GLUQuadricOrientation)(GLUquadricObj *q, GLenum orientation);
    void (*GLUCylinder)(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks);
    void (*GLUSphere)(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks);
    void (*GLUDisk)(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops);
    void (*GLUTInit)(int *argcp, char **argv);
    void (*GLUTInitDisplayMode)(unsigned int mode);
    void (*GLUTInitWindowSize)(int width, int height);      /* 181 */
    void (*GLUTInitWindowPosition)(int x, int y);
    int (*GLUTCreateWindow)(const char *title);
    void (*GLUTMainLoop)(void);
    void (*GLUTDisplayFunc)(void (*func)(void));
    void (*GLUTIdleFunc)(void (*func)(void));
    void (*GLUTKeyboardFunc)(void (*func)(unsigned char key, int x, int y));
    void (*GLUTReshapeFunc)(void (*func)(int width, int height));
    void (*GLUTSwapBuffers)(void);
    void (*GLUTPostRedisplay)(void);
    int (*GLUTGet)(GLenum state);      /* 191 */
    void (*GLUTGameModeString)(const char *string);
    int (*GLUTEnterGameMode)(void);
    void (*GLUTLeaveGameMode)(void);
    int (*GLUTGameModeGet)(GLenum query);
    void (*GLUTSolidCube)(GLdouble size);
    void (*GLUTSolidSphere)(GLdouble radius, GLint slices, GLint stacks);
    void (*GLUTSolidCone)(GLdouble base, GLdouble height, GLint slices, GLint stacks);
    void (*GLUTSolidTorus)(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings);
    void (*GLUTSolidDodecahedron)(void);
    void (*GLNormalPointer)(GLcontext context, GLenum type, GLsizei stride, const GLvoid *pointer);      /* 201 */
    void (*GLUQuadricCallback)(GLUquadricObj *q, GLenum which, MGLUfuncptr fn);
    void (*GLColorMaterial)(GLcontext context, GLenum face, GLenum mode);
    void (*GLTexEnvfv)(GLcontext context, GLenum target, GLenum pname, const GLfloat *params);
    void (*GLNormal3fv)(GLcontext context, GLfloat *n);
} MGLDispatchTable;

extern const MGLDispatchTable *MiniGLDispatch;

#ifndef MINIGL_LIBRARY_BUILD

/* glutInit registers this with atexit: a GLUT program usually leaves by
 * exit() from its keyboard function, and its context is let go here. */
extern int atexit(void (*func)(void));
static void mgl_glut_release(void)
{
    if (MiniGLDispatch && (*MiniGLDispatch->currentContext)) {
        GLcontext context = (*MiniGLDispatch->currentContext);
        (*MiniGLDispatch->currentContext) = (GLcontext)0;
        MiniGLDispatch->MGLDeleteContext(context);
    }
}

static __inline__ void MGLD_glActiveTextureARB(GLenum unit)
{
    MiniGLDispatch->GLActiveTextureARB((*MiniGLDispatch->currentContext), unit);
}
#undef glActiveTextureARB
#define glActiveTextureARB MGLD_glActiveTextureARB

static __inline__ void MGLD_glAlphaFunc(GLenum func, GLclampf ref)
{
    MiniGLDispatch->GLAlphaFunc((*MiniGLDispatch->currentContext), func, ref);
}
#undef glAlphaFunc
#define glAlphaFunc MGLD_glAlphaFunc

static __inline__ void MGLD_glArrayElement(GLint i)
{
    MiniGLDispatch->GLArrayElement((*MiniGLDispatch->currentContext), i);
}
#undef glArrayElement
#define glArrayElement MGLD_glArrayElement

static __inline__ void MGLD_glBegin(GLenum mode)
{
    MiniGLDispatch->GLBegin((*MiniGLDispatch->currentContext), mode);
}
#undef glBegin
#define glBegin MGLD_glBegin

static __inline__ void MGLD_glBindTexture(GLenum target, GLuint texture)
{
    MiniGLDispatch->GLBindTexture((*MiniGLDispatch->currentContext), target, texture);
}
#undef glBindTexture
#define glBindTexture MGLD_glBindTexture

static __inline__ void MGLD_glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    MiniGLDispatch->GLBlendFunc((*MiniGLDispatch->currentContext), sfactor, dfactor);
}
#undef glBlendFunc
#define glBlendFunc MGLD_glBlendFunc

static __inline__ void MGLD_glClear(GLbitfield mask)
{
    MiniGLDispatch->GLClear((*MiniGLDispatch->currentContext), mask);
}
#undef glClear
#define glClear MGLD_glClear

static __inline__ void MGLD_glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    MiniGLDispatch->GLClearColor((*MiniGLDispatch->currentContext), red, green, blue, alpha);
}
#undef glClearColor
#define glClearColor MGLD_glClearColor

static __inline__ void MGLD_glClearDepth(GLclampd depth)
{
    MiniGLDispatch->GLClearDepth((*MiniGLDispatch->currentContext), depth);
}
#undef glClearDepth
#define glClearDepth MGLD_glClearDepth

static __inline__ void MGLD_glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    MiniGLDispatch->GLColor4f((*MiniGLDispatch->currentContext), red, green, blue, 1.0f);
}
#undef glColor3f
#define glColor3f MGLD_glColor3f

static __inline__ void MGLD_glColor3fv(GLfloat *v)
{
    MiniGLDispatch->GLColor3fv((*MiniGLDispatch->currentContext), v);
}
#undef glColor3fv
#define glColor3fv MGLD_glColor3fv

static __inline__ void MGLD_glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    MiniGLDispatch->GLColor4ub((*MiniGLDispatch->currentContext), red, green, blue, 255);
}
#undef glColor3ub
#define glColor3ub MGLD_glColor3ub

static __inline__ void MGLD_glColor3ubv(GLubyte *v)
{
    MiniGLDispatch->GLColor3ubv((*MiniGLDispatch->currentContext), v);
}
#undef glColor3ubv
#define glColor3ubv MGLD_glColor3ubv

static __inline__ void MGLD_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    MiniGLDispatch->GLColor4f((*MiniGLDispatch->currentContext), red, green, blue, alpha);
}
#undef glColor4f
#define glColor4f MGLD_glColor4f

static __inline__ void MGLD_glColor4fv(GLfloat *v)
{
    MiniGLDispatch->GLColor4fv((*MiniGLDispatch->currentContext), v);
}
#undef glColor4fv
#define glColor4fv MGLD_glColor4fv

static __inline__ void MGLD_glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    MiniGLDispatch->GLColor4ub((*MiniGLDispatch->currentContext), red, green, blue, alpha);
}
#undef glColor4ub
#define glColor4ub MGLD_glColor4ub

static __inline__ void MGLD_glColor4ubv(GLubyte *v)
{
    MiniGLDispatch->GLColor4ubv((*MiniGLDispatch->currentContext), v);
}
#undef glColor4ubv
#define glColor4ubv MGLD_glColor4ubv

static __inline__ void MGLD_glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    MiniGLDispatch->GLColorMask((*MiniGLDispatch->currentContext), red, green, blue, alpha);
}
#undef glColorMask
#define glColorMask MGLD_glColorMask

static __inline__ void MGLD_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLColorPointer((*MiniGLDispatch->currentContext), size, type, stride, pointer);
}
#undef glColorPointer
#define glColorPointer MGLD_glColorPointer

static __inline__ void MGLD_glColorTable(GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
    MiniGLDispatch->GLColorTable((*MiniGLDispatch->currentContext), target, internalformat, width, format, type, data);
}
#undef glColorTable
#define glColorTable MGLD_glColorTable

static __inline__ void MGLD_glColorTableEXT(GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
    MiniGLDispatch->GLColorTable((*MiniGLDispatch->currentContext), target, internalformat, width, format, type, data);
}
#undef glColorTableEXT
#define glColorTableEXT MGLD_glColorTableEXT

static __inline__ void MGLD_glCullFace(GLenum mode)
{
    MiniGLDispatch->GLCullFace((*MiniGLDispatch->currentContext), mode);
}
#undef glCullFace
#define glCullFace MGLD_glCullFace

static __inline__ void MGLD_glDeleteTextures(GLsizei n, const GLuint *textures)
{
    MiniGLDispatch->GLDeleteTextures((*MiniGLDispatch->currentContext), n, textures);
}
#undef glDeleteTextures
#define glDeleteTextures MGLD_glDeleteTextures

static __inline__ void MGLD_glDepthFunc(GLenum func)
{
    MiniGLDispatch->GLDepthFunc((*MiniGLDispatch->currentContext), func);
}
#undef glDepthFunc
#define glDepthFunc MGLD_glDepthFunc

static __inline__ void MGLD_glDepthMask(GLboolean flag)
{
    MiniGLDispatch->GLDepthMask((*MiniGLDispatch->currentContext), flag);
}
#undef glDepthMask
#define glDepthMask MGLD_glDepthMask

static __inline__ void MGLD_glDepthRange(GLclampd n, GLclampd f)
{
    MiniGLDispatch->GLDepthRange((*MiniGLDispatch->currentContext), n, f);
}
#undef glDepthRange
#define glDepthRange MGLD_glDepthRange

static __inline__ void MGLD_glDisable(GLenum cap)
{
    MiniGLDispatch->MGLSetState((*MiniGLDispatch->currentContext), cap, GL_FALSE);
}
#undef glDisable
#define glDisable MGLD_glDisable

static __inline__ void MGLD_glDisableClientState(GLenum cap)
{
    MiniGLDispatch->GLDisableClientState((*MiniGLDispatch->currentContext), cap);
}
#undef glDisableClientState
#define glDisableClientState MGLD_glDisableClientState

static __inline__ void MGLD_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    MiniGLDispatch->GLDrawArrays((*MiniGLDispatch->currentContext), mode, first, count);
}
#undef glDrawArrays
#define glDrawArrays MGLD_glDrawArrays

static __inline__ void MGLD_glDrawBuffer(GLenum mode)
{
    MiniGLDispatch->GLDrawBuffer((*MiniGLDispatch->currentContext), mode);
}
#undef glDrawBuffer
#define glDrawBuffer MGLD_glDrawBuffer

static __inline__ void MGLD_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *pointer)
{
    MiniGLDispatch->GLDrawElements((*MiniGLDispatch->currentContext), mode, count, type, pointer);
}
#undef glDrawElements
#define glDrawElements MGLD_glDrawElements

static __inline__ void MGLD_glEnable(GLenum cap)
{
    MiniGLDispatch->MGLSetState((*MiniGLDispatch->currentContext), cap, GL_TRUE);
}
#undef glEnable
#define glEnable MGLD_glEnable

static __inline__ void MGLD_glEnableClientState(GLenum cap)
{
    MiniGLDispatch->GLEnableClientState((*MiniGLDispatch->currentContext), cap);
}
#undef glEnableClientState
#define glEnableClientState MGLD_glEnableClientState

static __inline__ void MGLD_glEnd(void)
{
    MiniGLDispatch->GLEnd((*MiniGLDispatch->currentContext));
}
#undef glEnd
#define glEnd MGLD_glEnd

static __inline__ void MGLD_glFinish(void)
{
    MiniGLDispatch->GLFinish((*MiniGLDispatch->currentContext));
}
#undef glFinish
#define glFinish MGLD_glFinish

static __inline__ void MGLD_glFlush(void)
{
    MiniGLDispatch->GLFlush((*MiniGLDispatch->currentContext));
}
#undef glFlush
#define glFlush MGLD_glFlush

static __inline__ void MGLD_glFogf(GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLFogf((*MiniGLDispatch->currentContext), pname, param);
}
#undef glFogf
#define glFogf MGLD_glFogf

static __inline__ void MGLD_glFogfv(GLenum pname, GLfloat *param)
{
    MiniGLDispatch->GLFogfv((*MiniGLDispatch->currentContext), pname, param);
}
#undef glFogfv
#define glFogfv MGLD_glFogfv

static __inline__ void MGLD_glFogi(GLenum pname, GLint param)
{
    MiniGLDispatch->GLFogf((*MiniGLDispatch->currentContext), pname, (GLfloat)param);
}
#undef glFogi
#define glFogi MGLD_glFogi

static __inline__ void MGLD_glFrontFace(GLenum mode)
{
    MiniGLDispatch->GLFrontFace((*MiniGLDispatch->currentContext), mode);
}
#undef glFrontFace
#define glFrontFace MGLD_glFrontFace

static __inline__ void MGLD_glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    MiniGLDispatch->GLFrustum((*MiniGLDispatch->currentContext), left, right, bottom, top, zNear, zFar);
}
#undef glFrustum
#define glFrustum MGLD_glFrustum

static __inline__ void MGLD_glGenTextures(GLsizei n, GLuint *textures)
{
    MiniGLDispatch->GLGenTextures((*MiniGLDispatch->currentContext), n, textures);
}
#undef glGenTextures
#define glGenTextures MGLD_glGenTextures

static __inline__ void MGLD_glGetBooleanv(GLenum pname, GLboolean *params)
{
    MiniGLDispatch->GLGetBooleanv((*MiniGLDispatch->currentContext), pname, params);
}
#undef glGetBooleanv
#define glGetBooleanv MGLD_glGetBooleanv

static __inline__ GLenum MGLD_glGetError(void)
{
    return MiniGLDispatch->GLGetError((*MiniGLDispatch->currentContext));
}
#undef glGetError
#define glGetError MGLD_glGetError

static __inline__ void MGLD_glGetFloatv(GLenum pname, GLfloat *params)
{
    MiniGLDispatch->GLGetFloatv((*MiniGLDispatch->currentContext), pname, params);
}
#undef glGetFloatv
#define glGetFloatv MGLD_glGetFloatv

static __inline__ void MGLD_glGetIntegerv(GLenum pname, GLint *params)
{
    MiniGLDispatch->GLGetIntegerv((*MiniGLDispatch->currentContext), pname, params);
}
#undef glGetIntegerv
#define glGetIntegerv MGLD_glGetIntegerv

static __inline__ const GLubyte *MGLD_glGetString(GLenum name)
{
    return MiniGLDispatch->GLGetString((*MiniGLDispatch->currentContext), name);
}
#undef glGetString
#define glGetString MGLD_glGetString

static __inline__ void MGLD_glHint(GLenum target, GLenum mode)
{
    MiniGLDispatch->GLHint((*MiniGLDispatch->currentContext), target, mode);
}
#undef glHint
#define glHint MGLD_glHint

static __inline__ GLboolean MGLD_glIsEnabled(GLenum cap)
{
    return MiniGLDispatch->GLIsEnabled((*MiniGLDispatch->currentContext), cap);
}
#undef glIsEnabled
#define glIsEnabled MGLD_glIsEnabled

static __inline__ void MGLD_glLoadIdentity(void)
{
    MiniGLDispatch->GLLoadIdentity((*MiniGLDispatch->currentContext));
}
#undef glLoadIdentity
#define glLoadIdentity MGLD_glLoadIdentity

static __inline__ void MGLD_glLoadMatrixd(const GLdouble *m)
{
    MiniGLDispatch->GLLoadMatrixd((*MiniGLDispatch->currentContext), m);
}
#undef glLoadMatrixd
#define glLoadMatrixd MGLD_glLoadMatrixd

static __inline__ void MGLD_glLoadMatrixf(const GLfloat *m)
{
    MiniGLDispatch->GLLoadMatrixf((*MiniGLDispatch->currentContext), m);
}
#undef glLoadMatrixf
#define glLoadMatrixf MGLD_glLoadMatrixf

static __inline__ void MGLD_glLockArrays(GLuint first, GLsizei count)
{
    MiniGLDispatch->GLLockArrays((*MiniGLDispatch->currentContext), first, count);
}
#undef glLockArrays
#define glLockArrays MGLD_glLockArrays

static __inline__ void MGLD_glMatrixMode(GLenum mode)
{
    MiniGLDispatch->GLMatrixMode((*MiniGLDispatch->currentContext), mode);
}
#undef glMatrixMode
#define glMatrixMode MGLD_glMatrixMode

static __inline__ void MGLD_glMultiTexCoord2fARB(GLenum unit, GLfloat s, GLfloat t)
{
    MiniGLDispatch->GLMultiTexCoord2fARB((*MiniGLDispatch->currentContext), unit, s, t);
}
#undef glMultiTexCoord2fARB
#define glMultiTexCoord2fARB MGLD_glMultiTexCoord2fARB

static __inline__ void MGLD_glMultiTexCoord2fvARB(GLenum unit, GLfloat *v)
{
    MiniGLDispatch->GLMultiTexCoord2fvARB((*MiniGLDispatch->currentContext), unit, v);
}
#undef glMultiTexCoord2fvARB
#define glMultiTexCoord2fvARB MGLD_glMultiTexCoord2fvARB

static __inline__ void MGLD_glMultMatrixd(const GLdouble *m)
{
    MiniGLDispatch->GLMultMatrixd((*MiniGLDispatch->currentContext), m);
}
#undef glMultMatrixd
#define glMultMatrixd MGLD_glMultMatrixd

static __inline__ void MGLD_glMultMatrixf(const GLfloat *m)
{
    MiniGLDispatch->GLMultMatrixf((*MiniGLDispatch->currentContext), m);
}
#undef glMultMatrixf
#define glMultMatrixf MGLD_glMultMatrixf

static __inline__ void MGLD_glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLNormal3f((*MiniGLDispatch->currentContext), x, y, z);
}
#undef glNormal3f
#define glNormal3f MGLD_glNormal3f

static __inline__ void MGLD_glNormal3fv(GLfloat *v)
{
    MiniGLDispatch->GLNormal3fv((*MiniGLDispatch->currentContext), v);
}
#undef glNormal3fv
#define glNormal3fv MGLD_glNormal3fv

static __inline__ void MGLD_glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    MiniGLDispatch->GLOrtho((*MiniGLDispatch->currentContext), left, right, bottom, top, zNear, zFar);
}
#undef glOrtho
#define glOrtho MGLD_glOrtho

static __inline__ void MGLD_glPixelStorei(GLenum pname, GLint param)
{
    MiniGLDispatch->GLPixelStorei((*MiniGLDispatch->currentContext), pname, param);
}
#undef glPixelStorei
#define glPixelStorei MGLD_glPixelStorei

static __inline__ void MGLD_glPointSize(GLfloat s)
{
    MiniGLDispatch->GLPointSize((*MiniGLDispatch->currentContext), s);
}
#undef glPointSize
#define glPointSize MGLD_glPointSize

static __inline__ void MGLD_glLineWidth(GLfloat w)
{
    MiniGLDispatch->GLLineWidth((*MiniGLDispatch->currentContext), w);
}
#undef glLineWidth
#define glLineWidth MGLD_glLineWidth

static __inline__ void MGLD_glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLTexGenfv((*MiniGLDispatch->currentContext), coord, pname, params);
}
#undef glTexGenfv
#define glTexGenfv MGLD_glTexGenfv

static __inline__ void MGLD_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    MiniGLDispatch->GLCopyTexImage2D((*MiniGLDispatch->currentContext), target, level, internalformat, x, y, width, height, border);
}
#undef glCopyTexImage2D
#define glCopyTexImage2D MGLD_glCopyTexImage2D

static __inline__ void MGLD_glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    MiniGLDispatch->GLCopyTexSubImage2D((*MiniGLDispatch->currentContext), target, level, xoffset, yoffset, x, y, width, height);
}
#undef glCopyTexSubImage2D
#define glCopyTexSubImage2D MGLD_glCopyTexSubImage2D

static __inline__ void MGLD_glPolygonMode(GLenum face, GLenum mode)
{
    MiniGLDispatch->GLPolygonMode((*MiniGLDispatch->currentContext), face, mode);
}
#undef glPolygonMode
#define glPolygonMode MGLD_glPolygonMode

static __inline__ void MGLD_glPolygonOffset(GLfloat factor, GLfloat units)
{
    MiniGLDispatch->GLPolygonOffset((*MiniGLDispatch->currentContext), factor, units);
}
#undef glPolygonOffset
#define glPolygonOffset MGLD_glPolygonOffset

static __inline__ void MGLD_glPopMatrix(void)
{
    MiniGLDispatch->GLPopMatrix((*MiniGLDispatch->currentContext));
}
#undef glPopMatrix
#define glPopMatrix MGLD_glPopMatrix

static __inline__ void MGLD_glPushMatrix(void)
{
    MiniGLDispatch->GLPushMatrix((*MiniGLDispatch->currentContext));
}
#undef glPushMatrix
#define glPushMatrix MGLD_glPushMatrix

static __inline__ void MGLD_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    MiniGLDispatch->GLReadPixels((*MiniGLDispatch->currentContext), x, y, width, height, format, type, pixels);
}
#undef glReadPixels
#define glReadPixels MGLD_glReadPixels

static __inline__ void MGLD_glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    MiniGLDispatch->GLRotated((*MiniGLDispatch->currentContext), angle, x, y, z);
}
#undef glRotated
#define glRotated MGLD_glRotated

static __inline__ void MGLD_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLRotatef((*MiniGLDispatch->currentContext), angle, x, y, z);
}
#undef glRotatef
#define glRotatef MGLD_glRotatef

static __inline__ void MGLD_glRotatefEXT(GLfloat angle, const GLint xyz)
{
    MiniGLDispatch->GLRotatefEXT((*MiniGLDispatch->currentContext), angle, xyz);
}
#undef glRotatefEXT
#define glRotatefEXT MGLD_glRotatefEXT

static __inline__ void MGLD_glRotatefEXTs(GLfloat sin_an, GLfloat cos_an, const GLint xyz)
{
    MiniGLDispatch->GLRotatefEXTs((*MiniGLDispatch->currentContext), sin_an, cos_an, xyz);
}
#undef glRotatefEXTs
#define glRotatefEXTs MGLD_glRotatefEXTs

static __inline__ void MGLD_glScaled(GLdouble x, GLdouble y, GLdouble z)
{
    MiniGLDispatch->GLScaled((*MiniGLDispatch->currentContext), x, y, z);
}
#undef glScaled
#define glScaled MGLD_glScaled

static __inline__ void MGLD_glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLScalef((*MiniGLDispatch->currentContext), x, y, z);
}
#undef glScalef
#define glScalef MGLD_glScalef

static __inline__ void MGLD_glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    MiniGLDispatch->GLScissor((*MiniGLDispatch->currentContext), x, y, width, height);
}
#undef glScissor
#define glScissor MGLD_glScissor

static __inline__ void MGLD_glShadeModel(GLenum mode)
{
    MiniGLDispatch->GLShadeModel((*MiniGLDispatch->currentContext), mode);
}
#undef glShadeModel
#define glShadeModel MGLD_glShadeModel

static __inline__ void MGLD_glTexCoord2f(GLfloat s, GLfloat t)
{
    MiniGLDispatch->GLTexCoord2f((*MiniGLDispatch->currentContext), s, t);
}
#undef glTexCoord2f
#define glTexCoord2f MGLD_glTexCoord2f

static __inline__ void MGLD_glTexCoord2fv(GLfloat *v)
{
    MiniGLDispatch->GLTexCoord2fv((*MiniGLDispatch->currentContext), v);
}
#undef glTexCoord2fv
#define glTexCoord2fv MGLD_glTexCoord2fv

static __inline__ void MGLD_glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    MiniGLDispatch->GLTexCoord4f((*MiniGLDispatch->currentContext), s, t, r, q);
}
#undef glTexCoord4f
#define glTexCoord4f MGLD_glTexCoord4f

static __inline__ void MGLD_glTexCoord4fv(GLfloat *v)
{
    MiniGLDispatch->GLTexCoord4fv((*MiniGLDispatch->currentContext), v);
}
#undef glTexCoord4fv
#define glTexCoord4fv MGLD_glTexCoord4fv

static __inline__ void MGLD_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLTexCoordPointer((*MiniGLDispatch->currentContext), size, type, stride, pointer);
}
#undef glTexCoordPointer
#define glTexCoordPointer MGLD_glTexCoordPointer

static __inline__ void MGLD_glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLTexEnvi((*MiniGLDispatch->currentContext), target, pname, (GLint)param);
}
#undef glTexEnvf
#define glTexEnvf MGLD_glTexEnvf

static __inline__ void MGLD_glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    MiniGLDispatch->GLTexEnvi((*MiniGLDispatch->currentContext), target, pname, param);
}
#undef glTexEnvi
#define glTexEnvi MGLD_glTexEnvi

static __inline__ void MGLD_glTexEnviv(GLenum target, GLenum pname, GLint *param)
{
    MiniGLDispatch->GLTexEnvi((*MiniGLDispatch->currentContext), target, pname, *param);
}
#undef glTexEnviv
#define glTexEnviv MGLD_glTexEnviv

static __inline__ void MGLD_glTexGeni(GLenum coord, GLenum mode, GLenum map)
{
    MiniGLDispatch->GLTexGeni((*MiniGLDispatch->currentContext), coord, mode, map);
}
#undef glTexGeni
#define glTexGeni MGLD_glTexGeni

static __inline__ void MGLD_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    MiniGLDispatch->GLTexImage2D((*MiniGLDispatch->currentContext), target, level, internalformat, width, height, border, format, type, pixels);
}
#undef glTexImage2D
#define glTexImage2D MGLD_glTexImage2D

static __inline__ void MGLD_glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLTexParameteri((*MiniGLDispatch->currentContext), target, pname, (GLint)param);
}
#undef glTexParameterf
#define glTexParameterf MGLD_glTexParameterf

static __inline__ void MGLD_glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    MiniGLDispatch->GLTexParameteri((*MiniGLDispatch->currentContext), target, pname, param);
}
#undef glTexParameteri
#define glTexParameteri MGLD_glTexParameteri

static __inline__ void MGLD_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    MiniGLDispatch->GLTexSubImage2D((*MiniGLDispatch->currentContext), target, level, xoffset, yoffset, width, height, format, type, pixels);
}
#undef glTexSubImage2D
#define glTexSubImage2D MGLD_glTexSubImage2D

static __inline__ void MGLD_glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    MiniGLDispatch->GLTranslated((*MiniGLDispatch->currentContext), x, y, z);
}
#undef glTranslated
#define glTranslated MGLD_glTranslated

static __inline__ void MGLD_glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLTranslatef((*MiniGLDispatch->currentContext), x, y, z);
}
#undef glTranslatef
#define glTranslatef MGLD_glTranslatef

static __inline__ void MGLD_gluLookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLULookAt(ex, ey, ez, cx, cy, cz, ux, uy, uz);
}
#undef gluLookAt
#define gluLookAt MGLD_gluLookAt

static __inline__ void MGLD_glUnlockArrays(void)
{
    MiniGLDispatch->GLUnlockArrays((*MiniGLDispatch->currentContext));
}
#undef glUnlockArrays
#define glUnlockArrays MGLD_glUnlockArrays

static __inline__ void MGLD_gluPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUPerspective(fovy, aspect, znear, zfar);
}
#undef gluPerspective
#define gluPerspective MGLD_gluPerspective

static __inline__ void MGLD_glVertex2f(GLfloat x, GLfloat y)
{
    MiniGLDispatch->GLVertex4f((*MiniGLDispatch->currentContext), x, y, 0.0f, 1.0f);
}
#undef glVertex2f
#define glVertex2f MGLD_glVertex2f

static __inline__ void MGLD_glVertex2fv(GLfloat *v)
{
    MiniGLDispatch->GLVertex2fv((*MiniGLDispatch->currentContext), v);
}
#undef glVertex2fv
#define glVertex2fv MGLD_glVertex2fv

static __inline__ void MGLD_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLVertex4f((*MiniGLDispatch->currentContext), x, y, z, 1.0f);
}
#undef glVertex3f
#define glVertex3f MGLD_glVertex3f

static __inline__ void MGLD_glVertex3fv(GLfloat *v)
{
    MiniGLDispatch->GLVertex3fv((*MiniGLDispatch->currentContext), v);
}
#undef glVertex3fv
#define glVertex3fv MGLD_glVertex3fv

static __inline__ void MGLD_glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    MiniGLDispatch->GLVertex4f((*MiniGLDispatch->currentContext), x, y, z, w);
}
#undef glVertex4f
#define glVertex4f MGLD_glVertex4f

static __inline__ void MGLD_glVertex4fv(GLfloat *v)
{
    MiniGLDispatch->GLVertex4fv((*MiniGLDispatch->currentContext), v);
}
#undef glVertex4fv
#define glVertex4fv MGLD_glVertex4fv

static __inline__ void MGLD_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLVertexPointer((*MiniGLDispatch->currentContext), size, type, stride, pointer);
}
#undef glVertexPointer
#define glVertexPointer MGLD_glVertexPointer

static __inline__ void MGLD_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLNormalPointer((*MiniGLDispatch->currentContext), type, stride, pointer);
}
#undef glNormalPointer
#define glNormalPointer MGLD_glNormalPointer

static __inline__ void MGLD_gluQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn)
{
    MiniGLDispatch->GLUQuadricCallback(q, which, fn);
}
#undef gluQuadricCallback
#define gluQuadricCallback MGLD_gluQuadricCallback

static __inline__ void MGLD_glColorMaterial(GLenum face, GLenum mode)
{
    MiniGLDispatch->GLColorMaterial((*MiniGLDispatch->currentContext), face, mode);
}
#undef glColorMaterial
#define glColorMaterial MGLD_glColorMaterial

static __inline__ void MGLD_glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLTexEnvfv((*MiniGLDispatch->currentContext), target, pname, params);
}
#undef glTexEnvfv
#define glTexEnvfv MGLD_glTexEnvfv

static __inline__ void MGLD_glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    MiniGLDispatch->GLViewport((*MiniGLDispatch->currentContext), x, y, width, height);
}
#undef glViewport
#define glViewport MGLD_glViewport

static __inline__ void MGLD_mglClearPointer(void)
{
    MiniGLDispatch->MGLClearPointer((*MiniGLDispatch->currentContext));
}
#undef mglClearPointer
#define mglClearPointer MGLD_mglClearPointer

static __inline__ void *MGLD_mglCreateContext(int offx, int offy, int w, int h)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContext(offx, offy, w, h);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}
#undef mglCreateContext
#define mglCreateContext MGLD_mglCreateContext

static __inline__ void *MGLD_mglCreateContextFromID(GLint id, GLint *w, GLint *h)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContextFromID(id, w, h);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}
#undef mglCreateContextFromID
#define mglCreateContextFromID MGLD_mglCreateContextFromID

static __inline__ void *MGLD_mglCreateContextFromWindow(struct Window *window)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContextFromWindow(window);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}
#undef mglCreateContextFromWindow
#define mglCreateContextFromWindow MGLD_mglCreateContextFromWindow

static __inline__ void *MGLD_mglCreateContextFromBitMap(struct BitMap *bitmap)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContextFromBitMap(bitmap);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}
#undef mglCreateContextFromBitMap
#define mglCreateContextFromBitMap MGLD_mglCreateContextFromBitMap

static __inline__ void MGLD_mglDrawMultitexBuffer(GLenum s, GLenum d, GLenum env)
{
    MiniGLDispatch->MGLDrawMultitexBuffer((*MiniGLDispatch->currentContext), s, d, env);
}
#undef mglDrawMultitexBuffer
#define mglDrawMultitexBuffer MGLD_mglDrawMultitexBuffer

static __inline__ void MGLD_mglEnableSync(GLboolean enable)
{
    MiniGLDispatch->MGLEnableSync((*MiniGLDispatch->currentContext), enable);
}
#undef mglEnableSync
#define mglEnableSync MGLD_mglEnableSync

static __inline__ void MGLD_mglExit(void)
{
    MiniGLDispatch->MGLExit((*MiniGLDispatch->currentContext));
}
#undef mglExit
#define mglExit MGLD_mglExit

static __inline__ void *MGLD_mglGetWindowHandle(void)
{
    return MiniGLDispatch->MGLGetWindowHandle((*MiniGLDispatch->currentContext));
}
#undef mglGetWindowHandle
#define mglGetWindowHandle MGLD_mglGetWindowHandle

static __inline__ void MGLD_mglIdleFunc(IdleFn i)
{
    MiniGLDispatch->MGLIdleFunc((*MiniGLDispatch->currentContext), i);
}
#undef mglIdleFunc
#define mglIdleFunc MGLD_mglIdleFunc

static __inline__ void MGLD_mglKeyFunc(KeyHandlerFn k)
{
    MiniGLDispatch->MGLKeyFunc((*MiniGLDispatch->currentContext), k);
}
#undef mglKeyFunc
#define mglKeyFunc MGLD_mglKeyFunc

static __inline__ GLboolean MGLD_mglLockBack(MGLLockInfo *info)
{
    return MiniGLDispatch->MGLLockBack((*MiniGLDispatch->currentContext), info);
}
#undef mglLockBack
#define mglLockBack MGLD_mglLockBack

static __inline__ GLboolean MGLD_mglLockDisplay(void)
{
    return MiniGLDispatch->MGLLockDisplay((*MiniGLDispatch->currentContext));
}
#undef mglLockDisplay
#define mglLockDisplay MGLD_mglLockDisplay

static __inline__ void MGLD_mglLockMode(GLenum lockMode)
{
    MiniGLDispatch->MGLLockMode((*MiniGLDispatch->currentContext), lockMode);
}
#undef mglLockMode
#define mglLockMode MGLD_mglLockMode

static __inline__ void MGLD_mglMainLoop(void)
{
    MiniGLDispatch->MGLMainLoop((*MiniGLDispatch->currentContext));
}
#undef mglMainLoop
#define mglMainLoop MGLD_mglMainLoop

static __inline__ void MGLD_mglMinTriArea(GLfloat area)
{
    MiniGLDispatch->MGLMinTriArea((*MiniGLDispatch->currentContext), area);
}
#undef mglMinTriArea
#define mglMinTriArea MGLD_mglMinTriArea

static __inline__ void MGLD_mglMouseFunc(MouseHandlerFn m)
{
    MiniGLDispatch->MGLMouseFunc((*MiniGLDispatch->currentContext), m);
}
#undef mglMouseFunc
#define mglMouseFunc MGLD_mglMouseFunc

static __inline__ void MGLD_mglPrintMatrix(GLenum mode)
{
    MiniGLDispatch->MGLPrintMatrix((*MiniGLDispatch->currentContext), mode);
}
#undef mglPrintMatrix
#define mglPrintMatrix MGLD_mglPrintMatrix

static __inline__ void MGLD_mglPrintMatrixStack(GLenum mode)
{
    MiniGLDispatch->MGLPrintMatrixStack((*MiniGLDispatch->currentContext), mode);
}
#undef mglPrintMatrixStack
#define mglPrintMatrixStack MGLD_mglPrintMatrixStack

static __inline__ GLboolean MGLD_mglResizeContext(GLsizei width, GLsizei height)
{
    return MiniGLDispatch->MGLResizeContext((*MiniGLDispatch->currentContext), width, height);
}
#undef mglResizeContext
#define mglResizeContext MGLD_mglResizeContext

static __inline__ void MGLD_mglSetPointer(void)
{
    MiniGLDispatch->MGLSetPointer((*MiniGLDispatch->currentContext));
}
#undef mglSetPointer
#define mglSetPointer MGLD_mglSetPointer

static __inline__ void MGLD_mglSetZOffset(GLfloat offset)
{
    MiniGLDispatch->MGLSetZOffset((*MiniGLDispatch->currentContext), offset);
}
#undef mglSetZOffset
#define mglSetZOffset MGLD_mglSetZOffset

static __inline__ void MGLD_mglSpecialFunc(SpecialHandlerFn s)
{
    MiniGLDispatch->MGLSpecialFunc((*MiniGLDispatch->currentContext), s);
}
#undef mglSpecialFunc
#define mglSpecialFunc MGLD_mglSpecialFunc

static __inline__ void MGLD_mglSwitchDisplay(void)
{
    MiniGLDispatch->MGLSwitchDisplay((*MiniGLDispatch->currentContext));
}
#undef mglSwitchDisplay
#define mglSwitchDisplay MGLD_mglSwitchDisplay

static __inline__ void MGLD_mglTexMemStat(GLint *Current, GLint *Peak)
{
    MiniGLDispatch->MGLTexMemStat((*MiniGLDispatch->currentContext), Current, Peak);
}
#undef mglTexMemStat
#define mglTexMemStat MGLD_mglTexMemStat

static __inline__ void MGLD_mglUnlockDisplay(void)
{
    MiniGLDispatch->MGLUnlockDisplay((*MiniGLDispatch->currentContext));
}
#undef mglUnlockDisplay
#define mglUnlockDisplay MGLD_mglUnlockDisplay

static __inline__ void MGLD_mglWriteShotPPM(char *filename)
{
    MiniGLDispatch->MGLWriteShotPPM((*MiniGLDispatch->currentContext), filename);
}
#undef mglWriteShotPPM
#define mglWriteShotPPM MGLD_mglWriteShotPPM

static __inline__ void MGLD_mglChooseNumberOfBuffers(int number)
{
    MiniGLDispatch->mglChooseNumberOfBuffers(number);
}
#undef mglChooseNumberOfBuffers
#define mglChooseNumberOfBuffers MGLD_mglChooseNumberOfBuffers

static __inline__ void MGLD_mglChooseVertexBufferSize(int size)
{
    MiniGLDispatch->mglChooseVertexBufferSize(size);
}
#undef mglChooseVertexBufferSize
#define mglChooseVertexBufferSize MGLD_mglChooseVertexBufferSize

static __inline__ void MGLD_mglChooseWindowMode(GLboolean flag)
{
    MiniGLDispatch->mglChooseWindowMode(flag);
}
#undef mglChooseWindowMode
#define mglChooseWindowMode MGLD_mglChooseWindowMode

static __inline__ void MGLD_mglProposeCloseDesktop(GLboolean closeme)
{
    MiniGLDispatch->mglProposeCloseDesktop(closeme);
}
#undef mglProposeCloseDesktop
#define mglProposeCloseDesktop MGLD_mglProposeCloseDesktop

static __inline__ void MGLD_mglChooseGuardBand(GLboolean flag)
{
    MiniGLDispatch->mglChooseGuardBand(flag);
}
#undef mglChooseGuardBand
#define mglChooseGuardBand MGLD_mglChooseGuardBand

static __inline__ void MGLD_mglChooseMtexBufferSize(int size)
{
    MiniGLDispatch->mglChooseMtexBufferSize(size);
}
#undef mglChooseMtexBufferSize
#define mglChooseMtexBufferSize MGLD_mglChooseMtexBufferSize

static __inline__ void MGLD_mglChooseTextureBufferSize(int size)
{
    MiniGLDispatch->mglChooseTextureBufferSize(size);
}
#undef mglChooseTextureBufferSize
#define mglChooseTextureBufferSize MGLD_mglChooseTextureBufferSize

static __inline__ void MGLD_mglProhibitAlphaFallback(GLboolean flag)
{
    MiniGLDispatch->mglProhibitAlphaFallback(flag);
}
#undef mglProhibitAlphaFallback
#define mglProhibitAlphaFallback MGLD_mglProhibitAlphaFallback

static __inline__ void MGLD_mglProhibitMipMapping(GLboolean flag)
{
    MiniGLDispatch->mglProhibitMipMapping(flag);
}
#undef mglProhibitMipMapping
#define mglProhibitMipMapping MGLD_mglProhibitMipMapping

static __inline__ GLint MGLD_mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn)
{
    return MiniGLDispatch->mglGetSupportedScreenModes(CallbackFn);
}
#undef mglGetSupportedScreenModes
#define mglGetSupportedScreenModes MGLD_mglGetSupportedScreenModes

static __inline__ void *MGLD_mglGetInputWindowHandle(void)
{
    return MiniGLDispatch->MGLGetInputWindowHandle((*MiniGLDispatch->currentContext));
}
#undef mglGetInputWindowHandle
#define mglGetInputWindowHandle MGLD_mglGetInputWindowHandle

static __inline__ void MGLD_glClientActiveTextureARB(GLenum unit)
{
    MiniGLDispatch->GLClientActiveTextureARB((*MiniGLDispatch->currentContext), unit);
}
#undef glClientActiveTextureARB
#define glClientActiveTextureARB MGLD_glClientActiveTextureARB

static __inline__ void MGLD_glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLInterleavedArrays((*MiniGLDispatch->currentContext), format, stride, pointer);
}
#undef glInterleavedArrays
#define glInterleavedArrays MGLD_glInterleavedArrays

static __inline__ void MGLD_glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    MiniGLDispatch->GLMultiDrawArrays((*MiniGLDispatch->currentContext), mode, first, count, primcount);
}
#undef glMultiDrawArrays
#define glMultiDrawArrays MGLD_glMultiDrawArrays

static __inline__ GLboolean MGLD_glIsTexture(GLuint texture)
{
    return MiniGLDispatch->GLIsTexture((*MiniGLDispatch->currentContext), texture);
}
#undef glIsTexture
#define glIsTexture MGLD_glIsTexture

static __inline__ void MGLD_mglChooseZBufferDepth(int bits)
{
    MiniGLDispatch->mglChooseZBufferDepth(bits);
}
#undef mglChooseZBufferDepth
#define mglChooseZBufferDepth MGLD_mglChooseZBufferDepth

static __inline__ void MGLD_glBlendEquation(GLenum mode)
{
    MiniGLDispatch->GLBlendEquation((*MiniGLDispatch->currentContext), mode);
}
#undef glBlendEquation
#define glBlendEquation MGLD_glBlendEquation

static __inline__ void MGLD_glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    MiniGLDispatch->GLBlendFuncSeparate((*MiniGLDispatch->currentContext), srcRGB, dstRGB, srcAlpha, dstAlpha);
}
#undef glBlendFuncSeparate
#define glBlendFuncSeparate MGLD_glBlendFuncSeparate

static __inline__ GLboolean MGLD_glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
    return MiniGLDispatch->GLAreTexturesResident((*MiniGLDispatch->currentContext), n, textures, residences);
}
#undef glAreTexturesResident
#define glAreTexturesResident MGLD_glAreTexturesResident

static __inline__ void MGLD_glEdgeFlag(GLboolean flag)
{
    MiniGLDispatch->GLEdgeFlag((*MiniGLDispatch->currentContext), flag);
}
#undef glEdgeFlag
#define glEdgeFlag MGLD_glEdgeFlag

static __inline__ void MGLD_glEdgeFlagPointer(GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLEdgeFlagPointer((*MiniGLDispatch->currentContext), stride, pointer);
}
#undef glEdgeFlagPointer
#define glEdgeFlagPointer MGLD_glEdgeFlagPointer

static __inline__ void MGLD_glEdgeFlagv(const GLboolean *flag)
{
    MiniGLDispatch->GLEdgeFlagv((*MiniGLDispatch->currentContext), flag);
}
#undef glEdgeFlagv
#define glEdgeFlagv MGLD_glEdgeFlagv

static __inline__ void MGLD_glGetDoublev(GLenum pname, GLdouble *params)
{
    MiniGLDispatch->GLGetDoublev((*MiniGLDispatch->currentContext), pname, params);
}
#undef glGetDoublev
#define glGetDoublev MGLD_glGetDoublev

static __inline__ void MGLD_glGetPointerv(GLenum pname, GLvoid **params)
{
    MiniGLDispatch->GLGetPointerv((*MiniGLDispatch->currentContext), pname, params);
}
#undef glGetPointerv
#define glGetPointerv MGLD_glGetPointerv

static __inline__ void MGLD_glIndexi(GLint c)
{
    MiniGLDispatch->GLIndexi((*MiniGLDispatch->currentContext), c);
}
#undef glIndexi
#define glIndexi MGLD_glIndexi

static __inline__ void MGLD_glIndexiv(const GLint *c)
{
    MiniGLDispatch->GLIndexiv((*MiniGLDispatch->currentContext), c);
}
#undef glIndexiv
#define glIndexiv MGLD_glIndexiv

static __inline__ void MGLD_glIndexPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLIndexPointer((*MiniGLDispatch->currentContext), type, stride, pointer);
}
#undef glIndexPointer
#define glIndexPointer MGLD_glIndexPointer

static __inline__ void MGLD_glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    MiniGLDispatch->GLPrioritizeTextures((*MiniGLDispatch->currentContext), n, textures, priorities);
}
#undef glPrioritizeTextures
#define glPrioritizeTextures MGLD_glPrioritizeTextures

static __inline__ void MGLD_glReadBuffer(GLenum mode)
{
    MiniGLDispatch->GLReadBuffer((*MiniGLDispatch->currentContext), mode);
}
#undef glReadBuffer
#define glReadBuffer MGLD_glReadBuffer

static __inline__ void MGLD_glLightf(GLenum light, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLLightf((*MiniGLDispatch->currentContext), light, pname, param);
}
#undef glLightf
#define glLightf MGLD_glLightf

static __inline__ void MGLD_glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLLightfv((*MiniGLDispatch->currentContext), light, pname, params);
}
#undef glLightfv
#define glLightfv MGLD_glLightfv

static __inline__ void MGLD_glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLMaterialf((*MiniGLDispatch->currentContext), face, pname, param);
}
#undef glMaterialf
#define glMaterialf MGLD_glMaterialf

static __inline__ void MGLD_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLMaterialfv((*MiniGLDispatch->currentContext), face, pname, params);
}
#undef glMaterialfv
#define glMaterialfv MGLD_glMaterialfv

static __inline__ void MGLD_glLightModelf(GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLLightModelf((*MiniGLDispatch->currentContext), pname, param);
}
#undef glLightModelf
#define glLightModelf MGLD_glLightModelf

static __inline__ void MGLD_glLightModelfv(GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLLightModelfv((*MiniGLDispatch->currentContext), pname, params);
}
#undef glLightModelfv
#define glLightModelfv MGLD_glLightModelfv

static __inline__ void MGLD_glGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    MiniGLDispatch->GLGetLightfv((*MiniGLDispatch->currentContext), light, pname, params);
}
#undef glGetLightfv
#define glGetLightfv MGLD_glGetLightfv

static __inline__ void MGLD_glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    MiniGLDispatch->GLGetMaterialfv((*MiniGLDispatch->currentContext), face, pname, params);
}
#undef glGetMaterialfv
#define glGetMaterialfv MGLD_glGetMaterialfv

static __inline__ GLuint MGLD_glGenLists(GLsizei range)
{
    return MiniGLDispatch->GLGenLists((*MiniGLDispatch->currentContext), range);
}
#undef glGenLists
#define glGenLists MGLD_glGenLists

static __inline__ void MGLD_glDeleteLists(GLuint list, GLsizei range)
{
    MiniGLDispatch->GLDeleteLists((*MiniGLDispatch->currentContext), list, range);
}
#undef glDeleteLists
#define glDeleteLists MGLD_glDeleteLists

static __inline__ GLboolean MGLD_glIsList(GLuint list)
{
    return MiniGLDispatch->GLIsList((*MiniGLDispatch->currentContext), list);
}
#undef glIsList
#define glIsList MGLD_glIsList

static __inline__ void MGLD_glNewList(GLuint list, GLenum mode)
{
    MiniGLDispatch->GLNewList((*MiniGLDispatch->currentContext), list, mode);
}
#undef glNewList
#define glNewList MGLD_glNewList

static __inline__ void MGLD_glEndList(void)
{
    MiniGLDispatch->GLEndList((*MiniGLDispatch->currentContext));
}
#undef glEndList
#define glEndList MGLD_glEndList

static __inline__ void MGLD_glCallList(GLuint list)
{
    MiniGLDispatch->GLCallList((*MiniGLDispatch->currentContext), list);
}
#undef glCallList
#define glCallList MGLD_glCallList

static __inline__ GLint MGLD_gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data)
{
    return MiniGLDispatch->GLUBuild2DMipmaps((*MiniGLDispatch->currentContext), target, internalFormat, width, height, format, type, data);
}
#undef gluBuild2DMipmaps
#define gluBuild2DMipmaps MGLD_gluBuild2DMipmaps

static __inline__ const GLubyte *MGLD_gluErrorString(GLenum errCode)
{
    return MiniGLDispatch->GLUErrorString(errCode);
}
#undef gluErrorString
#define gluErrorString MGLD_gluErrorString

static __inline__ GLUquadricObj *MGLD_gluNewQuadric(void)
{
    return MiniGLDispatch->GLUNewQuadric();
}
#undef gluNewQuadric
#define gluNewQuadric MGLD_gluNewQuadric

static __inline__ void MGLD_gluDeleteQuadric(GLUquadricObj *q)
{
    MiniGLDispatch->GLUDeleteQuadric(q);
}
#undef gluDeleteQuadric
#define gluDeleteQuadric MGLD_gluDeleteQuadric

static __inline__ void MGLD_gluQuadricNormals(GLUquadricObj *q, GLenum normals)
{
    MiniGLDispatch->GLUQuadricNormals(q, normals);
}
#undef gluQuadricNormals
#define gluQuadricNormals MGLD_gluQuadricNormals

static __inline__ void MGLD_gluQuadricTexture(GLUquadricObj *q, GLboolean textureCoords)
{
    MiniGLDispatch->GLUQuadricTexture(q, textureCoords);
}
#undef gluQuadricTexture
#define gluQuadricTexture MGLD_gluQuadricTexture

static __inline__ void MGLD_gluQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle)
{
    MiniGLDispatch->GLUQuadricDrawStyle(q, drawStyle);
}
#undef gluQuadricDrawStyle
#define gluQuadricDrawStyle MGLD_gluQuadricDrawStyle

static __inline__ void MGLD_gluQuadricOrientation(GLUquadricObj *q, GLenum orientation)
{
    MiniGLDispatch->GLUQuadricOrientation(q, orientation);
}
#undef gluQuadricOrientation
#define gluQuadricOrientation MGLD_gluQuadricOrientation

static __inline__ void MGLD_gluCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUCylinder(q, base, top, height, slices, stacks);
}
#undef gluCylinder
#define gluCylinder MGLD_gluCylinder

static __inline__ void MGLD_gluSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUSphere(q, radius, slices, stacks);
}
#undef gluSphere
#define gluSphere MGLD_gluSphere

static __inline__ void MGLD_gluDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUDisk(q, inner, outer, slices, loops);
}
#undef gluDisk
#define gluDisk MGLD_gluDisk

static __inline__ void MGLD_glutInitDisplayMode(unsigned int mode)
{
    MiniGLDispatch->GLUTInitDisplayMode(mode);
}
#undef glutInitDisplayMode
#define glutInitDisplayMode MGLD_glutInitDisplayMode

static __inline__ void MGLD_glutInitWindowSize(int width, int height)
{
    MiniGLDispatch->GLUTInitWindowSize(width, height);
}
#undef glutInitWindowSize
#define glutInitWindowSize MGLD_glutInitWindowSize

static __inline__ void MGLD_glutInitWindowPosition(int x, int y)
{
    MiniGLDispatch->GLUTInitWindowPosition(x, y);
}
#undef glutInitWindowPosition
#define glutInitWindowPosition MGLD_glutInitWindowPosition

static __inline__ int MGLD_glutCreateWindow(const char *title)
{
    return MiniGLDispatch->GLUTCreateWindow(title);
}
#undef glutCreateWindow
#define glutCreateWindow MGLD_glutCreateWindow

static __inline__ void MGLD_glutMainLoop(void)
{
    MiniGLDispatch->GLUTMainLoop();
}
#undef glutMainLoop
#define glutMainLoop MGLD_glutMainLoop

static __inline__ void MGLD_glutDisplayFunc(void (*func)(void))
{
    MiniGLDispatch->GLUTDisplayFunc(func);
}
#undef glutDisplayFunc
#define glutDisplayFunc MGLD_glutDisplayFunc

static __inline__ void MGLD_glutIdleFunc(void (*func)(void))
{
    MiniGLDispatch->GLUTIdleFunc(func);
}
#undef glutIdleFunc
#define glutIdleFunc MGLD_glutIdleFunc

static __inline__ void MGLD_glutKeyboardFunc(void (*func)(unsigned char key, int x, int y))
{
    MiniGLDispatch->GLUTKeyboardFunc(func);
}
#undef glutKeyboardFunc
#define glutKeyboardFunc MGLD_glutKeyboardFunc

static __inline__ void MGLD_glutReshapeFunc(void (*func)(int width, int height))
{
    MiniGLDispatch->GLUTReshapeFunc(func);
}
#undef glutReshapeFunc
#define glutReshapeFunc MGLD_glutReshapeFunc

static __inline__ void MGLD_glutSwapBuffers(void)
{
    MiniGLDispatch->GLUTSwapBuffers();
}
#undef glutSwapBuffers
#define glutSwapBuffers MGLD_glutSwapBuffers

static __inline__ void MGLD_glutPostRedisplay(void)
{
    MiniGLDispatch->GLUTPostRedisplay();
}
#undef glutPostRedisplay
#define glutPostRedisplay MGLD_glutPostRedisplay

static __inline__ int MGLD_glutGet(GLenum state)
{
    return MiniGLDispatch->GLUTGet(state);
}
#undef glutGet
#define glutGet MGLD_glutGet

static __inline__ void MGLD_glutGameModeString(const char *string)
{
    MiniGLDispatch->GLUTGameModeString(string);
}
#undef glutGameModeString
#define glutGameModeString MGLD_glutGameModeString

static __inline__ int MGLD_glutEnterGameMode(void)
{
    return MiniGLDispatch->GLUTEnterGameMode();
}
#undef glutEnterGameMode
#define glutEnterGameMode MGLD_glutEnterGameMode

static __inline__ void MGLD_glutLeaveGameMode(void)
{
    MiniGLDispatch->GLUTLeaveGameMode();
}
#undef glutLeaveGameMode
#define glutLeaveGameMode MGLD_glutLeaveGameMode

static __inline__ int MGLD_glutGameModeGet(GLenum query)
{
    return MiniGLDispatch->GLUTGameModeGet(query);
}
#undef glutGameModeGet
#define glutGameModeGet MGLD_glutGameModeGet

static __inline__ void MGLD_glutSolidCube(GLdouble size)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidCube(size);
}
#undef glutSolidCube
#define glutSolidCube MGLD_glutSolidCube

static __inline__ void MGLD_glutSolidSphere(GLdouble radius, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidSphere(radius, slices, stacks);
}
#undef glutSolidSphere
#define glutSolidSphere MGLD_glutSolidSphere

static __inline__ void MGLD_glutSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidCone(base, height, slices, stacks);
}
#undef glutSolidCone
#define glutSolidCone MGLD_glutSolidCone

static __inline__ void MGLD_glutSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidTorus(innerRadius, outerRadius, sides, rings);
}
#undef glutSolidTorus
#define glutSolidTorus MGLD_glutSolidTorus

static __inline__ void MGLD_glutSolidDodecahedron(void)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidDodecahedron();
}
#undef glutSolidDodecahedron
#define glutSolidDodecahedron MGLD_glutSolidDodecahedron

static __inline__ void MGLD_mglChoosePixelDepth(int depth)
{
    MiniGLDispatch->mglChoosePixelDepth(depth);
}
#undef mglChoosePixelDepth
#define mglChoosePixelDepth MGLD_mglChoosePixelDepth

static __inline__ void MGLD_mglDeleteContext(void)
{
    if ((*MiniGLDispatch->currentContext)) {
        GLcontext context = (*MiniGLDispatch->currentContext);
        (*MiniGLDispatch->currentContext) = (GLcontext)0;
        MiniGLDispatch->MGLDeleteContext(context);
    }
}
#undef mglDeleteContext
#define mglDeleteContext MGLD_mglDeleteContext

static __inline__ void MGLD_glutInit(int *argcp, char **argv)
{
    atexit(mgl_glut_release);
    MiniGLDispatch->GLUTInit(argcp, argv);
}
#undef glutInit
#define glutInit MGLD_glutInit

#endif /* MINIGL_LIBRARY_BUILD */

#ifdef __cplusplus
}
#endif

#endif

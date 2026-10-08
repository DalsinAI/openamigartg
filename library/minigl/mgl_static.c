/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Made by library/minigl/gen_api.py from library/minigl/minigl_api.txt. */
/* libmgl.a: every call as a function, for programs written for the
 * linked-in MiniGL. They open minigl.library with MGLInit() and close it with
 * MGLTerm(), and are otherwise unchanged. */
#define MINIGL_LIBRARY_BUILD
#include <proto/minigl.h>

GLboolean MGLInit(void) { return MiniGLOpen() ? GL_TRUE : GL_FALSE; }
void MGLTerm(void) { MiniGLClose(); }

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

void glActiveTextureARB(GLenum unit)
{
    MiniGLDispatch->GLActiveTextureARB((*MiniGLDispatch->currentContext), unit);
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    MiniGLDispatch->GLAlphaFunc((*MiniGLDispatch->currentContext), func, ref);
}

void glArrayElement(GLint i)
{
    MiniGLDispatch->GLArrayElement((*MiniGLDispatch->currentContext), i);
}

void glBegin(GLenum mode)
{
    MiniGLDispatch->GLBegin((*MiniGLDispatch->currentContext), mode);
}

void glBindTexture(GLenum target, GLuint texture)
{
    MiniGLDispatch->GLBindTexture((*MiniGLDispatch->currentContext), target, texture);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    MiniGLDispatch->GLBlendFunc((*MiniGLDispatch->currentContext), sfactor, dfactor);
}

void glClear(GLbitfield mask)
{
    MiniGLDispatch->GLClear((*MiniGLDispatch->currentContext), mask);
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    MiniGLDispatch->GLClearColor((*MiniGLDispatch->currentContext), red, green, blue, alpha);
}

void glClearDepth(GLclampd depth)
{
    MiniGLDispatch->GLClearDepth((*MiniGLDispatch->currentContext), depth);
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    MiniGLDispatch->GLColor4f((*MiniGLDispatch->currentContext), red, green, blue, 1.0f);
}

void glColor3fv(GLfloat *v)
{
    MiniGLDispatch->GLColor3fv((*MiniGLDispatch->currentContext), v);
}

void glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    MiniGLDispatch->GLColor4ub((*MiniGLDispatch->currentContext), red, green, blue, 255);
}

void glColor3ubv(GLubyte *v)
{
    MiniGLDispatch->GLColor3ubv((*MiniGLDispatch->currentContext), v);
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    MiniGLDispatch->GLColor4f((*MiniGLDispatch->currentContext), red, green, blue, alpha);
}

void glColor4fv(GLfloat *v)
{
    MiniGLDispatch->GLColor4fv((*MiniGLDispatch->currentContext), v);
}

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    MiniGLDispatch->GLColor4ub((*MiniGLDispatch->currentContext), red, green, blue, alpha);
}

void glColor4ubv(GLubyte *v)
{
    MiniGLDispatch->GLColor4ubv((*MiniGLDispatch->currentContext), v);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    MiniGLDispatch->GLColorMask((*MiniGLDispatch->currentContext), red, green, blue, alpha);
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLColorPointer((*MiniGLDispatch->currentContext), size, type, stride, pointer);
}

void glColorTable(GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
    MiniGLDispatch->GLColorTable((*MiniGLDispatch->currentContext), target, internalformat, width, format, type, data);
}

void glColorTableEXT(GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
{
    MiniGLDispatch->GLColorTable((*MiniGLDispatch->currentContext), target, internalformat, width, format, type, data);
}

void glCullFace(GLenum mode)
{
    MiniGLDispatch->GLCullFace((*MiniGLDispatch->currentContext), mode);
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
    MiniGLDispatch->GLDeleteTextures((*MiniGLDispatch->currentContext), n, textures);
}

void glDepthFunc(GLenum func)
{
    MiniGLDispatch->GLDepthFunc((*MiniGLDispatch->currentContext), func);
}

void glDepthMask(GLboolean flag)
{
    MiniGLDispatch->GLDepthMask((*MiniGLDispatch->currentContext), flag);
}

void glDepthRange(GLclampd n, GLclampd f)
{
    MiniGLDispatch->GLDepthRange((*MiniGLDispatch->currentContext), n, f);
}

void glDisable(GLenum cap)
{
    MiniGLDispatch->MGLSetState((*MiniGLDispatch->currentContext), cap, GL_FALSE);
}

void glDisableClientState(GLenum cap)
{
    MiniGLDispatch->GLDisableClientState((*MiniGLDispatch->currentContext), cap);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    MiniGLDispatch->GLDrawArrays((*MiniGLDispatch->currentContext), mode, first, count);
}

void glDrawBuffer(GLenum mode)
{
    MiniGLDispatch->GLDrawBuffer((*MiniGLDispatch->currentContext), mode);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *pointer)
{
    MiniGLDispatch->GLDrawElements((*MiniGLDispatch->currentContext), mode, count, type, pointer);
}

void glEnable(GLenum cap)
{
    MiniGLDispatch->MGLSetState((*MiniGLDispatch->currentContext), cap, GL_TRUE);
}

void glEnableClientState(GLenum cap)
{
    MiniGLDispatch->GLEnableClientState((*MiniGLDispatch->currentContext), cap);
}

void glEnd(void)
{
    MiniGLDispatch->GLEnd((*MiniGLDispatch->currentContext));
}

void glFinish(void)
{
    MiniGLDispatch->GLFinish((*MiniGLDispatch->currentContext));
}

void glFlush(void)
{
    MiniGLDispatch->GLFlush((*MiniGLDispatch->currentContext));
}

void glFogf(GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLFogf((*MiniGLDispatch->currentContext), pname, param);
}

void glFogfv(GLenum pname, GLfloat *param)
{
    MiniGLDispatch->GLFogfv((*MiniGLDispatch->currentContext), pname, param);
}

void glFogi(GLenum pname, GLint param)
{
    MiniGLDispatch->GLFogf((*MiniGLDispatch->currentContext), pname, (GLfloat)param);
}

void glFrontFace(GLenum mode)
{
    MiniGLDispatch->GLFrontFace((*MiniGLDispatch->currentContext), mode);
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    MiniGLDispatch->GLFrustum((*MiniGLDispatch->currentContext), left, right, bottom, top, zNear, zFar);
}

void glGenTextures(GLsizei n, GLuint *textures)
{
    MiniGLDispatch->GLGenTextures((*MiniGLDispatch->currentContext), n, textures);
}

void glGetBooleanv(GLenum pname, GLboolean *params)
{
    MiniGLDispatch->GLGetBooleanv((*MiniGLDispatch->currentContext), pname, params);
}

GLenum glGetError(void)
{
    return MiniGLDispatch->GLGetError((*MiniGLDispatch->currentContext));
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
    MiniGLDispatch->GLGetFloatv((*MiniGLDispatch->currentContext), pname, params);
}

void glGetIntegerv(GLenum pname, GLint *params)
{
    MiniGLDispatch->GLGetIntegerv((*MiniGLDispatch->currentContext), pname, params);
}

const GLubyte *glGetString(GLenum name)
{
    return MiniGLDispatch->GLGetString((*MiniGLDispatch->currentContext), name);
}

void glHint(GLenum target, GLenum mode)
{
    MiniGLDispatch->GLHint((*MiniGLDispatch->currentContext), target, mode);
}

GLboolean glIsEnabled(GLenum cap)
{
    return MiniGLDispatch->GLIsEnabled((*MiniGLDispatch->currentContext), cap);
}

void glLoadIdentity(void)
{
    MiniGLDispatch->GLLoadIdentity((*MiniGLDispatch->currentContext));
}

void glLoadMatrixd(const GLdouble *m)
{
    MiniGLDispatch->GLLoadMatrixd((*MiniGLDispatch->currentContext), m);
}

void glLoadMatrixf(const GLfloat *m)
{
    MiniGLDispatch->GLLoadMatrixf((*MiniGLDispatch->currentContext), m);
}

void glLockArrays(GLuint first, GLsizei count)
{
    MiniGLDispatch->GLLockArrays((*MiniGLDispatch->currentContext), first, count);
}

void glMatrixMode(GLenum mode)
{
    MiniGLDispatch->GLMatrixMode((*MiniGLDispatch->currentContext), mode);
}

void glMultiTexCoord2fARB(GLenum unit, GLfloat s, GLfloat t)
{
    MiniGLDispatch->GLMultiTexCoord2fARB((*MiniGLDispatch->currentContext), unit, s, t);
}

void glMultiTexCoord2fvARB(GLenum unit, GLfloat *v)
{
    MiniGLDispatch->GLMultiTexCoord2fvARB((*MiniGLDispatch->currentContext), unit, v);
}

void glMultMatrixd(const GLdouble *m)
{
    MiniGLDispatch->GLMultMatrixd((*MiniGLDispatch->currentContext), m);
}

void glMultMatrixf(const GLfloat *m)
{
    MiniGLDispatch->GLMultMatrixf((*MiniGLDispatch->currentContext), m);
}

void glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLNormal3f((*MiniGLDispatch->currentContext), x, y, z);
}

void glNormal3fv(GLfloat *v)
{
    MiniGLDispatch->GLNormal3fv((*MiniGLDispatch->currentContext), v);
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    MiniGLDispatch->GLOrtho((*MiniGLDispatch->currentContext), left, right, bottom, top, zNear, zFar);
}

void glPixelStorei(GLenum pname, GLint param)
{
    MiniGLDispatch->GLPixelStorei((*MiniGLDispatch->currentContext), pname, param);
}

void glPointSize(GLfloat s)
{
    MiniGLDispatch->GLPointSize((*MiniGLDispatch->currentContext), s);
}

void glLineWidth(GLfloat w)
{
    MiniGLDispatch->GLLineWidth((*MiniGLDispatch->currentContext), w);
}

void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLTexGenfv((*MiniGLDispatch->currentContext), coord, pname, params);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    MiniGLDispatch->GLCopyTexImage2D((*MiniGLDispatch->currentContext), target, level, internalformat, x, y, width, height, border);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    MiniGLDispatch->GLCopyTexSubImage2D((*MiniGLDispatch->currentContext), target, level, xoffset, yoffset, x, y, width, height);
}

void glPolygonMode(GLenum face, GLenum mode)
{
    MiniGLDispatch->GLPolygonMode((*MiniGLDispatch->currentContext), face, mode);
}

void glPolygonOffset(GLfloat factor, GLfloat units)
{
    MiniGLDispatch->GLPolygonOffset((*MiniGLDispatch->currentContext), factor, units);
}

void glPopMatrix(void)
{
    MiniGLDispatch->GLPopMatrix((*MiniGLDispatch->currentContext));
}

void glPushMatrix(void)
{
    MiniGLDispatch->GLPushMatrix((*MiniGLDispatch->currentContext));
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    MiniGLDispatch->GLReadPixels((*MiniGLDispatch->currentContext), x, y, width, height, format, type, pixels);
}

void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    MiniGLDispatch->GLRotated((*MiniGLDispatch->currentContext), angle, x, y, z);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLRotatef((*MiniGLDispatch->currentContext), angle, x, y, z);
}

void glRotatefEXT(GLfloat angle, const GLint xyz)
{
    MiniGLDispatch->GLRotatefEXT((*MiniGLDispatch->currentContext), angle, xyz);
}

void glRotatefEXTs(GLfloat sin_an, GLfloat cos_an, const GLint xyz)
{
    MiniGLDispatch->GLRotatefEXTs((*MiniGLDispatch->currentContext), sin_an, cos_an, xyz);
}

void glScaled(GLdouble x, GLdouble y, GLdouble z)
{
    MiniGLDispatch->GLScaled((*MiniGLDispatch->currentContext), x, y, z);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLScalef((*MiniGLDispatch->currentContext), x, y, z);
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    MiniGLDispatch->GLScissor((*MiniGLDispatch->currentContext), x, y, width, height);
}

void glShadeModel(GLenum mode)
{
    MiniGLDispatch->GLShadeModel((*MiniGLDispatch->currentContext), mode);
}

void glTexCoord2f(GLfloat s, GLfloat t)
{
    MiniGLDispatch->GLTexCoord2f((*MiniGLDispatch->currentContext), s, t);
}

void glTexCoord2fv(GLfloat *v)
{
    MiniGLDispatch->GLTexCoord2fv((*MiniGLDispatch->currentContext), v);
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    MiniGLDispatch->GLTexCoord4f((*MiniGLDispatch->currentContext), s, t, r, q);
}

void glTexCoord4fv(GLfloat *v)
{
    MiniGLDispatch->GLTexCoord4fv((*MiniGLDispatch->currentContext), v);
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLTexCoordPointer((*MiniGLDispatch->currentContext), size, type, stride, pointer);
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLTexEnvi((*MiniGLDispatch->currentContext), target, pname, (GLint)param);
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    MiniGLDispatch->GLTexEnvi((*MiniGLDispatch->currentContext), target, pname, param);
}

void glTexEnviv(GLenum target, GLenum pname, GLint *param)
{
    MiniGLDispatch->GLTexEnvi((*MiniGLDispatch->currentContext), target, pname, *param);
}

void glTexGeni(GLenum coord, GLenum mode, GLenum map)
{
    MiniGLDispatch->GLTexGeni((*MiniGLDispatch->currentContext), coord, mode, map);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    MiniGLDispatch->GLTexImage2D((*MiniGLDispatch->currentContext), target, level, internalformat, width, height, border, format, type, pixels);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLTexParameteri((*MiniGLDispatch->currentContext), target, pname, (GLint)param);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    MiniGLDispatch->GLTexParameteri((*MiniGLDispatch->currentContext), target, pname, param);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    MiniGLDispatch->GLTexSubImage2D((*MiniGLDispatch->currentContext), target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    MiniGLDispatch->GLTranslated((*MiniGLDispatch->currentContext), x, y, z);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLTranslatef((*MiniGLDispatch->currentContext), x, y, z);
}

void gluLookAt(GLfloat ex, GLfloat ey, GLfloat ez, GLfloat cx, GLfloat cy, GLfloat cz, GLfloat ux, GLfloat uy, GLfloat uz)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLULookAt(ex, ey, ez, cx, cy, cz, ux, uy, uz);
}

void glUnlockArrays(void)
{
    MiniGLDispatch->GLUnlockArrays((*MiniGLDispatch->currentContext));
}

void gluPerspective(GLfloat fovy, GLfloat aspect, GLfloat znear, GLfloat zfar)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUPerspective(fovy, aspect, znear, zfar);
}

void glVertex2f(GLfloat x, GLfloat y)
{
    MiniGLDispatch->GLVertex4f((*MiniGLDispatch->currentContext), x, y, 0.0f, 1.0f);
}

void glVertex2fv(GLfloat *v)
{
    MiniGLDispatch->GLVertex2fv((*MiniGLDispatch->currentContext), v);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    MiniGLDispatch->GLVertex4f((*MiniGLDispatch->currentContext), x, y, z, 1.0f);
}

void glVertex3fv(GLfloat *v)
{
    MiniGLDispatch->GLVertex3fv((*MiniGLDispatch->currentContext), v);
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    MiniGLDispatch->GLVertex4f((*MiniGLDispatch->currentContext), x, y, z, w);
}

void glVertex4fv(GLfloat *v)
{
    MiniGLDispatch->GLVertex4fv((*MiniGLDispatch->currentContext), v);
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLVertexPointer((*MiniGLDispatch->currentContext), size, type, stride, pointer);
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLNormalPointer((*MiniGLDispatch->currentContext), type, stride, pointer);
}

void gluQuadricCallback(GLUquadricObj *q, GLenum which, MGLUfuncptr fn)
{
    MiniGLDispatch->GLUQuadricCallback(q, which, fn);
}

void glColorMaterial(GLenum face, GLenum mode)
{
    MiniGLDispatch->GLColorMaterial((*MiniGLDispatch->currentContext), face, mode);
}

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLTexEnvfv((*MiniGLDispatch->currentContext), target, pname, params);
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    MiniGLDispatch->GLViewport((*MiniGLDispatch->currentContext), x, y, width, height);
}

void mglClearPointer(void)
{
    MiniGLDispatch->MGLClearPointer((*MiniGLDispatch->currentContext));
}

void *mglCreateContext(int offx, int offy, int w, int h)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContext(offx, offy, w, h);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}

void *mglCreateContextFromID(GLint id, GLint *w, GLint *h)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContextFromID(id, w, h);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}

void *mglCreateContextFromWindow(struct Window *window)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContextFromWindow(window);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}

void *mglCreateContextFromBitMap(struct BitMap *bitmap)
{
    GLcontext context = (GLcontext)MiniGLDispatch->MGLCreateContextFromBitMap(bitmap);
    if (context) (*MiniGLDispatch->currentContext) = context;
    return (void *)context;
}

void mglDrawMultitexBuffer(GLenum s, GLenum d, GLenum env)
{
    MiniGLDispatch->MGLDrawMultitexBuffer((*MiniGLDispatch->currentContext), s, d, env);
}

void mglEnableSync(GLboolean enable)
{
    MiniGLDispatch->MGLEnableSync((*MiniGLDispatch->currentContext), enable);
}

void mglExit(void)
{
    MiniGLDispatch->MGLExit((*MiniGLDispatch->currentContext));
}

void *mglGetWindowHandle(void)
{
    return MiniGLDispatch->MGLGetWindowHandle((*MiniGLDispatch->currentContext));
}

void mglIdleFunc(IdleFn i)
{
    MiniGLDispatch->MGLIdleFunc((*MiniGLDispatch->currentContext), i);
}

void mglKeyFunc(KeyHandlerFn k)
{
    MiniGLDispatch->MGLKeyFunc((*MiniGLDispatch->currentContext), k);
}

GLboolean mglLockBack(MGLLockInfo *info)
{
    return MiniGLDispatch->MGLLockBack((*MiniGLDispatch->currentContext), info);
}

GLboolean mglLockDisplay(void)
{
    return MiniGLDispatch->MGLLockDisplay((*MiniGLDispatch->currentContext));
}

void mglLockMode(GLenum lockMode)
{
    MiniGLDispatch->MGLLockMode((*MiniGLDispatch->currentContext), lockMode);
}

void mglMainLoop(void)
{
    MiniGLDispatch->MGLMainLoop((*MiniGLDispatch->currentContext));
}

void mglMinTriArea(GLfloat area)
{
    MiniGLDispatch->MGLMinTriArea((*MiniGLDispatch->currentContext), area);
}

void mglMouseFunc(MouseHandlerFn m)
{
    MiniGLDispatch->MGLMouseFunc((*MiniGLDispatch->currentContext), m);
}

void mglPrintMatrix(GLenum mode)
{
    MiniGLDispatch->MGLPrintMatrix((*MiniGLDispatch->currentContext), mode);
}

void mglPrintMatrixStack(GLenum mode)
{
    MiniGLDispatch->MGLPrintMatrixStack((*MiniGLDispatch->currentContext), mode);
}

GLboolean mglResizeContext(GLsizei width, GLsizei height)
{
    return MiniGLDispatch->MGLResizeContext((*MiniGLDispatch->currentContext), width, height);
}

void mglSetPointer(void)
{
    MiniGLDispatch->MGLSetPointer((*MiniGLDispatch->currentContext));
}

void mglSetZOffset(GLfloat offset)
{
    MiniGLDispatch->MGLSetZOffset((*MiniGLDispatch->currentContext), offset);
}

void mglSpecialFunc(SpecialHandlerFn s)
{
    MiniGLDispatch->MGLSpecialFunc((*MiniGLDispatch->currentContext), s);
}

void mglSwitchDisplay(void)
{
    MiniGLDispatch->MGLSwitchDisplay((*MiniGLDispatch->currentContext));
}

void mglTexMemStat(GLint *Current, GLint *Peak)
{
    MiniGLDispatch->MGLTexMemStat((*MiniGLDispatch->currentContext), Current, Peak);
}

void mglUnlockDisplay(void)
{
    MiniGLDispatch->MGLUnlockDisplay((*MiniGLDispatch->currentContext));
}

void mglWriteShotPPM(char *filename)
{
    MiniGLDispatch->MGLWriteShotPPM((*MiniGLDispatch->currentContext), filename);
}

void mglChooseNumberOfBuffers(int number)
{
    MiniGLDispatch->mglChooseNumberOfBuffers(number);
}

void mglChooseVertexBufferSize(int size)
{
    MiniGLDispatch->mglChooseVertexBufferSize(size);
}

void mglChooseWindowMode(GLboolean flag)
{
    MiniGLDispatch->mglChooseWindowMode(flag);
}

void mglProposeCloseDesktop(GLboolean closeme)
{
    MiniGLDispatch->mglProposeCloseDesktop(closeme);
}

void mglChooseGuardBand(GLboolean flag)
{
    MiniGLDispatch->mglChooseGuardBand(flag);
}

void mglChooseMtexBufferSize(int size)
{
    MiniGLDispatch->mglChooseMtexBufferSize(size);
}

void mglChooseTextureBufferSize(int size)
{
    MiniGLDispatch->mglChooseTextureBufferSize(size);
}

void mglProhibitAlphaFallback(GLboolean flag)
{
    MiniGLDispatch->mglProhibitAlphaFallback(flag);
}

void mglProhibitMipMapping(GLboolean flag)
{
    MiniGLDispatch->mglProhibitMipMapping(flag);
}

GLint mglGetSupportedScreenModes(MGLScreenModeCallback CallbackFn)
{
    return MiniGLDispatch->mglGetSupportedScreenModes(CallbackFn);
}

void *mglGetInputWindowHandle(void)
{
    return MiniGLDispatch->MGLGetInputWindowHandle((*MiniGLDispatch->currentContext));
}

void glClientActiveTextureARB(GLenum unit)
{
    MiniGLDispatch->GLClientActiveTextureARB((*MiniGLDispatch->currentContext), unit);
}

void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLInterleavedArrays((*MiniGLDispatch->currentContext), format, stride, pointer);
}

void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei primcount)
{
    MiniGLDispatch->GLMultiDrawArrays((*MiniGLDispatch->currentContext), mode, first, count, primcount);
}

GLboolean glIsTexture(GLuint texture)
{
    return MiniGLDispatch->GLIsTexture((*MiniGLDispatch->currentContext), texture);
}

void mglChooseZBufferDepth(int bits)
{
    MiniGLDispatch->mglChooseZBufferDepth(bits);
}

void glBlendEquation(GLenum mode)
{
    MiniGLDispatch->GLBlendEquation((*MiniGLDispatch->currentContext), mode);
}

void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    MiniGLDispatch->GLBlendFuncSeparate((*MiniGLDispatch->currentContext), srcRGB, dstRGB, srcAlpha, dstAlpha);
}

GLboolean glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
    return MiniGLDispatch->GLAreTexturesResident((*MiniGLDispatch->currentContext), n, textures, residences);
}

void glEdgeFlag(GLboolean flag)
{
    MiniGLDispatch->GLEdgeFlag((*MiniGLDispatch->currentContext), flag);
}

void glEdgeFlagPointer(GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLEdgeFlagPointer((*MiniGLDispatch->currentContext), stride, pointer);
}

void glEdgeFlagv(const GLboolean *flag)
{
    MiniGLDispatch->GLEdgeFlagv((*MiniGLDispatch->currentContext), flag);
}

void glGetDoublev(GLenum pname, GLdouble *params)
{
    MiniGLDispatch->GLGetDoublev((*MiniGLDispatch->currentContext), pname, params);
}

void glGetPointerv(GLenum pname, GLvoid **params)
{
    MiniGLDispatch->GLGetPointerv((*MiniGLDispatch->currentContext), pname, params);
}

void glIndexi(GLint c)
{
    MiniGLDispatch->GLIndexi((*MiniGLDispatch->currentContext), c);
}

void glIndexiv(const GLint *c)
{
    MiniGLDispatch->GLIndexiv((*MiniGLDispatch->currentContext), c);
}

void glIndexPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    MiniGLDispatch->GLIndexPointer((*MiniGLDispatch->currentContext), type, stride, pointer);
}

void glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    MiniGLDispatch->GLPrioritizeTextures((*MiniGLDispatch->currentContext), n, textures, priorities);
}

void glReadBuffer(GLenum mode)
{
    MiniGLDispatch->GLReadBuffer((*MiniGLDispatch->currentContext), mode);
}

void glLightf(GLenum light, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLLightf((*MiniGLDispatch->currentContext), light, pname, param);
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLLightfv((*MiniGLDispatch->currentContext), light, pname, params);
}

void glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLMaterialf((*MiniGLDispatch->currentContext), face, pname, param);
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLMaterialfv((*MiniGLDispatch->currentContext), face, pname, params);
}

void glLightModelf(GLenum pname, GLfloat param)
{
    MiniGLDispatch->GLLightModelf((*MiniGLDispatch->currentContext), pname, param);
}

void glLightModelfv(GLenum pname, const GLfloat *params)
{
    MiniGLDispatch->GLLightModelfv((*MiniGLDispatch->currentContext), pname, params);
}

void glGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    MiniGLDispatch->GLGetLightfv((*MiniGLDispatch->currentContext), light, pname, params);
}

void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    MiniGLDispatch->GLGetMaterialfv((*MiniGLDispatch->currentContext), face, pname, params);
}

GLuint glGenLists(GLsizei range)
{
    return MiniGLDispatch->GLGenLists((*MiniGLDispatch->currentContext), range);
}

void glDeleteLists(GLuint list, GLsizei range)
{
    MiniGLDispatch->GLDeleteLists((*MiniGLDispatch->currentContext), list, range);
}

GLboolean glIsList(GLuint list)
{
    return MiniGLDispatch->GLIsList((*MiniGLDispatch->currentContext), list);
}

void glNewList(GLuint list, GLenum mode)
{
    MiniGLDispatch->GLNewList((*MiniGLDispatch->currentContext), list, mode);
}

void glEndList(void)
{
    MiniGLDispatch->GLEndList((*MiniGLDispatch->currentContext));
}

void glCallList(GLuint list)
{
    MiniGLDispatch->GLCallList((*MiniGLDispatch->currentContext), list);
}

GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *data)
{
    return MiniGLDispatch->GLUBuild2DMipmaps((*MiniGLDispatch->currentContext), target, internalFormat, width, height, format, type, data);
}

const GLubyte *gluErrorString(GLenum errCode)
{
    return MiniGLDispatch->GLUErrorString(errCode);
}

GLUquadricObj *gluNewQuadric(void)
{
    return MiniGLDispatch->GLUNewQuadric();
}

void gluDeleteQuadric(GLUquadricObj *q)
{
    MiniGLDispatch->GLUDeleteQuadric(q);
}

void gluQuadricNormals(GLUquadricObj *q, GLenum normals)
{
    MiniGLDispatch->GLUQuadricNormals(q, normals);
}

void gluQuadricTexture(GLUquadricObj *q, GLboolean textureCoords)
{
    MiniGLDispatch->GLUQuadricTexture(q, textureCoords);
}

void gluQuadricDrawStyle(GLUquadricObj *q, GLenum drawStyle)
{
    MiniGLDispatch->GLUQuadricDrawStyle(q, drawStyle);
}

void gluQuadricOrientation(GLUquadricObj *q, GLenum orientation)
{
    MiniGLDispatch->GLUQuadricOrientation(q, orientation);
}

void gluCylinder(GLUquadricObj *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUCylinder(q, base, top, height, slices, stacks);
}

void gluSphere(GLUquadricObj *q, GLdouble radius, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUSphere(q, radius, slices, stacks);
}

void gluDisk(GLUquadricObj *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUDisk(q, inner, outer, slices, loops);
}

void glutInitDisplayMode(unsigned int mode)
{
    MiniGLDispatch->GLUTInitDisplayMode(mode);
}

void glutInitWindowSize(int width, int height)
{
    MiniGLDispatch->GLUTInitWindowSize(width, height);
}

void glutInitWindowPosition(int x, int y)
{
    MiniGLDispatch->GLUTInitWindowPosition(x, y);
}

int glutCreateWindow(const char *title)
{
    return MiniGLDispatch->GLUTCreateWindow(title);
}

void glutMainLoop(void)
{
    MiniGLDispatch->GLUTMainLoop();
}

void glutDisplayFunc(void (*func)(void))
{
    MiniGLDispatch->GLUTDisplayFunc(func);
}

void glutIdleFunc(void (*func)(void))
{
    MiniGLDispatch->GLUTIdleFunc(func);
}

void glutKeyboardFunc(void (*func)(unsigned char key, int x, int y))
{
    MiniGLDispatch->GLUTKeyboardFunc(func);
}

void glutReshapeFunc(void (*func)(int width, int height))
{
    MiniGLDispatch->GLUTReshapeFunc(func);
}

void glutSwapBuffers(void)
{
    MiniGLDispatch->GLUTSwapBuffers();
}

void glutPostRedisplay(void)
{
    MiniGLDispatch->GLUTPostRedisplay();
}

int glutGet(GLenum state)
{
    return MiniGLDispatch->GLUTGet(state);
}

void glutGameModeString(const char *string)
{
    MiniGLDispatch->GLUTGameModeString(string);
}

int glutEnterGameMode(void)
{
    return MiniGLDispatch->GLUTEnterGameMode();
}

void glutLeaveGameMode(void)
{
    MiniGLDispatch->GLUTLeaveGameMode();
}

int glutGameModeGet(GLenum query)
{
    return MiniGLDispatch->GLUTGameModeGet(query);
}

void glutSolidCube(GLdouble size)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidCube(size);
}

void glutSolidSphere(GLdouble radius, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidSphere(radius, slices, stacks);
}

void glutSolidCone(GLdouble base, GLdouble height, GLint slices, GLint stacks)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidCone(base, height, slices, stacks);
}

void glutSolidTorus(GLdouble innerRadius, GLdouble outerRadius, GLint sides, GLint rings)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidTorus(innerRadius, outerRadius, sides, rings);
}

void glutSolidDodecahedron(void)
{
    if ((*MiniGLDispatch->currentContext)) MiniGLDispatch->GLUTSolidDodecahedron();
}

void mglChoosePixelDepth(int depth)
{
    MiniGLDispatch->mglChoosePixelDepth(depth);
}

void mglDeleteContext(void)
{
    if ((*MiniGLDispatch->currentContext)) {
        GLcontext context = (*MiniGLDispatch->currentContext);
        (*MiniGLDispatch->currentContext) = (GLcontext)0;
        MiniGLDispatch->MGLDeleteContext(context);
    }
}

void glutInit(int *argcp, char **argv)
{
    atexit(mgl_glut_release);
    MiniGLDispatch->GLUTInit(argcp, argv);
}

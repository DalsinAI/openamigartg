/*
  SDL 2 on AmigaOS 3.x: SDL_GL_* on OpenGPU's GL module.
  Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).

  Every GL call goes through the program's GL (struct SDL2GLBridge, from
  libSDL2.a's SDL2_gl.o and the program's libGL.a), so SDL's contexts and
  the program's gl* calls are in one copy of Mesa. The module calls the
  program's code with the program's A4 (ogpu_module_callout).

  - A context is GLA's (gla_context_create) with the attributes set by
    SDL_GL_SetAttribute: the profile (compatibility, core or GLES), version,
    double buffer, depth (16 or 24), stencil (8) and alpha.
  - Each window gets one GL buffer, the size of its inside, made when a
    context is first made current on it and resized on SDL_GL_SwapWindow
    when the window's size has changed. Its frames are drawn into the
    window's RastPort (GimmeZeroZero, so at 0,0) by GL.module's present
    (OpenGPU, or WritePixelArray), which keeps the window's layers.
  - The display (virgl on the PC's graphics chip, or softpipe) is opened by
    SDL_GL_LoadLibrary, which SDL_CreateWindow calls for an
    SDL_WINDOW_OPENGL window, and closed by SDL_GL_UnloadLibrary.
  - Swap intervals are kept but not waited for: frames show when swapped.
*/
#include "../../SDL_internal.h"

#if defined(SDL_VIDEO_DRIVER_AMIGAOS3) && defined(SDL_VIDEO_OPENGL)

#include "SDL_hints.h"
#include "SDL_os3video.h"
#include "SDL_os3gl.h"
#include "sdl2_module.h"

#if defined(SDL_OPEN_LIBRARY_BUILD)
#include "module_rt.h"
#define OS3_GLCALL(fn, a, b, c) \
    ogpu_module_callout((const void *)(fn), (long)(a), (long)(b), (long)(c), 0)
#else
#define OS3_GLCALL(fn, a, b, c) \
    ((long (*)(long, long, long))(fn))((long)(a), (long)(b), (long)(c))
#endif

#define OS3_GL_WINDOW "SDL.OS3.GL"

/* The program's GL (SDL2Module's set_gl); none in libSDL2_static.a. */
static const struct SDL2GLBridge *OS3_gl;

void SDL_OS3_SetGLBridge(const struct SDL2GLBridge *bridge)
{
    OS3_gl = (bridge && bridge->version >= 1) ? bridge : NULL;
}

struct SDL_GLDriverData
{
    APTR display;
    int swap_interval;
};

typedef struct OS3_GLContext
{
    APTR gla;
    struct SDL2GLConfig cfg;
} OS3_GLContext;

typedef struct OS3_GLWindow
{
    APTR buffer;
    struct SDL2GLTarget target;
} OS3_GLWindow;

static void OS3_GL_Aim(SDL_Window *window, OS3_GLWindow *w)
{
    OS3_WindowData *data = (OS3_WindowData *)window->driverdata;
    w->target.rp = (data && data->window) ? data->window->RPort : NULL;
    w->target.left = 0;
    w->target.top = 0;
}

int OS3_GL_LoadLibrary(_THIS, const char *path)
{
    const char *want;
    LONG cpu_only;

    if (!OS3_gl) {
        return SDL_SetError("OpenGL: link the program with -lGL after -lSDL2 (`sdl2-config --libs` does), and use SDL2.module 3 or later");
    }
    if (_this->gl_data) {
        return 0;
    }
    _this->gl_data = (struct SDL_GLDriverData *)SDL_calloc(1, sizeof(struct SDL_GLDriverData));
    if (!_this->gl_data) {
        return SDL_OutOfMemory();
    }
    want = SDL_GetHint("SDL_OPENGPU_GL");
    cpu_only = want && (!SDL_strcasecmp(want, "cpu") || !SDL_strcasecmp(want, "softpipe") || !SDL_strcmp(want, "0"));
    _this->gl_data->display = (APTR)OS3_GLCALL(OS3_gl->display, cpu_only, 0, 0);
    if (!_this->gl_data->display) {
        SDL_free(_this->gl_data);
        _this->gl_data = NULL;
        return SDL_SetError("OpenGL: GL.module couldn't start (memory?)");
    }
    SDL_strlcpy(_this->gl_config.driver_path, "LIBS:OpenGPU/GL.module", sizeof(_this->gl_config.driver_path));
    return 0;
}

void *OS3_GL_GetProcAddress(_THIS, const char *proc)
{
    if (!OS3_gl || !proc) {
        return NULL;
    }
    return (void *)OS3_GLCALL(OS3_gl->get_proc_address, proc, 0, 0);
}

void OS3_GL_UnloadLibrary(_THIS)
{
    if (_this->gl_data) {
        if (OS3_gl && _this->gl_data->display) {
            OS3_GLCALL(OS3_gl->display_destroy, _this->gl_data->display, 0, 0);
        }
        SDL_free(_this->gl_data);
        _this->gl_data = NULL;
    }
}

/* SDL_GL_SetAttribute's settings, as GLA takes them. */
static void OS3_GL_Config(_THIS, struct SDL2GLConfig *cfg)
{
    const int mask = _this->gl_config.profile_mask;
    SDL_zerop(cfg);
    cfg->profile = (mask & SDL_GL_CONTEXT_PROFILE_ES) ? 2 : (mask & SDL_GL_CONTEXT_PROFILE_CORE) ? 1 : 0;
    cfg->major = _this->gl_config.major_version;
    cfg->minor = _this->gl_config.minor_version;
    /* The compatibility profile's default (2.1): the highest there is. */
    if (cfg->profile == 0 && cfg->major <= 2) {
        cfg->major = cfg->minor = 0;
    }
    cfg->doublebuf = _this->gl_config.double_buffer ? 1 : 0;
    cfg->depth = _this->gl_config.depth_size > 16 ? 24 : _this->gl_config.depth_size > 0 ? 16 : 0;
    cfg->stencil = _this->gl_config.stencil_size > 0 ? 8 : 0;
    cfg->alpha = _this->gl_config.alpha_size > 0 ? 1 : 0;
}

/* The window's GL buffer, made the first time a context is current on it. */
static OS3_GLWindow *OS3_GL_Window(_THIS, SDL_Window *window, const struct SDL2GLConfig *cfg)
{
    OS3_GLWindow *w = (OS3_GLWindow *)SDL_GetWindowData(window, OS3_GL_WINDOW);
    if (w || !cfg) {
        return w;
    }
    w = (OS3_GLWindow *)SDL_calloc(1, sizeof(*w));
    if (!w) {
        SDL_OutOfMemory();
        return NULL;
    }
    OS3_GL_Aim(window, w);
    w->target.width = window->w > 0 ? window->w : 1;
    w->target.height = window->h > 0 ? window->h : 1;
    w->buffer = (APTR)OS3_GLCALL(OS3_gl->buffer, _this->gl_data->display, cfg, &w->target);
    if (!w->buffer) {
        SDL_free(w);
        SDL_SetError("OpenGL: no GL buffer for the window (memory?)");
        return NULL;
    }
    SDL_SetWindowData(window, OS3_GL_WINDOW, w);
    return w;
}

SDL_GLContext OS3_GL_CreateContext(_THIS, SDL_Window *window)
{
    OS3_GLContext *c;
    APTR share = NULL;

    if (!OS3_gl || !_this->gl_data) {
        SDL_SetError("OpenGL: no GL library loaded");
        return NULL;
    }
    c = (OS3_GLContext *)SDL_calloc(1, sizeof(*c));
    if (!c) {
        SDL_OutOfMemory();
        return NULL;
    }
    OS3_GL_Config(_this, &c->cfg);
    if (_this->gl_config.share_with_current_context && SDL_GL_GetCurrentContext()) {
        share = ((OS3_GLContext *)SDL_GL_GetCurrentContext())->gla;
    }
    c->gla = (APTR)OS3_GLCALL(OS3_gl->context, _this->gl_data->display, &c->cfg, share);
    if (!c->gla) {
        SDL_free(c);
        SDL_SetError("OpenGL: GL.module has no context for these attributes (profile %d, version %d.%d)",
                     _this->gl_config.profile_mask, _this->gl_config.major_version, _this->gl_config.minor_version);
        return NULL;
    }
    /* A new context is current (SDL_GL_CreateContext). */
    if (OS3_GL_MakeCurrent(_this, window, c) < 0) {
        OS3_GLCALL(OS3_gl->context_destroy, c->gla, 0, 0);
        SDL_free(c);
        return NULL;
    }
    return c;
}

int OS3_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context)
{
    OS3_GLContext *c = (OS3_GLContext *)context;
    OS3_GLWindow *w;

    if (!OS3_gl) {
        return SDL_SetError("OpenGL: no GL library loaded");
    }
    if (!c || !window) {
        OS3_GLCALL(OS3_gl->make_current, 0, 0, 0);
        return 0;
    }
    w = OS3_GL_Window(_this, window, &c->cfg);
    if (!w) {
        return -1;
    }
    if (!OS3_GLCALL(OS3_gl->make_current, c->gla, w->buffer, 0)) {
        return SDL_SetError("OpenGL: the context doesn't fit the window's GL buffer");
    }
    return 0;
}

int OS3_GL_SetSwapInterval(_THIS, int interval)
{
    if (_this->gl_data) {
        _this->gl_data->swap_interval = interval;
    }
    return 0;
}

int OS3_GL_GetSwapInterval(_THIS)
{
    return _this->gl_data ? _this->gl_data->swap_interval : 0;
}

int OS3_GL_SwapWindow(_THIS, SDL_Window *window)
{
    OS3_GLContext *c = (OS3_GLContext *)SDL_GL_GetCurrentContext();
    OS3_GLWindow *w = (OS3_GLWindow *)SDL_GetWindowData(window, OS3_GL_WINDOW);
    ULONG width, height;

    if (!OS3_gl || !c || !w) {
        return SDL_SetError("OpenGL: no context is current on this window");
    }
    width = window->w > 0 ? window->w : 1;
    height = window->h > 0 ? window->h : 1;
    if (width != w->target.width || height != w->target.height) {
        OS3_GLCALL(OS3_gl->buffer_resize, w->buffer, width, height);
        w->target.width = width;
        w->target.height = height;
    }
    OS3_GL_Aim(window, w);
    if (!w->target.rp) {
        return 0;   /* the window is between screens (SDL_SetWindowFullscreen) */
    }
    OS3_GLCALL(OS3_gl->swap, c->gla, w->buffer, &w->target);
    return 0;
}

void OS3_GL_DeleteContext(_THIS, SDL_GLContext context)
{
    OS3_GLContext *c = (OS3_GLContext *)context;
    if (!c || !OS3_gl) {
        return;
    }
    if (SDL_GL_GetCurrentContext() == context) {
        OS3_GLCALL(OS3_gl->make_current, 0, 0, 0);
    }
    OS3_GLCALL(OS3_gl->context_destroy, c->gla, 0, 0);
    SDL_free(c);
}

void OS3_GL_DestroyWindow(_THIS, SDL_Window *window)
{
    OS3_GLWindow *w = (OS3_GLWindow *)SDL_GetWindowData(window, OS3_GL_WINDOW);
    if (!w) {
        return;
    }
    if (OS3_gl) {
        if (SDL_GL_GetCurrentWindow() == window) {
            OS3_GLCALL(OS3_gl->make_current, 0, 0, 0);
        }
        OS3_GLCALL(OS3_gl->buffer_destroy, w->buffer, 0, 0);
    }
    SDL_SetWindowData(window, OS3_GL_WINDOW, NULL);
    SDL_free(w);
}

#else

/* No OpenGL in this build: the module's set_gl has nothing to set. */
struct SDL2GLBridge;
void SDL_OS3_SetGLBridge(const struct SDL2GLBridge *bridge)
{
    (void)bridge;
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 && SDL_VIDEO_OPENGL */

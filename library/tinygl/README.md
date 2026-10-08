# TinyGL on OpenGPU

TinyGL programs build for AmigaOS 3.2 and run on OpenGPU's GL module: Mesa on the GPU through virgl on AmigaChrome, else Mesa's softpipe on the 68k. There is no second GL here. TinyGL's calls become the GL module's calls.

## Which TinyGL

There are two TinyGLs:

- **Fabrice Bellard's TinyGL** is a small software GL 1.1 subset with its own rasteriser.
- **The TinyGL of other Amiga-family systems** (tinygl.library) took Bellard's name and rewrote it for 3D hardware. It is the interface Amiga-family programs are written for.

The Team supports the second interface. Its calls are:

- `GLInit` and `GLClose`;
- the `GLA` calls, which start GL on a window, a screen or a bitmap and show its frames;
- the program's own `__tglContext` and `TinyGLBase`.

Bellard's rasteriser is not carried. The one library has a rasteriser already (`library/opengpu/ogpu_3d.c`), and a second would split the work. Programs written for Bellard's TinyGL use plain GL 1.1, so they build against the same headers. Only their few lines of window setup need changing.

## What is here

| Part | What it does |
| --- | --- |
| `include/proto/tinygl.h`, `include/tgl/gl.h`, `tgl/gla.h`, `tgl/types.h`, `include/clib/tinygl_protos.h` | The headers a TinyGL program includes. GL's own calls and types come from Mesa's `<GL/gl.h>`. |
| `include/tgl/glu.h` | `gluPerspective`, `gluLookAt`, `gluOrtho2D`, `gluBuild2DMipmaps` and `gluErrorString`. |
| `tinygl.c`, which builds `libtinygl.a` | The context calls and those GLU calls. It is linked into the program before `libGL.a`. |
| `tinygl_lib.c`, which builds `tinygl.library` 53 | A stand-in with no calls of its own, so that a program's `OpenLibrary("tinygl.library", ...)` works. It opens only where `opengpu.library` is installed. |

Build: `library/tinygl/build.sh [OUT_DIR]` (also run by `library/build.sh`). A program:

    m68k-amigaos-gcc -O2 prog.c -o Prog $(pkg-config --cflags --libs tinygl)

This is `-ltinygl -lGL -lm` with the kit's flags. `sdk/examples/tinygl/tglspin.c` is a complete program.

## How it works

- **The context.** Each `GLContext` is a GLA display, context and buffer (`library/modules/gl/gla/gla_core.h`). `GLAInitializeContext*` creates them, makes the context current, and points the frames at the target's RastPort.
- **The target.** The frames go inside a window's borders, onto a whole screen, or into a bitmap.
- **The GL calls.** `glBegin`, `glVertex3f` and the rest are Mesa's own, from `libGL.a`. They draw into the current context.
- **Showing a frame.** `GLASwapBuffers` makes its context current again if needed, picks up a window's new inner size, and shows the frame.
- **The route.** `ENV:TinyGL/Driver` picks it, as `ENV:MiniGL/Driver` does for MiniGL:
  - `CPU`: softpipe;
  - `OpenGPU`: the GPU, or fail;
  - `Auto` (the default): the GPU where OpenGPU's back end has virgl.
- **The tags.** `GLAInitializeContext` takes `TGL_CONTEXT_SCREEN`, `TGL_CONTEXT_WINDOW`, `TGL_CONTEXT_BITMAP` and `TGL_CONTEXT_STENCIL`. Where more than one target is given, the screen is taken first, then the window, then the bitmap.

## Not yet

- **The `GLXxx(context, ...)` forms of every GL call.** TinyGL's headers turn `glXxx(...)` into these. Here `glXxx` is GL's own call on the current context. A program that calls the capital forms directly needs them added. They can be generated from GL.module's list of names, as `libGL.a` is.
- **More than one context drawing in turn without a swap between.** The context that started or swapped last is the current one. Most programs have one context.
- **TinyGL's GLUT** (`glutInit`, `glutCreateWindow`, `glutMainLoop` and the rest).
- **`GLASetAttr`'s attributes** and TinyGL's context-version calls.
- **Auto-opening.** A program defines `TinyGLBase` and `__tglContext` itself, and calls `OpenLibrary` and `GLInit`.
- **Installing.** OpenUp's GL part must install `tinygl.library` into `LIBS:`.

## Licence

MIT, Copyright (c) 2026 Dalsin Limited: the Team wrote all of it. It follows TinyGL's published interface (names and arguments); no TinyGL code or header text is copied.

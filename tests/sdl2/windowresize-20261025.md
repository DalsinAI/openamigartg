# SDL2 window resize: native diagnostic, 25 October 2026

OpenTyrian creates a hidden 320x200 window and then asks for 640x400 for its
Scale2x output. The Amiga video device had no SetWindowSize callback. SDL's
logical size became 640x400, while the Intuition window stayed 320x200; the
resulting picture was clipped. The shared backend patch adds that callback,
its declaration and registration. No OpenTyrian source changes are involved.

`windowresize.c` reproduces that sequence, finds the native window by its
unique title, compares its GZZ dimensions with SDL's dimensions, creates a
software renderer and draws a marker at the requested bottom-right corner.
It exits 20 on the reproduced size mismatch and 0 when both are 640x400.
Run it on an RTG Workbench in an isolated instance, with SDL_OPENGPU=0.
This is not AGA/custom-screen resize qualification.

The unchanged open3 diagnostic returned:

```
Original module: logical 640x400 native 320x200 found 1; RC 20
Candidate:       logical 640x400 native 640x400 found 1; RC 0
```

The candidate was built offline with GCC 16.2.0-open3, -m68040 -m68881,
-fbaserel32, two build jobs, from pinned SDL 2.32.10 and the existing Amiga
backend pin. Its baserel and FPCR checks passed. The unchanged OpenTyrian
binary displayed its full title and accepted normal keyboard navigation
through player, episode and difficulty menus.

This is a local candidate based on the available SDL module source branch
(d35f752). It is not a replacement release of the SDK or current platform.
Review/integration against the currently maintained SDL source and intended
ROM/runtime, additional resize sequences, clamping, focus/input, fullscreen
transitions and AGA behaviour remain before release. No uploads or live
changes were made.

New-game transition subsequently stopped the candidate test instance after
the menus (black RTG window, red inactive AGA display, ROM PC 00f81e50 at
IPL 7). The original-module comparison also failed to establish gameplay,
but its CPU kept running and its output went black. These are different
observations: the cause remains unresolved, and neither is a gameplay or
clean-exit pass. The resize callback is not yet qualified as a complete SDK
replacement. The original lab module was restored and the lab stopped.

## Current-main integration

The patch is now 0010-window-resize.patch, after the current OpenGL, thread,
format and screensaver changes. It uses context anchored at the window-title
function; the complete backend patch series applies with zero fuzz. CI checks
that the callback remains at file scope and exercises its grow, shrink, missing
driver data and fullscreen-geometry contracts. These host contracts do not
replace native display tests.

Rebuilt against OpenRTG main 634c160 with GCC 16.2.0-open3, 68040/FPU and
-fbaserel32, the candidate is 1001500 bytes, SHA-256
6c2d2ef0c202b7a4f18231f5e9de11e96ab17d55a474513efe3ef4ee697c967d.
The baserel check and FPCR check pass. In the isolated muted AC090 lab the
native control again reports logical 640x400 / native 640x400, RC 0. With the
separate libnix fix, OpenTyrian reaches its full title, shop and first level.
Audio, broader window/fullscreen modes and an intended-ROM repeat remain
release gates; this is not a full game qualification.

Delivery: current amigachrome-guest ROM recipes include opengpu.library,
tinygl.library and ACRTG.gpu, not SDL2.module. This callback is in the disk
module at LIBS:OpenGPU/SDL2.module. Updating those ROM core libraries alone
will not deliver it. Use the qualified module in the coordinated system
update; rebuild statically linked SDL consumers separately.

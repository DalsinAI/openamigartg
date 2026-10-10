# SDL 2 in OpenGPU: SDL2.module

SDL 2 for AmigaOS 3.2, as part of the one library. Programs open only
opengpu.library and link `-lSDL2` as on any other system. Their SDL calls
go to `LIBS:OpenGPU/SDL2.module`: SDL 2.32.10 with Amiga back ends that
draw through OpenGPU, play through AHI and read the Amiga's input. When
OpenGPU can't do something, SDL's own software code does it, so every
program runs.

## What is built

`library/modules/sdl2/build.sh [OUT_DIR]` (default `build/`) builds, with
the GCC 16 stove:

| File | What it is |
| --- | --- |
| `SDL2.module` | SDL 2 and its back ends. Goes in `LIBS:OpenGPU/`. |
| `libSDL2.a` | The link library programs use (`stubs/sdl2`). |
| `include/SDL2/` | SDL's headers, and the satellites', for programs. |
| `libSDL2_static.a` | All of SDL inside the program, with no module. |
| `SDL2_image.module`, `SDL2_mixer.module` | SDL2_image and SDL2_mixer, shared and resident like SDL2.module. Go in `LIBS:OpenGPU/`. |
| `libSDL2_image.a`, `libSDL2_mixer.a` | Their link stubs (`stubs/sdl2/sat_stub.h`). |
| `libSDL2_image_static.a`, `libSDL2_mixer_static.a` | The same two inside the program, for `libSDL2_static.a`. |
| `libSDL2_ttf.a`, `libSDL2_net.a` | SDL2_ttf and SDL2_net, link libraries. |
| `tests/sdl2/` | The tests (`tests/sdl2` in the repository). |

Build a program the usual way:

    m68k-amigaos-gcc -m68040 -m68881 -noixemul -Ibuild/include/SDL2 game.c -Lbuild -lSDL2 -lm -lamiga

or, with the OpenGPU developer kit (`tools/make_sdk.py`, `sdk/README`):

    m68k-amigaos-gcc game.c -o Game $(sdl2-config --cflags --libs)

## How a program reaches SDL

- `libSDL2.a` is SDL's own dynamic API, cut down to the caller's half. Every
  `SDL_` function jumps through a table.
- The first SDL call loads `SDL2.module` and calls its `SDL_DYNAPI_entry`,
  which fills the table with the module's functions. The table's layout is
  SDL's `SDL_dynapi_procs.h`, so a program built today runs on any later
  module.
- The module is shared (residency step 2, DESIGN.md section 5): it is built
  `-fbaserel32`, so opengpu.library loads its code once for every program
  and each program's open gets its own copy of SDL's globals. Every stub
  function sets A4 to the program's copy for the call and puts the
  program's A4 back (`OGPU_A4`, `include/opengpu/module.h`).
- SDL's calls into the program (the audio callback, timers, a thread's
  function, event filters and watchers) run with the program's A4, and the
  threads SDL starts begin with the A4 of the program that started them
  (`src/SDL_os3callout.h`, `patches/sdl2/0005`, `patches/amigaos3/0005`).
  Other callbacks (the log output function, hint callbacks, an RWops of the
  program's) arrive with the module's A4, which only matters to a program
  built `-fbaserel` itself; it marks them `__saveds`, as Amiga callbacks
  always need.
- `make SHARED=0` builds a copy for each program instead (step 1).
- The module's interface is `include/opengpu/module.h`'s (`stubs/sdl2/sdl2_module.h`
  adds SDL's table): the first code takes SysBase, DOSBase, OpenGPUBase and
  a version, and returns a table. On opengpu.library 0.5 and later the stub
  loads it with `OGPU_ModuleOpen`; on older ones it LoadSegs it itself.
- The module has no program startup. `library/modules/common`
  (`module_start.S`, `module_rt.c`) runs libnix's init list (memory,
  standard I/O on the calling program's Input and Output, the libraries
  libnix opens, the constructors) and its exit list when the program ends,
  and gives `getenv` through GetVar.
- `tools/baserel_check.py` checks every build reaches SDL's data only
  through A4; `baserel.allow` lists the reviewed exceptions (tables of the
  back ends' bootstraps and render drivers, two constant tables), which only
  ever read the data's first values.
- Kalms' c2p (`patches/amigaos3/0005`) takes its sizes in registers, so the
  code keeps no data of its own.

## The Amiga back ends

| SDL part | Back end |
| --- | --- |
| Video | Intuition windows and screens. RTG through the CyberGraphX API (OpenRTG's cybergraphics.library); AGA through c2p. A window on an RTG screen is shown by OpenGPU, which copies the frame into video RAM through each visible part of the window's layer; WritePixelArray does it when part of the window is covered. |
| Renderer | `opengpu` first, then `software`. See below. |
| Audio | AHI first (8 or 16-bit, mono or stereo, double buffered with `ahir_Link`, opened in SDL's audio thread), then Paula's audio.device. |
| Threads, timers | Exec processes and semaphores. `SDL_Delay`, timed semaphore and condition waits use timer.device's UNIT_MICROHZ, so `SDL_Delay(1)` is a millisecond, not dos.library's 20 ms tick. |
| Input | Keyboard and mouse from Intuition; relative mouse mode with IDCMP_DELTAMOVE; Ctrl-C quits. Joysticks and game controllers through OpenInput (openinput.library), or through lowlevel.library where OpenInput isn't installed: see below. |
| Clipboard | clipboard.device unit 0, IFF FTXT, converted between UTF-8 and Latin-1. On AmigaChrome, ACClip carries it to the PC. |
| OpenGL | GL.module, through the program's own libGL.a (below). |
| Haptic | SDL's dummy driver: `SDL_Init(SDL_INIT_HAPTIC)` succeeds with no devices, as games that ask for it expect. |
| Environment | `SDL_getenv` reads the Shell's local and global variables (SetEnv, ENV:) as well as the program's own. |

Settings, with `SetEnv` or `SDL_SetHint`:

- `SDL_RENDER_DRIVER=software` picks the software renderer.
- `SDL_OPENGPU=0` keeps SDL off OpenGPU altogether.
- `SDL_OPENGPU_STATS=1` logs, when a renderer closes, how many commands
  OpenGPU drew and how many the CPU drew.
- `SDL_AUDIODRIVER=paula` picks Paula.
- `SDL_OPENGPU_GL=cpu` draws SDL's GL with softpipe even where the PC's
  graphics chip is there.
- `SDL_JOYSTICK_OPENINPUT=0` reads joysticks through lowlevel.library even
  where OpenInput is installed.
- `SDL_JOYSTICK_OPENINPUT_EXCLUSIVE=0` opens OpenInput's pads shared: a pad
  that also drives an Amiga port keeps driving it while the program runs.

### Joysticks and game controllers on OpenInput

`src/joystick/amigaos3/SDL_sysjoystick.c` has two back ends, chosen when
SDL's joystick subsystem starts.

**OpenInput** (openinput.library 1.2 or later; `openamigainput`,
Design-OpenInput.md). Version 1.1 is OpenInput's skeleton, which lists
nothing, so SDL treats it as missing.

- Every controller OpenInput lists is an SDL joystick, with its name, GUID,
  vendor, product and player number from the library.
- Hot-plug: OpenInput's notification gives `SDL_JOYDEVICEADDED` and
  `REMOVED`, and for game controllers `SDL_CONTROLLERDEVICEADDED` and
  `REMOVED`. Its messages go to a port that needs no signal, which SDL
  looks at each time it pumps events.
- A pad's SDL joystick is OpenInput's standard layout, which is SDL's
  GameController layout: buttons 0 to 20 are `SDL_GameControllerButton`'s
  (A, B, X, Y, Back, Guide, Start, the stick clicks, the shoulders, the
  d-pad, Misc, four paddles, the touchpad), axes 0 to 5 are
  `SDL_GameControllerAxis`'s with 16-bit sticks and analogue triggers, and
  one hat repeats the d-pad. The triggers run -32768 (released) to 32767
  (pulled) on the joystick, as on every SDL platform, and 0 to 32767 on the
  game controller.
- The driver gives SDL the mapping that says so, so `SDL_GameController`
  works with no database and no setup.
- Rumble: `SDL_JoystickRumble` and `SDL_GameControllerRumble` go to
  `OIN_Rumble` on pads that have it (`OICF_RUMBLE`). SDL stops the motors
  when the time it was given is up.
- Exclusive use: SDL opens pads with `OIT_Exclusive`, so a pad that also
  drives Amiga port 2 doesn't move the game's port joystick as well; the
  port's feed comes back when SDL closes the pad. When another program
  already has the pad to itself, SDL opens it shared rather than fail.
- Wheels and flight sticks are opened in their raw layout (every button,
  axis and hat as the device reports them), with no game controller
  mapping of their own.

**lowlevel.library**, when openinput.library isn't installed, is the
skeleton, or `SDL_JOYSTICK_OPENINPUT=0` asks for it: as before, the stick
or CD32 pad in each game port, with 2 digital axes, 1 hat and 7 buttons,
and no game controller mapping. Port 2 is joystick 0, and port 1 joystick
1 when lowlevel sees a stick there.

**Mappings: how SDL's and OpenInput's work together.**

- OpenInput maps every pad into the standard layout before SDL sees it,
  from its built-in lines (Xbox, PlayStation, Switch, 8BitDo, CD32-style
  pads), `ENVARC:OpenInput/mappings.txt` and OpenPrefs' Gamepads page; on
  AmigaChrome the x86 or ARM64 cores do it. That is the place to map a pad
  for every program.
- A program can still bring its own: `SDL_GameControllerAddMapping`,
  `SDL_GameControllerAddMappingsFromFile`, or the user's
  `SDL_GAMECONTROLLERCONFIG` (and `SDL_GAMECONTROLLERCONFIG_FILE`). When
  SDL has such a line for a pad's GUID when the pad is opened, the driver
  opens that pad in its raw layout, and SDL applies the line to the raw
  buttons, axes and hats, as on any other system. The raw numbers are the
  ones OpenInput's own lines use, so a line made in OpenPrefs works the
  same in an SDL program, and the other way round. A line only changes
  that program; nothing goes back into OpenInput.
- Add lines before opening the pad, as games do at start: a pad already
  open keeps the layout it was opened with.
- Lines in a file are loaded only when their `platform:` field matches
  SDL: SDL 2 here says `AmigaOS 3` (`SDL_GetPlatform`, patch
  `patches/sdl2/0007`). Lines without a `platform:` field are taken by
  `SDL_GameControllerAddMapping` itself, as SDL always does.
- `SDL_GameControllerMapping` on a pad SDL maps through OpenInput gives the
  standard layout's line (`a:b0,b:b1,...`), not OpenInput's line for the
  pad.

`include/openinput/` holds a copy of OpenInput's frozen headers
(`include/libraries/openinput.h` and `include/inline/openinput.h` from
`openamigainput` at efc932c); the header only grows, so a newer library
still answers this driver. The driver opens the library itself, with a
base of its own name (`SDL_OS3_OpenInputBase`), so a program that uses
openinput.library too has no clash.

**Programs built with GCC 6.** Six SDL calls return a struct:
`SDL_JoystickGetDeviceGUID`, `SDL_JoystickGetGUID`,
`SDL_JoystickGetGUIDFromString`, `SDL_GUIDFromString` and
`SDL_GameControllerGetBindForAxis` and `ForButton`. GCC 6 (the os32 stove)
passes the address for a returned struct in A0, while the GCC 16 that
builds SDL2.module and libSDL2.a takes it in A1, so a GCC 6 program got a
garbage GUID and had 16 bytes written wherever A1 pointed. SDL's headers
(`patches/sdl2/0008`) now send those six calls, in a program built with
GCC 6, through helpers in libSDL2.a and libSDL2_static.a that take the
address as an argument (`stubs/sdl2/SDL2_structret.c`). GCC 6 programs
built before this need rebuilding to get right GUIDs.

`tests/sdl2/padcheck` lists the joysticks and watches them (hot-plug,
every controller button and axis, rumble), with no window; with OpenInput's
test pad (vendor 0xDA15) every check runs.

### SDL_GL on GL.module

SDL2.module 3 (8 October 2026) gives `SDL_WINDOW_OPENGL`,
`SDL_GL_CreateContext`, `SDL_GL_SwapWindow` and the rest on OpenGPU's GL
module (`src/video/amigaos3/SDL_os3gl.c`, `patches/amigaos3/0006`):

- A program has one copy of GL, the one its `libGL.a` opens, so SDL uses
  that copy: `libSDL2.a`'s `SDL2_gl.o` (`stubs/sdl2/SDL2_gl.c`) hands the
  module the program's GLA calls (`struct SDL2GLBridge`,
  `stubs/sdl2/sdl2_module.h`), and the module calls them with the
  program's A4. SDL's context and the program's `gl` calls are then the
  same GL.
- `SDL2_gl.o` is a member of its own: only a program that calls an
  `SDL_GL_` function links it, and then needs `-lGL`. The Makefile splits
  `SDL_dynapi_procs.h` for that; SDL's tests, which call `SDL_GL_` through
  the test framework, link `tests/sdl2/nogl.c` instead of `libGL.a`.
- A stub asks for module version 2 and calls `set_gl` only on version 3, so
  programs run on a version 2 module too, without OpenGL.
- Each window gets a GL buffer the size of its inside, drawn into the
  window's RastPort by GL.module's present; `SDL_GL_SwapWindow` follows the
  window's size. Swap intervals are kept but not waited for.
- `libSDL2_static.a` has no OpenGL: there is no stub to hand it GL.

Measured on a scratch copy of Instance-11 (AC090 68040, virgl), 8 October
2026: the kit's GLSpin (a lit cube, 320x240) at 1,268 to 1,394 fps on
virgl and 52.6 fps on softpipe; GLA's own triangle at 680 and 39.6.

### The opengpu renderer

Textures and the target are surfaces in memory, described to OpenGPU as
slots. Each `RunCommandQueue` becomes one OpenGPU batch:

| SDL | OpenGPU |
| --- | --- |
| Clear, opaque fills | FILL |
| Fills in any SDL blend mode | FILL_BLEND (stream v1.2) |
| Points and lines | POINTS_BLEND, LINES_BLEND (v1.2) |
| Copies, scaled or not, with alpha | COMPOSITE in SDL's blend mode; COPY for a plain 1:1 copy |
| Copies with colour modulation, rotation, flips or renderer scale | COMPOSITE_AFFINE (v1.2), one command each |
| `SDL_RenderGeometry` | TRIANGLES, when `include/opengpu/build3d.h` is there |
| YUV and NV12 textures | YUV (v1.2) into the texture's own XRGB surface |

The renderer asks `OGPU_Query` for each operation on the target's format.
A library without stream v1.2, or a back end without an operation, sends
that command to SDL's software code, which draws into the same surface
after the batch so far has finished. Custom blend modes always take that
path. SDL chooses the software renderer by itself when opengpu.library
isn't there or the window's format isn't one OpenGPU draws.

`tests/sdl2/rendercompare` draws the same scenes with both renderers and
compares them.

## SDL2_image and SDL2_mixer: modules, decoding on the cores

The design is amigachrome's `docs/design/Design-OpenGPU-SDL2-Satellites.md`.
In short:

- `SDL2_image.module` and `SDL2_mixer.module` are shared modules like
  SDL2.module (`stubs/sdl2/sat_module.h`). A program links `-lSDL2_image`
  or `-lSDL2_mixer` (a stub) and `-lSDL2`; the module calls SDL through the
  program's own SDL jump table, with SDL2.module's A4 for each call
  (`satellites/module/sat_sdl.c`). SDL's audio callback into SDL2_mixer
  comes through `ogpu_sat_callin3` (`sat_entry.S`), which sets the
  module's A4. libSDL2.a closes the satellites before SDL
  (`SDL2Stub_AtClose`).
- Their ABI is `satellites/abi/SDL_image_procs.h` and `SDL_mixer_procs.h`:
  the order of the stubs' tables, append-only. `gen_sat_procs.py` makes
  and checks them against the headers.
- SDL2_image decodes down a ladder (`satellites/module/ogpu_image.c`): the
  x86 or ARM64 cores through `media.decode/1` for JPEG, PNG (not paletted),
  WebP, AVIF, HEIC, JPEG XL and TIFF; SDL2_image's own decoders for the rest
  (and for all when there is no services card or Cradle); the service
  again for what SDL2_image doesn't know; then datatypes. `SDL_IMAGE_DECODER`
  `cpu` keeps to SDL2_image's own.
- SDL2_mixer decodes Ogg Vorbis, MP3, FLAC and tracker modules through
  `media.decode/1` first (`satellites/module/ogpu_mixer.c`, four music
  interfaces before SDL2_mixer's own), two seconds ahead of what plays, and
  mixes into SDL's audio device. `SDL_MIXER_DECODER` `cpu` keeps to its own.
- `satellites/module/sat_service.c` is openservice.device for both: any
  task may start, wait for and close a request (SDL2_mixer waits in SDL's
  audio thread).
- `tests/sdl2/satladder.c` (with `make-satmedia.sh`'s pictures and sounds)
  says which rung decoded what, how long it took, and how far the pixels
  are from the original.
- File names mean the same in every process (`library/modules/common/
  ogpu_path.h`, 10 October 2026). `SDL_RWFromFile` (SDL2.module 4,
  `patches/sdl2/0009`, `src/filesystem/amigaos3/SDL_os3path.c`) opens the
  full path (`Lock` + `NameFromLock`, or the folder's full path and the
  file's name for a file still to be written), so `PROGDIR:`, a relative
  name and an assign all work wherever the file is read; `Mix_LoadMUS`
  (`patches/sdl2_mixer/0002`) and the datatype fallback of `IMG_Load`
  (`ogpu_image.c`) do the same in SDL2_mixer.module and SDL2_image.module
  (version 3, through `satellites/module/sat_path.c`). SDL's threads start
  with their parent's home folder and current directory and no requester
  window (`patches/amigaos3/0007`). `tests/sdl2/progdir.c` checks it.

## How the source is put together

Nothing third-party is committed. `build.sh`:

1. checks SDL 2.32.10's tarball against its sha256 in `UPSTREAM.json`
   (from `TARBALLS`, default `~/AmigaChrome-dev/upstream-tarballs`, or
   downloaded once);
2. fetches libSDL2-amigaos3 (Zlib) at its pinned commit for the Amiga
   back ends it started (video, AGA c2p, Paula, threads, timer, filesystem);
3. copies SDL's software renderer to `src/render/opengpu/`, then applies
   `patches/sdl2` (the platform in SDL's lists, three fixes, the
   environment, the opengpu renderer as a change to that copy, null
   checks where SDL used a display or surface it had not got, the
   platform name and a mapping question for OpenInput, and the struct
   returns for GCC 6 programs) and
   `patches/amigaos3` (the Team's changes to libSDL2-amigaos3's back ends);
4. adds `src/` (the Team's own files: AHI, the joystick on OpenInput or
   lowlevel.library, the clipboard, OpenGPU's helpers) and
   `include/SDL_config_amigaos.h`;
5. builds the satellites from their pinned tarballs (`satellites/`), with
   `patches/freetype` applied to FreeType (two null checks), and
   `patches/sdl2_image` and `patches/sdl2_mixer` (the decoding ladders, and
   SDL2_mixer's audio callback in a module).

| Library | Licence | Notes |
| --- | --- | --- |
| SDL 2.32.10 | Zlib | |
| libSDL2-amigaos3 0.7.0 | Zlib | |
| SDL2_image 2.8.12 | Zlib | stb_image (PNG, JPEG), nanosvg, QOI, miniz and tiny_jpeg are its own, built in |
| SDL2_mixer 2.8.2 | Zlib | stb_vorbis, minimp3, dr_flac and Timidity are its own, built in |
| SDL2_ttf 2.24.0 | Zlib | without HarfBuzz |
| SDL2_net 2.4.0 | Zlib | over bsdsocket.library (libnix's `-lsocket`; its `select()` is `satellites/net_shim.c`'s, as libnix's never sees a socket with data) |
| FreeType 2.14.3 | FreeType Licence | for SDL2_ttf |
| libxmp 4.7.3 | MIT | for SDL2_mixer's MOD, XM, S3M, IT, MED and the other tracker formats |

The Team's files are MIT, Copyright (c) 2026 Dalsin Limited. Files that
alter SDL's (the stub, the renderer patch) keep SDL's Zlib notice and say
they are altered.

On the GCC 16.2 stove the build gives one warning, in SDL's own
`src/video/SDL_surface.c` (`SDL_UpperBlitScaled`: `r`, `g`, `b` and
`alpha` "may be used uninitialized"). It is a false alarm, left as it is:
`SDL_GetSurfaceColorMod` and `SDL_GetSurfaceAlphaMod` set them for the
surface just checked, and the Team doesn't patch SDL's code for GCC's
sake. Any other warning is new and wants looking at.

## Measured

`measurements/20261008-sdl2-opengpu.txt`: SDL's testsprite2 and
testrendercopyex on the CPU and on OpenGPU, with `tests/sdl2/bench`.

Residency step 2 (8 October 2026, `measurements/20261008-residency-step2.txt`):
a first open of SDL2.module takes 1,057 KB and a further one 171 KB; a call
through libSDL2.a costs 0.021 µs against 0.015 before (SDL_GetCPUCount on
the AC090); testsprite2 runs at 504 to 539 fps, as before (525 to 553).

## Next

- `SDL_GL_*` in `libSDL2_static.a`.
- Remove the stub's own LoadSeg once every install has opengpu.library 0.5.
- The GCC 16 stove's FPCR clash (`fpcr_check.py`): three satellite files are
  built at -O0 until the stove is fixed, and the build checks everything.

# OpenAL in OpenGPU: OpenAL.module

OpenAL 1.1 for AmigaOS 3.2, built the same way as SDL2.module and
SDL3.module (`library/modules/sdl2`, `library/modules/sdl3`): one copy of
the code in `LIBS:OpenGPU/OpenAL.module` for every program, each program
with its own copy of OpenAL's state, and programs that include `<AL/al.h>`
and `<AL/alc.h>` and link `-lopenal` as on any other system. Built hard
float (`-m68040 -m68881`): it needs a 68040 or 68060 with its FPU, and
programs are built the same way (`alGetFloat` and `alGetDouble` return in
FP0).

The sound goes through AHI, so it reaches the Cradle's mixer like every
other AHI program. This is the first step (P1) of the Team's OpenAL design
of 24 October 2026: the core, the headers, the module and Mix.

## What it has

- **OpenAL 1.1 and ALC 1.1**: sources, buffers and the listener; the six
  distance models and AL_NONE; cones; Doppler (with OpenAL 1.0's
  `AL_DOPPLER_VELOCITY`); queued buffers; offsets in seconds, samples and
  bytes; every error case the specification gives (below where it gives
  two).
- **Extensions**:
  - `AL_EXT_FLOAT32`: float buffers, converted to 16 bit when loaded;
  - `AL_EXT_MCFORMATS`: quad, rear, 5.1, 6.1 and 7.1 buffers in 8, 16
    and 32 (float) bits, folded down to stereo when loaded;
  - `ALC_ENUMERATION_EXT`: the list of devices;
  - `ALC_EXT_disconnect`: `ALC_CONNECTED`, so a program learns when its
    sound has gone;
  - the 1.0-compatible names of 1.1's own features (`AL_EXT_OFFSET`,
    `AL_EXT_LINEAR_DISTANCE`, `AL_EXT_EXPONENT_DISTANCE`,
    `ALC_EXT_CAPTURE`), which the specification's appendix 9 says a 1.1
    implementation always has.
- **Not yet, refused the way the specification says** so programs fall
  back: capture (there is no capture device: the list is empty and
  `alcCaptureOpenDevice` returns NULL), EFX (`ALC_EXT_EFX` is absent and
  its entry points are NULL), HRTF and OpenAL Soft's own extensions.
  Unknown context attributes (EFX's `ALC_MAX_AUXILIARY_SENDS` among them)
  are passed over.

## What is built

`library/modules/openal/build.sh [OUT_DIR]` (default `build/`), with the
GCC 16 stove:

| File | What it is |
| --- | --- |
| `OpenAL.module` | OpenAL and its back end. Goes in `LIBS:OpenGPU/`. |
| `libopenal.a` | The link library programs use (`stubs/openal`). |
| `include/AL/` | `al.h`, `alc.h` and `alext.h`, for programs. |
| `libopenal_static.a` | All of OpenAL inside the program, with no module. |
| `tests/openal/` | `nomodule.m`, `alinfo.m` and `alinfo`, `altone.m` and `altone`, and the PC tests built for the 68k (below). |

Build a program the usual way:

    m68k-amigaos-gcc -m68040 -m68881 -noixemul -Ibuild/include game.c -Lbuild -lopenal -lm -lamiga

| Path | What it is |
| --- | --- |
| `include/AL/` | The headers, written from the specification and the extension texts. |
| `src/al_internal.h` | The objects, the two locks and the back-end interface. |
| `src/alc.c` | Devices, contexts, the ALC queries, capture refused; the locks, names and errors. |
| `src/al_state.c` | Errors, the global state, strings, extensions, the distance model, Doppler, the listener. |
| `src/al_source.c` | Sources: attributes, the queue, the four states, offsets. |
| `src/al_buffer.c` | Buffers and the formats they take. |
| `src/al_spatial.c` | A source's gains and pitch from its properties and the listener. |
| `src/al_mix.c` | The Mix back end: the mixer. |
| `src/al_ahi.c` | Mix's output on the Amiga: ahi.device. |
| `module/openal_module.c` | The module's table (`stubs/openal/openal_module.h`). |
| `../../../stubs/openal` | `libopenal.a`; `openal_procs.h` is the jump table's order. |
| `../../../tests/openal` | The tests (below). |

## How a program reaches OpenAL

- `libopenal.a` has every AL and ALC call as an entry that jumps through a
  table (`struct OpenALProcs`, in `openal_procs.h`'s order). The first call
  loads `OpenAL.module` with `OGPU_ModuleOpen` (opengpu.library 0.5 and
  later; a LoadSeg of its own before that) and asks it to fill the table.
  The table only grows at its end: a program built today runs on any later
  module, and a module refuses (the table's `fill` returns -1) a program
  that needs calls it hasn't got.
- The module is shared (residency step 2, DESIGN.md section 5): built
  `-fbaserel32`, its code is loaded once, and each program's open gets its
  own copy of OpenAL's data (424 bytes of data and BSS; 43,080 bytes of
  code shared). Every stub entry sets A4 to the program's copy for the
  call (`OGPU_A4`).
- `alGetProcAddress` and `alcGetProcAddress` give the stub's own entry for
  a call the module has, never the module's function, so a call through
  the pointer sets A4 too.
- Without the module a program still runs, with no sound: the first call
  says once on its Output that the module isn't installed, and every call
  then returns 0 (`alcOpenDevice` NULL: "no sound driver/device has been
  found", 6.1.1). libSDL3.a ends the program instead; a game plays without
  sound but can't draw without SDL.
- `library/modules/common` stands in for libnix's startup, as for SDL.

## The core and its back ends

The core holds every object and each source's timeline (which queue entry
plays, and where in it). A back end makes the sound, through
`struct al_backend` (`src/al_internal.h`): open and close (the device),
start and stop (from the first context to the last), and `update`, called
after an AL call changed a context (unless it is suspended) and at
`alcProcessContext`. Sources carry what changed (`AL_DIRTY_PARAMS`,
`_STATE`, `_QUEUE`); both back ends take a source's two gains and its pitch
from `al_source_params` and move its timeline with `al_source_next`.

- **Mix** (this step) works on any AHI. It mixes every playing source of a
  device into one 16-bit stereo stream at the device's rate:
  - each source read at its rate times its pitch (a 16.16 step) with linear
    interpolation; at the end of a buffer the next frame is the next queue
    entry's first, so a stream has no seam;
  - the two gains as Q14 integers up to just under 2.0; a change of gain
    ramps over 64 frames, so it doesn't click (4.1);
  - the sum in 32 bits, clamped to 16; integer only for each sample, with
    a loop of its own for the common case (the buffer at the device's rate,
    pitch 1: no interpolation);
  - a silent source isn't mixed, but its offset moves on (4.3.6).

  Its output (`src/al_ahi.c`) is SDL2.module's AHI driver's way: a process
  of the device's own ("OpenAL Mix", priority 5) opens ahi.device's unit 0
  and its two requests, and plays the updates double buffered with
  `ahir_Link`: each update is mixed and sent linked to the one playing,
  then the process waits for the one before to end. The latency is two
  updates: `ALC_REFRESH` is 25 a second unless the program asks (80 ms).
  The rate is unit 0's from AHI's prefs (`ENV:Sys/ahi.prefs`), so AHI
  needn't resample again, or `ALC_FREQUENCY` (8,000 to 96,000), or 22,050
  when the prefs say nothing. When the unit can't be opened, or a write
  comes back with an error (the board taken by a program that allocated it
  itself), the device is disconnected: every source stops, a play stops at
  once, and `ALC_CONNECTED` is `ALC_FALSE`.
- **Voices** (P2) will play each source on one of the board's voices on
  ACAHI's Host mix, and the broker in acaudio.audio (P3) will share the
  board between programs. Mix stays for every other AHI mode and for
  sources past the voices.

Two locks: `al_device_lock` around making and ending devices and contexts
(a back end's start and stop run under it alone, so they can wait for the
output process), and `al_state_lock` around every object's state, which Mix
also takes while it mixes an update. An AL call can so wait for at most one
update's mixing.

## Formats

`alBufferData` keeps the data as the mixer reads it, and `AL_BITS`,
`AL_CHANNELS` and `AL_SIZE` give that stored form (5.3.3: "the internal
representation of the buffer data"):

- 16-bit data as it is (big-endian on the 68k: the CPU's order);
- 8-bit mono and stereo data made signed (it arrives unsigned, 128 being
  silence);
- float data (`AL_EXT_FLOAT32`, and `AL_EXT_MCFORMATS`'s 32-bit formats)
  as 16 bit: -1.0 to 1.0 onto -32767 to 32767, clamped, a NaN silent;
- multichannel data folded to 16-bit stereo. The channels are read in the
  usual order of each layout (`alext.h`). Each side takes its front
  channel at 1, the centre and its own rear and side channels at 0.7071,
  and (6.1) the rear centre at 0.5; LFE is left out. The sum is scaled by
  1 over its weights, so a full-scale signal in every channel can't clip.

## Where the specification leaves a choice

- **Errors for a token.** A set call given another kind of object's token
  (`AL_ORIENTATION` to a source) gets `AL_INVALID_OPERATION` (2.15); any
  other token a call doesn't take, a query-only attribute given to a set
  call, or a form the attribute's signature lacks (`alListeneri` with
  `AL_GAIN`) gets `AL_INVALID_ENUM` (2.7, 2.16). A NULL pointer to a set
  call is `AL_INVALID_VALUE`; a NULL destination to a get call is quietly
  ignored (2.16).
- **Buffer queries** follow 5.3.1, which names their own errors: a name
  that isn't a buffer gives `AL_INVALID_OPERATION`, a token the call
  doesn't take `AL_INVALID_VALUE` (2.16's general rule would give
  `AL_INVALID_NAME` and `AL_INVALID_ENUM`).
- **Floats.** Every float property must be finite, or the call gets
  `AL_INVALID_VALUE` (2.3 lets an implementation leave NaN and infinity
  unspecified).
- **`AL_SOURCE_RELATIVE`**: position, velocity, direction and cone are in
  the listener's frame, its orientation as well as its position (4.3.2
  names the direction, which a position alone wouldn't move). A relative
  source's velocity is relative to the listener's, for Doppler.
- **The pan** is equal power from the side a source is on: left = gain *
  sqrt((1 - x) / 2), right = gain * sqrt((1 + x) / 2), x being the
  sideways part of the direction to the source. Buffers with two channels
  are played without 3D (5.3.4): AL_GAIN, clamped, times the listener's,
  to both channels, and AL_PITCH.
- **Cones**: the angle to the listener against half of each cone's angle,
  linear in the angle between; `AL_DIRECTION` (0, 0, 0) is not directional.
- **Suspend** (6.2.5) keeps the sound going: changes to the 3D properties
  wait for `alcProcessContext` and then apply together (the design's
  batching), while plays, stops and queues act at once. The specification's
  aside that a suspended context's offsets don't move is not followed:
  programs that suspend around each frame's updates would stutter.
- **Offsets** set on a playing or paused source jump at once, or fail with
  `AL_INVALID_VALUE` past the end of the queue; set on a stopped or initial
  one they wait for the next `alSourcePlay` (and are forgotten by
  `alSourceStop`, `alSourceRewind`, or a play that finds the queue
  shorter). Seconds within a float's precision of a whole frame are that
  frame (`0.01f` is frame 441 at 44,100 Hz). `AL_BYTE_OFFSET` counts in
  the stored form, as `AL_SIZE` does.
- **Queues**: buffers in one queue have the same format and frequency, the
  NULL buffer excepted (4.3.5); a looping source has nothing processed; a
  queue emptied by unqueuing makes the source `AL_UNDETERMINED`, as an
  `AL_BUFFER` of 0 does.
- **Sizes**: `alBufferData` with a size of 0 changes nothing (table 5-2: "a
  legal NOP"). `alIsBuffer(0)` is `AL_TRUE` (4.3.2: `AL_NONE` is a valid
  buffer name).
- **Devices and contexts.** `alcCloseDevice` fails while the device has
  contexts or buffers (6.1.2). A current context can't be destroyed
  (6.2.6); ALC has no `ALC_INVALID_OPERATION` token, which that section
  names, so the error is `ALC_INVALID_VALUE`. A synchronous context
  (`ALC_SYNC` true) can't be given, nor a second context at another rate
  on a device that plays (6.2.2). A context has
  `ALC_MONO_SOURCES` + `ALC_STEREO_SOURCES` sources (256 and 16 unless
  asked; 1,024 at most); more is `AL_INVALID_VALUE` (2.12).
- **Strings**: the error strings are the errors' names (2.7). Extension
  names are matched without regard to case.

## Clean room

The module, its headers and its tests are the Team's own, MIT. They are
written from the OpenAL 1.1 Specification and Reference and the extension
texts; the names and values of the tokens are the interface, and are kept.
Nothing is taken from OpenAL Soft or Creative's SDK. The AHI side follows
SDL2.module's own AHI driver, which is the Team's. The AHI headers come from
the OS 3.2 stove at build time, as for SDL.

## Tests

- `tests/openal/run_host.sh`, on a Linux PC (in `tests/run.sh`): the core
  built for the PC, with Mix's output as `tests/openal/host_out.c`, with
  AddressSanitizer and UBSan, every UBSan report fatal:
  - `test_al_api`: every AL and ALC call's error cases and state queries,
    from the specification's sections and tables;
  - `test_al_3d`: the distance models (672 distances), cones (84 angles)
    and Doppler (180 cases) against values worked out in doubles from the
    specification's formulas; the gain steps; the pan; then the same as
    mixed, the formats as stored, suspend and process, gain ramps, two
    contexts on a device;
  - `test_al_stream`: queued streams with no gap and no repeat, sample for
    sample, at the device's rate and resampled (32 kHz, 22,050 Hz at pitch
    1.37 and 48 kHz at pitch 0.61, into 44.1 kHz), with
    `AL_BUFFERS_PROCESSED` right after each update; underruns, looping,
    offsets, the NULL buffer;
  - `test_al_thread`: OpenAL from two threads while a third mixes, also
    under ThreadSanitizer;
  - `stub_nomodule`: `libopenal.a` with no module, an older one, and one
    that works.
- On the Amiga (`build.sh`'s tests): `test_al_api`, `test_al_3d` and
  `test_al_stream` built for the 68k with the same stand-in output;
  `nomodule.m`; `alinfo.m` and `alinfo` (what OpenAL says, and that the
  module's calls work, with or without a sound device); `altone.m` and
  `altone` (a tone, a 3D sweep round the listener and a stream, for a lab).
- `tests/openal/run_vamos.sh`, on a PC with amitools' vamos (no Amiga, no
  ROM): those three tests big-endian on a 68k, then `alinfo.m` through
  `libopenal.a` and `OpenAL.module` (vamos has no ahi.device, so no device
  opens), and `nomodule.m` and `alinfo.m` with no module. It wants a build
  with `CPU="-m68020 -m68881"`: the 68040 build takes square roots with
  FDSQRT, which vamos doesn't emulate (it gives sqrt(16) as 16); a real
  68040 has it.

## Next

- P2: Voices on ACAHI's Host mix, and Mix's output compared with the same
  scene through a model of the board's voices.
- P3: the broker in acaudio.audio.
- P4: the lab (a tone, a sweep and a stream recorded from the instance's
  `.ahi` ring; the 68k's time for each source update; OpenAL and SDL at
  once; Mix on the 16-bit mode), and Doom 3.
- `AL/efx.h` and EFX; `ALC_ENUMERATE_ALL_EXT`.
- Mixing on the cores (openmulticore), as SDL2_mixer decodes.

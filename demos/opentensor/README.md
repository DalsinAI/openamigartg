# OpenTensor: First Light

OpenTensor is a native AmigaOS 3.x demo and integration benchmark for the Open stack:

- Mesa through GLA and OpenGPU;
- OpenRTG/CyberGraphX display modes;
- AHI through `ahi.device`;
- AC090 68040 + FPU as the reference CPU.

The first build is intentionally SDL-free. It gives Mesa, AHI and RTG a clean reference path before an SDL2 edition is added.

## What it shows

The 64-second looping sequence moves through five scenes:

1. **The Noise** - a star field resolves from darkness.
2. **The Field** - a subtle Amiga-blue and warm-grey checkerboard landscape.
3. **The Boing Event** - a red-and-white sphere falls and sends rings across the field.
4. **Tensor Storm** - additive stars and a geometric tunnel.
5. **First Light** - the ball and vector title sequence: `OPENTENSOR`, `FIRST LIGHT`, and `AND THUS THE FIRST TENSOR WAS BORN`.

The soundtrack is generated in real time as 16-bit stereo PCM and streamed to `ahi.device` using linked double buffers. It is deliberately tracker-like: square arpeggio, triangle bass, saw pad, kick and noise hat.

## Build

```sh
STOVE=~/AmigaChrome/stoves/os32-gcc16 \
    sh demos/opentensor/build.sh
```

Output:

```text
build/opentensor/OpenTensor-First-Light-0.1.0.zip
```

The build expects the current GCC16 AmigaOS stove, AHI development headers, and the OpenGPU `libGL.a` SDK.

## Run

```text
Stack 1000000
OpenTensor
```

The default is `960x540x32`, VirGL when available, a private OpenRTG screen and AHI audio. If that mode is unavailable the demo falls back to `640x360x32`.

Options:

```text
CPU
WINDOW
W n H n
DEPTH 16|32
SECONDS n
NOAUDIO
NOVSYNC
```

Controls:

```text
Esc or mouse   exit
Space          pause
F1             performance overlay
```

## Why both full-screen and windowed modes exist

A private screen sets `gla_os3_target.bitmap`, selecting GLA's screen-owned OpenGPU presentation path. `WINDOW` leaves the bitmap unset and deliberately uses the layered-window compatibility path. That makes the demo a repeatable regression test for the direct-present work now being developed.

## First-light acceptance gates

- Build with GCC16 for `-m68040 -m68881`.
- Start Mesa through VirGL or fall back cleanly to softpipe.
- Open a private OpenRTG mode.
- Produce continuous AHI audio without hanging on shutdown.
- Exit cleanly from keyboard, mouse or Ctrl-C.
- Complete a timed run and report FPS plus AHI underruns.

## First real run

The first 18-second AC090 lab run passed at `960x540x32`, using Mesa 26.2.4 through VirGL and generated 44.1 kHz AHI audio. It returned cleanly to Workbench at 50.0 fps with one initial audio underrun. See [FIRST_LIGHT_RESULT.md](FIRST_LIGHT_RESULT.md) for the captured result and evidence hashes.

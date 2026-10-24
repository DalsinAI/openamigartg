# OpenTensor: Beyond First Light

OpenTensor is a native AmigaOS 3.x demo and integration benchmark for the Open stack:

- Mesa 26.2.4 through GLA and OpenGPU;
- OpenRTG/CyberGraphX display modes;
- AHI through `ahi.device`;
- AC090 68040 + FPU as the reference CPU.

It deliberately has no SDL dependency. Mesa, AHI and RTG are exercised directly, giving the platform a clean reference production while the SDL/Mesa presentation work continues.

## The production

The 164-second loop is split into eleven scenes.

### Act I — First Light

1. **The Noise** — a star field resolves from darkness.
2. **The Field** — a blue enamel checkerboard landscape with luminous bevels, a light pool and distance haze.
3. **The Boing Event** — a glossy red-and-white sphere falls above a soft contact shadow and planar reflection, sending rings across the field.
4. **Tensor Storm** — additive stars and a geometric tunnel.
5. **First Light** — `OPENTENSOR`, `FIRST LIGHT`, and `AND THUS THE FIRST TENSOR WAS BORN`.

### Act II — Beyond First Light

6. **The Timecode** — the surviving point becomes a deep broadcast timecode field: `THE NUMBER BECAME THE TIMECODE`.
7. **Across The Field** — eight luminous tensor streams move above the checkerboard and carry audio-synchronised nodes.
8. **Form** — nested wire tensors converge, a wireframe Boing form becomes solid, and `AND IT WAS GIVEN FORM` appears.
9. **Window Breakout** — the universe moves into a real Intuition window, gliding across an Amiga-style desktop while Mesa keeps rendering.
10. **Beyond First Light** — five orbiting Boing forms surround the tensor core and the full Open stack is named.
11. **The Stack Is Open** — Mesa, OpenRTG, OpenGPU, AHI and AC090 resolve to one final point before the loop begins again.

The original soundtrack is generated as 16-bit stereo PCM and streamed to
`ahi.device` using linked double buffers. Version 0.3.0 replaces the harsh
square/saw loop with a 128 BPM arrangement: A minor / F / C / G chords,
a bell-like lead, harmonic bass, filtered pads, kick/snare/hat and a cross-fed
stereo echo. The opening, timecode break and ending change the arrangement.
Oscillator tables are built once; the audio pump uses integer mixing. Each
buffer now holds 8192 frames to absorb rendering spikes.

## Visual polish (0.3.0 preview)

- Smooth sphere normals, warm key/cool fill lighting and white specular
  highlights give the Boing forms a studio-rendered appearance in every act.
- The Boing Event adds a soft height-dependent contact shadow and a restrained
  planar reflection. These are rasterised approximations, not runtime ray tracing.
- The field has bevel accents, a baked light pool and distance haze.
- 1200 warm/cool stars move in three depth/speed layers with sparse diffraction
  glints, over a dark blue sky gradient. Point draws are batched by layer.

The higher-detail meshes and lighting have a performance cost. See
[POLISH_RESULT.md](POLISH_RESULT.md) for exact build and target-test limits.


## Build

```sh
STOVE=~/AmigaChrome/stoves/os32-gcc16 \
    sh demos/opentensor/build.sh
```

Output:

```text
build/opentensor/OpenTensor-Beyond-First-Light-0.3.0.zip
```

## Run

```text
Stack 1000000
OpenTensor
```

The default is `960x540x32`, VirGL when available, a private OpenRTG screen and AHI audio. If that mode is unavailable the demo falls back to `640x360x32`.

Useful starts:

```text
OpenTensor START 64       ; Act II / timecode
OpenTensor START 78       ; tensor streams
OpenTensor START 96       ; form
OpenTensor START 114      ; real Intuition window breakout
OpenTensor START 130      ; beyond first light
OpenTensor START 152      ; final stack card
```

Options:

```text
CPU
WINDOW
W n H n
DEPTH 16|32
START seconds
SECONDS n
NOAUDIO
NOVSYNC
```

Controls:

```text
Esc or mouse   exit
Space          pause
F1             performance overlay
F2 / F3        next / previous scene
F4             restart current scene
```

## Presentation paths

A private screen sets `gla_os3_target.bitmap`, selecting GLA's screen-owned OpenGPU presentation path. `WINDOW`, and the built-in Window Breakout scene, set the target to an Intuition RastPort and deliberately use the layered-window compatibility path. The demo therefore provides a repeatable visual and performance regression test for the direct VirGL-to-window work.

## First-light baseline

The original 0.1.0 build completed an 18-second AC090 lab run at `960x540x32`, using Mesa 26.2.4 through VirGL and generated 44.1 kHz AHI audio. It returned cleanly to Workbench at 50.0 fps. See [FIRST_LIGHT_RESULT.md](FIRST_LIGHT_RESULT.md).

# OpenGPU stream v1.2 proposals

Status: draft implementation contract, 8 October 2026.

This records the v1.2 command surface from the approved "OpenGPU: one
library" design. It does **not** change `OGPU_STREAM_MINOR` yet. The stream
stays v1.1 until the CPU core implements every mandatory v1.2 command and the
golden/differential tests pass.

## Purpose

v1.2 adds the commands needed by the one-library OpenGPU design for SDL 2,
Warp3D, MiniGL and the native 3D path, while keeping the existing v1.1 2D,
A8-mask, composite and fence commands intact.

## Proposed commands

| Command | Opcode | Purpose |
| --- | ---: | --- |
| `FILL_BLEND` | `0x0018` | Fills using SDL-compatible blend modes |
| `LINES_BLEND` | `0x0019` | Lines using SDL-compatible blend modes |
| `POINTS_BLEND` | `0x001A` | Points using SDL-compatible blend modes |
| `COMPOSITE_AFFINE` | `0x0021` | Scaled, rotated and flipped copies with colour modulation; the `SDL_RenderCopyEx` case |
| `TRIANGLES` | `0x0030` | Lists, strips and fans; optional index list; per-vertex colour and texture coordinates; optional Z, W, specular/fog and a second texture unit |
| `YUV` | `0x0031` | I420, YV12, NV12, NV21, YUY2, UYVY and YVYU to RGB; BT.601, BT.709 or full range |
| `RENDER3D` | `0x0032` | Sticky 3D state: depth/stencil, blend factors, alpha test, fog, chroma key, logic op and colour mask |
| `TEXENV` | `0x0033` | Texture environment modes: REPLACE, DECAL, MODULATE, BLEND, ADD and SUB |
| `TEXTURE` | `0x0034` | Bind texture, filter, wrap and mip levels; generation number avoids re-uploading unchanged textures |
| `READBACK` | `0x0035` | Read depth, stencil or colour back into Amiga memory |

## Proposed formats

Add:

- `Z16`
- `Z32`
- `S8`

`FILL` also clears these surfaces. Texture upload input is ARGB32 or INDEX8;
front ends convert other source formats before issuing the stream.

## Raster rules fixed by the design

- pixel centres are at +0.5;
- triangles use the top-left fill rule;
- when W is present and perspective is enabled, all interpolated attributes,
  including colour, are perspective-correct.

## Compatibility

- v1.1 opcodes and wire encodings remain unchanged;
- unknown commands continue to be skipped by encoded command length and
  reported as `OGPU_ERR_BADOP`;
- `OGPU_Query` remains the per-command capability gate;
- a backend may answer `OGPU_PARTIAL`, with the CPU core completing the
  unsupported portion;
- 2D CPU/GPU output must match exactly; the design allows 3D GPU/CPU colour
  differences of +/-2 per channel, excluding edge pixels.

## Landing gate

Do not set `OGPU_STREAM_MINOR` to 2 until:

1. command layouts are frozen in `include/opengpu/stream.h`;
2. the integer CPU core implements every mandatory v1.2 command;
3. malformed-length and unsupported-format tests cover every new opcode;
4. golden stream tests are updated;
5. CPU reference pictures exist for 2D, YUV and textured/Z/fogged triangles;
6. at least one backend answers `OGPU_Query` honestly for every new command.

The implementation order from the design is:

1. ACRTG.gpu 2D parity;
2. library layout + stream v1.2 CPU core;
3. 3D rasteriser + Warp3D stub;
4. SDL 2 API in the library;
5. Mesa integration;
6. minigl.library;
7. demos and measurement;
8. fold the OpenGfx entry points into the one-library layout.

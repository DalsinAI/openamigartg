# OpenTensor Beyond First Light Result

Date: 12 October 2026  
Build: `OpenTensor Beyond First Light 0.2.0`  
Compiler: `m68k-amigaos-gcc 16.2.0b`  
Source branch: `demo/opentensor-first-light-20261012`  
Evidence workflow: `DalsinAI/amigachrome-gameports` run `38188416453`

The 164-second production was cross-built on the `daletop-amigachrome` self-hosted runner and exercised scene by scene in an archived disposable copy of Instance-32. The live Instance-32 installation was not modified.

## Platform result

Every Act II run reported:

```text
OpenTensor: renderer virgl (AMD Radeon RX 460 Graphics (radeonsi, polaris11, ACO...), driver virgl, OpenGL 4.3 (Compatibility Profile) Mesa 26.2.4
OpenTensor: 960x540x32 full-screen OpenRTG direct, AHI 44.1 kHz stereo, sequence 164 seconds
```

All six scene runs returned `RC 0`, used VirGL, and reported **zero AHI underruns**. Measured end-of-run rates were 49–50 fps.

## Scenes qualified

- `START 64` — The Timecode
- `START 78` — Across The Field
- `START 96` — Form
- `START 114` — Window Breakout, including a moving live Intuition window and the layered compatibility presentation path
- `START 130` — Beyond First Light
- `START 152` — The Stack Is Open

The evidence harness waits for the 960x540 RTG head before timing each capture, avoiding the cold Mesa startup interval where the old 320x240 black head can still be visible.

## Package

```text
OpenTensor-Beyond-First-Light-0.2.0.zip
SHA-256: 5386f13bb1510e24b3ccaf7f339eca6074f9053af67813992efeb94b93fc0cfc
```

The package contains a 319,120-byte AmigaOS loadseg-able executable, README, build information and SHA-256 manifest.

## Evidence hashes

```text
aa544483237a972de6d643d16d2fdf2e8374a146be9a26fdf0274d699d09c06c  opentensor-timecode.png
c7883a2b11ccf9e44ce3a45bcb4e444841f57b47013f83631761e4f30725587a  opentensor-streams.png
310adfb10d9e584e98a63d422973fc0fe156f1201e365cdff5f127ae36231c5b  opentensor-form.png
230f0c5ff3974235fd6097cd53a2e6b845819855f66af3dc3f955edfd8836714  opentensor-breakout.png
44b26195fe3af08f540e8e3e2a905815421c819c56a7995fb0f6a930c3eb3027  opentensor-beyond.png
06b96e538942df20015dac9a4b7ae01d1909d0c3ef1f45dceee5c8d57697dabc  opentensor-stack.png
eed6d4cc9eb6eb6554fe89f760452537e5db4f3199a1a3c2472847d02cc865d4  opentensor-after-tour.png
```

## Engineering value

OpenTensor now serves as both a production and a repeatable integration benchmark for:

- Mesa/GLA/VirGL rendering;
- private-screen direct OpenGPU presentation;
- layered Intuition-window compatibility presentation;
- OpenRTG mode switching and restoration;
- generated AHI stereo playback and clean shutdown;
- AC090 68040 + FPU performance.

The Window Breakout scene is intentionally retained as the visual acceptance test for future direct VirGL-to-window presentation: when that work lands, the same scene should remain visually identical while eliminating the 68k frame-copy path.

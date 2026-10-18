# OpenTensor First-Light Result

Date: 12 October 2026  
Build: `OpenTensor First Light 0.1.0`  
Compiler: `m68k-amigaos-gcc 16.2.0b`  
Source branch: `demo/opentensor-first-light-20261012`

The package was built on the `daletop-amigachrome` self-hosted runner and run for 18 seconds on an archived disposable copy of Instance-32.

## Guest result

```text
OPENTENSOR: starting
OpenTensor: renderer virgl (AMD Radeon RX 460 Graphics (radeonsi, polaris11, ACO...), driver virgl, OpenGL 4.3 (Compatibility Profile) Mesa 26.2.4
OpenTensor: 960x540x32 full-screen OpenRTG direct, AHI 44.1 kHz stereo
OpenTensor: 50.0 fps, 1 AHI underruns
OPENTENSOR: returned RC 0
```

The run proved:

- a GCC16 AmigaOS Hunk executable;
- Mesa 26.2.4 using the VirGL/OpenGPU path;
- a private 960x540x32 OpenRTG screen;
- generated 44.1 kHz signed 16-bit stereo through `ahi.device`;
- the live star-field scene and diagnostic overlay;
- clean return to Workbench with return code zero;
- clean lab shutdown and archival.

The first captured frame was 480x270 RGB PNG at half scale. The post-run capture was 960x540 RGB PNG and showed Workbench restored.

## Evidence hashes

```text
5ddd5b166ae453b42ebc2987b7ad5491ae946d73a27daa11d162477dade11841  opentensor-running.png
906d0079a18804a9f67991df45fc34959dc393fc75f2f5a529f9c28cca80b5ce  opentensor-after-run.png
ed585bb020328a50c77f22c3b53f13b60b04749c140e4839b85e9232515c5216  OpenTensor-First-Light-0.1.0.zip
```

## Follow-up

One AHI underrun occurred during the first run. It did not interrupt playback or shutdown, but the next audio pass should pre-roll a third buffer or start playback only after both queued buffers are ready.

The current private-screen presentation is the correct first-light path. The `WINDOW` mode remains a deliberate regression test for the layered-window presentation work.

# Restart: OpenRTG and OpenGPU

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

OpenRTG (openrtg.library, several monitors, Standard and All screen modes) and OpenGPU: the command stream, the shared C core and the Vulkan back end that put the host's or the Pi's graphics chip behind Amiga drawing.

## Where it stands

The Vulkan back end draws in the runtime's own video RAM (#15, 25274ad) and carries OGPU_OP_VIRGL for Mesa. On the home PC's RX 460, tools/gpu_check.sh passes all four video RAM modes in about a second each. opengpu.library 0.2 (stream v1.1) ships in OpenUp. The runtime side (ACRTG v3 ring) is amigachrome #213, still open.

## Merged lately

- #15 (25274ad, 2026-10-06): OpenGPU Vulkan: draw in the runtime's own video RAM (Pi 5 first)
- #16 (840a012, 2026-10-06): Credit who made OpenRTG: CONTRIBUTORS.md
- #14 (b115b0d, 2026-10-06): OpenGPU stream v1.1: A8 coverage masks for OpenGfx's text and clip paths
- #13 (d8b4a1c, 2026-10-06): OpenGPU G1 and G2 start: the stream, the shared core, opengpu.library 0.1 and the Vulkan back end
- #12 (0f0aa04, 2026-10-06): openrtg 0.10: Draw no longer runs for ever on shallow lines
- #11 (1bccb14, 2026-10-06): openrtg.library 0.9: the board's switch, so the instance window shows OpenRTG

## Open pull requests

- None.

## Next step

1. Merge amigachrome #213, then write ACRTG.gpu in amigachrome-guest.
2. Run gpu_check on the Pi 5.
3. Then gl.library and SDL_GL in openamigasdl.

## Waiting on @SacredTrees

- Set up Ubuntu on the Pi 5 and paste the gpu_check result.

## Who owns it

Open apps queue (OpenGPU); Main Discourse (OpenRTG builds).

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenGPU_Mesa_GPU_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)
- [`20261006_AmigaChrome_OpenApps_Queue_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.

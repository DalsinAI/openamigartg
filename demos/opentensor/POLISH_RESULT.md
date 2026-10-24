# OpenTensor 0.3.0 polish — 24 October 2026

This is a preview candidate, not a performance or audio qualification release.
The original score and procedural artwork require no downloaded assets.

## Changes

All solid Boing forms now use unit-sphere normals, normalisation under scaling,
colour materials and two fixed-function studio lights. Separate specular colour
keeps highlights white over the red checks. The higher-resolution mesh is
48 by 24 cells. The field bakes a cool pool of light and distance haze into its
vertices, adds narrow tile bevels, and the bounce scene composites a restrained
planar reflection with a soft contact shadow. It does not ray trace the scene.

The starfield has 1200 warm/cool stars, three sizes and forward-motion rates,
sparse diffraction crosses and a sky gradient. Three array draws replace
per-star immediate calls. The original geometric tunnel remains.

The new procedural score uses A minor / F / C / G chords at approximately
128 BPM, a harmonic lead/bass, filtered three-note pads, percussion and stereo
echo. Tables remove per-sample trigonometry; section timing avoids per-sample
64-bit division. AHI remains the backend. Larger linked buffers accommodate
rendering spikes; pause/seek semantics and the existing buffer-pump architecture
are unchanged. Seek starts a fresh synth state at that score position.

The icon generator now registers its dynamically imported writer in
`sys.modules`, needed by dataclass type resolution on the installed Python.

## Host checks

- `tools/preview_opentensor.py` extracts the actual Act I geometry/scene functions
  and renders four 960x540 frames with offscreen SDL/GL. All return no GL error
  on llvmpipe LLVM 21.1.8. This tests no Amiga display or AHI path.
- `tests/opentensor_score_test.c`, compiled with undefined-behaviour sanitisation,
  renders all 164 seconds: peak 20508/32767, no clipped samples, 7220293 frames
  differ between stereo channels. Arbitrarily split versus whole audio renders
  match byte for byte. These are signal checks, not a listening assessment.
- Target package built with GCC 16.2.0-open3, `-m68040 -m68881` and the
  `os32-gcc16` SDK/libGL. No SDL dependency was added to OpenTensor; SDL is only
  used by the optional host preview tool.

## Isolated 68k lab

Team 1's own lab at ports 8976–8979, muted. Actual runtime banner reports
`tuned build zen 20261023T202943Z`; ROM Kickstart 3.2.3 47.115, CRC b18d3b67.
This is not qualification on today's Open ROM or physical AC090 hardware.
The renderer reports Mesa 26.2.4 VirGL on AMD Radeon RX 460.

The initial 12-second bounce/storm run returned Shell RC 0, 24.5 final FPS and
2 AHI underruns. The subsequent batched-star/optimised-mixer 30-second run
through bounce/storm/title reported 19.0 final FPS and 1 underrun with the
older 4096-frame buffers. Target screenshots visibly show the new lit spheres.
These are different scene intervals, not a controlled speed comparison.

Final 8192-frame-buffer sequence result is recorded below.
The [deployment receipt](evidence/instance32-preview-20261024.json) records
all 11 verified files staged at `Instance-32 / DH1:OpenTensor-Preview`.
The original OpenTensor binary has the same hash before and after staging.
`Launch-New-Look` starts at 24 seconds; `Launch` runs the full production.
No live launch, restart or library replacement was performed. Audible output, sustained load, softpipe performance,
all resolutions and the live instance's own ROM/runtime combination remain
unqualified. AHI initialisation or a clean Shell exit is not proof of clean audio.

## Final build result

The exact staged executable (SHA-256
`a78acbe4ec58c3fddc46434d1092e7c16727c56a815a0b949f10ae93e257c83a`)
completed a 166-second timed lab run, through the entire 164-second sequence
and its loop boundary, with Shell RC 0 and **0 AHI underruns**. Its final
reported interval was 28.4 FPS; this is not an average or baseline comparison.
A screenshot confirmed the lit ball in the real Intuition breakout window;
diagnostic text there appeared distorted and needs comparison with baseline.
The lab was muted, so no audible-quality pass is claimed.

The [raw result](evidence/polish-lab-full-20261024.txt) records renderer and
mode. The final buffers are 8192 frames. No result here establishes performance
on today's Open ROM or the user's live instance.

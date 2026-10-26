# OpenTensor 0.3.1 — yielding field and ethereal light

24 October 2026. Follow-up to the 0.3.0 preview in PR #118.

## Creative direction

The user supplied these visual references:

- [Sapphire & Steel](https://www.youtube.com/watch?v=qNbca1Stzms&t=52s),
  approximately 0:52–1:32. Browser-inspected frames showed a curved luminous
  grid, receding rectangular frames, moving light and elemental forms against
  sparse stars. The first unrelated embed was blocked; this supplied link played.
- [Buck Rogers](https://www.youtube.com/watch?v=3uEVDdFLBDw), used as a
  space/optical-light reference. Browser-inspected frames included the space
  montage and optical flares. No audio-analysis or complete viewing claim.

The resulting geometry and glow texture are original, generated in code.
No footage, theme, narration, logos or other show assets are bundled. The
0.3.0 original procedural music remains unchanged, as requested.

## Animation and rendering

- A 2.2-second bounce cycle has a 0.45-second yielding contact phase, then a
  parabolic rebound. At maximum contact the field centre dips 0.9 world units;
  the ball's centre follows it. Radial damped waves propagate across the actual
  checkerboard geometry. This is authored spring-like motion, not a numerical
  simulation of a coupled ball/membrane system.
- A cached radius index per ground vertex and a 512-entry height table avoid
  per-vertex trigonometry. The grid has a vertex at the contact centre. Bevels
  follow deformation, and the following ground scenes restore the flat mesh.
- The contact shadow follows the central dip. The earlier planar reflection
  is omitted during compression because the local surface is no longer planar.
- A procedural 32x32 soft glow texture supplies starlight halos, white-hot
  centres, sparse optical flares and light ribbons. The 720-star field is
  distributed more evenly in the initial view, with three depth layers and
  mostly white cores. It replaces the denser coloured-dot appearance.
- Storm bends and recedes through rectangular dimensional frames, with warm
  travelling lights and restrained drifting ribbons. The Act II streams move
  more slowly, with soft luminous widths and nodes. Form uses drifting light
  loops and a slow fade into a solid Boing sphere rather than wire cubes.
- The diagnostic overlay starts hidden; F1 toggles it. The context-owned glow
  texture and the ground's radius cache are released at shutdown.

## Validation

The host preview extracts the actual rendering functions, including Act II.
Eight scene renders on offscreen SDL/OpenGL return no GL errors under
undefined-behaviour sanitisation. Assertions check maximum-contact height,
airborne rebound, finite deformed vertices and restoration of the flat field.
A host-rendered GIF records one complete contact/rebound cycle. These checks
are not an Amiga display or performance qualification.

The target package builds with GCC 16.2.0-open3, 68040 + FPU, with no compiler
warnings. This follow-up does not change the score; the 0.3.0 DSP evidence
remains relevant to that unchanged code.

Target lab and Instance-32 installation results follow below. The test lab
uses the older Kickstart 3.2.3 47.115 / runtime banner 20261023T202943Z recorded
in POLISH_RESULT.md. Do not interpret it as today's Open ROM qualification.

## Final target and staging result

The final 0.3.1 executable SHA-256 is
`4ead44ef9a053754c47628c220748c8a3084c6a57ad49647ac02145b47e5f687`.
It completed a 166-second isolated lab run through the sequence and loop
boundary: **Shell RC 0, zero AHI underruns**, final reported interval 50.0 FPS.
That final interval is not a whole-sequence average or a controlled baseline
comparison. The lab was muted; no listening qualification is claimed.
The updated starlight/timecode and later orbiting-ball scenes were visibly
observed. [Raw result](evidence/ether-lab-full-20261024.txt).

At the user's request, 0.3.1 is staged in `DH1:OpenTensor-Preview` on
Instance-32. All 15 files were verified, the 0.3.0 preview was moved into
the instance archive, and the original OpenTensor executable remained
byte-identical. Launch-New-Look, Launch-Storm and Launch-Form provide direct
starts. [Receipt](evidence/instance32-ether-preview-20261024.json).
No live launch, restart or platform-library replacement was performed.

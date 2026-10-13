# SDL2 text input - Open2 merge qualification

13 October 2026 UTC. Source main 714636f plus PR67, integration code commit
828a8ccd93705d7edd6f385d39136fcd3f1851e4. This adds the checked-in text patch,
shared/static test targets and helper regression tests to the current graphics
branch. It is not a full game release or a rollout to live instances.

## New correction

The Amiga modifier scancodes inherited from the backend were reversed:
0x64/0x65 are Alt, and 0x66/0x67 are the Amiga keys (SDL GUI). Patch 0003
corrects the four entries and gates raw-key delivery on the owning SDL focus
window, preventing delayed inactive-window input from re-establishing a key
that SDL released during focus loss. SDL's existing focus reset is retained.

Primary reference: the AmigaOS Keymap Library raw-key table:
https://wiki.amigaos.net/wiki/Keymap_Library

## Built and checked

The actual OpenAmigaGCC 16.2.0-open2 archive-backed stove built shared
SDL2.module, its link stub, static SDL, shared/static SDLTextCheck and the
satellite libraries. Target HUNK identities are in results.json. The base-relative
and FPCR checks completed successfully. Full log hashes and actual tail output
are retained. Four host tests pass; the helper-body harness reports 30 checks.
Build concurrency can be bounded with JOBS; a push no longer starts the legacy
self-hosted build with an implicit old stove. Manual target CI requires the
explicit OPENAMIGAGCC_OPEN2_PREFIX repository variable. Hosted CI checks the
actual committed patch series, diagnostic syntax and helper contracts.

## Actual guest tests

A new canonical copy of an owned, stopped AmigaOS 3.2/AC090 lab was used, muted.
No live instance was modified. Existing opengpu.library, GL and OpenInput were
reused; this is not a complete graphics-stack or operating-system qualification.

Both shared and static SDLTextCheck produced the exact text sequence:
`61 41 31 C2 A3 C3 A9 C3 84 7A` (a, A, 1, pound, e-acute, A-diaeresis, z).
MapANSI was queried on this guest to obtain its actual default-keymap dead-key
sequences. No keymap or profile data was injected to manufacture a pass.
Ctrl/Amiga shortcuts and non-text navigation/edit keys produced no text.
Text stopped/restarted through SDL's own F2 diagnostic toggle. Both diagnostics
returned 0, and now identify left/right Amiga as left/right GUI, not Alt.

The held-Shift focus test observed a real second-window focus loss, the forced
Shift release, no text into the unfocused SDL window, focus return and an
unshifted a with modifiers clear. The diagnostic returned 0.

AstroMenace 1.4.3, using the existing copied game binary (identity recorded),
accepted raw keyboard DALEX, edited to DALE with Backspace, and created the
pilot. After normal quit and relaunch, the mission menu showed Pilot Profile:
DALE. Both launch commands returned 0. The profile is binary encoded; its raw
file does NOT contain an ASCII DALE substring. Persistence is supported by the
actual relaunched UI capture, not a false substring assertion.

## Remaining limits

These are default-keymap examples, not exhaustive international-layout testing.
Audio remained muted and was not qualified. No full game mission, controller,
performance benchmark or pristine-OS repetition is claimed. The game data,
profile binary, proprietary OS and NDK are not committed. Only raw logs,
metadata, small test sources and actual guest screenshots are included here.
Historical gameports evidence remains unchanged; it describes older candidates.

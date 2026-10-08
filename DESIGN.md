<!-- OpenRTG's design, written in AmigaChrome's development tree on 4 October 2026. Where it mentions AmigaChrome's instances (Instance-23, the scratch copy), its capsule shelf or its runtime, those are the machines and records it was built and measured on. Copyright (c) 2026 Dalsin Limited, MIT licence (LICENSE). -->

# OpenRTG

Our own RTG system for AmigaOS 3.x: retargetable graphics, Warp3D and
several monitors, built for AmigaChrome's ACRTG boards and open source.

Team, 4 October 2026: "Build our own rtg library drivers and preferences app.
Support for warp3d and multi monitors as standard." His decisions the same
day:

- The product is **OpenRTG** (Our naming: products drop "Amiga", repos
  carry it, so the repository is `DalsinAI/openamigartg`); the library is
  **`openrtg.library`**, its own
  name, so it can sit beside an installed Picasso96. One of the two is active
  at a time.
- **Two RTG monitors and the AGA chipset** are standard on an instance; more
  can be switched on, up to four RTG monitors.
- It also answers to **`cybergraphics.library`** and
  **`Picasso96API.library`**, so existing programs work.
- Windows and prefs are GadTools (OS 3.x applications use GadTools or MUI).
- **It must work on real Amigas and on PiStorms**, not only in AmigaChrome
  (Team, 4 October 2026). See "Real Amigas and PiStorm" in section 3.
- **MIT licence, with the credit kept** (Team, 4 October 2026: "I want anyone
  to be able to run with it, fork it etc"; "credit to us though"). Copyright
  Dalsin Limited; the notice travels with every copy and fork, and forks are
  asked to say they are based on OpenRTG.
- Later the same day: "a patch or intuition library that gives the windows on
  Workbench etc more of an OS 4 feel", which "should become resident after it
  loads", "a clean OS compatible change and leveraging RTG", "with a prefs app
  written in GadTools to unify the screen mode, colors etc". That is the
  window look (section 8) and the one prefs app (section 9).

Earlier decisions it builds on: OpenRTG approved on 2 October, starting with
a spike that measures what Workbench, MUI and a game call; ACRTG, the board, from the 28 September design
(`capjumps/20260928_AmigaChrome_ACRTG_DualMonitor_Capsule.zip`): the RTG
monitors sit beside the AGA chipset, never switched into one picture.

## 1. The parts

| Part | Where | What it does |
| --- | --- | --- |
| ACRTG boards | the runtime (`native/acrtg.c`) | One Zorro III board per RTG monitor: video RAM, modes, blits, the pointer, and (protocol v3, 64 MiB) one command ring for 2D and 3D, drawn by the runtime's rasterizer. |
| `ACRTG.card`, `ACRTG.chip` | `LIBS:Picasso96/`, and `Kickstart/` on OS 4 | The board's driver, split and named as OS 4.1's are (section 3), so Picasso96, OpenRTG and OS 4 use the same one. |
| `openrtg.library` | `LIBS:` | The RTG system: finds the boards, puts their modes in the display database, opens screens on them, draws on RTG bitmaps with the boards' blitter, one pointer per monitor. |
| `C:OpenRTG` | `S:Startup-Sequence` | Starts OpenRTG before Workbench when it is the chosen RTG system, and puts the compatibility libraries in the library list. |
| `cybergraphics.library` | `LIBS:OpenRTG/` | The CyberGraphX API (GetCyberMapAttr, LockBitMapTagList, Read/Write/FillPixelArray, BestCModeIDTagList and the rest) over `openrtg.library`. |
| `Picasso96API.library` | `LIBS:OpenRTG/` | The Picasso96 API (p96AllocBitMap, p96GetBitMapAttr, p96OpenScreenTags, p96LockBitMap, p96WritePixelArray, p96BestModeIDTags and the rest) over `openrtg.library`. |
| `opengpu.library` | `LIBS:`, drivers in `LIBS:OpenGPU/` | One acceleration layer for 2D, compositing, 3D and batched maths (section 5): chip drivers (`ACRTG.gpu`, `AGA.gpu`, later VideoCore and real cards), CPU fallback per operation. On AmigaChrome its commands run on the host. |
| `Warp3D.library` | `LIBS:OpenRTG/` | The Warp3D V4 API (exported as V5's table, section 5), part of every install, over OpenGPU through `W3D_OpenGPU.library`. |
| AmigaChrome SDK, Amiga half | ACBuild's stoves, and an archive | Headers, FD and SFD files, autodocs, link libraries and examples for `openrtg.library` (with the OS 4-named calls and the `os4` header), the Warp3D driver interface, ACNet and `accontrol.device`. It sits beside the OS's own kit (NDK 3.2, the AROS SDK, the OS 4.1 SDK) and never replaces it. |
| The window look | in `openrtg.library` | The OS 4 feel for windows and screens: title bars, frames and border gadgets, on AGA and RTG screens alike. Resident once loaded. |
| `OpenRTG` prefs | `SYS:Prefs/` | One GadTools editor for monitors, screen modes, colours and the window look, plus the pointer and 3D settings. `ENVARC:Sys/openrtg.prefs`, and the OS's own prefs files for what the OS already has. |

The compatibility libraries and Warp3D live in `LIBS:OpenRTG/` and are added
to the library list by `C:OpenRTG`, so files Picasso96 installed in `LIBS:`
stay where they are and are simply not used while OpenRTG is active.

## 2. Several monitors

Each RTG monitor is its own ACRTG board, as each Picasso96 card is its own
monitor on a real Amiga: manufacturer Dalsin $DA15, product 9, serial number
the monitor's number. Boards are independent (their own 16 MiB window, video
RAM, mode, pointer); a bitmap belongs to one board.

- **In the machine:** the runtime fits as many boards as the instance's
  Hardware asks for (two by default). Head 0 is AGA, heads 1 to 4 are the
  RTG monitors.
- **In Cradle:** each head is a window (or a tab of the instance window). A
  click in a monitor's window brings that monitor's front screen forward, so
  the pointer and keys go to it.
- **In the Amiga:** each board is a monitor in the display database with its
  own ModeIDs (`OpenRTG.1`, `OpenRTG.2`, ...). Each monitor shows the
  frontmost screen that is open on it. A screen does not move between
  monitors (the formats differ; classic Intuition does not do that drag).
- **Picasso96 too:** `ACRTG.card` claims the next unclaimed board, so
  Picasso96 can drive two boards with two monitor files. This is the first
  step and is testable before OpenRTG exists.

### AGA as a pseudo card

Team, 4 October 2026: "AGA should exist as pseudo cards on A1200s". On an
A1200 the chipset is monitor 0, listed and handled like the ACRTG boards:

- **In Cradle's Hardware:** the displays list starts with "AGA (built in)",
  a card that cannot be taken out, with the same display choices as the RTG
  cards (in the instance window, a window of its own), and monitor 1's
  pass-through as where it goes when it shares a picture.
- **In OpenRTG:** a pseudo card driver, `AGA.card`, puts the chipset in
  the same monitor list as the boards. So the prefs' Monitors page, the
  monitor layout, the pointer crossing from monitor to monitor and the
  Workbench monitor choice treat it as one more monitor. Its native modes
  stay graphics.library's own and are never patched.
- **RTG modes on AGA too:** the pseudo card can offer chunky 8-bit screens
  (and true colour through HAM8) on the chipset, as CyberGraphX's AGA driver
  once did, so a program written for RTG opens on monitor 0 as well. On our
  machine the runtime converts chunky to planar as the board's blitter does;
  elsewhere the CPU does it.
- **The blitter as a crude 3D chip** (Team, the same day). `AGA.gpu` is
  OpenGPU's driver for the blitter (section 5), and Warp3D reaches it
  through `W3D_OpenGPU.library` on monitor 0. Triangles are drawn into the
  bitplanes with the blitter's line mode and area fill, flat-shaded, sorted
  back to front instead of a Z-buffer, with textures done by the CPU. So a
  Warp3D program runs on AGA too, slowly but with the real chip, as 1990s
  demos drew their filled vectors. It works on a real A1200 as well as ours.

### Pass-through: one monitor for AGA and RTG

Team, 4 October 2026: "pass through, have a look at recent PiStorm with AGA
emulation". What the PiStorm world does now:

- **Framethrower** (a board in the Denise socket) feeds the native picture
  into the Pi through its camera connector, so one HDMI cable carries both
  and Emu68 switches between RTG and native by itself.
- **RGB2RTG** (an Emu68 fork, prototype, September 2026) needs no extra
  hardware. It reads what the CPU writes to chip RAM and the custom
  registers, and a spare Pi core redraws the native picture line by line in
  24-bit colour inside the RTG output. HDMI is locked to the Amiga's beam
  (49.92 Hz PAL), with sharp or smooth scaling, cropping and a sync switch.
- **PiStorm-AGA-HAM6** runs a whole AGA chipset in software on a spare core,
  giving an A500 AGA games on HDMI. It is GPL-2.0, so it is a reference only.

Our runtime already draws the AGA picture itself, so the pass-through needs
no capture. For monitor 1 set to pass-through:

- **One picture, switched by the board.** The runtime puts the AGA picture
  or the RTG picture into monitor 1's frame, following the card's switch
  (SetSwitch, and under OpenRTG the front screen), so the page never
  changes layout and the switch is seamless.
- **Locked to the beam.** The switched head is sent at the chipset's own
  rate (50 Hz PAL, 60 Hz NTSC) with the timed frames, so scrolling stays
  smooth across the switch.
- **Scaling and crop**, sharp or smooth, from the same menu as the
  overscan button.
- **Further than real hardware:** with compositing (section 7) the AGA
  picture becomes one surface on monitor 1. An AGA screen dragged down shows
  the RTG screen behind it, and the other way round, which a real
  pass-through cable never could.

Only monitor 1 takes the pass-through; the others always have pictures of
their own.

## 3. The drivers, named and split the OS 4 way

Team, 4 October 2026: "make sure we can benefit from the driver too, like
OS 4 is more AmigaChrome friendly straight out of the box, and use similar
to OS 4 naming, i.e. .card". OS 4.1 splits a graphics driver into a bus part
and a chip part: `Kickstart/PCIGraphics.card` finds the boards and
`ATIRadeon.chip`, `RadeonHD.chip` or `siliconmotion502.chip` drives the
graphics chip. Warp3D has `LIBS:Warp3D/HWdrivers/W3D_<chip>.library` for the
chip and `LIBS:Warp3D/GFXdrivers/W3D_Picasso96.library` for the graphics
system. These come from the OS 4.1 Final Edition updates we already hold.
Picasso96 on OS 3 uses the same `.card` and `.chip` model. ACRTG's drivers
follow it:

| File | Part | Built for |
| --- | --- | --- |
| `ACRTG.card` | Zorro III: finds the ACRTG boards, one per monitor, and their memory | OS 3 (Picasso96 and OpenRTG), OS 4.1 Classic, AROS |
| `ACRTG.chip` | The board's registers: modes, blits, the pointer sprite, the command ring | The same source for 68k and PPC: OS 3, OS 4.1, AROS |
| `DEVS:Monitors/ACRTG` | The monitor file: one per board, with the board number | OS 3 and OS 4 |
| `ACRTG.gpu` | OpenGPU's driver for the board: the command ring | `LIBS:OpenGPU/`, OS 3 and OS 4 |
| `W3D_OpenGPU.library` | Warp3D's hardware driver for any chip OpenGPU drives (section 5) | `LIBS:Warp3D/HWdrivers/`, OS 3 and OS 4 |
| `W3D_OpenRTG.library` | Warp3D's graphics system driver for OpenRTG's bitmaps | `LIBS:Warp3D/GFXdrivers/`, OS 3 |

- **One driver everywhere.** OpenRTG loads `.card` and `.chip` drivers
  through the same board interface (BoardInfo) that Picasso96 and OS 4.1
  use. So `ACRTG.chip` is one driver for Picasso96, OpenRTG and OS 4, and
  OpenRTG can load other boards' drivers later.
- **OS 4 out of the box.** On the Sam460 board, ACRTG becomes a PCI device
  with the same registers. OS 4.1's own `PCIGraphics.card` finds it and our
  PPC `ACRTG.chip` drives it, so OS 4 gets AmigaChrome's monitors, pointer
  and fast blits with nothing but our driver. Still needed: a PCI vendor and
  device ID to present.
- **Warp3D the same way.** `W3D_OpenGPU.library` sits where Warp3D looks for
  hardware drivers, so our `Warp3D.library` and `W3D_Picasso96.library`
  find it. Whether OS 4's own Warp3D can load it depends on Warp3D's driver
  interface, which is not public; until then OS 4 gets its 3D through ours,
  and Warp3D Nova (`W3DN_OpenGPU.library`) comes later.
- **Today's `acrtg.card`** (one file, Picasso96 on OS 3) becomes
  `ACRTG.card` plus `ACRTG.chip`. The Installer replaces it, and the
  uninstaller puts the old one back.

**Porting from OS 4** (Team, the same day: "ideally so it makes porting
from OS 4 easier"). OS 4.1's graphics.library has the RTG calls OS 4
programs use; on OS 3 nothing does. `openrtg.library` offers them with
OS 4's names, arguments and tag values, so OS 4 code needs only a
recompile:

- **Calls:** AllocBitMapTags, LockBitMapTags and UnlockBitMap, BltBitMapTags,
  CompositeTags, RectFillColor, WritePixelColor and ReadPixelColor,
  WritePixelArray and ReadPixelArray, SetRastColor, GetBoardDataTags and
  GetMonitorDataTags, and SetRPAttrs's true-colour pens (RPTAG_APenColor and
  the others). The list is taken from the OS 4.1 Final Edition SDK
  (`interfaces/graphics.h`).
- **Headers:** our own `openrtg/` headers declare them with OS 4's tag
  values, which are the interface. Hyperion's header files are not copied.
  An `os4` header lets `IGraphics->CompositeTags(...)` compile on 68k as a
  call to `openrtg.library`.
- **The rest of the stack matches too:** Warp3D (section 6), MiniGL later,
  the iconify gadget (OS 3.2 has it), and the window look.
- **And the other way** (We: "or ports to OS 4"): a program written for
  OpenRTG on OS 3 uses the same calls OS 4's graphics.library has, so
  moving it to OS 4 is a recompile. On OS 4 the `openrtg/` header sends the
  calls to `IGraphics`. Our own applications use these calls, so they move
  to OS 4 the same way.

### Real Amigas and PiStorm

OpenRTG is an Amiga product first. AmigaChrome's boards make it fast, but
nothing in it may need them:

- **Other boards' drivers.** Because `openrtg.library` loads Picasso96
  `.card` and `.chip` drivers through the BoardInfo interface, the drivers a
  real Amiga already has work under it: Picasso II and IV, CyberVision 64,
  ZZ9000, on a PiStorm Emu68's VideoCore RTG driver, and the rest that have
  one: the Vampire's SAGA, MiSTer's Minimig, UAE's `uaegfx.card`. A board's drivers
  are found from its monitor file, as Picasso96 finds them.
- **No host needed.** Everything ACRTG does on the host (blits, the
  command ring, COMPOSITE, the rasterizer) also has a 68k path: BoardInfo's
  `...Default` routines for Picasso96's 2D, and OpenGPU's CPU code for
  everything OpenRTG draws, 3D included. It is slow on a real 68030 but quick on a PiStorm, where Emu68
  runs 68k code at hundreds of MIPS. Chip drivers come later (VideoCore's
  3D on the PiStorm is the first worth doing).
- **Early start without our ROM.** On a real Amiga `C:OpenRTG` comes first
  in `S:Startup-Sequence`, and OS 3.2's `LoadModule` makes it reset-resident
  so warm resets start it before DOS.
- **The look, the AGA pseudo card and the blitter's 3D** need only the
  chipset and the OS, so they work on any A1200 or A4000. Pass-through on
  a real card is the card's own switch, which SetSwitch already drives.
- **CPU:** 68020 or better, as Picasso96 needs; a PiStorm with Emu68 counts
  as a 68040.
- **Testing:** our runtime first, then real hardware. We or testers run a
  phase on a real Amiga with a Picasso96 card and on a PiStorm before it is
  called done there.

## 4. openrtg.library

On OS 3.x the display database, screens and drawing belong to graphics.library
and intuition.library, which know only the chipset. OpenRTG does what
Picasso96 and CyberGraphX do: it adds the boards' modes and takes over the
calls that touch RTG bitmaps, passing everything else to the original code.

- **Display database:** NextDisplayInfo, FindDisplayInfo, GetDisplayInfoData,
  ModeNotAvailable, BestModeIDA and GetVPModeID answer for the RTG ModeIDs as
  well, so ScreenMode prefs, the ASL screen mode requester and programs see
  the monitors. Phase 0 found these the busiest calls of all (MultiView asked
  about 3,800 times in 30 seconds), so the answers come from a table built
  once per monitor, never worked out per call.
- **Standard and All modes** (Team, 4 October 2026: "display modes should be
  reported in our next prefs as standard and all, standard being the popular
  PC modes"). Each monitor offers either:
  - **Standard** (the default): the popular PC resolutions the board can show,
    each in 8, 16 and 32-bit: 640x480, 800x600, 1024x768, 1280x720,
    1280x800, 1280x1024, 1366x768, 1440x900, 1600x900, 1680x1050, 1920x1080
    and 1920x1200. With protocol v3's 64 MiB boards, 2560x1440 and 3840x2160
    join them.
  - **All**: every mode the board can show, including the old Amiga-shaped
    ones (320x200, 640x256, 1024x384 and the like) that some programs and
    games ask for by size.
  The Standard list keeps the ScreenMode list and the ASL requester short and
  familiar, where Picasso96's default list for ACRTG runs to dozens of odd
  sizes and doubles with a second monitor. A program that asks for a size
  not in the list (BestModeIDA) still gets the nearest mode the board has,
  so nothing that worked before stops working.
- **ModeIDs** (`library/modes.c`, 4 October 2026):
  - **Monitor part:** monitor n's ModeIDs share `0x500n1000`, so
    graphics.library's `MONITOR_ID_MASK` keeps them on one monitor.
  - **Low twelve bits:** the size's place in one master table times four,
    plus the format: 8-bit CLUT, 16-bit or 32-bit.
  - **Stable IDs:** a ModeID means the same size and depth under Standard and
    All, so saved prefs stay valid when the choice changes. Sizes are only
    ever added at the end of the table.
  - **Lookup:** FindDisplayInfo answers for every mode the board shows,
    listed or not, so a saved All-only ModeID still works while Standard is
    chosen. NextDisplayInfo walks only the listed ones.
  - **Picasso96:** OpenRTG and Picasso96 are never active together, so their
    ModeIDs never meet.
- **Screens:** OpenScreen on an RTG ModeID allocates its bitmap in the board's
  video RAM; LoadView, MakeVPort, MrgCop, ScrollVPort, ChangeVPBitMap and
  ScreenToFront show the right screen on each board and leave the chipset to
  the native screens.
- **Bitmaps:** AllocBitMap with an RTG friend or format gives a chunky bitmap
  in video RAM (CPU memory when the board is full); GetBitMapAttr reports it.
  RTG bitmaps are marked as CyberGraphX and AROS mark them (no planar planes),
  so programs that check for RTG before poking planes keep working.
- **Drawing: OpenGfx owns the patches, OpenRTG provides the RTG side**
  (decided by the Team, 8 October 2026; amigachrome
  `docs/design/native-stack/Design-OS323-Platform-Integration.md`, section 4,
  and `Design-Graphics-OpenRTG-OpenGfx-OpenGPU.md`). `opengfx.library` owns
  every graphics.library drawing and text patch: BltBitMap,
  BltBitMapRastPort, BltMaskBitMapRastPort, ClipBlit, BltClear, BltTemplate,
  BltPattern, RectFill, SetRast, Draw and PolyDraw, area fills, WritePixel and
  ReadPixel, the pixel line and array functions, Text, TextLength,
  TextExtent, TextFit and ScrollRaster, drawing text with OpenFont's glyphs
  and metrics. OpenFont patches nothing. One owner per call means two
  patches never chain on the same function.
  OpenRTG stops patching those calls and becomes the RTG provider OpenGfx
  calls: for a bitmap that is OpenRTG's, OpenGfx asks `openrtg.library` to do
  the work (fill, copy, template, line, invert, planar-to-chunky as OpenGPU
  commands, or CPU code on the chunky bitmap) through a provider interface
  `openrtg.library` exports; planar bitmaps go to the original
  graphics.library as before. OpenRTG keeps everything that is not drawing:
  its screens, bitmaps, display database, monitors, the pointer and the board
  drivers.
  **The way there:** `openrtg.library` 0.10 (in OpenUp, off unless picked)
  still patches these calls itself, in `library/screens.c`. That stays until
  `opengfx.library`'s glue is built (the native stack roadmap's close of M0).
  Then OpenRTG's drawing patches become pass-through, since patches are never
  taken out (section 4's patching rules), and the same code is reached through
  the provider interface instead. A machine with OpenRTG and without OpenGfx
  draws on the CPU through the original graphics.library: correct, slower.
- **The pointer:** each monitor's front screen gets the board's hardware
  sprite (acrtg-v2), which Cradle shows as the PC's cursor.
- **Boards:** a small driver interface (find, init, mode, pan, fill, copy,
  template, line, sprite, 3D) with the ACRTG driver built in.

**OS-friendly patching** (Team, 4 October 2026: "it must be an OS friendly
patching solution"). Every patch OpenRTG makes, for RTG and for the window
look, keeps these rules:

- Public library functions only, replaced with `SetFunction()` under
  `Forbid()`, with the caches cleared after. No code in ROM is searched for or
  changed, and no private structure field is written. Public fields (a
  layer's window, a window's border rastport) are only read.
- Each patch keeps the vector `SetFunction()` returned and passes every call
  that is not its own to it. It never assumes it is the first or the last
  patch on that function, so SaferPatches, MCP-style tools and other
  patchers keep working.
- Patches are never taken out, because something may have chained after
  them. Switched off, they pass every call straight through.
- The patch code lives in `openrtg.library`'s own segment, which stays in
  memory, never in a program that exits.
- Patched functions run on the caller's stack and task (input.device's
  among them): they are re-entrant, use little stack, and never call DOS or
  `Wait()`.
- Where the OS offers a way in, it is used instead of a patch: sysiclass's
  dispatcher for the images, the display database's own calls for the modes,
  the prefs files IPrefs already reads. OS 3.2's intuition has an API for
  Picasso96 (IntuitionControlA, screen dragging), but it is private; OpenRTG
  uses it only if its documentation can be had.

**Starting early** (Team, the same day: "with early RTG enabling as
possible"). The goal is that the boot shell and Workbench open on the RTG
monitor from the first moment. Today Workbench opens on AGA and moves to RTG
when LoadMonDrvs and IPrefs run; OpenRTG should not make that switch.

1. **Before DOS, from the ACRTG board's own ROM.** The first board's boot
   ROM already carries a RomTag (RTF_COLDSTART) that Kickstart 3.2.3 and AROS
   run before DOS. Today it makes `acrtg.card` resident with
   `InitResident()`, exec's public way to start a module, so Picasso96 needs
   no disk for it. The same RomTag starts `openrtg.library` and its driver.
   Only the first board has the ROM, so it runs once. The mode at boot is
   the one OpenRTG prefs last saved. The board keeps it, because `ENVARC:`
   cannot be read before DOS; the runtime holds it per instance, as a
   monitor keeps its settings.
2. **Without the board's ROM** (a board fitted without one): `C:OpenRTG` comes
   first in `S:Startup-Sequence`, before LoadMonDrvs and IPrefs. It can also
   make the library reset-resident with OS 3.2's `LoadModule`, so after a
   warm reset it starts before DOS too.
3. **AROS** has its RTG system in ROM already, so the driver in the ACRTG
   ROM is found at boot.

Still to measure on the copy: how the Workbench screen's first mode can be
the RTG one without intuition's private calls.

**Boot to RTG, as a resident option** (Team, 4 October 2026: "OpenRTG should
have a resident option to boot to RTG", and "that could be a real win and
reason for folks to move into our ecosystem"):
- **The command:** `C:OpenRTG RESIDENT` makes `openrtg.library` reset-resident
  with OS 3.2's `LoadModule`; `C:OpenRTG RESIDENT OFF` takes it out again.
- **On boards with the ACRTG ROM,** nothing is needed: the board's RomTag
  starts OpenRTG before DOS.
- **Either way,** the boot shell and Workbench come up on the RTG monitor from
  the first moment, with no AGA screen first and no flick at LoadMonDrvs.
  That is a first impression people notice, and a reason to choose OpenRTG.
- **It needs the display database (done in 0.2) and OpenRTG's own screens**
  (the rest of phase 2).

The first measurement decides the order: which of these Workbench, a MUI
program and a game call, and how often (Phase 0).

## 5. OpenGPU: one layer for drawing, compositing, 3D and compute

Team, 4 October 2026: "Is there value in offering an opengpu.library that
lets drawing functions go via the graphics chip's GPU if it has one? A
wrapper for Warp3D, but I suspect a better unifier." Then: "For us it allows
easy speed up of the UI without emulation. Yes, let's adopt. We can create
[driver] cards for PiStorm 3D etc later." And: "It should allow a maths
library to call OpenGPU."

`opengpu.library` is the acceleration layer everything in OpenRTG draws
through. On AmigaChrome its commands are carried out by the host, so
Workbench and every program speed up without the CPU emulating drawing loops.

- **One command model**, in batched command buffers (few trips across Zorro
  or to the host), with fences:
  - **2D:** fill, copy, template (text), pattern, line, invert,
    planar-to-chunky, pixel arrays in and out with format conversion;
  - **compositing:** alpha, scaling, filtering and format conversion
    (COMPOSITE);
  - **3D:** states, textures, triangles, strips, fans, lines, points,
    Z-buffer, stencil, clears;
  - **compute:** batched maths, such as vector and matrix transforms (3D
    transform and lighting), FFT and DSP for audio, convolution and image
    filters, colour conversion, in single and double precision. A maths
    library hands OpenGPU batches. Single calls (one sine, one multiply)
    stay on the FPU, which is faster than any trip to a GPU, and under the
    AC090 JIT already native.
- **Surfaces** are described once (chunky in video RAM, CPU memory, AGA
  planar), so every operation knows what it draws into.
- **It says what it can do:** for each operation and destination format,
  fully, partly or not, as Warp3D's queries do (section 6).
- **It falls back per operation.** What the chip cannot do, OpenGPU's own
  CPU code does, so a program never needs two code paths.
- **Drivers**, one per chip, in `LIBS:OpenGPU/`:
  - `ACRTG.gpu`: the board's command ring (protocol v3), carried out by the
    runtime's rasterizer, and later by the PC's GPU behind the same commands;
  - `AGA.gpu`: the blitter (fills, copies, lines, area fill), the "crude 3D
    chip" of section 2;
  - the CPU, built in;
  - later, as we said: `VideoCore.gpu` for the PiStorm's 3D, and drivers
    for real cards' chips (Permedia2, ViRGE, Voodoo) where their documents
    allow. Linux's drivers are GPL, so they are references only; Mesa's
    are MIT and may be used, keeping their notices.
- **Who calls it:** `openrtg.library`'s drawing on RTG bitmaps (the busy
  calls phase 0 measured), CompositeTags and the MorphOS alpha calls, the
  window look's gradients and shadows, Warp3D through one hardware driver,
  `W3D_OpenGPU.library`, for every chip with an OpenGPU driver, MiniGL, and
  maths libraries.
- **For programs:** the 2D, compositing and compute batches are public, being
  what OS 3 lacks; our own applications use them first. 3D programs use
  Warp3D and MiniGL (classic games), or Mesa's OpenGL and GLES for ports
  (Team, 4 October 2026: SDL 2, SDL 3 and Mesa "that lean into our open
  capabilities"). Mesa runs on OpenGPU too: on AmigaChrome its `virgl`
  driver's stream goes through OpenGPU's ring to the host's GPU. So OpenGPU
  stays the one acceleration layer under every 3D API (amigachrome
  `docs/architecture/OPEN_SDL_MESA_DESIGN.md`).
- **It doesn't replace the Picasso96 drivers.** `ACRTG.card` and
  `ACRTG.chip` still serve Picasso96 and OS 4. `ACRTG.chip` and `ACRTG.gpu`
  share the board's register code.
- **On OS 4 too:** the same library for PPC, so programs that use it move
  between OS 3 and OS 4 with a recompile.

### OpenGPU 0.1: one stream, three back ends (6 October 2026)

Team, 5 October 2026: OpenGPU only with a PiStorm back end too, taking the
same Amiga-side messages. 6 October: OpenGPU first in the next Open apps,
and "we should tackle our compatibility layer for Warp3D and Wazp3D", as
part of this design. The design shown to us that day
is the artifact "OpenGPU 0.1 Design"; what it settles:

- **One command stream.** Every OpenGPU call becomes OGPU stream v1
  (`include/opengpu/stream.h`): big-endian words, an opcode and a length per
  command, surfaces by slot, fences. Every back end gets the same bytes.
  Stream v1.1 (6 October, agreed with the team designing OpenGfx, the
  accelerated graphics.library) adds A8 coverage for anti-aliased text and
  clip paths: the A8 format, MASK (a colour through an A8 mask in memory),
  and COMPOSITE's MASK (an A8 surface's coverage), ADD and IN flags. Its
  scene is `tests/golden/opengpu-v11.txt`; the Vulkan back end draws it too.
  A program asks `OGPU_Query(OGPU_OP_MASK, format)` first: a v1.0 back end
  answers OGPU_NONE.
- **One core, compiled three times.** `library/opengpu/ogpu_core.c` carries
  out the stream in plain C with integers only: on the 68k as the CPU back
  end (in `opengpu.library`, always there), in the Cradle's runtime behind
  `ACRTG.gpu`, and on a PiStorm's spare ARM core behind `PiStorm.gpu`. The
  golden scene in `tests/golden/opengpu-g1.txt` is the checksum all of them
  must give; `tests/run.sh` checks it on the host and, big-endian, on a
  68040 under qemu.
- **Back ends** share one interface (`include/opengpu/driver.h`): CPU (0.1),
  `ACRTG.gpu` over protocol v3 (G2), `PiStorm.gpu` (G3, released with G2),
  `AGA.gpu` later, VideoCore V3D to study.
- **The PiStorm hook.** Emu68 cannot yet start code on a spare core for an
  Amiga program. `PiStorm.gpu` needs a small hook for that; until it exists
  a PiStorm uses the CPU back end under Emu68's JIT. Whether the hook is
  offered to Emu68 upstream or carried in our own build is our call, and
  nothing goes upstream without our word.
- **Warp3D:** `Warp3D.library` exports the V5 68k function table (97 calls,
  a superset of V4), so V4 and V5 programs, MiniGL and StormMesa open it
  unchanged; with no back end, OpenGPU's CPU rasterizer draws. Warp3D Nova
  is OS 4 only and is not targeted; modern GL on OS 3 is Mesa's, over
  OpenGPU too.
- **Wazp3D** (GPL) is matched, never copied. Ours sits in `LIBS:OpenRTG/`,
  first in the library list, so programs get it while Wazp3D's files stay
  put; the installer carries Wazp3D's settings (renderer, filtering) into
  the prefs' Acceleration page. A `soft3d.library` stand-in, which would
  accelerate an existing Wazp3D install, is built only if a program needs it.
- **The window** is the prefs app's Acceleration page (replacing "Pointer
  and 3D"; the pointer moves to Monitors): who draws on each monitor, what
  Warp3D programs see, and a Test that measures each back end.

| OpenGPU phase | Delivers | Done when |
| --- | --- | --- |
| G1 | Stream v1, `opengpu.library` 0.1 with the CPU back end, 2D and COMPOSITE, OGPU_Query; then openrtg.library's busy calls through it | Started 6 Oct: the core and its tests, golden scene `de825f7b` on the host and a 68040. Done when Workbench and MultiView draw correctly through it on a 68040 |
| G2 | ACRTG protocol v3 (64 MiB, ring, fences) in the runtime; `ACRTG.gpu` drawing on the host's GPU through Vulkan (Pi 5 and Pi 4 Nano first, then Radeon/Ryzen and NVIDIA), the C core as fallback | Started 6 Oct: `host/vulkan/` draws the stream with compute shaders; on lavapipe it gives golden `de825f7b` and matches the core on 20,000 random streams. Done when the golden scene comes from the C core and from the GPU, on a Pi 5 Nano and on daletop |
| G3 | `PiStorm.gpu` and the Emu68 hook | The golden scene on a PiStorm; G2 and G3 released together |
| G4 | 3D in the stream; Warp3D V5 table, `W3D_OpenGPU`, `W3D_OpenRTG`; Wazp3D migration | Warp3D demos, GLQuake and a V4 game on all three back ends |
| G5 | Compute batches (the AmiSSL provider below) on the same Vulkan device, `AGA.gpu`, VideoCore on a PiStorm (study) | Each its own test |

Host GPUs (Team, 6 October 2026: "opengpu should have the pi4/pi5 gpu as a
backend early in development. x86 should look at specifics around touching
ryzen, nvidia, via the host os"). The runtime never programs a GPU itself; it
uses the host's driver through Vulkan:

- **Pi 5 / Pi 4 Nano:** VideoCore VII / VI with Mesa's V3DV. CPU and GPU share
  memory, so the board's video RAM is allocated as a Vulkan buffer and drawn
  in place; the result can be a KMS plane. Brought up first.
- **Ryzen with on-chip Radeon graphics:** RADV (Linux) or AMD's driver
  (Windows); video RAM imported in place with `VK_EXT_external_memory_host`.
- **Radeon or NVIDIA cards:** their own memory. Video RAM sits where the CPU
  still writes it fast (Resizable BAR when present); otherwise changed rows
  are copied. NVIDIA's own driver first, NVK later.
- **No usable GPU:** the C core, as before.
- **How:** the video RAM is one Vulkan storage buffer, and each command is a
  compute shader that does exactly what `ogpu_core.c` does, so the golden scene
  compares the two. A batch is one command buffer; fences are a timeline
  semaphore. Tiny batches stay on the C core. The tests run the shaders under
  Mesa's lavapipe, with no GPU.
- **Drawing in the board's own memory:** the runtime's 68k reaches video RAM
  directly, so the back end can take the runtime's memory (`host_vram`)
  instead of its own. It imports it where the driver can
  (`VK_EXT_external_memory_host`: RADV, lavapipe), or exports its own memory
  and maps it over the runtime's (`VK_KHR_external_memory_fd`, dma-buf: V3DV
  on the Pi 5), keeping what was there. `tools/gpu_check.sh` runs the golden
  scenes each way on a machine's own GPU and installs nothing.

### Compute for TLS: AmiSSL's maths through OpenGPU

Team, 4 October 2026: "consider a patch for AmiSSL that drives its key
generation via OpenGPU style, rather than the CPU driving the calculations".

AmiSSL 5 is OpenSSL 3.6.2, and OpenSSL 3 takes its algorithms from
*providers*. AmiSSL's 68k SDK exposes `OSSL_PROVIDER_add_builtin` and
`OSSL_PROVIDER_load` (through `amisslext`). So no binary patch is needed:

- **An `opengpu` provider** offers the costly parts of TLS:
  - key exchange: X25519, P-256 and P-384;
  - signatures: RSA, ECDSA and Ed25519;
  - bulk ciphers: AES-GCM and ChaCha20-Poly1305;
  - hashes: SHA-256 and SHA-384;
  - random numbers from the host's entropy (an Amiga has little of its own).

  Each goes to OpenGPU as a compute batch, with the same fences.
- **Where the maths runs:**
  - on AmigaChrome, the PC (through `ACRTG.gpu`'s ring);
  - on a PiStorm, the Pi's ARM cores, with their crypto extensions;
  - on a real Amiga, nowhere new: the provider steps aside and AmiSSL's own
    code runs as today.
- **Our programs first.** OpenMail's and OpenBrowser's shared network layer
  registers the provider when it opens AmiSSL, so their TLS handshakes stop
  waiting on the 68k.
- **Every AmiSSL program next.** The clean route is upstream: AmiSSL
  (open source) learns to load providers from `LIBS:`, and we offer it the
  change. An OS-friendly patch on AmiSSL's open call, adding the provider to
  each new context, is the fallback if upstream says no.
- **Keys stay on the machine.** They pass from the Amiga to its own host (the
  same PC, or the PiStorm's Pi), never over a network. The host side uses a
  constant-time library (the PC's OpenSSL or libsodium).

## 6. Warp3D, built in

Team, 4 October 2026: "we want Warp3D baked in to the design". 3D is part of
the board and of every OpenRTG install from the start, not a card or a
package added later.

- **Every ACRTG board has the 3D unit.** There is no separate 3D card: the
  "ACRTG 3D" design in the Hardware list becomes ACRTG itself.
- **Room for it: protocol v3 boards are 64 MiB.** At 1920x1200 in true
  colour, a front buffer, a back buffer and a Z-buffer alone need about 23 MB,
  more than today's 16 MiB board holds. The first 16 MiB stay exactly as now
  (boot ROM, video RAM, registers at +$FF0000), so v2 drivers and Picasso96
  see the board they know. The 48 MiB above are for 3D: back buffers,
  Z-buffers and textures.
- **One command ring for 2D and 3D.** Protocol v3 carries the 2D commands
  (fill, copy, template, line, planar-to-chunky, COMPOSITE) and the 3D ones
  (states, textures, triangles, strips, fans, lines, points, clears, fences)
  in the same ring in video RAM, so they stay in order. The runtime draws
  them with its own rasterizer, written in C so it runs in the native and the
  WASM runtime alike, and reports each fence as it completes. The look's
  gradients, compositing and the OS 4 behaviours use the same engine.
- **`Warp3D.library` is in every install** (`LIBS:OpenRTG/`). It is the
  Warp3D V4 API: contexts, states, textures, the Z-buffer, W3D_DrawTriangle,
  strips, fans, arrays, lines and points, and locking. It works on every
  monitor's bitmaps; textures and Z-buffers live in the board's 3D memory.
- **Saying what each driver can do.** Warp3D programs ask, so the drivers
  answer honestly (from the Warp3D V4 header in the OS 4.1 SDK).
  `W3D_GetDrivers()` lists the drivers: chip ID, destination formats, name,
  and whether it is a CPU driver. `W3D_TestMode()` and `W3D_BestModeID()`
  pick a screen mode that has one. `W3D_Query()` and `W3D_QueryDriver()`
  answer FULLY, PARTIALLY or NOT supported for each feature, per
  destination format: point, line and triangle drawing, texture mapping,
  mipmaps, the filters, perspective, Gouraud shading, the Z-buffer, alpha
  test and blending, fog, antialiasing, the scissor, the stencil, the
  largest texture. `W3D_GetTexFmtInfo()` says which texture formats are
  supported and which are fast. `W3D_CheckDriver()` says hardware or CPU.
  On ACRTG, `W3D_OpenGPU` answers as a full hardware driver. On AGA it answers as what
  it is: triangles, lines and flat shading fully; texture mapping partly
  (the CPU); no Gouraud and no Z-buffer; CLUT destinations only. The chip
  IDs stop at the Radeons (`W3D_CHIP_SI` is 13), so ours report
  `W3D_CHIP_UNKNOWN` with their own names rather than take a number that
  may clash with a future official one.
- **Later, behind the same commands:** a GPU path in the host, MiniGL
  (OpenGL over Warp3D, for the Warp3D ports of Quake II and Heretic II), and
  the newer 3D APIs (section 7).
- **Address space:** four 64 MiB boards take 256 MiB of Zorro III space
  beside AC090's RAM. That fits with 512 MiB of AC090 RAM; with 1 GiB it has
  to be measured on Kickstart's Zorro III allocation.

Wazp3D and AROS's Warp3D are references only: their code is not copied.
Wazp3D users move over as section 5's "OpenGPU 0.1" says.

## 7. OS 4 and MorphOS behaviours

Team, 4 October 2026: "We want more OS 4 behaviours or MorphOS". Where RTG
went after OS 3.x, as goals for OpenRTG on OS 3.x:

| Behaviour | From | How |
| --- | --- | --- |
| Compositing: windows with transparency and drop shadows, opaque window dragging | OS 4.1 (compositing effects), MorphOS 3 | Each window of a composited screen gets its own surface in video RAM; the board composes the screen from them (alpha, shadows) as the head is shown. layers.library's drawing goes to the window's surface. |
| Alpha blending and scaled blits for programs | OS 4 graphics.library `CompositeTagList`; MorphOS cybergraphics `BltBitMapAlpha`, `BltBitMapRastPortAlpha`, `WritePixelArrayAlpha`, `ProcessPixelArray` | A board COMPOSITE command (source, destination, alpha, scale, filter); `openrtg.library` exports CompositeTags, and our cybergraphics.library the MorphOS alpha calls. |
| Screen dragging on RTG, with the screen behind showing | OS 4, MorphOS | The board shows more than one screen per monitor: the front screen at its drag offset over the ones behind, composed by the board, not copied by the CPU. |
| The pointer moves from monitor to monitor | OS 4.1 multi-monitor, MorphOS | Monitors have a layout (left of, right of); the pointer leaving one monitor's edge enters the next, whose front screen becomes active. |
| A screen mode per monitor, Workbench on any monitor | OS 4, MorphOS | OpenRTG prefs, per monitor. |
| Modern 3D after Warp3D V4 | OS 4 Warp3D Nova, MorphOS TinyGL | Later, on the same rasterizer and command ring. |

The board protocol leaves room for these from Phase 1 on: surfaces
(bitmap, size, format, alpha) and a compose list per head, so screen
dragging and compositing are board work, not CPU copies.

## 8. The window look

**Moved to OpenGadTools** (Team, 4 October 2026, "yes opengadtools"). The
look patch and its prefs page now live in `DalsinAI/opengadtools`, because
they work on any screen, AGA included. OpenRTG keeps only what needs RTG:
smooth true-colour gradients, and with compositing (section 7) drop shadows
and transparency, which OpenGadTools' look asks OpenRTG for when the screen
is an RTG one. The OpenRTG prefs app still shows the Window look page as one
of its pages, so there is still one editor; the page's code is
OpenGadTools'. The rest of this section is the look as designed, now
OpenGadTools' to build.

The OS 4.1 feel for windows on Workbench and on every other screen:

- **Title bars** in a gradient: a colour ramp on the active window, a grey
  one on the others. The title is bold with a shadow.
- **Border gadgets** drawn as rounded buttons with clear glyphs: close,
  iconify, zoom, depth and size. The arrows, the scroller knobs, the
  checkboxes and the radio buttons match them.
- **A deeper 3D frame**, and the screen bar and menus in the same style.
- **On RTG screens** (true colour): smooth gradients, glyphs with soft
  edges, and with compositing (section 7) drop shadows and transparency.
  **On AGA screens:** a ramp of pens the look takes from the screen's free
  pens, dithered when there are few. On four-colour screens the classic look
  stays.

How it stays a clean, OS-compatible change:

- **Intuition keeps the geometry.** OS 3.2's IControl already sizes the
  border gadgets (aspect ratio, scaling to the title bar's height, the title
  bar height increment) and has its own iconify gadget (`WA_IconifyGadget`,
  `WFLG_HASICONIFY`, `ICONIFYIMAGE`). The look draws inside the boxes
  intuition gives, so every program sees exactly the window sizes it sees
  without the look.
- **Images through sysiclass.** sysiclass is a public BOOPSI class. The look
  chains its dispatcher: the images it draws (IM_DRAW, IM_DRAWFRAME) are its
  own; every other method, and every image it does not draw, goes to the
  original. No private structure is touched.
- **Frames and title bars through the drawing layer.** OS 3.x intuition has
  no public decoration hook (IntuitionControlA's hooks are private). So these
  are drawn where OpenRTG already sits, in its drawing layer (section 4).
  It recognises intuition filling a window's border (the layer's window, the
  title bar's box, FILLPEN or INACTIVEFILLPEN) and draws the look there
  instead. On an RTG screen that is one board command (a gradient fill).
- **Resident.** The look lives in `openrtg.library`. `C:OpenRTG` opens it,
  applies the prefs and exits; the library stays in memory with its patches
  (it refuses to be expunged while patched), and no task keeps running.
  When Picasso96 is the active RTG system, `C:OpenRTG LOOK` loads the look
  alone.
- **Off is off.** "Classic look" in the prefs makes every patch pass straight
  through, so the screen is pixel for pixel what intuition draws. Patches are
  never removed, because another program may have chained after them.
- **AROS** has decorator classes for windows, screens and menus in its own
  intuition, so there the look is a decorator, with no patching.

## 9. The prefs app

One GadTools editor, `SYS:Prefs/OpenRTG`, with Use, Save, Test and Cancel as
the OS's own editors have. Its pages:

| Page | What it sets | Where it is kept |
| --- | --- | --- |
| Monitors | Which monitors are on, their order and layout (left of, right of), which one Workbench opens on | `openrtg.prefs` |
| Screen mode | Each monitor's Workbench mode: size, depth, refresh. The list shows Standard modes (the popular PC resolutions) or All, by a switch above it; the same switch sets which modes the monitor offers programs | `screenmode.prefs` (the OS's own, so ScreenMode prefs and OpenRTG never disagree) and `openrtg.prefs` for monitors 2 to 4 and the Standard or All choice |
| Colours | The palette and the 13 DrawInfo pens, BARCONTOURPEN included | `palette.prefs` (the OS's own) |
| Window look | OS 4 or classic; the title bar colours (active and inactive), title alignment, gadget style; shadows and transparency on RTG; the same look for Zune programs | `openrtg.prefs`, and `ENV:zune/global.prefs` for Zune |
| Pointer and 3D | The pointer on each monitor; Warp3D settings | `openrtg.prefs` |

What the OS already has a prefs file for stays in that file, written in the
OS's own format, and IPrefs applies it as usual.

**Zune's look comes from here too.** Team, 4 October 2026: Zune's prefs,
"for our world, would be captured, controlled etc in our look and feel prefs
patch / prefs tool". `zunemaster.library` (OpenMUI, AROS's Zune on OS 3.2.x)
reads its look from `ENV:zune/global.prefs`. Each program's own
`ENV:zune/<program>.prefs` is watched and applied live. The Window look page
writes `global.prefs` from the same choices (frames, colours, fonts,
backgrounds), so Zune programs match Intuition and GadTools. Zune's own
editor, `SYS:Prefs/Zune`, is not installed as a separate tool. Per-program
settings, if offered, are a page here as well.

**The Installer and the classic editors.** Team, 4 October 2026: the
Installer "offers to replace [or] remove classic prefs apps doing the same
job". It lists what it found and asks, item by item (Novice users get the
recommended answer):

| Found | Same job as | The Installer offers |
| --- | --- | --- |
| `SYS:Prefs/ScreenMode` | Screen mode page | Replace: the original goes to `SYS:Storage/Prefs/`, and a ScreenMode icon in its place opens OpenRTG at that page |
| `SYS:Prefs/Palette` | Colours page | The same |
| `Picasso96Mode` | Monitors and Screen mode pages | Move it to `SYS:Storage/Prefs/` when OpenRTG becomes the active RTG system |
| VisualPrefs, SysIHack, MagicFrames, Birdie and other look patches (in `WBStartup` or the startup scripts) | The window look | Turn them off (the WBStartup icon to `WBStartup/Storage/`, the startup line commented out), since two patches drawing the same images fight |

Nothing is deleted: everything moved goes to a `Storage` drawer and is
listed in the install log, and the uninstaller puts it all back.

## 10. Phases

| Phase | Delivers | Done when |
| --- | --- | --- |
| 0 | LibCount (`openrtg/tools`): counts of graphics, intuition, layers and RTG library calls | Done 4 Oct for Workbench and MultiView on Picasso96 ("What programs call" below); a MUI program and a game still to measure |
| 1 | Several ACRTG boards; a window per monitor in Cradle; two as standard; `acrtg.card` claims the next unclaimed board | Picasso96 on OS 3.2.3 shows Workbench on monitor 1 and another screen on monitor 2, AGA on its own |
| 2 | (Started 4 Oct: the mode table, `library/modes.c`, with host tests; `openrtg.library` 0.1 and `C:OpenRTG` find both boards and list their modes on OS 3.2.3, `measurements/20261004-openrtg-0.1-two-monitors.txt`; 0.2 puts OpenRTG's modes in the display database, `measurements/20261004-openrtg-0.2-display-database.txt`. 5 Oct: 0.3 and 0.4 open 8-bit screens without Picasso96, and 0.5 16-bit (R5G6B5) and 32-bit (A8R8G8B8) screens, on the CPU (a pen is its colour from the screen's palette): Workbench with its backdrop, the pointer and the mouse, on the scratch copy; the modes are records in graphics' own display database, added as monitor drivers add theirs.) `openrtg.library`: boards, display database, screens; `C:OpenRTG`; started early from the boot ROM; the driver split into `ACRTG.card` and `ACRTG.chip`; AGA as the pseudo card `AGA.card` (monitor 0) | OS 3.2.3 without Picasso96 boots straight to Workbench on an OpenRTG monitor, with no switch from AGA |
| 3 | OpenGPU: `opengpu.library` with its CPU fallback, `ACRTG.gpu` over protocol v3 (64 MiB boards, one ring, the runtime's rasterizer), `AGA.gpu`; drawing on RTG bitmaps through it; the pointer per monitor; the pass-through switched in the runtime and locked to the beam | Workbench, MultiView and a few programs draw correctly and fast; the rasterizer's golden images pass |
| 4 | (Started 5 Oct: `cybergraphics.library` 43.1 over `openrtg.library` 0.4's pixel calls, on 8-bit screens.) `cybergraphics.library` and `Picasso96API.library`; OS 4's RTG calls in `openrtg.library`, with the `openrtg/` and `os4` headers | Programs written for either open screens and draw; an OS 4 example using CompositeTags and LockBitMapTags builds for 68k unchanged and runs |
| 5 | `Warp3D.library`, `W3D_OpenGPU.library` and `W3D_OpenRTG.library` on every monitor, the blitter's 3D on monitor 0; OpenGPU's compute batches; then MiniGL | Warp3D demos and a Warp3D game run, on monitor 1 and on monitor 2 |
| 6 | (Moved to OpenGadTools, 4 Oct.) OpenRTG's part of the look: true-colour gradients, then shadows and transparency once compositing exists. Was: the window look: the images through sysiclass, then frames and title bars; resident; `C:OpenRTG LOOK` beside Picasso96 | Workbench's windows on OS 3.2.3 have the OS 4 feel on an AGA and an RTG screen; Classic look is pixel for pixel intuition's |
| 7 | The OpenRTG prefs app | Monitors, screen modes, colours, the look and 3D set from one editor; ScreenMode and Palette prefs agree with it |
| 8 | AROS: several boards through its own RTG; OS 4.1 on the Sam460: ACRTG on PCI with the PPC `ACRTG.chip` and OpenGPU; `VideoCore.gpu` for the PiStorm's 3D, and real cards' `.gpu` drivers as testers' hardware allows; the look as a decorator; an Installer package that offers to replace the classic editors and turn off other look patches, and an uninstaller | AROS shows two RTG monitors; OpenRTG installs from its Installer and uninstalls back to the classic editors |
| 9 | OS 4 and MorphOS behaviours: OpenGPU's COMPOSITE and the alpha calls; screen dragging on RTG; the pointer across monitors; then compositing of windows | Transparent windows with shadows; an RTG screen dragged down shows the one behind; the pointer crosses monitors |

### What programs call (phase 0, 4 October 2026)

LibCount counted every call to graphics, intuition, layers, `rtg.library`
and `Picasso96API.library` on the OS 3.2.3 scratch copy, with Workbench on
an ACRTG monitor under Picasso96 at 1920x1080.

**Workbench, 45 seconds** (drawers opened, a window dragged): 100 functions
called. The busiest:

| Calls | Function |
| ---: | --- |
| 43,202 | GetRGB32 |
| 682 | RectFill, BltPattern (each) |
| 348 | FindDisplayInfo, GetDisplayInfoData (each) |
| 347 | ObtainBestPenA |
| 245 | LockLayer, UnlockLayer (each) |
| 224 | SetDrMd |
| 161 | LockLayerRom, UnlockLayerRom (each) |
| 160 | InstallClipRegion |
| 152 | BltBitMap |
| 132 | MoveSprite |
| 94, 76, 69, 30, 28 | ExtendFont, TextLength, TextExtent, Text, BltTemplate |

**MultiView showing a true-colour PNG, 30 seconds**, with its window
dragged:

| Calls | Function |
| ---: | --- |
| 3,805 | FindDisplayInfo |
| 2,525 | GetDisplayInfoData |
| 1,284 | NextDisplayInfo |
| 1,280 | ModeNotAvailable |
| 874, 806 | BltPattern, RectFill |
| 502, 401, 327 | SetAPen, Draw, Move |
| 345 | LockLayer, UnlockLayer (each) |
| 260 | GetRGB32 |
| 130 | AreaDraw |

What it means for the order of the work:

- **The display database comes first** (phase 2). Programs ask it
  constantly; MultiView walks every mode (NextDisplayInfo, ModeNotAvailable)
  when it opens. OpenRTG's answers must be fast, from a table built once.
- **Then the core drawing** (phase 3, through OpenGPU): RectFill and
  BltPattern (the fills under every window), BltBitMap, BltTemplate and Text,
  Draw and Move, area fills. Since 8 October 2026 these are OpenGfx's patches
  calling OpenRTG as the RTG provider (section 4, Drawing); the measurements
  here still say which calls to make fast first.
- **Pens must be cheap.** GetRGB32 was called about a thousand times a second
  on Workbench, and ObtainBestPenA hundreds of times.
- **Nothing called Picasso96's own libraries.** Workbench and MultiView reach
  RTG only through graphics and intuition, so the compatibility libraries
  (phase 4) matter for RTG-aware programs and games, not for the desktop.
- Still to measure: a MUI program and a game.

## 11. Tests

- The runtime: unit tests for every OpenGPU command and the rasterizer
  (golden images), as `acrtg_test.c` does now.
- The Amiga: a test program per phase on the sandboxed OS 3.2.3 copy, with
  screenshots of each monitor; Picasso96 stays the reference to compare with.
- Never on our validation instance (Instance-23) until a phase passes on
  the copy.
- Real hardware: a real Amiga with a Picasso96 card, and a PiStorm with
  Emu68, for every phase that ships to Amiga users.

## 12. To revisit

- **AGA first, on the appliance** (Team, 4 October 2026: "when we do
  appliance style deployment, AGA first is something to revisit"). Today the
  display order puts AGA first: Workbench opens on AGA and moves to RTG when
  LoadMonDrvs and IPrefs run, and AGA is monitor 0. On the appliance (one PC
  screen, Cradle as the OS face) an instance should probably come up straight
  on an RTG monitor, with the early start from the ACRTG ROM (section 4) as
  the way there. To decide when the appliance work resumes.


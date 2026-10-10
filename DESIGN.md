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

**OpenGfx handoff (8 October 2026).** OpenGfx 1.1 is now the sole owner of
eight `graphics.library` vectors: `Text`, `TextLength`, `TextExtent`,
`TextFit`, `RectFill`, `BltBitMap`, `BltTemplate` and
`ScrollRaster`. OpenFont patches nothing. OpenRTG registers its RTG
implementations through OpenGfx's size-compatible provider ABI and does not
install competing patches for the overlapping drawing calls. OpenRTG provides
`Text`, `RectFill`, `BltBitMap`, `BltTemplate` and
`ScrollRaster` for OpenRTG bitmaps; the three measurement calls currently
fall through to graphics.library because OpenRTG does not change font metrics.
If OpenGfx is absent, OpenRTG keeps its existing standalone patch path. All
other OpenRTG-specific hooks remain owned by OpenRTG. Since opengpu.library
0.6 (8 October 2026) OpenGfx is inside opengpu.library, and openrtg.library
0.12 registers its provider there; with an older opengpu.library it still
opens `opengfx.library` (by now a stub that forwards to opengpu.library).

**Every drawing call under OpenGfx (8 October 2026, opengpu.library 0.8,
openrtg.library 0.13).** The user's decision ("yes, under OpenGfx"): OpenGfx
owns every `graphics.library` drawing and text patch, not only the first
eight. OpenGfx 1.4 patches fourteen more, and OpenRTG provides them through
the longer provider record (`struct OGFXProviderAll`, `include/opengpu/gfx.h`)
instead of patching them itself. Screens, modes, bitmaps, palettes and
sprites are not drawing: they stay OpenRTG's. With an OpenGfx older than 1.4,
OpenRTG patches the fourteen itself as before; with no OpenGfx, all of them.

### Who owns each call

| Call | LVO | Owner (patches it) | Behind it |
| --- | --- | --- | --- |
| Text | -60 | OpenGfx | OpenLook's look, then OpenRTG (provider) |
| TextLength, TextExtent, TextFit | -54, -690, -696 | OpenGfx | graphics.library (OpenFont's metrics later) |
| RectFill | -306 | OpenGfx | OpenLook's look, then OpenRTG |
| BltBitMap, BltTemplate, ScrollRaster | -30, -36, -396 | OpenGfx | OpenRTG |
| BltPattern, SetRast, Draw, PolyDraw | -312, -234, -246, -336 | OpenGfx (0.8) | OpenRTG |
| WritePixel, ReadPixel | -324, -318 | OpenGfx (0.8) | OpenRTG |
| BltBitMapRastPort, BltMaskBitMapRastPort, ClipBlit | -606, -636, -552 | OpenGfx (0.8) | OpenRTG |
| WriteChunkyPixels, WritePixelArray8, WritePixelLine8 | -1056, -786, -774 | OpenGfx (0.8) | OpenRTG |
| ReadPixelLine8, ReadPixelArray8 | -768, -780 | OpenGfx (0.8) | OpenRTG |
| AllocBitMap, FreeBitMap, GetBitMapAttr | -918, -924, -960 | OpenRTG | (bitmaps, not drawing) |
| MakeVPort, MrgCop, LoadView | -216, -210, -222 | OpenRTG | (the display) |
| LoadRGB32, SetRGB32, LoadRGB4, SetRGB4 | -882, -852, -192, -288 | OpenRTG | (palettes) |
| MoveSprite, ChangeExtSpriteA | -426, -1026 | OpenRTG | (sprites) |
| NextDisplayInfo, FindDisplayInfo, GetDisplayInfoData, ModeNotAvailable, BestModeIDA | -732, -726, -756, -798, -1050 | OpenRTG (displaydb.c) | (the display database) |
| intuition: OpenScreenTagList, CloseScreen, MakeScreen, RemakeDisplay, RethinkDisplay | -612, -66, -378, -384, -390 | OpenRTG | (screens) |

On Picasso96 (no OpenRTG) OpenGfx's 22 patches sit in front of Picasso96's
own (rtg.library patches some of the same calls, among them BltPattern, the
pixel calls and the chunky and *8 calls): OpenGfx has no provider there and
passes each call on, so Picasso96 draws as before. OpenLook 0.6 asks OpenGfx
to put its patches in.


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
- **Drawing: OpenGfx owns the shared patch surface; OpenRTG provides the RTG side**
  (Team decision, 8 October 2026). `opengfx.library` 1.1 owns exactly these
  eight `graphics.library` vectors: `Text`, `TextLength`, `TextExtent`,
  `TextFit`, `RectFill`, `BltBitMap`, `BltTemplate` and
  `ScrollRaster`. OpenFont patches nothing.
  OpenRTG registers a provider with OpenGfx. For OpenRTG bitmaps it supplies
  `Text`, `RectFill`, `BltBitMap`, `BltTemplate` and
  `ScrollRaster`; the three text-measurement entries stay NULL until
  OpenFont-backed metrics are ready, so OpenGfx chains them to
  graphics.library. From opengpu.library 0.8 OpenGfx owns the other
  fourteen drawing calls too (`BltPattern`, `Draw`, the pixel calls and
  arrays, `ClipBlit` and the rest; "Who owns each call" above) and OpenRTG
  provides them; OpenRTG keeps the display database, bitmap and screen
  management, palettes, sprites and the board drivers.
  When OpenGfx is not installed, OpenRTG retains its standalone patch path so
  the library remains usable independently. OpenGfx lives in opengpu.library
  from 0.6 (`library/ogfx`, "One library" below).

- **The pointer:** each monitor's front screen gets the board's hardware
  sprite (acrtg-v2), which Cradle shows as the PC's cursor.
- **The mouse's range (0.13.1, 10 October 2026):** OS 3.2's Intuition keeps
  the pointer in a 16-bit range of ticks and stops it at 30000. A mode's
  ticks per pixel (18, as Picasso96 gives them) made a 1920-wide screen end
  at x = 1666: the screen bar's network, speaker and cog could not be clicked
  and a window's right edge could not be reached. Each mode now gets as many
  ticks (at most 18) as keep its larger side under 29900: 15 for 1920 x 1080,
  18 up to 1661 pixels. `ticks_of()` in `library/displaydb.c`; the monitor's
  ratio stays at 18.
- **Lock order (0.14.1, 10 October 2026):** a pixel call (WritePixelArray,
  ReadPixelArray, FillPixelArray and the alpha write) takes its RastPort's
  layer lock first and the palette lock second, and lets go in the opposite
  order. Taken the other way (the palette lock, then the layer's), an SDL
  window's present held the palette lock while it waited for its layer, which
  Intuition's window drag held; the drag waited for the screen bar's layer,
  held by OpenLook painting the bar's depth gadget, which waited for the
  palette lock. Three tasks, one cycle: dragging an SDL window stopped the whole
  machine. Programs that lock a layer and then call pixel functions (OpenLook
  does) are safe now; the rule for any new lock in this library is that it is
  taken after a layer's, never before. `begin()` and `end()` in
  `library/pixels.c`.
- **Pens a screen (0.14.2, 10 October 2026):** each 16 or 32-bit screen has
  its own table of pen colours (16 tables: one shared for each monitor, eleven
  a screen's own), set from its palette whenever the palette changes, in front
  or behind; its friends (backing store, double buffers) share it. Before, a
  monitor's screens shared one table that followed the front screen, so while
  a game's screen was in front (Neverball on MiniGL), what Workbench drew behind
  it (the bar's clock, the dock, a window's refresh) took the game's colours
  and kept them after the game: the desktop came back with its colours tweaked.
  The shared table still follows the front screen, and a new screen's table
  starts from it; with more screens than tables, a screen uses the shared one,
  as before. `own_pens()`, `show_front()` and `palette_changed()` in
  `library/screens.c`.
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
| G1 | Stream v1, `opengpu.library` with the CPU back end, 2D and COMPOSITE, OGPU_Query; then openrtg.library's busy calls through it | **Done 8 Oct.** 4,873/4,873 core checks and golden `de825f7b`; the OS 3.2 stove build passed; Workbench and MultiView ran on OpenRTG screens on an AC090 68040 with `OpenGPUCheck` reporting the G1 golden correct through `opengpu.library`. Evidence: `measurements/20261008-opengpu-v1.0-g1-completion.txt`. Completed the same day with lines, pixel arrays, alpha, patterns at any phase, source-free minterms and format-changing copies routed, each only where it draws OpenRTG's CPU pixels exactly (OpenRTGExact: 24 of 24 the same, through the ring and on the 68k); 97% of Workbench's start-up pixels and 99% of MultiView's go through OpenGPU (`C:OpenRTG STATS`). Evidence: `measurements/20261008-opengpu-v1.0-completion-and-v1.2.txt` |
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

### One library (8 October 2026)

Team, 8 October 2026: one library, opengpu.library, whose includes stand in
for what SDL 2 and others call, "so we dont need a gl library". OpenGfx
merges into it (a fork for now, while opengfx.library is being tested; they
come together later), Mesa lives inside it, SDL 2's API is provided by it,
and Warp3D.library becomes a stub over it. Then: "resident core, shared big
parts". The layout below was agreed that day; three helpers add files to it
without editing each other's.

**OpenGfx, merged (8 October 2026).** `library/ogfx/` holds amigachrome-guest
`libraries/opengfx` as of 90aa66f (opengfx.library 1.1: the eight
graphics.library drawing and text patches, the leaves, the premultiplied
composite reference), merged into opengpu.library 0.6 once the OpenGfx work's
testing was done; `FORK.md` there records the commit and every change. Its
six calls are OpenGPU's LVOs from offset 66 in their own order, and the eight
graphics.library calls follow as LVOs of their own (102 to 144), so a
program written for opengfx.library needs only its base changed. The patches
are thin entries in the library's base that call the same code.
`opengfx.library` 1.2 is a stub that forwards to these LVOs. Next, OpenGfx's
work moves onto OpenGPU: the leaves' rectangles become FILL, COPY and
TEMPLATE batches, its composites COMPOSITE and MASK, so the same drawing
reaches the ring on the Cradle and the CPU core elsewhere.

**OpenGfx's look hook (opengpu.library 0.7, 8 October 2026).** OpenLook
(OpenGadTools) drew window frames by patching RectFill and Text itself, so
on the OpenRTG part those two calls had two patches: OpenLook's on top of
OpenGfx's. OpenGfx now owns them alone. A program that draws the system's
look on them registers a look instead (`OGFX_RegisterLook`, LVO 150;
`OGFX_UnregisterLook`, 156; `struct OGFXLookV1` in `include/opengpu/gfx.h`).
OpenGfx asks the look first on every RectFill and Text, before the provider
and its own paths; the look returns non-zero when it has drawn the call. One
look at a time. Calls inside the look are counted, and `OGFX_Status()` shows
`OGFX_STATUS_LOOK` and `OGFX_STATUS_LOOK_BUSY`, so the look's owner can wait
for them before its code goes. The ABI is additive: the 0.6 LVOs, the
provider record and the status bits are unchanged, and OpenGfx's interface
is 1.3. With no look registered the two calls cost one byte test more.
OpenLook 0.6 registers its look and asks for OpenGfx's patches where nothing
has put them in (Picasso96 without the OpenRTG part).

#### 1. One library, heavy parts loaded on demand

- Programs open only opengpu.library and use one include tree.
- Mesa (about 20 MB) and SDL 2 are modules: `LIBS:OpenGPU/GL.module` and `LIBS:OpenGPU/SDL2.module`. Each loads on the first call a program makes into it, through two new LVOs: `OGPU_ModuleOpen(name, version, &table)` and `OGPU_ModuleClose(handle)`.
- Each calling program gets its own copy of a module's data, so SDL's global state and Mesa's globals stay private, as with static linking, and no opener is refused. Step 1 did this with a LoadSeg copy per program; since step 2 the code is loaded once and shared, and since opengpu.library 0.9 it stays loaded (section 5, "Residency").
- Link libraries:
  - `libSDL2.a` is SDL's dynapi stub. On the first SDL call, `OGPU_ModuleOpen("SDL2")` returns `SDL_DYNAPI_entry`, and every `SDL_` call jumps through that table (varargs work).
  - `libGL.a` is the same for GL. It has one entry per call, generated from the GL entry points Mesa's glapi made for the build (`stubs/gl/gen_gl.py`), with the 15 GLA calls. GL.module's table hands out the same calls by name. The first call opens it with `OGPU_ModuleOpen("GL")` and binds the whole table in one pass, so a program runs on an older or newer module (a missing call returns 0).
- So there is no gl.library and no SDL2 library: one library open, and the big code loads lazily.
- Module ABI (`include/opengpu/module.h`): the segment's first code is an entry taking {SysBase, DOSBase, OpenGPUBase, version} and returning the module's table. Modules call opengpu.library's LVOs like any program.
  - The table starts with the module's version; the rest is the module's own (SDL2: `dynapi_entry`, `close`; GL: `close` and its calls by name).
  - A module refuses (returns NULL) when the caller asks for a newer version than it is. `OGPU_ModuleOpen` then gives NULL with `IoErr()` `ERROR_OBJECT_WRONG_TYPE`; a missing file gives `ERROR_OBJECT_NOT_FOUND`.
  - The caller calls the module's own close, then `OGPU_ModuleClose`, which frees that program's data. A shared module's code stays loaded (section 5); a module for each program is unloaded.
  - A name with ':' or '/' is a path (tests load `PROGDIR:Test.module`).
  - opengpu.library 0.5 has the two calls. A stub on 0.4 or older loads the module itself, the same way.
  - Modules are linked without libnix's startup. `library/modules/common` stands in for it: the module's first code and libnix's list heads (`module_start.S`, first on the link line, naming `__initlibraries` and `__initcpp`), and `module_rt.c`, which runs the init and exit lists, turns `exit()` during start into a failed open, and gives `getenv` through `GetVar` (libnix's doesn't work in a module).
  - `tests/modules`: Test.module and ModuleCheck check this on an Amiga, and open SDL2.module and GL.module when they are installed.

#### 2. LVOs (bias 30)

| Offset | Call |
| --- | --- |
| 30 | OGPU_Query |
| 36 | OGPU_BackEndName |
| 42 | OGPU_Submit |
| 48 | OGPU_Wait |
| 54 | OGPU_ModuleOpen |
| 60 | OGPU_ModuleClose |
| 66 to 96 | OpenGfx's calls, in opengfx.library's order: OGFX_Version, InstallPatches, SetEnabled, Status, RegisterProvider, UnregisterProvider (0.6) |
| 102 to 144 | graphics.library's eight, through OpenGfx: OGFX_Text, TextLength, TextExtent, TextFit, RectFill, BltBitMap, BltTemplate, ScrollRaster (0.6) |
| 150, 156 | OpenGfx's look hook: OGFX_RegisterLook, OGFX_UnregisterLook (0.7); then 2D calls as they come |

Warp3D and SDL need no LVOs of their own. They build stream batches (0x0030–0x0035 and the rest) and pass them to `OGPU_Submit`.

#### 3. Directories (openamigartg)

##### include/: the one include tree for programs

- `opengpu/`:
  - library owner: `opengpu.h`, `stream.h` (v1.2), `build.h` (batch builders), `driver.h`, `module.h`, `virgl.h` (the ACVirgl request format);
  - Warp3D helper: `stream3d.h`, plus `build3d.h` with `ogpu_build3d.c` for its builders;
  - `gfx.h`: the OpenGfx fork's public calls.
- `proto/`, `inline/`: `opengpu.h`.
- `SDL2/`: SDL's public headers, fetched pinned at build into `build/include/SDL2` (Zlib, never committed). Only the Team's `SDL_config_amigaos.h` is committed, in `library/modules/sdl2/include/`.
- `GL/`, `GLES2/`, `GLES3/`, `KHR/`: Khronos and Mesa headers, fetched with Mesa (pinned) at build. The Team's `GL/gla.h` (the GLA calls) is committed.
- `Warp3D/`: `Warp3D.h`, Team-written and compatible, with no SDK text (Warp3D helper).

##### library/

- `opengpu/`, the core (library owner): `opengpu_lib.c` (the LVO table, the driver loader and the module loader), `ogpu_core.c/.h` and `ogpu_build.c`.
  - Exception: `ogpu_3d.c/.h`, the CPU rasteriser, belongs to the Warp3D helper. The owner wires 0x0030–0x0035 into `ogpu_core_run` with one call, `ogpu_3d_run(core, op, cmd, words)`.
  - The same files also compile into the runtime's host core, so `ogpu_3d.c` stays integer-only and free of the C library.
- `ogfx/`, OpenGfx (library owner): amigachrome-guest `libraries/opengfx` as of 90aa66f, merged into opengpu.library 0.6, with `FORK.md` recording the commit and every change. opengfx.library 1.1 is kept, archived, in amigachrome-guest; `opengfx.library` 1.2 there is a stub that forwards to opengpu.library.
- `warp3d/` (Warp3D helper): Warp3D.library, the 92-LVO stub, with its own `build.sh`.
- `modules/gl/` (library owner): GL.module, holding Mesa's port moved from openamigamesa:
  - `mesa/UPSTREAM.json` pin, `mesa/patches/`, the POSIX shim;
  - the `gla/` core and the OS 3 presenter;
  - the virgl winsys over `OGPU_OP_VIRGL`.
  - It builds softpipe and virgl. virgl is used when `OGPU_Query(OGPU_OP_VIRGL)` answers "full", softpipe otherwise.
  - openamigamesa keeps OpenDemos only, built against `libGL.a`.
- `modules/common/` (library owner): what a module has in place of libnix's startup (`module_start.S`, `module_rt.c`).
- `modules/sdl2/` (SDL 2 helper): SDL2.module, with the backends (video on openrtg, render on opengpu, audio on AHI, input, threads) and the SDL build (source fetched pinned).
- `stubs/sdl2/` (SDL 2 helper: libSDL2.a, the dynapi stub plus the ModuleOpen glue) and `stubs/gl/` (library owner: libGL.a, generated).
- `build.sh` builds everything. Each part has its own build script, and a helper adds one line to `library/build.sh` and nothing else.

##### host/

- `vulkan/`: as now.
- `virgl/acvirgl.c` (library owner): the host side of `OGPU_OP_VIRGL`. amigachrome vendors it like `ogpu_vk.c`.

##### tests/

- `golden/`: one file per scene.
- `golden_scenes.c`: the 2D scenes, shared by the host test and OpenGPUCheck.
- `test_3d.c`: Warp3D helper; the 3D goldens with the ±2 tolerance.
- `sdl2/`: SDL 2 helper.
- `gl/`: library owner; the GLA scenes on softpipe and virgl.

#### 4. Who edits what (no shared files)

- **Library owner:** `include/opengpu/{stream.h, build.h, driver.h, module.h, virgl.h, opengpu.h}`; `library/opengpu/` except `ogpu_3d.*`; `library/ogfx`, `library/modules/gl`, `stubs/gl`; `host/virgl`.
- **Warp3D helper:** `include/opengpu/stream3d.h`, `include/opengpu/build3d.h`, `library/opengpu/ogpu_3d.c/.h`, `library/opengpu/ogpu_build3d.c`, `library/warp3d/`, `include/Warp3D/`, `tests/test_3d.c` and its golden.
- **SDL 2 helper:** `library/modules/sdl2/`, `stubs/sdl2/`, `tests/sdl2/`.
- **Opcodes** live in `stream.h`, which the owner keeps. The Warp3D helper defines the layouts of 0x0030–0x0035 in `stream3d.h`. Reserved: 0x0018–0x001A, 0x0021 and 0x0030–0x0035.
  - **YUV moves from 0x0031 to 0x0022**, beside COMPOSITE_AFFINE, because 0x0031 sits inside the 3D range.


#### 5. Residency (8 October: "resident core, shared big parts")

##### The core: opengpu.library, resident from boot

opengpu.library holds 2D, the OpenGfx fork, the 3D rasteriser and the driver loader. It is built so that it can sit in the AmigaChrome boot ROM's resident list later, beside acrtg.card. What that needs:

- **RomTag first, RTF_AUTOINIT.** This is already so: `start()` then the RomTag, built with `-fno-toplevel-reorder`. For the ROM it becomes RTF_COLDSTART at a priority after expansion.library and before graphics.library's patches are installed. It should not be AFTERDOS, because OpenGfx patches graphics.library early.
- **No writable globals (done in 0.9).** Code in ROM can't write to itself.
  - Exec and dos.library are in the library base: exec as `lib_init` is given it (the same pointer as address 4), dos.library opened by the first DOS process that needs it. The library's functions take them from the base.
  - `drivers_load`'s name table (8 names of 32 bytes) is in the base too, used under the driver lock.
  - The module loader keeps its list in the base and is passed exec and dos.library on each call. OpenGfx's state has been in the base since 0.6.
  - `library/build.sh` checks it: `tools/hunk_rw_check.py` fails the build when opengpu.library has a data or BSS hunk with anything in it (0.7 had a BSS hunk of 264 bytes).
  - The same build then runs from RAM (LIBS:) or from ROM, flattened with `build/os3-autoboot/hunk_flatten.py` as acrtg.card is. Not done yet: the ROM build itself, and RTF_COLDSTART.
- **Nothing from disk at init.** Init sets up semaphores and adds the low-memory handler (below). Drivers load on first use from a DOS process, as 0.3 already does.
- **Resident drivers first (not built yet).** Before scanning `LIBS:OpenGPU/`, the library looks for drivers already resident (FindResident / the library list). ACRTG.gpu can then live in the ACRTG boot ROM next to acrtg.card, and a booted Cradle needs no driver file.
- **Stack.** The CPU back end runs on the caller's stack. The core struct is about 400 bytes on the stack; the 3D state (1.7 KB) comes from AllocVec, so patched graphics.library calls from small-stack tasks are safe.
- **OpenGfx's patches** are thin entry points in the core: `OGFX_InstallPatches` writes an 18-byte entry for each into the base and points graphics.library's vector at it (SetFunction at init once it is in ROM). They call the same code that programs reach through the LVOs, and OpenGfx keeps no writable globals (0.6).

##### Mesa and SDL 2: code loaded once, data per program

Chosen method: **-fbaserel32 (A4-relative data), with the per-program data made by OGPU_ModuleOpen. This is libnix's libinitr pattern, done by the module loader.**

- **Build.** The module (GL.module, SDL2.module) is built with `-fbaserel32 -mresident32` against libnix's `libb32` (both are in the os32-gcc16 stove). Code reaches every global and static, C++ statics included, through A4. The linker writes the table of data-to-data relocations.
- **Load once, then resident.** OGPU_ModuleOpen("SDL2") LoadSegs the module the first time a program asks. The code stays loaded and shared after the last program closes it, until the system resets or memory runs short. Nothing unloads it at a close (as built in 0.9: "Resident modules" below).
- **Data per program.** Each program's OGPU_ModuleOpen does this:
  - allocates a copy of the module's data and BSS;
  - copies the initial data in and applies the data-to-data relocations;
  - runs the module's constructors with A4 pointing at the copy;
  - returns a handle holding that A4.
  OGPU_ModuleClose runs the destructors and frees that copy.
- **Entry.** The program reaches the module only through its link stub: libSDL2.a (SDL's dynapi stub) or libGL.a (generated from Mesa's glapi XML).
  - Each stub function saves the program's A4, sets the instance's A4, copies its arguments (the generator knows each signature), calls, and restores A4.
  - Varargs calls are formatted in the stub first. SDL's dynapi already does this for SDL_SetError and SDL_Log.
  - Nothing in the module depends on A6.
- **Threads.** A thread the module starts (SDL's audio thread, Mesa's util_queue) gets the creating instance's A4. The pthread shim captures it at pthread_create and sets it at the new task's entry.
- **Callbacks into the program** (SDL's audio callback and event filters, GL debug callbacks) arrive with the module's A4 in place. That is harmless for ordinary programs. A program built -fbaserel itself must mark its callbacks __saveds, as Amiga callbacks always need.

**Why this method:**
- It keeps Mesa's and SDL's source as it is: no context structs threaded through Mesa's thousands of globals.
- It works for C++.
- The toolchain already has it (libb32, libinitr.o).
- It fits the one-library loader planned above.

A per-opener library base alone wouldn't do: SDL's and GL's calls go through function tables, not LVOs, so nothing would set A4 on entry.

**Order:**
1. First, per-program copies (LoadSeg per program, as in section 1). This gets SDL 2 and Mesa running.
2. Then the same modules rebuilt -fbaserel32, with the loader's per-program data. Programs don't change: the link stubs and OGPU_ModuleOpen hide it.

**What a user sees:** one copy of Mesa's and SDL's code in memory, a small data copy per running program, and no loading per program after the first.

##### Residency, step 2: as built (8 October 2026)

- **Two kinds, told apart by the first code.** A shared module (built `-fbaserel32`, linked `-resident32`) starts with a BRA.W over a header (`struct OGPUModuleHeader`: the magic "OGSM", the entry, `___a4_init`, `___data_size`, `___datadata_relocs`; `module_start.S`). Anything else is a module for each program and is loaded as in step 1. `opengpu.library` (`ogpu_module.c`) keeps shared modules in a list. (Step 2 found them again by `SameLock` on a lock it held, and unloaded one when its last program closed it; 0.9 changed both: "Resident modules" below.)
- **Each open:** a copy of the data and BSS from the data as loaded (which no program uses), the data-to-data relocations applied, then the entry with A4 on the copy (libnix's init list, the constructors). A stub on an older library calls the first code itself; that sets A4 on the data LoadSeg gave it, which is then the program's own.
- **Calls.** Every module table now starts with `struct OGPUModuleTable { version, a4, caller_a4 }`, and the tables are version 2 (`OGPU_MODULE_A4_VERSION`); a version 1 caller is refused, as it would call without A4. `libSDL2.a` sets A4 in C (built `-ffixed-a4`, `OGPU_A4`); `libGL.a`'s generated entries copy each call's arguments (their size from Mesa's glapi XML) below a saved A4 and A5. `gla_get_proc_address` hands out libGL.a's own entries.
- **Back into the program, and threads.** `ogpu_module_callout` calls the program with its A4 (SDL's audio callback, timers, thread functions, event filters and watchers). A thread a module starts takes the module's A4 from its `tc_TrapData`, set by its parent (libnix's `-resident32` convention): SDL's threads do. Mesa starts none here, and a shared GL.module refuses `pthread_create`, as the stove's libpthread would start one without A4.
- **Checked at build.** `tools/baserel_check.py` lists every absolute reference from the code into the data and every A4-relative one to something in the code; a module's `baserel.allow` names the reviewed ones (tables that are only read, and glsl's built-in struct field tables, which each program's constructors fill with the same values). It found libnix's `__initcpp` reaching the end of its list through A4, so `module_rt.c` has its own, and Kalms' c2p keeping its sizes in a BSS of its own, so it takes them in registers now.
- **Measured** on a scratch copy of the Showcase instance (AC090 68040, virgl): a first GL open takes 19.2 MB and a further one 345 KB (SDL 2: 1.06 MB, then 171 KB). Three softpipe Gears at once take 49.8 MB over idle, against 71.4 MB with a copy each; frame rates are as before.

##### Resident modules: as built (opengpu.library 0.9, 8 October 2026)

Step 2 still unloaded a shared module when its last program closed it, so each new GL program after a quiet spell loaded Mesa's 19 MB again. The decision was that the big parts stay resident once loaded. 0.9 does that:

- **Loaded on first use, then kept.** The first `OGPU_ModuleOpen` of a shared module loads it, as before. `OGPU_ModuleClose` frees only that program's data and never unloads the code. Modules are not loaded at boot, so a system that runs no GL program never holds Mesa.
- **Unloaded only when memory runs short.** The library adds an exec low-memory handler at init (`AddMemHandler`, exec 39 and later, priority 50, so before ramlib's handler expunges libraries). Exec runs it in the allocating task, under Forbid, when an allocation fails; `C:Avail FLUSH` makes one fail on purpose. It unloads every shared module no program has open and asks exec to try the allocation again. It never waits: when another task is inside the loader at that moment it does nothing that time (`AttemptSemaphore`). The library's expunge does the same unloading. Nothing else unloads a shared module; a reset clears them all.
  - The handler is needed because the expunge alone isn't enough: opengpu.library is nearly always open (OpenLook keeps it open for the look hook), and in the lab a flush while it was open unloaded nothing through the expunge.
- **Found again by file, with no lock held.** A loaded module is matched by the full name `NameFromLock` gives (so `PROGDIR:Test.module` from two drawers is two modules, and `LIBS:OpenGPU/GL.module` reached by two names is one), its date and its size. Step 2 held a lock on the file while it was loaded; a module kept for good would then have stopped an installer from replacing it, and unlocking from the low-memory handler isn't safe.
- **A replaced file is a new module.** When the file's date or size has changed, the next open loads the new file. Programs on the old copy keep it; the old copy goes at once if no program has it open, or at the next low-memory flush.
- **Modules for each program** (not built `-fbaserel32`; GL.module and SDL2.module are both shared) are still loaded at each open and unloaded at each close.
- **Measured** on a scratch copy of the OpenUp lab instance (OS 3.2.3, AC090 68040, ACRTG with virgl, OpenUp 0.6.18's GL.module and SDL2.module), 0.7 and the resident build (this change on 0.7) on the same copy the same hour. The full figures are in `measurements/20261008-resident-modules.txt`.
  - A GL program after all the others have closed: with 0.7 the open loads GL.module again (1,265 ms and 19,197 KB); with the resident build it takes 1 ms and 345 KB. SDL 2: 93 ms and 1,057 KB, now 1 ms and 171 KB.
  - Memory with 1, 2 and 3 Gears on virgl: 24.0, 29.4 and 34.8 MB over idle, the same with both. When all have ended: 0.02 MB over idle with 0.7, 18.4 MB with the resident build (GL.module kept). After `Avail FLUSH`: back to idle with both.
  - Frame rates (Mesa demos, testsprite2, W3DTest and MGLTest) are the same within the run-to-run spread.

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


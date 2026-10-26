# OpenGPU into a window: a design note

For Teams 3 and 4 to review. Written by Team 5 at Dale's request (25 October
2026). No code changes yet: the open questions below need your answers
first.

## The ask

Dale decided on 25 October that GFOffice draws smooth charts and pictures
through opengpu.library only, with no Cairo or Pixman.

Today a batch can only draw into memory: a `SURFACE` is an address, bytes per
row, a size and a format. So a program that wants anti-aliased drawing in a
window does three things:

1. draws into an ARGB32 buffer of its own;
2. copies the buffer into the window with `WritePixelArray`;
3. keeps the buffer, which is 1.5 MB for an 800 x 480 chart and 3.6 MB for
   a page at 100%.

The copy is a second pass over every pixel. On AmigaChrome it also takes the
pixels away from the board's GPU, which could have drawn them where they are
shown.

W139's assessment (gfoffice `docs/suite/cairo-rendering.md`, section 4) asked
for "a way to point an OpenGPU batch at a window's own surface, clipped by its
layer". This note proposes one.

## Where a window's pixels are

| The RastPort | Its pixels | OpenGPU can draw there |
| --- | --- | --- |
| No layer (a screen's own RastPort) | the bitmap | yes, if the bitmap is OpenRTG's |
| A classic layer, a ClipRect that shows | the screen's bitmap, inside the ClipRect | yes |
| A classic smart refresh layer, a ClipRect that is covered | its backing bitmap, pixel (x, y) at (x - (MinX & ~15), y - MinY) | yes, but its (0, 0) is not the layer's (question 1) |
| A classic simple refresh layer, a ClipRect that is covered | nowhere: Intuition asks for a refresh later | nothing to draw |
| A super bitmap layer | the super bitmap, scrolled | not proposed |
| A composited layer (OpenLayers 47.101 and up) | one ClipRect, covered, flagged `CR_OPENLAYERS_COMPOSED`, its BitMap the layer's surface, by the same x rule | yes, then the compositor must be told (question 2) |
| A planar (AGA) bitmap, CLUT8, or packed 24-bit | | no OpenGPU format for paths and blends: the program draws as today |

OpenRTG gives a bitmap's memory, bytes a row and format with
`ORTG_BitMapInfo`, as layers.library's composited mode uses it. OpenRTG's
32-bit pixels keep their top byte 0, so a screen's bitmap is `OGPU_FMT_XRGB32`
(stream v1.3), as the compositor treats it.

## Proposal: two calls in opengpu.library 0.21

```c
ULONG OGPU_RastPortFormat(struct RastPort *rp)(a1)
LONG  OGPU_SubmitRastPort(struct RastPort *rp, APTR body, ULONG words, ULONG flags)(a1,a0,d0,d1)
```

**`OGPU_RastPortFormat`** answers the OpenGPU format a batch for `rp` draws
in, or 0 where it can't draw. The format is `OGPU_FMT_XRGB32` for OpenRTG's
32-bit bitmaps, `OGPU_FMT_RGB565` for its 16-bit ones, and `OGPU_FMT_ARGB32` for
a composited layer with `LAYERLOOK_ALPHA`. A program then asks
`OGPU_Query(op, format)` as it does now, and draws as it does today when either
answer is no.

**`OGPU_SubmitRastPort`** draws `body` into the RastPort. `body` is a batch
whose slot 0 is the RastPort, in the RastPort's coordinates, with (0, 0) at the
layer's top left, as graphics.library's calls have it. Slots 1 to 15 are the
program's own. The body doesn't use `TARGET` or `CLIP`, since the library sets
them for each piece. To clip, a program uses `PATH_MASK` or an A8 mask.

The call does this:

1. It locks the layer (`LockLayerRom`), as OpenGfx's `core_begin` does.
2. For each piece of the table above where the RastPort's pixels are, it
   runs `SURFACE 0` (the piece's memory, with its (0, 0) at the layer's),
   then `TARGET 0` and `CLIP` (the piece), then the body. All pieces go in
   one batch: the prefix and the body are copied into a buffer the library
   keeps.
3. It waits for the batch, so the layer is never unlocked under a running
   batch, and then unlocks the layer.
4. For a composited layer, it marks the box that was drawn (question 2).

It answers the batch's first error. `flags` is 0 for now.

## Open questions

1. **Where slot 0 starts.** For the screen's bitmap and for a composited
   layer's surface, the layer's (0, 0) is inside the same memory. So
   `SURFACE` can start there: the bitmap plus `MinY * bpr + MinX * bpp`, or the
   surface plus `(MinX & 15) * bpp`. A smart refresh layer's backing bitmap
   holds only its piece, so the layer's (0, 0) is outside it. The ways round
   that:
   - **(a)** A stream v1.8 command, `ORIGIN dx, dy`, which moves the target's
     (0, 0). The core would offset the target's pointer, and its bounds would
     be the clip. This is the one we propose: it is small, and drivers that
     don't know it leave such batches to the CPU, as for any newer command.
   - **(b)** A `SURFACE` address before the bitmap's memory. The CPU back end
     never reads outside the clip, so it would work there. A host back end's
     `map` may refuse it.
   - **(c)** The library draws covered pieces into a scratch buffer and
     copies them across. That is the copy this note wants to avoid.
   - **(d)** Only the pieces that show are drawn, and a smart refresh window's
     covered parts are left for a refresh. That changes what smart refresh
     means.
2. **Telling the compositor.** layers.library's composited mode finds out
   what was drawn through its thunks on graphics.library, cybergraphics.library
   and openrtg.library calls (openamigagfx `docs/design/compositing.md`,
   "The marks"). A batch drawn
   straight into a surface goes through none of them, so it would not show
   until something else is drawn there. The options:
   - layers.library gives a call that marks a box of a layer;
   - or OpenRTG, or OpenGPU, has a damage callback for a layer's surface.
     This is wish 1 in that file's list for Team 4.

   GFOffice's windows are on the Workbench, which is composited by default
   on AmigaChrome, so this one is needed from the start.
3. **The card part.** A composited surface may be in the board's card part
   (`ORTG_BMI_CARD`, acrtg-v7), which the CPU reads "at a crawl". A batch
   for such a piece must go to the driver. If it would run on the CPU, the
   call should answer `OGPU_ERR_UNSUPPORTED`, so the program draws as today.
   A real 68040 has no card part, so this happens only on AmigaChrome when
   the driver can't draw the batch.
4. **Locks.** The order is the layer's lock first and OpenGPU's semaphores
   last, as for every drawing call. The call waits for its batch, so it
   costs a program that draws often one fence wait each time; GFOffice draws
   a paint as one batch.
5. **Where the code goes, and its size.** `opengpu_lib.c`, beside OpenGfx's
   layer walk. It needs openrtg.library open for `ORTG_BitMapInfo`. We think
   it is about 1 to 1.5 KB of code, and a page of `ORIGIN` in the core.

## What GFOffice would do with it

- **OpenWrite's `draw_picture`:** one `OGPU_SubmitRastPort` per paint, with
  a `COMPOSITE` and `BILINEAR` from the cached picture in slot 1. No band
  buffer and no `WritePixelArray`.
- **OpenCalc's charts:** on a 68040, the chart is drawn once into a cached
  ARGB32 picture, which then goes into the window with one `COPY` or
  `COMPOSITE` per paint. On AmigaChrome it can be drawn straight into the
  window.
- **Palette and AGA screens** answer 0 from `OGPU_RastPortFormat` and keep
  today's drawing, as Dale decided.

## Not done here

This note adds no code. The calls change opengpu.library's API (two LVOs and
a stream command), and question 2 needs the compositor's owners. Team 5 will
write it as a PR for your review once questions 1 to 3 are answered.

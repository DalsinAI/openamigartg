/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ACVirgl: the request blocks OGPU_OP_VIRGL carries (stream.h) from Mesa's
 * virgl driver on the Amiga to virglrenderer in the Cradle's runtime
 * (host/virgl/acvirgl.c), so GL and GLES draw on the PC's graphics chip.
 * The same calls a virtio-gpu guest makes through its kernel, as commands
 * in Amiga memory.
 *
 * A block is a run of commands. Each starts with a header word (the command
 * in the top 16 bits, its length in words, header included, in the bottom
 * 16) and a result word the host writes: ACV_OK (0), a value the command
 * names, or an ACV_ERR_. Words are big-endian, as in an OGPU stream.
 * Addresses are the Amiga's own. The host carries the commands out in
 * order and is done with all of them when the OGPU_OP_VIRGL command is (a
 * FENCE after it says so).
 *
 * Two things are little-endian, as virglrenderer wants them:
 *   - SUBMIT's command stream: Mesa's virgl encoder writes it so on every
 *     CPU (the 68k's encoder writes each word little-endian, and strings,
 *     such as shader text, as bytes);
 *   - CAPS' result is turned into the Amiga's order, word by word, so the
 *     68k reads struct virgl_caps_v2 as it is.
 * Pixels and buffer data are in the Amiga's memory as the 68k has them.
 * PUT and GET name the size of the units to turn round (ACV_SWAP_), which
 * the Amiga side knows from the resource's format or a buffer's use: 4 for
 * floats and 32-bit indices, 2 for 16-bit ones, 1 for bytes. */
#ifndef OPENGPU_VIRGL_H
#define OPENGPU_VIRGL_H

#define ACV_PROTOCOL            1

#define ACV_HDR(cmd, words)     (((unsigned long)(cmd) << 16) | ((unsigned long)(words) & 0xFFFFUL))
#define ACV_HDR_CMD(w)          ((unsigned)((w) >> 16))
#define ACV_HDR_WORDS(w)        ((unsigned)((w) & 0xFFFFUL))

/* Commands. Arguments after the header and result words:                          words */
#define ACV_HELLO           1   /* result: ACV_PROTOCOL; then writes the host's flags  3 */
#define ACV_CAPSET_INFO     2   /* set; then writes max version, max size              5 */
#define ACV_CAPS            3   /* set, version, address, bytes: the caps, in the
                                   Amiga's order; result: bytes written                6 */
#define ACV_CTX_CREATE      4   /* context, name address, name bytes                   5 */
#define ACV_CTX_DESTROY     5   /* context                                             3 */
#define ACV_RES_CREATE      6   /* handle, target, format, bind, width, height, depth,
                                   array size, last level, samples, flags, context
                                   (attached to it; 0 none)                          14 */
#define ACV_RES_UNREF       7   /* handle                                              3 */
#define ACV_ATTACH          8   /* context, handle                                     4 */
#define ACV_DETACH          9   /* context, handle                                     4 */
#define ACV_SUBMIT         10   /* context, address, words (little-endian words)       5 */
#define ACV_PUT            11   /* handle, context, level, stride, layer stride, x, y,
                                   z, w, h, d, address, bytes, swap: Amiga memory into
                                   the resource's box                                16 */
#define ACV_GET            12   /* the same, the box into Amiga memory               16 */

/* PUT's and GET's swap: the units turned round on the way. */
#define ACV_SWAP_NONE       1
#define ACV_SWAP_16         2
#define ACV_SWAP_32         4
#define ACV_SWAP_64         8

/* HELLO's flags. */
#define ACV_HOST_GPU        1   /* virglrenderer draws on a graphics chip (else llvmpipe or the like) */
#define ACV_HOST_GLES       2   /* ... through GLES */

/* Results. */
#define ACV_OK              0
#define ACV_ERR_BADCMD     -1   /* an unknown command, or a wrong length (the rest is skipped) */
#define ACV_ERR_NOMAP      -2   /* an address the host can't reach */
#define ACV_ERR_RENDERER   -3   /* virglrenderer said no */
#define ACV_ERR_NOTREADY   -4   /* no virglrenderer in this runtime */

#endif

#!/usr/bin/env python3
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""Records an AmigaChrome instance's sound ring into a WAV file, for the
OpenAL lab (allab.c, allab_compare.py). The runtime writes each board's
output to a ring file ("ACA1": rate, capacity and the frames written so
far in its header, then 16-bit stereo little-endian frames): Paula's is
PORT.aud and ACAHI's, the AHI board's, PORT.ahi beside it (the bridge's
native_bridge.py). This reads the ring as it fills, from the frame it was
at when it started, so it hears everything the board plays meanwhile.

    ahi_ring_rec.py RING OUT.wav SECONDS [STOPFILE]

It stops after SECONDS, or as soon as STOPFILE exists, and says how many
frames it got and whether it ever fell a whole ring behind."""
import mmap
import os
import struct
import sys
import time


def main() -> int:
    ring_path, out_path, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
    stop = sys.argv[4] if len(sys.argv) > 4 else None
    fd = os.open(ring_path, os.O_RDONLY)
    ring = mmap.mmap(fd, 0, prot=mmap.PROT_READ)
    os.close(fd)
    magic, rate, cap = struct.unpack_from("<3I", ring, 0)
    pos = struct.unpack_from("<Q", ring, 16)[0]
    first = pos
    data = bytearray()
    behind = 0
    end = time.time() + seconds
    while time.time() < end and not (stop and os.path.exists(stop)):
        total = struct.unpack_from("<Q", ring, 16)[0]
        if total - pos > cap:
            behind += 1
            pos = total - cap
        while pos < total:
            at = pos % cap
            n = min(total - pos, cap - at)
            data += ring[32 + at * 4: 32 + (at + n) * 4]
            pos += n
        time.sleep(0.05)
    frames = len(data) // 4
    with open(out_path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 2, rate, rate * 4, 4, 16) + b"data" + struct.pack("<I", len(data)))
        f.write(data)
    print(f"ahi_ring_rec: {out_path}: {frames} frames at {rate} Hz from frame {first} of the ring "
          f"({magic:#x}, {cap} frames); {behind} times a ring behind")
    return 0 if not behind else 1


if __name__ == "__main__":
    sys.exit(main())

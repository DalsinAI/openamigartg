#!/usr/bin/env python3
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""Sets a recording of an instance's sound ring (ahi_ring_rec.py) while
ALLab played its scenes (allab.c) against the PC's references
(allab_ref.c): the tone, the sweep and the stream, each found in the
recording in turn, after the one before.

    allab_compare.py RECORDING.wav REFDIR voices|mix [--scale] [--from=SECONDS]

Each scene is found by its level, 10 ms at a time (the left and right
levels set against the reference's), then to the frame by the
correlation of the samples. Then:
  - the tone and the stream: the recording less the reference, frame for
    frame: the frames that differ, the largest difference, and the signal
    to difference ratio; a gap or a repeat in a stream makes every frame
    after it differ;
  - the sweep, whose steps fall where the Amiga's 20 ms polls fell rather
    than exactly every 882 frames: each channel's level, 20 ms at a time,
    against the reference's (largest and mean difference in dB, where the
    reference is louder than -50 dB), and the ratio as for the others.
--scale fits one gain to each scene first (Mix through ahi.device's
16-bit mode, where the unit's 4 channels halve it), and prints it.
Needs numpy. Prints one line a scene and exits 0."""
import struct
import sys

import numpy as np


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    pos = 12
    rate = 44100
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        if cid == b"fmt ":
            rate = struct.unpack_from("<I", data, pos + 12)[0]
        if cid == b"data":
            pcm = np.frombuffer(data[pos + 8:pos + 8 + size], dtype="<i2").astype(np.float64)
            return pcm.reshape(-1, 2), rate
        pos += 8 + size + (size & 1)
    raise SystemExit(f"{path}: no data")


def levels(x, block):
    n = len(x) // block
    return np.sqrt(np.mean(x[:n * block].reshape(n, block, 2) ** 2, axis=1) + 1.0)


def correlate(a, b):
    """Cross-correlation of a (long) with b, for every placement of b in a."""
    n = len(a) + len(b)
    size = 1 << (n - 1).bit_length()
    fa = np.fft.rfft(a, size)
    fb = np.fft.rfft(b[::-1], size)
    full = np.fft.irfft(fa * fb, size)
    return full[len(b) - 1:len(a)]


def find(rec, ref, start):
    """Where ref starts in rec, at or after start: its levels with a
    quarter of a second of silence each side (the scenes are half a second
    apart), then its samples."""
    block, pad = 441, 25 * 441
    padded = np.concatenate([np.zeros((pad, 2)), ref, np.zeros((pad, 2))])
    lr, lf = np.log(levels(rec[start:], block)), np.log(levels(padded, block))
    lr -= lr.mean(axis=0)
    lf -= lf.mean(axis=0)
    if len(lr) < len(lf):
        return None
    score = sum(correlate(lr[:, c], lf[:, c]) for c in range(2))
    coarse = start + int(np.argmax(score)) * block + pad
    lo = max(start, coarse - pad)
    hi = min(len(rec), coarse + len(ref) + pad)
    seg = rec[lo:hi]
    if len(seg) < len(ref):
        return None
    fine = sum(correlate(seg[:, c], ref[:, c]) for c in range(2))
    return lo + int(np.argmax(fine))


def compare(name, rec, ref, at, scale, sweep):
    got = rec[at:at + len(ref)]
    g = 1.0
    if scale:
        g = float(np.sum(got * ref) / max(np.sum(ref * ref), 1.0))
    diff = got - g * ref
    sig = float(np.sum((g * ref) ** 2))
    noise = float(np.sum(diff ** 2))
    snr = 10.0 * np.log10(sig / noise) if noise > 0 else float("inf")
    out = {"scene": name, "at": at, "frames": len(ref), "gain": round(g, 4), "snr_db": round(snr, 1),
           "max_diff": int(np.max(np.abs(diff))), "frames_differing": int(np.sum(np.any(np.abs(diff) > 0.5, axis=1))),
           "frames_off_by_more_than_2": int(np.sum(np.any(np.abs(diff) > 2.5, axis=1)))}
    if sweep:
        lr, lf = levels(got, 882), levels(g * ref, 882)
        loud = 20 * np.log10(lf) > 20 * np.log10(32768.0) - 50
        d = np.abs(20 * np.log10(lr) - 20 * np.log10(lf))[loud]
        out["level_db_max"] = round(float(np.max(d)), 2) if d.size else None
        out["level_db_mean"] = round(float(np.mean(d)), 3) if d.size else None
    return out


def main() -> int:
    rec_path, refdir, kind = sys.argv[1], sys.argv[2], sys.argv[3]
    scale = "--scale" in sys.argv[4:]
    rec, rate = read_wav(rec_path)
    if rate != 44100:
        raise SystemExit(f"{rec_path}: {rate} Hz, not 44100")
    start = 0
    for a in sys.argv[4:]:
        if a.startswith("--from="):                 # seconds into the recording where the scenes begin
            start = int(float(a[7:]) * rate)
    for name in ("tone", "sweep", "stream"):
        ref, _ = read_wav(f"{refdir}/ref_{kind}_{name}.wav")
        at = find(rec, ref, start)
        if at is None:
            print(f"allab_compare: {name}: not found after frame {start}")
            return 1
        r = compare(name, rec, ref, at, scale, name == "sweep")
        print("allab_compare: " + ", ".join(f"{k} {v}" for k, v in r.items()))
        start = at + len(ref)
    return 0


if __name__ == "__main__":
    sys.exit(main())

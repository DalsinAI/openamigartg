#!/bin/sh
# Makes the pictures and sounds tests/sdl2/satladder.c reads, on the x86 or
# ARM64 cores (Python's Pillow, avifenc and FFmpeg with libvorbis and
# libmp3lame). Copyright (c) 2026 Dalsin Limited. MIT licence (LICENSE).
#
# usage: tests/sdl2/make-satmedia.sh OUTDIR
set -eu
OUT=${1:?usage: make-satmedia.sh OUTDIR}
mkdir -p "$OUT"
python3 - "$OUT" <<'PY'
import struct, sys
from PIL import Image
out = sys.argv[1]
W, H = 640, 480
im = Image.new('RGB', (W, H))
px = im.load()
for y in range(H):
    for x in range(W):
        px[x, y] = ((x * 255) // (W - 1), (y * 255) // (H - 1), ((x * x + y * 3) >> 4) & 255)
im.save(out + '/photo.jpg', quality=90)
im.save(out + '/photo.png')
im.save(out + '/photo.webp', quality=90)
im.save(out + '/photo.tga')
im.save(out + '/photo-src.png')
pal = im.convert('P', palette=Image.ADAPTIVE, colors=64)
pal.save(out + '/pal.png')
# A bigger picture with detail, as a photo has: 1920x1080.
from PIL import ImageChops, ImageFilter
big = im.resize((1920, 1080), Image.BICUBIC)
noise = Image.effect_noise((1920, 1080), 48).convert('RGB').filter(ImageFilter.GaussianBlur(1))
big = ImageChops.add(ImageChops.multiply(big, Image.new('RGB', big.size, (200, 200, 200))), noise, 1.0, -40)
big.save(out + '/big.jpg', quality=85)
big.save(out + '/big.png')
# An IFF ILBM: 256 colours, uncompressed, interleaved planes.
p8 = im.convert('P', palette=Image.ADAPTIVE, colors=256)
cmap = bytes(p8.getpalette()[:768])
idx = p8.load()
rowbytes = ((W + 15) // 16) * 2
body = bytearray()
for y in range(H):
    for plane in range(8):
        row = bytearray(rowbytes)
        for x in range(W):
            if (idx[x, y] >> plane) & 1:
                row[x >> 3] |= 0x80 >> (x & 7)
        body += row
bmhd = struct.pack('>HHhhBBBBHBBhh', W, H, 0, 0, 8, 0, 0, 0, 0, 10, 11, W, H)
def chunk(t, d):
    return t + struct.pack('>I', len(d)) + d + (b'\0' if len(d) & 1 else b'')
form = b'ILBM' + chunk(b'BMHD', bmhd) + chunk(b'CMAP', cmap) + chunk(b'BODY', bytes(body))
open(out + '/photo.iff', 'wb').write(b'FORM' + struct.pack('>I', len(form)) + form)
# A 4-channel ProTracker module: a square wave sample, one pattern of notes
# on all four channels, played twice (about 13 seconds).
mod = bytearray(b'satladder'.ljust(20, b'\0'))
for i in range(31):
    s = bytearray(30)
    if i == 0:
        s[0:6] = b'square'
        s[22:24] = struct.pack('>H', 32)      # 64 bytes
        s[25] = 64                            # volume
        s[26:28] = struct.pack('>H', 0)
        s[28:30] = struct.pack('>H', 32)      # loop the whole sample
    else:
        s[28:30] = struct.pack('>H', 1)
    mod += s
mod += bytes([2, 127]) + bytes(128) + b'M.K.'
periods = [428, 381, 339, 320, 285, 254, 226, 214]
pat = bytearray()
for row in range(64):
    for ch in range(4):
        if row % 4 == 0:
            p = periods[(row // 4 + ch * 2) % 8] >> (ch & 1)
            pat += struct.pack('>BBBB', (p >> 8) & 0x0F, p & 0xFF, 0x10, 0)
        else:
            pat += bytes(4)
mod += pat + bytes([0x40] * 32 + [0xC0] * 32)
open(out + '/tune.mod', 'wb').write(mod)
PY
avifenc -q 80 "$OUT/photo-src.png" "$OUT/photo.avif" >/dev/null
rm -f "$OUT/photo-src.png"
for f in wav ogg mp3 flac; do
    case $f in ogg) c="-c:a libvorbis -q:a 4" ;; mp3) c="-c:a libmp3lame -b:a 128k" ;; *) c= ;; esac
    ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=10" \
        -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=10" \
        -filter_complex "[0:a][1:a]amerge=inputs=2[a]" -map "[a]" $c "$OUT/tone.$f"
done
ls -l "$OUT"

#!/usr/bin/env python3
"""Decode an IFF ILBM (ByteRun1 or uncompressed, CMAP palette, no HAM) to PNG.  ilbm2png.py in.iff out.png"""
import struct, sys
from PIL import Image

def chunks(d, o, end):
    while o + 8 <= end:
        cid, ln = d[o:o + 4], struct.unpack('>I', d[o + 4:o + 8])[0]
        yield cid, o + 8, ln
        o += 8 + ln + (ln & 1)

def decode(d):
    assert d[:4] == b'FORM' and d[8:12] == b'ILBM'
    bmhd = cmap = body = camg = None
    for cid, o, ln in chunks(d, 12, 8 + struct.unpack('>I', d[4:8])[0]):
        if cid == b'BMHD': bmhd = struct.unpack('>HHhhBBBBHBBhh', d[o:o + 20])
        elif cid == b'CMAP': cmap = d[o:o + ln]
        elif cid == b'BODY': body = d[o:o + ln]
        elif cid == b'CAMG': camg = struct.unpack('>I', d[o:o + 4])[0]
    w, h, _, _, nplanes, masking, comp = bmhd[:7]
    rowbytes = ((w + 15) // 16) * 2
    nrows = nplanes + (1 if masking == 1 else 0)
    out, i = bytearray(), 0
    need = rowbytes * nrows * h
    while len(out) < need and i < len(body):
        if comp == 0:
            out += body[i:i + need]; break
        n = body[i]; i += 1
        if n < 128: out += body[i:i + n + 1]; i += n + 1
        elif n > 128: out += bytes([body[i]]) * (257 - n); i += 1
    pal = [tuple(cmap[k * 3:k * 3 + 3]) for k in range(len(cmap) // 3)]
    img = Image.new('RGB', (w, h))
    px = img.load()
    for y in range(h):
        base = y * rowbytes * nrows
        for x in range(w):
            v = 0
            for p in range(nplanes):
                if out[base + p * rowbytes + x // 8] >> (7 - x % 8) & 1: v |= 1 << p
            px[x, y] = pal[v] if v < len(pal) else (255, 0, 255)
    return img, (w, h, nplanes, comp, camg)

if __name__ == '__main__':
    img, info = decode(open(sys.argv[1], 'rb').read())
    print(info)
    img.save(sys.argv[2])

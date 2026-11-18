#!/usr/bin/env python3
"""Decode a Toaster effect file: FORM ANIM (ILBM + op 5 DLTA frames), the crouton ILBM and the CrUD chunk.
anim.py <effect> <outprefix>  -> <outprefix>-NN.png for each frame (values scaled to grey), prints info."""
import struct, sys
from PIL import Image

def chunks(d, o, end):
    while o + 8 <= end:
        cid, ln = d[o:o + 4], struct.unpack('>I', d[o + 4:o + 8])[0]
        yield cid, o + 8, ln
        o += 8 + ln + (ln & 1)

def unpack_body(body, rowbytes, nplanes, h, comp):
    out, i, need = bytearray(), 0, rowbytes * nplanes * h
    if comp == 0: return bytearray(body[:need])
    while len(out) < need and i < len(body):
        n = body[i]; i += 1
        if n < 128: out += body[i:i + n + 1]; i += n + 1
        elif n > 128: out += bytes([body[i]]) * (257 - n); i += 1
    return out

def op5(planes, dlta, rowbytes, h):
    if len(dlta) < 64: return
    ptrs = struct.unpack(">16I", dlta[:64])
    for p in range(len(planes)):
        o = ptrs[p]
        if not o: continue
        pl = planes[p]
        for col in range(rowbytes):
            ops = dlta[o]; o += 1; row = 0
            for _ in range(ops):
                op = dlta[o]; o += 1
                if op == 0:
                    cnt, val = dlta[o], dlta[o + 1]; o += 2
                    for _ in range(cnt): pl[row * rowbytes + col] = val; row += 1
                elif op & 0x80:
                    cnt = op & 0x7f
                    for _ in range(cnt): pl[row * rowbytes + col] = dlta[o]; o += 1; row += 1
                else:
                    row += op

def load(path):
    d = open(path, 'rb').read()
    frames, info, crud = [], {}, {}
    for cid, o, ln in chunks(d, 0, len(d)):
        if cid != b'FORM': continue
        kind = d[o:o + 4]
        if kind == b'ANIM':
            bufs = None
            for c2, o2, l2 in chunks(d, o + 4, o + ln):
                sub = {c: d[oo:oo + ll] for c, oo, ll in chunks(d, o2 + 4, o2 + l2)}
                if b'BMHD' in sub:
                    w, h, _, _, npl, mask, comp = struct.unpack('>HHhhBBB', sub[b'BMHD'][:11])
                    rb = ((w + 15) // 16) * 2
                    raw = unpack_body(sub[b'BODY'], rb, npl, h, comp)
                    pls = [bytearray(rb * h) for _ in range(npl)]
                    for y in range(h):
                        for p in range(npl):
                            pls[p][y * rb:(y + 1) * rb] = raw[(y * npl + p) * rb:(y * npl + p + 1) * rb]
                    bufs = [[bytearray(x) for x in pls], [bytearray(x) for x in pls]]
                    info = dict(w=w, h=h, planes=npl, cmap=sub.get(b'CMAP', b'').hex())
                    frames.append([bytes(x) for x in pls]); k = 0
                elif b'DLTA' in sub:
                    k += 1
                    ah = sub[b'ANHD']; interleave = ah[18]
                    b = bufs[k % 2] if interleave != 1 else bufs[0]
                    op5(b, sub[b'DLTA'], rb, h)
                    frames.append([bytes(x) for x in b])
        elif kind == b'CrUD':
            for c2, o2, l2 in chunks(d, o + 4, o + ln): crud[c2.decode()] = d[o2:o2 + l2]
    return frames, info, crud

if __name__ == '__main__':
    frames, info, crud = load(sys.argv[1])
    print(info, len(frames), 'frames', {k: v.hex()[:64] for k, v in crud.items()})
    w, h, rb = info['w'], info['h'], ((info['w'] + 15) // 16) * 2
    for i, f in enumerate(frames):
        im = Image.new('L', (w, h)); px = im.load()
        for y in range(h):
            for x in range(w):
                v = sum(((f[p][y * rb + x // 8] >> (7 - x % 8)) & 1) << p for p in range(len(f)))
                px[x, y] = v * 255 // ((1 << len(f)) - 1)
        im.save('%s-%02d.png' % (sys.argv[2], i))

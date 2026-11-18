#!/usr/bin/env python3
"""Dump the AlgoFX parameter block (CrUD TAGS 0x1000001c) of ALGO croutons as signed words.  algoblob.py <effect>..."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(__file__)); import anim

def crud(path):
    for src in (open(path, 'rb').read(), open(path + '.i', 'rb').read() if os.path.exists(path + '.i') else b''):
        for cid, o, ln in anim.chunks(src, 0, len(src)):
            if cid == b'FORM' and src[o:o + 4] == b'CrUD':
                return {c: src[oo:oo + ll] for c, oo, ll in anim.chunks(src, o + 4, o + ln)}
    return {}

def tags(t):
    out, o = {}, 0
    while o + 8 <= len(t):
        i, v = struct.unpack('>II', t[o:o + 8])
        if not i: break
        if i & 0x10000000:
            out[i] = t[o + 8:o + 8 + v]; o += 8 + ((v + 3) & ~3); continue
        out[i] = v; o += 8
    return out

if __name__ == '__main__':
    for p in sys.argv[1:]:
        tg = tags(crud(p).get(b'TAGS', b''))
        blob = tg.get(0x1000001c, b'')
        w = struct.unpack('>%dh' % (len(blob) // 2), blob[:len(blob) // 2 * 2])
        print('== %s  type %s mode %s  (%d words)' % (os.path.basename(p), tg.get(0x80000063), tg.get(0x80000007), len(w)))
        print('   ' + ' '.join(str(x) for x in w))

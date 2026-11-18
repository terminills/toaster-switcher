#!/usr/bin/env python3
"""Parse an AmigaOS hunk executable: hunks (type, mem flags, data), RELOC32 tables.
python3 hunk.py <exe> [outdir]  -> prints a summary; with outdir writes hNN.bin and hNN.rel (offset target_hunk target_value)."""
import struct, sys, os, json

T = {0x3e9: 'CODE', 0x3ea: 'DATA', 0x3eb: 'BSS'}

def load(path):
    d = open(path, 'rb').read()
    o = 0
    def L():
        nonlocal o
        v = struct.unpack_from('>I', d, o)[0]; o += 4; return v
    assert L() == 0x3f3
    while L():
        pass
    n, first, last = L(), L(), L()
    sizes = [L() for _ in range(last - first + 1)]
    hunks = []
    cur = None
    while o < len(d):
        t = L() & 0x3fffffff
        if t in (0x3e9, 0x3ea):
            ln = L() & 0x3fffffff
            cur = {'type': T[t], 'data': d[o:o + 4 * ln], 'relocs': [], 'size': (sizes[len(hunks)] & 0x3fffffff) * 4,
                   'mem': sizes[len(hunks)] >> 30}
            o += 4 * ln
        elif t == 0x3eb:
            ln = L() & 0x3fffffff
            cur = {'type': 'BSS', 'data': b'', 'relocs': [], 'size': (sizes[len(hunks)] & 0x3fffffff) * 4,
                   'mem': sizes[len(hunks)] >> 30}
        elif t == 0x3ec:
            while True:
                k = L()
                if not k:
                    break
                th = L()
                for _ in range(k):
                    cur['relocs'].append((L(), th))
        elif t == 0x3f0:
            while True:
                k = L()
                if not k:
                    break
                o += 4 * k + 4
        elif t == 0x3f1:
            ln = L(); o += 4 * ln
        elif t == 0x3f2:
            hunks.append(cur); cur = None
        else:
            raise SystemExit('unknown hunk type %x at %x' % (t, o - 4))
    return hunks

if __name__ == '__main__':
    hs = load(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else None
    if out:
        os.makedirs(out, exist_ok=True)
    for i, h in enumerate(hs):
        print('%3d %-4s mem=%d size=%7d data=%7d relocs=%5d' % (i, h['type'], h['mem'], h['size'], len(h['data']), len(h['relocs'])))
        if out:
            open('%s/h%02d.bin' % (out, i), 'wb').write(h['data'])
            with open('%s/h%02d.rel' % (out, i), 'w') as f:
                for off, th in sorted(h['relocs']):
                    v = struct.unpack_from('>I', h['data'], off)[0]
                    f.write('%x %d %x\n' % (off, th, v))
    if out:
        json.dump([{'type': h['type'], 'mem': h['mem'], 'size': h['size']} for h in hs], open(out + '/hunks.json', 'w'))

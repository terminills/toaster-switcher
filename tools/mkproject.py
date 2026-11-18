#!/usr/bin/env python3
"""Write a Toaster 4.x project of croutons.  mkproject.py <out> <toaster-root> <name-or-relpath>...
Each name is found under <root>/effects; entries are Toaster:<relpath> as the original projects store them."""
import os, struct, sys

def find(root, n):
    if os.path.exists(os.path.join(root, n)): return n
    for r, ds, fs in os.walk(os.path.join(root, 'effects')):
        if n in fs: return os.path.relpath(os.path.join(r, n), root)
    sys.exit('not found: ' + n)

out, root = sys.argv[1], sys.argv[2]
b = bytearray(open(os.path.join(os.path.dirname(__file__), '..', 'rig', 'orig-4.2-programs', 'defaultproject.orig'), 'rb').read()[:44])
for n in sys.argv[3:]:
    rel = find(root, n)
    name = ('Toaster:' + rel).encode() + b'\0'
    if len(name) & 1: name += b'\0'
    body = struct.pack('>4sI16x', b'ALGO', 0) + struct.pack('>H', len(name)) + name + struct.pack('>I', 0)
    b += struct.pack('>I', len(body)) + body
    print(rel)
b += struct.pack('>I', 0)
open(out, 'wb').write(b)

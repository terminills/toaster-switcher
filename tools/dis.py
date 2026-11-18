#!/usr/bin/env python3
"""Disassemble every CODE hunk of a hunk executable with objdump and replace relocated
absolute operands with hunk-relative symbols (H<hunk>_<hexoff>).
python3 dis.py <exe> <outdir>  -> <outdir>/hNN.s per code hunk, <outdir>/insns.tsv (hunk off len mnem ops)"""
import sys, os, re, subprocess, struct
sys.path.insert(0, os.path.dirname(__file__))
import hunk

OD = os.environ.get('M68K_OBJDUMP', 'm68k-aros-objdump')        # any binutils objdump for m68k
LINE = re.compile(r'^\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(\S+)\s*(.*)$')
CONT = re.compile(r'^\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*$')

def sym(h, v):
    return 'H%02d_%x' % (h, v)

def main(exe, out):
    hs = hunk.load(exe)
    os.makedirs(out, exist_ok=True)
    tsv = open(out + '/insns.tsv', 'w')
    for i, h in enumerate(hs):
        if h['type'] == 'BSS' or not h['data']:
            continue
        raw = '%s/h%02d.bin' % (out, i)
        open(raw, 'wb').write(h['data'])
        rel = {off: th for off, th in h['relocs']}
        txt = subprocess.run([OD, '-D', '-b', 'binary', '-m', 'm68k:68040', raw], capture_output=True, text=True).stdout
        ins = []
        for ln in txt.splitlines():
            m = LINE.match(ln)
            if m:
                ins.append([int(m.group(1), 16), m.group(3), m.group(4)])
        ins.append([len(h['data']), '', ''])
        with open('%s/h%02d.s' % (out, i), 'w') as f:
            for k in range(len(ins) - 1):
                off, mn, ops = ins[k]
                end = ins[k + 1][0]
                for r in range(off, end - 3, 2):
                    if r in rel:
                        th = rel[r]
                        v = struct.unpack_from('>I', h['data'], r)[0]
                        # objdump prints absolute values as 0x...; branch targets inside this hunk are also 0x... (pc-relative, not relocated)
                        ops = re.sub(r'0x0*%x\b' % v if v else r'\b0x0\b|\b0\b', sym(th, v), ops, count=1) if v else ops + '  ; ' + sym(th, 0)
                ops = re.sub(r'\b0x([0-9a-f]+)\b', lambda m: sym(i, int(m.group(1), 16)) if mn.startswith(('b', 'db', 'f')) and not mn.startswith(('bset', 'bclr', 'btst', 'bchg', 'bfext', 'bfins', 'bfset', 'bfclr', 'bftst', 'bfffo', 'bfchg')) else m.group(0), ops)
                f.write('%s_%05x  %-8s %s\n' % ('H%02d' % i, off, mn, ops))
                tsv.write('%d\t%x\t%d\t%s\t%s\n' % (i, off, end - off, mn, ops))
        print('hunk %d: %d insns' % (i, len(ins) - 1))

main(sys.argv[1], sys.argv[2])

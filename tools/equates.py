#!/usr/bin/env python3
"""Recover symbol values (struct offsets, constants) the missing include files defined, from aligned
source/binary instruction pairs. python3 equates.py <pairs> <out.tsv>   out: SYMBOL value votes conflicts"""
import re, sys
from collections import defaultdict, Counter

def split_ops(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(' : depth += 1
        if ch == ')' : depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def num(x):
    x = x.strip()
    try:
        return int(x, 16) if x.lower().startswith(('0x', '-0x')) else int(x)
    except ValueError:
        return None

IDENT = r'[A-Za-z_][\w.]*'
REG = {'a6': 'fp', 'a7': 'sp'}

votes = defaultdict(Counter)
for ln in open(sys.argv[1], encoding='latin-1'):
    mod, no, text, addr, bops = ln.rstrip('\n').split('\t')
    t = text.split(';')[0].split()
    if len(t) < 2:
        continue
    op = t[0].split('.')[0].upper()
    sops = split_ops(' '.join(t[1:]).split(';')[0])
    bl = split_ops(bops)
    if op in ('GET', 'PUT'):
        sops = [s if not re.fullmatch(IDENT, s) or re.fullmatch(r'[ad][0-7]|sp', s, re.I) else s + '(a5)' for s in sops]
    if op in ('CALLTL', 'CALL', 'XJSR', 'XSYS', 'XCALL', 'CALLROM', 'CALLEF'):
        continue
    if len(sops) != len(bl):
        continue
    for s, b in zip(sops, bl):
        m = re.fullmatch(r'(%s)\((a[0-7]|sp|pc)\)' % IDENT, s, re.I)
        mb = re.fullmatch(r'%(\w+)@\((-?\w+)\)', b)
        if m and mb:
            r = REG.get(m.group(2).lower(), m.group(2).lower())
            if r == mb.group(1) and num(mb.group(2)) is not None:
                votes[m.group(1)][num(mb.group(2))] += 1
            continue
        m = re.fullmatch(r'#(%s)' % IDENT, s)
        mb = re.fullmatch(r'#(-?\w+)', b)
        if m and mb and num(mb.group(1)) is not None:
            votes[m.group(1)][num(mb.group(1))] += 1
            continue
        m = re.fullmatch(IDENT, s)
        if m and not re.fullmatch(r'[ad][0-7]|sp|ccr|sr|usp', s, re.I) and num(b) is not None:
            votes[s][num(b)] += 1

with open(sys.argv[2], 'w') as f:
    nconf = 0
    for k in sorted(votes):
        (v, c), *rest = votes[k].most_common()
        if rest: nconf += 1
        f.write('%s\t%d\t%d\t%s\n' % (k, v, c, ' '.join('%d:%d' % x for x in rest)))
print('symbols', len(votes), 'with conflicting votes', nconf)

#!/usr/bin/env python3
"""Align GenAm source modules to the disassembled binary.
python3 align.py <insns.tsv> <out.tsv> src1.a src2.a ...
out.tsv: label  hunk_off  module  srcline   (only labels whose instruction matched inside an anchored run)"""
import sys, os, bisect
from collections import defaultdict
sys.path.insert(0, os.path.dirname(__file__))
import srcparse

K = 6
PAIRS = None

def bnorm(mn):
    if mn in srcparse.OPS68:
        b = mn
    elif mn[:-1] in srcparse.OPS68:
        b = mn[:-1]
    else:
        b = mn
    return srcparse.norm(b)

def lis(pairs):
    # longest chain increasing in both coordinates (pairs sorted by s)
    tails, idx, prev = [], [], [None] * len(pairs)
    for i, (s, b) in enumerate(pairs):
        j = bisect.bisect_left(tails, b)
        if j == len(tails): tails.append(b); idx.append(i)
        else: tails[j] = b; idx[j] = i
        prev[i] = idx[j - 1] if j else None
    out, k = [], idx[-1] if idx else None
    while k is not None:
        out.append(pairs[k]); k = prev[k]
    return out[::-1]

def main():
    B = []
    for ln in open(sys.argv[1]):
        h, off, n, mn, ops = (ln.rstrip('\n').split('\t') + [''])[:5]
        B.append(('H%02d_%s' % (int(h), off), bnorm(mn), ops))
    BT = [t for _, t, _ in B]
    grams = defaultdict(list)
    for i in range(len(BT) - K):
        grams[tuple(BT[i:i + K])].append(i)
    out = open(sys.argv[2], 'w')
    global PAIRS
    PAIRS = open(sys.argv[2] + '.pairs', 'w')
    used = set()
    for src in sys.argv[3:]:
        items = [x for x in srcparse.parse(src) if x[1] not in ('?', 'DATA')]
        ST = [t for _, t, _, _ in items]
        pairs = []
        for i in range(len(ST) - K):
            hits = grams.get(tuple(ST[i:i + K]), ())
            if 0 < len(hits) <= 3:
                for b in hits:
                    pairs.append((i, b))
        pairs.sort()
        chain = lis(pairs)
        # extend anchors into runs of exact token equality
        match = {}
        for s, b in chain:
            i, j = s, b
            while i >= 0 and j >= 0 and ST[i] == BT[j] and i not in match:
                match[i] = j; i -= 1; j -= 1
            i, j = s + 1, b + 1
            while i < len(ST) and j < len(BT) and ST[i] == BT[j] and i not in match:
                match[i] = j; i += 1; j += 1
        # fill gaps between consecutive anchors with a local diff (macros the parser cannot expand leave holes)
        import difflib
        ms = sorted(match.items())
        for (s1, b1), (s2, b2) in zip(ms, ms[1:]):
            if s2 - s1 > 1 and 0 < b2 - b1 < 4 * (s2 - s1) + 64 and b2 > b1:
                sm = difflib.SequenceMatcher(None, ST[s1 + 1:s2], BT[b1 + 1:b2], autojunk=False)
                for a, b, n in sm.get_matching_blocks():
                    if n >= 2:
                        for t in range(n):
                            match.setdefault(s1 + 1 + a + t, b1 + 1 + b + t)
        nl = 0
        if PAIRS:
            for i, j in sorted(match.items()):
                PAIRS.write('%s\t%d\t%s\t%s\t%s\n' % (os.path.basename(src), items[i][2], items[i][3].replace('\t', ' '), B[j][0], B[j][2]))
        for i, (labels, tok, no, text) in enumerate(items):
            if labels and i in match:
                for l in labels:
                    out.write('%s\t%s\t%s\t%d\n' % (l, B[match[i]][0], os.path.basename(src), no)); nl += 1
        span = (B[min(match.values())][0], B[max(match.values())][0]) if match else ('-', '-')
        print('%-22s insns %5d matched %5d (%3d%%) labels %4d span %s..%s' % (os.path.basename(src), len(ST), len(match), 100 * len(match) // max(1, len(ST)), nl, *span))

main()

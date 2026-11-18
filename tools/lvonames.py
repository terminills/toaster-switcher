#!/usr/bin/env python3
"""Name the ToasterBase jump table: align switcher.a's LIBJMP list (source order = most negative LVO first) to the
binary table with a DP that scores agreement with aligned code labels; slots the source lacks stay '?unnamed'.
python3 lvonames.py <switcher.a> <jumptable.tsv> <labels.tsv> <lvo.tsv>"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import srcparse
src, jtp, labp, outp = sys.argv[1:5]
names = [l.split()[1] for _, t, no, l in srcparse.parse(src) if l.split()[0] == 'LIBJMP']
jt = sorted((int(a), b) for a, b in (ln.split('\t') for ln in open(jtp).read().split('\n') if ln))
lab = {}
for ln in open(labp):
    l, a, m, no = ln.rstrip('\n').split('\t'); lab.setdefault(l, set()).add(a)
n, m = len(jt), len(names)
NEG = -10 ** 9
dp = [[NEG] * (m + 1) for _ in range(n + 1)]; bk = [[None] * (m + 1) for _ in range(n + 1)]
dp[0][0] = 0
for i in range(n + 1):
    for j in range(m + 1):
        v = dp[i][j]
        if v == NEG: continue
        if i < n and j < m:
            s = v + (1 if names[j] in lab and jt[i][1] in lab[names[j]] else (-1 if names[j] in lab else 0))
            if s > dp[i + 1][j + 1]: dp[i + 1][j + 1] = s; bk[i + 1][j + 1] = (i, j)
        if i < n and dp[i + 1][j] < v: dp[i + 1][j] = v; bk[i + 1][j] = (i, j)
i, j = n, m; assign = {}
while (i, j) != (0, 0):
    pi, pj = bk[i][j]
    if pj != j: assign[pi] = names[pj]
    i, j = pi, pj
agree = sum(1 for k, nm in assign.items() if nm in lab and jt[k][1] in lab[nm])
dis = [(jt[k], nm, sorted(lab[nm])) for k, nm in assign.items() if nm in lab and jt[k][1] not in lab[nm]]
with open(outp, 'w') as f:
    for k, (lvo, t) in enumerate(jt):
        f.write('%d\t%s\t%s\n' % (lvo, assign.get(k, '?unnamed'), t))
print('table %d names %d unnamed %d agree %d disagree %d' % (n, m, n - len(assign), agree, len(dis)))
for d in dis: print('  DIS', d)

#!/usr/bin/env python3
"""Rollback R2 helper: name the symbols around addresses (from `nm -n -S --defined-only exe`).
usage: snap_sym.py nm.txt addr [addr ...]   (hex addresses)"""
import sys, bisect
syms = []
for ln in open(sys.argv[1], errors='replace'):
    p = ln.split()
    if len(p) == 4:
        syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    elif len(p) == 3:
        syms.append((int(p[0], 16), 0, p[2]))
syms.sort()
keys = [s[0] for s in syms]
for a in sys.argv[2:]:
    v = int(a, 16)
    i = bisect.bisect_right(keys, v) - 1
    out = []
    for j in range(max(0, i - 1), min(len(syms), i + 2)):
        s = syms[j]
        out.append('%s(%08x+%d)' % (s[2], s[0], s[1]))
    print('%s: %s' % (a, '  '.join(out)))

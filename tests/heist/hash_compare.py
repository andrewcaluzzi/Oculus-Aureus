#!/usr/bin/env python3
"""Compares two GE_HASHLOG files (port_coop.c: one line per sim tick, the state hash and its field hashes): prints the tick count of
each, how many leading ticks match, and the first tick where they part (with both lines). Exit 0 = identical over the common ticks
and the same length. usage: hash_compare.py <a> <b>"""
import sys


def ticks(path):
    out = []
    with open(path, errors="replace") as f:
        for line in f:
            if line.startswith("tick "):
                out.append(line.rstrip("\n"))
    return out


a, b = ticks(sys.argv[1]), ticks(sys.argv[2])
n = min(len(a), len(b))
for i in range(n):
    if a[i] != b[i]:
        print("DIFFER at line %d of %d / %d ticks:\n  a: %s\n  b: %s" % (i + 1, len(a), len(b), a[i], b[i]))
        sys.exit(1)
print("%s: %d ticks identical%s" % ("IDENTICAL" if len(a) == len(b) else "PREFIX", n,
                                    "" if len(a) == len(b) else " (lengths %d / %d)" % (len(a), len(b))))
sys.exit(0 if len(a) == len(b) else 2)

#!/usr/bin/env python3
"""throwstats.py <log>...: knife throw distances from 'ge: mod: enemy_knives chr N threw at T dist D' lines (units): count, buckets, median."""
import re, sys, statistics
for f in sys.argv[1:]:
    d = [int(m.group(1)) for l in open(f, errors='replace') for m in [re.search(r'enemy_knives chr \d+ threw at \S+ dist (\d+)', l)] if m]
    if not d:
        print(f, 'no throws'); continue
    b = lambda lo, hi: sum(1 for x in d if lo <= x < hi)
    print('%s: %d throws, min %d median %d max %d; <1500: %d, 1500-2500: %d, 2500-3000: %d, 3000+: %d' % (f, len(d), min(d), statistics.median(d), max(d), b(0, 1500), b(1500, 2500), b(2500, 3000), b(3000, 99999)))

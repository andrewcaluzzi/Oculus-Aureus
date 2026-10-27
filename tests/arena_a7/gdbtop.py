#!/usr/bin/env python3
"""gdbtop.py <samples.txt>: histogram of gdbsample.sh stacks - self (top frame) and inclusive (any frame), by function."""
import re, sys, collections
stacks, cur = [], []
for l in open(sys.argv[1], errors='replace'):
    l = l.rstrip()
    if l.startswith('----'):
        if cur: stacks.append(cur)
        cur = []
    elif l.startswith('#'):
        m = re.match(r'#(\d+)\s+(?:0x[0-9a-f]+ in )?(\w+)', l)
        if m: cur.append(m.group(2))
if cur: stacks.append(cur)
# gdb prints frame 0 twice (thread line + bt): drop the duplicate
stacks = [s[1:] if len(s) > 1 and s[0] == s[1] else s for s in stacks]
n = len(stacks)
selfc = collections.Counter(s[0] for s in stacks if s)
incl = collections.Counter(f for s in stacks for f in set(s))
print('%d samples' % n)
print('self:'); [print('  %5.1f%% %s' % (100.0 * c / n, f)) for f, c in selfc.most_common(12)]
print('inclusive:'); [print('  %5.1f%% %s' % (100.0 * c / n, f)) for f, c in incl.most_common(22)]

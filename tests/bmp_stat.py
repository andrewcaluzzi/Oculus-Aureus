#!/usr/bin/env python3
"""bmp_stat.py <in.bmp> [...]: one line per GE_SHOT 24-bit BMP: "<name> mean <0..255> red <fraction of strongly red pixels> lit <fraction of non-black pixels>".
Mission-end tests: a view that renders a scene is not black (lit), a death overlay is red. No dependencies."""
import struct
import sys


def stat(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    total = w * h
    s = red = lit = 0
    for y in range(0, h, 2):
        row = d[off + y * stride: off + y * stride + w * 3]
        for x in range(0, w * 3, 6):
            b, g, r = row[x], row[x + 1], row[x + 2]
            s += (r + g + b) // 3
            if r > 100 and r > 2 * g and r > 2 * b:
                red += 1
            if r + g + b > 30:
                lit += 1
    n = ((h + 1) // 2) * ((w + 1) // 2)
    return s / n, red / n, lit / n


for p in sys.argv[1:]:
    m, r, l = stat(p)
    print('%s mean %.1f red %.3f lit %.3f' % (p.split('/')[-1], m, r, l))

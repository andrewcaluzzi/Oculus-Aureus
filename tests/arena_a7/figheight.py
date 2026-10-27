#!/usr/bin/env python3
"""figheight.py <shot.bmp> <x0-x1> [<x0-x1> ...]: for each column band, the top and bottom row (from the top of the image) of the
dark pixels (a figure on the sand) and their height in pixels. For arena7-look.lua: three soldiers side by side at different scales."""
import struct
import sys


def main():
    d = open(sys.argv[1], 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    stride = (w * 3 + 3) & ~3
    for band in sys.argv[2:]:
        x0, x1 = (int(v) for v in band.split('-'))
        top, bot = None, None
        for row in range(h):            # row 0 = top of the image (BMP rows are stored bottom-up)
            y = h - 1 - row
            for x in range(x0, x1):
                p = off + y * stride + x * 3
                b, g, r = d[p], d[p + 1], d[p + 2]
                if r + g + b < 200 and h * 0.35 < row < h * 0.75:   # dark: the figure (the HUD text and the stands' crowd are above)
                    if top is None:
                        top = row
                    bot = row
                    break
        print(band, 'top', top, 'bottom', bot, 'height', (bot - top + 1) if top is not None else None)


if __name__ == '__main__':
    main()

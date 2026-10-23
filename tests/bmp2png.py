#!/usr/bin/env python3
"""bmp2png.py <in.bmp> <out.png>: converts a GE_SHOT 24-bit BMP to PNG (no dependencies), so a screenshot can be viewed."""
import struct
import sys
import zlib


def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert bpp == 24, 'only 24-bit BMPs'
    flip = h > 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    raw = bytearray()
    for y in range(h):
        sy = h - 1 - y if flip else y
        row = d[off + sy * stride: off + sy * stride + w * 3]
        raw.append(0)
        for x in range(w):
            b, g, r = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
            raw += bytes((r, g, b))

    def chunk(t, c):
        body = t + c
        return struct.pack('>I', len(c)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)


main()

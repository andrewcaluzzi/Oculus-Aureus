#!/usr/bin/env python3
"""bmp_sheet.py <out.png> <cols> <divisor> <in1.bmp> [in2.bmp ...]: tiles GE_SHOT 24-bit BMPs into one PNG contact sheet, each shrunk
by the integer divisor (nearest sample). No dependencies. Netplay job 7: one image to look at instead of a dozen."""
import struct
import sys
import zlib


def load(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert bpp == 24, 'only 24-bit BMPs'
    flip = h > 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        sy = h - 1 - y if flip else y
        rows.append(d[off + sy * stride: off + sy * stride + w * 3])
    return w, h, rows


def main():
    out, cols, div = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    files = sys.argv[4:]
    imgs = [load(f) for f in files]
    tw = max(i[0] for i in imgs) // div
    th = max(i[1] for i in imgs) // div
    rowsn = (len(imgs) + cols - 1) // cols
    W, H = tw * cols, th * rowsn
    canvas = [bytearray(W * 3) for _ in range(H)]
    for n, (w, h, rows) in enumerate(imgs):
        ox, oy = (n % cols) * tw, (n // cols) * th
        for y in range(0, h // div):
            src = rows[y * div]
            dst = canvas[oy + y]
            for x in range(0, w // div):
                b, g, r = src[x * div * 3], src[x * div * 3 + 1], src[x * div * 3 + 2]   # BMP is BGR
                dst[(ox + x) * 3:(ox + x) * 3 + 3] = bytes(bytearray((r, g, b)))
    raw = b''.join(b'\x00' + bytes(r) for r in canvas)

    def chunk(t, data):
        c = struct.pack('>I', len(data)) + t + data
        return c + struct.pack('>I', zlib.crc32(t + data) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(out, 'wb').write(png)


if __name__ == '__main__':
    main()

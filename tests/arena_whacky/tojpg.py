#!/usr/bin/env python3
"""Convert the BMP screenshots of one run to JPEG and delete the BMPs (disk: BMP batches once filled C:).
usage: tojpg.py <shots dir> <out dir> <prefix>"""
import os, sys
from PIL import Image

src, out, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
names = sorted(f for f in os.listdir(src) if f.lower().endswith('.bmp'))
for i, f in enumerate(names):
    p = os.path.join(src, f)
    im = Image.open(p).convert('RGB')
    if im.width > 1280:
        im = im.resize((im.width // 2, im.height // 2))
    dst = os.path.join(out, '%s_%02d.jpg' % (prefix, i))
    im.save(dst, quality=85)
    os.remove(p)
    print(dst)

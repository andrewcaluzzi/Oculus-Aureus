"""Contact sheet of the crosshair screenshots (tests/crosshair/run.sh api with GE_XHAIR_SHOTS=1).

usage: py -3 sheet.py <shots dir> <api.log> <out.png>
Pairs build_pc/xhair_shots/shot_*.bmp (in order) with the log's "XHAIR SHOT <name>" lines, crops the middle of each frame
(the aim point) and tiles them 4 across with the style's name under each.
"""
import glob
import os
import re
import sys

from PIL import Image, ImageDraw


def main():
    shots_dir, log, out = sys.argv[1], sys.argv[2], sys.argv[3]
    names = []
    with open(log, "rb") as f:
        for line in f.read().decode("latin-1").splitlines():
            m = re.search(r"XHAIR SHOT (\S+)", line)
            if m:
                names.append(m.group(1))
    files = sorted(glob.glob(os.path.join(shots_dir, "shot_*.bmp")))
    n = min(len(names), len(files))
    if n == 0:
        print("no shots")
        return 1
    tiles = []
    for name, path in zip(names[:n], files[:n]):
        im = Image.open(path).convert("RGB")
        w, h = im.size
        cw, ch = w // 5, h // 5
        box = (w // 2 - cw // 2, h // 2 - ch // 2, w // 2 + cw // 2, h // 2 + ch // 2)
        tile = im.crop(box).resize((cw * 2, ch * 2), Image.NEAREST)
        tiles.append((name, tile))
    tw, th = tiles[0][1].size
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tw, rows * (th + 18)), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    for i, (name, tile) in enumerate(tiles):
        x, y = (i % cols) * tw, (i // cols) * (th + 18)
        sheet.paste(tile, (x, y))
        draw.text((x + 6, y + th + 3), name, fill=(230, 230, 230))
    sheet.save(out)
    print("wrote %s (%d tiles)" % (out, len(tiles)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

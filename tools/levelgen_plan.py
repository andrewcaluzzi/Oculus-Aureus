#!/usr/bin/env python3
"""
levelgen_plan.py - a top-down plan of a generated level's layout (tools/levelgen/*.json, modding/levelgen.md): rooms (shaded by floor
height; ramps and stairs hatched), furniture blocks, doorways coloured by the key that opens their door, cameras with their view, alarm
boxes, monitors, the safe, keys, padlocks, named pads, loot pads and the start. Writes an SVG and a PNG (no dependencies: the PNG has
its own 5x7 font).

    python3 tools/levelgen_plan.py tools/levelgen/janus_bank.json out/janus_bank_plan     (-> .svg and .png)
"""
import json
import math
import struct
import sys
import zlib

KEYS = [  # (mask, colour, label)
    (0x40000000, (150, 70, 200), "VAULT (cracker)"),
    (0x10, (220, 170, 20), "GOLD"),
    (0x8, (215, 40, 40), "RED"),
    (0x4, (40, 90, 220), "BLUE"),
    (0x2, (30, 160, 60), "GREEN"),
    (0x1, (140, 90, 40), "floor key"),
]
C_OPEN = (250, 250, 250)
C_DOOR = (120, 120, 120)
C_WALL = (40, 40, 48)
C_BG = (28, 30, 36)
C_BLOCK = (95, 80, 70)
C_CAM = (255, 120, 0)
C_ALARM = (255, 30, 30)
C_MON = (40, 210, 230)
C_PAD = (90, 90, 100)
C_LOOT = (240, 200, 40)
C_TEXT = (20, 20, 25)


def ival(v, d=0):
    if v is None:
        return d
    return int(v, 0) if isinstance(v, str) else int(v)


def key_colour(kf):
    for m, c, _ in KEYS:
        if kf & m:
            return c
    return C_DOOR


def floor_colour(y, ramp):
    if ramp:
        return (205, 200, 175)
    if y < -100:
        return (176, 196, 222)       # vault level
    return (236, 230, 214)           # ground floor


# ------------------------------------------------------------------------------------------------------------------- raster ---

FONT = {  # 5x7, rows top to bottom, '#' set
    "A": ".###.#...##...#######...##...##...#", "B": "####.#...##...#####.#...##...#####.", "C": ".###.#...##....#....#....#...#.###.",
    "D": "####.#...##...##...##...##...#####.", "E": "######....#....####.#....#....#####", "F": "######....#....####.#....#....#....",
    "G": ".###.#...##....#.####...##...#.####", "H": "#...##...##...#######...##...##...#", "I": ".###...#....#....#....#....#...###.",
    "J": "..###...#....#....#....#.#..#..##..", "K": "#...##..#.#.#..##...#.#..#..#.#...#", "L": "#....#....#....#....#....#....#####",
    "M": "#...###.###.#.##...##...##...##...#", "N": "#...###..##.#.##..###...##...##...#", "O": ".###.#...##...##...##...##...#.###.",
    "P": "####.#...##...#####.#....#....#....", "Q": ".###.#...##...##...##.#.##..#..##.#", "R": "####.#...##...#####.#.#..#..#.#...#",
    "S": ".####......#....###.....#....#####.", "T": "#####..#....#....#....#....#....#..", "U": "#...##...##...##...##...##...#.###.",
    "V": "#...##...##...##...##...#.#.#...#..", "W": "#...##...##...##.#.##.#.##.#.#.#.#.", "X": "#...##...#.#.#...#...#.#.#...##...#",
    "Y": "#...##...#.#.#...#....#....#....#..", "Z": "#####....#...#...#...#...#....#####",
    "0": ".###.#...##..###.#.###..##...#.###.", "1": "..#...##....#....#....#....#...###.", "2": ".###.#...#....#..##..#...#....#####",
    "3": "####.....#....#.###.....#....#####.", "4": "...#...##..#.#.#..#.#####...#....#.", "5": "######....####.....#....##...#.###.",
    "6": "..##..#...#....####.#...##...#.###.", "7": "#####....#...#...#...#....#....#...", "8": ".###.#...##...#.###.#...##...#.###.",
    "9": ".###.#...##...#.####....#...#..##..", "_": "..............................#####", "-": "...............###.................",
    ".": "..............................##...", ":": ".......##.........##...............", "/": "....#....#...#...#...#...#....#....",
    "(": "...#...#...#....#....#.....#.....#.", ")": ".#.....#.....#....#....#...#...#...", " ": "." * 35, "+": ".......#....#..#####..#....#.......",
    ",": ".........................##...#....", "'": "..#....#...........................", "!": "..#....#....#....#....#.........#..",
    "?": ".###.#...#....#...#...#.........#..", "%": "##...##..#...#...#...#...#..##...##", "$": "..#...####.#.#..###..#.#.####...#..",
    "&": ".##..#..#.#.#...#...#.#.##..#..##.#", ">": ".#.....#.....#.....#...#...#...#...", "<": "...#...#...#...#.....#.....#.....#.",
    "=": "..........#####.....#####..........", "*": ".......#.#.#.#.###.#.#.#.#.........", "#": ".#.#.#####.#.#.#.#.#####.#.#.......",
}


class Raster:
    def __init__(self, w, h, bg):
        self.w, self.h = w, h
        self.px = bytearray(bytes(bg) * (w * h))

    def put(self, x, y, c, a=1.0):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 3
            if a >= 1.0:
                self.px[i:i + 3] = bytes(c)
            else:
                for k in range(3):
                    self.px[i + k] = int(self.px[i + k] * (1 - a) + c[k] * a)

    def rect(self, x0, y0, x1, y1, c, a=1.0):
        for y in range(max(0, int(min(y0, y1))), min(self.h, int(math.ceil(max(y0, y1))))):
            for x in range(max(0, int(min(x0, x1))), min(self.w, int(math.ceil(max(x0, x1))))):
                self.put(x, y, c, a)

    def line(self, x0, y0, x1, y1, c, w=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for k in range(n + 1):
            t = k / float(n)
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            r = w / 2.0
            self.rect(x - r, y - r, x + r, y + r, c)

    def disc(self, cx, cy, r, c, a=1.0):
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    self.put(x, y, c, a)

    def wedge(self, cx, cy, ang, half, r, c, a, clip=None):
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                if clip and not (clip[0] <= x <= clip[2] and clip[1] <= y <= clip[3]):
                    continue
                dx, dy = x - cx, y - cy
                d = math.hypot(dx, dy)
                if 3 < d <= r:
                    da = (math.atan2(dy, dx) - ang + math.pi) % (2 * math.pi) - math.pi
                    if abs(da) <= half:
                        self.put(x, y, c, a)

    def text(self, x, y, s, c, scale=1):
        for ch in s.upper():
            g = FONT.get(ch, FONT["?"])
            for row in range(7):
                for col in range(5):
                    if g[row * 5 + col] == "#":
                        self.rect(x + col * scale, y + row * scale, x + (col + 1) * scale, y + (row + 1) * scale, c)
            x += 6 * scale
        return x

    def png(self, path):
        raw = b"".join(b"\x00" + bytes(self.px[y * self.w * 3:(y + 1) * self.w * 3]) for y in range(self.h))

        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0)) +
                    chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# ---------------------------------------------------------------------------------------------------------------------- plan ---

def wall_point(r, side, along, off=0):
    x0, z0, x1, z1 = r["rect"]
    return {"x0": (x0 + off, along), "x1": (x1 - off, along), "z0": (along, z0 + off), "z1": (along, z1 - off)}[side]


def main():
    src, out = sys.argv[1], sys.argv[2]
    L = json.load(open(src))
    rooms = {r["name"]: r for r in L["rooms"]}
    pads = {}
    for r in L["rooms"]:
        for p in r.get("pads", []):
            pads[p["name"]] = (p["at"][0], p["at"][1])
    xs = [v for r in L["rooms"] for v in (r["rect"][0], r["rect"][2])]
    zs = [v for r in L["rooms"] for v in (r["rect"][1], r["rect"][3])]
    X0, X1, Z0, Z1 = min(xs) - 150, max(xs) + 150, min(zs) - 150, max(zs) + 150
    S = 0.2
    legend_w = 300
    W, H = int((X1 - X0) * S) + legend_w, int((Z1 - Z0) * S) + 40

    def P(x, z):
        return (x - X0) * S, (Z1 - z) * S + 30

    img = Raster(W, H, C_BG)
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" font-family="sans-serif">' % (W, H),
           '<rect width="100%%" height="100%%" fill="rgb%s"/>' % (C_BG,)]

    def srect(x0, z0, x1, z1, c, extra=""):
        a, b = P(x0, z1)
        c2, d = P(x1, z0)
        svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="rgb%s" %s/>' % (a, b, c2 - a, d - b, tuple(c), extra))

    title = L.get("comment", "")[:0] or src.split("/")[-1]
    img.text(8, 8, "PLAN: " + title + "  (X EAST, Z NORTH, 1 PX = 5 UNITS)", (230, 230, 230), 2)
    svg.append('<text x="8" y="22" fill="#e6e6e6" font-size="16">Plan: %s (x east, z north, 1 px = 5 units)</text>' % title)
    # rooms
    for r in L["rooms"]:
        x0, z0, x1, z1 = r["rect"]
        y = r.get("floor", 0)
        ramp = r.get("ramp")
        col = floor_colour(y, ramp)
        a, b = P(x0, z1)
        c, d = P(x1, z0)
        img.rect(a - 3, b - 3, c + 3, d + 3, C_WALL)
        img.rect(a, b, c, d, col)
        srect(x0 - 15, z0 - 15, x1 + 15, z1 + 15, C_WALL)
        srect(x0, z0, x1, z1, col)
        if r.get("open_top"):
            img.rect(a, b, c, d, (120, 140, 170), 0.35)
            srect(x0, z0, x1, z1, (120, 140, 170), 'fill-opacity="0.35"')
        if ramp:
            n = int(ramp.get("stairs", 0)) or 10
            ax = ramp["axis"]
            for k in range(1, n):
                t = k / float(n)
                if ax == "z":
                    zz = z0 + (z1 - z0) * t
                    p, q = P(x0, zz), P(x1, zz)
                else:
                    xx = x0 + (x1 - x0) * t
                    p, q = P(xx, z0), P(xx, z1)
                img.line(p[0], p[1], q[0], q[1], (150, 140, 110), 1 if ramp.get("stairs") else 1)
                svg.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="rgb(150,140,110)" stroke-width="1"/>' % (p + q))
        for bl in r.get("blocks", []):
            bx0, bz0, bx1, bz1 = bl["rect"]
            p, q = P(bx0, bz1), P(bx1, bz0)
            img.rect(p[0], p[1], q[0], q[1], C_BLOCK)
            srect(bx0, bz0, bx1, bz1, C_BLOCK)
    # doorways
    for dd in L["doors"]:
        A, B = rooms[dd["rooms"][0]], rooms[dd["rooms"][1]]
        ax0, az0, ax1, az1 = A["rect"]
        bx0, bz0, bx1, bz1 = B["rect"]
        w = int(dd.get("width", L["defaults"].get("door_width", 180)))
        if 0 < bx0 - ax1 <= 200 or 0 < ax0 - bx1 <= 200:
            f0, f1 = (ax1, bx0) if bx0 > ax1 else (bx1, ax0)
            lo, hi = max(az0, bz0), min(az1, bz1)
            at = dd.get("at", (lo + hi) // 2)
            box = (f0 - 10, at - w / 2.0, f1 + 10, at + w / 2.0)
        else:
            f0, f1 = (az1, bz0) if bz0 > az1 else (bz1, az0)
            lo, hi = max(ax0, bx0), min(ax1, bx1)
            at = dd.get("at", (lo + hi) // 2)
            box = (at - w / 2.0, f0 - 10, at + w / 2.0, f1 + 10)
        o = dd.get("object")
        col = C_OPEN if not o else key_colour(ival(o.get("keyflags")))
        p, q = P(box[0], box[3]), P(box[2], box[1])
        img.rect(p[0], p[1], q[0], q[1], col)
        srect(box[0], box[1], box[2], box[3], col)
        if o and o.get("double"):
            cx, cz = (box[0] + box[2]) / 2.0, (box[1] + box[3]) / 2.0
            m = P(cx, cz)
            img.disc(m[0], m[1], 2, (255, 255, 255))
        if o and o.get("padlock"):
            cx, cz = (box[0] + box[2]) / 2.0, (box[1] + box[3]) / 2.0
            m = P(cx, cz)
            img.disc(m[0], m[1] - 9, 5, (255, 140, 0))
            svg.append('<circle cx="%.1f" cy="%.1f" r="5" fill="rgb(255,140,0)"/>' % (m[0], m[1] - 9))
    # objects and pads
    for r in L["rooms"]:
        for o in r.get("objects", []):
            t = o["type"]
            if t in ("camera", "alarm", "monitor"):
                x, z = wall_point(r, o["wall"], float(o["along"]), 12)
                m = P(x, z)
                if t == "camera":
                    w = o.get("watch")
                    tx, tz = pads[w] if isinstance(w, str) else (w or ((r["rect"][0] + r["rect"][2]) / 2, (r["rect"][1] + r["rect"][3]) / 2))
                    tp = P(tx, tz)
                    ang = math.atan2(tp[1] - m[1], tp[0] - m[0])
                    rng = math.hypot(tp[0] - m[0], tp[1] - m[1]) + 40
                    ca, cb = P(r["rect"][0], r["rect"][3])
                    cc, cd = P(r["rect"][2], r["rect"][1])
                    img.wedge(m[0], m[1], ang, math.radians(45), rng, C_CAM, 0.22, (ca, cb, cc, cd))
                    img.disc(m[0], m[1], 5, C_CAM)
                    p1 = (m[0] + rng * math.cos(ang - math.radians(45)), m[1] + rng * math.sin(ang - math.radians(45)))
                    p2 = (m[0] + rng * math.cos(ang + math.radians(45)), m[1] + rng * math.sin(ang + math.radians(45)))
                    cid = "clip_%s" % o.get("name", "cam%d" % len(svg))
                    svg.append('<clipPath id="%s"><rect x="%.1f" y="%.1f" width="%.1f" height="%.1f"/></clipPath>' % (cid, ca, cb, cc - ca, cd - cb))
                    svg.append('<path d="M%.1f %.1f L%.1f %.1f A%.1f %.1f 0 0 1 %.1f %.1f Z" fill="rgb%s" fill-opacity="0.22" clip-path="url(#%s)"/>' % (
                        m[0], m[1], p1[0], p1[1], rng, rng, p2[0], p2[1], C_CAM, cid))
                    svg.append('<circle cx="%.1f" cy="%.1f" r="5" fill="rgb%s"><title>%s</title></circle>' % (m[0], m[1], C_CAM, o.get("name", "camera")))
                elif t == "alarm":
                    img.rect(m[0] - 4, m[1] - 4, m[0] + 4, m[1] + 4, C_ALARM)
                    svg.append('<rect x="%.1f" y="%.1f" width="8" height="8" fill="rgb%s"><title>%s</title></rect>' % (m[0] - 4, m[1] - 4, C_ALARM, o.get("name", "alarm")))
                else:
                    img.rect(m[0] - 2, m[1] - 2, m[0] + 2, m[1] + 2, C_MON)
                    svg.append('<rect x="%.1f" y="%.1f" width="4" height="4" fill="rgb%s"/>' % (m[0] - 2, m[1] - 2, C_MON))
            elif t == "safe":
                m = P(o["at"][0], o["at"][1])
                img.rect(m[0] - 6, m[1] - 6, m[0] + 6, m[1] + 6, (220, 170, 20))
                svg.append('<rect x="%.1f" y="%.1f" width="12" height="12" fill="rgb(220,170,20)" stroke="black"><title>safe</title></rect>' % (m[0] - 6, m[1] - 6))
            elif t == "key":
                m = P(o["at"][0], o["at"][1])
                img.disc(m[0], m[1], 4, key_colour(ival(o.get("keyflags"), 1)))
                svg.append('<circle cx="%.1f" cy="%.1f" r="4" fill="rgb%s"><title>key</title></circle>' % (m[0], m[1], key_colour(ival(o.get("keyflags"), 1))))
            elif t == "prop":
                m = P(o["at"][0], o["at"][1])
                img.rect(m[0] - 3, m[1] - 3, m[0] + 3, m[1] + 3, (150, 120, 90))
                svg.append('<rect x="%.1f" y="%.1f" width="6" height="6" fill="rgb(150,120,90)"/>' % (m[0] - 3, m[1] - 3))
        for p in r.get("pads", []):
            m = P(p["at"][0], p["at"][1])
            img.disc(m[0], m[1], 1.6, C_PAD)
            svg.append('<circle cx="%.1f" cy="%.1f" r="1.6" fill="rgb%s"><title>%s</title></circle>' % (m[0], m[1], C_PAD, p["name"]))
        for lt in r.get("loot", []):
            for at in lt["at"]:
                m = P(at[0], at[1])
                img.disc(m[0], m[1], 3, C_LOOT)
                svg.append('<circle cx="%.1f" cy="%.1f" r="3" fill="rgb%s"><title>%s</title></circle>' % (m[0], m[1], C_LOOT, lt["name"]))
    st = L["start"]
    m = P(st["at"][0], st["at"][1])
    img.disc(m[0], m[1], 6, (255, 255, 255))
    img.disc(m[0], m[1], 3, (0, 0, 0))
    svg.append('<circle cx="%.1f" cy="%.1f" r="6" fill="white" stroke="black" stroke-width="3"><title>start</title></circle>' % m)
    # labels last (on top)
    for r in L["rooms"]:
        x0, z0, x1, z1 = r["rect"]
        y = r.get("floor", 0)
        name = r["name"] + ("" if not r.get("ramp") else " (%d TO %d)" % (y, r["ramp"]["to"])) + (" Y%d" % y if y and not r.get("ramp") else "")
        p = P(x0, z1)
        img.text(p[0] + 4, p[1] + 4, name, C_TEXT, 1)
        svg.append('<text x="%.1f" y="%.1f" font-size="11" fill="rgb%s">%s</text>' % (p[0] + 4, p[1] + 13, C_TEXT, name))
    # legend
    lx, ly = W - legend_w + 12, 50
    img.text(lx, ly, "LEGEND", (230, 230, 230), 2)
    svg.append('<text x="%d" y="%d" fill="#e6e6e6" font-size="16">Legend</text>' % (lx, ly + 14))
    ly += 26
    items = [((236, 230, 214), "GROUND FLOOR Y 0"), ((176, 196, 222), "VAULT LEVEL Y -340"), ((205, 200, 175), "STAIRS / RAMP (LINES)"),
             ((120, 140, 170), "OPEN TOP (STREET)"), (C_BLOCK, "BLOCK (COUNTER, PILLAR, SHELF)"), (C_OPEN, "OPEN DOORWAY"),
             (C_DOOR, "DOOR, UNLOCKED")] + [(c, "DOOR: " + l.upper()) for _, c, l in KEYS] + \
            [((255, 140, 0), "PADLOCK"), (C_CAM, "CAMERA + VIEW"), (C_ALARM, "ALARM BOX"), (C_MON, "MONITOR"), ((220, 170, 20), "SAFE"),
             ((150, 120, 90), "PROP"), (C_LOOT, "LOOT PAD"), (C_PAD, "NAMED PAD"), ((255, 255, 255), "START / DOUBLE DOOR DOT")]
    for c, label in items:
        img.rect(lx, ly, lx + 14, ly + 10, c)
        img.text(lx + 22, ly + 2, label, (230, 230, 230), 1)
        svg.append('<rect x="%d" y="%d" width="14" height="10" fill="rgb%s"/><text x="%d" y="%d" fill="#e6e6e6" font-size="11">%s</text>' % (
            lx, ly, tuple(c), lx + 22, ly + 10, label.lower()))
        ly += 16
    svg.append("</svg>")
    with open(out + ".svg", "w", newline="\n") as f:
        f.write("\n".join(svg))
    img.png(out + ".png")
    print("wrote %s.svg and %s.png (%d x %d)" % (out, out, W, H))


if __name__ == "__main__":
    main()

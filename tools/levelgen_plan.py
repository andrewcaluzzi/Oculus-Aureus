#!/usr/bin/env python3
"""
levelgen_plan.py - a top-down plan of a generated level's layout (tools/levelgen/*.json, modding/levelgen.md): rooms (shaded by floor
height; ramps and stairs hatched), furniture blocks, doorways coloured by the key that opens their door, cameras with their view, alarm
boxes, monitors, the safe, keys, padlocks, named pads, loot pads and the start. Writes an SVG and a PNG (no dependencies: the PNG has
its own 5x7 font).

    python3 tools/levelgen_plan.py tools/levelgen/janus_bank.json out/janus_bank_plan     (-> .svg and .png)

Bank v2: a layout on more than one level also gets a sheet per level (<out>_<name>.svg / .png: the rooms whose floor is on that level,
ramps on both of theirs, the other levels' rooms as faint outlines, openings in cyan) and a side elevation (<out>_elevation.png: every
room's section along z, y up, coloured by zone); <out>.png is then all the sheets side by side at half scale. Stair cores
(`stairwells`) are expanded as tools/gen_level.py expands them.
"""
import json
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_level                # noqa: E402  (expand_stairwells)

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
C_OPENING = (60, 220, 240)
ZONE_COLOUR = {"public": (236, 230, 214), "staff": (214, 228, 200), "restricted": (232, 196, 190)}


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
        if a >= 1.0:            # bank v2: a solid fill a row at a time (a sheet of a hundred rooms)
            xa, xb = max(0, int(min(x0, x1))), min(self.w, int(math.ceil(max(x0, x1))))
            if xb <= xa:
                return
            row = bytes(c) * (xb - xa)
            for y in range(max(0, int(min(y0, y1))), min(self.h, int(math.ceil(max(y0, y1))))):
                i = (y * self.w + xa) * 3
                self.px[i:i + len(row)] = row
            return
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

    def blit_half(self, src, ox, oy):
        """Paste src at half size (2x2 box filter) with its top left at ox, oy."""
        for y in range(src.h // 2):
            for x in range(src.w // 2):
                s = [0, 0, 0]
                for dy in (0, 1):
                    i = ((2 * y + dy) * src.w + 2 * x) * 3
                    for k in range(3):
                        s[k] += src.px[i + k] + src.px[i + 3 + k]
                self.put(ox + x, oy + y, tuple(v // 4 for v in s))


# ---------------------------------------------------------------------------------------------------------------------- plan ---

def wall_point(r, side, along, off=0):
    x0, z0, x1, z1 = r["rect"]
    return {"x0": (x0 + off, along), "x1": (x1 - off, along), "z0": (along, z0 + off), "z1": (along, z1 - off)}[side]


def draw_sheet(L, sheet, others, title, S, out):
    """One plan: the rooms of `sheet` in full, those of `others` as faint outlines (the other levels). Writes out.svg and out.png and
    returns the Raster. The extent is the whole layout's, so every sheet of a level set lines up."""
    rooms = {r["name"]: r for r in L["rooms"]}
    mine = set(r["name"] for r in sheet)
    pads = {}
    for r in L["rooms"]:
        for p in r.get("pads", []):
            pads[p["name"]] = (p["at"][0], p["at"][1])
    xs = [v for r in L["rooms"] for v in (r["rect"][0], r["rect"][2])]
    zs = [v for r in L["rooms"] for v in (r["rect"][1], r["rect"][3])]
    X0, X1, Z0, Z1 = min(xs) - 150, max(xs) + 150, min(zs) - 150, max(zs) + 150
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

    unit = 1.0 / S
    img.text(8, 8, "PLAN: " + title + "  (X EAST, Z NORTH, 1 PX = %g UNITS)" % unit, (230, 230, 230), 2)
    svg.append('<text x="8" y="22" fill="#e6e6e6" font-size="16">Plan: %s (x east, z north, 1 px = %g units)</text>' % (title, unit))
    # the other levels' rooms: faint outlines
    for r in others:
        x0, z0, x1, z1 = r["rect"]
        a, b = P(x0, z1)
        c, d = P(x1, z0)
        for (p, q, u, v) in ((a, b, c, b + 1), (a, d - 1, c, d), (a, b, a + 1, d), (c - 1, b, c, d)):
            img.rect(p, q, u, v, (70, 72, 82))
        svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="none" stroke="rgb(70,72,82)"/>' % (a, b, c - a, d - b))
    # rooms
    for r in sheet:
        x0, z0, x1, z1 = r["rect"]
        y = r.get("floor", 0)
        ramp = r.get("ramp")
        col = floor_colour(y, ramp) if ramp or not r.get("zone") else ZONE_COLOUR.get(r["zone"], floor_colour(y, ramp))
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
    # doorways (bank v2: and the openings, in cyan: no door, no floor through)
    for dd in list(L["doors"]) + [dict(o, opening=True) for o in L.get("openings", [])]:
        if dd["rooms"][0] not in mine and dd["rooms"][1] not in mine:
            continue
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
        if dd.get("opening"):
            col = C_OPENING
            box = (box[0] + 6, box[1], box[2] - 6, box[3]) if box[2] - box[0] < box[3] - box[1] else (box[0], box[1] + 6, box[2], box[3] - 6)
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
    for r in sheet:
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
    if st["room"] in mine:
        m = P(st["at"][0], st["at"][1])
        img.disc(m[0], m[1], 6, (255, 255, 255))
        img.disc(m[0], m[1], 3, (0, 0, 0))
        svg.append('<circle cx="%.1f" cy="%.1f" r="6" fill="white" stroke="black" stroke-width="3"><title>start</title></circle>' % m)
    # labels last (on top)
    for r in sheet:
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
    items = [(floor_colour(0, None), "FLOOR (TINT: ZONE IF TAGGED)"), (ZONE_COLOUR["public"], "ZONE PUBLIC"),
             (ZONE_COLOUR["staff"], "ZONE STAFF"), (ZONE_COLOUR["restricted"], "ZONE RESTRICTED"), ((205, 200, 175), "STAIRS / RAMP (LINES)"),
             ((120, 140, 170), "OPEN TOP (STREET)"), (C_BLOCK, "BLOCK (COUNTER, PILLAR, SHELF)"), (C_OPEN, "OPEN DOORWAY"),
             (C_OPENING, "OPENING (BALCONY, WINDOW)"), (C_DOOR, "DOOR, UNLOCKED")] + [(c, "DOOR: " + l.upper()) for _, c, l in KEYS] + \
            [((255, 140, 0), "PADLOCK"), (C_CAM, "CAMERA + VIEW"), (C_ALARM, "ALARM BOX"), (C_MON, "MONITOR"), ((220, 170, 20), "SAFE"),
             ((150, 120, 90), "PROP"), (C_LOOT, "LOOT PAD"), (C_PAD, "NAMED PAD"), ((255, 255, 255), "START / DOUBLE DOOR DOT"),
             ((70, 72, 82), "OTHER LEVELS (OUTLINE)")]
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
    return img


LEVEL_NAMES = {-840: "B2", -420: "B1", 0: "G", 480: "F1", 960: "F2", 1440: "F3"}


def floor_span(r):
    y = r.get("floor", 0)
    to = r["ramp"]["to"] if r.get("ramp") else y
    return min(y, to), max(y, to)


def levels_of(L):
    """The layout's levels: the floors of its level rooms that carry at least two rooms (a stair landing is not a level)."""
    count = {}
    for r in L["rooms"]:
        if not r.get("ramp"):
            count[r.get("floor", 0)] = count.get(r.get("floor", 0), 0) + 1
    return sorted(y for y, n in count.items() if n >= 2)


def elevation(L, out, S=0.2):
    """A side section: z across, y up; every room's floor-to-ceiling outline (ramps sloped), filled by zone, translucent (rooms behind
    one another along x add up)."""
    zs = [v for r in L["rooms"] for v in (r["rect"][1], r["rect"][3])]
    ys = [v for r in L["rooms"] for v in floor_span(r)] + [floor_span(r)[1] + r.get("height", 320) for r in L["rooms"]]
    Z0, Z1, Y0, Y1 = min(zs) - 200, max(zs) + 200, min(ys) - 150, max(ys) + 150
    W, H = int((Z1 - Z0) * S), int((Y1 - Y0) * S) + 40
    img = Raster(W, H, C_BG)

    def P(z, y):
        return (z - Z0) * S, (Y1 - y) * S + 30
    img.text(8, 8, "ELEVATION (LOOKING WEST: Z ACROSS, Y UP, 1 PX = %g UNITS)" % (1.0 / S), (230, 230, 230), 2)
    for y, nm in LEVEL_NAMES.items():
        if y in levels_of(L):
            a, b = P(Z0, y)
            img.rect(0, b, W, b + 1, (60, 62, 70))
            img.text(4, b - 9, "%s %d" % (nm, y), (150, 150, 160), 1)
    for r in sorted(L["rooms"], key=lambda r: -(r["rect"][2] - r["rect"][0])):
        x0, z0, x1, z1 = r["rect"]
        h = r.get("height", 320)
        col = ZONE_COLOUR.get(r.get("zone"), (200, 200, 200))
        y = r.get("floor", 0)
        ramp = r.get("ramp")
        if ramp and ramp["axis"] == "z":
            for k in range(int((z1 - z0) * S) + 1):
                zz = z0 + k / S
                t = (zz - z0) / float(z1 - z0)
                yf = y + (ramp["to"] - y) * t
                a, b = P(zz, yf + h)
                c, d = P(zz, yf)
                img.rect(a, b, a + 1, d, col, 0.35)
        else:
            lo, hi = floor_span(r)
            a, b = P(z0, hi + h)
            c, d = P(z1, lo)
            img.rect(a, b, c, d, col, 0.18 if r.get("open_top") else 0.35)
    for r in L["rooms"]:
        x0, z0, x1, z1 = r["rect"]
        if z1 - z0 >= 900 and not r.get("open_top"):
            lo, hi = floor_span(r)
            a, b = P(z0, lo + r.get("height", 320))
            img.text(a + 3, b + 3, r["name"], C_TEXT, 1)
    img.png(out + ".png")
    print("wrote %s.png (%d x %d)" % (out, W, H))


def main():
    src, out = sys.argv[1], sys.argv[2]
    L = json.load(open(src))
    gen_level.expand_stairwells(L)
    title = src.replace("\\", "/").split("/")[-1]
    lv = levels_of(L)
    if len(lv) <= 1:
        draw_sheet(L, L["rooms"], [], title, 0.2, out)
        return
    sheets = []
    for k, y in enumerate(lv):
        nxt = lv[k + 1] if k + 1 < len(lv) else 10 ** 9
        on = [r for r in L["rooms"] if floor_span(r)[0] < nxt and floor_span(r)[1] >= y]
        rest = [r for r in L["rooms"] if r not in on]
        nm = LEVEL_NAMES.get(y, "Y%d" % y)
        sheets.append(draw_sheet(L, on, rest, "%s  LEVEL %s (Y %d)" % (title, nm, y), 0.12, "%s_%s" % (out, nm.lower())))
    elevation(L, out + "_elevation", 0.12)
    cols = 2
    sw, sh = sheets[0].w // 2, sheets[0].h // 2
    rows = (len(sheets) + cols - 1) // cols
    big = Raster(sw * cols, sh * rows, C_BG)
    for k, s in enumerate(reversed(sheets)):         # the top floor first
        big.blit_half(s, (k % cols) * sw, (k // cols) * sh)
    big.png(out + ".png")
    print("wrote %s.png (%d x %d: every level)" % (out, big.w, big.h))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
gen_level.py - generate a multi-room level from a layout description (designs/bank-heist.md P2, modding/levelgen.md).

Reads a JSON layout (tools/levelgen/*.json: box rooms, doorways between them, materials, lights, named pads; P2.2 setup objects;
P2.3 ramps and stairs, furniture blocks, doorway frames, double doors, open tops) and writes,
into port/<name>/, the same data shape the asset extractor produces from the ROM (and gen_flatland.py / gen_colosseum.py
already produce for one room):

  bg_<name>_all_p.c            one bg room per layout room (point table + display list, 1172 = raw deflate), a portal per
                               doorway (the quad of the opening), the global visibility commands
  Tbg_<name>_all_p_stanZ.c     the walkable floor: triangles per room (grouped by room, the room byte set), the doorway
                               floors, every shared edge linked (rooms join up through the doorways)
  Usetup<name>Z.c              pads (a grid per room, one in each doorway and one in front of it on each side, the named
                               pads of the layout), the waypoint graph (a group per room and per doorway: routes cross rooms
                               only through doorways), Bond's start pad; no objects or guards (yet)
  <name>_layout.lua            the layout for mode scripts: room ids, pads, doorways and portals (LEVEL table)

The textures are the game's own, by number: only the numbers are written, the texels are read from the ROM at run time.
CMake compiles the C into the exe on a cut stage slot (the ROM CRC gate serves stock data only). Regenerate with:

    python3 tools/gen_level.py tools/levelgen/lshape.json

Units: levelscale for the slot is 1.0, so bg, stan and world units are equal (about a centimetre). Heading 0 = +z.
"""

import argparse
import json
import math
import os
import re
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_flatland as gf          # noqa: E402  (deflate_1172, be_words, fmt_words, vtx, gfx)

LINK_BASE = 16                     # stan link = (0x80 + byte offset of the tile) >> 3; a triangle tile is 32 bytes
ST_ALIGN = 4096                    # ST shift step (128 texels): keeps any texture up to 128 texels wide seamless


# ----------------------------------------------------------------- layout ----

def split_span(a, b, maxlen):
    """Breakpoints of [a, b] in equal parts no longer than maxlen (ints; a deterministic function of (a, b))."""
    n = max(1, int(math.ceil((b - a) / float(maxlen))))
    return [a + int(round((b - a) * k / float(n))) for k in range(n + 1)]


def grid(points, maxlen):
    """Sorted unique breakpoints, every gap subdivided to at most maxlen."""
    pts = sorted(set(points))
    out = [pts[0]]
    for a, b in zip(pts, pts[1:]):
        out.extend(split_span(a, b, maxlen)[1:])
    return out


class Room:
    pass


class Door:
    pass


def load_layout(path):
    with open(path) as f:
        L = json.load(f)
    d = L.get("defaults", {})
    mats = L["materials"]
    rooms, byname = [], {}
    for k, r in enumerate(L["rooms"]):
        R = Room()
        R.name = r["name"]
        R.id = k + 1                                   # bg room 0 is unused
        R.x0, R.z0, R.x1, R.z1 = [int(v) for v in r["rect"]]
        if not (R.x0 < R.x1 and R.z0 < R.z1):
            sys.exit("room %s: rect must be [x0, z0, x1, z1] with x0 < x1 and z0 < z1" % R.name)
        R.y = int(r.get("floor", d.get("floor", 0)))
        R.h = int(r.get("height", d.get("height", 320)))
        for part in ("floor", "wall", "ceiling"):
            m = r.get(part + "_mat", d.get(part + "_mat"))
            if m not in mats:
                sys.exit("room %s: unknown %s material %r" % (R.name, part, m))
            setattr(R, part + "_mat", m)
        R.wall_mats = dict(r.get("wall_mats", {}))            # per side: {"x0" | "x1" | "z0" | "z1": material}
        for side, m in R.wall_mats.items():
            if side not in ("x0", "x1", "z0", "z1") or m not in mats:
                sys.exit("room %s: wall_mats %s = %r: a side is x0, x1, z0 or z1 and a known material" % (R.name, side, m))
        light = dict(d.get("light", {}))
        light.update(r.get("light", {}))
        R.light = light
        R.shade = int(r.get("chr_light", d.get("chr_light", 13)))     # stan tile colour 0..15: how lit props/chrs are
        # P2.3: a ramp room's floor slopes along `axis` from `floor` (at the low x0 / z0 end) to `to` (at the x1 / z1 end); `stairs`
        # draws that many steps over the slope (the walkable floor stays the smooth ramp, as on stock stairs)
        R.ramp = r.get("ramp")
        if R.ramp is not None:
            if R.ramp.get("axis") not in ("x", "z"):
                sys.exit("room %s: ramp axis must be x or z" % R.name)
            R.ramp_to = int(R.ramp["to"])
            R.stairs = int(R.ramp.get("stairs", 0))
        else:
            R.ramp_to, R.stairs = R.y, 0
        R.open_top = bool(r.get("open_top", False))    # P2.3: no ceiling (the clear colour above: an open-air street at night)
        R.area = r.get("area")                         # P2.3: a free tag for mode scripts (LEVEL.rooms[name].area)
        R.blocks = []                                  # P2.3: furniture boxes on the floor (a stan hole under each)
        for k, b in enumerate(r.get("blocks", [])):
            B = Room()
            B.name = b.get("name", "%s_block%d" % (R.name, k + 1))
            B.x0, B.z0, B.x1, B.z1 = [int(v) for v in b["rect"]]
            B.h = int(b.get("height", 110))
            B.mat = b.get("mat", R.wall_mat)
            B.top_mat = b.get("top_mat", B.mat)
            for m in (B.mat, B.top_mat):
                if m not in mats:
                    sys.exit("room %s block %s: unknown material %r" % (R.name, B.name, m))
            if not (R.x0 <= B.x0 < B.x1 <= R.x1 and R.z0 <= B.z0 < B.z1 <= R.z1):
                sys.exit("room %s block %s: not inside the room" % (R.name, B.name))
            if R.ramp is not None:
                sys.exit("room %s: blocks on a ramp are not supported" % R.name)
            R.blocks.append(B)
        R.pads_named = list(r.get("pads", []))
        R.objects = r.get("objects", [])               # P2.2: setup objects (cameras, alarms, monitors, keys, safes, props)
        R.loot = r.get("loot", [])                     # P2.2: named loot spawn pad lists
        for lt in R.loot:
            for k, at in enumerate(lt["at"]):
                R.pads_named.append({"name": "%s_%d" % (lt["name"], k + 1), "at": at})
        R.doors = []
        rooms.append(R)
        if R.name in byname:
            sys.exit("duplicate room name %s" % R.name)
        byname[R.name] = R
    for a in rooms:
        for b in rooms:
            if a is not b and a.x0 < b.x1 and b.x0 < a.x1 and a.z0 < b.z1 and b.z0 < a.z1:
                (ay0, ay1), (by0, by1) = yspan(a), yspan(b)
                if ay0 < by1 and by0 < ay1:          # P2.3: rooms on different floors may share x, z
                    sys.exit("rooms %s and %s overlap" % (a.name, b.name))
    wallmax = int(d.get("max_wall", 200))
    doors = []
    for k, dd in enumerate(L.get("doors", [])):
        a, b = byname[dd["rooms"][0]], byname[dd["rooms"][1]]
        D = Door()
        D.index = k
        D.a, D.b = a, b
        D.w = int(dd.get("width", d.get("door_width", 180)))
        D.h = int(dd.get("height", d.get("door_height", 240)))
        # which walls face each other: the gap between them is the wall thickness
        if 0 < b.x0 - a.x1 <= wallmax:
            D.axis, D.fa, D.fb, lo, hi = "x", a.x1, b.x0, max(a.z0, b.z0), min(a.z1, b.z1)
        elif 0 < a.x0 - b.x1 <= wallmax:
            D.axis, D.fa, D.fb, lo, hi = "x", a.x0, b.x1, max(a.z0, b.z0), min(a.z1, b.z1)
        elif 0 < b.z0 - a.z1 <= wallmax:
            D.axis, D.fa, D.fb, lo, hi = "z", a.z1, b.z0, max(a.x0, b.x0), min(a.x1, b.x1)
        elif 0 < a.z0 - b.z1 <= wallmax:
            D.axis, D.fa, D.fb, lo, hi = "z", a.z0, b.z1, max(a.x0, b.x0), min(a.x1, b.x1)
        else:
            sys.exit("door %s-%s: the rooms have no facing walls within %d units" % (a.name, b.name, wallmax))
        at = int(dd.get("at", (lo + hi) // 2))
        D.lo, D.hi = at - D.w // 2, at - D.w // 2 + D.w
        if D.lo < lo + 20 or D.hi > hi - 20:
            sys.exit("door %s-%s: the opening %d..%d is not on the shared wall %d..%d (20 units margin)" % (
                a.name, b.name, D.lo, D.hi, lo, hi))
        # P2.3: the floor on each side, at the doorway (a ramp room's doorways sit on its two end walls, where its floor is level)
        for R in (a, b):
            if R.ramp is not None and R.ramp["axis"] != D.axis:
                sys.exit("door %s-%s: on a side wall of the ramp %s (doorways go on a ramp's end walls)" % (a.name, b.name, R.name))
        fa = floor_at(a, D.fa, at) if D.axis == "x" else floor_at(a, at, D.fa)
        fb = floor_at(b, D.fb, at) if D.axis == "x" else floor_at(b, at, D.fb)
        if fa != fb:
            sys.exit("door %s-%s: the floors differ at the doorway (%d and %d)" % (a.name, b.name, fa, fb))
        D.y = fa
        D.name = dd.get("name", "%s-%s" % (a.name, b.name))
        # P2.3: a doorway frame (an architrave round the opening on both faces; the passage's jambs and soffit in its material)
        fr = dd.get("frame", d.get("door_frame"))
        D.frame = None
        if fr:
            fr = fr if isinstance(fr, dict) else {"mat": fr}
            if fr.get("mat") not in mats:
                sys.exit("door %s: unknown frame material %r" % (D.name, fr.get("mat")))
            D.frame = {"mat": fr["mat"], "w": int(fr.get("width", d.get("frame_width", 14))),
                       "depth": int(fr.get("depth", d.get("frame_depth", 6)))}
        if D.h + (D.frame["w"] if D.frame else 0) > min(a.h, b.h):
            sys.exit("door %s-%s: taller than a room" % (a.name, b.name))
        D.obj = dd.get("object")                       # P2.2: a door in the doorway (None = an open doorway)
        a.doors.append(D)
        b.doors.append(D)
        doors.append(D)
    return L, d, mats, rooms, byname, doors


def floor_at(R, x, z):
    """The floor height of room R at (x, z): level, or a ramp's slope (an int; the same function everywhere, so shared vertices match)."""
    if R.ramp is None:
        return R.y
    lo, hi, c = (R.x0, R.x1, x) if R.ramp["axis"] == "x" else (R.z0, R.z1, z)
    t = min(1.0, max(0.0, (c - lo) / float(hi - lo)))
    return int(round(R.y + (R.ramp_to - R.y) * t))


def yspan(R):
    lo = min(R.y, R.ramp_to)
    hi = max(R.y, R.ramp_to) + R.h
    return lo, hi


def in_block(R, x, z):
    """The block of room R that covers (x, z), or None."""
    for B in R.blocks:
        if B.x0 <= x <= B.x1 and B.z0 <= z <= B.z1:
            return B
    return None


_PROP_NAMES = None


def prop_half_extents(root, model):
    """(half width along the model's x, half depth along its z) in world units of a stock prop model: its file's bounding box
    (assets/obseg/prop/<name>/Model.c, the first BoundingBoxRecord) times its file scale (propFileRecord.inc.c). The model id is the
    PROP_* index (the "PROP_..." name list in src/bondconstants.h). None if the model's files are not found."""
    global _PROP_NAMES
    if _PROP_NAMES is None:
        _PROP_NAMES = []
        with open(os.path.join(root, "src", "bondconstants.h")) as f:
            for line in f:
                m = re.match(r'\s*"PROP_(\w+)",', line)
                if m:
                    _PROP_NAMES.append(m.group(1).lower())
    if not 0 <= model < len(_PROP_NAMES):
        return None
    d = os.path.join(root, "assets", "obseg", "prop", _PROP_NAMES[model])
    try:
        with open(os.path.join(d, "Model.c")) as f:
            src = f.read()
        with open(os.path.join(d, "propFileRecord.inc.c")) as f:
            rec = f.read()
    except OSError:
        return None
    m = re.search(r"BoundingBoxRecord_\w+\s*=\s*\{\s*0x[0-9a-fA-F]+,\s*\{([^}]*)\}", src)
    s = re.search(r"PROPFILERECORD\(\s*\w+\s*,\s*([0-9.eE+-]+)\s*\)", rec)
    if not m or not s:
        return None
    b = [float(v) for v in m.group(1).split(",")]
    k = float(s.group(1))
    return max(abs(b[0]), abs(b[1])) * k, max(abs(b[4]), abs(b[5])) * k


def footprint(root, o):
    """A floor prop's footprint as an x/z box (x0, z0, x1, z1): its model's box turned to face `face` (the model's +z points along the
    facing). A prop may give "size": [width, depth] itself (a model whose files are not found must)."""
    at = o["at"]
    face = o.get("face", [at[0], at[1] + 100])
    if "size" in o:
        hx, hz = float(o["size"][0]) / 2.0, float(o["size"][1]) / 2.0
    else:
        e = prop_half_extents(root, ival(o.get("model")))
        if e is None:
            sys.exit("prop %s: no footprint for model %s (give it \"size\": [width, depth])" % (o.get("name"), o.get("model")))
        hx, hz = e
    fx, fz = face[0] - at[0], face[1] - at[1]
    ln = math.hypot(fx, fz) or 1.0
    fx, fz = fx / ln, fz / ln
    ex = abs(fz) * hx + abs(fx) * hz             # the turned box's half extents along world x and z (the model's x is the
    ez = abs(fx) * hx + abs(fz) * hz             # facing turned a quarter turn)
    return at[0] - ex, at[1] - ez, at[0] + ex, at[1] + ez


def check_door_clearance(d, rooms, doors, root):
    """P2.4: no furniture in a doorway's way. Each doorway keeps clear a box on both sides: the opening widened by `door_clearance`
    (default 60) each way along the wall, and from the wall out to the approach pad plus the clearance (`door_approach` 110 + 60).
    A floor prop (its model's box, turned) or a furniture block that comes into that box on a room's side stops the generator: a desk
    across a jamb, half in front of the opening, is what AC walked into in the bank's hall."""
    clr = float(d.get("door_clearance", 60))
    depth = float(d.get("door_approach", 110)) + clr
    bad = []
    for D in doors:
        for R, face in ((D.a, D.fa), (D.b, D.fb)):
            inward = 1 if ((R.x0 + R.x1) / 2.0 if D.axis == "x" else (R.z0 + R.z1) / 2.0) > face else -1
            a0, a1 = sorted((face, face + inward * depth))
            if D.axis == "x":   # the wall runs along z at x = face
                zone = (a0, D.lo - clr, a1, D.hi + clr)
            else:
                zone = (D.lo - clr, a0, D.hi + clr, a1)
            boxes = [("block " + B.name, (B.x0, B.z0, B.x1, B.z1)) for B in R.blocks]
            boxes += [("prop " + o.get("name", "?"), footprint(root, o)) for o in R.objects if o.get("type") == "prop"]
            for what, (x0, z0, x1, z1) in boxes:
                if x0 < zone[2] and zone[0] < x1 and z0 < zone[3] and zone[1] < z1:
                    bad.append("%s in room %s (%.0f..%.0f x %.0f..%.0f) is within %d units of doorway %s (keep-out %.0f..%.0f x %.0f..%.0f)" % (
                        what, R.name, x0, x1, z0, z1, clr, D.name, zone[0], zone[2], zone[1], zone[3]))
    if bad:
        sys.exit("furniture in a doorway's way (move it, or lower \"door_clearance\" in the defaults):\n  " + "\n  ".join(bad))


def seg_hits_rect(ax, az, bx, bz, x0, z0, x1, z1):
    """True if the segment a-b passes through the rectangle (Liang-Barsky clip)."""
    t0, t1 = 0.0, 1.0
    dx, dz = bx - ax, bz - az
    for p, q in ((-dx, ax - x0), (dx, x1 - ax), (-dz, az - z0), (dz, z1 - az)):
        if p == 0:
            if q < 0:
                return False
        else:
            r = q / float(p)
            if p < 0:
                t0 = max(t0, r)
            else:
                t1 = min(t1, r)
            if t0 > t1:
                return False
    return True


def line_clear(R, p, q, margin=40):
    """No block of room R (grown by `margin`, about a chr's width) lies across the straight walk p -> q."""
    return not any(seg_hits_rect(p[0], p[1], q[0], q[1], B.x0 - margin, B.z0 - margin, B.x1 + margin, B.z1 + margin)
                   for B in R.blocks)


def side_of(R, D):
    """The side of room R the door D is on: 'x0', 'x1', 'z0' or 'z1'."""
    face = D.fa if D.a is R else D.fb
    if D.axis == "x":
        return "x0" if face == R.x0 else "x1"
    return "z0" if face == R.z0 else "z1"


# --------------------------------------------------------------- the mesh ----

class Mesh:
    """Polygons of one bg room, grouped by texture number."""

    def __init__(self):
        self.polys = {}

    def add(self, tex, pts):
        """pts: [(x, y, z, s, t, (r, g, b))] with s, t in ST units (32 per texel)."""
        self.polys.setdefault(tex, []).append(pts)


def lamps_of(R):
    li = R.light
    lh = float(li.get("lamp_height", R.h - 20.0))        # above the floor (street lamps in an open-top room sit lower)
    if "lamps" in li:
        return [(float(p[0]), floor_at(R, p[0], p[1]) + lh, float(p[1])) for p in li["lamps"]]
    step = float(li.get("lamp_spacing", 520))
    nx = max(1, int(round((R.x1 - R.x0) / step)))
    nz = max(1, int(round((R.z1 - R.z0) / step)))
    out = []
    for j in range(nz):
        for i in range(nx):
            x, z = R.x0 + (R.x1 - R.x0) * (i + 0.5) / nx, R.z0 + (R.z1 - R.z0) * (j + 0.5) / nz
            out.append((x, floor_at(R, x, z) + lh, z))
    return out


def shade(R, lamps, x, y, z, face):
    li = R.light
    amb = float(li.get("ambient", 0.42))
    inten = float(li.get("intensity", 0.75))
    rad = float(li.get("radius", 420))
    b = amb
    for (lx, ly, lz) in lamps:
        d2 = (x - lx) ** 2 + (y - ly) ** 2 + (z - lz) ** 2
        b += inten / (1.0 + d2 / (rad * rad))
    b = min(b, float(li.get("max", 1.15))) * face
    tint = li.get("tint", [255, 244, 228])
    return tuple(max(0, min(255, int(round(c * b)))) for c in tint)


FACE_FLOOR, FACE_CEIL, FACE_WALL_X, FACE_WALL_Z = 1.0, 0.72, 0.80, 0.92


def material(mats, name):
    m = mats[name]
    return int(m["tex"]), float(m.get("texels_per_unit", 0.25)) * 32.0


def hpoly(mesh, R, lamps, mats, matname, pts, y, face):
    """Horizontal polygon [(x, z)]: s, t from world x, z."""
    tex, k = material(mats, matname)
    mesh.add(tex, [(x, y, z, x * k, z * k, shade(R, lamps, x, y, z, face)) for (x, z) in pts])


def hpoly3(mesh, R, lamps, mats, matname, pts, face):
    """A polygon [(x, y, z)] that is not level (a ramp's floor or ceiling): s, t from world x, z."""
    tex, k = material(mats, matname)
    mesh.add(tex, [(x, y, z, x * k, z * k, shade(R, lamps, x, y, z, face)) for (x, y, z) in pts])


def vquad(mesh, R, lamps, mats, matname, a, b, ya0, ya1, yb0, yb1, top):
    """Vertical quad along floor segment a -> b (x, z): ya0..ya1 at a, yb0..yb1 at b; s along the wall, t down from `top`."""
    tex, k = material(mats, matname)
    face = FACE_WALL_X if a[0] == b[0] else FACE_WALL_Z
    ax = 1 if a[0] == b[0] else 0            # s is the world coordinate along the wall: the texture runs on across segments
    corners = [(a, ya1, a[ax]), (b, yb1, b[ax]), (b, yb0, b[ax]), (a, ya0, a[ax])]
    mesh.add(tex, [(p[0], y, p[1], u * k, (top - y) * k, shade(R, lamps, p[0], y, p[1], face)) for (p, y, u) in corners])


def vpoly(mesh, R, lamps, mats, matname, a, b, y0, y1, top):
    """Vertical quad along floor segment a -> b (x, z), y0..y1; s along the wall, t down from `top`."""
    vquad(mesh, R, lamps, mats, matname, a, b, y0, y1, y0, y1, top)


def stair_steps(R):
    """The treads of a stair room: [(c0, c1, y)] along its axis (c: x or z), or [] for a level room or a smooth ramp."""
    if not R.stairs:
        return []
    lo, hi = (R.x0, R.x1) if R.ramp["axis"] == "x" else (R.z0, R.z1)
    n = R.stairs
    cs = [lo + int(round((hi - lo) * k / float(n))) for k in range(n + 1)]
    return [(cs[k], cs[k + 1], int(round(R.y + (R.ramp_to - R.y) * (k + 0.5) / n))) for k in range(n)]


def half_step(R):
    return int(math.ceil(abs(R.ramp_to - R.y) / (2.0 * R.stairs))) if R.stairs else 0


def room_grids(rooms, doors, maxcell):
    """Each room's floor grid {id: (xs, zs)}: its corners, every doorway and block edge, every gap split to `cell` or less. A doorway's
    floor meets both rooms' floors edge to edge, so the grid lines across a doorway must be the same in both rooms: a line one room
    has there (from another doorway or block of its own) is added to the other, until they agree. D.span is the doorway's lines."""
    raw = {}
    for R in rooms:
        xs, zs = [R.x0, R.x1], [R.z0, R.z1]
        for D in R.doors:
            if D.axis == "z":
                xs += [D.lo, D.hi]
            else:
                zs += [D.lo, D.hi]
        for B in R.blocks:
            xs += [B.x0, B.x1]
            zs += [B.z0, B.z1]
        raw[R.id] = (xs, zs)
    for _ in range(50):
        grids = {R.id: (grid(raw[R.id][0], maxcell), grid(raw[R.id][1], maxcell)) for R in rooms}
        changed = False
        for D in doors:
            k = 1 if D.axis == "x" else 0                 # an x-axis doorway runs along z
            pts = set(p for R in (D.a, D.b) for p in grids[R.id][k] if D.lo <= p <= D.hi)
            for R in (D.a, D.b):
                missing = pts - set(grids[R.id][k])
                if missing:
                    raw[R.id][k].extend(sorted(missing))
                    changed = True
        if not changed:
            for D in doors:
                k = 1 if D.axis == "x" else 0
                D.span = [p for p in grids[D.a.id][k] if D.lo <= p <= D.hi]
            return grids
    sys.exit("grids: the doorways' grid lines do not settle")


def build_room_mesh(R, mats, xs, zs):
    """The room's geometry on its floor grid (the stan and the pads use the same grid)."""
    mesh = Mesh()
    lamps = lamps_of(R)
    top = R.y + R.h if R.ramp is None else max(R.y, R.ramp_to) + R.h    # the walls' texture origin (t runs down from it)
    steps = stair_steps(R)
    for j in range(len(zs) - 1):
        for i in range(len(xs) - 1):
            q = [(xs[i], zs[j]), (xs[i + 1], zs[j]), (xs[i + 1], zs[j + 1]), (xs[i], zs[j + 1])]
            covered = in_block(R, (xs[i] + xs[i + 1]) / 2.0, (zs[j] + zs[j + 1]) / 2.0)
            if R.ramp is None:
                if not covered:
                    hpoly(mesh, R, lamps, mats, R.floor_mat, q, R.y, FACE_FLOOR)
                if not R.open_top:
                    hpoly(mesh, R, lamps, mats, R.ceiling_mat, q, top, FACE_CEIL)
            else:
                if not steps:
                    hpoly3(mesh, R, lamps, mats, R.floor_mat, [(x, floor_at(R, x, z), z) for (x, z) in q], FACE_FLOOR)
                if not R.open_top:
                    hpoly3(mesh, R, lamps, mats, R.ceiling_mat, [(x, floor_at(R, x, z) + R.h, z) for (x, z) in q], FACE_CEIL)
    if steps:
        # stairs: a level tread per step and a riser between treads, drawn over the smooth ramp the stan and the chrs walk on
        axis = R.ramp["axis"]
        riser = R.ramp.get("riser_mat", R.floor_mat)
        cross = xs if axis == "z" else zs

        def pt(c, u):
            return (u, c) if axis == "z" else (c, u)

        for (c0, c1, y) in steps:
            for u0, u1 in zip(cross, cross[1:]):
                hpoly(mesh, R, lamps, mats, R.floor_mat, [pt(c0, u0), pt(c0, u1), pt(c1, u1), pt(c1, u0)], y, FACE_FLOOR)
        for k in range(1, len(steps)):
            c, ya, yb = steps[k][0], steps[k - 1][2], steps[k][2]
            for u0, u1 in zip(cross, cross[1:]):
                vpoly(mesh, R, lamps, mats, riser, pt(c, u0), pt(c, u1), min(ya, yb), max(ya, yb), top)
        for D in R.doors:          # the half step between a doorway's floor and the first / last tread, inside the opening
            end = side_of(R, D)[1] == "0"
            tread = steps[0][2] if end else steps[-1][2]
            c = (R.x0 if end else R.x1) if axis == "x" else (R.z0 if end else R.z1)
            if tread != D.y:
                span = D.span
                for u0, u1 in zip(span, span[1:]):
                    vpoly(mesh, R, lamps, mats, riser, pt(c, u0), pt(c, u1), min(tread, D.y), max(tread, D.y), top)
    # the four walls, cut at the door openings; every segment of a wall is split at the heights of its doors (no T-junctions)
    sides = {"z0": [((x, R.z0), (x2, R.z0)) for x, x2 in zip(xs, xs[1:])],
             "z1": [((x2, R.z1), (x, R.z1)) for x, x2 in zip(xs, xs[1:])],
             "x0": [((R.x0, z2), (R.x0, z)) for z, z2 in zip(zs, zs[1:])],
             "x1": [((R.x1, z), (R.x1, z2)) for z, z2 in zip(zs, zs[1:])]}
    hs = half_step(R)
    for side, segs in sides.items():
        mat = R.wall_mats.get(side, R.wall_mat)
        if R.ramp is not None and side[0] != R.ramp["axis"]:
            # a side wall of a ramp: sloped along the floor (a stair's runs half a step lower, under its treads), no doorways
            for (a, b) in segs:
                fa, fb = floor_at(R, a[0], a[1]), floor_at(R, b[0], b[1])
                vquad(mesh, R, lamps, mats, mat, a, b, fa - hs, fa + R.h, fb - hs, fb + R.h, top)
            continue
        fl = floor_at(R, segs[0][0][0], segs[0][0][1])
        base = fl
        if steps:
            base = min(fl, steps[0][2] if side[1] == "0" else steps[-1][2])
        sdoors = [D for D in R.doors if side_of(R, D) == side]
        ys = sorted(set([base, fl + R.h] + [D.y + D.h for D in sdoors]))
        for (a, b) in segs:
            along = (a[0] + b[0]) / 2.0 if side[0] == "z" else (a[1] + b[1]) / 2.0
            opening = [D for D in sdoors if D.lo < along < D.hi]
            for y0, y1 in zip(ys, ys[1:]):
                if opening and y1 <= opening[0].y + opening[0].h:
                    continue
                vpoly(mesh, R, lamps, mats, mat, a, b, y0, y1, top)
    # furniture blocks: a top and the sides that are not against a wall (the stan has a hole under each: build_stan)
    for B in R.blocks:
        bx = [x for x in xs if B.x0 <= x <= B.x1]
        bz = [z for z in zs if B.z0 <= z <= B.z1]
        yt = min(R.y + B.h, R.y + R.h)
        if yt < R.y + R.h:
            for j in range(len(bz) - 1):
                for i in range(len(bx) - 1):
                    hpoly(mesh, R, lamps, mats, B.top_mat, [(bx[i], bz[j]), (bx[i + 1], bz[j]), (bx[i + 1], bz[j + 1]),
                                                             (bx[i], bz[j + 1])], yt, FACE_FLOOR)
        faces = []
        if B.z0 > R.z0:
            faces += [((x, B.z0), (x2, B.z0)) for x, x2 in zip(bx, bx[1:])]
        if B.z1 < R.z1:
            faces += [((x2, B.z1), (x, B.z1)) for x, x2 in zip(bx, bx[1:])]
        if B.x0 > R.x0:
            faces += [((B.x0, z2), (B.x0, z)) for z, z2 in zip(bz, bz[1:])]
        if B.x1 < R.x1:
            faces += [((B.x1, z), (B.x1, z2)) for z, z2 in zip(bz, bz[1:])]
        for (a, b) in faces:
            vpoly(mesh, R, lamps, mats, B.mat, a, b, R.y, yt, yt)
    return mesh


def add_passage_mesh(mesh, D, mats, maxcell):
    """The doorway's own faces (they belong to room a): floor, the lintel's underside, the two jambs (in the frame's material when
    the doorway has a frame)."""
    R = D.a
    lamps = lamps_of(R)
    f0, f1 = sorted((D.fa, D.fb))
    span = D.span
    ytop = D.y + D.h
    top = R.y + R.h
    reveal = D.frame["mat"] if D.frame else R.wall_mat
    for s0, s1 in zip(span, span[1:]):
        if D.axis == "x":
            q = [(f0, s0), (f1, s0), (f1, s1), (f0, s1)]
        else:
            q = [(s0, f0), (s1, f0), (s1, f1), (s0, f1)]
        hpoly(mesh, R, lamps, mats, R.floor_mat, q, D.y, FACE_FLOOR)
        hpoly(mesh, R, lamps, mats, reveal, q, ytop, FACE_CEIL)
    for s in (D.lo, D.hi):
        if D.axis == "x":
            a, b = (f0, s), (f1, s)
        else:
            a, b = (s, f0), (s, f1)
        vpoly(mesh, R, lamps, mats, reveal, a, b, D.y, ytop, top)


def add_frame_mesh(mesh, R, D, face, inward, mats):
    """A doorway frame on room R's face of the wall (`face` = its plane, `inward` = +1 / -1 into R): two posts beside the opening
    and a lintel over it, `depth` proud of the wall and `width` wide. Drawn in R's room."""
    lamps = lamps_of(R)
    mat, w, dp = D.frame["mat"], D.frame["w"], D.frame["depth"]
    y0, y1 = D.y, D.y + D.h
    fin = face + inward * dp
    top = R.y + R.h

    def P(along, n):
        return (n, along) if D.axis == "x" else (along, n)

    for a0, a1 in ((D.lo - w, D.lo), (D.hi, D.hi + w)):           # the posts, floor to the lintel's top
        vpoly(mesh, R, lamps, mats, mat, P(a0, fin), P(a1, fin), y0, y1 + w, top)
        for a in (a0, a1):
            vpoly(mesh, R, lamps, mats, mat, P(a, face), P(a, fin), y0, y1 + w, top)
        hpoly(mesh, R, lamps, mats, mat, [P(a0, face), P(a1, face), P(a1, fin), P(a0, fin)], y1 + w, FACE_FLOOR)
    vpoly(mesh, R, lamps, mats, mat, P(D.lo, fin), P(D.hi, fin), y1, y1 + w, top)          # the lintel between them
    hpoly(mesh, R, lamps, mats, mat, [P(D.lo, face), P(D.hi, face), P(D.hi, fin), P(D.lo, fin)], y1, FACE_CEIL)
    hpoly(mesh, R, lamps, mats, mat, [P(D.lo, face), P(D.hi, face), P(D.hi, fin), P(D.lo, fin)], y1 + w, FACE_FLOOR)


# ------------------------------------------------------------- bg emission ---

def tri4_from(tris):
    tris = list(tris) + [(0, 0, 0)] * (4 - len(tris))
    w0, w1 = 0xB1000000, 0
    for k, (a, b, c) in enumerate(tris):
        w0 |= (c & 0xF) << (4 * k)
        w1 |= (a & 0xF) << (8 * k)
        w1 |= (b & 0xF) << (8 * k + 4)
    return gf.gfx(w0, w1)


def room_binaries(mesh):
    """(vertex bytes, display list bytes) of one room, the stock state prefix and one texture command per texture."""
    vbytes = b""
    dl = b""
    texs = sorted(mesh.polys)
    dl += gf.gfx(0xE7000000, 0)
    dl += gf.gfx(0xBA001402, 0x00100000)
    dl += gf.gfx(0xB900031D, 0x0C182078)
    dl += gf.gfx(0xFC26A004, 0x1F1093FF)
    dl += gf.gfx(0xBA001001, 0x00010000)
    dl += gf.gfx(0xBB002801, 0xFFFFFFFF)
    dl += gf.gfx(0xC0080002, texs[0])
    dl += gf.gfx(0xBA001102, 0)
    dl += gf.gfx(0xBA000C02, 0x00002000)
    dl += gf.gfx(0xB6000000, 0x00003000)            # culling off: winding does not matter
    dl += gf.gfx(0xFB000000, 0x000000FF)
    nv = 0
    for ti, tx in enumerate(texs):
        if ti:
            dl += gf.gfx(0xC0080002, tx)
        bverts, btris = [], []

        def flush():
            nonlocal vbytes, dl, nv, bverts, btris
            if not bverts:
                return
            n = len(bverts)
            for v in bverts:
                vbytes += v
            dl += gf.gfx(0x04000000 | ((n - 1) << 20) | (n * 16), 0x0E000000 + nv * 16)
            nv += n
            dl += tri4_from(btris)
            bverts, btris = [], []

        for poly in mesh.polys[tx]:
            nvp = len(poly)
            if len(bverts) + nvp > 16 or len(btris) + nvp - 2 > 4:
                flush()
            base = len(bverts)
            s0 = min(p[3] for p in poly)
            t0 = min(p[4] for p in poly)
            ds = ST_ALIGN * math.floor(s0 / ST_ALIGN)
            dt = ST_ALIGN * math.floor(t0 / ST_ALIGN)
            for (x, y, z, s, t, col) in poly:
                si, tj = int(round(s - ds)), int(round(t - dt))
                if not (-32768 <= si <= 32767 and -32768 <= tj <= 32767):
                    sys.exit("texture coordinates out of range (%d, %d): a polygon is too big for its texture density" % (si, tj))
                bverts.append(gf.vtx(int(x), int(y), int(z), si, tj, tuple(col) + (0xFF,)))
            for k in range(1, nvp - 1):
                btris.append((base, base + k, base + k + 1))
        flush()
    dl += gf.gfx(0xB8000000, 0)
    assert len(vbytes) % 16 == 0 and len(dl) % 8 == 0
    return vbytes, dl


def gen_bg(name, rooms, meshes, doors, describe):
    out = [HEADER_NOTE % ("bg_%s_all_p.c" % name, describe),
           '#include "../../assets/obseg/bg/bg_all_p.h"',
           '#include "bg_%s_all_p.h"' % name, "",
           "struct bg_header header = {0, &room_data_table, &portal_data_table, &global_visibility_commands, 0};", "",
           "struct room_data_table_entry room_data_table[] = {", "    {0},"]
    for R in rooms:
        out.append("    {&point_table_binary_%d, &pri_mapping_binary_%d, 0, 0.0, 0.0, 0.0},   /* %s */" % (R.id, R.id, R.name))
    out += ["    {&point_table_binary_end, &pri_mapping_binary_end, &pri_mapping_binary_end, 0.0, 0.0, 0.0},",
            "    {0}", "};", "",
            "u32 global_visibility_commands[] ={0x10000, 0};", "",
            "struct portal_data_table_entry portal_data_table[] = {"]
    for D in doors:
        out.append("    {&portal_%d, %d, %d, 0},   /* %s */" % (D.index, D.a.id, D.b.id, D.name))
    out += ["    {0}", "};", ""]
    for D in doors:
        y0, y1 = float(D.y), float(D.y + D.h)
        if D.axis == "x":
            f = float(D.fb)
            pts = [(f, y1, D.lo), (f, y1, D.hi), (f, y0, D.hi), (f, y0, D.lo)]
        else:
            f = float(D.fb)
            pts = [(D.lo, y1, f), (D.hi, y1, f), (D.hi, y0, f), (D.lo, y0, f)]
        out.append("struct portal_4_point portal_%d = {4, 0, 0, 0, %s};" % (
            D.index, ", ".join("%.1f, %.1f, %.1f" % p for p in pts)))
    out.append("")
    stats = []
    pris = []
    for R in rooms:
        vb, dl = room_binaries(meshes[R.id])
        stats.append((len(vb) // 16, len(dl) // 8))
        out.append(gf.fmt_words("point_table_binary_%d" % R.id, gf.be_words(gf.deflate_1172(vb))))
        out.append("")
        pris.append(gf.fmt_words("pri_mapping_binary_%d" % R.id, gf.be_words(gf.deflate_1172(dl))))
    out.append("u32 point_table_binary_end[] = {\n    0\n};")
    out.append("")
    for p in pris:            # stock order: every point table, then every display list (bg.c sizes each from the next one's start)
        out.append(p)
        out.append("")
    out.append("u32 pri_mapping_binary_end[] = {\n    0, 0, 0, 0\n};")
    out.append("")
    guard = "_BG_%s_ALL_P_H_" % name.upper()
    hdr = ["#ifndef " + guard, "#define " + guard, "", "#include <ultra64.h>", ""]
    for R in rooms:
        hdr.append("extern u32 point_table_binary_%d[];" % R.id)
    hdr.append("extern u32 point_table_binary_end[];")
    for R in rooms:
        hdr.append("extern u32 pri_mapping_binary_%d[];" % R.id)
    hdr.append("extern u32 pri_mapping_binary_end[];")
    for D in doors:
        hdr.append("extern struct portal_4_point portal_%d;" % D.index)
    hdr += ["#endif", ""]
    return "\n".join(out), "\n".join(hdr), stats


# -------------------------------------------------------------------- stan ---

def cell_tris(x0, z0, x1, z1, y):
    """Two triangles of a cell, each wound with cross((B-A), (C-A)) < 0 in (x, z) (the stan inside test). `y` is a height or a
    function (x, z) -> height (a ramp)."""
    Y = y if callable(y) else (lambda x, z: y)
    out = []
    for (a, b, c) in (((x0, Y(x0, z0), z0), (x1, Y(x1, z1), z1), (x1, Y(x1, z0), z0)),
                      ((x0, Y(x0, z0), z0), (x0, Y(x0, z1), z1), (x1, Y(x1, z1), z1))):
        cr = (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])
        assert cr != 0
        if cr > 0:
            b, c = c, b
        out.append((a, b, c))
    return out


def inside_xz(p, t, margin):
    def side(a, b):
        ln = math.hypot(b[0] - a[0], b[2] - a[2])
        return ((b[0] - a[0]) * (p[1] - a[2]) - (b[2] - a[2]) * (p[0] - a[0])) / ln
    return all(side(t[k], t[(k + 1) % 3]) < -margin for k in range(3))


def build_stan(rooms, grids, doors, maxcell):
    """-> tris [(room id, (a, b, c))] grouped by room (stan.c wants each room's tiles in one run)."""
    tris = []
    for R in rooms:
        xs, zs = grids[R.id]
        yf = R.y if R.ramp is None else (lambda x, z, R=R: floor_at(R, x, z))
        for j in range(len(zs) - 1):
            for i in range(len(xs) - 1):
                if in_block(R, (xs[i] + xs[i + 1]) / 2.0, (zs[j] + zs[j + 1]) / 2.0):
                    continue                  # a hole under a furniture block: nobody walks there
                for t in cell_tris(xs[i], zs[j], xs[i + 1], zs[j + 1], yf):
                    tris.append((R.id, t))
        for D in doors:
            if D.a is not R:
                continue
            f0, f1 = sorted((D.fa, D.fb))
            span = D.span
            for s0, s1 in zip(span, span[1:]):
                cell = (f0, s0, f1, s1) if D.axis == "x" else (s0, f0, s1, f1)
                for t in cell_tris(cell[0], cell[1], cell[2], cell[3], D.y):
                    tris.append((R.id, t))
    return tris


def stan_edges(tris):
    edges = {}
    for n, (_, t) in enumerate(tris):
        for k in range(3):
            edges.setdefault(frozenset((t[k], t[(k + 1) % 3])), []).append(n)
    return edges


def gen_stan(name, tris, rooms, describe):
    edges = stan_edges(tris)
    for key, v in edges.items():
        assert len(v) <= 2, "edge shared by %d tiles" % len(v)

    def link(n, k):
        t = tris[n][1]
        v = edges[frozenset((t[k], t[(k + 1) % 3]))]
        if len(v) == 1:
            return 0
        return LINK_BASE + 4 * (v[0] if v[1] == n else v[1])

    sym = "Tbg_%s_all_p_stanZ" % name
    out = [HEADER_NOTE % (sym + ".c", describe),
           "#include <ultra64.h>", "#include <game/stan.h>", "",
           "// forward declarations", "StandTile tile_0;", "",
           "StandFileHeader %s = {" % sym, "    NULL,", "    &tile_0,",
           "    {0x00, 0x00, 0x00, 0x00}", "};", ""]
    byid = {R.id: R for R in rooms}
    for n, (room, t) in enumerate(tris):
        c = byid[room].shade
        out.append("StandTile tile_%d = {" % n)
        out.append("    0x%06x, 0x%02x," % (((n + 1) << 8) | 1, room))
        out.append("    0x0,")
        out.append("    0x%x, 0x%x, 0x%x," % (c, c, c))
        out.append("    3,")
        out.append("    0x0, 0x1, 0x2,")
        out.append("    {")
        out.append(",\n".join("        {%d, %d, %d, 0x%04x}" % (t[k][0], t[k][1], t[k][2], link(n, k)) for k in range(3)))
        out.append("    }")
        out.append("};")
        out.append("")
    out.append("StandTile tile_%d = {" % len(tris))
    out.append("    0x000000, 0x00,\n    0x0,\n    0x0, 0x0, 0x0,\n    0,\n    0x0, 0x0, 0x0\n};")
    out.append("")
    out.append("")
    out.append('StandFileFooter footer = {\n    "unstric",\n    NULL,\n    NULL,\n    NULL,\n    NULL\n};')
    out.append("")
    nlinks = sum(1 for v in edges.values() if len(v) == 2)
    cross = sum(1 for v in edges.values() if len(v) == 2 and tris[v[0]][0] != tris[v[1]][0])
    return "\n".join(out), nlinks, cross


# -------------------------------------------------------------- pads, setup --

class Pad:
    pass


def tile_at(tris, room, x, z):
    """Index of the room's triangle that holds (x, z) well inside (nudged off an edge if it sits on one)."""
    for (dx, dz) in ((0, 0), (9, -9), (-9, 9), (12, 0), (0, 12), (-12, 0), (0, -12), (9, 9), (-9, -9)):
        for n, (r, t) in enumerate(tris):
            if r == room and inside_xz((x + dx, z + dz), t, 4.0):
                return n, x + dx, z + dz
    return None, x, z


def build_pads(L, d, rooms, byname, grids, doors, tris, maxcell):
    pads = []

    def add(room, x, z, kind, name=None, look=(0.0, 1.0), door=None):
        n, px, pz = tile_at(tris, room.id, x, z)
        if n is None:
            sys.exit("pad %s at (%d, %d): no floor in room %s" % (name or kind, x, z, room.name))
        P = Pad()
        P.id = len(pads)
        P.room, P.x, P.z, P.y, P.tile, P.kind, P.name, P.door = room, px, pz, floor_at(room, px, pz), n, kind, name, door
        ln = math.hypot(look[0], look[1]) or 1.0
        P.look = (look[0] / ln, look[1] / ln)
        pads.append(P)
        return P

    minc = int(d.get("pad_min_cell", 150))
    for R in rooms:
        xs, zs = grids[R.id]
        R.grid_pads = []
        for j in range(len(zs) - 1):
            for i in range(len(xs) - 1):
                w, h = xs[i + 1] - xs[i], zs[j + 1] - zs[j]
                if w >= minc and h >= minc and not in_block(R, (xs[i] + xs[i + 1]) / 2.0, (zs[j] + zs[j + 1]) / 2.0):
                    R.grid_pads.append(add(R, (xs[i] + xs[i + 1]) // 2, (zs[j] + zs[j + 1]) // 2, "grid"))
    appr = int(d.get("door_approach", 110))
    for D in doors:
        mid = (D.lo + D.hi) // 2
        f0, f1 = sorted((D.fa, D.fb))
        sgn_a = -1 if D.fa < D.fb else 1          # room a lies on this side of its face
        if D.axis == "x":
            pa = (D.fa + sgn_a * appr, mid)
            pb = (D.fb - sgn_a * appr, mid)
            pm = ((f0 + f1) // 2, mid)
        else:
            pa = (mid, D.fa + sgn_a * appr)
            pb = (mid, D.fb - sgn_a * appr)
            pm = (mid, (f0 + f1) // 2)
        D.pad_a = add(D.a, pa[0], pa[1], "approach", door=D)
        D.pad_b = add(D.b, pb[0], pb[1], "approach", door=D)
        D.pad = add(D.a, pm[0], pm[1], "door", door=D)
    for R in rooms:
        for p in R.pads_named:
            look = p.get("face")
            lk = (look[0] - p["at"][0], look[1] - p["at"][1]) if look else (0.0, 1.0)
            add(R, int(p["at"][0]), int(p["at"][1]), "named", name=p["name"], look=lk)
    st = L["start"]
    R = byname[st["room"]]
    at = st.get("at", [(R.x0 + R.x1) // 2, (R.z0 + R.z1) // 2])
    look = st.get("face")
    lk = (look[0] - at[0], look[1] - at[1]) if look else (0.0, 1.0)
    start = add(R, int(at[0]), int(at[1]), "start", name="start", look=lk)
    return pads, start


def build_waypoints(pads, rooms, doors, maxcell):
    """Links: pads of one room within reach of each other (a box room is convex: every such line is clear), and each doorway pad
    to the two pads in front of it. Groups: one per room, one per doorway, so a route crosses rooms only through a doorway."""
    reach = 2.3 * maxcell
    adj = {P.id: set() for P in pads}
    for R in rooms:
        mine = [P for P in pads if P.room is R and P.kind != "door"]
        for i, p in enumerate(mine):
            for q in mine[i + 1:]:
                if math.hypot(p.x - q.x, p.z - q.z) <= reach and line_clear(R, (p.x, p.z), (q.x, q.z)):
                    adj[p.id].add(q.id)
                    adj[q.id].add(p.id)
        for p in mine:              # nothing may be left on its own
            if not adj[p.id] and len(mine) > 1:
                cands = [q for q in mine if q is not p and line_clear(R, (p.x, p.z), (q.x, q.z))]
                if not cands:
                    sys.exit("waypoints: pad %d (%s) in room %s has no clear line to any other pad (blocks)" % (
                        p.id, p.name or p.kind, R.name))
                q = min(cands, key=lambda q: math.hypot(p.x - q.x, p.z - q.z))
                adj[p.id].add(q.id)
                adj[q.id].add(p.id)
    for D in doors:
        for q in (D.pad_a, D.pad_b):
            adj[D.pad.id].add(q.id)
            adj[q.id].add(D.pad.id)
    group = {}
    ngroups = 0
    for R in rooms:
        R.group = ngroups
        ngroups += 1
    for D in doors:
        D.group = ngroups
        ngroups += 1
    for P in pads:
        group[P.id] = P.door.group if P.kind == "door" else P.room.group
    gadj = {g: set() for g in range(ngroups)}
    for u, vs in adj.items():
        for v in vs:
            if group[u] != group[v]:
                gadj[group[u]].add(group[v])
    members = {g: [P.id for P in pads if group[P.id] == g] for g in range(ngroups)}
    return adj, group, gadj, members


# ----------------------------------------------------------- setup objects (P2.2) ---
#
# Records follow the stock compiled setups word for word (assets/obseg/setup/Usetup*Z.c: the same _mkword layout, the same tails), so
# the stock setup code (prop.c setupDoor / domakedefaultobj / setupCctv / setupSingleMonitor) builds them as it builds any stage's.
# Wall-mounted objects (cameras, alarm boxes, monitors) sit on bound pads the stock way: up = the wall's inward normal, look = +y, a
# zero-depth box offset back onto the wall (PROPFLAG_ONSCREEN puts the model's back on it). Doors sit on a bound pad in the doorway
# whose box is the opening; PROPFLAG_CULL_BEHIND_DOOR makes setupDoor take the portal between the rooms on its two sides
# (bgGetPortalBetweenRooms) and close it while the door is shut. Floor objects (keys, props, padlocks) use plain pads.

MODEL = {   # stock object models (PROP_* in bondconstants.h; the setup's `obj` field)
    "alarm": 1,            # PROP_ALARM2: the wall alarm box (Bunker 1's)
    "camera": 24,          # PROP_CCTV
    "padlock": 63,         # PROP_PADLOCK (Dam's)
    "monitor": 77,         # PROP_TVSCREEN: a wall screen (Bunker 2's banking-hall wall)
    "keycard": 86,         # PROP_SWIPE_CARD2 (Bunker 2's carried keys)
    "key": 244,            # PROP_CHRKEYYALE (Bunker 2's floor key)
    "door": 138,           # PROP_SEV_DOOR4_WIND: Bunker 2's office doors (grey, a window)
    "shutter": 140,        # PROP_SEV_DOOR_V1: Bunker 2's security-wing shutters (vertical)
    "cell": 182,           # PROP_DOORPRISON1: Bunker 2's deposit-cell doors (bars)
    "vault": 152,          # PROP_DOOR_DEST1: a grey steel door with caution stripes (Frigate's)
    "safe": 254,           # PROP_SAFE (the body)
    "safedoor": 293,       # PROP_SAFEDOOR (Bunker 2's manager's safe)
}
PROPDEF = {"door": 1, "prop": 3, "key": 4, "alarm": 5, "cctv": 6, "monitor": 10, "lockdoor": 38, "safe": 43}
DOOR_TYPE = {"slide": 0, "shutter": 4, "swing": 5}
F_ONSCREEN, F_INAIR, F_SCALE_XY, F_COLL, F_CULL = 0x2, 0x8, 0x60, 0x100, 0x10000000


def ival(v, default=0):
    """An int from JSON: a number or a string such as "0x40000000"."""
    if v is None:
        return default
    return int(v, 0) if isinstance(v, str) else int(v)


def obj_head(ptype, model, pad, flags, flags2=0, health=1000, extrascale=256):
    """The 32-word ObjectRecord every object record starts with."""
    return ("_mkword(%d, _mkshort(0, %d)), _mkword(%d, %d), 0x%08x, 0x%08x, 0x00000000, 0x00000000, " % (
        extrascale, ptype, model, pad, flags & 0xFFFFFFFF, flags2 & 0xFFFFFFFF) + "0, " * 19 +
        "0x00000000, 0x00000000, 0, 0, _mkword(%d, 0), 0, 0" % health)


DOOR_RUNTIME = ("0.0f, 0, 0, 0, 0.0f, _mkword(0, 0), 0, _mkword(0, _mkshort(0, 0)), 0x00000000, _mkword(_mkshort(0, 0), 0), " +
                ", ".join(["0"] * 12))


def fx16(v):
    return int(round(v * 65536)) & 0xFFFFFFFF


def angle16(deg):
    return int(round(deg / 360.0 * 65536)) & 0xFFFFFFFF


class Objects:
    """Bound pads, extra (non-waypoint) plain pads and the propDefs records of a level."""

    def __init__(self, first_pad):
        self.bpads = []          # (x, y, z, up, look, tile, bbox(xmin, xmax, ymin, ymax, zmin, zmax), note)
        self.opads = []          # Pad objects, ids from first_pad on (they are not waypoints)
        self.next_pad = first_pad
        self.recs = []           # (comment, text)
        self.lua = {"objects": [], "cameras": [], "alarms": [], "monitors": [], "keys": [], "safes": [], "padlocks": []}

    def rec(self, comment, text):
        self.recs.append((comment, text))
        return len(self.recs) - 1

    def tag(self, what):
        """A Tag record on the next record. A tagged object is a use target (propobj.c: the interact scan takes an object only when
        it is tagged or flagged 'interaction only'); Bunker 2 tags each of its monitors this way."""
        self.next_tag = getattr(self, "next_tag", 0) + 1
        self.rec("Tag %d on %s" % (self.next_tag, what), "_mkword(0, _mkshort(0, 22)), _mkword(%d, 0x0001), 0, 0" % self.next_tag)
        return self.next_tag

    def bound(self, tris, room, x, y, z, up, look, bbox, note):
        n, px, pz = tile_at(tris, room.id, x, z)
        if n is None:
            sys.exit("%s: no floor under its bound pad at (%d, %d) in room %s" % (note, x, z, room.name))
        # P2.3: the pad keeps its exact position (a wall mount or a door leaf on a tile edge was moved 9 units by the nudge that
        # finds its tile); the nudged point only picks the tile it names
        self.bpads.append((x, y, z, up, look, n, bbox, note))
        return len(self.bpads) - 1

    def plain(self, tris, room, x, y, z, look, note):
        n, px, pz = tile_at(tris, room.id, x, z)
        if n is None:
            sys.exit("%s: no floor under its pad at (%d, %d) in room %s" % (note, x, z, room.name))
        P = Pad()
        P.id = self.next_pad
        self.next_pad += 1
        ln = math.hypot(look[0], look[1]) or 1.0
        P.room, P.x, P.z, P.y, P.tile, P.kind, P.name, P.door = room, px, pz, y, n, "object", note, None
        P.look = (look[0] / ln, look[1] / ln)
        self.opads.append(P)
        return P


def wall_point(R, side, along, off):
    """(x, z) of a point `off` units in front of wall `side` of room R at `along`, and the wall's inward normal."""
    if side == "x0":
        return (R.x0 + off, along), (1.0, 0.0, 0.0)
    if side == "x1":
        return (R.x1 - off, along), (-1.0, 0.0, 0.0)
    if side == "z0":
        return (along, R.z0 + off), (0.0, 0.0, 1.0)
    if side == "z1":
        return (along, R.z1 - off), (0.0, 0.0, -1.0)
    sys.exit("room %s: wall must be x0, x1, z0 or z1, not %r" % (R.name, side))


def build_objects(rooms, byname, doors, pads, tris, objs_first_pad):
    O = Objects(objs_first_pad)
    named = {P.name: P for P in pads if P.name}
    padlocks = []

    def entry(name, kind, idx, room, **kw):
        e = {"name": name, "type": kind, "index": idx, "room": room.name}
        e.update(kw)
        O.lua["objects"].append(e)
        return e

    # doors in doorways
    for D in doors:
        o = D.obj
        if not o:
            continue
        kind = o.get("type", "slide")
        if kind not in DOOR_TYPE or kind == "swing":
            sys.exit("door %s: type must be slide or shutter" % D.name)
        m = o.get("model", "shutter" if kind == "shutter" else "door")
        model = MODEL[m] if isinstance(m, str) and m in MODEL else ival(m)
        t = float(o.get("thickness", 10))
        # the door's plane: just inside the passage at room b's face, so the two test points 50 units either side of its centre
        # (prop.c sub_GAME_7F00324C) land on room a's and room b's tiles and setupDoor finds the doorway's portal
        sgn = 1.0 if D.fb > D.fa else -1.0             # room b lies on this side
        plane = D.fb - sgn * (t / 2.0 + 1.0)
        mid = (D.lo + D.hi) / 2.0
        if D.axis == "x":
            x, z, up = plane, mid, (0.0, 0.0, 1.0)
        else:
            x, z, up = mid, plane, (1.0, 0.0, 0.0)
        w, h = float(D.w), float(D.h)
        keyflags = ival(o.get("keyflags"), 0)
        autoclose = ival(o.get("autoclose"), 300 if kind == "slide" else 1500)
        # see_through: a door with a window (the stock windowed office doors): the room behind stays drawn (PROPFLAG_NO_PORTAL_CLOSE)
        flags = (0x40000000 if o.get("see_through") else F_CULL) | (0x80000000 if o.get("open") else 0)
        perim = fx16(0.9)
        if o.get("double"):
            # P2.3: a double door the stock way (Bunker 2's pairs 46/47 ...): two leaves, each on a bound pad at its jamb with the box
            # running to the middle (up = toward the middle), linked to each other (linkedDoorOffset +1 / -1: one opens both)
            if kind != "slide":
                sys.exit("door %s: a double door slides" % D.name)
            hw = w / 2.0 + 1.0
            axis_up = (0.0, 0.0, 1.0) if D.axis == "x" else (1.0, 0.0, 0.0)
            leaves = []
            for k, (s, sg) in enumerate(((D.lo, 1.0), (D.hi, -1.0))):
                lx, lz = (plane, s) if D.axis == "x" else (s, plane)
                lup = tuple(sg * c for c in axis_up)
                leaves.append((lx, lz, lup, (-t / 2.0, t / 2.0, 0.0, hw, 0.0, h), 1 if k == 0 else -1, ival(o.get("sound"), 3) if k == 0 else 0))
        else:
            bb = (-t / 2.0, t / 2.0, -w / 2.0, w / 2.0, 0.0, h)       # x: thickness (normal), y: width (up), z: height (look)
            leaves = [(x, z, up, bb, 0, ival(o.get("sound"), 3))]
        idxs = []
        for k, (lx, lz, lup, bb, link, sound) in enumerate(leaves):
            bp = O.bound(tris, D.a, int(round(lx)), D.y, int(round(lz)), lup, (0.0, 1.0, 0.0), bb,
                         "door %s%s" % (D.name, (" leaf %d" % (k + 1)) if len(leaves) > 1 else ""))
            tail = "0x%08x, 0x%08x, 0x%08x, 0x00000051, 0x00000051, 0x00000666, _mkword(4, %d), 0x%08x, %d, %d, %s" % (
                link & 0xFFFFFFFF, fx16(0.95), perim, DOOR_TYPE[kind], keyflags & 0xFFFFFFFF, autoclose, sound, DOOR_RUNTIME)
            idxs.append(O.rec("Door %s%s (%s, model %d, keyflags 0x%x)" % (D.name, (" leaf %d" % (k + 1)) if len(leaves) > 1 else "",
                                                                          kind, model, keyflags),
                              obj_head(PROPDEF["door"], model, bp, flags) + ", " + tail))
        idx = idxs[0]
        D.obj_index = idx
        D.obj_index2 = idxs[1] if len(idxs) > 1 else None
        D.keyflags = keyflags
        name = o.get("name", D.name)
        if D.obj_index2 is not None:
            entry(name, "door", idx, D.a, keyflags=keyflags, doorway=D.name, pair=D.obj_index2)
        else:
            entry(name, "door", idx, D.a, keyflags=keyflags, doorway=D.name)
        if o.get("padlock"):
            # the padlock hangs on room a's side of the door, 120 up; shooting it frees the door (stock doorIsPadlockFree)
            back = -sgn * (t / 2.0 + 14.0)
            px, pz = (plane + back, mid + w * 0.3) if D.axis == "x" else (mid + w * 0.3, plane + back)
            P = O.plain(tris, D.a, int(round(px)), D.y + 110, int(round(pz)), (-sgn, 0.0) if D.axis == "x" else (0.0, -sgn),
                        "padlock " + D.name)
            padlocks.append((D, P, name))

    def bpos(bp):
        """x, y, z of a bound pad (P2.3: LEVEL.objects carry their position, e.g. the screens a use press must be near)"""
        b = O.bpads[bp]
        return {"x": b[0], "y": b[1], "z": b[2]}

    def mount(R, o, w, h, off, what):
        side = o["wall"]
        (x, z), n = wall_point(R, side, float(o["along"]), off)
        y = floor_at(R, x, z) + int(o.get("y", R.h - 40))
        bb = (-w / 2.0, w / 2.0, -off, -off, -h / 2.0, h / 2.0)
        return O.bound(tris, R, int(round(x)), y, int(round(z)), n, (0.0, 1.0, 0.0), bb, "%s in room %s" % (what, R.name))

    for R in rooms:
        for k, o in enumerate(R.objects):
            kind = o["type"]
            name = o.get("name", "%s_%s%d" % (R.name, kind, k + 1))
            if kind == "camera":
                bp = mount(R, o, 18, 18, 10, "camera " + name)
                watch = o.get("watch")
                if isinstance(watch, str):
                    if watch not in named:
                        sys.exit("camera %s: no pad named %s" % (name, watch))
                    look = named[watch].id
                else:
                    wx, wz = watch if watch else ((R.x0 + R.x1) // 2, (R.z0 + R.z1) // 2)
                    look = O.plain(tris, R, int(wx), floor_at(R, wx, wz), int(wz), (0.0, 1.0), "camera %s look" % name).id
                sweep = o.get("sweep", [30, -30])
                rng = ival(o.get("range"), -100)            # <= 0: no range limit (stock)
                tail = "0x%08x, %s, 0x%08x, 0x%08x, 0x00000000, 0x00000000, 0x%08x, 0x00000000, 0x00000000, 0x%08x" % (
                    look, ", ".join(["0x00000000"] * 18), angle16(sweep[0]), angle16(sweep[1]), ival(o.get("speed"), 0x88),
                    rng & 0xFFFFFFFF)
                idx = O.rec("Cctv %s (watches pad %d)" % (name, look), obj_head(PROPDEF["cctv"], MODEL["camera"], 10000 + bp, F_ONSCREEN) +
                            ", " + tail)
                O.lua["cameras"].append(idx)
                entry(name, "camera", idx, R, lookpad=look, **bpos(bp))
            elif kind == "alarm":
                bp = mount(R, o, 18, 18, 3, "alarm " + name)
                O.tag("alarm " + name)              # a use target: Bond can switch the alarm at the box
                idx = O.rec("Alarm %s" % name, obj_head(PROPDEF["alarm"], MODEL["alarm"], 10000 + bp, F_ONSCREEN))
                O.lua["alarms"].append(idx)
                entry(name, "alarm", idx, R, pad=10000 + bp, **bpos(bp))        # a raiser's alarm_pad (spawn.chr) is this bound pad
            elif kind == "monitor":
                w, h = float(o.get("width", 70)), float(o.get("height", 52))
                bp = mount(R, o, w, h, 4, "monitor " + name)
                # the image (monitorSetImageByNum, propobj.c): Bunker 2's records say 51 (monAnim33BlackSolid) and its AI lists switch the
                # screens to their pictures (tv_change_screen_bank); a generated level has no AI lists, so 51 stayed black (P2.2 / P2.3).
                # The default is 16 (monAnim10GlobalMap, one of Bunker 2's); "image" picks another (0-51; 20 = off, 51 = black).
                tail = ", ".join(["0"] * 24) + ", _mkshort(0, 0), _mkshort(0, 0), _mkshort(0, 0), 0, 0, 0xffffffff, 0xffffffff, %d" % (
                    ival(o.get("image"), 16))
                O.tag("monitor " + name)            # a use target: the `use` event names the monitor
                idx = O.rec("SingleMonitor %s" % name, obj_head(PROPDEF["monitor"], ival(o.get("model"), MODEL["monitor"]), 10000 + bp,
                                                                  F_ONSCREEN | F_SCALE_XY) + ", " + tail)
                O.lua["monitors"].append(idx)
                entry(name, "monitor", idx, R, pad=10000 + bp, **bpos(bp))
            elif kind in ("key", "prop"):
                at = o["at"]
                face = o.get("face", [at[0], at[1] + 100])
                P = O.plain(tris, R, int(at[0]), floor_at(R, at[0], at[1]), int(at[1]), (face[0] - at[0], face[1] - at[1]),
                            "%s %s" % (kind, name))
                if kind == "key":
                    m = o.get("model", "keycard")
                    model = MODEL[m] if isinstance(m, str) and m in MODEL else ival(m)
                    kf = ival(o.get("keyflags"), 1)
                    idx = O.rec("Key %s (0x%x)" % (name, kf), obj_head(PROPDEF["key"], model, P.id, 0) + ", %d" % kf)
                    O.lua["keys"].append(idx)
                    entry(name, "key", idx, R, pad=P.id, keyflags=kf, x=P.x, y=P.y, z=P.z)
                else:
                    idx = O.rec("StandardProp %s" % name, obj_head(PROPDEF["prop"], ival(o["model"]), P.id, ival(o.get("flags"), 0)))
                    entry(name, "prop", idx, R, pad=P.id, x=P.x, y=P.y, z=P.z)
            elif kind == "safe":
                # Bunker 2's manager's safe (bound pads 73 / 74 of UsetupsevbZ.c), scaled: the body on the floor facing `face`, the
                # swinging door on its front. The door's keyflags lock it; the mode opens it with a card (door.try) or door.open.
                s = float(o.get("scale", 1.3))
                at = o["at"]
                fc = o.get("face", [at[0], at[1] - 100])
                fx, fz = fc[0] - at[0], fc[1] - at[1]
                ln = math.hypot(fx, fz) or 1.0
                f = (fx / ln, 0.0, fz / ln)
                nb = (f[2], 0.0, -f[0])                         # (0,1,0) x f: the body's normal
                bb = (-42 * s, 41 * s, 0.0, 83 * s, -38 * s, 38 * s)
                if R.ramp is not None:
                    sys.exit("safe %s: not on a ramp" % name)
                bp = O.bound(tris, R, int(at[0]), R.y, int(at[1]), (0.0, 1.0, 0.0), f, bb, "safe " + name)
                body = O.rec("Safe %s (body)" % name, obj_head(PROPDEF["safe"], MODEL["safe"], 10000 + bp, 0x000201e8))
                dx = at[0] + f[0] * 31 * s + nb[0] * 35 * s
                dz = at[1] + f[2] * 31 * s + nb[2] * 35 * s
                dbb = (-7 * s, 7 * s, 0.0, 70 * s, -35 * s, 34 * s)
                dp = O.bound(tris, R, int(round(dx)), R.y + int(round(42 * s)), int(round(dz)), (-nb[0], 0.0, -nb[2]), (0.0, 1.0, 0.0),
                             dbb, "safe door " + name)
                kf = ival(o.get("keyflags"), 0x10)
                tail = "0x00000000, 0x005a0000, 0x03e80000, 0x00000f5c, 0x00000f5c, 0x00010000, _mkword(8, 5), 0x%08x, 268435455, 16, %s" % (
                    kf & 0xFFFFFFFF, DOOR_RUNTIME)
                door_idx = O.rec("Door %s (the safe's door, keyflags 0x%x)" % (name, kf), obj_head(PROPDEF["door"], MODEL["safedoor"], dp, 0,
                                                                                             0x20000000) + ", " + tail)
                O.lua["safes"].append(door_idx)
                entry(name, "safe", door_idx, R, body=body, keyflags=kf, x=int(at[0]), y=R.y, z=int(at[1]))
            else:
                sys.exit("room %s: unknown object type %r" % (R.name, kind))

    for D, P, name in padlocks:
        lock = O.rec("StandardProp padlock on %s" % D.name, obj_head(PROPDEF["prop"], MODEL["padlock"], P.id, 0x01000008, 0x2, health=200))
        here = len(O.recs)
        O.rec("LockDoor %s" % D.name, "_mkword(0, _mkshort(0, %d)), %d, %d, 0" % (PROPDEF["lockdoor"], D.obj_index - here, lock - here))
        D.padlock_index = lock
        O.lua["padlocks"].append(lock)
        for e in O.lua["objects"]:
            if e["type"] == "door" and e["index"] == D.obj_index:
                e["padlock"] = lock
    return O


def gen_setup(name, pads, start, adj, group, gadj, members, describe, O=None):
    sym = "Usetup%sZ" % name
    out = [HEADER_NOTE % (sym + ".c", describe),
           '#include "ultra64.h"', '#include "bondtypes.h"', '#include "bondaicommands.h"', "",
           "// forward declarations", "PadRecord padlist[];", "BoundPadRecord pad3dlist[];",
           "s32 propDefs[];", "s32 intro[];", "waygroup pathsets[];", "waypoint pathwaypoints[];",
           "PathRecord patrolpaths[];", "AIListRecord ailists[];", "",
           "stagesetup %s = {" % sym, "    &pathwaypoints,", "    &pathsets,", "    &intro,",
           "    &propDefs,", "    &patrolpaths,", "    &ailists,", "    &padlist,", "    &pad3dlist,",
           "    NULL,", "    NULL", "};", "", "", "PadRecord padlist[] = {"]
    for P in pads + (O.opads if O else []):
        out.append('    { {%d.0f, %d.0f, %d.0f}, {0.0f, 1.0f, 0.0f}, {%.4ff, 0.0f, %.4ff}, "p%da1", 0 },   /* %d %s %s */' % (
            P.x, P.y, P.z, P.look[0], P.look[1], P.tile + 1, P.id, P.room.name, P.name or P.kind))
    out += ["    { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, NULL, 0 }", "};", "", "",
            "BoundPadRecord pad3dlist[] = {"]
    for k, (x, y, z, up, look, tile, bb, note) in enumerate(O.bpads if O else []):
        out.append('    { {%d.0f, %d.0f, %d.0f}, {%.4ff, %.4ff, %.4ff}, {%.4ff, %.4ff, %.4ff}, "p%da1", 0, {%.2ff, %.2ff, %.2ff, %.2ff, %.2ff, %.2ff} },'
                   '   /* %d %s */' % ((x, y, z) + tuple(up) + tuple(look) + (tile + 1,) + tuple(bb) + (k, note)))
    out += ["    { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, NULL, 0, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f} }",
            "};", "", "", "s32 propDefs[] = {"]
    recs = O.recs if O else []
    for k, (comment, text) in enumerate(recs):
        out.append("    /* index = %d: %s */" % (k, comment))
        out.append("    %s," % text)
    out += ["    /* Type = EndProps; index = %d */" % len(recs), "    _mkword(0, _mkshort(0, 48))", "};", "", "",
            "s32 intro[] = {",
            "    /* Type = Spawn; index = 0 */", "    _mkword(0, _mkshort(0, 0)), %d, 0," % start.id,
            "    /* Type = Cuff; index = 1 */", "    _mkword(0, _mkshort(0, 5)), 3,",
            "    /* Type = StartWeapon; index = 2 */", "    _mkword(0, _mkshort(0, 1)), 5, -1, 0,",
            "    /* Type = StartAmmo; index = 3 */", "    _mkword(0, _mkshort(0, 2)), 1, 100, 0,",
            "    /* Type = EndIntro; index = 4 */", "    _mkword(0, _mkshort(0, 9))", "};", "", ""]
    for P in pads:
        out.append("s32 path_table_%d[] = { %s };" % (P.id, ", ".join(str(v) for v in sorted(adj[P.id]) + [-1])))
    out += ["", "waypoint pathwaypoints[] = {"]
    for P in pads:
        out.append("    { 0x%08x, &path_table_%d, 0x%08x, 0x00000000 }," % (P.id, P.id, group[P.id]))
    out += ["    { 0xffffffff, NULL, 0x00000000, 0x00000000 }", "};", ""]
    for g in sorted(members):
        out.append("s32 path_neighbors_%d[] = { %s };" % (g, ", ".join(str(v) for v in sorted(gadj[g]) + [-1])))
    out.append("")
    for g in sorted(members):
        out.append("s32 path_indeces_%d[] = { %s };" % (g, ", ".join(str(v) for v in members[g] + [-1])))
    out += ["", "waygroup pathsets[] = {"]
    for g in sorted(members):
        out.append("    { &path_neighbors_%d, &path_indeces_%d, 0 }," % (g, g))
    out += ["    { NULL, NULL, 0 }", "};", "", "",
            "PathRecord patrolpaths[] = {", "    { NULL, 0x00, 0x00, 0x0000 }", "};", "", "",
            "AIListRecord ailists[] = {", "    { NULL, 0x00000000 }", "};", ""]
    return "\n".join(out)


def lua_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def lua_val(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    return lua_str(str(v))


def gen_lua(name, src, rooms, doors, pads, start, O=None):
    o = ["-- Generated by tools/gen_level.py from %s - do not edit; rerun the script." % src,
         "-- The layout of the generated level for mode scripts: include(\"%s_layout\") (modding/levelgen.md)." % name,
         "LEVEL = {", "  name = %s," % lua_str(name), "  start_pad = %d," % start.id, "  rooms = {"]
    for R in rooms:
        grid_ids = ", ".join(str(P.id) for P in R.grid_pads)
        named = ", ".join("[%s] = %d" % (lua_str(P.name), P.id) for P in pads if P.room is R and P.kind == "named")
        extra = ""
        if R.area:
            extra += ", area = %s" % lua_str(R.area)
        if R.ramp is not None:
            extra += ", ramp = { axis = %s, to = %d, stairs = %d }" % (lua_str(R.ramp["axis"]), R.ramp_to, R.stairs)
        if R.blocks:
            extra += ", blocks = { %s }" % ", ".join("{ name = %s, rect = { %d, %d, %d, %d }, height = %d }" % (
                lua_str(B.name), B.x0, B.z0, B.x1, B.z1, B.h) for B in R.blocks)
        o.append("    [%s] = { id = %d, rect = { %d, %d, %d, %d }, floor = %d, height = %d, pads = { %s }, named = { %s }%s }," % (
            lua_str(R.name), R.id, R.x0, R.z0, R.x1, R.z1, R.y, R.h, grid_ids, named, extra))
    o.append("  },")
    o.append("  room_name = { %s }," % ", ".join("[%d] = %s" % (R.id, lua_str(R.name)) for R in rooms))
    o.append("  doors = {")
    for D in doors:
        mid = (D.lo + D.hi) / 2.0
        cx, cz = ((D.fa + D.fb) / 2.0, mid) if D.axis == "x" else (mid, (D.fa + D.fb) / 2.0)
        extra = ""
        if getattr(D, "obj_index", None) is not None:
            extra = ", object = %d, keyflags = %d" % (D.obj_index, D.keyflags)
            if getattr(D, "obj_index2", None) is not None:
                extra += ", object2 = %d" % D.obj_index2
            if getattr(D, "padlock_index", None) is not None:
                extra += ", padlock = %d" % D.padlock_index
        o.append("    { name = %s, a = %s, b = %s, portal = %d, pad = %d, pad_a = %d, pad_b = %d, x = %.0f, z = %.0f, width = %d, height = %d%s }," % (
            lua_str(D.name), lua_str(D.a.name), lua_str(D.b.name), D.index, D.pad.id, D.pad_a.id, D.pad_b.id, cx, cz, D.w, D.h, extra))
    o.append("  },")
    o.append("  pads = { %s }," % ", ".join("[%s] = %d" % (lua_str(P.name), P.id) for P in pads if P.name))
    if O is not None:
        # setup objects (P2.2): every object by name, and the setup indices of each kind (door.*, cctv.*, the use / gadget events)
        o.append("  objects = {")
        for e in O.lua["objects"]:
            o.append("    [%s] = { %s }," % (lua_str(e["name"]), ", ".join("%s = %s" % (k, lua_val(v)) for k, v in e.items() if k != "name")))
        o.append("  },")
        for kind in ("cameras", "alarms", "monitors", "keys", "safes", "padlocks"):
            o.append("  %s = { %s }," % (kind, ", ".join(str(i) for i in O.lua[kind])))
        loot = []
        for R in rooms:
            for lt in R.loot:
                ids = [P.id for P in pads if P.name and P.name.startswith(lt["name"] + "_") and P.room is R]
                loot.append("[%s] = { %s }" % (lua_str(lt["name"]), ", ".join(str(i) for i in ids)))
        o.append("  loot = { %s }," % ", ".join(loot))
    o.append("}")
    o.append("")
    return "\n".join(o)


HEADER_NOTE = """/*
* Generated by tools/gen_level.py - do not edit; rerun the script.
* %s
* %s
*/
"""


def main():
    ap = argparse.ArgumentParser(description="Generate a multi-room level from a layout (modding/levelgen.md)")
    ap.add_argument("layout", help="layout JSON (tools/levelgen/*.json)")
    ap.add_argument("--out", default=None, help="output directory (default port/<name>)")
    a = ap.parse_args()
    L, d, mats, rooms, byname, doors = load_layout(a.layout)
    name = L["name"]
    if not name.isalnum() or not name.islower():
        sys.exit("name must be lower-case letters and digits")
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = a.out or os.path.join(root, "port", name)
    os.makedirs(out, exist_ok=True)
    maxcell = int(d.get("cell", 300))
    check_door_clearance(d, rooms, doors, root)
    src = os.path.relpath(os.path.abspath(a.layout), root).replace("\\", "/")
    describe = "%s: %d rooms, %d doorways (from %s)" % (name, len(rooms), len(doors), src)

    grids = room_grids(rooms, doors, maxcell)
    meshes = {R.id: build_room_mesh(R, mats, grids[R.id][0], grids[R.id][1]) for R in rooms}
    for D in doors:
        add_passage_mesh(meshes[D.a.id], D, mats, maxcell)
        if D.frame:
            ina = -1 if D.fa < D.fb else 1                 # room a lies this way from its face
            add_frame_mesh(meshes[D.a.id], D.a, D, D.fa, ina, mats)
            add_frame_mesh(meshes[D.b.id], D.b, D, D.fb, -ina, mats)
    tris = build_stan(rooms, grids, doors, maxcell)
    pads, start = build_pads(L, d, rooms, byname, grids, doors, tris, maxcell)
    adj, group, gadj, members = build_waypoints(pads, rooms, doors, maxcell)
    O = build_objects(rooms, byname, doors, pads, tris, len(pads))

    bgc, bgh, stats = gen_bg(name, rooms, meshes, doors, describe)
    stan, nlinks, cross = gen_stan(name, tris, rooms, describe)
    setup = gen_setup(name, pads, start, adj, group, gadj, members, describe, O)
    lua = gen_lua(name, src, rooms, doors, pads, start, O)
    for fn, text in (("bg_%s_all_p.c" % name, bgc), ("bg_%s_all_p.h" % name, bgh),
                     ("Tbg_%s_all_p_stanZ.c" % name, stan), ("Usetup%sZ.c" % name, setup),
                     ("%s_layout.lua" % name, lua)):
        with open(os.path.join(out, fn), "w", newline="\n") as f:
            f.write(text)
    # self-checks: every doorway joins its rooms' floors, every room's pads reach every other room's
    if cross < len(doors):            # the doorway floor is room a's: it meets room b's floor at the portal
        linked = set()
        for key, v in stan_edges(tris).items():
            if len(v) == 2 and tris[v[0]][0] != tris[v[1]][0]:
                linked.add(frozenset((tris[v[0]][0], tris[v[1]][0])))
        bad = [D.name for D in doors if frozenset((D.a.id, D.b.id)) not in linked]
        sys.exit("stan: only %d links cross a room boundary for %d doorways (not joined: %s)" % (cross, len(doors), ", ".join(bad)))
    seen, todo = {start.id}, [start.id]
    while todo:
        u = todo.pop()
        for v in adj[u]:
            if v not in seen:
                seen.add(v)
                todo.append(v)
    if len(seen) != len(pads):
        sys.exit("waypoints: %d of %d pads are not reachable from the start" % (len(pads) - len(seen), len(pads)))
    for R, (nv, nc) in zip(rooms, stats):
        print("  room %d %-10s %5d x %5d  verts %4d  dl cmds %4d  pads %d" % (
            R.id, R.name, R.x1 - R.x0, R.z1 - R.z0, nv, nc, sum(1 for P in pads if P.room is R)))
    for D in doors:
        print("  doorway %d %s: rooms %d-%d, portal at %s=%d, opening %d..%d x %d high, pads %d (door) %d %d" % (
            D.index, D.name, D.a.id, D.b.id, D.axis, D.fb, D.lo, D.hi, D.h, D.pad.id, D.pad_a.id, D.pad_b.id))
    for k, (comment, _) in enumerate(O.recs):
        print("  object %d: %s" % (k, comment))
    print("%s: %d tiles (%d links, %d across rooms), %d pads (start %d) + %d object pads, %d bound pads, %d objects, %d waypoint groups, out=%s" % (
        describe, len(tris), nlinks, cross, len(pads), start.id, len(O.opads), len(O.bpads), len(O.recs), len(members), out))


if __name__ == "__main__":
    main()

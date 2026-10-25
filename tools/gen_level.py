#!/usr/bin/env python3
"""
gen_level.py - generate a multi-room level from a layout description (designs/bank-heist.md P2, modding/levelgen.md).

Reads a JSON layout (tools/levelgen/*.json: box rooms, doorways between them, materials, lights, named pads) and writes,
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
        R.pads_named = r.get("pads", [])
        R.doors = []
        rooms.append(R)
        if R.name in byname:
            sys.exit("duplicate room name %s" % R.name)
        byname[R.name] = R
    for a in rooms:
        for b in rooms:
            if a is not b and a.x0 < b.x1 and b.x0 < a.x1 and a.z0 < b.z1 and b.z0 < a.z1:
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
        if a.y != b.y:
            sys.exit("door %s-%s: the rooms' floors differ (stairs are not supported yet)" % (a.name, b.name))
        D.y = a.y
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
        if D.h > min(a.h, b.h):
            sys.exit("door %s-%s: taller than a room" % (a.name, b.name))
        D.name = dd.get("name", "%s-%s" % (a.name, b.name))
        a.doors.append(D)
        b.doors.append(D)
        doors.append(D)
    return L, d, mats, rooms, byname, doors


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
    if "lamps" in li:
        return [(float(p[0]), R.y + R.h - 20.0, float(p[1])) for p in li["lamps"]]
    step = float(li.get("lamp_spacing", 520))
    nx = max(1, int(round((R.x1 - R.x0) / step)))
    nz = max(1, int(round((R.z1 - R.z0) / step)))
    return [(R.x0 + (R.x1 - R.x0) * (i + 0.5) / nx, R.y + R.h - 20.0, R.z0 + (R.z1 - R.z0) * (j + 0.5) / nz)
            for j in range(nz) for i in range(nx)]


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


def vpoly(mesh, R, lamps, mats, matname, a, b, y0, y1, top):
    """Vertical quad along floor segment a -> b (x, z), y0..y1; s along the wall, t down from `top`."""
    tex, k = material(mats, matname)
    face = FACE_WALL_X if a[0] == b[0] else FACE_WALL_Z
    ax = 1 if a[0] == b[0] else 0            # s is the world coordinate along the wall: the texture runs on across segments
    corners = [(a, y1, a[ax]), (b, y1, b[ax]), (b, y0, b[ax]), (a, y0, a[ax])]
    mesh.add(tex, [(p[0], y, p[1], u * k, (top - y) * k, shade(R, lamps, p[0], y, p[1], face)) for (p, y, u) in corners])


def build_room_mesh(R, mats, maxcell):
    """The room's geometry and its floor grid (the stan and the pads use the same grid)."""
    mesh = Mesh()
    lamps = lamps_of(R)
    xs = [R.x0, R.x1]
    zs = [R.z0, R.z1]
    for D in R.doors:
        if D.axis == "z":
            xs += [D.lo, D.hi]
        else:
            zs += [D.lo, D.hi]
    xs, zs = grid(xs, maxcell), grid(zs, maxcell)
    top = R.y + R.h
    for j in range(len(zs) - 1):
        for i in range(len(xs) - 1):
            q = [(xs[i], zs[j]), (xs[i + 1], zs[j]), (xs[i + 1], zs[j + 1]), (xs[i], zs[j + 1])]
            hpoly(mesh, R, lamps, mats, R.floor_mat, q, R.y, FACE_FLOOR)
            hpoly(mesh, R, lamps, mats, R.ceiling_mat, q, top, FACE_CEIL)
    # the four walls, cut at the door openings; every segment of a wall is split at the heights of its doors (no T-junctions)
    sides = {"z0": [((x, R.z0), (x2, R.z0)) for x, x2 in zip(xs, xs[1:])],
             "z1": [((x2, R.z1), (x, R.z1)) for x, x2 in zip(xs, xs[1:])],
             "x0": [((R.x0, z2), (R.x0, z)) for z, z2 in zip(zs, zs[1:])],
             "x1": [((R.x1, z), (R.x1, z2)) for z, z2 in zip(zs, zs[1:])]}
    for side, segs in sides.items():
        sdoors = [D for D in R.doors if side_of(R, D) == side]
        ys = sorted(set([R.y, top] + [D.y + D.h for D in sdoors]))
        for (a, b) in segs:
            along = (a[0] + b[0]) / 2.0 if side[0] == "z" else (a[1] + b[1]) / 2.0
            opening = [D for D in sdoors if D.lo < along < D.hi]
            for y0, y1 in zip(ys, ys[1:]):
                if opening and y1 <= opening[0].y + opening[0].h:
                    continue
                vpoly(mesh, R, lamps, mats, R.wall_mats.get(side, R.wall_mat), a, b, y0, y1, top)
    return mesh, xs, zs, lamps


def add_passage_mesh(mesh, D, mats, maxcell):
    """The doorway's own faces (they belong to room a): floor, the lintel's underside, the two jambs."""
    R = D.a
    lamps = lamps_of(R)
    f0, f1 = sorted((D.fa, D.fb))
    span = split_span(D.lo, D.hi, maxcell)
    ytop = D.y + D.h
    top = R.y + R.h
    for s0, s1 in zip(span, span[1:]):
        if D.axis == "x":
            q = [(f0, s0), (f1, s0), (f1, s1), (f0, s1)]
        else:
            q = [(s0, f0), (s1, f0), (s1, f1), (s0, f1)]
        hpoly(mesh, R, lamps, mats, R.floor_mat, q, D.y, FACE_FLOOR)
        hpoly(mesh, R, lamps, mats, R.wall_mat, q, ytop, FACE_CEIL)
    for s in (D.lo, D.hi):
        if D.axis == "x":
            a, b = (f0, s), (f1, s)
        else:
            a, b = (s, f0), (s, f1)
        vpoly(mesh, R, lamps, mats, R.wall_mat, a, b, D.y, ytop, top)


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
    """Two triangles of a cell, each wound with cross((B-A), (C-A)) < 0 in (x, z) (the stan inside test)."""
    out = []
    for (a, b, c) in (((x0, y, z0), (x1, y, z1), (x1, y, z0)), ((x0, y, z0), (x0, y, z1), (x1, y, z1))):
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
        for j in range(len(zs) - 1):
            for i in range(len(xs) - 1):
                for t in cell_tris(xs[i], zs[j], xs[i + 1], zs[j + 1], R.y):
                    tris.append((R.id, t))
        for D in doors:
            if D.a is not R:
                continue
            f0, f1 = sorted((D.fa, D.fb))
            span = split_span(D.lo, D.hi, maxcell)
            for s0, s1 in zip(span, span[1:]):
                cell = (f0, s0, f1, s1) if D.axis == "x" else (s0, f0, s1, f1)
                for t in cell_tris(cell[0], cell[1], cell[2], cell[3], D.y):
                    tris.append((R.id, t))
    return tris


def gen_stan(name, tris, rooms, describe):
    edges = {}
    for n, (_, t) in enumerate(tris):
        for k in range(3):
            edges.setdefault(frozenset((t[k], t[(k + 1) % 3])), []).append(n)
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
        P.room, P.x, P.z, P.y, P.tile, P.kind, P.name, P.door = room, px, pz, room.y, n, kind, name, door
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
                if w >= minc and h >= minc:
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
                if math.hypot(p.x - q.x, p.z - q.z) <= reach:
                    adj[p.id].add(q.id)
                    adj[q.id].add(p.id)
        for p in mine:              # nothing may be left on its own
            if not adj[p.id] and len(mine) > 1:
                q = min((q for q in mine if q is not p), key=lambda q: math.hypot(p.x - q.x, p.z - q.z))
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


def gen_setup(name, pads, start, adj, group, gadj, members, describe):
    sym = "Usetup%sZ" % name
    out = [HEADER_NOTE % (sym + ".c", describe),
           '#include "ultra64.h"', '#include "bondtypes.h"', '#include "bondaicommands.h"', "",
           "// forward declarations", "PadRecord padlist[];", "BoundPadRecord pad3dlist[];",
           "s32 propDefs[];", "s32 intro[];", "waygroup pathsets[];", "waypoint pathwaypoints[];",
           "PathRecord patrolpaths[];", "AIListRecord ailists[];", "",
           "stagesetup %s = {" % sym, "    &pathwaypoints,", "    &pathsets,", "    &intro,",
           "    &propDefs,", "    &patrolpaths,", "    &ailists,", "    &padlist,", "    &pad3dlist,",
           "    NULL,", "    NULL", "};", "", "", "PadRecord padlist[] = {"]
    for P in pads:
        out.append('    { {%d.0f, %d.0f, %d.0f}, {0.0f, 1.0f, 0.0f}, {%.4ff, 0.0f, %.4ff}, "p%da1", 0 },   /* %d %s %s */' % (
            P.x, P.y, P.z, P.look[0], P.look[1], P.tile + 1, P.id, P.room.name, P.name or P.kind))
    out += ["    { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, NULL, 0 }", "};", "", "",
            "BoundPadRecord pad3dlist[] = {",
            "    { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, NULL, 0, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f} }",
            "};", "", "",
            "s32 propDefs[] = {", "    /* Type = EndProps; index = 0 */", "    _mkword(0, _mkshort(0, 48))", "};", "", "",
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


def gen_lua(name, src, rooms, doors, pads, start):
    o = ["-- Generated by tools/gen_level.py from %s - do not edit; rerun the script." % src,
         "-- The layout of the generated level for mode scripts: include(\"%s_layout\") (modding/levelgen.md)." % name,
         "LEVEL = {", "  name = %s," % lua_str(name), "  start_pad = %d," % start.id, "  rooms = {"]
    for R in rooms:
        grid_ids = ", ".join(str(P.id) for P in R.grid_pads)
        named = ", ".join("[%s] = %d" % (lua_str(P.name), P.id) for P in pads if P.room is R and P.kind == "named")
        o.append("    [%s] = { id = %d, rect = { %d, %d, %d, %d }, floor = %d, height = %d, pads = { %s }, named = { %s } }," % (
            lua_str(R.name), R.id, R.x0, R.z0, R.x1, R.z1, R.y, R.h, grid_ids, named))
    o.append("  },")
    o.append("  room_name = { %s }," % ", ".join("[%d] = %s" % (R.id, lua_str(R.name)) for R in rooms))
    o.append("  doors = {")
    for D in doors:
        mid = (D.lo + D.hi) / 2.0
        cx, cz = ((D.fa + D.fb) / 2.0, mid) if D.axis == "x" else (mid, (D.fa + D.fb) / 2.0)
        o.append("    { name = %s, a = %s, b = %s, portal = %d, pad = %d, pad_a = %d, pad_b = %d, x = %.0f, z = %.0f, width = %d, height = %d }," % (
            lua_str(D.name), lua_str(D.a.name), lua_str(D.b.name), D.index, D.pad.id, D.pad_a.id, D.pad_b.id, cx, cz, D.w, D.h))
    o.append("  },")
    o.append("  pads = { %s }," % ", ".join("[%s] = %d" % (lua_str(P.name), P.id) for P in pads if P.name))
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
    src = os.path.relpath(os.path.abspath(a.layout), root).replace("\\", "/")
    describe = "%s: %d rooms, %d doorways (from %s)" % (name, len(rooms), len(doors), src)

    meshes, grids = {}, {}
    for R in rooms:
        m, xs, zs, _ = build_room_mesh(R, mats, maxcell)
        meshes[R.id] = m
        grids[R.id] = (xs, zs)
    for D in doors:
        add_passage_mesh(meshes[D.a.id], D, mats, maxcell)
    tris = build_stan(rooms, grids, doors, maxcell)
    pads, start = build_pads(L, d, rooms, byname, grids, doors, tris, maxcell)
    adj, group, gadj, members = build_waypoints(pads, rooms, doors, maxcell)

    bgc, bgh, stats = gen_bg(name, rooms, meshes, doors, describe)
    stan, nlinks, cross = gen_stan(name, tris, rooms, describe)
    setup = gen_setup(name, pads, start, adj, group, gadj, members, describe)
    lua = gen_lua(name, src, rooms, doors, pads, start)
    for fn, text in (("bg_%s_all_p.c" % name, bgc), ("bg_%s_all_p.h" % name, bgh),
                     ("Tbg_%s_all_p_stanZ.c" % name, stan), ("Usetup%sZ.c" % name, setup),
                     ("%s_layout.lua" % name, lua)):
        with open(os.path.join(out, fn), "w", newline="\n") as f:
            f.write(text)
    # self-checks: every doorway joins its rooms' floors, every room's pads reach every other room's
    if cross < len(doors):            # the doorway floor is room a's: it meets room b's floor at the portal
        sys.exit("stan: only %d links cross a room boundary for %d doorways" % (cross, len(doors)))
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
    print("%s: %d tiles (%d links, %d across rooms), %d pads (start %d), %d waypoint groups, out=%s" % (
        describe, len(tris), nlinks, cross, len(pads), start.id, len(members), out))


if __name__ == "__main__":
    main()

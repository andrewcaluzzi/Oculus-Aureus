#!/usr/bin/env python3
"""
gen_colosseum.py - generate "Colosseum", the gladiatorial arena level.

Same data shape (and a lot of the same helper code) as gen_flatland.py; writes
four C sources into port/colosseum/:

  bg_colosseum_all_p.c         one bg room: sand floor, perimeter wall, pillars,
                               low walls, four gate alcoves, tiered stands
  Tbg_colosseum_all_p_stanZ.c  walkable triangles (octagon pit + gate alcoves,
                               holes under the pillars and low walls)
  UsetupcolosseumZ.c           pads on a 500-unit grid, a waypoint graph over
                               them, Bond start pad, no objects or guards
  colosseum_layout.json        pad ids per gate, cover rects, seat list (for
                               the Lua arena mode and the crowd)

CMake compiles them into the exe (see CMakeLists.txt "Colosseum"). The level
takes over the cut MP stage slot LEVELID_EAR (see modding/arena.md).

    python3 tools/gen_colosseum.py

Units: levelscale for the slot is 1.0, so bg, stan and world units are equal.
The pit floor is at y = 0, centred on the origin. Heading 0 = +z, 90 = -x.
"""

import argparse
import json
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_flatland as gf          # noqa: E402  (deflate, words, vtx, gfx, tri4 helpers)

CELL = 500                  # floor cell = pad spacing
HALF = 4500                 # axis half extent of the pit octagon (9000 across)
CUT = 2500                  # corner leg of the octagon: |x|+|z| <= 2*HALF-CUT
DIAG = 2 * HALF - CUT       # 6500
NC = 2 * HALF // CELL       # 18 cells per side

WALL_H = 350                # perimeter wall height (blocks Bond visually; the stan stops him)
WALL_T = 200                # wall thickness (top cap)
TIERS = 12
TIER_DEPTH = 325
TIER_RISE = 220
GATE_HALF = 500             # alcove half width = stand gap half width
GATE_DEPTH_CELLS = 3
GATE_TIERS = 4              # tiers cut open by the gate corridor (4 * 325 + 200 = 1500 = alcove depth)

SEAT_STEP = 420             # spacing of spectator seats along a tread

PILLAR_H = 700
LOWWALL_H = 120

TEX_FLOOR = 1264
TEX_WALL = 1263


# ------------------------------------------------------------- geometry ------

def oct_pts(d):
    """Octagon offset outward by d: 8 points; side i runs P[i] -> P[i+1].
    Even sides are axis sides (+x, +z, -x, -z); odd sides are the diagonals."""
    a = HALF + d
    s = DIAG + d * math.sqrt(2.0)
    t = s - a
    return [(a, -t), (a, t), (t, a), (-t, a), (-a, t), (-a, -t), (-t, -a), (t, -a)]


def side_frame(i):
    """(outward normal, along-side unit vector) of side i of the octagon."""
    p0, p1 = oct_pts(0)[i], oct_pts(0)[(i + 1) % 8]
    dx, dz = p1[0] - p0[0], p1[1] - p0[1]
    ln = math.hypot(dx, dz)
    u = (dx / ln, dz / ln)
    n = (u[1], -u[0])          # right-hand normal; points outward for this winding
    return n, u


def tier_d(k):
    return WALL_T + k * TIER_DEPTH


def tier_h(k):
    return WALL_H + (k + 1) * TIER_RISE


class Mesh:
    """Polygons (3 or 4 vertices) grouped by texture."""

    def __init__(self):
        self.polys = {TEX_FLOOR: [], TEX_WALL: []}
        self.seats = []

    def add(self, tex, pts, rgb, shade=1.0):
        """pts: [(x, y, z, u, v)] with u, v in world units; the poly's min is moved to 0."""
        umin = min(p[3] for p in pts)
        vmin = min(p[4] for p in pts)
        col = tuple(max(0, min(255, int(c * shade))) for c in rgb) + (0xFF,)
        self.polys[tex].append([(p[0], p[1], p[2], p[3] - umin, p[4] - vmin, col) for p in pts])


def vquad(m, tex, a, b, y0, y1, rgb, shade=1.0):
    """Vertical quad along ground segment a -> b (x, z), from y0 up to y1."""
    ln = math.hypot(b[0] - a[0], b[1] - a[1])
    m.add(tex, [(a[0], y1, a[1], 0, 0), (b[0], y1, b[1], ln, 0),
                (b[0], y0, b[1], ln, y1 - y0), (a[0], y0, a[1], 0, y1 - y0)], rgb, shade)


def hquad(m, tex, pts, y, rgb, shade=1.0):
    """Horizontal polygon; u, v from world x, z."""
    m.add(tex, [(p[0], y, p[1], p[0], p[1]) for p in pts], rgb, shade)


def box(m, x0, x1, z0, z1, h, side_rgb, top_rgb):
    c = [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]
    for k in range(4):
        vquad(m, TEX_WALL, c[k], c[(k + 1) % 4], 0, h, side_rgb)
    hquad(m, TEX_FLOOR, c, h, top_rgb)


# ------------------------------------------------------------ walkable set ---

def rot90(r):
    x0, x1, z0, z1 = r
    return (-z1, -z0, x0, x1)


def rotations(r):
    out = [r]
    for _ in range(3):
        r = rot90(r)
        out.append(r)
    return out


PILLARS = rotations((2500, 3500, -500, 500))
LOWWALLS = rotations((1000, 3000, 1500, 2000))


def in_rect(x, z, r):
    return r[0] < x < r[1] and r[2] < z < r[3]


def classify_pit(i, j):
    """'full', 'out' or ('cut', far corner (x, z)) for pit cell (i, j)."""
    x0, z0 = -HALF + i * CELL, -HALF + j * CELL
    x1, z1 = x0 + CELL, z0 + CELL
    res = "full"
    for sx in (1, -1):
        for sz in (1, -1):
            xn, zn = (x0, z0) if sx > 0 else (x1, z1)
            xf, zf = (x1, z1) if sx > 0 else (x0, z0)
            nsum = sx * xn + sz * (z0 if sz > 0 else z1)
            fsum = sx * xf + sz * (z1 if sz > 0 else z0)
            if nsum >= DIAG:
                return "out"
            if fsum > DIAG:
                if nsum + CELL != DIAG:
                    raise SystemExit("cell (%d,%d) is cut off the diagonal" % (i, j))
                res = ("cut", (xf, z1 if sz > 0 else z0))
    return res


def gate_cells(g):
    """Alcove cells of gate g (0=+x, 1=+z, 2=-x, 3=-z): (centre x, centre z, depth index 0 = mouth)."""
    out = []
    for dpt in range(GATE_DEPTH_CELLS):
        for lat in (-1, 1):
            a = HALF + dpt * CELL + CELL // 2            # along the gate axis
            b = lat * (CELL // 2)                        # lateral
            x, z = [(a, b), (-b, a), (-a, -b), (b, -a)][g]
            out.append((x, z, dpt))
    return out


def build_cells():
    """-> list of full cells {x0,z0,kind,gate} and list of cut triangles."""
    full, cuts = [], []
    for j in range(NC):
        for i in range(NC):
            c = classify_pit(i, j)
            x0, z0 = -HALF + i * CELL, -HALF + j * CELL
            cx, cz = x0 + CELL // 2, z0 + CELL // 2
            if c == "out":
                continue
            if any(in_rect(cx, cz, r) for r in PILLARS + LOWWALLS):
                continue
            if c == "full":
                full.append({"x0": x0, "z0": z0, "gate": -1})
            else:
                cuts.append((x0, z0, c[1]))
    for g in range(4):
        for (cx, cz, dpt) in gate_cells(g):
            full.append({"x0": cx - CELL // 2, "z0": cz - CELL // 2, "gate": g, "depth": dpt})
    return full, cuts


# ----------------------------------------------------------------- stan ------

LINK_BASE = 16


def build_tris(full, cuts):
    """Triangles as ((x,y,z) x3) with the stan winding (cross in x,z < 0); also the first tile per full cell.
    Then the stands' treads (flat tiles at the tread height, a separate mesh: no tile links to the pit or to
    another tier): returns (tris, first, stand) with stand = [(tri index, tier, tri)]."""
    tris = []
    first = {}
    stand = []

    def add(a, b, c):
        cr = (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0])
        if cr == 0:
            return None
        if cr > 0:
            b, c = c, b
        tris.append((a, b, c))
        return len(tris) - 1

    for c in full:
        x0, z0 = c["x0"], c["z0"]
        x1, z1 = x0 + CELL, z0 + CELL
        p00, p10, p11, p01 = (x0, 0, z0), (x1, 0, z0), (x1, 0, z1), (x0, 0, z1)
        first[(x0, z0)] = len(tris)
        add(p00, p11, p10)
        add(p00, p01, p11)
    for (x0, z0, far) in cuts:
        x1, z1 = x0 + CELL, z0 + CELL
        corners = [(x0, 0, z0), (x1, 0, z0), (x1, 0, z1), (x0, 0, z1)]
        keep = [p for p in corners if (p[0], p[2]) != far]
        assert len(keep) == 3
        add(*keep)
    for k in range(TIERS):
        d0, d1, h = tier_d(k), tier_d(k + 1), tier_h(k)
        for i in range(8):
            for q in strips(i, d0, d1, k < GATE_TIERS):
                pts = [(int(round(p[0])), h, int(round(p[1]))) for p in q]
                for tri in ((pts[0], pts[1], pts[2]), (pts[0], pts[2], pts[3])):
                    n = add(*tri)
                    if n is not None:
                        stand.append((n, k, tris[n]))
    return tris, first, stand


def inside_xz(p, t):
    """(x, z) strictly inside triangle t (3 (x,y,z) points), with a small margin."""
    def side(a, b):
        return (b[0] - a[0]) * (p[1] - a[2]) - (b[2] - a[2]) * (p[0] - a[0])
    s = [side(t[0], t[1]), side(t[1], t[2]), side(t[2], t[0])]
    return all(v < -8000 for v in s)    # winding is cross < 0; 8000 = ~ 20 units from a 400-unit edge


def gen_stan(tris, describe):
    edges = {}
    for n, t in enumerate(tris):
        for k in range(3):
            key = frozenset((t[k], t[(k + 1) % 3]))
            edges.setdefault(key, []).append((n, k))
    for key, v in edges.items():
        assert len(v) <= 2, "edge shared by %d tiles" % len(v)

    def link(n, k):
        t = tris[n]
        v = edges[frozenset((t[k], t[(k + 1) % 3]))]
        if len(v) == 1:
            return 0
        other = v[0][0] if v[1][0] == n else v[1][0]
        return LINK_BASE + 4 * other

    out = [gf.HEADER_NOTE % ("Tbg_colosseum_all_p_stanZ.c", describe),
           "#include <ultra64.h>", "#include <game/stan.h>", "",
           "// forward declarations", "StandTile tile_0;", "",
           "StandFileHeader Tbg_colosseum_all_p_stanZ = {", "    NULL,", "    &tile_0,",
           "    {0x00, 0x00, 0x00, 0x00}", "};", ""]
    for n, t in enumerate(tris):
        ident = ((n + 1) << 8) | 1
        out.append("StandTile tile_%d = {" % n)
        out.append("    0x%06x, 0x%02x," % (ident, 1))
        out.append("    0x0,")
        out.append("    0x4, 0x4, 0x4,")
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
    return "\n".join(out)


# ---------------------------------------------------------------- setup ------

GROUP_MAX = 1     # drop A5: one waypoint per group. A route crosses a group border at the FIRST linked waypoint found, so 12-pad clusters made chrs zig-zag


def gen_setup(pads, adj, start, describe, extra=()):
    """pads: [(x, z, tilename)] (the waypoint pads), adj: {i: [j..]} (reciprocal); extra: more pads after them, with no
    waypoint: [(x, y, z, look_x, look_z, tilename)] (the spectators' seats)."""
    n = len(pads)
    # groups: BFS clusters of up to GROUP_MAX connected waypoints
    grp = [-1] * n
    members = []
    for s in range(n):
        if grp[s] >= 0:
            continue
        g = len(members)
        cl = [s]
        grp[s] = g
        q = [s]
        while q and len(cl) < GROUP_MAX:
            u = q.pop(0)
            for v in adj[u]:
                if grp[v] < 0 and len(cl) < GROUP_MAX:
                    grp[v] = g
                    cl.append(v)
                    q.append(v)
        members.append(cl)
    gneigh = [set() for _ in members]
    for u in range(n):
        for v in adj[u]:
            if grp[u] != grp[v]:
                gneigh[grp[u]].add(grp[v])

    out = [gf.HEADER_NOTE % ("UsetupcolosseumZ.c", describe),
           '#include "ultra64.h"', '#include "bondtypes.h"', '#include "bondaicommands.h"', "",
           "// forward declarations", "PadRecord padlist[];", "BoundPadRecord pad3dlist[];",
           "s32 propDefs[];", "s32 intro[];", "waygroup pathsets[];", "waypoint pathwaypoints[];",
           "PathRecord patrolpaths[];", "AIListRecord ailists[];", "",
           "stagesetup UsetupcolosseumZ = {", "    &pathwaypoints,", "    &pathsets,", "    &intro,",
           "    &propDefs,", "    &patrolpaths,", "    &ailists,", "    &padlist,", "    &pad3dlist,",
           "    NULL,", "    NULL", "};", "", "", "PadRecord padlist[] = {"]
    for (x, z, name) in pads:
        out.append('    { {%d.0f, 0.0f, %d.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, "%s", 0 },' % (x, z, name))
    for (x, y, z, lx, lz, name) in extra:
        out.append('    { {%d.0f, %d.0f, %d.0f}, {0.0f, 1.0f, 0.0f}, {%.4ff, 0.0f, %.4ff}, "%s", 0 },' % (x, y, z, lx, lz, name))
    out.append("    { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, NULL, 0 }")
    out.append("};")
    out.append("")
    out.append("")
    out.append("BoundPadRecord pad3dlist[] = {")
    out.append("    { {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, NULL, 0, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f} }")
    out.append("};")
    out.append("")
    out.append("")
    out.append("s32 propDefs[] = {")
    out.append("    /* Type = EndProps; index = 0 */")
    out.append("    _mkword(0, _mkshort(0, 48))")
    out.append("};")
    out.append("")
    out.append("")
    out.append("s32 intro[] = {")
    out.append("    /* Type = Spawn; index = 0 */")
    out.append("    _mkword(0, _mkshort(0, 0)), %d, 0," % start)
    out.append("    /* Type = Cuff; index = 1 */")
    out.append("    _mkword(0, _mkshort(0, 5)), 3,")
    out.append("    /* Type = StartWeapon; index = 2 */")
    out.append("    _mkword(0, _mkshort(0, 1)), 5, -1, 0,")
    out.append("    /* Type = StartAmmo; index = 3 */")
    out.append("    _mkword(0, _mkshort(0, 2)), 1, 100, 0,")
    out.append("    /* Type = EndIntro; index = 4 */")
    out.append("    _mkword(0, _mkshort(0, 9))")
    out.append("};")
    out.append("")
    out.append("")
    for w in range(n):
        out.append("s32 path_table_%d[] = { %s };" % (w, ", ".join(str(v) for v in adj[w] + [-1])))
    out.append("")
    out.append("waypoint pathwaypoints[] = {")
    for w in range(n):
        out.append("    { 0x%08x, &path_table_%d, 0x%08x, 0x00000000 }," % (w, w, grp[w]))
    out.append("    { 0xffffffff, NULL, 0x00000000, 0x00000000 }")
    out.append("};")
    out.append("")
    for g in range(len(members)):
        out.append("s32 path_neighbors_%d[] = { %s };" % (g, ", ".join(str(v) for v in sorted(gneigh[g]) + [-1])))
    out.append("")
    for g in range(len(members)):
        out.append("s32 path_indeces_%d[] = { %s };" % (g, ", ".join(str(v) for v in members[g] + [-1])))
    out.append("")
    out.append("waygroup pathsets[] = {")
    for g in range(len(members)):
        out.append("    { &path_neighbors_%d, &path_indeces_%d, 0 }," % (g, g))
    out.append("    { NULL, NULL, 0 }")
    out.append("};")
    out.append("")
    out.append("")
    out.append("PathRecord patrolpaths[] = {")
    out.append("    { NULL, 0x00, 0x00, 0x0000 }")
    out.append("};")
    out.append("")
    out.append("")
    out.append("AIListRecord ailists[] = {")
    out.append("    { NULL, 0x00000000 }")
    out.append("};")
    out.append("")
    return "\n".join(out), len(members)


# ------------------------------------------------------------- the visuals ---

SAND = (0xE8, 0xCE, 0x98)
SAND_GATE = (0x8E, 0x74, 0x54)
STONE_WALL = (0xD2, 0xB2, 0x8A)
STONE_CAP = (0xE6, 0xCE, 0xA6)
STONE_PILLAR = (0xC8, 0xAE, 0x8A)
STONE_LOW = (0xC0, 0xA8, 0x86)
GATE_DOOR = (0x58, 0x4C, 0x46)
STAND_A = (0xC8, 0xBC, 0xA6)
STAND_B = (0xA8, 0x98, 0x80)
STAND_RISER = (0x98, 0x8A, 0x76)


def ring_point(d, i, lat=None):
    """Point on side i of ring d; lat (axis sides only) = lateral coordinate, else None."""
    pts = oct_pts(d)
    return pts[i] if lat is None else None


def axis_point(d, i, lat):
    """Point on axis side i (even) of ring d at lateral coordinate lat (along-side unit vector)."""
    n, u = side_frame(i)
    a = HALF + d
    return (n[0] * a + u[0] * lat, n[1] * a + u[1] * lat)


def side_lat(i, p):
    n, u = side_frame(i)
    return p[0] * u[0] + p[1] * u[1]


def strips(i, d0, d1, gap):
    """Quads (as 4-point lists, inner edge first) of the strip between ring d0 and d1 on side i.
    Axis sides are split around the gate gap when `gap` is true."""
    r0, r1 = oct_pts(d0), oct_pts(d1)
    a0, b0, a1, b1 = r0[i], r0[(i + 1) % 8], r1[i], r1[(i + 1) % 8]
    if not (gap and i % 2 == 0):
        return [[a0, b0, b1, a1]]
    lo0, lo1 = axis_point(d0, i, -GATE_HALF), axis_point(d1, i, -GATE_HALF)
    hi0, hi1 = axis_point(d0, i, GATE_HALF), axis_point(d1, i, GATE_HALF)
    return [[a0, lo0, lo1, a1], [hi0, b0, b1, hi1]]


def riser_segs(i, d, gap):
    """Ground segments along ring d on side i, split around the gate gap if asked."""
    r = oct_pts(d)
    a, b = r[i], r[(i + 1) % 8]
    if not (gap and i % 2 == 0):
        return [(a, b)]
    return [(a, axis_point(d, i, -GATE_HALF)), (axis_point(d, i, GATE_HALF), b)]


def build_visuals(m, full, cuts, rng):
    # ---- floor
    for c in full:
        x0, z0 = c["x0"], c["z0"]
        x1, z1 = x0 + CELL, z0 + CELL
        base = SAND_GATE if c["gate"] >= 0 else SAND
        shade = 0.93 + 0.07 * (((x0 // CELL) + (z0 // CELL)) & 1) + rng.uniform(-0.03, 0.03)
        m.add(TEX_FLOOR, [(x0, 0, z0, 0, 0), (x1, 0, z0, CELL, 0), (x1, 0, z1, CELL, CELL), (x0, 0, z1, 0, CELL)],
              base, shade)
    for (x0, z0, far) in cuts:
        x1, z1 = x0 + CELL, z0 + CELL
        corners = [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]
        keep = [p for p in corners if p != far]
        shade = 0.95 + rng.uniform(-0.03, 0.03)
        m.add(TEX_FLOOR, [(p[0], 0, p[1], p[0] - x0, p[1] - z0) for p in keep], SAND, shade)

    # ---- cover
    for r in PILLARS:
        box(m, r[0], r[1], r[2], r[3], PILLAR_H, STONE_PILLAR, STONE_CAP)
    for r in LOWWALLS:
        box(m, r[0], r[1], r[2], r[3], LOWWALL_H, STONE_LOW, STONE_CAP)

    # ---- perimeter wall: inner face, top cap (axis sides open at the gates)
    for i in range(8):
        for (a, b) in riser_segs(i, 0, True):
            vquad(m, TEX_WALL, a, b, 0, WALL_H, STONE_WALL)
        for q in strips(i, 0, WALL_T, True):
            m.add(TEX_FLOOR, [(p[0], WALL_H, p[1], p[0], p[1]) for p in q], STONE_CAP)

    # ---- stands: risers and treads, 12 tiers
    for k in range(TIERS):
        d0, d1 = tier_d(k), tier_d(k + 1)
        h_prev = WALL_H if k == 0 else tier_h(k - 1)
        h = tier_h(k)
        gap = k < GATE_TIERS
        col = STAND_A if k % 2 == 0 else STAND_B
        for i in range(8):
            for (a, b) in riser_segs(i, d0, gap):
                vquad(m, TEX_WALL, a, b, h_prev, h, STAND_RISER)
            for q in strips(i, d0, d1, gap):
                m.add(TEX_FLOOR, [(p[0], h, p[1], p[0], p[1]) for p in q], col)
    # outer face of the top tier, so the stands read as a solid from the back
    for i in range(8):
        for (a, b) in riser_segs(i, tier_d(TIERS), False):
            vquad(m, TEX_WALL, a, b, 0, tier_h(TIERS - 1), STAND_RISER, 0.8)

    # ---- gate corridors: side walls stepped with the stands, and the end door
    for g in range(4):
        i = 2 * g
        for sgn in (-1, 1):
            lat = sgn * GATE_HALF
            spans = [(0, WALL_T, WALL_H)] + [(tier_d(k), tier_d(k + 1), tier_h(k)) for k in range(GATE_TIERS)]
            for (d0, d1, h) in spans:
                vquad(m, TEX_WALL, axis_point(d0, i, lat), axis_point(d1, i, lat), 0, h, STONE_WALL, 0.9)
        end_d = tier_d(GATE_TIERS)
        vquad(m, TEX_WALL, axis_point(end_d, i, -GATE_HALF), axis_point(end_d, i, GATE_HALF),
              0, tier_h(GATE_TIERS - 1), GATE_DOOR)

    # ---- seats (for the crowd): one every 260 along each tread, facing the pit
    for k in range(TIERS):
        d0, d1 = tier_d(k), tier_d(k + 1)
        dm = 0.5 * (d0 + d1)
        h = tier_h(k)
        rm = oct_pts(dm)
        for i in range(8):
            a, b = rm[i], rm[(i + 1) % 8]
            ln = math.hypot(b[0] - a[0], b[1] - a[1])
            n = max(1, int((ln - 200) // SEAT_STEP))
            for s in range(n + 1):
                f = 0.5 if n == 0 else 0.5 + (s - n / 2.0) * (SEAT_STEP / ln)
                px, pz = a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f
                if k < GATE_TIERS and i % 2 == 0 and abs(side_lat(i, (px, pz))) < GATE_HALF + 150:
                    continue
                yaw = math.degrees(math.atan2(px, -pz)) % 360     # facing the origin: dir = (-px, -pz)
                m.seats.append((int(round(px)), int(h), int(round(pz)), int(round(yaw)), k))


# ----------------------------------------------------------- bg emission -----

def tri4_from(tris):
    tris = list(tris) + [(0, 0, 0)] * (4 - len(tris))
    w0, w1 = 0xB1000000, 0
    for k, (a, b, c) in enumerate(tris):
        w0 |= (c & 0xF) << (4 * k)
        w1 |= (a & 0xF) << (8 * k)
        w1 |= (b & 0xF) << (8 * k + 4)
    return gf.gfx(w0, w1)


def build_room(m):
    tex = 32 * 32                       # st units per texture repeat (32 texels)
    lo, hi = gf.ST_MARGIN, tex - 1 - gf.ST_MARGIN
    vbytes = b""
    dl = b""
    dl += gf.gfx(0xE7000000, 0)
    dl += gf.gfx(0xBA001402, 0x00100000)
    dl += gf.gfx(0xB900031D, 0x0C182078)
    dl += gf.gfx(0xFC26A004, 0x1F1093FF)
    dl += gf.gfx(0xBA001001, 0x00010000)
    dl += gf.gfx(0xBB002801, 0xFFFFFFFF)
    dl += gf.gfx(0xC0080002, TEX_FLOOR)
    dl += gf.gfx(0xBA001102, 0)
    dl += gf.gfx(0xBA000C02, 0x00002000)
    dl += gf.gfx(0xB6000000, 0x00003000)
    dl += gf.gfx(0xFB000000, 0x000000FF)

    nv = 0
    for tx in (TEX_FLOOR, TEX_WALL):
        if tx != TEX_FLOOR:
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

        for poly in m.polys[tx]:
            nvp = len(poly)
            ntri = nvp - 2
            if len(bverts) + nvp > 16 or len(btris) + ntri > 4:
                flush()
            base = len(bverts)
            for (x, y, z, u, v, col) in poly:
                s = int(round(lo + (hi - lo) * u / 500.0))
                t = int(round(lo + (hi - lo) * v / 500.0))
                assert -32768 <= s <= 32767 and -32768 <= t <= 32767, (s, t)
                bverts.append(gf.vtx(int(round(x)), int(round(y)), int(round(z)), s, t, col))
            if nvp == 3:
                btris.append((base, base + 1, base + 2))
            else:
                btris.append((base, base + 1, base + 2))
                btris.append((base, base + 2, base + 3))
        flush()
    dl += gf.gfx(0xB8000000, 0)
    assert len(vbytes) % 16 == 0 and len(dl) % 8 == 0
    return vbytes, dl


def gen_bg(m, describe):
    vbytes, dl = build_room(m)
    pt = gf.be_words(gf.deflate_1172(vbytes))
    pri = gf.be_words(gf.deflate_1172(dl))
    out = [gf.HEADER_NOTE % ("bg_colosseum_all_p.c", describe),
           '#include "../../assets/obseg/bg/bg_all_p.h"',
           '#include "bg_colosseum_all_p.h"', "",
           "struct bg_header header = {0, &room_data_table, &portal_data_table, &global_visibility_commands, 0};", "",
           "struct room_data_table_entry room_data_table[] = {",
           "    {0},",
           "    {&point_table_binary_1, &pri_mapping_binary_1, 0, 0.0, 0.0, 0.0},",
           "    {&point_table_binary_2, &pri_mapping_binary_2, &pri_mapping_binary_2, 0.0, 0.0, 0.0},",
           "    {0}", "};", "",
           "u32 global_visibility_commands[] ={0x10000, 0};", "",
           "struct portal_data_table_entry portal_data_table[] = {", "    {0},", "    {0}", "};", "",
           gf.fmt_words("point_table_binary_1", pt), "",
           "u32 point_table_binary_2[] = {\n    0\n};", "",
           gf.fmt_words("pri_mapping_binary_1", pri), "",
           "u32 pri_mapping_binary_2[] = {\n    0, 0, 0, 0\n};", ""]
    hdr = ["#ifndef _BG_COLOSSEUM_ALL_P_H_", "#define _BG_COLOSSEUM_ALL_P_H_", "",
           "#include <ultra64.h>", "",
           "extern u32 point_table_binary_1[];", "extern u32 point_table_binary_2[];",
           "extern u32 pri_mapping_binary_1[];", "extern u32 pri_mapping_binary_2[];",
           "#endif", ""]
    return "\n".join(out), "\n".join(hdr), len(vbytes) // 16, len(dl) // 8


# ------------------------------------------------------------------ main -----

def main():
    ap = argparse.ArgumentParser(description="Generate the Colosseum arena level")
    ap.add_argument("--out", default=None, help="output directory (default port/colosseum)")
    ap.add_argument("--seed", type=int, default=7, help="floor tint noise seed")
    a = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = a.out or os.path.join(root, "port", "colosseum")
    os.makedirs(out, exist_ok=True)

    full, cuts = build_cells()
    tris, first, stand = build_tris(full, cuts)
    m = Mesh()
    build_visuals(m, full, cuts, random.Random(a.seed))
    describe = "pit octagon %d across, %d full cells + %d half cells, 4 gates, %d tiers" % (
        2 * HALF, len(full), len(cuts), TIERS)

    # pads: pit cells first (row-major), then gate cells gate by gate (mouth first)
    pit = [c for c in full if c["gate"] < 0]
    pads = []
    pad_of = {}
    for c in pit:
        k = len(pads)
        pad_of[(c["x0"], c["z0"])] = k
        pads.append((c["x0"] + CELL // 2, c["z0"] + CELL // 2, "p%da1" % (first[(c["x0"], c["z0"])] + 1), -1, 0))
    for g in range(4):
        gc = sorted([c for c in full if c["gate"] == g], key=lambda c: (c["depth"], c["z0"], c["x0"]))
        for c in gc:
            k = len(pads)
            pad_of[(c["x0"], c["z0"])] = k
            pads.append((c["x0"] + CELL // 2, c["z0"] + CELL // 2, "p%da1" % (first[(c["x0"], c["z0"])] + 1), g, c["depth"]))
    adj = {k: [] for k in range(len(pads))}
    for (x0, z0), k in pad_of.items():
        for (dx, dz) in ((0, -CELL), (-CELL, 0), (CELL, 0), (0, CELL)):
            o = pad_of.get((x0 + dx, z0 + dz))
            if o is not None:
                adj[k].append(o)
        # drop A5: diagonal links (when both cells beside the corner are open, so no route cuts a pillar or wall corner): a route
        # over the grid is then octile-shortest, not a Manhattan staircase
        for (dx, dz) in ((-CELL, -CELL), (CELL, -CELL), (-CELL, CELL), (CELL, CELL)):
            o = pad_of.get((x0 + dx, z0 + dz))
            if o is not None and (x0 + dx, z0) in pad_of and (x0, z0 + dz) in pad_of:
                adj[k].append(o)
    start = min(range(len(pads)), key=lambda k: pads[k][0] ** 2 + pads[k][1] ** 2)

    # spectator seats: a pad on each seat that sits well inside a stand tile of its tier, facing the pit; no waypoint
    seat_pads = []
    for (sx, sh, sz, yaw, k) in m.seats:
        for (n, tk, tri) in stand:
            if tk == k and inside_xz((sx, sz), tri):
                nl = math.hypot(sx, sz)
                seat_pads.append((sx, sh, sz, -sx / nl, -sz / nl, "p%da1" % (n + 1), k))
                break
    npit = len(pads)
    setup, ngroups = gen_setup([(p[0], p[1], p[2]) for p in pads], adj, start, describe,
                               [p[:6] for p in seat_pads])
    stan = gen_stan(tris, describe)

    bgc, bgh, nverts, ncmds = gen_bg(m, describe)

    for name, text in (("bg_colosseum_all_p.c", bgc), ("bg_colosseum_all_p.h", bgh),
                       ("Tbg_colosseum_all_p_stanZ.c", stan), ("UsetupcolosseumZ.c", setup)):
        with open(os.path.join(out, name), "w", newline="\n") as f:
            f.write(text)

    gate_names = ["E", "N", "W", "S"]
    layout = {
        "pit": {"half": HALF, "cut": CUT, "diag_sum": DIAG, "cell": CELL, "wall_height": WALL_H},
        "start_pad": start,
        "pads": [{"id": k, "x": p[0], "z": p[1], "gate": p[3], "depth": p[4]} for k, p in enumerate(pads)],
        "gates": [{"name": gate_names[g], "axis": [(1, 0), (0, 1), (-1, 0), (0, -1)][g],
                   "mouth": [(HALF, 0), (0, HALF), (-HALF, 0), (0, -HALF)][g],
                   "pads": [k for k, p in enumerate(pads) if p[3] == g]} for g in range(4)],
        "pillars": PILLARS, "low_walls": LOWWALLS,
        "seat_pads": [{"id": npit + j, "x": p[0], "y": p[1], "z": p[2], "tier": p[6]} for j, p in enumerate(seat_pads)],
    }
    with open(os.path.join(out, "colosseum_layout.json"), "w", newline="\n") as f:
        json.dump(layout, f, separators=(",", ":"))

    print("colosseum: %s; %d tiles, %d pads (start %d), %d groups, %d verts, %d dl cmds, %d seat pads (of %d seats; ids %d..), out=%s" % (
        describe, len(tris), len(pads), start, ngroups, nverts, ncmds, len(seat_pads), len(m.seats), npit, out))


if __name__ == "__main__":
    main()

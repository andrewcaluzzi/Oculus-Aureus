#!/usr/bin/env python3
"""SEVERNAYA SNOWFIELD: a prototype sniper map, one long open rolling snowfield (a yay/nay spike after AC's ridge feedback).

    python3 tools/levelgen/build_snowfield.py            -> port/levelgen_snowfield/ (build with -DGE_LEVELGEN_EXTRA=port/levelgen_snowfield
                                                             -DGE_LEVELGEN_EXTRA_NAME=snowfield; GE_LEVELGEN=snowfield picks it)

Not a gen_level.py layout: the ground is a HEIGHTFIELD, one bg room, written here with gen_level.py's own writers (the display list
batches, the deflate, the setup). One height function h(x, z) on one CELL grid gives both the drawn triangles and the stan tiles (the
same corners, the same diagonal), so what Bond sees of the ground and what the walk / the stan line use are the same surface.
- the field: 200 m wide (x) and 340 m long (z) of play, Bond's shoulder at the south end, a shallow basin, two long ridgelines and a few
  hummocks across the sight line (cover, dead ground), the hunters' rise at the north end; gentle slopes everywhere in the play area;
- the perimeter: the ground rises (a walkable apron) into rock cliffs; the stan ends at the cliff foot, so nobody walks off the map; on
  the crest a ring of Surface's own mountain skyline (its four stacked translucent strips, drawn in the room's second display list with
  Surface's own render state) stands against the sky;
- textures by number (Surface's snow 1264, rock 1263 on the steep faces, the skyline strips 1198..1201): the texels are read from the
  ROM at run time, nothing ROM-derived is written;
- the sky and fog are Surface's own record from the ROM's table (bgfog.c, the levelgen slot when the level is "snowfield"), with the far
  plane and the fog band moved out for the long sight lines.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import gen_level as gl      # noqa: E402
import gen_flatland as gf   # noqa: E402

NAME = "levelgen"
OUT = os.path.join(ROOT, "port", "levelgen_snowfield")

CELL = 500                      # one grid for the drawn ground and the stan
X0, X1, Z0, Z1 = -18000, 18000, -25000, 25000
PW, PL, RC = 10000, 17000, 6000  # the play area's half width (x), half length (z), its corner radius
APRON = 2600                    # the walkable rise past the play area (the stan ends here: the cliff foot)
CLIFF = 3200                    # the cliff's horizontal run
RING = APRON + CLIFF + 200      # the skyline ring
DRAW = RING + 900               # nothing is drawn past this
BASE = 600                      # every height is above this much (perch pads need y >= 60)

SNOW, ROCK = 1264, 1263
SKY_STRIPS = [1201, 1200, 1199, 1198]  # bottom to top (Surface's stacking)
K_SNOW = 0.064 * 32.0           # ST units per world unit (32 per texel), the ridge's density
K_ROCK = 0.035 * 32.0
BAND = 900                      # one skyline strip's height
SEG = 2600                      # one skyline image's width


def g(v, c, s):
    return math.exp(-((v - c) / float(s)) ** 2)


def seg_dist(x, z, a, b):
    ax, az = a
    bx, bz = b
    dx, dz = bx - ax, bz - az
    t = max(0.0, min(1.0, ((x - ax) * dx + (z - az) * dz) / float(dx * dx + dz * dz)))
    return math.hypot(x - (ax + t * dx), z - (az + t * dz)), t


def sdf_plain(x, z):
    """distance outside the rounded play rectangle (negative inside)"""
    qx, qz = abs(x) - (PW - RC), abs(z) - (PL - RC)
    return math.hypot(max(qx, 0.0), max(qz, 0.0)) + min(max(qx, qz), 0.0) - RC


def sdf(x, z):
    """the same with a natural wobble in the boundary"""
    wob = 900.0 * math.sin(x / 2300.0 + 1.3) * math.cos(z / 3100.0) + 600.0 * math.sin(z / 1700.0 + x / 4100.0)
    return sdf_plain(x, z) + wob


RIDGES = [  # (a, b, height, half width): long ridgelines across the sight line
    ((-9500, -7600), (7200, -4000), 650, 1300),
    ((-4200, 7000), (9500, 2400), 850, 1600),
    ((-9200, 1400), (-3400, 3600), 450, 1100),
]
HUMPS = [   # (x, z, height, sigma)
    (6500, -10000, 550, 1900), (-6500, 11000, 750, 2400), (3000, 12600, 420, 1300), (-2200, -11400, 300, 1500),
    (-1000, 500, -450, 5000), (7800, 9000, 380, 1800),
]


def field(x, z):
    y = 900.0 * g(z, -14800, 3500) + 1300.0 * g(z, 15600, 4500) - 300.0 * g(z, -1500, 6500)
    for a, b, hh, w in RIDGES:
        d, t = seg_dist(x, z, a, b)
        y += hh * math.exp(-(d / float(w)) ** 2) * (0.35 + 0.65 * math.sin(math.pi * t))
    for hx, hz, hh, s in HUMPS:
        y += hh * g(x, hx, s) * g(z, hz, s)
    y += 110.0 * math.sin(x / 1900.0 + 0.4) * math.sin(z / 2300.0 + 0.7) + 70.0 * math.sin((x + 1.3 * z) / 1400.0)
    return y


def height(x, z):
    y = BASE + 450 + field(x, z)
    s = sdf(x, z)
    if s > 0:
        y += 0.16 * min(s, APRON)
    if s > APRON:
        c = min(1.0, (s - APRON) / float(CLIFF))
        sm = c * c * (3 - 2 * c)
        y += 2800.0 * sm * (0.75 + 0.25 * math.sin(x / 2100.0) * math.cos(z / 1700.0)) + 220.0 * math.sin(x / 700.0) * math.sin(z / 900.0) * c
        if s > APRON + CLIFF:
            y += 0.12 * (s - APRON - CLIFF)
    return y


# ------------------------------------------------------------------- grid ---
NX, NZ = (X1 - X0) // CELL, (Z1 - Z0) // CELL
XS = [X0 + CELL * i for i in range(NX + 1)]
ZS = [Z0 + CELL * j for j in range(NZ + 1)]
HG = [[int(round(height(x, z))) for x in XS] for z in ZS]     # HG[j][i]


def H(i, j):
    return HG[j][i]


def cell_centre(i, j):
    return XS[i] + CELL / 2.0, ZS[j] + CELL / 2.0


def is_stan(i, j):
    if not (0 <= i < NX and 0 <= j < NZ):
        return False
    return sdf(*cell_centre(i, j)) <= APRON


def is_drawn(i, j):
    return min(sdf(XS[a], ZS[b]) for a in (i, i + 1) for b in (j, j + 1)) <= DRAW


def P(i, j):
    return (XS[i], H(i, j), ZS[j])


def cell_tris_raw(i, j):
    """the two triangles of a cell, diagonal (x0,z0)-(x1,z1): the drawn order (gl.cell_tris winds the same corners for the stan)"""
    return (P(i, j), P(i + 1, j + 1), P(i + 1, j)), (P(i, j), P(i, j + 1), P(i + 1, j + 1))


def normal(t):
    a, b, c = t
    u = [b[k] - a[k] for k in range(3)]
    v = [c[k] - a[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    ln = math.sqrt(sum(q * q for q in n)) or 1.0
    n = tuple(q / ln for q in n)
    return n if n[1] >= 0 else tuple(-q for q in n)


SUN = (0.55, 0.45, -0.70)
_sl = math.sqrt(sum(q * q for q in SUN))
SUN = tuple(q / _sl for q in SUN)
TINT = (222, 230, 250)


def vnormal(i, j):
    i0, i1 = max(0, i - 1), min(NX, i + 1)
    j0, j1 = max(0, j - 1), min(NZ, j + 1)
    dx = (H(i1, j) - H(i0, j)) / float(XS[i1] - XS[i0])
    dz = (H(i, j1) - H(i, j0)) / float(ZS[j1] - ZS[j0])
    ln = math.sqrt(dx * dx + 1 + dz * dz)
    return (-dx / ln, 1 / ln, -dz / ln)


VCOL = {}


def vcol(i, j):
    k = (i, j)
    if k not in VCOL:
        n = vnormal(i, j)
        d = max(0.0, n[0] * SUN[0] + n[1] * SUN[1] + n[2] * SUN[2])
        b = 0.50 + 0.62 * d
        VCOL[k] = tuple(max(0, min(255, int(round(c * b)))) for c in TINT)
    return VCOL[k]


def idx_of(p):
    return (p[0] - X0) // CELL, (p[2] - Z0) // CELL


def tri_tex(t):
    n = normal(t)
    return SNOW if n[1] > 0.66 else ROCK, n


def st_of(p, tex, n):
    x, y, z = p
    if tex == SNOW or abs(n[1]) > 0.8:
        k = K_SNOW if tex == SNOW else K_ROCK
        return x * k, z * k
    k = K_ROCK   # the rock's strata: slanted and waved with the position, so the bands are not ruler-straight
    u = z if abs(n[0]) > abs(n[2]) else x
    return u * k, (-y + 0.2 * u + 700.0 * math.sin(x / 2600.0 + z / 3900.0)) * k


def build_ground(mesh):
    drawn = 0
    for j in range(NZ):
        for i in range(NX):
            if not is_drawn(i, j):
                continue
            drawn += 1
            t1, t2 = cell_tris_raw(i, j)
            (x1, n1), (x2, n2) = tri_tex(t1), tri_tex(t2)
            if x1 == x2 == SNOW:
                quad = [P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1)]   # fan from (x0,z0): the same diagonal as the stan
                mesh.add(SNOW, [(p[0], p[1], p[2]) + st_of(p, SNOW, (0, 1, 0)) + (vcol(*idx_of(p)),) for p in quad])
                continue
            for t, tex, n in ((t1, x1, n1), (t2, x2, n2)):
                mesh.add(tex, [(p[0], p[1], p[2]) + st_of(p, tex, n) + (vcol(*idx_of(p)),) for p in t])
    return drawn


def ring_points():
    """the skyline ring: the play rectangle offset by RING (rounded corners), points about SEG apart, counter-clockwise seen from above
    (+x, +z): walking it, the outside is on the walker's right"""
    hw, hl, r = PW - RC, PL - RC, RC + RING
    pts = []
    # corners at (+hw,+hl) (+hw..), go round: east side z from -hl to +hl, then north-east corner, north side x from +hw to -hw, ...
    def arc(cx, cz, a0, a1):
        n = max(2, int(math.ceil(abs(a1 - a0) * r / SEG)))
        for k in range(n):
            a = a0 + (a1 - a0) * k / float(n)
            pts.append((cx + r * math.cos(a), cz + r * math.sin(a)))

    def line(ax, az, bx, bz):
        ln = math.hypot(bx - ax, bz - az)
        n = max(1, int(round(ln / SEG)))
        for k in range(n):
            pts.append((ax + (bx - ax) * k / float(n), az + (bz - az) * k / float(n)))
    line(hw + r, -hl, hw + r, hl)
    arc(hw, hl, 0.0, math.pi / 2)
    line(hw, hl + r, -hw, hl + r)
    arc(-hw, hl, math.pi / 2, math.pi)
    line(-hw - r, hl, -hw - r, -hl)
    arc(-hw, -hl, math.pi, 1.5 * math.pi)
    line(-hw, -hl - r, hw, -hl - r)
    arc(hw, -hl, 1.5 * math.pi, 2 * math.pi)
    return pts


def build_skyline(mesh):
    pts = ring_points()
    n = len(pts)
    white = (230, 230, 230)
    quads = 0
    for k in range(n):
        a, b = pts[k], pts[(k + 1) % n]
        ya, yb = height(*a) - 500.0, height(*b) - 500.0
        # Surface's strips: one image (s 0..2048, 64 texels) a segment, s growing to the right of a viewer inside looking out; t 0 at a
        # strip's foot to 512 at its top (16 texels a strip)
        for q, tex in enumerate(SKY_STRIPS):
            a0, a1 = ya + q * BAND, ya + (q + 1) * BAND
            b0, b1 = yb + q * BAND, yb + (q + 1) * BAND
            mesh.add(tex, [(a[0], a1, a[1], 0, 512, white), (b[0], b1, b[1], 2048, 512, white),
                           (b[0], b0, b[1], 2048, 0, white), (a[0], a0, a[1], 0, 0, white)])
            quads += 1
    return quads, n


# ------------------------------------------------------------------- bg ---
PRI_HEAD = [(0xE7000000, 0), (0xBA001402, 0x00100000), (0xB900031D, 0x0C182078), (0xFC26A004, 0x1F1093FF), (0xBA001001, 0x00010000),
            (0xBB002801, 0xFFFFFFFF)]
PRI_MID = [(0xBA001102, 0), (0xBA000C02, 0x00002000), (0xB6000000, 0x00003000), (0xFB000000, 0x000000FF)]
# Surface's own translucent skyline state (its sec display lists): alpha-blended render mode, its combiner and texture command
SEC_HEAD = [(0xE7000000, 0), (0xBA001402, 0x00100000), (0xB900031D, 0x0C1849D8), (0xFC26E404, 0x1F10FFFF), (0xBA001001, 0x00010000),
            (0xBB002801, 0xFFFFFFFF)]
SEC_MID = [(0xBA001102, 0x00040000), (0xBA000C02, 0x00002000), (0xB6000000, 0x00003000), (0xFB000000, 0x000000FF)]


def pri_texcmd(tex):
    return (0xC0080002, tex)


def sec_texcmd(tex):
    return (0xC01B8001, 0xFA9A1000 | tex)


def dl_of(mesh, nv0, head, mid, texcmd):
    vbytes, dl = b"", b""
    texs = sorted(mesh.polys)
    for w in head:
        dl += gf.gfx(*w)
    nv = nv0
    for ti, tx in enumerate(texs):
        dl += gf.gfx(*texcmd(tx))
        if ti == 0:
            for w in mid:
                dl += gf.gfx(*w)
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
            dl += gl.tri4_from(btris)
            bverts, btris = [], []

        for poly in mesh.polys[tx]:
            nvp = len(poly)
            if len(bverts) + nvp > 16 or len(btris) + nvp - 2 > 4:
                flush()
            base = len(bverts)
            s0 = min(p[3] for p in poly)
            t0 = min(p[4] for p in poly)
            ds = gl.ST_ALIGN * math.floor(s0 / gl.ST_ALIGN)
            dt = gl.ST_ALIGN * math.floor(t0 / gl.ST_ALIGN)
            for (x, y, z, s, t, col) in poly:
                si, tj = int(round(s - ds)), int(round(t - dt))
                assert -32768 <= si <= 32767 and -32768 <= tj <= 32767, (si, tj)
                bverts.append(gf.vtx(int(round(x)), int(round(y)), int(round(z)), si, tj, tuple(col) + (0xFF,)))
            for k in range(1, nvp - 1):
                btris.append((base, base + k, base + k + 1))
        flush()
    dl += gf.gfx(0xB8000000, 0)
    return vbytes, dl, nv


def gen_bg(describe, ground, sky):
    vb1, pri, nv = dl_of(ground, 0, PRI_HEAD, PRI_MID, pri_texcmd)
    vb2, sec, nv2 = dl_of(sky, nv, SEC_HEAD, SEC_MID, sec_texcmd)
    vb = vb1 + vb2
    out = [gl.HEADER_NOTE.replace("gen_level.py", "levelgen/build_snowfield.py") % ("bg_%s_all_p.c" % NAME, describe),
           '#include "../../assets/obseg/bg/bg_all_p.h"', '#include "bg_%s_all_p.h"' % NAME, "",
           "struct bg_header header = {0, &room_data_table, &portal_data_table, &global_visibility_commands, 0};", "",
           "struct room_data_table_entry room_data_table[] = {", "    {0},",
           "    {&point_table_binary_1, &pri_mapping_binary_1, &sec_mapping_binary_1, 0.0, 0.0, 0.0},   /* field */",
           "    {&point_table_binary_end, &pri_mapping_binary_end, &pri_mapping_binary_end, 0.0, 0.0, 0.0},",
           "    {0}", "};", "",
           "u32 global_visibility_commands[] ={0x10000, 0};", "",
           "struct portal_data_table_entry portal_data_table[] = {", "    {0}", "};", "",
           gf.fmt_words("point_table_binary_1", gf.be_words(gf.deflate_1172(vb))), "",
           "u32 point_table_binary_end[] = {\n    0\n};", "",
           gf.fmt_words("pri_mapping_binary_1", gf.be_words(gf.deflate_1172(pri))), "",
           gf.fmt_words("sec_mapping_binary_1", gf.be_words(gf.deflate_1172(sec))), "",
           "u32 pri_mapping_binary_end[] = {\n    0, 0, 0, 0\n};", ""]
    guard = "_BG_%s_ALL_P_H_" % NAME.upper()
    hdr = ["#ifndef " + guard, "#define " + guard, "", "#include <ultra64.h>", "",
           "extern u32 point_table_binary_1[];", "extern u32 point_table_binary_end[];", "extern u32 pri_mapping_binary_1[];",
           "extern u32 sec_mapping_binary_1[];", "extern u32 pri_mapping_binary_end[];", "#endif", ""]
    return "\n".join(out), "\n".join(hdr), (len(vb) // 16, len(pri) // 8, len(sec) // 8, len(vb) + len(pri) + len(sec))


# ------------------------------------------------------------------ stan ---
def build_stan():
    tris, index = [], {}
    for j in range(NZ):
        for i in range(NX):
            if not is_stan(i, j):
                continue
            Y = (lambda x, z: HG[(z - Z0) // CELL][(x - X0) // CELL])
            for sub, t in enumerate(gl.cell_tris(XS[i], ZS[j], XS[i + 1], ZS[j + 1], Y)):
                index[(i, j, sub)] = len(tris)
                tris.append((1, t))
    return tris, index


def gen_stan(describe, tris):
    edges = gl.stan_edges(tris)
    for v in edges.values():
        assert len(v) <= 2

    def link(n, k):
        t = tris[n][1]
        v = edges[frozenset((t[k], t[(k + 1) % 3]))]
        return 0 if len(v) == 1 else gl.LINK_BASE + 4 * (v[0] if v[1] == n else v[1])

    sym = "Tbg_%s_all_p_stanZ" % NAME
    out = [gl.HEADER_NOTE.replace("gen_level.py", "levelgen/build_snowfield.py") % (sym + ".c", describe),
           "#include <ultra64.h>", "#include <game/stan.h>", "", "// forward declarations", "StandTile tile_0;", "",
           "StandFileHeader %s = {" % sym, "    NULL,", "    &tile_0,", "    {0x00, 0x00, 0x00, 0x00}", "};", ""]
    c = 14   # the tiles' colour (chr light), as the ridge's
    for n, (room, t) in enumerate(tris):   # one line a tile (the 2 MB file limit)
        out.append("StandTile tile_%d = {0x%06x, 0x%02x, 0x0, 0x%x, 0x%x, 0x%x, 3, 0x0, 0x1, 0x2, {%s}};" % (
            n, ((n + 1) << 8) | 1, room, c, c, c, ", ".join("{%d, %d, %d, 0x%04x}" % (t[k][0], t[k][1], t[k][2], link(n, k)) for k in range(3))))
    out.append("StandTile tile_%d = {0x000000, 0x00, 0x0, 0x0, 0x0, 0x0, 0, 0x0, 0x0, 0x0};" % len(tris))
    out.append("")
    out.append('StandFileFooter footer = {\n    "unstric",\n    NULL,\n    NULL,\n    NULL,\n    NULL\n};')
    out.append("")
    return "\n".join(out), sum(1 for v in edges.values() if len(v) == 2)


# ------------------------------------------------------------------ pads ---
class Room:
    pass


FIELD = Room()
FIELD.name, FIELD.id = "field", 1


def tri_y(t, x, z):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
    det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
    l1 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / float(det)
    l2 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / float(det)
    return l1 * ay + l2 * by + (1 - l1 - l2) * cy


def tile_for(tris, index, x0, z0):
    """the stan tile well inside which (x, z) is (nudged off a cell edge or the diagonal if it sits on one)"""
    for dx, dz in ((0, 0), (37, 13), (-29, 41), (43, -31), (-41, -17)):
        x, z = x0 + dx, z0 + dz
        i, j = int((x - X0) // CELL), int((z - Z0) // CELL)
        for sub in (0, 1):
            n = index.get((i, j, sub))
            if n is not None and gl.inside_xz((x, z), tris[n][1], 6.0):
                return n, x, z
    return None, x0, z0


def build_pads(tris, index, named, start_name):
    pads = []

    def add(x, z, kind, name=None, look=(0.0, 1.0)):
        n, x, z = tile_for(tris, index, x, z)
        if n is None:
            sys.exit("pad %s at (%d, %d): not on the stan" % (name or kind, x, z))
        p = gl.Pad()
        p.id, p.room, p.x, p.z, p.tile, p.kind, p.name = len(pads), FIELD, int(round(x)), int(round(z)), n, kind, name
        p.y = int(round(tri_y(tris[n][1], p.x, p.z)))
        ln = math.hypot(look[0], look[1]) or 1.0
        p.look = (look[0] / ln, look[1] / ln)
        pads.append(p)
        return p

    grid = {}
    for j in range(1, NZ, 3):
        for i in range(1, NX, 3):
            if is_stan(i, j) and all(is_stan(i + a, j + b) for a in (-1, 0, 1) for b in (-1, 0, 1)):
                x, z = cell_centre(i, j)
                grid[(i, j)] = add(x + 60, z - 110, "grid")
    for nm, (x, z), face in named:
        add(x, z, "named", nm, (face[0] - x, face[1] - z))
    st =[n for n in named if n[0] == start_name][0]
    start = add(st[1][0], st[1][1], "start", "start", (st[2][0] - st[1][0], st[2][1] - st[1][1]))
    return pads, grid, start


def seg_on_stan(p, q):
    n = max(2, int(math.hypot(q.x - p.x, q.z - p.z) // 150))
    for k in range(n + 1):
        x = p.x + (q.x - p.x) * k / float(n)
        z = p.z + (q.z - p.z) * k / float(n)
        if not is_stan(int((x - X0) // CELL), int((z - Z0) // CELL)):
            return False
    return True


def build_waypoints(pads, grid):
    adj = {p.id: set() for p in pads}
    for (i, j), p in grid.items():
        for a, b in ((3, 0), (0, 3), (3, 3), (3, -3)):
            q = grid.get((i + a, j + b))
            if q and seg_on_stan(p, q):
                adj[p.id].add(q.id)
                adj[q.id].add(p.id)
    gp = list(grid.values())
    for p in pads:
        if p.kind == "grid":
            continue
        near = sorted(gp, key=lambda q: math.hypot(q.x - p.x, q.z - p.z))[:3]
        for q in near:
            if seg_on_stan(p, q):
                adj[p.id].add(q.id)
                adj[q.id].add(p.id)
        if not adj[p.id]:
            sys.exit("pad %s has no waypoint link" % p.name)
    seen, todo = {pads[0].id}, [pads[0].id]
    while todo:
        u = todo.pop()
        for v in adj[u]:
            if v not in seen:
                seen.add(v)
                todo.append(v)
    if len(seen) != len(pads):
        sys.exit("waypoints: %d pads not connected" % (len(pads) - len(seen)))
    group = {p.id: 0 for p in pads}
    return adj, group, {0: set()}, {0: [p.id for p in pads]}


def gen_lua(pads, start, grid):
    o = ["-- Generated by tools/levelgen/build_snowfield.py - do not edit; rerun the script.",
         "-- SEVERNAYA SNOWFIELD for mode scripts: include(\"levelgen_layout_snowfield\") (modding/levelgen.md).",
         "LEVEL = {", '  name = "levelgen",', "  variant = \"snowfield\",", "  start_pad = %d," % start.id, "  rooms = {",
         '    ["field"] = { id = 1, rect = { %d, %d, %d, %d }, floor = %d, height = 8000, pads = { %s }, named = { %s } },' % (
             -PW, -PL, PW, PL, BASE, ", ".join(str(p.id) for p in grid.values()),
             ", ".join('["%s"] = %d' % (p.name, p.id) for p in pads if p.kind == "named")),
         "  },", '  room_name = { [1] = "field" },', "  doors = {},",
         "  pads = { %s }," % ", ".join('["%s"] = %d' % (p.name, p.id) for p in pads if p.name),
         "  terrain = { cell = %d, play = { %d, %d, %d, %d }, apron = %d },"
         % (CELL, -PW, -PL, PW, PL, APRON),
         "}", ""]
    return "\n".join(o)


# Named pads. Bond's shoulder (south, z about -14800, high ground), the midfield (behind the ridgelines' crests, 100 to 200 m), the
# hunters' rise (north, 250 to 300 m). Each faces the other end.
SOUTH, NORTH = (0, -15000), (0, 15000)
NAMED = (
    [("b%d" % (k + 1), (x, -14600), NORTH) for k, x in enumerate((0, -2500, 2500, -5200, 5200))] +
    [("m%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((-6000, -5600), (-1500, -4600), (4800, -4500), (-6800, 2200), (1500, 4900),
                                                           (6600, 3300), (-3000, 6800), (8000, 8800)))] +
    [("f%d" % (k + 1), xz, SOUTH) for k, xz in enumerate(((-7000, 11200), (-4200, 12400), (-1000, 13600), (2800, 12700), (5600, 13400),
                                                           (8000, 12000), (-2600, 15600), (2000, 15800)))]
)


def main():
    os.makedirs(OUT, exist_ok=True)
    describe = "SEVERNAYA SNOWFIELD: heightfield %d x %d m (play %d x %d m), cell %d" % (
        (X1 - X0) // 100, (Z1 - Z0) // 100, 2 * PW // 100, 2 * PL // 100, CELL)
    ground, sky = gl.Mesh(), gl.Mesh()
    drawn = build_ground(ground)
    quads, nseg = build_skyline(sky)
    tris, index = build_stan()
    if len(tris) > gl.MAX_TILES:
        sys.exit("%d stan tiles > %d" % (len(tris), gl.MAX_TILES))
    pads, grid, start = build_pads(tris, index, NAMED, "b1")
    adj, group, gadj, members = build_waypoints(pads, grid)
    bgc, bgh, (nv, npri, nsec, nbytes) = gen_bg(describe, ground, sky)
    stan, nlinks = gen_stan(describe, tris)
    setup = gl.gen_setup(NAME, pads, start, adj, group, gadj, members, describe, None)
    setup = setup.replace("tools/gen_level.py", "tools/levelgen/build_snowfield.py")
    lua = gen_lua(pads, start, grid)
    for fn, text in (("bg_%s_all_p.c" % NAME, bgc), ("bg_%s_all_p.h" % NAME, bgh), ("Tbg_%s_all_p_stanZ.c" % NAME, stan),
                     ("Usetup%sZ.c" % NAME, setup), ("levelgen_layout.lua", lua)):
        with open(os.path.join(OUT, fn), "w", newline="\n") as f:
            f.write(text)
    ys = [y for row in HG for y in row]
    print(describe)
    print("  drawn cells %d, stan tiles %d (%d links), skyline %d quads on %d segments" % (drawn, len(tris), nlinks, quads, nseg))
    print("  bg: %d vertices, pri %d cmds, sec %d cmds, %d bytes uncompressed" % (nv, npri, nsec, nbytes))
    print("  pads %d (grid %d), start %d at (%d, %d, %d); heights %d..%d" % (len(pads), len(grid), start.id, start.x, start.y, start.z, min(ys), max(ys)))
    for p in pads:
        if p.kind != "grid":
            print("   %-5s pad %3d (%6d, %5d, %6d)  %.0f m from b1" % (p.name, p.id, p.x, p.y, p.z, math.hypot(p.x - start.x, p.z - start.z) / 100))
    for fn in os.listdir(OUT):
        print("  %-28s %8d bytes" % (fn, os.path.getsize(os.path.join(OUT, fn))))


if __name__ == "__main__":
    main()

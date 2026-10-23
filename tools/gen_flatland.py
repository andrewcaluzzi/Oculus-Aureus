#!/usr/bin/env python3
"""
gen_flatland.py - generate "Flatland", a flat square test level.

Writes three C sources in the same shape the asset extractor produces from the
ROM (assets/obseg/bg, stan, setup), into port/flatland/:

  bg_flatland_all_p.c       one bg room: a textured floor and a low perimeter
                            wall (room data is 1172 = raw deflate, big-endian
                            u32 words, like bg_len_all_p.c)
  Tbg_flatland_all_p_stanZ.c  floor tiles (two triangles per cell)
  UsetupflatlandZ.c         Bond start pad, a pad grid with a waypoint graph
                            (no objects, guards or objectives)

CMake compiles them into the exe (see CMakeLists.txt "Flatland"), because the
ROM CRC gate serves stock data only. The level takes over the cut MP stage slot
LEVELID_SHO. Regenerate with:

    python3 tools/gen_flatland.py            # defaults: 6000 x 6000
    python3 tools/gen_flatland.py --size 8000 --pad-spacing 500 --wall-height 120

Units: levelscale for the slot is 1.0, so bg units, stan units and world units
are all the same. The floor is at y = 0, centred on the origin.
"""

import argparse
import os
import struct
import sys
import zlib

# ---------------------------------------------------------------- bg --------

VTX_BATCH = 16            # vertices per G_VTX (the vertex cache is 16 wide)
ST_MARGIN = 8             # same half-texel margin the stock rooms use (8 .. 1015)


def deflate_1172(data: bytes) -> bytes:
    """Rare '1172' container: two magic bytes then a raw deflate stream."""
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return b"\x11\x72" + c.compress(data) + c.flush()


def be_words(data: bytes):
    data = data + b"\x00" * (-len(data) % 16)   # 16-aligned like the stock arrays
    return [struct.unpack(">I", data[i:i + 4])[0] for i in range(0, len(data), 4)]


def fmt_words(name, words, per_line=5):
    lines = ["u32 %s[] = {" % name]
    for i in range(0, len(words), per_line):
        chunk = ",".join("0x%08X" % w for w in words[i:i + per_line])
        lines.append("    " + chunk + ("," if i + per_line < len(words) else ""))
    lines.append("};")
    return "\n".join(lines)


def vtx(x, y, z, s, t, rgba):
    # Vtx (big-endian image): x y z flag s t, then r g b a
    return struct.pack(">hhhHhh", x, y, z, 0, s, t) + bytes(rgba)


def gfx(w0, w1):
    return struct.pack(">II", w0 & 0xFFFFFFFF, w1 & 0xFFFFFFFF)


def tri4(quads):
    """G_TRI4 for up to two quads (vertex nibbles 0..15 of the current batch).
    A quad (a,b,c,d) becomes triangles (a,b,c) and (a,c,d); unused slots are 0."""
    tris = []
    for (a, b, c, d) in quads:
        tris.append((a, b, c))
        tris.append((a, c, d))
    while len(tris) < 4:
        tris.append((0, 0, 0))
    w0 = 0xB1000000
    w1 = 0
    for k, (a, b, c) in enumerate(tris):
        w0 |= (c & 0xF) << (4 * k)
        w1 |= (a & 0xF) << (8 * k)
        w1 |= (b & 0xF) << (8 * k + 4)
    return gfx(w0, w1)


def build_room(p):
    """Returns (vertex_bytes, dl_bytes, room_bounds)."""
    hx, hz = p.nx * p.cell // 2, p.nz * p.cell // 2
    floor_col = (0xD8, 0xD8, 0xD8, 0xFF)
    wall_col = (0xB0, 0xB0, 0xB8, 0xFF)
    tex = p.tex_texels * 32          # st units per texture repeat
    lo, hi = ST_MARGIN, tex - 1 - ST_MARGIN

    # -- floor quads: one per cell, one full texture repeat each
    floor = []
    for j in range(p.nz):
        for i in range(p.nx):
            x0, x1 = -hx + i * p.cell, -hx + (i + 1) * p.cell
            z0, z1 = -hz + j * p.cell, -hz + (j + 1) * p.cell
            floor.append([
                vtx(x0, 0, z0, lo, lo, floor_col),
                vtx(x1, 0, z0, hi, lo, floor_col),
                vtx(x1, 0, z1, hi, hi, floor_col),
                vtx(x0, 0, z1, lo, hi, floor_col),
            ])

    # -- wall quads: along the four edges, one per cell; the texture repeats
    #    horizontally in proportion to the quad's aspect so it is not stretched
    walls = []
    H = p.wall_height
    reps = max(1, round(p.cell / float(H)))
    whi = tex * reps - 1 - ST_MARGIN

    def wall(ax, az, bx, bz):
        return [
            vtx(ax, H, az, lo, lo, wall_col),
            vtx(bx, H, bz, whi, lo, wall_col),
            vtx(bx, 0, bz, whi, hi, wall_col),
            vtx(ax, 0, az, lo, hi, wall_col),
        ]

    if H > 0:
        for i in range(p.nx):
            x0, x1 = -hx + i * p.cell, -hx + (i + 1) * p.cell
            walls.append(wall(x0, -hz, x1, -hz))
            walls.append(wall(x1, hz, x0, hz))
        for j in range(p.nz):
            z0, z1 = -hz + j * p.cell, -hz + (j + 1) * p.cell
            walls.append(wall(hx, z0, hx, z1))
            walls.append(wall(-hx, z1, -hx, z0))

    vbytes = b""
    dl = b""

    def tex_cmd(texnum):
        # G_NOOP-with-texture: type 2 (mipmap), wrap/wrap, tile offset 2
        return gfx(0xC0080002, texnum)

    # state prefix, same as the stock outdoor rooms (Surface/Runway); culling is
    # cleared so winding does not matter
    dl += gfx(0xE7000000, 0)
    dl += gfx(0xBA001402, 0x00100000)
    dl += gfx(0xB900031D, 0x0C182078)
    dl += gfx(0xFC26A004, 0x1F1093FF)
    dl += gfx(0xBA001001, 0x00010000)
    dl += gfx(0xBB002801, 0xFFFFFFFF)
    dl += tex_cmd(p.floor_tex)
    dl += gfx(0xBA001102, 0)
    dl += gfx(0xBA000C02, 0x00002000)
    dl += gfx(0xB6000000, 0x00003000)
    dl += gfx(0xFB000000, 0x000000FF)

    nv = 0   # vertices emitted so far

    def emit(quads):
        nonlocal vbytes, dl, nv
        per = VTX_BATCH // 4
        for k in range(0, len(quads), per):
            group = quads[k:k + per]
            n = len(group) * 4
            for q in group:
                for v in q:
                    vbytes += v
            dl += gfx(0x04000000 | ((n - 1) << 20) | (n * 16), 0x0E000000 + nv * 16)
            nv += n
            idx = [(4 * m, 4 * m + 1, 4 * m + 2, 4 * m + 3) for m in range(len(group))]
            for t in range(0, len(idx), 2):
                dl += tri4(idx[t:t + 2])

    emit(floor)
    if walls:
        dl += tex_cmd(p.wall_tex)
        emit(walls)
    dl += gfx(0xB8000000, 0)

    assert len(vbytes) % 16 == 0 and len(dl) % 8 == 0
    return vbytes, dl


def gen_bg(p):
    vbytes, dl = build_room(p)
    pt = be_words(deflate_1172(vbytes))
    pri = be_words(deflate_1172(dl))
    out = []
    out.append(HEADER_NOTE % ("bg_flatland_all_p.c", p.describe()))
    out.append('#include "../../assets/obseg/bg/bg_all_p.h"')
    out.append('#include "bg_flatland_all_p.h"')
    out.append("")
    out.append("struct bg_header header = {0, &room_data_table, &portal_data_table, &global_visibility_commands, 0};")
    out.append("")
    out.append("struct room_data_table_entry room_data_table[] = {")
    out.append("    {0},")
    out.append("    {&point_table_binary_1, &pri_mapping_binary_1, 0, 0.0, 0.0, 0.0},")
    out.append("    {&point_table_binary_2, &pri_mapping_binary_2, &pri_mapping_binary_2, 0.0, 0.0, 0.0},")
    out.append("    {0}")
    out.append("};")
    out.append("")
    out.append("u32 global_visibility_commands[] ={0x10000, 0};")
    out.append("")
    out.append("struct portal_data_table_entry portal_data_table[] = {")
    out.append("    {0},")
    out.append("    {0}")
    out.append("};")
    out.append("")
    out.append(fmt_words("point_table_binary_1", pt))
    out.append("")
    out.append("u32 point_table_binary_2[] = {\n    0\n};")
    out.append("")
    out.append(fmt_words("pri_mapping_binary_1", pri))
    out.append("")
    out.append("u32 pri_mapping_binary_2[] = {\n    0, 0, 0, 0\n};")
    out.append("")
    hdr = ["#ifndef _BG_FLATLAND_ALL_P_H_", "#define _BG_FLATLAND_ALL_P_H_", "",
           "#include <ultra64.h>", "",
           "extern u32 point_table_binary_1[];", "extern u32 point_table_binary_2[];",
           "extern u32 pri_mapping_binary_1[];", "extern u32 pri_mapping_binary_2[];",
           "#endif", ""]
    return "\n".join(out), "\n".join(hdr)


# ---------------------------------------------------------------- stan ------

LINK_BASE = 16            # link = (0x80 + byte offset of the tile in the stream) >> 3


def tile_name(cell, sub):
    # 'p<number>a<sub>' packs to id24 = (number << 8) | sub (stan.c stanPackId)
    return "p%da%d" % (cell, sub), (cell << 8) | sub


def tri_cell(p, i, j):
    """Two triangles of cell (i,j): T1 = (P00,P11,P10), T2 = (P00,P01,P11).
    The winding keeps cross((B-A),(C-A)) < 0 in (x,z), which is what the stan
    inside test wants. Point k's link is the tile across edge k -> k+1."""
    hx, hz = p.nx * p.cell // 2, p.nz * p.cell // 2
    x0, x1 = -hx + i * p.cell, -hx + (i + 1) * p.cell
    z0, z1 = -hz + j * p.cell, -hz + (j + 1) * p.cell
    p00, p10, p11, p01 = (x0, z0), (x1, z0), (x1, z1), (x0, z1)
    return (p00, p11, p10), (p00, p01, p11)


def tile_index(p, i, j, sub):
    return 2 * (j * p.nx + i) + (sub - 1)


def link_of(p, i, j, sub):
    if i < 0 or j < 0 or i >= p.nx or j >= p.nz:
        return 0
    return LINK_BASE + 4 * tile_index(p, i, j, sub)


def gen_stan(p):
    out = [HEADER_NOTE % ("Tbg_flatland_all_p_stanZ.c", p.describe()),
           "#include <ultra64.h>", "#include <game/stan.h>", "",
           "// forward declarations", "StandTile tile_0;", "",
           "StandFileHeader Tbg_flatland_all_p_stanZ = {", "    NULL,", "    &tile_0,",
           "    {0x00, 0x00, 0x00, 0x00}", "};", ""]
    n = 0
    for j in range(p.nz):
        for i in range(p.nx):
            cell = j * p.nx + i + 1
            t1, t2 = tri_cell(p, i, j)
            # links per edge (edge k = point k -> point k+1)
            #  T1 e0 diagonal -> own T2, e1 (x1 side) -> T2 of (i+1,j), e2 (z0 side) -> T2 of (i,j-1)
            #  T2 e0 (x0 side) -> T1 of (i-1,j), e1 (z1 side) -> T1 of (i,j+1), e2 diagonal -> own T1
            l1 = (link_of(p, i, j, 2), link_of(p, i + 1, j, 2), link_of(p, i, j - 1, 2))
            l2 = (link_of(p, i - 1, j, 1), link_of(p, i, j + 1, 1), link_of(p, i, j, 1))
            for sub, tri, lk in ((1, t1, l1), (2, t2, l2)):
                name, ident = tile_name(cell, sub)
                assert tile_index(p, i, j, sub) == n
                out.append("StandTile tile_%d = {" % n)
                out.append("    0x%06x, 0x%02x," % (ident, 1))
                out.append("    0x0,")
                out.append("    0x%x, 0x%x, 0x%x," % (p.tile_rgb[0], p.tile_rgb[1], p.tile_rgb[2]))
                out.append("    3,")
                out.append("    0x0, 0x1, 0x2,")
                out.append("    {")
                pts = []
                for k in range(3):
                    x, z = tri[k]
                    pts.append("        {%d, %d, %d, 0x%04x}" % (x, 0, z, lk[k]))
                out.append(",\n".join(pts))
                out.append("    }")
                out.append("};")
                out.append("")
                n += 1
    out.append("StandTile tile_%d = {" % n)
    out.append("    0x000000, 0x00,\n    0x0,\n    0x0, 0x0, 0x0,\n    0,\n    0x0, 0x0, 0x0\n};")
    out.append("")
    out.append("")
    out.append('StandFileFooter footer = {\n    "unstric",\n    NULL,\n    NULL,\n    NULL,\n    NULL\n};')
    out.append("")
    return "\n".join(out)


# --------------------------------------------------------------- setup ------

def gen_setup(p):
    hx, hz = p.nx * p.cell // 2, p.nz * p.cell // 2
    step = p.pad_every
    # pads sit at cell centres, every `pad_every` cells
    pcols = list(range(step // 2, p.nx, step))
    prows = list(range(step // 2, p.nz, step))
    P, Q = len(pcols), len(prows)
    pads = []   # (x, z, name)
    for gj, j in enumerate(prows):
        for gi, i in enumerate(pcols):
            cx = -hx + i * p.cell + p.cell // 2
            cz = -hz + j * p.cell + p.cell // 2
            name, _ = tile_name(j * p.nx + i + 1, 1)
            pads.append((cx, cz, name))
    start = (Q // 2) * P + (P // 2)

    out = [HEADER_NOTE % ("UsetupflatlandZ.c", p.describe()),
           '#include "ultra64.h"', '#include "bondtypes.h"', '#include "bondaicommands.h"', "",
           "// forward declarations", "PadRecord padlist[];", "BoundPadRecord pad3dlist[];",
           "s32 propDefs[];", "s32 intro[];", "waygroup pathsets[];", "waypoint pathwaypoints[];",
           "PathRecord patrolpaths[];", "AIListRecord ailists[];", "",
           "stagesetup UsetupflatlandZ = {", "    &pathwaypoints,", "    &pathsets,", "    &intro,",
           "    &propDefs,", "    &patrolpaths,", "    &ailists,", "    &padlist,", "    &pad3dlist,",
           "    NULL,", "    NULL", "};", "", "", "PadRecord padlist[] = {"]
    for (x, z, name) in pads:
        out.append('    { {%d.0f, 0.0f, %d.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, "%s", 0 },' % (x, z, name))
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

    # waypoints: one per pad, 4-neighbour links. Groups ("halls") are GxG blocks.
    G = p.group
    ngx, ngy = (P + G - 1) // G, (Q + G - 1) // G

    def wp(gi, gj):
        return gj * P + gi

    def grp(gi, gj):
        return (gj // G) * ngx + (gi // G)

    neigh = {}
    for gj in range(Q):
        for gi in range(P):
            n = []
            for (di, dj) in ((0, -1), (-1, 0), (1, 0), (0, 1)):
                a, b = gi + di, gj + dj
                if 0 <= a < P and 0 <= b < Q:
                    n.append(wp(a, b))
            neigh[wp(gi, gj)] = n
    for w in range(P * Q):
        out.append("s32 path_table_%d[] = { %s };" % (w, ", ".join(str(v) for v in neigh[w] + [-1])))
    out.append("")
    out.append("waypoint pathwaypoints[] = {")
    for gj in range(Q):
        for gi in range(P):
            w = wp(gi, gj)
            out.append("    { 0x%08x, &path_table_%d, 0x%08x, 0x00000000 }," % (w, w, grp(gi, gj)))
    out.append("    { 0xffffffff, NULL, 0x00000000, 0x00000000 }")
    out.append("};")
    out.append("")
    members = {}
    for gj in range(Q):
        for gi in range(P):
            members.setdefault(grp(gi, gj), []).append(wp(gi, gj))
    gneigh = {}
    for gy in range(ngy):
        for gx in range(ngx):
            g = gy * ngx + gx
            n = []
            for (dx, dy) in ((0, -1), (-1, 0), (1, 0), (0, 1)):
                a, b = gx + dx, gy + dy
                if 0 <= a < ngx and 0 <= b < ngy:
                    n.append(b * ngx + a)
            gneigh[g] = n
    for g in sorted(members):
        out.append("s32 path_neighbors_%d[] = { %s };" % (g, ", ".join(str(v) for v in gneigh[g] + [-1])))
    out.append("")
    for g in sorted(members):
        out.append("s32 path_indeces_%d[] = { %s };" % (g, ", ".join(str(v) for v in members[g] + [-1])))
    out.append("")
    out.append("waygroup pathsets[] = {")
    for g in sorted(members):
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
    return "\n".join(out), P, Q, start


HEADER_NOTE = """/*
* Generated by tools/gen_flatland.py - do not edit; rerun the script.
* %s
* %s
*/
"""


class Params:
    pass


def main():
    ap = argparse.ArgumentParser(description="Generate the Flatland test level")
    ap.add_argument("--size", type=int, default=6000, help="floor edge length, world units (default 6000)")
    ap.add_argument("--size-z", type=int, default=None, help="Z length if different from --size")
    ap.add_argument("--wall-height", type=int, default=100, help="perimeter wall height, 0 for none (default 100)")
    ap.add_argument("--pad-spacing", type=int, default=400, help="distance between pads, also the floor cell size (default 400)")
    ap.add_argument("--pad-every", type=int, default=1, help="pads every N floor cells (default 1; cell = pad-spacing / N)")
    ap.add_argument("--group", type=int, default=4, help="waypoint group (hall) edge, in pads (default 4)")
    ap.add_argument("--floor-tex", type=lambda s: int(s, 0), default=1264, help="floor texture number (default 1264, Surface snow)")
    ap.add_argument("--wall-tex", type=lambda s: int(s, 0), default=1263, help="wall texture number")
    ap.add_argument("--tex-texels", type=int, default=32, help="texels per repeat of the textures (default 32)")
    ap.add_argument("--out", default=None, help="output directory (default port/flatland next to this script's repo root)")
    a = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = a.out or os.path.join(root, "port", "flatland")
    os.makedirs(out, exist_ok=True)

    sz = a.size
    szz = a.size_z or a.size
    if a.pad_spacing % a.pad_every:
        sys.exit("--pad-spacing must be a multiple of --pad-every")
    p = Params()
    p.cell = a.pad_spacing // a.pad_every
    p.nx, p.nz = sz // p.cell, szz // p.cell
    if p.nx < 2 or p.nz < 2 or p.cell % 2 or (p.nx * p.cell) % 2 or (p.nz * p.cell) % 2:
        sys.exit("need >= 2 cells per side and an even cell size; adjust --size / --pad-spacing")
    if p.nx * p.cell // 2 > 30000 or p.nz * p.cell // 2 > 30000:
        sys.exit("size too large for s16 coordinates")
    p.pad_every = a.pad_every
    p.group = a.group
    p.wall_height = a.wall_height
    p.floor_tex = a.floor_tex
    p.wall_tex = a.wall_tex
    p.tex_texels = a.tex_texels
    p.tile_rgb = (4, 4, 4)
    p.describe = lambda: "floor %dx%d (%dx%d cells of %d), wall %d, pad spacing %d" % (
        p.nx * p.cell, p.nz * p.cell, p.nx, p.nz, p.cell, p.wall_height, p.cell * p.pad_every)

    bgc, bgh = gen_bg(p)
    stan = gen_stan(p)
    setup, P, Q, start = gen_setup(p)
    for name, text in (("bg_flatland_all_p.c", bgc), ("bg_flatland_all_p.h", bgh),
                       ("Tbg_flatland_all_p_stanZ.c", stan), ("UsetupflatlandZ.c", setup)):
        with open(os.path.join(out, name), "w", newline="\n") as f:
            f.write(text)
    print("flatland: %s; %d tiles, %dx%d pads (start pad %d), out=%s" % (
        p.describe(), 2 * p.nx * p.nz, P, Q, start, out))


if __name__ == "__main__":
    main()

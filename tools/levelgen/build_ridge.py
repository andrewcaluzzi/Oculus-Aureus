#!/usr/bin/env python3
"""SEVERNAYA RIDGE: the sniper duel's own level (tools/levelgen/ridge.json, then `python3 tools/gen_level.py tools/levelgen/ridge.json --out port/levelgen_ridge`).

Two ridges across a valley, ~113 m lip to lip, built from the levelgen box rooms (modding/levelgen.md): five open-top rooms in a row along z, Bond's ridge
(south, z < 0) and the hunters' (north) at floor 700, a ramp room down to the valley floor on each side, the valley between. The rooms are joined by
doorways as wide and as tall as their walls, so the whole thing is one open view (portals), and what stops a shot is the same box the eye sees:
- each ridge has a PARAPET along its lip (tall blocks with gaps: firing windows) and a second wall behind it (a trench between) with the gaps the other
  way round; a block is bg geometry AND a stan hole, so the AI's line and the drawn wall agree (the visibility glitch of the stock Surface is the stan
  tiles and the drawn ground disagreeing);
- ROCKS in the valley (blocks) break the sight lines;
- named pads: sp1..sp7 behind Bond's windows (his start is sp4), np1..np7 behind the hunters', vp1..vp6 beside the valley rocks (close perches).
Textures are the stock Surface snow and rock numbers (the game's own texels, read from the ROM at run time): nothing ROM-derived is written.
"""
import json
import os

H = 700            # the ridges' floor
V = 2800           # half the valley's width
R = 2400           # a ramp room's depth
T = 1800           # a ridge top's depth
G = 40             # wall thickness
X0, X1 = -5200, 5200
GAPS = [-4800 + 1600 * i for i in range(7)]   # window centres along x (7)
SEG = 1100         # a parapet segment's length (window = the rest of the 1600 pitch)

z_valley = (-V, V)
z_sramp = (-V - G - R, -V - G)
z_stop = (z_sramp[0] - G - T, z_sramp[0] - G)
z_nramp = (V + G, V + G + R)
z_ntop = (z_nramp[1] + G, z_nramp[1] + G + T)

LIP_S = z_stop[1]          # -5680
LIP_N = z_ntop[0]          # 5680
PARAPET = 90               # thickness
P_OFF = 200                # the parapet's distance from the lip (the doorway keeps 170 clear)


def segs(offset):
    """parapet segments: between the windows (window centre g -> wall from g+250 to g+250+SEG)"""
    out = []
    for g in GAPS:
        x0 = g + 250 + (offset or 0)
        if x0 < X0 + 100 or x0 + SEG > X1 - 100:
            continue
        out.append((x0, x0 + SEG))
    return out


def blocks_ridge(sign, lip):
    """sign -1 south, +1 north: the parapet at the lip, a second wall behind with the windows the other way round, rocks on the ridge's back"""
    out = []
    if sign < 0:
        pz = (lip - P_OFF - PARAPET, lip - P_OFF)
        wz = (lip - P_OFF - 700 - PARAPET, lip - P_OFF - 700)
    else:
        pz = (lip + P_OFF, lip + P_OFF + PARAPET)
        wz = (lip + P_OFF + 700, lip + P_OFF + 700 + PARAPET)
    for i, (a, b) in enumerate(segs(0)):
        out.append({"name": "par%d" % i, "rect": [a, pz[0], b, pz[1]], "height": 300, "mat": "rock", "top_mat": "snow_top"})
    for i, (a, b) in enumerate(segs(-800)):
        out.append({"name": "wall%d" % i, "rect": [a, wz[0], b, wz[1]], "height": 300, "mat": "rock", "top_mat": "snow_top"})
    # the ends of the trench are shut (a wall across it at each end)
    return out


def pad_rows(sign, lip, prefix):
    z = (lip - P_OFF - 350) if sign < 0 else (lip + P_OFF + 350)   # in the trench between the two walls
    return [{"name": "%s%d" % (prefix, i + 1), "at": [g, z], "face": [g, -sign * 4000]} for i, g in enumerate(GAPS)]


ROCKS = [(-4300, -700, 500, 400, 380), (-2300, 1200, 640, 520, 420), (-100, -350, 900, 520, 1100), (2200, -1300, 620, 520, 400),
         (4200, 900, 520, 420, 1000), (-900, 1900, 420, 420, 340), (1000, -2100, 520, 420, 360), (4500, -1000, 520, 420, 1000),
         (-4700, 700, 480, 420, 360), (3000, 2200, 560, 460, 400)]


def valley_blocks():
    out = []
    for i, (x, z, w, d, h) in enumerate(ROCKS):
        out.append({"name": "rock%d" % i, "rect": [x - w // 2, z - d // 2, x + w // 2, z + d // 2], "height": h, "mat": "rock", "top_mat": "snow_top"})
    return out


def valley_pads():
    out = []
    for i, (x, z, w, d, h) in enumerate(ROCKS[:6]):
        out.append({"name": "vp%d" % (i + 1), "at": [x + w // 2 + 360, z], "face": [x, -4000]})
    return out


def room(name, z, floor, **kw):
    r = {"name": name, "rect": [X0, z[0], X1, z[1]], "floor": floor, "height": 1500, "open_top": True}
    r.update(kw)
    return r


layout = {
    "name": "levelgen",
    "comment": "SEVERNAYA RIDGE, the sniper duel's level (tools/levelgen/build_ridge.py). Edit the script, not this file.",
    "defaults": {
        "height": 1500, "cell": 420, "door_width": 10200, "door_height": 1300, "max_wall": 200,
        "floor_mat": "snow", "wall_mat": "rock", "ceiling_mat": "snow", "chr_light": 14,
        "light": {"ambient": 0.74, "intensity": 0.28, "radius": 900, "lamp_spacing": 1400, "tint": [214, 226, 255], "lamp_height": 900},
    },
    "materials": {
        "snow": {"tex": 1264, "texels_per_unit": 0.064, "from": "Surface floor (the Colosseum's TEX_FLOOR)"},
        "snow_top": {"tex": 1264, "texels_per_unit": 0.064},
        "rock": {"tex": 1263, "texels_per_unit": 0.064, "from": "Surface wall (the Colosseum's TEX_WALL)"},
    },
    "rooms": [
        room("s_top", z_stop, H, blocks=blocks_ridge(-1, LIP_S), pads=pad_rows(-1, LIP_S, "sp")),
        room("s_ramp", z_sramp, H, ramp={"axis": "z", "to": 0}),
        room("valley", z_valley, 0, blocks=valley_blocks(), pads=valley_pads()),
        room("n_ramp", z_nramp, 0, ramp={"axis": "z", "to": H}),
        room("n_top", z_ntop, H, blocks=blocks_ridge(1, LIP_N), pads=pad_rows(1, LIP_N, "np")),
    ],
    "doors": [
        {"rooms": ["s_top", "s_ramp"], "at": 0, "width": 10200, "height": 1300},
        {"rooms": ["s_ramp", "valley"], "at": 0, "width": 10200, "height": 1300},
        {"rooms": ["valley", "n_ramp"], "at": 0, "width": 10200, "height": 1300},
        {"rooms": ["n_ramp", "n_top"], "at": 0, "width": 10200, "height": 1300},
    ],
    "start": {"room": "s_top", "at": [GAPS[3], LIP_S - P_OFF - 350], "face": [GAPS[3], 4000]},
}

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ridge.json")
    with open(out, "w") as f:
        json.dump(layout, f, indent=1)
    print("wrote", out, "rooms", len(layout["rooms"]))

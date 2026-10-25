#!/usr/bin/env python3
"""
build_janus_bank.py - writes tools/levelgen/janus_bank.json, the Janus bank v2 (designs/bank-heist.md section 17), for
tools/gen_level.py. The layout is written by code so that a building of a hundred rooms on six levels stays consistent: every room
comes from a band of x and z (the wings, the corridors, the strips round the hall) and a level, and doorways and openings name rooms.

    python3 tools/levelgen/build_janus_bank.py            -> tools/levelgen/janus_bank.json
    python3 tools/gen_level.py tools/levelgen/janus_bank.json

x east, z north, y up; units about a centimetre. Levels: B2 -840 (plant, tunnels, the canal), B1 -420 (the vault level), G 0 (the
street, the banking hall, the wings), F1 480 (the galleries round the hall, the loan offices), F2 960 (the hall's upper windows, open
offices, the library), F3 1440 (the executive floor, the roof). Interior walls are 40 thick (the gap between two rooms' rects).
Room `zone`: public / staff / restricted (the disguise rules); `area`: the heist's area tag (bank-heist.md section 17).
"""
import json
import os

G = 40                      # wall
Y_B2, Y_B1, Y_G, Y_F1, Y_F2, Y_F3 = -840, -420, 0, 480, 960, 1440

# x bands (west to east) and z bands (south to north) of the building
WW, WM, WC, WA, H, EA, EC, EM, EE = (-4400, -3400), (-3360, -2280), (-2240, -1840), (-1800, -1240), (-1200, 1200), \
    (1240, 1800), (1840, 2240), (2280, 3360), (3400, 4400)
ZF, ZS, ZT, ZN1, ZN2, ZN3 = (-1260, -860), (-820, 2160), (2200, 3000), (3040, 3360), (3400, 4600), (4640, 5000)

MATERIALS = {
    "white_marble":   {"tex": 308,  "texels_per_unit": 0.108, "from": "Archives wall: pale grey-white marble, vertical streaks"},
    "pink_marble":    {"tex": 179,  "texels_per_unit": 0.115, "from": "Archives wall: pale pink marble panels"},
    "pink_granite":   {"tex": 2376, "texels_per_unit": 0.879, "from": "Archives wall: small pink granite blocks"},
    "wood_panel":     {"tex": 1530, "texels_per_unit": 0.268, "from": "Train wall: brown wood panelling"},
    "wood_lattice":   {"tex": 1135, "texels_per_unit": 0.26,  "from": "Archives ceiling: dark wood lattice with copper strips (coffered)"},
    "light_wood":     {"tex": 1204, "texels_per_unit": 0.21,  "from": "Train ceiling: light wood planks"},
    "stone_flags":    {"tex": 405,  "texels_per_unit": 0.174, "from": "Egypt floor: large beige stone flags"},
    "red_carpet":     {"tex": 760,  "texels_per_unit": 0.31,  "from": "Train / Depot floor: red speckled carpet"},
    "planks":         {"tex": 1136, "texels_per_unit": 0.21,  "from": "Archives floor: brown planks"},
    "brown_tiles":    {"tex": 1137, "texels_per_unit": 0.8,   "from": "Archives floor: brown square tiles"},
    "white_tiles":    {"tex": 901,  "texels_per_unit": 0.75,  "from": "Bunker 2 floor"},
    "lobby_tiles":    {"tex": 632,  "texels_per_unit": 1.18,  "from": "Archives floor: small beige tiles"},
    "beige_panels":   {"tex": 315,  "texels_per_unit": 0.33,  "from": "Bunker 2 wall"},
    "grey_panels":    {"tex": 296,  "texels_per_unit": 0.42,  "from": "Bunker 2 wall: light grey vertical panels"},
    "office_ceiling": {"tex": 257,  "texels_per_unit": 0.60,  "from": "Bunker 2 ceiling: suspended tiles"},
    "plaster":        {"tex": 2374, "texels_per_unit": 0.27,  "from": "Archives wall: plain grey"},
    "metal_slats":    {"tex": 299,  "texels_per_unit": 1.16,  "from": "Archives wall: grey metal slats (shelving)"},
    "stone_blocks":   {"tex": 1469, "texels_per_unit": 0.414, "from": "Archives wall: grey stone blocks"},
    "stone_pillar":   {"tex": 1368, "texels_per_unit": 0.26,  "from": "Archives wall: stone pillars with copper bands"},
    "green_stone":    {"tex": 1474, "texels_per_unit": 0.306, "from": "Archives wall: green stone bands"},
    "brick":          {"tex": 195,  "texels_per_unit": 0.461, "from": "Archives wall: red brick"},
    "asphalt":        {"tex": 20,   "texels_per_unit": 0.475, "from": "Archives floor: grey speckled"},
    "concrete":       {"tex": 311,  "texels_per_unit": 0.16,  "from": "Facility floor: grey speckled concrete"},
    "concrete_wall":  {"tex": 284,  "texels_per_unit": 0.177, "from": "Train wall: stained grey concrete blocks"},
    "concrete_lines": {"tex": 283,  "texels_per_unit": 0.29,  "from": "Facility wall: grey concrete with lines"},
    "black_corrugated": {"tex": 1376, "texels_per_unit": 0.2, "from": "Depot wall: black corrugated metal"},
    "green_corrugated": {"tex": 1426, "texels_per_unit": 0.15, "from": "Train / Depot wall: green rusty corrugated metal"},
    "rusty_roof":     {"tex": 1081, "texels_per_unit": 0.39,  "from": "Depot ceiling: rusty corrugated"},
    "dark_grid":      {"tex": 258,  "texels_per_unit": 0.39,  "from": "Facility ceiling"},
    "steel_bulkhead": {"tex": 477,  "texels_per_unit": 0.531, "from": "Frigate wall: steel bulkhead with ribs"},
    "steel_panels":   {"tex": 478,  "texels_per_unit": 0.52,  "from": "Frigate wall: grey steel panels"},
    "steel_pipes":    {"tex": 476,  "texels_per_unit": 0.53,  "from": "Frigate wall: steel panels with black pipes"},
    "steel_floor":    {"tex": 1138, "texels_per_unit": 0.78,  "from": "Frigate floor: steel stud plate"},
    "grating":        {"tex": 235,  "texels_per_unit": 0.305, "from": "Archives floor: metal grating"},
    "riveted_metal":  {"tex": 727,  "texels_per_unit": 0.78,  "from": "Frigate ceiling: riveted metal"},
    "pipe_grid":      {"tex": 491,  "texels_per_unit": 0.347, "from": "Frigate ceiling: open metal grid with pipes above"},
    "diamond_plate":  {"tex": 400,  "texels_per_unit": 0.363, "from": "Depot wall: rusty diamond plate"},
    "dark_tiles":     {"tex": 440,  "texels_per_unit": 0.95,  "from": "Bunker 2 wall: small dark grey tiles"},
    "arc_blocks":     {"tex": 665,  "texels_per_unit": 0.578, "from": "Bunker 2 wall: grey blocks with an arc relief"},
    "sandstone":      {"tex": 404,  "texels_per_unit": 0.27,  "from": "Egypt wall: sandstone"},
}

LIGHT_WARM = [255, 240, 215]
LIGHT_OFFICE = [255, 244, 228]
LIGHT_COLD = [225, 236, 255]
LIGHT_SODIUM = [255, 205, 150]

rooms, doors, openings = [], [], []
_by = {}


# the heist's cover classes read the room `area` tags (designs/bank-heist.md section 16c: street, hall public; offices, corridor,
# records, manager, dock staff; security, vault_stairs, vault_ante, vault, deposit, service_ramp, power restricted; anything else
# public): the v2 floors map onto those names, and `zone` says the class outright
AREA_TAG = {"upper1": "offices", "upper2": "offices", "executive": "offices", "plant": "power", "stairs": "corridor"}


def room(name, rect, y=0, h=360, area=None, zone=None, **kw):
    r = {"name": name, "rect": list(rect), "floor": y, "height": h}
    if area:
        r["area"] = AREA_TAG.get(area, area)
    if zone:
        r["zone"] = zone
    r.update(kw)
    rooms.append(r)
    _by[name] = r
    return r


def bx(x, z):
    """A rect from an x band and a z band."""
    return [x[0], z[0], x[1], z[1]]


def door(a, b, at=None, width=None, height=None, name=None, frame=None, obj=None):
    d = {"rooms": [a, b]}
    for k, v in (("at", at), ("width", width), ("height", height), ("name", name), ("frame", frame), ("object", obj)):
        if v is not None:
            d[k] = v
    doors.append(d)
    return d


def opening(a, b, at, width, y, name=None, frame=None):
    o = {"rooms": [a, b], "at": at, "width": width, "y": list(y)}
    if name:
        o["name"] = name
    if frame:
        o["frame"] = frame
    openings.append(o)


def pads(r, *ps):
    r.setdefault("pads", []).extend({"name": n, "at": list(at), "face": list(face)} for (n, at, face) in ps)


def objs(r, *os_):
    r.setdefault("objects", []).extend(os_)


def prop(name, model, at, face, flags="0x101", **kw):
    o = {"type": "prop", "name": name, "model": model, "at": list(at), "face": list(face), "flags": flags}
    o.update(kw)
    return o


def cam(name, wall, along, y, watch, sweep=(30, -30)):
    return {"type": "camera", "name": name, "wall": wall, "along": along, "y": y, "watch": watch, "sweep": list(sweep)}


def alarm(name, wall, along, y=140):
    return {"type": "alarm", "name": name, "wall": wall, "along": along, "y": y}


def blocks(r, *bs):
    r.setdefault("blocks", []).extend(bs)


def block(name, rect, h, mat, top=None):
    b = {"name": name, "rect": list(rect), "height": h, "mat": mat}
    if top:
        b["top_mat"] = top
    return b


def light(ambient, intensity, radius, tint=LIGHT_OFFICE, **kw):
    li = {"ambient": ambient, "intensity": intensity, "radius": radius, "tint": tint}
    li.update(kw)
    return li


SLIDE = lambda name, **kw: dict({"name": name, "type": "slide", "see_through": True}, **kw)     # noqa: E731  an office door
WOOD, STEEL = "wood_panel", "steel_panels"
OFFICE = dict(floor_mat="red_carpet", wall_mat="beige_panels", ceiling_mat="office_ceiling")
STAFFC = dict(floor_mat="white_tiles", wall_mat="plaster", ceiling_mat="office_ceiling")
SECUR = dict(floor_mat="white_tiles", wall_mat="grey_panels", ceiling_mat="office_ceiling")
MARBLE = dict(floor_mat="stone_flags", wall_mat="white_marble", ceiling_mat="wood_lattice")
VAULTM = dict(floor_mat="steel_floor", wall_mat="steel_bulkhead", ceiling_mat="riveted_metal", chr_light=12)
SERVICE = dict(floor_mat="concrete", wall_mat="concrete_wall", ceiling_mat="dark_grid", chr_light=11)


# ======================================================================================================================== outside ==
def outside():
    st = room("street", (-5600, -2800, 6000, -1300), Y_G, 2000, "street", "public", open_top=True,
              floor_mat="asphalt", wall_mat="brick", wall_mats={"z1": "stone_blocks"}, chr_light=9,
              light=light(0.26, 1.0, 420, LIGHT_SODIUM, lamp_height=520,
                          lamps=[[x, -1450] for x in range(-5000, 6000, 1100)] + [[x, -2650] for x in range(-4500, 6000, 1500)]))
    pads(st, ("start_1", (-300, -1700), (0, -1300)), ("start_2", (-150, -1750), (0, -1300)), ("start_3", (-450, -1750), (0, -1300)),
         ("van", (-1600, -1820), (-1600, -2100)), ("street_e", (3600, -1900), (0, -1900)), ("street_w", (-4400, -2000), (0, -1900)),
         ("street_steps", (5600, -2050), (6040, -2050)))
    objs(st, prop("van_truck", 279, (-1600, -2150), (-1100, -2150)),
         prop("street_truck_2", 279, (3400, -2450), (2900, -2450)))
    al = room("alley", (4440, -1260, 4800, 5400), Y_G, 1600, "street", "public", open_top=True,
              floor_mat="asphalt", wall_mat="brick", chr_light=8,
              light=light(0.22, 1.0, 360, LIGHT_SODIUM, lamp_height=480, lamps=[[4620, -600], [4620, 1300], [4620, 3200], [4620, 4900]]))
    pads(al, ("alley_s", (4620, -1000), (4620, 4000)), ("alley_garage", (4620, 1200), (4400, 1200)),
         ("alley_dock", (4620, 4200), (4400, 4200)), ("alley_n", (4620, 5250), (4620, 0)))
    objs(al, prop("alley_bins", 114, (4700, 2600), (4400, 2600)))
    door("street", "alley", at=4620, width=300, height=320)
    # the embankment steps down from the street's east end to the canal towpath (the tunnel's outfall: the third way in)
    em = room("embankment", (6040, -2600, 8440, -1500), Y_G, 600, "street", "public", open_top=True,
              ramp={"axis": "x", "to": Y_B2, "stairs": 34, "riser_mat": "stone_blocks"},
              floor_mat="stone_flags", wall_mat="stone_blocks", chr_light=9,
              light=light(0.24, 1.0, 400, LIGHT_SODIUM, lamp_height=420, lamps=[[6400, -2050], [7300, -2050], [8200, -2050]]))
    pads(em, ("emb_top", (6200, -2050), (8440, -2050)), ("emb_bot", (8300, -2050), (6040, -2050)))
    door("street", "embankment", at=-2050, width=400, height=320, frame="stone_blocks")
    ca = room("canal", (8480, -2800, 9680, 8400), Y_B2, 2600, "street", "public", open_top=True,
              floor_mat="concrete", wall_mat="brick", wall_mats={"x1": "stone_blocks"}, chr_light=8,
              light=light(0.2, 1.0, 420, LIGHT_SODIUM, lamp_height=500, lamps=[[9080, z] for z in range(-2200, 8400, 1500)]))
    pads(ca, ("canal_s", (9080, -2000), (9080, 8000)), ("canal_mid", (9080, 3000), (9080, 8000)),
         ("canal_outfall", (8700, 7260), (8480, 7260)), ("canal_n", (9080, 8200), (9080, 0)))
    objs(ca, prop("canal_crates", 81, (9500, 5000), (8480, 5000)))
    door("embankment", "canal", at=-2050, width=400, height=320)


# ===================================================================================================================== ground =====
def ground():
    v = room("vestibule", (-700, -1260, 700, -860), Y_G, 440, "hall", "public", **MARBLE, wall_mats={"x0": "pink_marble", "x1": "pink_marble"},
             chr_light=13, light=light(0.5, 0.7, 380))
    pads(v, ("vest_mid", (0, -1060), (0, -860)))
    door("street", "vestibule", at=0, width=260, height=280, frame=WOOD,
         obj={"name": "front_door", "type": "slide", "double": True, "model": "door", "see_through": True})
    hall = room("hall", bx(H, (ZS[0], ZT[1])), Y_G, 1320, "hall", "public", **MARBLE, chr_light=14,
                light=light(0.5, 0.8, 760, LIGHT_WARM, lamp_spacing=800, lamp_height=760))
    blocks(hall,
           block("counter_w", (-1200, 2200, -900, 2280), 110, WOOD, "white_marble"),
           block("counter_w2", (-750, 2200, -620, 2280), 110, WOOD, "white_marble"),
           block("return_w", (-620, 2200, -540, 3000), 110, WOOD, "white_marble"),
           block("return_e", (540, 2200, 620, 3000), 110, WOOD, "white_marble"),
           block("counter_e2", (620, 2200, 750, 2280), 110, WOOD, "white_marble"),
           block("counter_e", (900, 2200, 1200, 2280), 110, WOOD, "white_marble"),
           *[block("pillar_%s%d" % (s, k + 1), (x - 60, z - 60, x + 60, z + 60), 1320, "stone_pillar")
             for k, z in enumerate((360, 1160, 1860)) for s, x in (("w", -600), ("e", 600))])
    pads(hall,
         ("flap_s", (-825, 2110), (-825, 3000)), ("flap_n", (-825, 2370), (-825, -820)),
         ("flap2_s", (825, 2110), (825, 3000)), ("flap2_n", (825, 2370), (825, -820)),
         ("teller_1", (-1060, 2350), (-1060, 2200)), ("teller_2", (-690, 2350), (-690, 2200)), ("teller_3", (690, 2350), (690, 2200)),
         ("teller_4", (1060, 2350), (1060, 2200)),
         ("teller_mid", (-880, 2600), (-880, 2200)), ("teller_back", (-700, 2880), (-900, 2400)), ("teller_back_e", (880, 2700), (880, 2200)),
         ("cust_1", (-1050, 2080), (-1050, 2200)), ("cust_2", (-680, 2080), (-680, 2200)), ("cust_3", (1000, 2080), (1000, 2200)),
         ("cust_4", (-300, -500), (-1200, -400)), ("cust_5", (-950, 1650), (-1200, 1650)), ("cust_6", (300, 700), (0, 700)),
         ("cust_7", (-250, 1400), (0, 1400)), ("cust_8", (-300, 0), (-400, -820)), ("cust_9", (150, -150), (-400, -680)),
         ("hall_mid", (0, 900), (0, -820)), ("hall_post_sw", (-1000, -650), (0, 900)), ("hall_post_se", (1000, -650), (0, 900)),
         ("hall_post_ne", (1000, 1900), (0, 900)), ("hall_post_nw", (-1000, 1900), (0, 900)),
         ("hall_entry_w", (-400, -680), (-400, 2200)), ("hall_entry_e", (400, -680), (400, 2200)),
         ("hall_n", (0, 2700), (0, -820)), ("screens", (-900, -700), (-900, -820)))
    scr = [(-1080, 0), (-960, 0), (-840, 0), (-720, 0)]
    imgs = [15, 11, 3, 16, 9, 16, 4, 10]
    objs(hall,
         cam("cam_hall_s", "z0", 1100, 1000, "cust_1", (35, -35)), cam("cam_hall_n", "z1", -1100, 1000, "teller_mid", (30, -30)),
         cam("cam_hall_e", "x1", 1950, 1000, "hall_mid", (35, -35)),
         alarm("alarm_hall", "z1", -1150),
         *[{"type": "monitor", "name": "screen_%d" % (k + 1), "wall": "z0", "along": scr[k % 4][0], "y": 150 if k < 4 else 232,
            "width": 100, "height": 72, "image": imgs[k]} for k in range(8)],
         prop("teller_desk_1", 38, (-1100, 2900), (-1100, 2200)), prop("teller_desk_2", 38, (1100, 2900), (1100, 2200)),
         prop("writing_table_1", 85, (0, 300), (0, 2200)), prop("writing_table_2", 85, (0, 1500), (0, 2200)))
    door("vestibule", "hall", name="vestibule-hall-w", at=-400, width=180, height=280, frame=STEEL,
         obj={"name": "blast_w", "type": "shutter", "open": True, "autoclose": 268435455})
    door("vestibule", "hall", name="vestibule-hall-e", at=400, width=180, height=280, frame=STEEL,
         obj={"name": "blast_e", "type": "shutter", "open": True, "autoclose": 268435455})

    # the aisles: arcades of four arches each side of the hall (the galleries above them look down through the hall's upper openings)
    for s, xb, wall in (("w", WA, "x0"), ("e", EA, "x1")):
        a = room("aisle_" + s, bx(xb, ZS), Y_G, 440, "hall", "public", floor_mat="stone_flags", wall_mat="pink_marble",
                 ceiling_mat="wood_lattice", chr_light=13, light=light(0.46, 0.75, 420, LIGHT_WARM))
        cx = (xb[0] + xb[1]) // 2
        for k, z in enumerate((-350, 350, 1050, 1750)):
            door("aisle_" + s, "hall", name="arch_%s%d" % (s, k + 1), at=z, width=440, height=380, frame="stone_blocks")
        pads(a, ("aisle_%s_s" % s, (cx, -650), (cx, 2160)), ("aisle_%s_n" % s, (cx, 2000), (cx, -820)))
        far = xb[0] + 60 if s == "w" else xb[1] - 60
        objs(a, prop("aisle_%s_desk_1" % s, 38, (far, 0), (cx, 0)), prop("aisle_%s_desk_2" % s, 38, (far, 700), (cx, 700)),
             prop("aisle_%s_desk_3" % s, 38, (far, 1400), (cx, 1400)))

    # ---- the west wing (GREEN): the offices
    oc = room("office_corr", (WC[0], ZT[0], WA[1], ZT[1]), Y_G, 360, "offices", "staff", **OFFICE)
    pads(oc, ("ocorr_s", (-2040, 2300), (-2040, 3000)), ("ocorr_mid", (-1640, 2640), (-2240, 2640)), ("ocorr_n", (-2040, 2900), (-2040, 2200)))
    door("hall", "office_corr", at=2640, width=200, frame=WOOD,
         obj={"name": "green_hall", "type": "slide", "double": True, "keyflags": "0x2", "see_through": True})
    cw = room("corr_w", bx(WC, ZS), Y_G, 360, "offices", "staff", **STAFFC)
    pads(cw, ("corr_w_s", (-2040, -650), (-2040, 2160)), ("corr_w_mid", (-2040, 700), (-2040, 2160)), ("corr_w_n", (-2040, 2000), (-2040, -820)))
    objs(cw, cam("cam_corr_w", "z0", -2040, 300, "corr_w_mid", (12, -12)))
    door("corr_w", "office_corr", at=-2040, width=200, frame=WOOD, obj=SLIDE("corr_w_door"))
    door("aisle_w", "corr_w", at=1950, width=160, frame=WOOD, obj={"name": "green_aisle", "type": "slide", "keyflags": "0x2"})
    oa = room("office_a", bx(WM, (-820, 120)), Y_G, 360, "offices", "staff", **OFFICE)
    pads(oa, ("office_a_mid", (-2820, -350), (-3360, -350)), ("office_a_1", (-3100, -350), (-3100, -820)),
         ("office_a_2", (-2500, -100), (-3360, -350)), ("office_a_3", (-2500, -650), (-3360, -350)), ("office_a_4", (-3200, -100), (-2280, -350)))
    objs(oa, cam("cam_offices", "x0", -700, 280, "office_a_mid"), alarm("alarm_offices", "z0", -2500),
         prop("desk_a1", 38, (-3280, -700), (-2280, -700)), prop("desk_a2", 38, (-3280, 0), (-2280, 0)),
         prop("desk_a3", 38, (-2800, -760), (-2800, 120)), prop("cabinet_a1", 43, (-2900, 70), (-2900, -820)),
         prop("cabinet_a2", 43, (-2800, 70), (-2800, -820)))
    door("corr_w", "office_a", at=-350, width=160, frame=WOOD, obj=SLIDE("office_a_door"))
    ob = room("office_b", bx(WM, (160, 1060)), Y_G, 360, "offices", "staff", **OFFICE)
    pads(ob, ("office_b_1", (-2820, 610), (-2280, 610)), ("office_b_2", (-2500, 300), (-3360, 610)), ("office_b_3", (-3200, 950), (-2280, 400)))
    objs(ob, prop("desk_b1", 38, (-3280, 300), (-2280, 300)), prop("desk_b2", 38, (-3280, 900), (-2280, 900)),
         prop("cabinet_b1", 43, (-2800, 1010), (-2800, 160)))
    door("corr_w", "office_b", at=610, width=160, frame=WOOD, obj=SLIDE("office_b_door"))
    mg = room("meeting_g", bx(WM, (1100, 2160)), Y_G, 360, "offices", "staff", **OFFICE)
    blocks(mg, block("meeting_table_g", (-3060, 1450, -2580, 1810), 76, WOOD, "wood_panel"))
    pads(mg, ("meeting_g_1", (-3200, 1630), (-2280, 1630)), ("meeting_g_2", (-2440, 1300), (-3360, 1630)))
    door("corr_w", "meeting_g", at=1630, width=160, frame=WOOD, obj=SLIDE("meeting_g_door"))
    tp = room("typing_pool", bx(WW, ZS), Y_G, 360, "offices", "staff", **OFFICE, light=light(0.46, 0.7, 420, lamp_spacing=480))
    pads(tp, ("pool_s", (-3900, -650), (-3900, 2160)), ("pool_mid", (-3900, 700), (-3400, 700)), ("pool_n", (-3900, 2000), (-3900, -820)))
    objs(tp, cam("cam_typing", "z1", -4250, 280, "pool_mid", (25, -25)),
         *[prop("pool_desk_%d" % (k + 1), 95 if k % 3 == 0 else 38, (x, z), (x + 400, z))
           for k, (x, z) in enumerate([(xx, zz) for zz in (-400, 200, 800, 1400) for xx in (-4250, -3750)])])
    door("office_a", "typing_pool", at=-350, width=160, frame=WOOD, obj=SLIDE("pool_door_a"))
    door("office_b", "typing_pool", at=610, width=160, frame=WOOD, obj=SLIDE("pool_door_b"))
    rec = room("records", (WW[0], ZT[0], WM[1], ZT[1]), Y_G, 360, "records", "staff", floor_mat="planks", wall_mat="plaster",
               ceiling_mat="dark_grid", chr_light=12, light=light(0.38, 0.75, 380))
    blocks(rec, *[block("shelf_%d" % (k + 1), (x, 2420, x + 60, 2780), 230, "metal_slats", "plaster")
                  for k, x in enumerate((-4150, -3850, -3550, -3250, -2950))])
    pads(rec, ("rec_1", (-2450, 2400), (-4400, 2400)), ("rec_2", (-2450, 2850), (-4400, 2850)), ("rec_3", (-3700, 2900), (-2280, 2900)),
         ("rec_4", (-4300, 2600), (-4300, 3000)), ("rec_aisle", (-3400, 2600), (-2280, 2600)))
    objs(rec, {"type": "key", "name": "floor_key", "at": [-4300, 2900], "keyflags": "0x1", "model": "key"})
    door("typing_pool", "records", at=-3900, width=160, frame=WOOD, obj=SLIDE("records_pool_door"))
    door("records", "office_corr", at=2640, width=160, frame=WOOD, obj=SLIDE("records_side_door"))

    # ---- the north range: the staff corridors either side of the grand stair's foot
    bw = room("back_corr_w", (WW[0], ZN1[0], -540, ZN1[1]), Y_G, 360, "corridor", "staff", **STAFFC)
    pads(bw, ("back_w", (-4250, 3200), (-540, 3200)), ("back_1", (-3300, 3200), (-540, 3200)), ("back_2", (-2100, 3200), (-4400, 3200)),
         ("back_3", (-1300, 3200), (-4400, 3200)), ("back_4", (-700, 3200), (-4400, 3200)))
    objs(bw, cam("cam_corridor", "x0", 3200, 280, "back_2", (10, -10)), alarm("alarm_corridor", "z0", -3500))
    door("hall", "back_corr_w", at=-800, width=160, frame=WOOD, obj=SLIDE("staff_door"))
    door("office_corr", "back_corr_w", at=-2040, width=180, frame=WOOD, obj={"name": "green_back", "type": "slide", "keyflags": "0x2"})
    door("records", "back_corr_w", at=-3100, width=160, frame=WOOD, obj=SLIDE("records_door"))
    sf = room("stair_foot", (-500, ZN1[0], 500, ZN1[1]), Y_G, 360, "mezzanine", "public", **MARBLE, chr_light=13)
    pads(sf, ("stair_foot", (0, 3200), (0, 4600)))
    door("hall", "stair_foot", at=0, width=760, height=320, frame="stone_blocks")
    door("back_corr_w", "stair_foot", at=3200, width=160, frame=WOOD, obj=SLIDE("staff_west"))
    gs = room("stair_grand", (-500, ZN2[0], 500, ZN2[1]), Y_G, 440, "mezzanine", "public",
              ramp={"axis": "z", "to": Y_F1, "stairs": 24, "riser_mat": "pink_marble"},
              floor_mat="stone_flags", wall_mat="white_marble", ceiling_mat="wood_lattice", chr_light=13, light=light(0.48, 0.75, 420, LIGHT_WARM))
    pads(gs, ("grand_bot", (0, 3500), (0, 4600)), ("grand_mid", (0, 4000), (0, 4600)), ("grand_top", (0, 4500), (0, 3400)))
    door("stair_foot", "stair_grand", at=0, width=760, height=320)
    cn = room("canteen", (WM[0], ZN2[0], WM[1], ZN2[1]), Y_G, 360, "corridor", "staff", floor_mat="white_tiles", wall_mat="beige_panels",
              ceiling_mat="office_ceiling", light=light(0.48, 0.7, 420))
    blocks(cn, block("canteen_table_1", (-3200, 3700, -2900, 3800), 76, WOOD, "light_wood"),
           block("canteen_table_2", (-3200, 4100, -2900, 4200), 76, WOOD, "light_wood"))
    pads(cn, ("canteen_1", (-2500, 3600), (-3360, 3600)), ("canteen_2", (-3050, 3950), (-3050, 3400)), ("canteen_3", (-2500, 4450), (-3360, 4000)))
    door("back_corr_w", "canteen", at=-2500, width=200, frame=WOOD, obj=SLIDE("canteen_door"))
    kt = room("kitchen", (WC[0], ZN2[0], -1680, ZN2[1]), Y_G, 360, "corridor", "staff", floor_mat="white_tiles", wall_mat="dark_tiles",
              ceiling_mat="office_ceiling")
    blocks(kt, block("kitchen_counter", (-1800, 3700, -1720, 4300), 96, "steel_panels", "steel_panels"))
    pads(kt, ("kitchen_1", (-1850, 3600), (-2240, 3600)), ("kitchen_2", (-1850, 4400), (-2240, 4400)))
    door("canteen", "kitchen", at=4000, width=160)
    mgr = room("manager", (-1640, ZN2[0], -540, ZN2[1]), Y_G, 360, "manager", "staff", floor_mat="red_carpet", wall_mat="wood_panel",
               ceiling_mat="wood_lattice", light=light(0.44, 0.7, 400, [255, 236, 205]))
    pads(mgr, ("mgr_desk", (-1300, 4300), (-1090, 3400)), ("mgr_1", (-1300, 3650), (-540, 4300)), ("safe_front", (-760, 4150), (-760, 4600)))
    objs(mgr, {"type": "safe", "name": "mgr_safe", "at": [-760, 4430], "face": [-760, 4000], "keyflags": "0x10"},
         prop("mgr_desk_prop", 38, (-1300, 4480), (-1300, 3400)), prop("mgr_shelf", 11, (-1590, 4000), (-540, 4000)),
         prop("mgr_plant", 317, (-600, 3500), (-1640, 3500)))
    mgr["loot"] = [{"name": "safe_loot", "at": [[-760, 4200]]}]
    door("back_corr_w", "manager", at=-1090, width=160, frame=WOOD, obj={"name": "manager_door", "type": "slide", "keyflags": "0x4"})

    be = room("back_corr_e", (540, ZN1[0], EE[1], ZN1[1]), Y_G, 360, "corridor", "staff", **STAFFC)
    pads(be, ("back_e1", (700, 3200), (4400, 3200)), ("back_e2", (1700, 3200), (4400, 3200)), ("back_e3", (2700, 3200), (540, 3200)),
         ("back_e", (4250, 3200), (540, 3200)))
    objs(be, cam("cam_back_e", "x1", 3200, 280, "back_e2", (10, -10)), alarm("alarm_back_e", "z0", 1100))
    door("hall", "back_corr_e", at=800, width=160, frame=WOOD, obj=SLIDE("staff_door_e"))
    door("stair_foot", "back_corr_e", at=3200, width=160, frame=WOOD, obj=SLIDE("staff_east"))
    sl = room("staff_lounge", (540, ZN2[0], 1800, ZN2[1]), Y_G, 360, "corridor", "staff", floor_mat="red_carpet", wall_mat="beige_panels",
              ceiling_mat="office_ceiling")
    pads(sl, ("lounge_1", (900, 3700), (1800, 4000)), ("lounge_2", (1500, 4400), (540, 4000)))
    objs(sl, prop("lounge_table", 85, (1170, 4000), (1170, 3400)), prop("lounge_plant", 316, (600, 4500), (1800, 4500)))
    door("back_corr_e", "staff_lounge", at=1170, width=160, frame=WOOD, obj=SLIDE("lounge_door"))
    lk = room("lockers", (1840, ZN2[0], 2880, ZN2[1]), Y_G, 360, "security", "restricted", **SECUR)
    pads(lk, ("lockers_1", (2050, 3700), (2880, 3700)), ("lockers_2", (2650, 4350), (1840, 4350)))
    objs(lk, *[prop("locker_%d" % (k + 1), 96 if k % 2 else 97, (1890, 3800 + 160 * k), (2880, 3800 + 160 * k)) for k in range(4)])
    door("back_corr_e", "lockers", at=2360, width=160, frame=WOOD, obj={"name": "lockers_door", "type": "slide", "keyflags": "0x4"})
    st = room("stairs", (2920, ZN2[0], EM[1], ZN2[1]), Y_G, 360, "vault_stairs", "restricted",
              ramp={"axis": "z", "to": Y_B1, "stairs": 21, "riser_mat": "stone_blocks"},
              floor_mat="stone_flags", wall_mat="stone_blocks", ceiling_mat="dark_grid", chr_light=12, light=light(0.4, 0.8, 360))
    pads(st, ("stair_top", (3140, 3500), (3140, 4600)), ("stair_mid", (3140, 4000), (3140, 4600)), ("stair_bot", (3140, 4500), (3140, 3400)))
    door("back_corr_e", "stairs", at=3140, width=240, height=260, frame=STEEL,
         obj={"name": "red_stairs", "type": "slide", "double": True, "model": "vault", "keyflags": "0x8"})
    dk = room("dock", (EE[0], ZN2[0], EE[1], ZN3[1]), Y_G, 480, "dock", "staff", floor_mat="concrete", wall_mat="black_corrugated",
              ceiling_mat="rusty_roof", chr_light=11, light=light(0.34, 0.85, 440))
    pads(dk, ("dock_mid", (3900, 4200), (4400, 4200)), ("dock_exit", (4250, 4200), (4400, 4200)), ("dock_1", (3550, 4850), (4400, 3600)),
         ("dock_2", (4250, 3600), (3400, 4800)), ("dock_3", (3550, 3600), (4400, 4800)))
    objs(dk, cam("cam_dock", "z0", 4300, 400, "dock_mid"), alarm("alarm_dock", "x0", 4300),
         prop("dock_crate_1", 79, (3500, 4300), (4400, 4300)), prop("dock_crate_2", 80, (3500, 4500), (4400, 4500)),
         prop("dock_boxes", 90, (4250, 4850), (4250, 3400)))
    door("back_corr_e", "dock", at=3900, width=200, frame=STEEL, obj=SLIDE("dock_door"))
    door("alley", "dock", at=4200, width=260, height=300, obj={"name": "dock_shutter", "type": "shutter", "padlock": True})
    rc = room("rear_corr", (-2400, ZN3[0], EM[1], ZN3[1]), Y_G, 360, "corridor", "staff", **STAFFC)
    pads(rc, ("rear_w", (-2250, 4820), (3360, 4820)), ("rear_mid", (0, 4820), (3360, 4820)), ("rear_e", (3200, 4820), (-2400, 4820)))
    door("kitchen", "rear_corr", at=-1960, width=160, frame=WOOD, obj=SLIDE("kitchen_back"))
    door("staff_lounge", "rear_corr", at=1170, width=160, frame=WOOD, obj=SLIDE("lounge_back"))
    door("lockers", "rear_corr", at=2360, width=160, frame=WOOD, obj={"name": "lockers_back", "type": "slide", "keyflags": "0x4"})
    door("rear_corr", "dock", at=4820, width=200, frame=STEEL, obj=SLIDE("dock_back"))

    # ---- the east wing (BLUE): the security wing, the garage
    so = room("sec_office", (EA[0], ZT[0], EC[1], ZT[1]), Y_G, 360, "security", "restricted", **SECUR)
    pads(so, ("console", (2050, 2850), (2240, 2850)), ("sec_mid", (1740, 2600), (1240, 2600)), ("sec_1", (1400, 2350), (2240, 2600)),
         ("sec_2", (2100, 2350), (1240, 2600)))
    objs(so, cam("cam_security", "z0", 1400, 280, "sec_mid"), alarm("alarm_security", "z1", 1800),
         prop("console_desk", 25, (2170, 2850), (1900, 2850)),
         {"type": "monitor", "name": "sec_screen_1", "wall": "x1", "along": 2600, "y": 200, "width": 90, "height": 66, "image": 11},
         {"type": "monitor", "name": "sec_screen_2", "wall": "x1", "along": 2700, "y": 200, "width": 90, "height": 66, "image": 15},
         {"type": "monitor", "name": "sec_screen_3", "wall": "x1", "along": 2800, "y": 200, "width": 90, "height": 66, "image": 9})
    door("hall", "sec_office", at=2640, width=200, frame=WOOD, obj={"name": "blue_hall", "type": "slide", "double": True, "keyflags": "0x4"})
    door("sec_office", "back_corr_e", at=1500, width=160, frame=WOOD, obj={"name": "sec_back", "type": "slide", "keyflags": "0x4"})
    sc = room("sec_corr", bx(EC, ZS), Y_G, 360, "security", "restricted", **SECUR)
    pads(sc, ("sec_corr_s", (2040, -650), (2040, 2160)), ("sec_corr_mid", (2040, 700), (2040, 2160)), ("sec_corr_n", (2040, 2000), (2040, -820)))
    objs(sc, cam("cam_sec_corr", "z1", 2040, 300, "sec_corr_mid", (12, -12)))
    door("sec_corr", "sec_office", at=2040, width=200, frame=WOOD, obj=SLIDE("sec_corr_door"))
    gr = room("guard_room", bx(EM, (1100, 2160)), Y_G, 360, "security", "restricted", **SECUR)
    pads(gr, ("guard_1", (2450, 1250), (3360, 1630)), ("guard_2", (3200, 1250), (2280, 1630)), ("guard_3", (2820, 1950), (2820, 1100)))
    objs(gr, prop("guard_table", 85, (2820, 1630), (2820, 2160)), prop("guard_locker_1", 97, (3310, 1900), (2280, 1900)),
         prop("guard_locker_2", 97, (3310, 2050), (2280, 2050)))
    door("sec_corr", "guard_room", at=1630, width=180)
    ar = room("armory", bx(EM, (160, 1060)), Y_G, 360, "security", "restricted", floor_mat="steel_floor", wall_mat="diamond_plate",
              ceiling_mat="riveted_metal", chr_light=12)
    blocks(ar, block("rack_1", (2500, 260, 3200, 320), 200, "metal_slats", "steel_panels"),
           block("rack_2", (2500, 900, 3200, 960), 200, "metal_slats", "steel_panels"))
    pads(ar, ("armory_1", (2820, 610), (3360, 610)), ("armory_2", (3250, 400), (2280, 610)))
    objs(ar, prop("ammo_1", 5, (3250, 800), (2280, 800)), prop("ammo_2", 7, (3250, 650), (2280, 650)))
    door("sec_corr", "armory", at=610, width=160, frame=STEEL, obj={"name": "armory_door", "type": "slide", "keyflags": "0x4"})
    bk = room("barracks", bx(EM, (-820, 120)), Y_G, 360, "security", "restricted", **SECUR)
    pads(bk, ("barracks_1", (2500, -350), (3360, -350)), ("barracks_2", (3150, -100), (2280, -350)), ("barracks_3", (3150, -650), (2280, -350)))
    objs(bk, *[prop("bunk_locker_%d" % (k + 1), 96, (3310, -700 + 180 * k), (2280, -700 + 180 * k)) for k in range(3)],
         prop("barracks_table", 85, (2800, -350), (2800, 120)))
    door("sec_corr", "barracks", at=-350, width=160, frame=WOOD, obj=SLIDE("barracks_door"))
    co = room("cash_office", (EM[0], ZT[0], EM[1], ZT[1]), Y_G, 360, "security", "restricted", floor_mat="white_tiles", wall_mat="steel_panels",
              ceiling_mat="office_ceiling")
    blocks(co, block("cash_table", (2600, 2500, 3040, 2700), 90, "steel_panels", "steel_panels"))
    pads(co, ("cash_1", (2450, 2350), (3360, 2600)), ("cash_2", (3200, 2850), (2280, 2600)))
    co["loot"] = [{"name": "cash_office_loot", "at": [[2700, 2400], [2950, 2400], [2820, 2850]]}]
    door("sec_office", "cash_office", at=2300, width=160, frame=STEEL, obj={"name": "cash_door", "type": "slide", "keyflags": "0x4"})
    ga = room("garage", (EE[0], ZS[0], EE[1], ZT[1]), Y_G, 840, "dock", "staff", floor_mat="concrete", wall_mat="concrete_lines",
              ceiling_mat="pipe_grid", chr_light=11, light=light(0.32, 0.9, 520, [235, 240, 255], lamp_height=760, lamp_spacing=760))
    blocks(ga, *[block("garage_col_%d" % (k + 1), (3870, z - 50, 3970, z + 50), 840, "concrete_wall") for k, z in enumerate((100, 1100, 2100))])
    pads(ga, ("garage_1", (3600, -500), (4400, 1000)), ("garage_2", (4200, 600), (3400, 1000)), ("garage_3", (3600, 1600), (4400, 1000)),
         ("garage_4", (4200, 2700), (3400, 2700)))
    objs(ga, cam("cam_garage", "z0", 4250, 700, "garage_2", (30, -30)),
         prop("armoured_van", 279, (3650, 600), (3650, 1600)), prop("garage_truck", 279, (4150, 1900), (4150, 2900)),
         prop("garage_crates", 88, (4300, -650), (3400, -650)))
    door("garage", "alley", at=1200, width=320, height=320, obj={"name": "garage_shutter", "type": "shutter", "keyflags": "0x4"})
    door("guard_room", "garage", at=1630, width=160, frame=STEEL, obj=SLIDE("garage_guard_door"))
    door("cash_office", "garage", at=2600, width=200, frame=STEEL, obj={"name": "cash_garage", "type": "slide", "keyflags": "0x4"})
    door("garage", "back_corr_e", at=3900, width=200, frame=STEEL, obj=SLIDE("garage_back"))


# ===================================================================================================================== F1 ========
def first():
    gal = dict(floor_mat="red_carpet", wall_mat="white_marble", ceiling_mat="wood_lattice", chr_light=13, light=light(0.48, 0.72, 420, LIGHT_WARM))
    gs = room("gallery_s", bx(H, ZF), Y_F1, 400, "mezzanine", "public", **gal)
    pads(gs, ("gal_s_w", (-900, -1060), (900, -1060)), ("gal_s_e", (900, -1060), (-900, -1060)))
    for x in (-480, 480):
        opening("gallery_s", "hall", x, 440, (Y_F1 + 100, Y_F1 + 380), name="gal_s~hall_%s" % ("w" if x < 0 else "e"), frame=WOOD)
    for x in (-760, 760):
        opening("street", "gallery_s", x, 320, (Y_F1 + 120, Y_F1 + 340), name="facade_1_%s" % ("w" if x < 0 else "e"), frame="stone_blocks")
    for s, xb in (("w", WA), ("e", EA)):
        cx = (xb[0] + xb[1]) // 2
        g = room("gallery_" + s, bx(xb, (ZF[0], ZT[1])), Y_F1, 400, "mezzanine", "public", **gal)
        pads(g, ("gal_%s_s" % s, (cx, -1100), (cx, 3000)), ("gal_%s_mid" % s, (cx, 1050), (cx, 3000)), ("gal_%s_n" % s, (cx, 2850), (cx, -1260)))
        for k, z in enumerate((-350, 350, 1050, 1750, 2600)):
            opening("gallery_" + s, "hall", z, 440, (Y_F1 + 100, Y_F1 + 380), name="gal_%s~hall_%d" % (s, k + 1), frame=WOOD)
        door("gallery_s", "gallery_" + s, at=-1060, width=200, frame=WOOD)
        objs(g, cam("cam_gallery_" + s, "z0", cx, 340, "gal_%s_mid" % s, (10, -10)))
    objs(_by["gallery_w"], alarm("alarm_mezz", "x0", 1400))
    c1n = room("corr1_n", (WA[0], ZN1[0], EA[1], ZN1[1]), Y_F1, 400, "mezzanine", "public", **gal)
    pads(c1n, ("c1n_w", (-1500, 3200), (1800, 3200)), ("c1n_mid", (0, 3200), (0, 3000)), ("c1n_e", (1500, 3200), (-1800, 3200)))
    for x in (-880, 880):
        opening("corr1_n", "hall", x, 440, (Y_F1 + 100, Y_F1 + 380), name="c1n~hall_%s" % ("w" if x < 0 else "e"), frame=WOOD)
    door("gallery_w", "corr1_n", at=-1520, width=200, frame=WOOD)
    door("gallery_e", "corr1_n", at=1520, width=200, frame=WOOD)
    mz = room("mezz", (-500, ZN3[0], 500, ZN3[1]), Y_F1, 400, "mezzanine", "public", **gal)
    pads(mz, ("mezz_mid", (0, 4820), (0, 3400)))
    objs(mz, cam("cam_mezz", "z1", 400, 340, "grand_mid", (25, -25)))
    door("stair_grand", "mezz", at=0, width=760, height=320, frame=WOOD)
    for s, rect, cx in (("w", (WA[0], ZN3[0], -540, ZN3[1]), -1170), ("e", (540, ZN3[0], EA[1], ZN3[1]), 1170)):
        r = room("corr1_rear_" + s, rect, Y_F1, 400, "mezzanine", "public", **gal)
        pads(r, ("c1r_%s" % s, (cx, 4820), (0, 4820)))
        door("mezz", "corr1_rear_" + s, at=4820, width=200, frame=WOOD) if s == "e" else door("corr1_rear_w", "mezz", at=4820, width=200, frame=WOOD)
    for s, rect, cx, label in (("w", (WA[0], ZN2[0], -540, ZN2[1]), -1170, "conference"), ("e", (540, ZN2[0], EA[1], ZN2[1]), 1170, "training")):
        r = room("conf_" + s, rect, Y_F1, 400, "upper1", "staff", floor_mat="red_carpet", wall_mat="wood_panel", ceiling_mat="office_ceiling",
                 light=light(0.46, 0.7, 420, [255, 236, 210]))
        blocks(r, block("conf_table_" + s, (cx - 300, 3800, cx + 300, 4200), 76, WOOD, "wood_panel"))
        pads(r, ("conf_%s_1" % s, (cx - 450, 3550), (cx, 4000)), ("conf_%s_2" % s, (cx + 450, 4450), (cx, 4000)))
        door("corr1_n", "conf_" + s, at=cx, width=180, frame=WOOD, obj=SLIDE("conf_%s_door" % s))
        door("conf_" + s, "corr1_rear_" + s, at=cx, width=180, frame=WOOD, obj=SLIDE("conf_%s_back" % s))
    # the loan offices (west) and the advisers (east): long corridors behind the galleries
    for s, xb in (("w", WC), ("e", EC)):
        cx = (xb[0] + xb[1]) // 2
        c = room("corr1_" + s, (xb[0], ZF[0], xb[1], ZN3[1]), Y_F1, 400, "upper1", "staff", **STAFFC)
        pads(c, ("c1%s_s" % s, (cx, -1100), (cx, 5000)), ("c1%s_mid" % s, (cx, 1500), (cx, 5000)), ("c1%s_n" % s, (cx, 4850), (cx, -1260)))
        a, b = ("corr1_w", "gallery_w") if s == "w" else ("gallery_e", "corr1_e")
        door(a, b, at=0, width=180, frame=WOOD, obj=SLIDE("gal_%s_staff1" % s))
        door(a, b, at=2600, width=180, frame=WOOD, obj=SLIDE("gal_%s_staff2" % s))
        a, b = ("corr1_w", "corr1_n") if s == "w" else ("corr1_n", "corr1_e")
        door(a, b, at=3200, width=180, frame=WOOD, obj=SLIDE("c1n_%s_door" % s))
        a, b = ("corr1_w", "corr1_rear_w") if s == "w" else ("corr1_rear_e", "corr1_e")
        door(a, b, at=4820, width=180, frame=WOOD, obj=SLIDE("c1r_%s_door" % s))
    objs(_by["corr1_w"], cam("cam_corr1_w", "z0", -2040, 340, "c1w_mid", (8, -8)), alarm("alarm_upper1", "x1", -600))
    for k, (zr, nm) in enumerate((((-1260, -100), "loan_1"), ((-60, 2160), "loan_2"), ((2200, 3000), "meeting_1"))):
        r = room(nm, bx(WM, zr), Y_F1, 400, "upper1", "staff", **OFFICE)
        cz = (zr[0] + zr[1]) // 2
        pads(r, ("%s_1" % nm, (-2820, cz), (-2280, cz)), ("%s_2" % nm, (-3200, zr[0] + 150), (-2280, cz)))
        objs(r, prop("%s_desk" % nm, 38, (-2800, zr[1] - 150), (-2800, zr[0])), prop("%s_cab" % nm, 43, (-3300, zr[0] + 60), (-3300, zr[1])))
        door("corr1_w", nm, at=cz, width=160, frame=WOOD, obj=SLIDE("%s_door" % nm))
    op1 = room("open_plan_1", (WW[0], ZF[0], WW[1], ZT[1]), Y_F1, 400, "upper1", "staff", **OFFICE, light=light(0.46, 0.7, 420, lamp_spacing=480))
    pads(op1, ("op1_s", (-3900, -1100), (-3900, 3000)), ("op1_mid", (-3900, 900), (-3400, 900)), ("op1_n", (-3900, 2850), (-3900, -1260)))
    objs(op1, cam("cam_open_plan_1", "z0", -4250, 340, "op1_mid", (20, -20)),
         *[prop("op1_desk_%d" % (k + 1), 95 if k % 2 else 38, (x, z), (x + 400, z))
           for k, (x, z) in enumerate([(xx, zz) for zz in (-700, 0, 700, 1400, 2100) for xx in (-4250, -3700)])])
    door("open_plan_1", "loan_1", at=-680, width=160, frame=WOOD, obj=SLIDE("op1_loan1"))
    door("open_plan_1", "loan_2", at=1000, width=160, frame=WOOD, obj=SLIDE("op1_loan2"))
    c1nw = room("corr1_nw", (WW[0], ZN1[0], WM[1], ZN1[1]), Y_F1, 400, "upper1", "staff", **STAFFC)
    pads(c1nw, ("c1nw_w", (-4250, 3200), (-2280, 3200)), ("c1nw_e", (-2450, 3200), (-4400, 3200)))
    door("corr1_nw", "corr1_w", at=3200, width=180, frame=WOOD, obj=SLIDE("c1nw_door"))
    door("open_plan_1", "corr1_nw", at=-3700, width=180, frame=WOOD, obj=SLIDE("op1_north"))
    tr = room("training", (WM[0], ZN2[0], WM[1], ZN3[1]), Y_F1, 400, "upper1", "staff", **OFFICE)
    pads(tr, ("training_1", (-2820, 3700), (-2820, 5000)), ("training_2", (-2820, 4700), (-2820, 3400)))
    objs(tr, *[prop("training_desk_%d" % (k + 1), 39, (-3100 + 300 * (k % 3), 4000 + 300 * (k // 3)), (-3100 + 300 * (k % 3), 3400))
               for k in range(6)])
    door("corr1_nw", "training", at=-2820, width=180, frame=WOOD, obj=SLIDE("training_door"))
    for k, (zr, nm) in enumerate((((-1260, 600), "advisor_1"), ((640, 2160), "advisor_2"), ((2200, 3000), "it_office"))):
        r = room(nm, bx(EM, zr), Y_F1, 400, "upper1", "staff", **OFFICE)
        cz = (zr[0] + zr[1]) // 2
        pads(r, ("%s_1" % nm, (2820, cz), (2280, cz)), ("%s_2" % nm, (3200, zr[0] + 150), (2280, cz)))
        objs(r, prop("%s_desk" % nm, 95 if nm == "it_office" else 38, (3280, cz), (2280, cz)),
             prop("%s_cab" % nm, 43, (2700, zr[1] - 50), (2700, zr[0])))
        door("corr1_e", nm, at=cz, width=160, frame=WOOD, obj=SLIDE("%s_door" % nm))
    c1ne = room("corr1_ne", (EM[0], ZN1[0], EE[1], ZN1[1]), Y_F1, 400, "upper1", "staff", **STAFFC)
    pads(c1ne, ("c1ne_w", (2450, 3200), (4400, 3200)), ("c1ne_e", (4250, 3200), (2280, 3200)))
    door("corr1_e", "corr1_ne", at=3200, width=180, frame=WOOD, obj=SLIDE("c1ne_door"))
    se = room("stair_e", (EM[0], ZN2[0], 2720, ZN2[1]), Y_F1, 400, "upper1", "staff",
              ramp={"axis": "z", "to": Y_F2, "stairs": 24, "riser_mat": "plaster"}, **STAFFC)
    pads(se, ("stair_e_bot", (2500, 3500), (2500, 4600)), ("stair_e_top", (2500, 4500), (2500, 3400)))
    door("corr1_ne", "stair_e", at=2500, width=200, frame=WOOD)
    a1 = room("archive_1", (2760, ZN2[0], EE[1], ZN2[1]), Y_F1, 400, "records", "staff", floor_mat="planks", wall_mat="plaster",
              ceiling_mat="dark_grid", chr_light=12, light=light(0.38, 0.75, 380))
    blocks(a1, *[block("arch_shelf_%d" % (k + 1), (3000, z, 4200, z + 60), 230, "metal_slats", "plaster") for k, z in enumerate((3650, 3950, 4250))])
    pads(a1, ("arch1_1", (2900, 3500), (4400, 3500)), ("arch1_2", (4300, 4500), (2760, 4500)))
    door("corr1_ne", "archive_1", at=3600, width=160, frame=WOOD, obj=SLIDE("archive_1_door"))


# ===================================================================================================================== F2 ========
def second():
    gal2 = dict(floor_mat="planks", wall_mat="white_marble", ceiling_mat="wood_lattice", chr_light=13, light=light(0.46, 0.72, 420, LIGHT_WARM))
    cs = room("corr2_s", bx(H, ZF), Y_F2, 400, "upper2", "staff", **gal2)
    pads(cs, ("c2s_w", (-900, -1060), (900, -1060)), ("c2s_e", (900, -1060), (-900, -1060)))
    for x in (-480, 480):
        opening("corr2_s", "hall", x, 400, (Y_F2 + 100, Y_F2 + 300), name="c2s~hall_%s" % ("w" if x < 0 else "e"), frame=WOOD)
    for x in (-760, 760):
        opening("street", "corr2_s", x, 320, (Y_F2 + 120, Y_F2 + 340), name="facade_2_%s" % ("w" if x < 0 else "e"), frame="stone_blocks")
    for s, xb in (("w", WA), ("e", EA)):
        cx = (xb[0] + xb[1]) // 2
        c = room("corr2_" + s, bx(xb, (ZF[0], ZT[1])), Y_F2, 400, "upper2", "staff", **gal2)
        pads(c, ("c2%s_s" % s, (cx, -1100), (cx, 3000)), ("c2%s_mid" % s, (cx, 1050), (cx, 3000)), ("c2%s_n" % s, (cx, 2850), (cx, -1260)))
        for k, z in enumerate((0, 1050, 2100)):
            opening("corr2_" + s, "hall", z, 400, (Y_F2 + 100, Y_F2 + 300), name="c2%s~hall_%d" % (s, k + 1), frame=WOOD)
        if s == "w":
            door("corr2_s", "corr2_w", at=-1060, width=200, frame=WOOD)
        else:
            door("corr2_s", "corr2_e", at=-1060, width=200, frame=WOOD)
    cn = room("corr2_n", (WA[0], ZN1[0], EA[1], ZN1[1]), Y_F2, 400, "upper2", "staff", **gal2)
    pads(cn, ("loggia_w", (-1500, 3200), (1800, 3200)), ("loggia_mid", (0, 3200), (0, 3000)), ("loggia_e", (1500, 3200), (-1800, 3200)))
    objs(cn, cam("cam_loggia", "x1", 3200, 340, "loggia_mid", (12, -12)), alarm("alarm_upper2", "z1", 400))
    for x in (-600, 600):
        opening("corr2_n", "hall", x, 600, (Y_F2 + 100, Y_F2 + 340), name="loggia~hall_%s" % ("w" if x < 0 else "e"), frame=WOOD)
    door("corr2_w", "corr2_n", at=-1520, width=200, frame=WOOD)
    door("corr2_e", "corr2_n", at=1520, width=200, frame=WOOD)
    op2 = room("open_plan_2w", (WM[0], ZF[0], WC[1], ZT[1]), Y_F2, 400, "upper2", "staff", **OFFICE, light=light(0.46, 0.7, 420, lamp_spacing=480))
    blocks(op2, *[block("op2_partition_%d" % (k + 1), (-3000, z, -2200, z + 30), 150, "grey_panels") for k, z in enumerate((-300, 700, 1700))])
    pads(op2, ("op2_s", (-2600, -1100), (-2600, 3000)), ("op2_mid", (-2600, 1200), (-1840, 1200)), ("op2_n", (-2600, 2850), (-2600, -1260)))
    objs(op2, *[prop("op2_desk_%d" % (k + 1), 95 if k % 2 else 39, (x, z), (x, z + 300))
                for k, (x, z) in enumerate([(xx, zz) for zz in (-900, 100, 1100, 2100) for xx in (-3100, -2350)])])
    door("open_plan_2w", "corr2_w", at=800, width=180, frame=WOOD, obj=SLIDE("op2_door_1"))
    door("open_plan_2w", "corr2_w", at=2300, width=180, frame=WOOD, obj=SLIDE("op2_door_2"))
    sv = room("server_room", bx(WW, (ZF[0], 1000)), Y_F2, 400, "security", "restricted", floor_mat="grating", wall_mat="dark_tiles",
              ceiling_mat="pipe_grid", chr_light=10, light=light(0.3, 0.8, 360, LIGHT_COLD))
    blocks(sv, *[block("rack_%d" % (k + 1), (x, -1000, x + 100, 400), 260, "metal_slats", "steel_panels") for k, x in enumerate((-4200, -3900, -3600))])
    pads(sv, ("server_1", (-3500, -1150), (-4400, -1150)), ("server_2", (-4300, 850), (-3400, 850)))
    objs(sv, cam("cam_server", "z1", -3500, 340, "server_1", (25, -25)), prop("mainframe_1", 49, (-3460, 0), (-4400, 0)))
    door("open_plan_2w", "server_room", at=600, width=160, frame=STEEL, obj={"name": "server_door", "type": "slide", "keyflags": "0x4"})
    it = room("it_dept", bx(WW, (1040, ZT[1])), Y_F2, 400, "upper2", "staff", **OFFICE)
    pads(it, ("it_1", (-3900, 1300), (-3400, 2000)), ("it_2", (-3900, 2700), (-3400, 2000)))
    objs(it, prop("it_desk_1", 95, (-4300, 1600), (-3400, 1600)), prop("it_desk_2", 95, (-4300, 2300), (-3400, 2300)))
    door("open_plan_2w", "it_dept", at=2000, width=160, frame=WOOD, obj=SLIDE("it_door"))
    c2nw = room("corr2_nw", (WW[0], ZN1[0], WC[1], ZN1[1]), Y_F2, 400, "upper2", "staff", **STAFFC)
    pads(c2nw, ("c2nw_w", (-4250, 3200), (-1840, 3200)), ("c2nw_e", (-2000, 3200), (-4400, 3200)))
    door("corr2_nw", "corr2_n", at=3200, width=180, frame=WOOD, obj=SLIDE("c2nw_door"))
    door("open_plan_2w", "corr2_nw", at=-2600, width=180, frame=WOOD, obj=SLIDE("op2_north"))
    door("it_dept", "corr2_nw", at=-3900, width=160, frame=WOOD, obj=SLIDE("it_north"))
    ar2 = room("archive_2", (WM[0], ZN2[0], WC[1], ZN2[1]), Y_F2, 400, "records", "staff", floor_mat="planks", wall_mat="plaster",
               ceiling_mat="dark_grid", chr_light=12, light=light(0.38, 0.75, 380))
    blocks(ar2, *[block("arch2_shelf_%d" % (k + 1), (-3100, z, -2100, z + 60), 230, "metal_slats", "plaster") for k, z in enumerate((3700, 4000, 4300))])
    pads(ar2, ("arch2_1", (-3250, 3500), (-1840, 3500)), ("arch2_2", (-1950, 4500), (-3360, 4500)))
    door("corr2_nw", "archive_2", at=-2600, width=160, frame=WOOD, obj=SLIDE("archive_2_door"))
    lib = room("library", (WA[0], ZN2[0], EA[1], ZN2[1]), Y_F2, 400, "upper2", "staff", floor_mat="planks", wall_mat="wood_panel",
               ceiling_mat="light_wood", chr_light=12, light=light(0.42, 0.75, 480, [255, 232, 196], lamp_spacing=600))
    blocks(lib, *[block("lib_shelf_%s%d" % (s, k + 1), (x0, z, x0 + 900, z + 60), 240, "wood_panel", "light_wood")
                  for k, z in enumerate((3700, 4100)) for s, x0 in (("w", -1500), ("e", 600))])
    pads(lib, ("lib_w", (-1600, 3500), (1800, 4000)), ("lib_mid", (0, 4000), (0, 3400)), ("lib_e", (1600, 4500), (-1800, 4000)))
    objs(lib, cam("cam_library", "z1", -1600, 340, "lib_mid", (30, -30)),
         prop("lib_table_1", 85, (0, 3700), (0, 4600)), prop("lib_table_2", 85, (0, 4300), (0, 3400)))
    door("corr2_n", "library", at=-1200, width=180, frame=WOOD, obj=SLIDE("library_w"))
    door("corr2_n", "library", at=1200, width=180, frame=WOOD, obj=SLIDE("library_e"))
    for nm, xr in (("open_plan_2e", (EC[0], EM[1])), ("accounts", EE)):
        r = room(nm, (xr[0], ZF[0], xr[1], ZT[1]), Y_F2, 400, "upper2", "staff", **OFFICE, light=light(0.46, 0.7, 420, lamp_spacing=480))
        cx = (xr[0] + xr[1]) // 2
        pads(r, ("%s_s" % nm, (cx, -1100), (cx, 3000)), ("%s_mid" % nm, (cx, 900), (cx, 3000)), ("%s_n" % nm, (cx, 2850), (cx, -1260)))
        xs = (xr[0] + 250, xr[1] - 250)
        objs(r, *[prop("%s_desk_%d" % (nm, k + 1), 39 if k % 2 else 95, (x, z), (x, z + 300))
                  for k, (x, z) in enumerate([(xx, zz) for zz in (-800, 300, 1400) for xx in xs])])
    door("corr2_e", "open_plan_2e", at=800, width=180, frame=WOOD, obj=SLIDE("op2e_door_1"))
    door("corr2_e", "open_plan_2e", at=2300, width=180, frame=WOOD, obj=SLIDE("op2e_door_2"))
    door("open_plan_2e", "accounts", at=2000, width=180, frame=WOOD, obj=SLIDE("accounts_door"))
    c2ne = room("corr2_ne", (EC[0], ZN1[0], EE[1], ZN1[1]), Y_F2, 400, "upper2", "staff", **STAFFC)
    pads(c2ne, ("c2ne_w", (2000, 3200), (4400, 3200)), ("c2ne_e", (4250, 3200), (1840, 3200)))
    door("corr2_n", "corr2_ne", at=3200, width=180, frame=WOOD, obj=SLIDE("c2ne_door"))
    door("open_plan_2e", "corr2_ne", at=2600, width=180, frame=WOOD, obj=SLIDE("op2e_north"))
    door("accounts", "corr2_ne", at=3900, width=180, frame=WOOD, obj=SLIDE("accounts_north"))
    its = room("it_store", (EC[0], ZN2[0], EC[1], ZN2[1]), Y_F2, 400, "upper2", "staff", **SERVICE)
    pads(its, ("it_store_1", (2040, 3600), (2040, 4600)))
    objs(its, prop("it_boxes", 22, (2150, 4000), (1840, 4000)))
    door("corr2_ne", "it_store", at=2040, width=160, obj=SLIDE("it_store_door"))
    cm = room("comms", (2760, ZN2[0], EE[1], ZN2[1]), Y_F2, 400, "security", "restricted", floor_mat="grating", wall_mat="steel_panels",
              ceiling_mat="pipe_grid", chr_light=11, light=light(0.34, 0.8, 380, LIGHT_COLD))
    pads(cm, ("comms_1", (2950, 3550), (4400, 3550)), ("comms_2", (4200, 4400), (2760, 4400)))
    objs(cm, prop("comms_unit_1", 67, (4350, 3800), (2760, 3800)), prop("comms_unit_2", 66, (4350, 4000), (2760, 4000)),
         prop("comms_mainframe", 48, (3100, 4500), (3100, 3400)))
    door("corr2_ne", "comms", at=3700, width=160, frame=STEEL, obj={"name": "comms_door", "type": "slide", "keyflags": "0x4"})
    cr = room("corr2_rear", (WA[0], ZN3[0], EE[1], ZN3[1]), Y_F2, 400, "upper2", "staff", **STAFFC)
    pads(cr, ("c2r_w", (-1650, 4820), (4400, 4820)), ("c2r_mid", (600, 4820), (4400, 4820)), ("c2r_e", (4250, 4820), (-1800, 4820)))
    door("library", "corr2_rear", at=0, width=200, frame=WOOD, obj=SLIDE("library_back"))
    door("stair_e", "corr2_rear", at=2500, width=200, frame=WOOD)
    door("comms", "corr2_rear", at=3700, width=160, frame=STEEL, obj={"name": "comms_back", "type": "slide", "keyflags": "0x4"})
    door("it_store", "corr2_rear", at=2040, width=160, obj=SLIDE("it_store_back"))


# ===================================================================================================================== F3 ========
def executive():
    c3 = room("corr3", (WW[0], ZN1[0], EE[1], ZN1[1]), Y_F3, 420, "executive", "staff", floor_mat="red_carpet", wall_mat="wood_panel",
              ceiling_mat="wood_lattice", light=light(0.44, 0.7, 420, [255, 236, 205]))
    pads(c3, ("exec_w", (-4250, 3200), (4400, 3200)), ("exec_mid", (0, 3200), (4400, 3200)), ("exec_e", (4250, 3200), (-4400, 3200)))
    objs(c3, cam("cam_exec", "x1", 3200, 360, "exec_mid", (6, -6)), alarm("alarm_exec", "z0", -2000))
    exm = dict(floor_mat="red_carpet", wall_mat="wood_panel", ceiling_mat="wood_lattice", light=light(0.44, 0.7, 440, [255, 236, 205]))
    sec = room("secretary", (WM[0], ZN2[0], WC[1], ZN2[1]), Y_F3, 420, "executive", "staff", **exm)
    pads(sec, ("secretary_1", (-2600, 3700), (-2600, 4600)), ("secretary_2", (-3200, 4450), (-1840, 4000)))
    objs(sec, prop("secretary_desk", 38, (-2600, 4000), (-2600, 3400)), prop("secretary_cab", 43, (-3300, 3600), (-2280, 3600)))
    door("corr3", "secretary", at=-2600, width=180, frame=WOOD, obj=SLIDE("secretary_door"))
    dr = room("director", (WA[0], ZN2[0], -200, ZN2[1]), Y_F3, 420, "executive", "staff", **exm)
    pads(dr, ("director_desk", (-1000, 4300), (-1000, 3400)), ("director_1", (-1600, 3600), (-200, 4300)), ("dir_safe_front", (-400, 4150), (-400, 4600)))
    objs(dr, {"type": "safe", "name": "director_safe", "at": [-400, 4430], "face": [-400, 4000], "keyflags": "0x10"},
         prop("director_desk_prop", 38, (-1000, 4480), (-1000, 3400)), prop("director_shelf", 11, (-1750, 3600), (-200, 3600)),
         prop("director_plant", 317, (-300, 3500), (-1800, 3500)))
    dr["loot"] = [{"name": "director_loot", "at": [[-400, 4200], [-1300, 3700]]}]
    door("secretary", "director", at=4000, width=180, frame=WOOD, obj={"name": "director_door", "type": "slide", "keyflags": "0x4"})
    door("corr3", "director", at=-1000, width=180, frame=WOOD, obj={"name": "director_hall", "type": "slide", "keyflags": "0x4"})
    bd = room("boardroom", (-160, ZN2[0], EA[1], ZN2[1]), Y_F3, 420, "executive", "staff", **exm)
    blocks(bd, block("board_table", (400, 3800, 1200, 4200), 78, WOOD, "wood_panel"))
    pads(bd, ("board_1", (100, 4000), (800, 4000)), ("board_2", (1550, 4000), (800, 4000)))
    objs(bd, prop("board_plant", 316, (1700, 3500), (0, 4000)))
    door("corr3", "boardroom", at=800, width=200, frame=WOOD, obj=SLIDE("boardroom_door"))
    el = room("exec_lounge", (EC[0], ZN2[0], EE[1], ZN2[1]), Y_F3, 420, "executive", "staff", **exm)
    blocks(el, block("exec_bar", (2400, 4300, 3600, 4380), 110, WOOD, "white_marble"))
    pads(el, ("lounge3_1", (2200, 3600), (4400, 3600)), ("lounge3_2", (4100, 4000), (1840, 4000)))
    objs(el, prop("exec_table", 85, (3600, 3700), (3600, 4600)))
    door("corr3", "exec_lounge", at=3100, width=200, frame=WOOD, obj=SLIDE("exec_lounge_door"))
    rf = room("roof", (WW[0], ZF[0], EE[1], ZT[1]), Y_F3, 260, "executive", "staff", open_top=True,
              floor_mat="asphalt", wall_mat="stone_blocks", chr_light=9,
              light=light(0.24, 1.0, 520, LIGHT_SODIUM, lamp_height=180, lamps=[[x, z] for x in (-3600, -1200, 1200, 3600) for z in (-700, 1000, 2600)]))
    blocks(rf, block("skylight", (-900, 200, 900, 2000), 140, "steel_panels", "grating"),
           block("roof_plant_w", (-3800, 0, -3000, 600), 180, "green_corrugated", "rusty_roof"),
           block("roof_plant_e", (3000, 1400, 3800, 2000), 180, "green_corrugated", "rusty_roof"))
    pads(rf, ("roof_door", (0, 2850), (0, -1260)), ("roof_w", (-4200, -1100), (4400, 2000)), ("roof_e", (4200, -1100), (-4400, 2000)),
         ("roof_helipad", (2400, 600), (0, 600)))
    objs(rf, cam("cam_roof", "z1", -2000, 160, "roof_helipad", (40, -40)), prop("roof_unit", 67, (-2400, 2600), (-2400, 0)))
    door("roof", "corr3", at=0, width=200, frame=STEEL, obj={"name": "roof_door", "type": "slide", "keyflags": "0x4"})


# ===================================================================================================================== B1, B2 ====
def basement():
    bl = room("b1_lobby", (EM[0], ZN3[0], EM[1], 5400), Y_B1, 360, "vault_ante", "restricted", **VAULTM, light=light(0.42, 0.75, 400, LIGHT_COLD))
    pads(bl, ("b1_lobby_1", (2500, 4800), (3360, 5200)), ("b1_lobby_2", (3200, 5250), (2280, 4800)))
    objs(bl, cam("cam_b1_lobby", "x0", 5250, 300, "b1_lobby_1", (20, -20)))
    door("stairs", "b1_lobby", at=3140, width=240, height=260)
    vc = room("vault_corr", (-2400, 5440, EM[1], 5800), Y_B1, 360, "vault_ante", "restricted", **VAULTM, light=light(0.4, 0.75, 400, LIGHT_COLD))
    pads(vc, ("vcorr_w", (-2250, 5620), (3360, 5620)), ("vcorr_mid", (0, 5620), (3360, 5620)), ("vcorr_e", (3200, 5620), (-2400, 5620)))
    objs(vc, cam("cam_vault_corr", "x1", 5620, 300, "vcorr_mid", (6, -6)), alarm("alarm_b1", "z0", 1500))
    door("b1_lobby", "vault_corr", at=2820, width=240, height=260, frame=STEEL,
         obj={"name": "b1_gate", "type": "shutter", "open": True, "autoclose": 268435455})
    an = room("ante_a", (-600, 5840, 600, 6640), Y_B1, 360, "vault_ante", "restricted", **VAULTM, light=light(0.42, 0.75, 420, LIGHT_COLD))
    pads(an, ("crack", (0, 6500), (0, 6640)), ("ante_1", (-450, 5980), (0, 6640)), ("ante_2", (450, 5980), (0, 6640)),
         ("ante_3", (-450, 6480), (600, 6200)), ("ante_4", (450, 6480), (-600, 6200)))
    objs(an, cam("cam_vault", "z0", 450, 300, "crack", (25, -25)), alarm("alarm_vault", "x0", 6300))
    door("vault_corr", "ante_a", at=0, width=240, height=260, frame=STEEL)
    va = room("vault", (-1000, 6680, 1000, 7880), Y_B1, 400, "vault", "restricted", floor_mat="steel_floor", wall_mat="steel_panels",
              ceiling_mat="riveted_metal", chr_light=12, light=light(0.45, 0.7, 440, LIGHT_COLD))
    blocks(va, block("vault_cage_w", (-1000, 7300, -700, 7340), 260, "metal_slats"), block("vault_cage_e", (700, 7600, 1000, 7640), 260, "metal_slats"))
    pads(va, ("vault_in_a", (0, 6800), (0, 7880)), ("vault_in_b", (880, 7280), (-1000, 7280)), ("vault_captain", (0, 7750), (0, 6680)),
         ("vault_g1", (-850, 6850), (1000, 7800)), ("vault_g2", (-850, 7750), (1000, 6850)), ("vault_g3", (850, 7780), (-1000, 6850)))
    objs(va, prop("vault_crate_1", 51, (-930, 7050), (1000, 7050)), prop("vault_crate_2", 52, (-930, 7180), (1000, 7180)),
         prop("vault_crate_3", 79, (-500, 7820), (-500, 6680)), prop("vault_crate_4", 80, (500, 7820), (500, 6680)))
    va["loot"] = [{"name": "vault_gold", "at": [[x, z] for z in (7050, 7300, 7550) for x in (-450, 0, 450) if (x, z) != (0, 7300)]}]
    door("ante_a", "vault", at=0, width=240, height=270, frame={"mat": STEEL, "width": 24, "depth": 10},
         obj={"name": "vault_door_a", "type": "slide", "double": True, "model": "vault", "keyflags": "0x40000000", "autoclose": 268435455})
    dp = room("deposit", (1040, 6680, 2240, 7880), Y_B1, 360, "deposit", "restricted", floor_mat="steel_floor", wall_mat="pink_granite",
              ceiling_mat="riveted_metal", chr_light=12, light=light(0.42, 0.75, 400, [235, 236, 250]))
    pads(dp, ("dep_mid", (1640, 7280), (1040, 7280)), ("dep_1", (1200, 6850), (2240, 7800)), ("dep_2", (2080, 6850), (1040, 7800)),
         ("dep_3", (2080, 7720), (1040, 6850)))
    door("vault", "deposit", at=7280, width=220, height=270, frame={"mat": STEEL, "width": 24, "depth": 10},
         obj={"name": "vault_door_b", "type": "slide", "double": True, "model": "vault", "keyflags": "0x40000000", "autoclose": 268435455})
    for k, (x0, x1, kf, nm) in enumerate(((1040, 1600, "0x2", "cell_green"), (1680, 2240, "0x1", "cell_floor"))):
        c = room("cell_%d" % (k + 1), (x0, 7920, x1, 8400), Y_B1, 300, "deposit", "restricted", floor_mat="steel_floor", wall_mat="dark_tiles",
                 ceiling_mat="riveted_metal")
        cx = (x0 + x1) // 2
        c["loot"] = [{"name": "cell%d_cash" % (k + 1), "at": [[cx - 120, 8250], [cx + 120, 8250], [cx, 8330]]}]
        door("deposit", "cell_%d" % (k + 1), at=cx, width=140, height=220, obj={"name": nm, "type": "slide", "model": "cell", "keyflags": kf})
    cp = room("cash_processing", (EM[0], 6280, 4000, 7880), Y_B1, 360, "deposit", "restricted", floor_mat="white_tiles", wall_mat="steel_panels",
              ceiling_mat="office_ceiling", chr_light=12)
    blocks(cp, block("sorting_table_1", (2700, 6700, 3500, 6800), 90, "steel_panels", "steel_panels"),
           block("sorting_table_2", (2700, 7300, 3500, 7400), 90, "steel_panels", "steel_panels"))
    pads(cp, ("cashp_1", (2450, 6450), (4000, 7000)), ("cashp_2", (3800, 7700), (2280, 7000)), ("cashp_3", (3100, 7050), (3100, 7880)))
    cp["loot"] = [{"name": "cashp_loot", "at": [[2900, 6900], [3300, 6900], [3100, 7500]]}]
    door("deposit", "cash_processing", at=7280, width=200, frame=STEEL, obj=SLIDE("cashp_door"))
    sr = room("service_ramp", (3600, 5040, 4000, 6240), Y_G, 360, "deposit", "restricted", ramp={"axis": "z", "to": Y_B1}, **SERVICE,
              light=light(0.36, 0.8, 360))
    pads(sr, ("ramp_top", (3800, 5150), (3800, 6240)), ("ramp_mid", (3800, 5640), (3800, 5040)), ("ramp_bot", (3800, 6130), (3800, 5040)))
    door("dock", "service_ramp", at=3800, width=240, height=260, frame=STEEL, obj={"name": "red_ramp", "type": "shutter", "keyflags": "0x8"})
    door("service_ramp", "cash_processing", at=3800, width=240, height=260)
    bx_ = room("deposit_boxes", (-1960, 5840, -640, 6640), Y_B1, 360, "deposit", "restricted", floor_mat="steel_floor", wall_mat="arc_blocks",
               ceiling_mat="riveted_metal", chr_light=12, light=light(0.4, 0.75, 380, [235, 236, 250]))
    blocks(bx_, block("box_wall", (-1700, 6150, -900, 6330), 260, "metal_slats", "steel_panels"))
    pads(bx_, ("boxes_1", (-1800, 6000), (-640, 6400)), ("boxes_2", (-800, 6500), (-1960, 6000)))
    bx_["loot"] = [{"name": "box_loot", "at": [[-1500, 6000], [-1100, 6000], [-1500, 6500], [-1100, 6500]]}]
    door("vault_corr", "deposit_boxes", at=-1300, width=200, frame=STEEL, obj={"name": "boxes_door", "type": "slide", "model": "vault", "keyflags": "0x1"})
    ct = room("counting_room", (640, 5840, 2240, 6640), Y_B1, 360, "vault_ante", "restricted", floor_mat="white_tiles", wall_mat="steel_panels",
              ceiling_mat="office_ceiling", chr_light=12)
    blocks(ct, block("count_table", (1100, 6150, 1800, 6300), 90, "steel_panels", "steel_panels"))
    pads(ct, ("count_1", (800, 6000), (2240, 6400)), ("count_2", (2100, 6500), (640, 6000)))
    objs(ct, cam("cam_counting", "x1", 6000, 300, "count_1", (25, -25)), prop("count_console", 29, (2190, 6400), (640, 6400)))
    door("vault_corr", "counting_room", at=1440, width=180, frame=STEEL, obj=SLIDE("counting_door"))
    sb = room("stair_b2", (-2400, 5840, -2000, 7040), Y_B1, 360, "plant", "restricted",
              ramp={"axis": "z", "to": Y_B2, "stairs": 21, "riser_mat": "concrete_wall"}, **SERVICE, light=light(0.34, 0.8, 360))
    pads(sb, ("stair_b2_top", (-2200, 5950), (-2200, 7040)), ("stair_b2_bot", (-2200, 6930), (-2200, 5840)))
    door("vault_corr", "stair_b2", at=-2200, width=200, height=260, frame=STEEL, obj={"name": "plant_gate", "type": "slide", "keyflags": "0x8"})
    # ---- B2: the plant level
    b2 = room("b2_corr", (-2840, 7080, 2400, 7440), Y_B2, 360, "plant", "restricted", floor_mat="concrete", wall_mat="steel_pipes",
              ceiling_mat="pipe_grid", chr_light=10, light=light(0.3, 0.85, 380, [255, 225, 190]))
    pads(b2, ("b2_w", (-2690, 7260), (2400, 7260)), ("b2_mid", (0, 7260), (2400, 7260)), ("b2_e", (2250, 7260), (-2840, 7260)))
    door("stair_b2", "b2_corr", at=-2200, width=200, height=260)
    ms = room("maint_stair", (-2840, ZN3[0], -2440, 7040), Y_G, 360, "plant", "restricted",
              ramp={"axis": "z", "to": Y_B2, "stairs": 40, "riser_mat": "concrete_wall"}, **SERVICE, light=light(0.32, 0.8, 340))
    pads(ms, ("maint_top", (-2640, 4750), (-2640, 7040)), ("maint_mid", (-2640, 5840), (-2640, 7040)), ("maint_bot", (-2640, 6930), (-2640, 4640)))
    door("canteen", "maint_stair", at=-2640, width=200, height=260, frame=STEEL, obj={"name": "maint_door", "type": "slide", "keyflags": "0x2"})
    door("maint_stair", "b2_corr", at=-2640, width=200, height=260)
    pw = room("power", (-4000, 7480, -1040, 9080), Y_B2, 600, "plant", "restricted", floor_mat="concrete", wall_mat="steel_pipes",
              ceiling_mat="pipe_grid", chr_light=10, light=light(0.28, 0.95, 560, [255, 220, 180], lamp_height=520, lamp_spacing=700))
    # the plant room (designs/bank-heist.md section 16: the power cut): restricted, no camera; the main breaker on its east wall, the
    # backup generator in its own room behind it
    blocks(pw, block("dynamo_1", (-3600, 7900, -3000, 8300), 300, "diamond_plate", "steel_panels"),
           block("dynamo_2", (-3600, 8500, -3000, 8900), 300, "diamond_plate", "steel_panels"),
           block("transformer", (-2200, 8500, -1600, 8900), 360, "green_corrugated", "rusty_roof"),
           block("switchgear", (-1180, 7700, -1040, 8000), 240, "steel_panels", "steel_panels"),
           block("switchgear_2", (-1180, 8400, -1040, 8700), 240, "steel_panels", "steel_panels"))
    pads(pw, ("power_1", (-2500, 7650), (-1040, 8300)), ("power_2", (-3800, 8400), (-1040, 8400)), ("breaker_front", (-1230, 8200), (-1040, 8200)),
         ("power_mid", (-2400, 8150), (-1040, 8150)), ("power_3", (-2500, 8950), (-2500, 7480)))
    objs(pw, alarm("alarm_plant", "x1", 7650),
         prop("breaker", 30, (-1110, 8200), (-1600, 8200)),
         prop("power_console_1", 31, (-3300, 7550), (-3300, 9080)), prop("power_console_2", 35, (-3000, 7550), (-3000, 9080)),
         prop("power_tank", 117, (-3850, 7650), (-1040, 7650)))
    door("b2_corr", "power", at=-2300, width=240, height=280, frame=STEEL,
         obj={"name": "power_door", "type": "slide", "keyflags": "0x8", "guard_access": "any"})   # (the investigators of a power cut open it)
    pads(b2, ("power_way", (-1500, 7260), (-2300, 7480)))
    objs(b2, cam("cam_plant", "x1", 7260, 300, "b2_mid", (6, -6)))
    gn = room("generator", (-4000, 9120, -1040, 9900), Y_B2, 400, "plant", "restricted", floor_mat="concrete", wall_mat="steel_pipes",
              ceiling_mat="pipe_grid", chr_light=10, light=light(0.28, 0.9, 420, [255, 220, 180]))
    pads(gn, ("generator_front", (-2500, 9650), (-2500, 9900)), ("gen_1", (-3700, 9300), (-1040, 9500)))
    objs(gn, prop("generator", 48, (-2500, 9780), (-2500, 9120)), prop("gen_tank_1", 117, (-3800, 9750), (-1040, 9750)),
         prop("gen_tank_2", 117, (-1250, 9750), (-4000, 9750)))
    door("power", "generator", at=-2500, width=200, height=260, frame=STEEL, obj=SLIDE("generator_door"))
    av = room("archive_vault", (-1000, 7480, 2400, 8680), Y_B2, 400, "plant", "restricted", floor_mat="concrete", wall_mat="concrete_lines",
              ceiling_mat="dark_grid", chr_light=10, light=light(0.3, 0.8, 400))
    blocks(av, *[block("deep_shelf_%d" % (k + 1), (x, 7700, x + 60, 8500), 300, "metal_slats", "steel_panels") for k, x in enumerate((-600, -200, 200, 600, 1000))])
    pads(av, ("deep_1", (1800, 7650), (-1000, 8100)), ("deep_2", (2200, 8500), (-1000, 8100)), ("deep_3", (-850, 8550), (2400, 8100)))
    av["loot"] = [{"name": "deep_loot", "at": [[1500, 8000], [1900, 8300]]}]
    door("b2_corr", "archive_vault", at=1800, width=200, frame=STEEL, obj=SLIDE("archive_vault_door"))
    t1 = room("tunnel_1", (2440, 7120, 5440, 7400), Y_B2, 300, "plant", "restricted", floor_mat="concrete", wall_mat="concrete_wall",
              ceiling_mat="riveted_metal", chr_light=9, light=light(0.24, 0.9, 300, [255, 215, 170], lamp_spacing=700))
    pads(t1, ("tunnel_1w", (2600, 7260), (5440, 7260)), ("tunnel_1e", (5300, 7260), (2440, 7260)))
    door("b2_corr", "tunnel_1", at=7260, width=240, height=260)
    t2 = room("tunnel_2", (5480, 7120, 8440, 7400), Y_B2, 300, "plant", "restricted", floor_mat="concrete", wall_mat="concrete_wall",
              ceiling_mat="riveted_metal", chr_light=8, light=light(0.2, 0.9, 300, [255, 215, 170], lamp_spacing=900))
    pads(t2, ("tunnel_2w", (5600, 7260), (8440, 7260)), ("tunnel_2e", (8300, 7260), (5480, 7260)))
    door("tunnel_2", "tunnel_1", at=7260, width=240, height=260, obj={"name": "tunnel_grate", "type": "shutter", "padlock": True})
    door("tunnel_2", "canal", at=7260, width=240, height=260, frame="stone_blocks")


def stairwell():
    """The west stair core: a switchback from the ground floor to the executive floor (gen_level.py `stairwells`)."""
    return {"name": "core_w", "rect": [WW[0], ZN2[0], WW[1], ZN2[1]], "axis": "z", "levels": [Y_G, Y_F1, Y_F2, Y_F3],
            "landing": 400, "height": 330, "stairs": 12, "door_width": 220, "floor_mat": "stone_flags", "wall_mat": "plaster",
            "ceiling_mat": "dark_grid", "riser_mat": "stone_blocks", "area": "corridor", "zone": "staff", "chr_light": 12,
            "light": light(0.42, 0.8, 360)}


def core_doors():
    lane_a, lane_b = (-4400 - 3920) // 2, (-3880 - 3400) // 2
    door("back_corr_w", "core_w_0a", at=lane_a, width=220)
    door("corr1_nw", "core_w_0b", at=lane_b, width=220)
    door("corr1_nw", "core_w_1a", at=lane_a, width=220)
    door("corr2_nw", "core_w_1b", at=lane_b, width=220)
    door("corr2_nw", "core_w_2a", at=lane_a, width=220)
    door("corr3", "core_w_2b", at=lane_b, width=220)


def main():
    outside()
    ground()
    first()
    second()
    executive()
    basement()
    core_doors()
    # which guards may open a keyed door (the coming door rule, LEVEL.objects[name].guard_access): never the vault doors; a card tier's
    # doors its own area's guards (BLUE the security wing, RED the vault level, GREEN the offices); the barracks door: the squads
    # (designs/bank-heist.md 16c: "any" | "never" | "role:<role>,..." | "barracks"; a guard's role is his post's area, or squad, vault,
    # wave, response, barracks)
    staff = "hall,mezzanine,corridor,offices,upper1,upper2,executive,security,dock,records,manager,plant"
    tiers = {0x40000000: "never",
             0x8: "role:vault_ante,deposit,vault,plant,squad,response",
             0x4: "role:security,offices,upper1,upper2,executive,wave,response",
             0x2: "role:" + staff + ",squad,wave,response"}
    never = ("cell_green", "cell_floor", "dock_shutter", "tunnel_grate")
    for d in doors:
        o = d.get("object")
        if not o or "guard_access" in o:
            continue
        kf = int(o.get("keyflags", "0"), 0) if isinstance(o.get("keyflags"), str) else int(o.get("keyflags", 0))
        if o.get("name") == "barracks_door":
            o["guard_access"] = "role:barracks,squad,wave,response"     # the reinforcements, not the wing's posted guards
        elif o.get("name") in never or o.get("padlock"):
            o["guard_access"] = "never"
        elif kf in tiers:
            o["guard_access"] = tiers[kf]
        else:
            o["guard_access"] = "any"
    for r in rooms:
        for o in r.get("objects", []):
            if o.get("type") == "safe":
                o["guard_access"] = "never"
    L = {
        "name": "levelgen",
        "comment": "Bank v2 (designs/bank-heist.md section 17): the Janus bank, St Petersburg branch, written by tools/levelgen/build_janus_bank.py "
                   "(edit that, not this file). Six levels: B2 -840 (the plant room, the maintenance tunnel to the canal), B1 -420 (the vault "
                   "level), G 0 (the street, the triple-height banking hall with its arcades, the offices, the security wing, the garage, the "
                   "dock), F1 480 (the galleries round the hall, loan offices, advisers), F2 960 (the hall's upper windows, open offices, the "
                   "server room, the library), F3 1440 (the executive floor, the roof). x east, z north.",
        "defaults": {
            "height": 360, "cell": 300, "door_width": 180, "door_height": 240, "max_wall": 200,
            "floor_mat": "white_tiles", "wall_mat": "plaster", "ceiling_mat": "office_ceiling", "chr_light": 13,
            "frame_width": 14, "frame_depth": 6,
            "light": {"ambient": 0.46, "intensity": 0.7, "radius": 420, "lamp_spacing": 520, "tint": LIGHT_OFFICE},
        },
        "materials": MATERIALS,
        "rooms": rooms,
        "stairwells": [stairwell()],
        "doors": doors,
        "openings": openings,
        "start": {"room": "street", "at": [-100, -1850], "face": [0, -1300]},
    }
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "janus_bank.json")
    with open(out, "w", newline="\n") as f:
        json.dump(L, f, indent=1)
        f.write("\n")
    print("wrote %s: %d rooms (+ the stair core), %d doorways, %d openings" % (out, len(rooms), len(doors), len(openings)))


if __name__ == "__main__":
    main()

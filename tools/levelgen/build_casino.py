#!/usr/bin/env python3
"""
build_casino.py - writes tools/levelgen/casino.json, the heist's SECOND JOB: Le Grand Casino Janus, a Monte-Carlo-style casino
(designs/job2-casino.md: the research, the design, the routes), for tools/gen_level.py. It proves the heist systems are not welded to
the bank: the same mode (tools/mode_heist.lua) runs it from its own job definition (tools/jobs/casino.lua, named by this layout's
LEVEL.script.heist.job = "casino"), with its own room, door, pad and object names.

    python3 tools/levelgen/build_casino.py                                    -> tools/levelgen/casino.json
    python3 tools/gen_level.py tools/levelgen/casino.json --out port/levelgen_casino
    cmake ... "-DGE_LEVELGEN_DIR=port/levelgen_casino" (a build dir of its own: the slot holds one level)

x east, z north, y up; units about a centimetre. Levels: B1 -420 (the house vault, the count rooms, the plant), G 0 (the place, the
atrium, the Salle Renaissance, the Salle Europe, the bar, the cage, back of house, security, the dock), F1 480 (the eye-in-the-sky
catwalks behind one-way glass over the Salle Europe, the surveillance room, the executive suite, the Salons Prives, the sea terrace).
Interior walls are 40 thick. Room `zone`: public / staff / restricted (the disguise rules); `area`: the heist's guard area tag.
The materials, lights and object helpers are the bank v3 builder's (tools/levelgen/build_janus_bank_v3.py: the game's own texture
numbers, read from your ROM at run time).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_janus_bank_v3 as B   # noqa: E402  (MATERIALS, light, prop, cam, alarm, block(s), pads, objs)

Y_B1, Y_G, Y_F1 = -420, 0, 480
light, prop, cam, alarm, block, blocks, pads, objs = B.light, B.prop, B.cam, B.alarm, B.block, B.blocks, B.pads, B.objs
WARM, COLD, SODIUM = B.LIGHT_WARM, B.LIGHT_COLD, B.LIGHT_SODIUM
WOOD, STEEL = "wood_panel", "steel_panels"

rooms, doors, openings = [], [], []


def room(name, rect, y=0, h=360, area=None, zone=None, **kw):
    r = {"name": name, "rect": list(rect), "floor": y, "height": h}
    if area:
        r["area"] = area
    if zone:
        r["zone"] = zone
    r.update(kw)
    rooms.append(r)
    return r


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


def slide(name, **kw):
    return dict({"name": name, "type": "slide", "see_through": True}, **kw)


GAMING = dict(floor_mat="red_carpet", wall_mat="pink_marble", ceiling_mat="wood_lattice")
MARBLE = dict(floor_mat="stone_flags", wall_mat="white_marble", ceiling_mat="wood_lattice")
STAFF = dict(floor_mat="white_tiles", wall_mat="plaster", ceiling_mat="office_ceiling")
SECUR = dict(floor_mat="white_tiles", wall_mat="grey_panels", ceiling_mat="office_ceiling")
VAULTM = dict(floor_mat="steel_floor", wall_mat="steel_bulkhead", ceiling_mat="riveted_metal", chr_light=12)
SERVICE = dict(floor_mat="concrete", wall_mat="concrete_wall", ceiling_mat="dark_grid", chr_light=11)
CATWALK = dict(floor_mat="grating", wall_mat="steel_pipes", ceiling_mat="pipe_grid", chr_light=10)


# ===================================================================================================================== outside ==
def outside():
    pl = room("place", (-4800, -2800, 4800, -1300), Y_G, 2000, "street", "public", open_top=True,
              floor_mat="stone_flags", wall_mat="stone_blocks", chr_light=9,
              light=light(0.28, 1.0, 420, SODIUM, lamp_height=520, lamps=[[x, -1450] for x in range(-4200, 4800, 1200)]))
    pads(pl, ("start_1", (-300, -1700), (0, -1300)), ("start_2", (-150, -1750), (0, -1300)), ("start_3", (-450, -1750), (0, -1300)),
         ("limo", (-1600, -1800), (-1600, -2100)), ("place_e", (3000, -1900), (0, -1900)), ("place_w", (-3800, -2000), (0, -1900)),
         ("patrol_perim_1", (-2500, -1600), (2500, -1600)), ("fountain_n", (0, -2200), (0, -1300)))
    objs(pl, prop("limo_car", 279, (-1600, -2150), (-1100, -2150)), prop("place_plant_1", 316, (-900, -1400), (-900, -2800)),
         prop("place_plant_2", 316, (900, -1400), (900, -2800)), cam("cam_place", "z1", 2000, 900, "start_1", (30, -30)))
    al = room("alley", (4440, -1260, 4800, 8400), Y_G, 1600, "street", "public", open_top=True,
              floor_mat="asphalt", wall_mat="brick", chr_light=8,
              light=light(0.22, 1.0, 360, SODIUM, lamp_height=480, lamps=[[4620, z] for z in range(-600, 8400, 1800)]))
    pads(al, ("alley_s", (4620, -1000), (4620, 4000)), ("alley_dock", (4620, 2000), (4400, 2000)), ("alley_n", (4620, 8200), (4620, 0)),
         ("patrol_perim_2", (4620, 3000), (4620, 8000)), ("alley_garden", (4620, 7700), (4400, 7700)))
    objs(al, prop("alley_bins", 114, (4700, 1000), (4400, 1000)), cam("cam_alley", "x0", 1400, 500, "alley_dock", (25, -25)))
    door("place", "alley", at=4620, width=300, height=320)
    # the sea garden under the terrace (the grapple up to the Salons Prives' terrace starts here)
    gd = room("garden", (-2400, 7040, 4400, 8400), Y_G, 1600, "street", "public", open_top=True,
              floor_mat="stone_flags", wall_mat="stone_blocks", chr_light=8,
              light=light(0.22, 1.0, 380, SODIUM, lamp_height=420, lamps=[[x, 7700] for x in range(-2000, 4400, 1300)]))
    pads(gd, ("garden_mid", (1000, 7700), (1000, 7040)), ("grapple_terrace_low", (0, 7200), (0, 7040)),
         ("patrol_perim_3", (-1800, 7800), (4000, 7800)))
    objs(gd, prop("garden_palm_1", 317, (-2000, 8200), (0, 8200)), prop("garden_palm_2", 317, (2600, 8200), (0, 8200)))
    door("alley", "garden", at=7700, width=300, height=320)


# ===================================================================================================================== ground =====
def ground():
    at = room("atrium", (-800, -1260, 800, -460), Y_G, 700, "floor", "public", **MARBLE, chr_light=14, light=light(0.52, 0.8, 420, WARM))
    pads(at, ("atrium_mid", (0, -860), (0, -460)), ("atrium_w", (-600, -700), (0, -700)), ("cafe_1", (500, -1000), (0, -1000)))
    objs(at, cam("cam_atrium", "z0", 600, 500, "atrium_mid", (25, -25)), prop("cafe_table_1", 85, (-550, -1050), (0, -1050)),
         prop("cafe_table_2", 85, (500, -1050), (0, -1050)))
    door("place", "atrium", at=0, width=280, height=300, frame="stone_blocks",
         obj={"name": "casino_doors", "type": "slide", "double": True, "model": "door", "see_through": True})
    # the Salle Renaissance: the first gaming room (slots), the anteroom to the Salle Europe (the Monte-Carlo order of rooms)
    sr = room("salle_renaissance", (-1800, -420, 1800, 1000), Y_G, 520, "floor", "public", **GAMING, light=light(0.5, 0.75, 460, WARM))
    for k, x in enumerate((-400, 400)):
        door("atrium", "salle_renaissance", name="atrium-renaissance-%d" % k, at=x, width=180, height=280, frame="stone_blocks",
             obj={"name": "grille_%s" % ("w" if x < 0 else "e"), "type": "shutter", "open": True, "autoclose": 268435455})
    blocks(sr, *[block("slot_row_%d" % (k + 1), (x - 40, 100, x + 40, 700), 150, "steel_panels", "steel_panels") for k, x in enumerate((-900, 900))])
    pads(sr, ("floor_entry_w", (-400, -250), (-400, 1000)), ("floor_entry_e", (400, -250), (400, 1000)), ("slots_1", (-1200, 400), (-900, 400)),
         ("slots_2", (1200, 400), (900, 400)), ("slots_3", (-600, 400), (-900, 400)), ("slots_4", (600, 400), (900, 400)),
         ("ren_mid", (0, 400), (0, 1000)), ("ren_post_w", (-1500, -200), (0, 400)), ("ren_post_e", (1500, -200), (0, 400)),
         ("cage_queue", (-1600, 600), (-1800, 600)), ("patrol_floor_1", (0, -200), (0, 1000)), ("patrol_floor_6", (-1300, 850), (0, 850)))
    objs(sr, cam("cam_renaissance", "z0", -1500, 420, "ren_mid", (30, -30)), cam("cam_cage_window", "z1", -1500, 420, "cage_queue", (15, -15)),
         *[prop("slot_%d" % (k + 1), 29 if k % 2 else 31, (x, z), (x + (300 if x < 0 else -300), z))
           for k, (x, z) in enumerate([(-950, 200), (-950, 600), (950, 200), (950, 600)])])
    # the Salle Europe: the main gaming room (roulette, trente et quarante, punto banco), 11 m high; the pit in the middle; the
    # pit podium's floor monitors on its south-west wall (the camera feeds: the casino's "screens"); one-way glass high in its walls,
    # the catwalks behind it (the eye in the sky, the 1950s-80s way: research section 2)
    se = room("salle_europe", (-1800, 1040, 1800, 3400), Y_G, 1100, "floor", "public", **GAMING, chr_light=14,
              light=light(0.5, 0.8, 700, WARM, lamp_spacing=800, lamp_height=700))
    for k, x in enumerate((-1100, 0, 1100)):
        door("salle_renaissance", "salle_europe", name="arch_%d" % (k + 1), at=x, width=400, height=380, frame="stone_blocks")
    tables = [(-1000, 1700), (0, 1700), (1000, 1700), (-1000, 2700), (1000, 2700)]
    blocks(se, *[block("table_%d" % (k + 1), (x - 220, z - 120, x + 220, z + 120), 80, WOOD, "red_carpet") for k, (x, z) in enumerate(tables)],
           block("pit_podium", (-120, 2150, 120, 2350), 110, WOOD, "white_marble"))
    pads(se, ("croupier_1", (-1000, 1900), (-1000, 1700)), ("croupier_2", (0, 1900), (0, 1700)), ("croupier_3", (1000, 1900), (1000, 1700)),
         ("croupier_4", (-1000, 2900), (-1000, 2700)), ("croupier_5", (1000, 2900), (1000, 2700)),
         ("player_1", (-1300, 1500), (-1000, 1700)), ("player_2", (-700, 1500), (-1000, 1700)), ("player_3", (300, 1500), (0, 1700)),
         ("player_4", (1300, 1500), (1000, 1700)), ("player_5", (-1300, 2500), (-1000, 2700)), ("player_6", (700, 2500), (1000, 2700)),
         ("player_7", (-300, 3100), (0, 2200)), ("player_8", (1300, 3100), (1000, 2700)),
         ("pit_mid", (0, 2050), (0, 1040)), ("pit_n", (0, 2500), (0, 3400)), ("floor_post_sw", (-1550, 1300), (0, 2200)),
         ("floor_post_se", (1550, 1300), (0, 2200)), ("floor_post_nw", (-1550, 3200), (0, 2200)), ("floor_post_ne", (1550, 3200), (0, 2200)),
         ("pit_screens", (-1600, 1260), (-1600, 1040)),
         ("patrol_floor_2", (-500, 2200), (500, 2200)), ("patrol_floor_3", (500, 3150), (500, 2200)), ("patrol_floor_4", (1500, 2200), (0, 2200)),
         ("patrol_floor_5", (-1500, 2200), (0, 2200)),
         ("grapple_catwalk_low", (-1650, 2350), (-1800, 2350)))
    objs(se, cam("cam_floor_s", "z0", 1500, 900, "pit_mid", (35, -35)), cam("cam_floor_n", "z1", -1500, 900, "croupier_4", (30, -30)),
         cam("cam_pit", "x1", 2200, 900, "croupier_2", (30, -30)), alarm("alarm_floor", "z1", 1500),
         *[{"type": "monitor", "name": "pit_screen_%d" % (k + 1), "wall": "z0", "along": (-1720, -1600, -1480)[k % 3],
            "y": 150 if k < 3 else 232, "width": 100, "height": 72, "image": (15, 11, 3, 16, 9, 4)[k]} for k in range(6)])
    # the bar (Le Bar Americain): public, off the Salle Renaissance
    ba = room("bar", (1840, -420, 3400, 1000), Y_G, 420, "floor", "public", floor_mat="planks", wall_mat=WOOD, ceiling_mat="wood_lattice",
              chr_light=12, light=light(0.42, 0.7, 380, [255, 225, 190]))
    blocks(ba, block("bar_counter", (2900, -200, 3000, 800), 110, WOOD, "white_marble"))
    pads(ba, ("bar_1", (2600, 0), (3000, 0)), ("bar_2", (2400, 700), (1840, 300)), ("barman", (3200, 300), (2900, 300)))
    objs(ba, cam("cam_bar", "x0", 600, 320, "bar_1", (20, -20)), prop("bar_table", 85, (2200, 0), (2200, 1000)))
    door("salle_renaissance", "bar", at=300, width=300, height=300, frame=WOOD)
    # ---- the cage (the cashier cage: chips for cash; windows onto the Salle Renaissance, a mantrap from back of house: research 1)
    cg = room("cage", (-3400, -420, -1840, 1000), Y_G, 360, "cage", "restricted", floor_mat="white_tiles", wall_mat="steel_panels",
              ceiling_mat="office_ceiling", chr_light=13, light=light(0.5, 0.75, 400, COLD))
    blocks(cg, block("cage_counter", (-2060, -300, -1980, 900), 105, "steel_panels", "steel_panels"))
    pads(cg, ("cashier_1", (-2200, 100), (-1840, 100)), ("cashier_2", (-2200, 600), (-1840, 600)), ("cage_mid", (-2800, 300), (-1840, 300)),
         ("cage_back", (-3200, 800), (-1840, 300)), ("cage_post", (-2800, -200), (-2800, 1000)))
    objs(cg, cam("cam_cage", "z0", -2600, 300, "cage_mid", (25, -25)), cam("cam_cage_n", "z1", -3100, 300, "cashier_1", (20, -20)),
         alarm("alarm_cage", "x0", 800))
    cg["loot"] = [{"name": "cage_cash", "at": [[-3000, 0], [-2700, 0]]}]
    for z in (100, 600):
        opening("cage", "salle_renaissance", z, 300, (110, 250), name="cage_window_%d" % z, frame=STEEL)
    cb = room("chip_bank", (-4400, -420, -3440, 400), Y_G, 300, "cage", "restricted", floor_mat="steel_floor", wall_mat="dark_tiles",
              ceiling_mat="riveted_metal")
    cb["loot"] = [{"name": "chip_bank_cash", "at": [[-4100, -200], [-3700, -200], [-3900, 200]]}]
    door("cage", "chip_bank", at=0, width=140, height=220, obj={"name": "chip_bank_door", "type": "slide", "model": "cell", "keyflags": "0x2"})
    mt = room("cage_mantrap", (-3400, 1040, -2280, 1600), Y_G, 360, "cage", "restricted", **SECUR)
    pads(mt, ("mantrap_post", (-2840, 1350), (-2280, 1350)))
    objs(mt, cam("cam_mantrap", "x0", 1320, 280, "mantrap_post", (15, -15)))
    door("cage_mantrap", "cage", at=-2840, width=180, frame=STEEL, obj={"name": "cage_inner", "type": "slide", "keyflags": "0x2"})
    # ---- back of house (staff): the staff corridor behind the west wall, the lounge, the lockers (the count key), the pit office
    sc = room("staff_corr", (-2240, 1040, -1840, 3400), Y_G, 360, "corridor", "staff", **STAFF)
    pads(sc, ("staff_s", (-2040, 1150), (-2040, 3400)), ("staff_mid", (-2040, 2200), (-2040, 3400)), ("staff_n", (-2040, 3250), (-2040, 1040)))
    objs(sc, cam("cam_staff", "z0", -2040, 300, "staff_mid", (10, -10)), alarm("alarm_staff", "x0", 2200))
    door("salle_europe", "staff_corr", at=2900, width=160, frame=WOOD, obj=slide("staff_door"))
    door("staff_corr", "cage_mantrap", at=1320, width=180, frame=STEEL, obj={"name": "cage_outer", "type": "slide", "keyflags": "0x2"})
    lg = room("staff_lounge", (-3400, 1640, -2280, 2400), Y_G, 360, "corridor", "staff", floor_mat="red_carpet", wall_mat="beige_panels",
              ceiling_mat="office_ceiling")
    pads(lg, ("lounge_1", (-2600, 1800), (-3400, 2000)), ("lounge_2", (-3200, 2250), (-2280, 2000)),
         ("vent_count_a", (-3300, 1750), (-3400, 1750)))      # the old cash-chute duct down to the soft count room
    objs(lg, prop("lounge_table", 85, (-2840, 2000), (-2840, 1640)))
    door("staff_corr", "staff_lounge", at=2020, width=160, frame=WOOD, obj=slide("lounge_door"))
    lk = room("lockers", (-3400, 2440, -2280, 3400), Y_G, 360, "corridor", "staff", **SECUR)
    pads(lk, ("lockers_1", (-2600, 2600), (-3400, 2600)), ("lockers_2", (-3100, 3250), (-2280, 2900)))
    objs(lk, *[prop("locker_%d" % (k + 1), 96 if k % 2 else 97, (-3350, 2550 + 180 * k), (-2280, 2550 + 180 * k)) for k in range(4)],
         {"type": "key", "name": "count_key", "at": [-2500, 3250], "keyflags": "0x1", "model": "key"})
    door("staff_corr", "lockers", at=2900, width=160, frame=WOOD, obj=slide("lockers_door"))
    po = room("pit_office", (-4400, 1040, -3440, 2400), Y_G, 360, "offices", "staff", floor_mat="red_carpet", wall_mat=WOOD,
              ceiling_mat="office_ceiling", light=light(0.44, 0.7, 380, [255, 236, 210]))
    pads(po, ("pit_office_1", (-3700, 1300), (-4400, 1300)), ("pit_office_2", (-4100, 2100), (-3440, 1700)), ("pit_office_mid", (-3900, 1700), (-3440, 1700)))
    objs(po, cam("cam_pit_office", "z1", -4250, 280, "pit_office_mid", (20, -20)), prop("pit_desk", 38, (-4300, 1700), (-3440, 1700)),
         prop("pit_cabinet", 43, (-4100, 1090), (-4100, 2400)))
    door("staff_lounge", "pit_office", at=1800, width=160, frame=WOOD, obj={"name": "pit_office_door", "type": "slide", "keyflags": "0x2"})
    # ---- the north range: the staff corridors either side of the grand stair's foot, the kitchen (and the goods lift), the lounge,
    # the RED stairs down to the house vault, the security stair core up to the surveillance floor
    nw = room("north_corr_w", (-4400, 3440, -540, 3840), Y_G, 360, "corridor", "staff", **STAFF)
    pads(nw, ("nw_w", (-4200, 3640), (-540, 3640)), ("nw_mid", (-2400, 3640), (-540, 3640)), ("nw_e", (-800, 3640), (-4400, 3640)))
    objs(nw, cam("cam_north_w", "x0", 3640, 280, "nw_mid", (8, -8)))
    door("staff_corr", "north_corr_w", at=-2040, width=180, frame=WOOD, obj=slide("staff_north"))
    sf = room("stair_foot", (-500, 3440, 500, 3840), Y_G, 360, "salons", "public", **MARBLE)
    pads(sf, ("stair_foot_mid", (0, 3640), (0, 4600)))
    door("salle_europe", "stair_foot", at=0, width=760, height=320, frame="stone_blocks")
    door("north_corr_w", "stair_foot", at=3640, width=160, frame=WOOD, obj=slide("salons_staff_w"))
    gs = room("grand_stair", (-500, 3880, 500, 4600), Y_G, 440, "salons", "public", ramp={"axis": "z", "to": Y_F1, "stairs": 20, "riser_mat": "pink_marble"},
              floor_mat="red_carpet", wall_mat="white_marble", ceiling_mat="wood_lattice", chr_light=13, light=light(0.48, 0.75, 420, WARM))
    pads(gs, ("grand_bot", (0, 3980), (0, 4600)), ("grand_top", (0, 4500), (0, 3880)))
    door("stair_foot", "grand_stair", at=0, width=760, height=320)
    kt = room("kitchen", (-3400, 3880, -1840, 5000), Y_G, 360, "corridor", "staff", floor_mat="white_tiles", wall_mat="dark_tiles",
              ceiling_mat="office_ceiling")
    blocks(kt, block("kitchen_counter", (-3000, 4200, -2400, 4300), 96, "steel_panels", "steel_panels"))
    pads(kt, ("kitchen_1", (-3200, 4000), (-1840, 4000)), ("kitchen_2", (-2000, 4500), (-3400, 4500)),
         ("grapple_goodslift_high", (-2240, 4880), (-2240, 5000)))
    door("north_corr_w", "kitchen", at=-2600, width=200, frame=WOOD, obj=slide("kitchen_door"))
    ne = room("north_corr_e", (540, 3440, 4400, 3840), Y_G, 360, "corridor", "staff", **STAFF)
    pads(ne, ("ne_w", (700, 3640), (4400, 3640)), ("ne_mid", (1900, 3640), (4400, 3640)), ("ne_e", (4200, 3640), (540, 3640)),
         ("ne_core", (2520, 3640), (2520, 3840)))
    objs(ne, cam("cam_north_e", "x1", 3640, 280, "ne_mid", (8, -8)))
    door("stair_foot", "north_corr_e", at=3640, width=160, frame=WOOD, obj=slide("salons_staff_e"))
    sl = room("staff_canteen", (980, 3880, 1800, 5000), Y_G, 360, "corridor", "staff", floor_mat="white_tiles", wall_mat="beige_panels",
              ceiling_mat="office_ceiling")
    pads(sl, ("canteen_1", (1400, 4100), (1400, 5000)), ("canteen_2", (1600, 4800), (980, 4400)))
    objs(sl, prop("canteen_table", 85, (1390, 4450), (1390, 3880)))
    door("north_corr_e", "staff_canteen", at=1390, width=160, frame=WOOD, obj=slide("canteen_door"))
    rs = room("red_stairs", (540, 3880, 940, 5000), Y_G, 360, "vault_ante", "restricted", ramp={"axis": "z", "to": Y_B1, "stairs": 20, "riser_mat": "stone_blocks"},
              floor_mat="stone_flags", wall_mat="stone_blocks", ceiling_mat="dark_grid", chr_light=12, light=light(0.4, 0.8, 360))
    pads(rs, ("stair_top", (740, 3980), (740, 5000)), ("stair_bot", (740, 4900), (740, 3880)))
    door("north_corr_e", "red_stairs", at=740, width=240, height=260, frame=STEEL,
         obj={"name": "vault_stairs_door", "type": "slide", "double": True, "model": "vault", "keyflags": "0x8"})
    # ---- the east wing (BLUE): security, the guard room, the armoury, the barracks (the squads), the dock
    sq = room("security_corr", (1840, 1040, 2240, 3400), Y_G, 360, "security", "restricted", **SECUR)
    pads(sq, ("sec_s", (2040, 1150), (2040, 3400)), ("sec_mid", (2040, 2200), (2040, 3400)), ("sec_n", (2040, 3250), (2040, 1040)))
    objs(sq, cam("cam_security", "z1", 2040, 300, "sec_mid", (10, -10)))
    door("salle_europe", "security_corr", at=2900, width=200, frame=WOOD,
         obj={"name": "security_door", "type": "slide", "double": True, "keyflags": "0x4"})
    door("security_corr", "north_corr_e", at=2040, width=180, frame=WOOD, obj={"name": "security_back", "type": "slide", "keyflags": "0x4"})
    gr = room("guard_room", (2280, 1040, 3400, 2000), Y_G, 360, "security", "restricted", **SECUR)
    pads(gr, ("guard_1", (2500, 1300), (3400, 1500)), ("guard_2", (3200, 1300), (2280, 1500)), ("guard_3", (2840, 1850), (2840, 1040)))
    objs(gr, prop("guard_table", 85, (2840, 1500), (2840, 2000)), alarm("alarm_security", "x1", 1500))
    door("security_corr", "guard_room", at=1500, width=180, frame=WOOD, obj=slide("guard_room_door"))
    ar = room("armory", (2280, 2040, 3400, 2600), Y_G, 360, "security", "restricted", floor_mat="steel_floor", wall_mat="diamond_plate",
              ceiling_mat="riveted_metal", chr_light=12)
    pads(ar, ("armory_1", (2840, 2300), (3400, 2300)))
    objs(ar, prop("ammo_1", 5, (3300, 2200), (2280, 2200)), prop("ammo_2", 7, (3300, 2450), (2280, 2450)))
    door("security_corr", "armory", at=2300, width=160, frame=STEEL, obj={"name": "armory_door", "type": "slide", "keyflags": "0x4"})
    bk = room("barracks", (2280, 2640, 3400, 3400), Y_G, 360, "security", "restricted", **SECUR)
    pads(bk, ("barracks_1", (2500, 2800), (3400, 3000)), ("barracks_2", (3200, 2800), (2280, 3000)), ("barracks_3", (2840, 3250), (2840, 2640)))
    objs(bk, *[prop("bunk_locker_%d" % (k + 1), 96, (3350, 2750 + 180 * k), (2280, 2750 + 180 * k)) for k in range(3)])
    door("security_corr", "barracks", at=3000, width=160, frame=WOOD, obj=slide("barracks_door"))
    dk = room("dock", (3440, 1040, 4400, 3400), Y_G, 440, "dock", "staff", floor_mat="concrete", wall_mat="black_corrugated",
              ceiling_mat="rusty_roof", chr_light=11, light=light(0.34, 0.85, 440))
    pads(dk, ("dock_mid", (3900, 2000), (4400, 2000)), ("dock_exit", (4250, 2000), (4400, 2000)), ("dock_1", (3600, 3200), (4400, 1400)),
         ("dock_2", (4250, 1300), (3440, 2800)), ("dock_3", (3600, 1300), (4400, 3200)))
    objs(dk, cam("cam_dock", "z0", 4200, 380, "dock_mid"), alarm("alarm_dock", "x0", 1500),
         prop("dock_crate_1", 79, (3550, 2400), (4400, 2400)), prop("dock_crate_2", 80, (3550, 2650), (4400, 2650)),
         prop("cash_van", 279, (3800, 1600), (3800, 3400)))
    door("alley", "dock", at=2000, width=300, height=300, obj={"name": "dock_shutter", "type": "shutter", "padlock": True})
    door("north_corr_e", "dock", at=3900, width=200, frame=STEEL, obj=slide("dock_door"))


# ===================================================================================================================== F1 ========
def first():
    # the eye in the sky: catwalks behind one-way glass high over the Salle Europe (west, north, east), walked by the surveillance
    # guards; they look down through the glass (openings) into the room; the piton from the floor to the west catwalk
    cw = room("catwalk_w", (-2240, 1040, -1840, 3400), Y_F1, 400, "catwalk", "restricted", **CATWALK, light=light(0.24, 0.8, 300, COLD, lamp_spacing=700))
    pads(cw, ("cw_s", (-2040, 1150), (-2040, 3400)), ("patrol_catwalk_1", (-2040, 1700), (-2040, 3400)), ("grapple_catwalk_high", (-2040, 2350), (-1840, 2350)),
         ("patrol_catwalk_2", (-2040, 3250), (2040, 3640)))
    for z in (1500, 2350, 3100):
        opening("catwalk_w", "salle_europe", z, 360, (Y_F1 + 100, Y_F1 + 300), name="mirror_w_%d" % z, frame=STEEL)
    ce = room("catwalk_e", (1840, 1040, 2240, 3400), Y_F1, 400, "catwalk", "restricted", **CATWALK, light=light(0.24, 0.8, 300, COLD, lamp_spacing=700))
    pads(ce, ("ce_s", (2040, 1150), (2040, 3400)), ("patrol_catwalk_4", (2040, 1700), (2040, 3400)), ("ce_mid", (2040, 2600), (2040, 1040)))
    for z in (1500, 2350, 3100):
        opening("catwalk_e", "salle_europe", z, 360, (Y_F1 + 100, Y_F1 + 300), name="mirror_e_%d" % z, frame=STEEL)
    cn = room("catwalk_n", (-2240, 3440, 2240, 3840), Y_F1, 400, "catwalk", "restricted", **CATWALK, light=light(0.24, 0.8, 300, COLD, lamp_spacing=700))
    pads(cn, ("cn_w", (-1800, 3640), (2240, 3640)), ("patrol_catwalk_3", (0, 3640), (2240, 3640)), ("cn_e", (1800, 3640), (-2240, 3640)))
    objs(cn, cam("cam_catwalk", "x0", 3640, 340, "patrol_catwalk_3", (6, -6)))
    for x in (-1000, 1000):
        opening("catwalk_n", "salle_europe", x, 400, (Y_F1 + 100, Y_F1 + 300), name="mirror_n_%d" % x, frame=STEEL)
    door("catwalk_w", "catwalk_n", at=-2040, width=180, frame=STEEL)
    door("catwalk_e", "catwalk_n", at=2040, width=180, frame=STEEL)
    # the surveillance room (the security booth with every camera: the console, a wall of monitors), its corridor off the stair core
    sv = room("surv_corr", (2280, 3440, 4400, 3840), Y_F1, 360, "security", "restricted", **SECUR)
    pads(sv, ("surv_corr_w", (2450, 3640), (4400, 3640)), ("surv_corr_mid", (3040, 3640), (3040, 3840)), ("surv_corr_e", (4200, 3640), (2280, 3640)))
    door("catwalk_n", "surv_corr", at=3640, width=180, frame=STEEL, obj=slide("catwalk_gate"))
    su = room("surveillance", (2280, 1040, 3400, 3400), Y_F1, 360, "security", "restricted", floor_mat="white_tiles", wall_mat="grey_panels",
              ceiling_mat="office_ceiling", chr_light=11, light=light(0.36, 0.7, 380, COLD))
    blocks(su, block("monitor_desk", (2600, 1900, 3100, 2000), 90, "steel_panels", "steel_panels"))
    pads(su, ("console", (3200, 2900), (3400, 2900)), ("surv_mid", (2840, 2400), (2840, 3400)), ("surv_1", (2500, 1700), (2840, 1900)),
         ("surv_2", (3100, 1700), (2840, 1900)), ("surv_3", (2500, 3100), (3400, 3100)))
    objs(su, cam("cam_surveillance", "z0", 2500, 280, "surv_mid", (20, -20)), alarm("alarm_surv", "z1", 2500),
         prop("console_desk", 25, (3320, 2900), (3000, 2900)),
         *[{"type": "monitor", "name": "surv_screen_%d" % (k + 1), "wall": "x1", "along": 1300 + 120 * (k % 6), "y": 170 if k < 6 else 250,
            "width": 100, "height": 72, "image": (11, 15, 9, 3, 16, 4, 10, 15, 11, 9, 3, 16)[k]} for k in range(12)])
    door("surv_corr", "surveillance", at=2840, width=200, frame=WOOD, obj={"name": "surveillance_door", "type": "slide", "keyflags": "0x4"})
    door("catwalk_e", "surveillance", at=2600, width=160, frame=STEEL, obj=slide("surv_catwalk_door"))
    # the executive suite: the casino director's office (RED: he carries the vault stairs' card), off the surveillance corridor
    ec = room("exec_corr", (3440, 2440, 4400, 3400), Y_F1, 360, "executive", "staff", floor_mat="red_carpet", wall_mat=WOOD, ceiling_mat="wood_lattice")
    pads(ec, ("exec_mid", (3920, 2900), (3920, 3400)), ("secretary_1", (4200, 2600), (3440, 2900)))
    objs(ec, cam("cam_exec", "z0", 3700, 300, "exec_mid", (20, -20)), prop("secretary_desk", 38, (4300, 3100), (3440, 3100)))
    door("surv_corr", "exec_corr", at=3920, width=200, frame=WOOD, obj={"name": "exec_door", "type": "slide", "keyflags": "0x4"})
    dr = room("director", (3440, 1040, 4400, 2400), Y_F1, 360, "executive", "staff", floor_mat="red_carpet", wall_mat=WOOD, ceiling_mat="wood_lattice",
              light=light(0.42, 0.7, 380, [255, 236, 205]))
    pads(dr, ("director_desk", (3920, 1400), (3920, 2400)), ("director_1", (3700, 2100), (4400, 1400)))
    objs(dr, prop("director_desk_prop", 38, (3920, 1200), (3920, 2400)), prop("director_plant", 317, (4300, 2300), (3440, 2300)))
    door("exec_corr", "director", at=3920, width=160, frame=WOOD, obj={"name": "director_door", "type": "slide", "keyflags": "0x4"})
    # the Salons Prives: up the grand stair, the high rollers' rooms (the Salle Blanche and its terrace over the sea, the private
    # salon with the house's high-roller safe: GOLD)
    sl = room("salons_lobby", (-1800, 4640, 1800, 5000), Y_F1, 400, "salons", "staff", **MARBLE)
    pads(sl, ("lobby_mid", (0, 4820), (0, 4640)), ("lobby_w", (-1500, 4820), (1800, 4820)), ("lobby_e", (1500, 4820), (-1800, 4820)))
    objs(sl, cam("cam_salons", "x0", 4820, 320, "lobby_mid", (10, -10)), alarm("alarm_salons", "z1", 0))
    door("grand_stair", "salons_lobby", at=0, width=760, height=320, frame=WOOD)
    sp = room("salon_prive", (-1800, 5040, -40, 6200), Y_F1, 420, "salons", "staff", floor_mat="red_carpet", wall_mat=WOOD,
              ceiling_mat="wood_lattice", chr_light=12, light=light(0.42, 0.7, 400, WARM))
    blocks(sp, block("prive_table", (-1100, 5500, -700, 5750), 80, WOOD, "red_carpet"))
    pads(sp, ("prive_1", (-1400, 5300), (-900, 5620)), ("prive_2", (-400, 5300), (-900, 5620)), ("prive_safe_front", (-1500, 5980), (-1500, 6200)),
         ("vent_salon_b", (-300, 6100), (-900, 5600)))
    objs(sp, {"type": "safe", "name": "house_safe", "at": [-1500, 6110], "face": [-1500, 5700], "keyflags": "0x10"},
         cam("cam_prive", "z0", -300, 340, "prive_1", (20, -20)))
    sp["loot"] = [{"name": "house_safe_loot", "at": [[-1500, 5900]]}]
    door("salons_lobby", "salon_prive", at=-900, width=200, frame=WOOD, obj=slide("prive_door"))
    sb = room("salle_blanche", (0, 5040, 1800, 6200), Y_F1, 420, "salons", "public", floor_mat="red_carpet", wall_mat="white_marble",
              ceiling_mat="wood_lattice", chr_light=13, light=light(0.48, 0.75, 420, WARM))
    blocks(sb, block("blanche_table", (700, 5500, 1100, 5750), 80, WOOD, "red_carpet"))
    pads(sb, ("whale_1", (600, 5400), (900, 5620)), ("whale_2", (1300, 5400), (900, 5620)), ("whale_3", (900, 5950), (900, 5620)),
         ("blanche_post", (200, 5200), (900, 5620)))
    objs(sb, cam("cam_blanche", "x1", 5300, 340, "whale_1", (25, -25)))
    door("salons_lobby", "salle_blanche", at=900, width=200, frame=WOOD)
    te = room("terrace", (-1800, 6240, 1800, 7000), Y_F1, 900, "salons", "public", open_top=True, floor_mat="stone_flags", wall_mat="stone_blocks",
              chr_light=9, light=light(0.24, 1.0, 380, SODIUM, lamp_height=380, lamps=[[-1200, 6600], [0, 6600], [1200, 6600]]))
    pads(te, ("terrace_mid", (0, 6500), (0, 7000)), ("grapple_terrace_high", (0, 6900), (0, 6240)), ("vent_salon_a", (-300, 6350), (-300, 6240)))
    door("salle_blanche", "terrace", at=900, width=260, height=280, frame="stone_blocks")


# ===================================================================================================================== B1 ========
def basement():
    # the house vault (the casino's main bank: cash, and the house's gold reserve), its antechamber, the count rooms (soft count:
    # notes; hard count: chips and coin; research 1: dedicated cameras, two-person rule), the drop-box cage, the goods-lift shaft, the
    # plant room
    vc = room("vault_corr", (-2000, 5040, 3400, 5440), Y_B1, 360, "vault_ante", "restricted", **VAULTM, light=light(0.4, 0.75, 400, COLD))
    pads(vc, ("vc_w", (-1800, 5240), (3400, 5240)), ("vc_mid", (0, 5240), (3400, 5240)), ("vc_e", (3200, 5240), (-2000, 5240)),
         ("patrol_vault_1", (740, 5240), (-2000, 5240)), ("patrol_vault_2", (-1000, 5240), (-2000, 5240)), ("patrol_vault_3", (-1800, 5240), (3400, 5240)),
         ("patrol_vault_4", (2200, 5240), (3400, 5240)))
    objs(vc, cam("cam_vault_corr", "x1", 5240, 300, "vc_mid", (6, -6)))
    door("red_stairs", "vault_corr", at=740, width=240, height=260)
    an = room("vault_ante", (-600, 5480, 600, 6200), Y_B1, 360, "vault_ante", "restricted", **VAULTM, light=light(0.42, 0.75, 420, COLD))
    pads(an, ("crack_point", (0, 6050), (0, 6200)), ("ante_1", (-450, 5600), (0, 6200)), ("ante_2", (450, 5600), (0, 6200)),
         ("ante_3", (-450, 6050), (600, 5800)), ("ante_4", (450, 6050), (-600, 5800)))
    objs(an, cam("cam_ante", "z0", 450, 300, "crack_point", (25, -25)), alarm("alarm_vault", "x0", 5800))
    door("vault_corr", "vault_ante", at=0, width=240, height=260, frame=STEEL)
    va = room("house_vault", (-1000, 6240, 1000, 7400), Y_B1, 400, "vault", "restricted", floor_mat="steel_floor", wall_mat="steel_panels",
              ceiling_mat="riveted_metal", chr_light=12, light=light(0.45, 0.7, 440, COLD))
    door("vault_ante", "house_vault", at=0, width=240, height=270, frame={"mat": STEEL, "width": 24, "depth": 10},
         obj={"name": "house_vault_door", "type": "slide", "double": True, "model": "vault", "keyflags": "0x40000000", "autoclose": 268435455})
    pads(va, ("vault_in_a", (0, 6360), (0, 7400)), ("vault_in_b", (880, 6820), (-1000, 6820)), ("vault_manager", (0, 7280), (0, 6240)),
         ("vault_g1", (-850, 6400), (1000, 7300)), ("vault_g2", (-850, 7280), (1000, 6400)), ("vault_g3", (850, 7300), (-1000, 6400)),
         ("vent_vault_b", (-880, 6820), (1000, 6820)))
    objs(va, prop("vault_crate_1", 51, (-930, 6600), (1000, 6600)), prop("vault_crate_2", 52, (-930, 7050), (1000, 7050)),
         prop("vault_crate_3", 79, (-500, 7340), (-500, 6240)), prop("vault_crate_4", 80, (500, 7340), (500, 6240)))
    va["loot"] = [{"name": "house_reserve", "at": [[x, z] for z in (6550, 6820, 7090) for x in (-450, 0, 450) if (x, z) != (0, 6820)]}]
    hc = room("hard_count", (1040, 6240, 2240, 7400), Y_B1, 360, "vault_ante", "restricted", floor_mat="white_tiles", wall_mat="steel_panels",
              ceiling_mat="office_ceiling", chr_light=12)
    blocks(hc, block("chip_sorter", (1400, 6900, 1900, 7000), 100, "steel_panels", "steel_panels"))
    pads(hc, ("hard_1", (1200, 6400), (2240, 6820)), ("hard_2", (2080, 7250), (1040, 6820)), ("hard_mid", (1640, 6600), (1040, 6820)))
    objs(hc, cam("cam_hard_count", "x1", 6500, 300, "hard_mid", (25, -25)))
    door("house_vault", "hard_count", at=6820, width=220, height=270, frame={"mat": STEEL, "width": 24, "depth": 10},
         obj={"name": "count_vault_door", "type": "slide", "double": True, "model": "vault", "keyflags": "0x40000000", "autoclose": 268435455})
    so = room("soft_count", (640, 5480, 2240, 6200), Y_B1, 360, "vault_ante", "restricted", floor_mat="white_tiles", wall_mat="steel_panels",
              ceiling_mat="office_ceiling", chr_light=12)
    blocks(so, block("count_table", (1100, 5750, 1800, 5900), 90, "steel_panels", "steel_panels"))
    pads(so, ("count_1", (800, 5600), (2240, 6000)), ("count_2", (2100, 6100), (640, 5600)), ("vent_count_b", (2100, 5560), (2240, 5560)))
    objs(so, cam("cam_soft_count", "x0", 5650, 300, "count_1", (25, -25)), alarm("alarm_count", "z0", 1000),
         prop("count_console", 29, (690, 5900), (2240, 5900)))
    door("vault_corr", "soft_count", at=1440, width=180, frame=STEEL, obj=slide("soft_count_door"))
    door("soft_count", "hard_count", at=1640, width=180, frame=STEEL)
    dc = room("drop_cage", (2280, 5480, 2880, 6200), Y_B1, 300, "vault_ante", "restricted", floor_mat="steel_floor", wall_mat="dark_tiles",
              ceiling_mat="riveted_metal")
    dc["loot"] = [{"name": "drop_cash", "at": [[2580, 5650], [2580, 6050], [2750, 5850]]}]
    door("soft_count", "drop_cage", at=5840, width=140, height=220, obj={"name": "drop_cage_door", "type": "slide", "model": "cell", "keyflags": "0x1"})
    # the goods lift (Hatton Garden 2015): a shaft from the kitchen's service landing down to the vault corridor, its car parked up,
    # its gate at the bottom rusted shut to the guards; a rope (grapple_goodslift_*) from the landing to the shaft's floor
    ls = room("goods_lift", (-2440, 5040, -2040, 5440), Y_B1, 820, "vault_ante", "restricted", **SERVICE, light=light(0.22, 0.8, 300))
    pads(ls, ("grapple_goodslift_low", (-2240, 5300), (-2240, 5040)), ("shaft_mid", (-2240, 5200), (-2240, 5440)))
    opening("kitchen", "goods_lift", -2240, 240, (Y_G + 20, Y_G + 250), name="lift_landing", frame=STEEL)
    door("goods_lift", "vault_corr", at=5240, width=180, height=240, frame=STEEL,
         obj={"name": "lift_gate", "type": "slide", "model": "cell", "guard_access": "never"})
    # the plant (the power cut: the main breaker, the backup generator)
    pc = room("plant_corr", (-2400, 5480, -640, 5880), Y_B1, 360, "plantroom", "restricted", floor_mat="concrete", wall_mat="steel_pipes",
              ceiling_mat="pipe_grid", chr_light=10, light=light(0.3, 0.85, 380, [255, 225, 190]))
    pads(pc, ("plant_mid", (-1500, 5680), (-640, 5680)), ("power_way", (-1700, 5680), (-1700, 5880)))
    objs(pc, cam("cam_plant", "x1", 5680, 300, "plant_mid", (6, -6)))
    door("vault_corr", "plant_corr", at=-1500, width=200, height=260, frame=STEEL, obj={"name": "plant_gate", "type": "slide", "keyflags": "0x8"})
    pw = room("power", (-2400, 5920, -1040, 7000), Y_B1, 400, "plantroom", "restricted", floor_mat="concrete", wall_mat="steel_pipes",
              ceiling_mat="pipe_grid", chr_light=10, light=light(0.28, 0.95, 480, [255, 220, 180], lamp_height=360))
    blocks(pw, block("dynamo_1", (-2200, 6400, -1800, 6700), 300, "diamond_plate", "steel_panels"))
    pads(pw, ("power_1", (-1300, 6100), (-2400, 6400)), ("breaker_front", (-2230, 6150), (-2400, 6150)), ("power_mid", (-1700, 6250), (-2400, 6250)),
         ("vent_vault_a", (-1150, 6820), (-1040, 6820)))
    objs(pw, alarm("alarm_plant", "z0", -1400), prop("breaker", 30, (-2330, 6150), (-1040, 6150)),
         prop("power_console", 31, (-1300, 6950), (-1300, 5920)))
    door("plant_corr", "power", at=-1700, width=240, height=280, frame=STEEL, obj={"name": "power_door", "type": "slide", "guard_access": "any"})
    gn = room("generator", (-2400, 7040, -1040, 7600), Y_B1, 400, "plantroom", "restricted", floor_mat="concrete", wall_mat="steel_pipes",
              ceiling_mat="pipe_grid", chr_light=10)
    pads(gn, ("generator_front", (-1700, 7350), (-1700, 7600)))
    objs(gn, prop("generator", 48, (-1700, 7500), (-1700, 7040)))
    door("power", "generator", at=-1700, width=200, height=260, frame=STEEL, obj=slide("generator_door"))


def stairwell():
    """The security stair core: a switchback from the north corridor (G) to the surveillance corridor (F1)."""
    return {"name": "core_e", "rect": [2280, 3880, 3280, 5080], "axis": "z", "levels": [Y_G, Y_F1], "landing": 400, "height": 330,
            "stairs": 12, "door_width": 220, "floor_mat": "stone_flags", "wall_mat": "grey_panels", "ceiling_mat": "dark_grid",
            "riser_mat": "stone_blocks", "area": "security", "zone": "restricted", "chr_light": 12, "light": light(0.4, 0.8, 360)}


def core_doors():
    door("north_corr_e", "core_e_0a", at=2520, width=220, obj={"name": "core_door", "type": "slide", "keyflags": "0x4"})
    door("surv_corr", "core_e_0b", at=3040, width=220)


# the heist's tables for this layout (LEVEL.script.heist): the guard areas, the cameras, the interfaces; tools/jobs/casino.lua has
# the rest of the job (the names the mode drives, the cards, the words)
HEIST_AREAS = [
    ("floor", ["atrium", "salle_renaissance", "salle_europe", "bar", "stair_foot"],
     ["floor_post_sw", "floor_post_se", "floor_post_nw", "floor_post_ne", "pit_mid", "pit_n", "floor_entry_w", "floor_entry_e", "ren_post_w",
      "ren_post_e", "ren_mid", "atrium_mid", "bar_2"], "alarm_floor", 22, 3),
    ("cage", ["cage", "cage_mantrap"], ["cage_post", "cage_mid", "mantrap_post"], "alarm_cage", 7, 1),
    ("corridor", ["staff_corr", "staff_lounge", "lockers", "north_corr_w", "north_corr_e", "kitchen", "staff_canteen"],
     ["staff_s", "staff_mid", "staff_n", "nw_mid", "ne_mid", "ne_e", "kitchen_1"], "alarm_staff", 10, 1),
    ("offices", ["pit_office"], ["pit_office_mid"], "alarm_staff", 2, 0),
    ("security", ["security_corr", "guard_room", "armory", "surveillance", "surv_corr"],
     ["sec_mid", "guard_1", "guard_2", "surv_mid", "surv_1", "surv_corr_mid"], "alarm_security", 10, 1),
    ("catwalk", ["catwalk_w", "catwalk_n", "catwalk_e"], ["patrol_catwalk_1", "patrol_catwalk_3", "ce_mid"], "alarm_surv", 5, 1),
    ("dock", ["dock", "alley"], ["dock_mid", "dock_1", "dock_2", "alley_s", "alley_dock"], "alarm_dock", 8, 2),
    ("executive", ["exec_corr", "director"], ["exec_mid", "director_1"], None, 2, 0),
    ("salons", ["salons_lobby", "salon_prive", "salle_blanche", "grand_stair"], ["lobby_mid", "blanche_post", "prive_2"], "alarm_salons", 8, 1),
    ("vault_ante", ["vault_corr", "vault_ante", "red_stairs", "soft_count", "hard_count"],
     ["ante_1", "ante_2", "ante_3", "ante_4", "stair_top", "stair_bot", "vc_mid", "vc_e", "hard_mid"], "alarm_vault", 14, 2),
    ("plantroom", ["plant_corr"], ["plant_mid"], "alarm_plant", 2, 0),
]
HEIST_CAMS = [
    ("cam_place", "PLACE DU CASINO", "dock"), ("cam_atrium", "ATRIUM", "floor"), ("cam_renaissance", "SALLE RENAISSANCE", "floor"),
    ("cam_cage_window", "CAGE WINDOWS", "floor"), ("cam_floor_s", "SALLE EUROPE S", "floor"), ("cam_floor_n", "SALLE EUROPE N", "floor"),
    ("cam_pit", "THE PIT", "floor"), ("cam_bar", "BAR AMERICAIN", "floor"),
    ("cam_cage", "CAGE", "cage"), ("cam_cage_n", "CAGE BACK", "cage"), ("cam_mantrap", "CAGE MANTRAP", "cage"),
    ("cam_staff", "STAFF CORRIDOR", "corridor"), ("cam_north_w", "NORTH CORRIDOR W", "corridor"), ("cam_north_e", "NORTH CORRIDOR E", "corridor"),
    ("cam_pit_office", "PIT OFFICE", "offices"),
    ("cam_security", "SECURITY", "security"), ("cam_surveillance", "EYE IN THE SKY", "security"), ("cam_catwalk", "CATWALK", "catwalk"),
    ("cam_dock", "DOCK", "dock"), ("cam_alley", "ALLEY", "dock"),
    ("cam_exec", "EXECUTIVE SUITE", "executive"),
    ("cam_salons", "SALONS PRIVES", "salons"), ("cam_prive", "SALON PRIVE", "salons"), ("cam_blanche", "SALLE BLANCHE", "salons"),
    ("cam_vault_corr", "VAULT CORRIDOR", "vault_ante"), ("cam_ante", "VAULT DOOR", "vault_ante"), ("cam_soft_count", "SOFT COUNT", "vault_ante"),
    ("cam_hard_count", "HARD COUNT", "vault_ante"), ("cam_plant", "PLANT", "plantroom"),
]
PATROLS = {"floor": 6, "catwalk": 4, "vault": 4, "perim": 3}
STATIONS = ["patrol_floor_2", "patrol_catwalk_2", "patrol_vault_3", "patrol_perim_2"]
VENTS = ["count", "salon", "vault"]
GRAPPLES = ["catwalk", "terrace", "goodslift"]


def script():
    return {"heist": {
        "job": "casino",
        "areas": [{"name": n, "rooms": r, "posts": p, "panel": pa, "share": sh, "raisers": ra} for (n, r, p, pa, sh, ra) in HEIST_AREAS],
        "cams": [list(c) for c in HEIST_CAMS],
        "guard_areas": ["floor", "cage", "security", "catwalk", "corridor", "dock", "salons", "vault_ante", "executive", "offices"],
        "patrols": {r: ["patrol_%s_%d" % (r, k + 1) for k in range(n)] for r, n in PATROLS.items()},
        "stations": STATIONS,
        "vents": VENTS,
        "grapples": GRAPPLES,
        "map_label": "Le Grand Casino Janus",
        "no_cams": "(lockers, chip bank: no cams)",
    }}


def check_names(L):
    """Every pad the script and the interfaces name exists (a missing one is a typo here, not a runtime surprise)."""
    have = set()
    for r in L["rooms"]:
        have.update(p["name"] for p in r.get("pads", []))
        have.update(o["name"] for o in r.get("objects", []) if "name" in o)
    have.update(d["object"]["name"] for d in L["doors"] if d.get("object") and "name" in d["object"])
    roomnames = {r["name"] for r in L["rooms"]}
    h = L["script"]["heist"]
    want = [p for a in h["areas"] for p in a["posts"]] + [a["panel"] for a in h["areas"] if a["panel"]] + [c[0] for c in h["cams"]]
    want += [p for ps in h["patrols"].values() for p in ps] + h["stations"]
    want += ["vent_%s_%s" % (v, s) for v in h["vents"] for s in "ab"] + ["grapple_%s_%s" % (g, s) for g in h["grapples"] for s in ("low", "high")]
    missing = sorted(set(w for w in want if w not in have))
    badrooms = sorted(set(r for a in h["areas"] for r in a["rooms"] if r not in roomnames))
    if missing or badrooms:
        raise SystemExit("build_casino: missing pads/objects %s, rooms %s" % (missing, badrooms))
    ncam = sum(1 for r in L["rooms"] for o in r.get("objects", []) if o.get("type") == "camera")
    if ncam > 32:
        raise SystemExit("build_casino: %d cameras (limit 32)" % ncam)


def main():
    outside()
    ground()
    first()
    basement()
    core_doors()
    # which guards may open a keyed door (LEVEL.objects[name].guard_access): never the vault doors, the cells, the padlocks; a card
    # tier's doors its own areas' guards (BLUE security / the catwalks / the executive suite, RED the vault level, GREEN all staff)
    staff = "floor,cage,corridor,offices,security,catwalk,dock,executive,salons,vault_ante,plantroom"
    tiers = {0x40000000: "never",
             0x8: "role:vault_ante,plantroom,vault,squad,response",
             0x4: "role:security,catwalk,offices,executive,wave,response",
             0x2: "role:" + staff + ",squad,wave,response",
             0x1: "never"}
    never = ("chip_bank_door", "drop_cage_door", "dock_shutter")
    for d in doors:
        o = d.get("object")
        if not o or "guard_access" in o:
            continue
        kf = int(o.get("keyflags", "0"), 0) if isinstance(o.get("keyflags"), str) else int(o.get("keyflags", 0))
        if o.get("name") == "barracks_door":
            o["guard_access"] = "role:barracks,squad,wave,response"
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
        "comment": "Le Grand Casino Janus (designs/job2-casino.md): the heist's second job, written by tools/levelgen/build_casino.py (edit "
                   "that, not this file). A Monte-Carlo-style casino on three levels: the place, the atrium, the Salle Renaissance (slots), "
                   "the Salle Europe (tables, the pit, one-way glass), the cage and its mantrap, back of house, security; the eye-in-the-sky "
                   "catwalks, the surveillance room, the executive suite and the Salons Prives with their sea terrace above; the house vault, "
                   "the count rooms, the goods lift and the plant below. x east, z north.",
        "defaults": {
            "height": 360, "cell": 300, "door_width": 180, "door_height": 240, "max_wall": 200,
            "floor_mat": "white_tiles", "wall_mat": "plaster", "ceiling_mat": "office_ceiling", "chr_light": 13,
            "frame_width": 14, "frame_depth": 6,
            "light": {"ambient": 0.46, "intensity": 0.7, "radius": 420, "lamp_spacing": 520, "tint": B.LIGHT_OFFICE},
        },
        "materials": B.MATERIALS,
        "rooms": rooms,
        "stairwells": [stairwell()],
        "doors": doors,
        "openings": openings,
        "start": {"room": "place", "at": [-100, -1850], "face": [0, -1300]},
        "script": script(),
    }
    check_names(L)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "casino.json")
    with open(out, "w", newline="\n") as f:
        json.dump(L, f, indent=1)
        f.write("\n")
    print("wrote %s: %d rooms (+ the stair core), %d doorways, %d openings" % (out, len(rooms), len(doors), len(openings)))


if __name__ == "__main__":
    main()

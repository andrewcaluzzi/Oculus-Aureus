# Generated levels (levelgen)

A level made of box rooms joined by doorways, generated from a small JSON layout by `tools/gen_level.py`. It is the foundation of the bank heist's own map (`designs/bank-heist.md` section 3, option B, and section 6, P2). The generator writes C data in the same shape the asset extractor produces from the ROM; CMake compiles it into the exe on a cut stage slot, as Flatland (`tools/gen_flatland.py`) and the Colosseum (`tools/gen_colosseum.py`, `modding/arena.md`) are. Only texture **numbers** are written: the texels are the game's own, read from your ROM at run time. Nothing ROM-derived is in the repo.

## Launching

| Variable | Value |
| --- | --- |
| `GE_STAGE` | `levelgen` (or `lee`): the cut stage slot `LEVELID_LEE`. |
| `GE_MODE` | `levelgen_test`: walk the rooms; a guard stands on post in every room but the first, and HUD line 1 names the room you are in. `bank-walk` (`tools/mode_bank_walk.lua`, `Walk Janus Bank.bat`): no guards, Bond invincible, every keyed door opens for him, the HUD names the room and its area. Any other mode works too. |

Add `GE_SKIPINTRO=1` to start in Bond's eyes. The slot borrows Surface's text bank and briefing (no objectives are shown), plays the Archives music, and has no fog or sky (a near-black clear colour). It reserves 200 spare chr slots, since the level has no guards of its own.

## Making a level

1. Write or edit a layout in `tools/levelgen/` (the shipped ones: `lshape.json`, three rooms in an L; `testbox.json`, two rooms; `objects.json`, the L plus two rooms with every kind of setup object; `janus_bank.json`, the heist's bank v2, designs/bank-heist.md section 17, **written by `tools/levelgen/build_janus_bank.py`**: edit the script and rerun it, not the JSON).
2. `python3 tools/gen_level.py tools/levelgen/lshape.json` rewrites `port/levelgen/` and prints each room's size, vertex and display-list count, every doorway, and the pad and tile counts. It stops with a message on a layout it cannot build (overlapping rooms, a doorway that is not on a shared wall or joins two floor heights, a pad off the floor or with no clear line to any other pad, a pad the start cannot reach, a doorway whose floors do not join).
3. Build. The slot holds one level at a time: whichever layout was generated last (the committed one is `janus_bank.json`).
4. A plan: `python3 tools/levelgen_plan.py tools/levelgen/janus_bank.json <out>` writes `<out>.svg` and `<out>.png` (rooms shaded by floor, or by `zone` when tagged, ramps hatched, blocks, doorways coloured by the key of their door, openings in cyan, padlocks, cameras with their view, alarm boxes, monitors, the safe, keys, props, named and loot pads, the start). A layout on several levels also gets a sheet per level (`<out>_b2.png` ... `<out>_f3.png`, the other levels' rooms as faint outlines) and a side elevation (`<out>_elevation.png`); `<out>.png` is then every sheet at half scale. No dependencies.

The `name` in the layout must stay `levelgen` while the slot's code expects `port/levelgen/` (`bg.c`, `prop.c`, `CMakeLists.txt`).

## The layout format

Units are world units (about a centimetre; Bond's eye is about 175 up). `x` and `z` are the floor plane, `y` is up.

```json
{
  "name": "levelgen",
  "defaults": { "height": 320, "cell": 300, "door_width": 180, "door_height": 240, "max_wall": 200,
                "floor_mat": "lobby_tiles", "wall_mat": "pink_marble", "ceiling_mat": "light_wood", "chr_light": 13,
                "light": { "ambient": 0.42, "intensity": 0.75, "radius": 420, "lamp_spacing": 520, "tint": [255, 244, 228] } },
  "materials": { "lobby_tiles": { "tex": 632, "texels_per_unit": 1.18 }, "...": {} },
  "rooms": [
    { "name": "A", "rect": [-1240, -500, -200, 500],
      "pads": [ { "name": "a_corner", "at": [-1100, 400], "face": [-200, -300] } ] },
    { "name": "B", "rect": [-160, -500, 840, 500], "floor_mat": "white_tiles", "wall_mats": { "x1": "plaster" } }
  ],
  "doors": [ { "rooms": ["A", "B"], "at": -300 } ],
  "start": { "room": "A", "at": [-900, 300], "face": [-200, -300] }
}
```

| Key | Meaning |
| --- | --- |
| `defaults` | Values every room and door takes unless it sets its own. |
| `cell` | The largest floor cell. Each room's floor, ceiling and walls are cut on one grid (its corners, every doorway edge, then every gap split to `cell` or less); the walkable tiles and the pads use the same grid. |
| `max_wall` | The thickest wall a doorway may cross (default 200). |
| `materials` | Name → `{tex, texels_per_unit}`. `tex` is a stock texture number; `texels_per_unit` sets the scale (how many of its texels per world unit; take it from the stock level the texture comes from, below). Other keys (`from`) are notes. |
| `rooms[]` | `name`; `rect` = `[x0, z0, x1, z1]`, the room's **inside**; `floor` (y, default 0); `height`; `floor_mat`, `wall_mat`, `ceiling_mat`; `wall_mats` = per wall side (`x0`, `x1`, `z0`, `z1`) over `wall_mat`; `light` (merged over the default); `chr_light` (0..15, the floor tiles' colour, which shades props and chrs standing on them; stock tiles are 15); `pads` = named pads `{name, at = [x, z], face = [x, z]}`. Rooms may not overlap. Bg room ids are the order in this list, from 1. |
| `doors[]` | `rooms = [a, b]`: two rooms whose walls face each other across a gap of 1..`max_wall` units (that gap is the wall). `at` = the doorway's centre along the wall (default the middle of the shared stretch); `width`, `height`; `name` (default `"A-B"`; give two doorways between the same rooms their own names); `frame` (P2.3, below). The two floors must be level with each other at the doorway (a ramp's end). |
| `start` | Bond's start: `room`, `at`, `face`. |
| `light` | Fake lighting baked into the vertex colours: `ambient` plus, per lamp, `intensity / (1 + d² / radius²)`, capped at `max` (1.15), times a face factor (floor 1, ceiling 0.72, walls 0.8 / 0.92 by direction), times `tint`. Lamps sit `lamp_height` above the floor (default 20 under the ceiling), on a grid every `lamp_spacing`, or exactly at `lamps = [[x, z], ...]`. |

### Ramps, stairs, furniture, frames (P2.3)

```json
{ "name": "stairs", "rect": [700, 1290, 1100, 2400], "ramp": { "axis": "z", "to": -340, "stairs": 17, "riser_mat": "stone_blocks" } },
{ "name": "street", "rect": [-1900, -1600, 3000, -800], "height": 900, "open_top": true, "area": "street" },
{ "name": "hall", "rect": [-300, -380, 1700, 900], "blocks": [ { "name": "counter_w", "rect": [-300, 420, 1250, 500], "height": 110,
                                                                  "mat": "wood_panel", "top_mat": "white_marble" } ] },
"doors": [ { "rooms": ["hall", "office_corr"], "at": 700, "width": 200, "frame": "wood_panel",
             "object": { "type": "slide", "double": true, "keyflags": "0x2", "see_through": true } } ]
```

| Key | Meaning |
| --- | --- |
| room `ramp` | `axis` (`x` or `z`), `to`: the floor slopes along the axis from `floor` at the low end (x0 or z0) to `to` at the high end; the ceiling and the side walls follow it. Doorways go on the two end walls only (there the floor is level). `stairs` = n draws n level treads and their risers (in `riser_mat`, default the floor's) over the slope; the walkable floor (stan) stays the smooth ramp, as on the stock stairs, so Bond's and the guards' feet follow the line through the treads' middles. |
| room `open_top` | No ceiling: the slot's near-black clear colour is the night sky (the street and the alley). Make the walls tall. |
| room `blocks` | Furniture boxes on the floor: `rect` (inside the room), `height` (a block as tall as the room is a pillar: no top), `mat`, `top_mat`. The floor under a block has no stan (a hole): nobody walks through it, it stops bullets (bg geometry) and it blocks the stock stan line of sight (guards, cameras) whatever its height. Grid pads under a block are dropped and no waypoint link passes within 40 units of one. Not on ramps. |
| room `area` | A free tag passed to `LEVEL.rooms[name].area` (the heist's areas). |
| doorway `frame` | A material name, or `{mat, width, depth}` (defaults `frame_width` 14, `frame_depth` 6): an architrave of two posts and a lintel round the opening on both faces of the wall, and the passage's jambs and soffit in that material. |
| door `double` | Two leaves, each on a bound pad at its jamb with its box running to the middle (Bunker 2's pairs), linked to each other (`linkedDoorOffset` +1 / -1): opening or closing either moves both. `LEVEL.doors[i].object2` and `LEVEL.objects[name].pair` give the second leaf. |
| door `see_through` | `PROPFLAG_NO_PORTAL_CLOSE` instead of `PROPFLAG_CULL_BEHIND_DOOR`: the room behind stays drawn while the door is shut (doors with a window, as Bunker 2's office doors are). |

Rooms may share x, z when they are on different floors (their height spans do not overlap): the vault level runs under nothing in the bank, but it may. A doorway's grid lines are the same in both rooms (a line one room has across the opening from another doorway or block is added to the other), so the floors always join edge to edge.

**Doorways stay clear (P2.4).** The generator refuses a layout in which furniture is in a doorway's way: on each side of every doorway it keeps clear a box from the wall out to the approach pad plus a clearance (`door_approach` 110 + `door_clearance` 60 = 170 units deep) and the opening widened by the clearance each way along the wall. A block's rect, or a floor prop's footprint (its stock model's bounding box from `assets/obseg/prop/<name>/Model.c` times the model's file scale, turned to its `face`; a prop may give `"size": [width, depth]` instead, and must when its model's files are not found), that reaches into that box stops the generator with the prop, the doorway and both boxes named. `door_clearance` is a `defaults` key. Why: a teller desk in the bank's hall stood across the staff door's jamb, half in front of the opening (AC found it walking the bank); nothing checked props against doorways before. The check found that one and the dock's boxes in front of the alley shutter (also 30 units into the wall); both moved (`teller_desk_3` x 1000 → 850, `dock_boxes` to (2340, 1985), turned to face the dock).

### Openings, stair cores, zones, big layouts (bank v2)

```json
"openings": [ { "rooms": ["gallery_w", "hall"], "at": 350, "width": 440, "y": [580, 860], "frame": "wood_panel", "name": "gal_w~hall_2" } ],
"stairwells": [ { "name": "core_w", "rect": [-4400, 3400, -3400, 4600], "axis": "z", "levels": [0, 480, 960, 1440], "landing": 400,
                  "height": 330, "stairs": 12, "door_width": 220, "zone": "staff", "area": "corridor" } ],
"rooms": [ { "name": "server_room", "zone": "restricted", "area": "security", "...": "" } ],
"doors": [ { "rooms": ["vault", "deposit"], "object": { "name": "vault_door_b", "guard_access": "never", "...": "" } } ]
```

| Key | Meaning |
| --- | --- |
| `openings[]` | A see-through hole in the wall two rooms share (the same facing-walls rule as a doorway): `y` = [bottom, top], absolute heights inside both rooms' walls; `at`, `width`, `name` (default `a~b`), `frame`. It is a portal (so each room is drawn through it, clipped to it) with no door, no floor through it, no pads and no waypoint: you see and shoot through, nobody walks through. The wall under it is the sill: a balcony's rail (a gallery 100 under the bottom), a window's ledge. The rooms' floors may differ (a gallery over a hall). Each room's wall is cut at its edges (wall cuts only: they are not floor grid lines) and has a hole between `y`; the sill, soffit and jambs are room a's. Not in a ramp's wall. `LEVEL.openings[i]` = `{name, a, b, portal, x, z, y0, y1, width}`. The stock line of sight walks the floor tiles, so guards and cameras do **not** see through an opening (a guard on a balcony does not see Bond in the hall below). |
| `stairwells[]` | A switchback stair core: for each pair of `levels`, flight `a` climbs lane 1 along `axis` from the core's low end to a landing at its high end (half way up), flight `b` climbs lane 2 back to the low end at the next level. It expands into ordinary rooms (`<name>_<k>a`, `<name>_<k>l`, `<name>_<k>b`, storey k from the bottom) and their doorways; the layout's own doors join each floor to the flights' low ends (`a` at level k, `b` at level k + 1). Keys as above plus `wall` (40) and the room keys (materials, `light`, `area`, `zone`). |
| room `zone` | A second free tag (`LEVEL.rooms[name].zone`): the bank uses public / staff / restricted (the cover rules, designs/bank-heist.md section 16). |
| door object `guard_access` | Passed to `LEVEL.objects[name].guard_access` (the bank: `never`, `role:<area>`, `barracks`: which guards may open it). |

**What changed for big layouts.** Rooms stacked in x, z may share x, z when one is wholly above the other: the test is exact for ramps (floors and ceilings are planes; at the four corners of the shared rectangle, a stair's treads half a step under its slope), so the flights of a stair core stack 480 apart with 330 high flights. A room's pads must be one connected graph (the stock `init_path_table_links` floods each waypoint group and **freezes the game** with "not all locs in hall are connected" if one is not; a block across a room did that): the generator joins the pieces by their nearest clear pair and refuses a room cut in two. Tiles are indexed by room (a 100-room layout has 12000 tiles and 4000 pads; it generates in about 5 s). A point on a sliver of floor (a doorway's grid line 10 units from its jamb) takes the tile that holds it at all, or the nearest. A big level room's ceiling is the fine grid only in its outer ring of cells and coarse quads (600) inside (the banking hall: 10000 vertices to 7900). A wall too tall for its texture's density is cut into bands (the s16 texture coordinates). The engine's limits are checked (below).

**The engine's limits for one level** (read off the code, checked by `gen_level.py`):

| Limit | Value | Where |
| --- | --- | --- |
| rooms | 148 at most (`MAXROOMCOUNT` 150 counts room 0 and the end entry); the generator stops at 140 | `bondconstants.h`, `g_BgRoomInfo[]`, `s_SimRooms[]` |
| portals (doorways + openings) | under 200 (`PORTMAX`); the generator stops at 190 | `bg.h`, `table_for_portals[]` and the portal state arrays |
| portal depth | a room more than 15 portals deep from the camera's is not drawn | `bgPortalDescend`, `D_8004489C` |
| stan tiles | 16380 (a link is a u16: `(0x80 + 32 * tile) >> 3`) | `stan.c` |
| pads | under 10000 (bound pads are numbered from 10000) | `setBoundPadNum` |
| coordinates | within +-32000 (bg vertices and stan points are s16) | |
| cameras | 32 (co-op's per-Bond camera timers, the cctv overrides) | `port_heist.c` `CCTV_BOND_CAMS`, `CCTV_OV_MAX` |
| room heap | the slot now has its own memory row (`-ma1000`: 2 MB of room geometry on PC; before, it took the last stage's, doubled) | `boss.c` `memallocstringtable` |

### Objects (P2.2)

A door goes in a doorway with `"object"` on the door; everything else goes in a room's `"objects"` list; loot spawn points are a room's
`"loot"` lists. `tools/levelgen/objects.json` uses every kind.

```json
"doors": [ { "rooms": ["B", "D"], "at": 0, "object": { "name": "office_door", "type": "slide", "keyflags": "0x2" } },
           { "rooms": ["D", "E"], "at": 1500, "object": { "name": "shutter", "type": "shutter", "padlock": true } } ],
"rooms": [ { "name": "D", "rect": [880, -500, 1880, 500],
             "objects": [ { "type": "camera", "name": "cam_office", "wall": "x1", "along": 0, "y": 280, "watch": "d_mid", "sweep": [35, -35] },
                          { "type": "alarm", "wall": "z0", "along": 1100, "y": 140 },
                          { "type": "monitor", "wall": "z0", "along": 1600, "y": 160 },
                          { "type": "safe", "at": [1700, 360], "face": [1700, 0], "keyflags": "0x10" },
                          { "type": "key", "at": [300, 900], "keyflags": "0x2" } ],
             "loot": [ { "name": "vault_gold", "at": [[1200, 1200], [1380, 1200]] } ] } ]
```

| Kind | Keys | What is written |
| --- | --- | --- |
| door (on a doorway) | `type` `slide` (default) or `shutter` (rises into the lintel); `model` (a name below or a number); `keyflags` (number or `"0x..."`; 0 = unlocked); `autoclose` (ticks; 300 slide, 1500 shutter); `open` (starts open); `padlock` (true: a padlock on room a's side keeps it shut until shot off); `thickness` (10); `name` | A stock door record on a bound pad whose box is the opening, set just inside the passage at room b's face, flagged `PROPFLAG_CULL_BEHIND_DOOR`: `setupDoor` tests 50 units either side of its centre, finds rooms a and b, takes their portal (`bgGetPortalBetweenRooms`) and closes it while the door is shut, so the room behind a shut door is not drawn. A padlock is a `PROP_PADLOCK` prop plus a `LockDoor` record (Dam's layout). |
| `camera` | `wall` (`x0` `x1` `z0` `z1`), `along` (the coordinate along that wall), `y` (above the floor), `watch` (a named pad, or `[x, z]`, default the room's centre), `sweep` (`[left, right]` degrees, default ±30), `speed`, `range` (≤ 0 none) | A stock CCTV record (Bunker 2's tail) on a wall bound pad; its lens aims at the watched pad and sweeps. `cctv.*` and the stock spot / alarm work on it. |
| `alarm` | `wall`, `along`, `y` | A stock alarm box (Bunker 1's) on a wall bound pad. Its bound pad id (`10000 + n`) is the raiser's `alarm_pad`: the stock raiser runs there and sounds the alarm while the box is intact; Bond can use it to switch the alarm. |
| `monitor` | `wall`, `along`, `y`, `width` (70), `height` (52), `image` (16), `model` | A stock single monitor (Bunker 2's banking-hall screen record), scaled to the box. A use press on it fires `use` with its index and objtype 10 (the heist opens the camera feeds on that). |
| `safe` | `at`, `face` (the front looks at it), `keyflags` (0x10), `scale` (1.3) | Bunker 2's manager's safe: a `PROPDEF_SAFE` body on the floor and its swinging door (a door record, never auto-closes) on its front; the door's key flags lock it. |
| `key` | `at`, `keyflags`, `model` (`keycard` or `key`) | A stock key record on a floor pad (picked up by walking over it). |
| `prop` | `at`, `face`, `model`, `flags` | Any stock object model as a plain prop on the floor. |
| `loot` (room key) | `name`, `at` (a list of `[x, z]`) | Named pads `<name>_1..n` on the waypoint graph and `LEVEL.loot[name]` = their ids (for `spawn.item`). |

Stock model ids (`MODEL` in `tools/gen_level.py`; the `obj` field of a setup record, `PROP_*` in `bondconstants.h`):

| Name | Id | `PROP_*` | Taken from |
| --- | --- | --- | --- |
| `door` | 138 | `PROP_SEV_DOOR4_WIND` | Bunker 2's office doors (46-57) |
| `shutter` | 140 | `PROP_SEV_DOOR_V1` | Bunker 2's security-wing shutters (78/79) |
| `cell` | 182 | `PROP_DOORPRISON1` | Bunker 2's cell doors |
| `vault` | 152 | `PROP_DOOR_DEST1` | Frigate's grey steel doors (caution stripes) |
| `camera` | 24 | `PROP_CCTV` | every stock camera |
| `alarm` | 1 | `PROP_ALARM2` | Bunker 1's alarm boxes |
| `monitor` | 77 | `PROP_TVSCREEN` | Bunker 2's banking-hall screens |
| `safe` / `safedoor` | 254 / 293 | `PROP_SAFE` / `PROP_SAFEDOOR` | Bunker 2's manager's safe |
| `padlock` | 63 | `PROP_PADLOCK` | Dam's padlocked gate |
| `keycard` / `key` | 86 / 244 | `PROP_SWIPE_CARD2` / `PROP_CHRKEYYALE` | Bunker 2's carried cards / its floor key |

Object pads (keys, props, padlocks, a camera's `[x, z]` look point) are plain pads after the waypoint pads and are not on the waypoint
graph; wall objects, doors and the safe use bound pads (`pad3dlist`). Record indices are the setup indices every binding takes
(`door.*`, `cctv.*`, `obj.get`, the `use` / `gadget_used` events): `LEVEL.objects[name].index`, `LEVEL.doors[i].object`.

### Stock textures found so far

Read off the stock levels' display lists by face direction (floor, wall, ceiling); `texels_per_unit` is the stock density converted to world units. They are seen in the screenshots of `tests/levelgen/run.sh`.

| Texture | From | Use | `texels_per_unit` | Looks like |
| --- | --- | --- | --- | --- |
| 632 | Archives | floor | 1.18 | small beige square tiles |
| 901 | Bunker 2 | floor | 0.75 | white tiles |
| 311 | Facility | floor | 0.16 | grey speckled concrete |
| 1136 | Archives | floor | 0.21 | brown planks on a grid |
| 179 | Archives | wall | 0.115 | pale pink marble panels with a skirting band at the foot |
| 315 | Bunker 2 | wall | 0.33 | beige panels with horizontal seams |
| 2374 | Archives | wall | 0.27 | plain grey plaster |
| 1469 | Archives | wall | 0.414 | grey stone blocks |
| 283 | Facility | wall | 0.29 | grey concrete with lines |
| 1480 | Archives | wall | 0.196 | stone with baked pools of lamp light |
| 296 | Bunker 2 | wall | 0.42 | light grey vertical panels |
| 1474 | Archives | wall | 0.30 | green stone bands |
| 299, 379, 373 | Archives | wall | 1.16, 0.57, 0.59 | grey metal slats, beige / brown slats |
| 153 | Archives | wall | 0.62 | black bars (a cage) |
| 1468 | Archives | wall | 0.24 | grey blotchy stone in bands |
| 1524 | Archives | ceiling | 0.122 | light wood grain |
| 257 | Bunker 2 | ceiling | 0.60 | suspended office ceiling tiles |
| 258 | Facility | ceiling | 0.39 | dark ceiling grid |
| 1135 | Archives | ceiling | 0.26 | dark wooden lattice |

Added in P2.3, looked at in a gallery of box rooms on the slot (one candidate per wall, floor and ceiling). `texels_per_unit` is the stock density per bg unit times the stage's level scale (the same conversion as the table above).

| Texture | From | Use | `texels_per_unit` | Looks like |
| --- | --- | --- | --- | --- |
| 308 | Archives | wall | 0.108 | pale grey-white marble, vertical streaks (the banking hall) |
| 2376 | Archives | wall | 0.879 | small pink granite blocks (the deposit hall) |
| 1530 | Train | wall | 0.268 | brown wood panelling with knots (the manager's office, the counter, frames) |
| 1204 | Train | ceiling | 0.21 | light wood planks |
| 405 | Egypt | floor | 0.174 | large beige stone flags (the hall) |
| 760 | Train, Depot | floor | 0.31 | red speckled carpet (offices) |
| 195 | Archives | wall | 0.461 | red brick (the street) |
| 20 | Archives | floor | 0.475 | grey speckled, like asphalt |
| 284 | Train | wall | 0.177 | stained grey concrete blocks |
| 1376, 1375 | Depot | wall | 0.2, 0.194 | black corrugated metal |
| 1081, 385 | Depot | ceiling | 0.39, 0.194 | rusty corrugated metal |
| 477, 478, 476 | Frigate | wall | 0.53 | grey steel bulkheads: ribbed / plain panels / with black pipes (the vault level) |
| 1138 | Frigate | floor | 0.78 | steel stud plate |
| 727 | Frigate | ceiling | 0.78 | riveted metal strips |
| 491 | Frigate | ceiling | 0.347 | open metal grid with pipes above |
| 522 | Frigate | wall | 0.19 | plain grey |
| 440 | Bunker 2 | wall | 0.95 | small dark grey tiles |
| 665 | Bunker 2 | wall | 0.578 | grey blocks with an arc relief |
| 1368 | Archives | wall | 0.26 | stone pillars with copper bands |
| 1515 | Archives | wall | 0.311 | black window grid |
| 1474 | Archives | wall | 0.306 | green stone bands (dark green, as in the table above) |
| 404, 1175, 1174, 410 | Egypt | wall | 0.27, 0.22, 0.237, 0.173 | sandstone |
| 400 | Depot | wall | 0.363 | rusty diamond plate |
| 186 | Bunker 2 | wall | 0.14 | red-purple granite (blurry at this scale) |
| 1426 | Train, Depot | wall | 0.15 | green rusty corrugated metal |
| 235 | Archives | floor | 0.305 | metal grating |
| 1137 | Archives | floor / ceiling | 0.8 | brown square tiles |

No plain brass turned up; the bank's frames are wood (1530) and steel (478).

## What is generated

| File | What is in it |
| --- | --- |
| `bg_levelgen_all_p.c` | One bg room per layout room: a point table (vertices, s16, at room origin 0) and a display list (the stock state prefix, one texture command per texture, `G_VTX` batches of 16 and `G_TRI4`), each a 1172 (raw deflate) stream. The point tables come first, then the display lists, then the end sentinels: `bg.c` sizes each compressed block from the start of the next one. One portal per doorway: the 4-point quad of the opening on room b's wall face. `global_visibility_commands` is the stock `{0x10000, 0}`. Each doorway's own faces (its floor, the lintel's underside, the two jambs inside the wall) belong to room a. |
| `Tbg_levelgen_all_p_stanZ.c` | The walkable floor: two triangles per grid cell, wound for the stan inside test; the room byte is the bg room id; each room's tiles in one run (`stanBuildRoomData` indexes the first tile of each room and stops a room's search at the next room's tile); the doorway's floor is room a's and meets room b's floor at the portal plane. Every shared edge is linked (link = 16 + 4 × tile index). |
| `UsetuplevelgenZ.c` | Pads: a grid pad in every floor cell at least 150 square (`pad_min_cell`), one in each doorway, one 110 in front of it on each side (`door_approach`), the named pads, the start. Each pad names the tile under it (`p<tile+1>a1`). Waypoints: every pad; links between pads of one room within 2.3 cells (a box room is convex, so every such line is clear), and doorway pad to its two approach pads. Waypoint groups: one per room and one per doorway, so the group route is the room graph and a chr's route crosses rooms only through a doorway. The setup objects (P2.2, above) with their bound pads and object pads. No guards or AI lists (modes spawn them). |
| `levelgen_layout.lua` | The layout for mode scripts (`include("levelgen_layout")`, shipped as `Data/Scripts/modes/levelgen_layout.lua`), below. |

### `LEVEL` in Lua

```lua
LEVEL.name, LEVEL.start_pad
LEVEL.rooms.A = { id = 1, rect = { x0, z0, x1, z1 }, floor = 0, height = 320, pads = { grid pad ids }, named = { a_corner = 54 } }
LEVEL.room_name[1] = "A"                     -- bond.room(p) and spawn.room(chr) give the id
LEVEL.doors[1] = { name = "A-B", a = "A", b = "B", portal = 0, pad = 50, pad_a = 48, pad_b = 49, x = -180, z = -300, width = 180, height = 240 }
LEVEL.pads.a_corner = 54                     -- every named pad, and "start"
-- with setup objects (P2.2):
LEVEL.doors[3] = { name = "B-D", ..., object = 0, keyflags = 2 }            -- the door in that doorway (setup index), its padlock
LEVEL.objects.cam_office = { type = "camera", index = 5, room = "D", lookpad = 109 }
LEVEL.objects.alarm_office = { type = "alarm", index = 6, room = "D", pad = 10004 }   -- pad: a raiser's alarm_pad
LEVEL.objects.office_safe = { type = "safe", index = 9, room = "D", body = 8, keyflags = 16 }   -- index: the safe's door
LEVEL.cameras, LEVEL.alarms, LEVEL.monitors, LEVEL.keys, LEVEL.safes, LEVEL.padlocks = { setup indices }
LEVEL.loot.vault_gold = { 112, 113, ... }    -- loot spawn pads
-- P2.3:
LEVEL.rooms.stairs = { ..., area = "vault_stairs", ramp = { axis = "z", to = -340, stairs = 17 } }   -- floor = the low end's height
LEVEL.rooms.hall = { ..., blocks = { { name = "counter_w", rect = { -300, 420, 1250, 500 }, height = 110 }, ... } }
LEVEL.doors[14] = { name = "back_corr-stairs", ..., object = 14, keyflags = 8, object2 = 15 }     -- a double door's two leaves
LEVEL.objects.red_stairs = { type = "door", index = 14, pair = 15, ... }
LEVEL.objects.screen_1 = { type = "monitor", index = 31, pad = 10028, x = -296, y = 150, z = -180, ... }  -- every object's x, y, z
```

`tools/heist_bank_map.lua` (shipped next to the modes) turns the bank's `LEVEL` into the heist mode's map tables by name (designs/bank-heist.md section 14).

Iterate rooms by id (`for id = 1, #LEVEL.room_name`), not with `pairs`, so co-op machines agree on the order.

`bond.rooms_drawn(p)` (tests) lists the bg rooms Bond `p`'s view drew last frame on this machine (presentation, not sim state).

## How the engine uses it

- **Rooms and culling.** The world draw is a portal walk from the room the camera is in (Bond's floor tile's room): a room is drawn when a portal into it is on screen, clipped to the screen box of the portals it was seen through. A room behind a wall with no portal on screen is not drawn. At load the game measures each room's box from its vertices and orders each portal's two rooms by its plane (`bgOrderPortal`), so the portal's winding does not matter.
- **Rooms of things.** A chr's or Bond's room is its floor tile's room byte. `bond.room`, `spawn.room`, the camera feeds' room lists and every "same room" test read that.
- **Walking between rooms.** Bond and the chrs move over linked stan tiles; a doorway's floor links to both rooms', so walking through is ordinary movement.
- **Routing.** Chrs route on the stock waypoints (a spawned enemy first tries a straight line when it is clear, `chraction.c`): the nearest pad from a flood fill over its floor tiles, a group route (rooms and doorways), then pads inside each group.

## Tests

`tests/levelgen/run.sh` (inside the build container, Xvfb on `:99`, `build_pc` built) runs `tests/levelgen/levelgen_walk.lua` on the slot: the culling (facing away from the doorway draws room A only; facing it draws A and B but not C, two walls away), Bond walking from A through the doorway into B on stick input (`bond.room` A then B), the view back through the doorway and on into C, and an unarmed guard spawned in C routing to Bond in A through both doorways (`spawn.room` C, B, A). Screenshots go to `build_pc/levelgen_shots/` (convert with `tests/bmp2png.py`). On `testbox.json` the C checks are skipped. It also passes on `objects.json` (the same A, B, C with open doorways).

`tests/levelgen/run_objects.sh` runs `tests/levelgen/levelgen_objects.lua` on `objects.json` (33 checks): every object is a live prop; a shut door culls the room behind it and an open one shows it (the door took its doorway's portal); the GREEN door refuses Bond and blocks his walk until he picks the card off its pad, then opens and he walks through; the camera spots Bond (`cctv_spotted`) and raises the alarm; a use press at the monitor names it in the `use` event (objtype 10) and the script opens the feed; a raiser (`alarm_pad` = the box's bound pad) runs to the alarm box and sounds it, and once the box is shot out (`obj.test_damage`) the next raiser cannot; the safe refuses Bond until he holds GOLD, then swings open; the padlocked shutter refuses until the padlock is shot off, then rises and shows E; no card opens the vault door, the cracker (`gadget_used`) names it, `door.open` opens it (what the heist's crack does), Bond walks in, gold bars spawn on the six loot pads, and `door.snap` shuts it and E is culled again. Screenshots to `build_pc/levelgen_obj_shots/`.

**Bank v2** (designs/bank-heist.md section 17): `python3 tests/levelgen/test_gen_level.py` (no game: openings, stair cores, the exact overlap test, rooms in pieces, the limits, the shipped bank generating; 11 tests). `tests/levelgen/run_bank.sh` runs `levelgen_bank.lua` in four processes side by side (Xvfb on `:99`-`:102`): `static` (the layout's counts and zones, linked double doors, the heist map resolving, a double door culling, the openings: the hall draws the arcade, the gallery and the second floor's windows, a gallery draws the hall and the far gallery, the balcony rail stops Bond, a facade window draws the street; the counter and its flap, a shelf), `route_a` (the building on foot, every room in order: the hall, the GREEN offices, the grand stair, the first floor, the east stair, the library, up the stair core to the roof and down to the ground, BLUE, the RED stairs, the vault, the deposit hall, up the service ramp, the alley), `route_b` (the plant level, the power room and the generator, the tunnel and its padlocked grate to the canal, the embankment to the street; the maintenance stair up to the canteen), `guards` (unarmed guards routing records -> the hall's customer side, up the service ramp, up the RED stairs, and down the stair core three storeys). `tests/levelgen/run_bank_shots.sh`: 16 screenshots of the signature spaces (`bench.look` sets the pitch).

(v1, before section 17:) `tests/levelgen/run_bank.sh` runs `tests/levelgen/levelgen_bank.lua` on `janus_bank.json` (designs/bank-heist.md section 14): the layout's counts, every double door's leaves linked, `tools/heist_bank_map.lua` resolving, a shut double door culling the stairs and one leaf opening both, the counter and a shelf stopping Bond and the counter's flap letting him through, the whole route on stick input (street to the van round the bank, down the stairs and up the service ramp, every room in order, his height following the slope), and unarmed guards routing records → staff corridor → the hall's customer side (round the counter), up the ramp and up the stairs. Screenshots to `build_pc/levelgen_bank_shots/`. `run.sh` and `run_objects.sh` need `objects.json` on the slot (generate, build, run, then generate `janus_bank.json` again).

## Status (2026-10-25, P2.3)

The bank is the layout on the slot: `tools/levelgen/janus_bank.json`, 20 rooms, 23 doorways, 444 pads, 96 setup objects. The P2.3 features above (ramps and stairs, blocks, frames, double and see-through doors, open tops, areas, object positions, doorway grids that always join) are in `gen_level.py`; flat layouts still generate byte for byte as before. `tools/levelgen_plan.py` draws a layout's plan.

## Status (2026-10-25, P2.2)

Done and run: the setup objects above (`objects.json` is the layout on the slot now), `tests/levelgen/run_objects.sh` 33 of 33, `tests/levelgen/run.sh` 11 of 11 on `objects.json`, the heist suites, `tests/coop_determinism.sh` and dam1 (identical), and the screenshots (looked at). The model and record layouts are the stock ones; the only engine-side additions are the `obj.get` / `obj.test_damage` bindings (`port_heist.c`).

## Limits (P2.3)

- Box rooms only (a room is a rectangle); an L-shaped space is two rooms and a wide doorway.
- A ramp slopes along one axis, with doorways on its two end walls only; stairs are drawn over a smooth stan ramp, so feet sink or float by up to half a step.
- A block is a stan hole: it blocks the stock line of sight (guards, cameras) whatever its height, so a low counter is full cover for sight. Blocks need a level floor. Props (desks, crates) are placed by hand; the generator keeps them out of doorways (above) but not out of other walking lines or the waypoint links.
- One door per doorway (or one double door), sliding or a shutter; no swinging room doors (only the safe's door swings), no glass. A frame is flat boxes, not a door-frame model.
- A monitor shows a stock picture (`image`, 0-51, the `monitorSetImageByNum` list in `propobj.c`: 3 waves, 9 / 10 bar graphs, 11 radar, 15 satellite targeting, 16 the global map, 20 off, 51 black), not the camera's video (the feeds are a view switch, `cctv.view`). Until P2.4 every generated monitor was black: the record is Bunker 2's word for word, and its image is 51 (`monAnim33BlackSolid`); Bunker 2's own AI lists switch its screens to their pictures (`tv_change_screen_bank`), and a generated level has no AI lists. The default is 16 now.
- Objects are placed by hand in the layout; nothing checks that two objects overlap.
- The texture scale is set by hand per material; a texture's own size is not read.
- The slot holds one generated level; the layout's `name` must be `levelgen`.
- The lighting is baked vertex colour; stan tile colours (`chr_light`) are per room.

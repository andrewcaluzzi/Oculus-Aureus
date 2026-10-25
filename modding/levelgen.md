# Generated levels (levelgen)

A level made of box rooms joined by doorways, generated from a small JSON layout by `tools/gen_level.py`. It is the foundation of the bank heist's own map (`designs/bank-heist.md` section 3, option B, and section 6, P2). The generator writes C data in the same shape the asset extractor produces from the ROM; CMake compiles it into the exe on a cut stage slot, as Flatland (`tools/gen_flatland.py`) and the Colosseum (`tools/gen_colosseum.py`, `modding/arena.md`) are. Only texture **numbers** are written: the texels are the game's own, read from your ROM at run time. Nothing ROM-derived is in the repo.

## Launching

| Variable | Value |
| --- | --- |
| `GE_STAGE` | `levelgen` (or `lee`): the cut stage slot `LEVELID_LEE`. |
| `GE_MODE` | `levelgen_test`: walk the rooms; a guard stands on post in every room but the first, and HUD line 1 names the room you are in. Any other mode works too. |

Add `GE_SKIPINTRO=1` to start in Bond's eyes. The slot borrows Surface's text bank and briefing (no objectives are shown), plays the Archives music, and has no fog or sky (a near-black clear colour). It reserves 200 spare chr slots, since the level has no guards of its own.

## Making a level

1. Write or edit a layout in `tools/levelgen/` (the shipped ones: `lshape.json`, three rooms in an L; `testbox.json`, two rooms; `objects.json`, the L plus two rooms with every kind of setup object).
2. `python3 tools/gen_level.py tools/levelgen/lshape.json` rewrites `port/levelgen/` and prints each room's size, vertex and display-list count, every doorway, and the pad and tile counts. It stops with a message on a layout it cannot build (overlapping rooms, a doorway that is not on a shared wall, a pad off the floor, a pad the start cannot reach).
3. Build. The slot holds one level at a time: whichever layout was generated last.

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
| `doors[]` | `rooms = [a, b]`: two rooms whose walls face each other across a gap of 1..`max_wall` units (that gap is the wall). `at` = the doorway's centre along the wall (default the middle of the shared stretch); `width`, `height`; `name` (default `"A-B"`). Both rooms need the same floor height. |
| `start` | Bond's start: `room`, `at`, `face`. |
| `light` | Fake lighting baked into the vertex colours: `ambient` plus, per lamp, `intensity / (1 + d² / radius²)`, capped at `max` (1.15), times a face factor (floor 1, ceiling 0.72, walls 0.8 / 0.92 by direction), times `tint`. Lamps sit 20 under the ceiling, on a grid every `lamp_spacing`, or exactly at `lamps = [[x, z], ...]`. |

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
| `monitor` | `wall`, `along`, `y`, `width` (70), `height` (52), `image` (51), `model` | A stock single monitor (Bunker 2's banking-hall screen record), scaled to the box. A use press on it fires `use` with its index and objtype 10 (the heist opens the camera feeds on that). |
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
```

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

## Status (2026-10-25, P2.2)

Done and run: the setup objects above (`objects.json` is the layout on the slot now), `tests/levelgen/run_objects.sh` 33 of 33, `tests/levelgen/run.sh` 11 of 11 on `objects.json`, the heist suites, `tests/coop_determinism.sh` and dam1 (identical), and the screenshots (looked at). The model and record layouts are the stock ones; the only engine-side additions are the `obj.get` / `obj.test_damage` bindings (`port_heist.c`).

## Limits (P2.2)

- Box rooms only (a room is a rectangle); an L-shaped space is two rooms and a wide doorway.
- Every room of a doorway has the same floor height: no stairs or ramps yet.
- One door per doorway, sliding into the wall on one side or a shutter rising into the lintel; no double doors, no swinging room doors (only the safe's door swings). No door frame model: the opening's jambs are the wall's.
- The monitor shows a black screen (stock image 51 is not animated here); the camera feeds are a view switch anyway (`cctv.view`).
- Objects are placed by hand in the layout; nothing checks that two objects overlap.
- The texture scale is set by hand per material; a texture's own size is not read.
- The slot holds one generated level; the layout's `name` must be `levelgen`.
- The lighting is baked vertex colour; stan tile colours (`chr_light`) are per room.

# Levels: stan, rooms, pads, and generating your own

## The three layers of a level

| Layer | File | What it is | Who uses it |
| --- | --- | --- | --- |
| **BG** | `bg_<stage>_all_p` | the visible geometry, split into **rooms** joined by **portals** | the renderer and the room walk ([rendering.md](rendering.md)); bullets test it |
| **Stan** | `Tbg_<stage>_all_p_stanZ` | the walkable floor: small triangles ("stan tiles", 1-3k per level) linked edge to edge | Bond's and chrs' movement and collision, line-of-sight tests (`stanTestLineUnobstructed`) |
| **Setup** | `Usetup<stage>Z` | pads, waypoints, objects, guards, AI, objectives | everything else ([objectives-and-setup.md](objectives-and-setup.md)) |

They are separate. A visible wall stops nothing; **an unlinked stan edge is the wall** for Bond. Shots test the bg
(`bgTestBulletHitBackground`), but in Flatland and the Colosseum bullets pass through the walls and pillars (one-sided,
zero-thickness bg planes; the pillars are holes in the stan plus bg boxes). Chr sight is a stan line, so the pillars
still block guards' sight, and with it their hits on Bond ([guards-and-ai.md](guards-and-ai.md)).

## Stan tiles

- Each tile: an 8-byte header (24-bit id from its name `p<number><letter><subtri>`, the **room byte**, a colour that
  shades chrs standing on it, the point count) plus 8 bytes per point (s16 x, y, z and a link).
- **The link of point k names the tile across edge k -> k+1**, as `(0x80 + byte offset of that tile) >> 3`, so tile n
  links as `16 + 4n`; anything below 16 means no neighbour. Linked tiles share their edge points, so there are no hidden
  steps.
- **A chr's or Bond's room is its floor tile's room byte.** "Same room" tests, `bond.room`, the camera's start room all
  read that.
- `stanBuildRoomData` indexes the first tile of each room and stops a room's search at the next room's tile, so each
  room's tiles must be one contiguous run.
- The player bot navigates on the stan tiles (A* plus string-pulling through shared edges), not on the waypoint graph,
  which is in pieces on some levels (Dam: 2 components).
- Ladders are tiles with a ladder flag (not handled by the bot yet).

## BG rooms and portals

- `header {0, &room_data_table, &portal_data_table, &vis_cmds, 0}` at `.data:0`. Rooms are numbered from 1; entry 0 and
  the last entry are placeholders, and **the last real entry is a sentinel**: room sizes are pointer differences, so N
  rooms need N+1 entries in source order.
- Room data is `0x11 0x72` + raw deflate (Python `zlib` with wbits -15 works), stored as big-endian u32 words. Vertices
  are 16-byte big-endian `Vtx` relative to the room's position; the display list is F3D with a `G_NOOP` carrying a
  texture number that `texLoadFromGdl` expands.
- One portal per doorway: a 4-point quad plus its two rooms. The game orders each portal's rooms by its plane at load
  (`bgOrderPortal`), so winding does not matter.

## Pads and waypoints

- Pads: position, look, up, the stan tile under them. Chrs spawn on pads; `getposstan` finds the tile.
- Waypoints: `{padID, &neighbours, groupNum}`. **Every link must be reciprocal, no self links, each group connected
  inside, group links reciprocal and backed by a real waypoint link**: `init_path_table_links` spins forever on any
  inconsistency.
- Routing goes group by group and crosses each border at the first linked waypoint it finds
  ([guards-and-ai.md](guards-and-ai.md) "Routing"). The Colosseum's 27 BFS groups of 12 pads made routes zig-zag; one
  waypoint per group plus diagonal links fixed it.
- A pad centre lies on a tile edge (the diagonal of its two triangles): warping Bond exactly there breaks line-of-sight
  and pickup tests. Offset by ~20 units.

## Cut stage slots: where generated levels live

GoldenEye has unused stage ids. The fork compiles generated levels into them (all `#ifndef TARGET_N64`):

| Slot | Level | `GE_STAGE` | Generator |
| --- | --- | --- | --- |
| `LEVELID_SHO` (42) | Flatland: a 6000x6000 flat test floor | `flatland` | `tools/gen_flatland.py` -> `port/flatland/` |
| `LEVELID_EAR` (51) | The Colosseum | `colosseum` | `tools/gen_colosseum.py` -> `port/colosseum/` |
| `LEVELID_LEE` | levelgen: JSON box rooms with doorways | `levelgen` | `tools/gen_level.py` -> `port/levelgen/` |

Still free: `LIP`, `RIT`, `WAX`, `PAM`, `ELD` (`bondconstants.h`).

What wiring a slot needs (learned on Flatland):

- **Fetch stan and setup by symbol, not by file name** (`bg.c load_bg_file`, `prop.c proplvreset2`). Adding entries to
  the file table would shift the indexes the ROM-served mapping uses.
- Remove the slot's 64 KB zero stub from `asset_bin_stubs.c` so the generated bg takes the symbol; add the symbols to
  `PORT_ASSET_KEEP_SYMS` so they link with FALLBACK off.
- `levelinfotable` levelscale 1.0 (bg units = stan units = world units).
- `language.c` looped forever on an unknown level: borrow a text bank (Surface's). Map fog in `bgfog.c` rather than
  adding a fog row (that table is ROM-served).
- **The mission-end screen crashed** on a stage with no row in the briefing folder table: the fallback row was a
  chapter header with a NULL briefing name (`strcmp(NULL, ...)` in `fileGetIndex`). The fallback is now the first row
  with a briefing file, preferring Surface's.
- Reserve chr slots: a generated level has no guards, so the stock "count + 10" is too few (`port_flatland.c`).

## The level generator (fork, heist P2.1)

`tools/gen_level.py tools/levelgen/<layout>.json` turns box rooms, doorways, materials (stock texture **numbers**; the
texels come from your ROM), baked lamp lighting and named pads into bg, stan and setup C plus a Lua layout table.
One bg room per layout room, one portal per doorway, stan tiles grouped and room-tagged per room and linked through
doorways, a pad grid per room plus doorway and approach pads, waypoint groups per room and per doorway (so a route
crosses rooms only through doorways). Proven by `tests/levelgen/run.sh`: culling, Bond walking A -> B on stick input, a
guard routing C -> B -> A. Limits: box rooms only, one floor height per doorway, open doorways, no setup objects yet.
Format, textures found and the Lua `LEVEL` table: [`modding/levelgen.md`](../modding/levelgen.md).

## Bunker 2's map (the heist's stand-in bank)

From the BG portal table and the waypoint graph (stan rooms and BG rooms are the same numbers). Full tables:
[`designs/bank-heist-bunker2.md`](../designs/bank-heist-bunker2.md).

- **Start**: pad 49 in room 40, a prison cell behind door idx 75 (key 0x1); the key lies outside in room 39.
- **North tunnel** 68 -(door 72, key 0x1)- 4 - 3 - 2 - 1 -(blast doors 80-85, key 0x4)- **6, the big hall**: 25 of the
  37 monitors (a wall bank at x -477), 8 waypoint pads.
- **Central**: 16 - 13 - 14 - 59 - 60 - 61 (west wing). Offices 11, 12 off 13; the security wing 43, 44, 46 off 14 and 18.
- **East loop**: 48 - 49 - 50 -(58/59)- 51 -(68/69)- 56 ... 47 -(70/71)- 54 -(60/61)- 53 - 52. The four stock key-0x8
  pairs are two antechamber doors and two vault doors; **the vault is rooms 55, 56, 57, 58 and 47**.
- **South/west**: cells 39/40/41, the safe room 26/27, records rooms, the west wing loop with three autoguns.
- 122 pads; 68 are waypoints (pads 50-117).
- The key ladder (stock and the heist's tiers): [objectives-and-setup.md](objectives-and-setup.md).

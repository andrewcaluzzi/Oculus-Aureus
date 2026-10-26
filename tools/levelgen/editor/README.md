# Level editor (levelgen layouts)

A browser tool that opens a level generator layout (`tools/levelgen/*.json`, [modding/levelgen.md](../../../modding/levelgen.md)), draws
each level top down, lets you edit it with live checks, and exports the JSON back in the generator's exact format. `preview.sh`
then shows the result in the game.

No build step and no network: `index.html` loads `layout.js` (the model: JSON reader and writer, stair cores, the checks),
`edits.js` (the edit operations) and `editor.js` (the page).

## Opening it

| How | What you get |
| --- | --- |
| Open `tools/levelgen/editor/index.html` in a browser | an empty editor: **Open** a layout, or drop one on the plan |
| `py -3 -m http.server -d tools/levelgen` then `http://localhost:8000/editor/` | it opens `janus_bank_v3.json` |
| `py -3 tools/levelgen/editor/build_standalone.py out.html [layout.json]` | one file with the scripts inlined and the layout inside (default bank v3); works offline, on a phone, or as a published page |

Deep links: `index.html#level=B1&sel=room:vault&tab=issues&ro=1`. `level` is a level button's name. `sel` is `room:<name>`,
`pad:<name>`, `door:<index>`, `opening:<index>`, `core:<index>` or `start:`. `tab` is `inspect`, `issues`, `layers` or `help`.
`ro=1` opens it view-only.

## What it draws

One sheet per level (B2, B1, G, F1, F2, F3 for the bank; a level is a floor height with at least two level rooms, as
`tools/levelgen_plan.py` does it). A ramp or stair shows on both its levels. Other levels' rooms are faint outlines.

| Thing | Drawn as |
| --- | --- |
| Rooms | filled by `zone` (public, staff, restricted), else by floor height. Open-top rooms are tinted blue-grey. Labels show the name and `y` when the floor is not 0 |
| Ramps and stair flights | treads across the slope, an arrow uphill, `(from→to)` in the label |
| Blocks (counters, pillars, shelves) | brown boxes |
| Doorways | a box across the wall, coloured by the door's key: white = open doorway, grey = unlocked door, GREEN, BLUE, RED, GOLD, floor key, VAULT (cracker only). A white dot marks a double door, an orange disc a padlock, and a black outline a shutter |
| Openings (windows, balconies) | cyan |
| Cameras | an orange dot on the wall, its view (the sweep plus the lens, clipped to the room) and a line to the pad it watches |
| Alarm boxes, monitors, safes, keys, props | red squares, cyan squares, gold boxes, key-coloured discs, brown squares (plus the footprint once footprints are loaded) |
| Named pads, loot pads | grey dots, gold dots |
| Guard posts (`script.heist.areas[].posts`) | blue triangles |
| Patrol routes (`script.heist.patrols`), watchclock stations | a dashed loop per route, a white ring per station |
| Vents (`vent_<id>_a` / `_b`), piton lines (`grapple_<id>_low` / `_high`) | purple and orange dashed lines between the two pads. The end on another level is hollow |
| Bond's start | a white ring |
| Problems | red (error) or amber (warning) outlines |

The **Layers** tab hides any of these and holds the legend.

## Editing

Desktop: **Editing** is on. On a phone or touch screen the editor opens in **View only**; the button in the top right switches modes.

| Do | How |
| --- | --- |
| Select | tap or click. Pads, objects and the start win over doorways, doorways over rooms, and a small room over a big one. The **Selected** tab shows it and its problems |
| Move | drag. A room carries its pads, loot, blocks, objects, lamps and Bond's start, and its doorways and openings slide with it along their walls. A doorway or opening slides along its wall. A pad may move into another room. A camera, alarm or monitor snaps to the nearest wall of its room. A stair core moves whole |
| Resize a room | drag its yellow handles (the contents stay put) |
| Add a doorway | **Door** tool (`D`): click a wall two rooms share. It takes the default width, centred where you clicked, snapped, kept 20 units off the corners. A second doorway between the same rooms gets its own name |
| Change a door | in **Selected**: none / sliding / shutter, the key rule (unlocked, GREEN, BLUE, RED, GOLD, floor key, GREEN + floor key, VAULT), double, see-through, padlock, starts open, guard access, `at`, width, height, name |
| Add a pad, a room | **Pad** tool (`P`): click in a room. **Room** tool (`R`): drag a box (it copies the floor, height, zone and materials of the room under it) |
| Rename | a room: every doorway, opening, the start and the heist areas follow. A pad: camera `watch`, guard posts, patrols, stations and civilians follow |
| Delete | `Del` or the button: rooms (with their doorways), doorways, openings, pads, objects |
| Undo / redo | `Ctrl+Z` / `Ctrl+Y` (120 steps) |
| Snap | the `snap` box (1, 10, 20, 50, 100 units) |
| Export | **Export JSON** or `Ctrl+S`: downloads the layout under its own file name |

Keys: `V` select, `F` fit, `1`-`9` levels, `Esc` deselect. Pan by dragging empty floor; zoom with the wheel or a pinch.

Stair-core rooms (`stairwells`) are generated, so you move the core, not its flights. A layout's other keys (materials, lights,
`script`, comments) are kept as they are.

## The checks

They run on every edit. **Checks** lists them; tap one to go to it. Tags: **gen_level** = `tools/gen_level.py` stops on it; **engine** =
a game limit the generator does not test; *warn* / *info* = worth a look.

Ported from `gen_level.py`, with its own messages:

- the stair cores (axis, rising levels, big enough)
- each room's rect, materials (floor, wall, ceiling, per wall), ramp axis, blocks (inside, materials, not on a ramp), duplicate names
- overlapping rooms (the exact test: rooms on different floors may share x, z when one is wholly above the other, ramps by their planes)
- doorways: rooms that exist, facing walls within `max_wall`, the opening on the shared wall with 20 units' margin, not in a ramp's
  side wall, level floors on both sides, frame material, not taller than a room; door type (slide or shutter; a double door slides)
- openings: facing walls, on the shared wall, not in a ramp's wall, `y` inside both rooms' walls (with the frame)
- the limits: 140 rooms and 190 portals (doorways + openings), coordinates within ±32000, the layout's `name`
- doorway clearance (P2.4): blocks always; props once you load their footprints (below), with the generator's numbers
- named pads, loot pads and the start on their room's floor (the room's rect and its doorways' floors; a pad in a block is a warning,
  since the generator may take a tile up to 40 units off)
- objects: wall names, camera `watch` pads that exist, safes not on ramps, known object types
- reachability: every room with pads must be reachable through doorways from the start's room (gen_level: "pads are not reachable
  from the start")

Added by the editor:

- **engine**: 32 cameras at most (`port_heist.c` `CCTV_BOND_CAMS`; the bank v3 test holds it too)
- *warn*: pad names used twice; a wall object's `along` off its wall; heist table names (`script.heist`: posts, panels, cameras,
  civilians, patrols, stations, vents, grapples, area rooms) that are not in the layout, as `build_janus_bank_v3.py check_names` checks

Not ported (gen_level still checks them when you preview): the tile and pad totals (16380, 10000), the floor grids settling, a room
whose pads are cut in two by blocks, and the stan links across doorways. They need the whole floor mesh.

**Prop footprints.** The clearance check needs each prop model's size, which comes from your extracted assets (not the repo).
`preview.sh` writes `build_preview/footprints.json`, or run `python3 tools/levelgen/editor/footprints.py <layout> <out.json>`. Drop that
file on the editor; until you do, Checks says how many props it skipped.

## Preview in the game

```
tools/levelgen/preview.sh my_layout.json [room ...]        # Linux shell with Docker; on Windows, in WSL from the repo
```

1. It generates the layout into `build_preview/levelgen/` with `gen_level.py`, and stops with the generator's message if the layout is refused.
2. It builds `build_preview/`, a build dir of its own (CMake `-DLEVELGEN_DIR=build_preview/levelgen`), so `port/levelgen/` and
   `build_pc/` are never touched. The first run builds everything, which takes minutes; later runs rebuild only the level.
3. It boots the game headless in a throwaway container (Xvfb, `GE_STAGE=levelgen`) with `tests/levelgen/levelgen_preview_shots.lua`.
   That script takes Bond's start, then one shot per room you name, or the five largest rooms. For each room, Bond stands on the
   room's south-west grid pad and looks across.
4. It writes `build_preview/shots/NN_<room>.png`, plus `gen_level.log` and `shots/game.log`.

Environment: `IMAGE` (default `goldeneye`), `JOBS` (build `-j`, default 2), `SHOTS` (how many rooms when none are named),
`GE_FRAMES`. It needs `baserom.u.z64` and the extracted assets in the repo root, as any build does.

## Files

| File | What |
| --- | --- |
| `index.html` | the page and its styles |
| `editor.js` | drawing, input, panels, deep links |
| `layout.js` | `parse` / `stringify` (byte-exact with Python's `json.dump(indent=1)`), `expandStairwells`, `build`, `validate` |
| `edits.js` | the edit operations, shared with `cli.js` |
| `cli.js` | node: `roundtrip`, `validate [--footprints]`, `edit <ops.json>` |
| `build_standalone.py` | the one-file copy (`--selftest` adds `selftest.js`) |
| `selftest.js` | drives the page with synthetic input (`#selftest`): tap, drag, undo, the door tool and its rule, delete, export |
| `footprints.py` | prop footprints from your extracted models |
| `../test_editor_roundtrip.py` | the tests (below) |
| `../preview.sh`, `tests/levelgen/levelgen_preview_shots.lua` | the in-game preview |

## Testing

`py -3 tools/levelgen/test_editor_roundtrip.py` (or `python3`) runs 9 tests. They skip when node or a headless Chromium/Edge is missing.

- **Round trip.** Every shipped layout, read and written by `layout.js`, matches Python's `json.dump(indent=1)` byte for byte.
  `janus_bank_v3.json` comes back identical to the file. Types survive too: `1.0` stays a float. A synthetic layout covers escapes,
  non-ASCII, 1e-05 / 1e16 floats, -0.0 and empty containers.
- **Agreement.** The small test layout and bank v3 (with footprints) pass both gen_level and the editor. 30 broken layouts each fail
  both, and gen_level's output contains the editor's message for the fault. One of them is a prop moved into a doorway on bank v3.
- **Edits.** Rename, move, add a pad or a doorway, or set a door rule, and the layout still generates. Moving a room carries its
  contents. Removing a doorway strands exactly the rooms behind it.
- **Page.** `selftest.js` runs in headless Edge or Chrome (`--dump-dom`): 17 checks.

Why byte-exact: the layouts are generated files (`build_janus_bank_v3.py` writes `janus_bank_v3.json`). With byte-exact export,
opening and saving changes nothing, and a real edit shows up in `git diff` as only the lines you changed. Python writes a whole float
as `1.0`, so the reader keeps such numbers as floats (`PyFloat`). Python's `ensure_ascii` escapes DEL and every non-ASCII character,
so the writer does too.

Bank v3 is written by `build_janus_bank_v3.py`. If you edit `janus_bank_v3.json` in the editor, the next run of that script
overwrites the edit. Carry the change into the script, or make the edited JSON the layout you keep.

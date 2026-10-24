# Bank Heist P0 job 1: Bunker 2 probe results

Source: `tools/mode_probe.lua` + `port/src/port_probe.c` (Lua `probe.*`), run as a mode on Bunker 2 (`GE_STAGE=bunker2`, stage id 27). Every number below was logged by the probe from the loaded setup, except where marked **inferred** (from coordinates) or **not run**. Coordinates are world units (x, y, z; y is up). "Room" is the stan-tile room id of the object or pad.

Re-run (Linux build, Xvfb, no visible window):

```
GE_MODE=probe GE_STAGE=bunker2 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FRAMES=1500 DISPLAY=:99 ./ProjectMidas
```

Optional switches in `mode_probe.lua`: `GE_PROBE_KEEPLOCKS=1` (calls `mode.keep_locks()`), `GE_PROBE_CCTV=1` (warp Bond into camera 95's view and log the alarm timer), `GE_PROBE_SCAN=1` (warp Bond to every pad, log each camera's live state), `GE_PROBE_TOUR=1` (warp through a fixed pad list for screenshots).

---

## 1. Do a mode's objects survive? Yes.

`probe.level()` lists the setup records and how many became live props. In a mode on Bunker 2: 38 of 38 doors, 6 of 6 CCTV, 37 of 37 monitor records (single monitors) plus racks, the safe and its linked door, 39 of 40 world props are live. Only things tied to chrs are missing: the 37 guard records ("setup lint: guard N did not spawn"), the key and weapon records assigned to those chrs (`live=0`), and the level AI. The stock camera code keeps ticking: a live test put Bond in camera 95's view and the alarm fired (section 4).

One thing the reset does add: the UNLOCK DOORS cheat is turned on one second into the stage (`portModeOneSecond`), so every keyed door opens for Bond. `probe.doors_unlocked()` returned **true** by default and **false** with `mode.keep_locks()` at script top level. `modding/lua.md` line 19 (it said objects were wiped) is fixed, and the new calls are listed in its reference.

## 2. Doors and keys

38 door records. 22 are keyed (grouped by key flag below) and 16 are unkeyed. The keyed ones:

| Key flag | Opens (door idx, rooms, position) | Holder |
|---|---|---|
| 0x1 | cell idx 75 (rooms 40/39, -3473,121,4769); exit door idx 72 (rooms 4/68, 0,462,-2468) | **Floor pickup**, not a guard: key model 244 at -3288,167,4993 (room 39, the cell corridor). Setup pad 10076 |
| 0x2 | cell idx 77 (rooms 41/39, -3051,121,4769) | chr 18: guard idx 240, spawn pad 100 (-2286,65,4923, room 39), ailist 0x404, health 1000 |
| 0x4 | idx 64/65 (25/24, -1091,129,4973/4863); idx 66/67 (37/36, -2352..-2463,300,2478); idx 80-85 (room 1, triple blast doors near 0..100,114,-573) | chr 34: guard idx 263, pad 105 (-3388,185,2103, room 61), ailist 0x403. Key model 86 (swipe card) |
| 0x8 | idx 58/59 (51/50, 528,129,2993/2881); 60/61 (53/54, 528,129,4862/4974); 68/69 (56/51, 856..967,129,3081); 70/71 (47/54, 856..967,129,4774) | chr 26: guard idx 253, pad 81 (916,65,4906, room 54), ailist 0x401, health 500. Key model 86 |
| 0x10 + 0x20 (0x30) | **safe door** idx 102 (room 26, -762,77,4474, door type 5, autoclose never) | 0x10: chr 11, guard idx 231, pad 10 (-1457,65,4074, room 27, next to the safe room). 0x20: chr 6, guard idx 224, pad 5 (-1626,208,302, room 11). Key model 243 for both. The door needs both bits |

Each key is a `KEY` record with `PROPFLAG_ASSIGNEDTOCHR` (0x4000): its `pad` field holds the **chrnum**, not a pad. There is no key in the safe (the design doc's "flag 1 in the safe" was wrong): the safe holds two model-204 collectables (weaponnum 5, pads 43 and 45, y 27) and one model-270 prop (pad 44). Guard chr 20 also carries a model-269 prop (idx 246). Other doors: idx 78/79 (type 4, rooms 46/43 and 46/44, autoclose 1500) are the only non-standard unkeyed doors; the rest (idx 46-57, 62, 63) are plain 300-tick auto-close doors. Raw `flags2` bits 0x8000000 / 0x10000000 appear on the keyed pairs (the stock one-way and key-type bits); not decoded further.

**Locks under a mode.** Default: all open (cheat). With `mode.keep_locks()`: the keyed doors stay locked, which exposes a start-state problem: **Bond's start pad 49 (-3404,91,4539) is in room 40, behind cell door idx 75 (key 0x1), and key 0x1 lies outside in room 39.** Stock level AI opens that cell; with it gone, the mode needs a `door.open`/unlock binding (or a start warp) or Bond is locked in. **Inferred** from rooms; not tested by walking out.

**Recreating a key on a chosen chr.** The stock path is `domakedefaultobj` (`src/game/prop.c:221-238`): a record with `PROPFLAG_ASSIGNEDTOCHR` finds the chr by `chrFindByLiteralId(rec->pad)`, builds the object model and calls `chrpropReparent(obj, chr->prop)`, so it drops when the chr dies. For a `spawn.key(chrnum, keyflags [, model])` binding: allocate a `KeyRecord` the way `spawn.item` does for armour records, set `keyflags`, `flags = PROPFLAG_ASSIGNEDTOCHR`, `pad = chrnum`, run it through the same create function after the chr exists. Needs the chr to have a model and prop (the check on line 225). Keys for the same flags can also be placed on a pad (`pad` real, no flag) as a floor pickup, which is how flag 0x1 works now.

## 3. Cameras (6, all live in a mode)

Facing is degrees clockwise from +Z toward +X; the cone is +-45 degrees about (facing + sweep), as in `propobj.c:5647`. `lookpad` is the pad the camera looks at. The raw range field reads -100 on all six (no range limit is applied by the stock tick; it is cone + stan line of sight only).

| Idx | Position | Room | Facing | Sweep | Look pad |
|---|---|---|---|---|---|
| 87 | -2100,441,2072 | 44 | 93 | +40..-20 | 78 |
| 89 | -2128,441,730 | 11 | 109 | +-30 | 74 |
| 91 | -1001,412,1661 | 16 | 360 (0) | +-30 | 71 |
| 93 | -1387,655,2135 | 46/44 | 0 | +-30 | 82 |
| 95 | 318,466,-870 | 8 | 65 | +-30 | 57 |
| 97 | -408,206,2842 | 49 | 359 | +-30 | 87 |

Live test (`GE_PROBE_CCTV=1`): Bond warped to pad 57 (777,182,-660, 591 units from camera 95, bearing 65). Camera 95's sight timer rose 60 per second; the alarm rang at 540 -> 600 ticks, which is **10 s at Agent difficulty** (`F_80030B14` = 2.0, `lv.c:1533`) and **5 s (300 ticks) at Secret Agent / 00 / 007**. The siren counter then counted up (alarm_timer 2, 62, ... 422 ticks) and the camera timer reset to 0 and refilled. The other five cameras stayed at 0. So the CCTV runs in a mode, and the alarm threshold depends on difficulty (a mode that wants a fixed 5 s should override that).

No alarm boxes exist on Bunker 2 (0 `PROPDEF_ALARM`).

## 4. Safe, monitors, cells

- **Safe**: `PROPDEF_SAFE` idx 101 (-705,77,4473, rooms 26/27), door idx 102 (key 0x30), 3 linked items: two model-204 collectables (weaponnum 5) at pads 43 and 45, one model-270 prop at pad 44. Pads 43-45 sit at y 26 inside the safe.
- **Monitors**: 37 single-monitor records plus 2 racks (idx 155 and 160, pads 28 and 29). By room: **room 6: 25** (22 wall-bank screens at x=-477, z 148..794, y 160..470; 2 rack tops; 2 desk screens at 904,125,383/511), room 43: 2, room 27: 2, and one each in rooms 8 and 10. Eight monitor records (idx 156-164) use pad -1 (they are the rack's sub-screens, no own position).
- **Control room = room 6.** It is the large hall at x -500..1000, z -600..1300, 25 monitors, 8 waypoint pads (54, 55, 58-65) and 6 guard spawn pads.
- **Cells / gates**: 2 cell doors (`PROP_DOORPRISON1`): idx 75 (key 0x1, rooms 40/39) and idx 77 (key 0x2, rooms 41/39). No chain-gates. The cell block (rooms 39/40/41) is the natural bullion cage.
- Autoguns: 3 (idx 98-100, rooms 67, 62, 65, west wing).

## 5. Rooms, with the pads in each

Pad table: 122 pads. 68 are on the AI waypoint graph (wp=1: pads 50-117). Pads 0-49 and 118-120 are not waypoints (guard spawn, item and prop pads). Pad positions and rooms come from the log; areas are a reading of the coordinates (**inferred**, not checked on screen).

| Area | Rooms | What is there | Pads (waypoint pads in bold) |
|---|---|---|---|
| **Cell block / start** | 39, 40, 41 | Bond start (pad 49, room 40), cells idx 75 and 77, key 0x1 on the floor, chr 18 and chr 0 spawn here | 49, 94; 16, 91, 96, 99, 100 (room 39); 95, 97, 98 (room 41) |
| **West wing** | 61, 65, 66, 67, 36, 63 | Key 0x4 carrier (chr 34, pad 105), 3 autoguns, upper walkway | **102-105** (61), **106-108** (65), **109-111** (66), **112, 113, 116** (67), **114** (36), **115** (63); 23, 24, 48 (65); 39 (67, y 662), 40, 41, 42 (62/65, y 447) |
| **Central corridors** | 10, 11, 12, 13, 14, 16, 18, 43, 44, 46, 59 | Cameras 87, 89, 91, 93; 2 monitors in 43 | **67-79** (e.g. 67 room 16, 72-76 room 11), **82, 83** (43), **117** (10), **71** (18); 21, 22, 47 (10), 46 (43), 4, 5 |
| **Safe room / records** | 25, 26, 27, 28, 29, 30, 52 | Safe (26/27), chr 11 (key 0x10) pad 10; keyed doors 0x4 at idx 64/65 | 10, 11, 15 (27/29), 12 (26), 43-45 (safe interior); **92** (30), **93** (25), **101** (27), **80** (52) |
| **East wing** | 47, 48, 49, 51, 54, 55, 56, 57, 58, 22, 20, 33 | Key 0x8 carrier (chr 26, pad 81), camera 97 (room 49), four 0x8 door pairs | **81** (54), **85** (51), **86** (49), **87** (48), **88** (47), **89** (56), **84** (20), **90** (33); 8, 17 (56), 9, 18, 30-37 (room 55, 10 pads), 19 (58), 20 (57), 13 (48), 14 (22) |
| **Control room hall** | 6 (plus 5, 7, 8, 9) | 25 monitors, racks (pads 28, 29), camera 95 in room 8 | **54, 55, 58-65** (6), **52, 56** (7), **53, 57** (8), **66** (5); 6, 7 (guard), 26, 27 (floor y 0), 28, 29 (rack tops), 0 (8), 1 (9), 2, 3 (7) |
| **North exit tunnel** | 1, 2, 4, 68 | Blast doors idx 80-85 (key 0x4, room 1), exit door idx 72 (key 0x1, rooms 4/68), final corridor | **50** (4), **51** (68, 0,569,-4187); 25 (room 2, -17,65,-1387), 118, 119, 120 (68, not waypoints) |

### Candidate pads per area

| Role | Candidates | Why |
|---|---|---|
| **Entrance** (player start for a heist from outside) | 25, 50 (north end, rooms 2/4). Fallback: keep pad 49 | Behind the key-0x4 blast doors; an approach to the hall. Pad 49 is the stock start (a cell) |
| **Control room** (room 6) | Hall floor 54, 55, 59, 61, 62, 63, 64, 65. Viewing stations 7 (-167,65,-315), 62 (9,65,-347), 63 (-20,65,1292) | Room 6 has 25 monitors. The wall banks are at x=-477 |
| **Cells / cage** | Rooms 40, 41: pads 49, 94, 95, 97, 98, doors idx 75 and 77 | Two real cell doors. Keys 0x1 and 0x2 |
| **Safe room** | 43, 44, 45 (inside), approach 12, 11, 10, 101 | Safe at pad 10073 position (-705,77,4473); door needs 0x10 and 0x20 |
| **Exits** | A: 51 (0,569,-4187) behind door idx 72 (0x1). B: 120 or 119 (room 68). C: far east end 18 (1613,65,4367, room 55) or 9 (1233,65,4671). D: west upper 39 (-3740,662,3087, room 67) | Candidates only; no exit is built |
| **Guard posts** | Waypoint pads. Corridor pairs 67-79; east wing 81-89; west wing 102-113; hall 54-65 | Wp pads are known walkable |
| **Camera landing pads** | Look pads 57, 71, 74, 78, 82, 87 | Where the six cameras point |

## 6. Weapon loudness and hearing radius

Formula (read from source): `chrCheckGuardsHeardSound(noise)` (`chr.c:3998`) alerts a chr to Bond's position when `distance_to_Bond < hearingscale * noise * 100`. `noise` is the larger of the firing hands' loudness (`bondview2.c:8252-8257`). Each tick that a hand fires, `gunTickNoise` (`gunfire.c:5132`) adds the gun's `NoiseIncreasePerShot` to that hand's noise, capped at `LoudnessMax`, and it decays toward `LoudnessMin`. **`hearingscale` is 1.0 for every chr** (`chr.c:1742`); a script can retune it (`SET_HEARING_SCALE`). The distance is straight-line (no walls); only the shooter's own Bond counts. Unarmed fire adds no noise (`gunfire.c:5148`).

Values from `weapon.get`, radius in world units at scale 1.0:

| Gun | loudnessmin | loudnessmax | per shot | Radius at rest | After 1 shot | At cap |
|---|---|---|---|---|---|---|
| PP7 unsilenced (`wppk`) | 1.0 | 12.0 | 2.0 | 100 | 300 | **1200** |
| **PP7 silenced (`wppksil`)** | **0.0** | **5.0** | **1.0** | **0** | **100** | **500** |
| KF7 / AK47, Uzi, MP5K, Spectre, M16, FN P90, grenade launcher | 2.0 | 20.0 | 2.0 | 200 | 400 | 2000 |
| MP5K silenced (`mp5ksil`) | 0.0 | 7.0 | 1.2 | 0 | 120 | 700 |
| Sniper rifle | 0.0 | 7.0 | 1.2 | 0 | 120 | 700 |
| TT33 | 2.0 | 16.0 | 3.0 | 200 | 500 | 1600 |
| Shotgun | 2.0 | 25.0 | 4.0 | 200 | 600 | 2500 |
| Golden gun | 1.0 | 9.0 | 1.5 | 100 | 250 | 900 |
| Rocket launcher | 2.0 | 25.0 | 10.0 | 200 | 1200 | 2500 |
| Knife, throwing knife | 0.0 | 1.0 | 1.0 | 0 | 100 | 100 |

Reading it for the heist: a silenced pistol is heard within **100 units on a first shot** (about a metre; a standing guard in the same corridor can still be farther), climbing to **500 at full cap** after about five rapid shots; the unsilenced PP7 is **300 on one shot and 1200 sustained**. A corridor on this map is 100-300 units wide, rooms 500-1500. Decay rates (`NoiseDecayLinearTime` / `NoiseDecayScaledTime`) are in the weapon data and were not logged; the table above is steady-state peaks.

## 7. Camera view switch: feasibility (built in P1: designs/bank-heist.md section 10, port/src/port_cctvview.c)

(As built: the prop and room matrices are not rebuilt for the camera; the two projection matrices the display list loads are rewritten instead, and the chrs only the camera sees are posed for the feed with their Model structs restored. The "first hour" function is `bondviewUpdateCameraMatrices` (bondview2.c), called from `bondviewRenderDebugBondView`, which is the normal per-pass camera setup despite its name.)

**Goal.** Using a monitor moves the player's view to a CCTV camera (static or black if the camera is shot), render-only; the sim keeps Bond's canonical sim view, so it is safe for co-op and netplay.

**How it fits the existing code (read, not run).** The netplay work already splits two worlds (`bg.c:6314-6324`, `port_roomvis.c`):
- SIM visibility is computed per player from the canonical sim view (320x240, vanilla FOV) and per-player portal state. Every sim read (on-screen flags, spawn gates, hit tests) takes that snapshot.
- RENDER visibility is a second, separate walk from the drawing Bond's real camera and window, kept per player (`s_RenderList`) and swapped in only for the world draw (`bgLevelRender`). It already runs whenever the window is not the canonical view (`portRenderWalkAlways`, `GE_NET_RENDERWALK=1` forces it).

So a camera view is a third camera for the same render-walk slot: when the local player's "monitor view" is set to camera N, the render pass starts its walk at the camera prop's stan tile and uses the camera's position, yaw (base `unkC4` + 180 deg + live sweep `unkC8`), a fixed pitch and a chosen FOV for the view matrix, instead of Bond's. Nothing in the sim walk changes. The view state is a per-local-player presentation flag, like the watch gate.

**Pieces.**
1. Lua: `view.camera(cam_index | nil)` for the local player (and the existing `cctv.list` binding from P0 job 2 for camera data).
2. Draw setup: override the camera matrix and fov and the render walk's start room for that player's pass (the same place co-op overrides the real camera).
3. A dead/disabled camera (`PROPFLAG_CCTV_DISABLED`, or destroyed): skip the world draw and draw a static or black full-screen quad (trivial).
4. Gate input: while viewing, Bond must be defenseless (no fire, no move). That is a sim state ("using a console"), so it must be sim-side and synchronized; only the camera choice is presentation.
5. Hide the first-person gun and HUD parts that read Bond's matrices; add an on-screen "CAM n" label.

**Risks.**
- **Chr and prop freshness (the big one).** Characters animate and rebuild matrices only when their rooms are in the sim's visible list. Guards in the camera's room but not in Bond's would draw with a stale pose or not at all. Forcing them to update would change sim state (animation clocks, RNG), which co-op forbids. Presentation-only options: draw the last pose, or use the crowd-atlas impostors (design section 5, T2). First version could accept frozen-looking guards in other rooms. **Needs a prototype to see how bad.**
- **Prop matrices are read back.** The design notes (and `netplay.md`) say drawing props from another viewpoint rebuilds matrices the sim reads (`model->render_pos`), which is why props never followed the window. The render walk already avoids this for chrs; the camera view needs a check that a pass from an off-room camera cannot touch what the sim reads. Test with the existing `GE_ROOMLOG`/diverge checks plus a hash comparison with the view on and off.
- **Room visibility/onscreen flags.** The camera's room may have closed portals/doors; those are world state, so they behave correctly. The `onscreen`/autoaim flags come from the sim walk and stay Bond-based, so Bond's view of a guard does not change; the player cannot aim anyway (item 4). Doors: door vertex data is shared (`GE_NET_TEST_DOORVTXOLD` history), so a door drawn from the camera pass must not mutate it; same check as props.
- **Camera inside its own model/near plane**, fog and skybox tables per room: cosmetic.
- **Co-op/netplay**: view state is local; the remote Bond's immobile avatar is ordinary sim. Replays and demos (`dam1` baseline) are untouched if all of it is `#ifndef TARGET_N64` and off by default.

**Effort: M (about 3-5 days).** About a day for the matrix/fov/start-room override in solo plus the Lua binding; 1-2 days for the chr/prop freshness and read-back checks (the unknown); about a day for static/black, input lock, HUD/gun hiding and a co-op soak. If chr freshness turns out to need impostors, add the T2 effort (M-L) to that slice. Not traced: the exact function that builds the view matrix (the `bondviewSetupCamera`-style entry point); finding it is the first hour.

## 7b. Room graph and where the keyed doors sit (job 6)

From the BG portal table (`assets/obseg/bg/bg_sevb_all_p.c`, `portal_data_table`: room pairs) and the waypoint graph (`UsetupsevbZ.c`, `path_table_*` by waypoint, `pathwaypoints` for their pads); stan rooms and BG rooms are the same numbers.

- North tunnel: 68 -(door 72, key 0x1)- 4 - 3 - 2 - 1 (blast doors 80-85, key 0x4) - 6 (the hall). 6 also joins 5, 7, 8; 5 - 15 - 16.
- Central: 16 - 13 - 14 - 59 - 60 - 61 (west wing); 16 - 17 - 18 - 19 - 20 - 21 - 48 (east loop). Offices: 13 -(48/49)- 12 -(46/47)- 11. Security: 14 -(50/51)- 44 - 46 - 43 -(52/53)- 18; 46 also joins 42, 45; doors 78/79 sit inside 46.
- East loop: 48 - 49 - 50 -(**58/59, key 0x8**)- 51 -(**68/69, key 0x8**)- 56; 56 - 55 - 57 - 58 - 47 (the vault rooms, crates in 55); 47 -(**70/71, key 0x8**)- 54 -(**60/61, key 0x8**)- 53 - 52 - 23/24 - 22 - 48. So the four stock 0x8 pairs are two antechamber doors and two vault doors.
- South / west: 52 - 24 -(64/65, key 0x4)- 25 - 30 -(62/63)- 39 (cells: 40 behind cell door 75, 41 behind 77; 40 - 41 also share a portal); 25 - 28 - 27 (safe room, safe in 26), 27 - 29; 30 - 31 - 32 - 33 - 34 - 63 - 64 - 67 - 66 - 65 - 61 (west wing loop), 35 - 36 -(54/55)- 10, 36 - 37 -(66/67, key 0x4)- 59.
- Rooms 24 and 37 have no pads (short passages behind the 0x4 doors 64/65 and 66/67); rooms 50 and 53 have none either (the antechamber corridors between 49/51 and 52/54). The waypoint edge 50-62 (tunnel to hall) passes through the blast doors in room 1.

A tool for this: a throwaway script read the probe log's `PAD` lines (rooms) and the setup's path tables and printed every waypoint edge with the doors whose two rooms match it.

## 8. P0 plan changes from this probe

1. **Key flag 1 is a floor pickup, not in the safe.** The safe door needs keys 0x10 and 0x20 together (chr 11 and chr 6) and holds two ammo-class collectables and one prop.
2. **Bond starts in a cell (room 40) behind a key-0x1 door** while keys are on. P0 job 2 must include `door.open/unlock` (or the mode warps Bond out), alongside `mode.keep_locks()` (added here).
3. **Doors are all open by default in a mode**; the mode must call `mode.keep_locks()` before the 1 s mark, then hand out keys itself with `spawn.key`.
4. **CCTV alarm time is difficulty dependent** (10 s Agent, 5 s above). Plan an explicit override in `cctv.*` if the heist wants a fixed value.
5. **Bunker 2 has 6 cameras and 0 alarm boxes, as planned**; the control room is room 6 (25 monitors).
6. Silenced pistol hears at 100-500 units; guard post spacing in the mode should respect that.

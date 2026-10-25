# Objectives, setup files and per-level findings

## Setup files

A stage's setup (`assets/obseg/setup/Usetup<stage>Z.c` in an extracted tree; Jungle and Train under `setup/u/`)
holds:

- `propDefs[]`: every placed thing. Guards are type 9; objects, doors, keys, cameras, monitors, safes, switches and the
  objective records (types 23-34) follow the same header format.
- `ailists[]`: the stage's AI scripts (`ai_N` runs as list 0x401 + N) written with the `bondaicommands.h` macros, whose
  arguments are **byte-swapped** ([guards-and-ai.md](guards-and-ai.md) "The AI bytecode").
- pads (position, look, up, stan tile name), bound pads, waypoints and waypoint groups, patrol paths, the intro
  (spawn pad, start weapons and ammo).

**By default these come from your ROM, not from the `.c` files** (`port/src/romfile_setup.c` swaps them in place).
Editing a setup `.c` has no effect unless you build with `GE_ROMDATA_FALLBACK=ON` and run with `GE_ROMSETUP=0`. The
`.c` files are the readable reference. `prop.c` builds the setup file name and inserts `mp_` for 2+ players.

Useful record facts (Bunker 2 probe, `designs/bank-heist-bunker2.md`):

- A key with `PROPFLAG_ASSIGNEDTOCHR` (0x4000) keeps the **chrnum in its `pad` field**. `domakedefaultobj`
  (`prop.c` ~221-238) reparents it to that chr's prop, so it drops when the chr dies. Without the flag the key lies on
  its pad.
- A door opens if the Bonds' held keys cover its 32-bit `keyflags` (`propdoorInteract`). `openPosition` runs from 0 to
  `maxFrac`; `frac` is the door's size. A padlocked door (`LockDoorRecord`) stays shut until its lock object is shot.
- Wall consoles that work a door are `PROPDEF_SWITCH` records (`g_LevelLoadPropSwitch`: first the switch, second the
  door). Dam's paired tunnel gates open only while the sibling is shut (`DOORSTATE_WAITING`).

## What a Lua mode removes

A mode (`GE_MODE=<name>`) wipes the stage's **guards, objectives and level AI** but keeps every object: doors, locks,
padlocks, cells, cameras, monitors, safes, pickups, pads, paths (`prop.c` `proplvreset2`, `port/src/port_mode.c`).
Confirmed on Bunker 2: 38/38 doors and 6/6 cameras live. The reset also turns on the UNLOCK DOORS cheat one second in;
`mode.keep_locks()` at script load prevents it. The stock mission end (`bossReturnTitleStage`) is suppressed in a mode:
modes end through `portModeEndMission` (`mission.win` / `mission.fail`).

## Objective types and how their status is computed

Objective records (types from `tools/bot/objectives.py`, bot branch):

| Type | Record | Status (`get_status_of_objective`, `src/game/objective_status.c` ~178) |
| --- | --- | --- |
| 23 / 24 | ObjectiveStart / End | brackets one objective; Start carries the text and the minimum difficulty |
| 25 | destroy | incomplete while the tagged object exists and is healthy |
| 26 | complete-on-flag | incomplete until the stage flag is set |
| 27 | fail-on-flag | failed once the stage flag is set |
| 28 | collect | **failed** if the tagged object is gone or destroyed, else incomplete until it is in an inventory |
| 29 | deposit | incomplete while the tagged object is still carried |
| 30 | photograph | incomplete until the "taken" field is set by `objectiveTakePictureHandler` (~512); failed if the object is destroyed first |
| 32 | enter-room | incomplete until `objectivestatusCheckRoomEntered` sets its field |
| 33 | deposit-in-room | incomplete until `objectivestatusCheckDeposit` sets its field |
| 34 | copy-item | incomplete until the key analyser flag is set |

How it works:

- **Status is recomputed on demand**, not latched: each call walks the objective's records, and the result is the
  first non-complete record's status (a later "failed" overrides "incomplete"). So a collect objective whose item is
  dropped on the floor goes back to incomplete, and picking it up again completes it.
- `objectiveIsAllComplete` (~348) checks only objectives whose minimum difficulty is at or below the current one.
- Stage flags are the register `objectiveregisters1`, read with `chrHasStageFlag(NULL, flag)`. Level AI sets them
  (`objective_bitfield_set_on`, byte-swapped in the macro). To plan a level, find which AI line sets each flag and the
  conditions before it; `tools/bot/objectives.py <setup.c>` prints exactly that.
- In co-op (fork) `bondinvHasPropInInv` and the key tests check every Bond's inventory.

### The photo objective depends on rendering

`objectiveTakePictureHandler` judges the picture with `objGetOnscreenRenderBounds` (`propobj.c` ~12129), which requires
`PROPFLAG_ONSCREEN` and reads the object's **model matrix from the last draw** (`getsubmatrix(obj->model)`, the
render matrices). Only ticks that draw refresh those. On the N64 every tick draws, so this never shows; on PC a run
that draws 1 tick in 60 (sim-only ticks) and its replay that draws none disagree about whether the photo was taken
(seen on Bunker 1 and Silo, right after photos). Replays must draw the same ticks as the recording. The objective wants
the object's whole box on screen; the bot photographs from 350-850 units with the box centred.

## Per-level findings

From the player bot's campaign runs (bot branch, `modding/bot-notes.md`, Agent). "Completes" means the stage's own
mission end with all objectives done, and the recording replayed to the same result without the bot.

| Stage | What we learned |
| --- | --- |
| Dam | Completes (157-250 s). The tunnel gates are a switch pair; from some approaches neither console of gate 1 is reachable. A padlock on the way is shot off. |
| Facility | The key comes from its carrier (a scientist; only Secret Agent+ counts his death). Gas tanks leave gas that drains health; the bottling room (catwalk guards + gas) is where runs die. |
| Runway | Completes in 60-72 s. |
| Surface 1 | Completes: the console, then the vent hatch's four padlocks (they lie on the floor: aim at the lock's origin from any range), then down. |
| Bunker 1 | Completes. The main screen hangs on a wall and its own tile is behind it: the photo needs a tile with a clear line to it. The way out is the room of pad 100. Replays can miss the photo (above). |
| Frigate | The tracker bug is planted on the helicopter (gadgets on big targets from 250 units); the hostage objective times out (captors the route cannot reach). |
| Bunker 2 | Bond starts unarmed in a cell (pad 49, room 40) behind a key-0x1 door; key 0x1 lies outside in room 39. The magnet watch (equip, face the key, hold Z) can pull it in; otherwise wait for a guard to open up. Punch a guard for a gun. Natalya's cell (door tag 3) opens with key 0x2 from guard chr 18. Completes on some seeds. |
| Statue | See below. Completes 5/5 since round 4. |
| Archives | Bond starts unarmed (the bot punches). |
| Silo, Depot, Streets, Jungle, Cradle, Egypt | Die in fights; combat (several guards with a line at once) is the blocker, not navigation. |

### Statue: the full chain

Read from the Statue setup's AI by the bot agent and confirmed by completed runs (lists run as 0x401 + N):

1. **Valentin** (chr 1, list 0x412) only starts talking when he *sees* Bond (`IFISeeBond`: his cone), so meet him from
   the front. Objective 0 completes.
2. **Janus** spawns (ai_36, at pad 54) when objective 0 is done and Bond is near pad 59. He checks after 2 s that Bond
   holds `ITEM_FIST`. Stage flag 0x2000 ("Janus walks off", objective 1 fails) comes from a shot near him or his
   guards, Bond more than 1400 from pad 59, or a gun out once 0x4000 is set. ai_19 sets 0x400 after 10 s of talk
   unless Bond shoots.
3. **Natalya** (ai_21) spawns at pad 6 with a 180 s countdown, which drops to 15 s once Bond is within 500 of the
   aircraft at pad 24; the blast at its end kills her if she is still there.
4. Her captors and the exit-scene chr (lists 0x419, 0x41c, 0x41d) shot or killed set 0x04000000, and ai_39 then kills
   her, even after objective 3 completed. Do not shoot them.
5. **The exit**: pad 22's room (room 10). ai_42 fades out and ends the level the moment Bond is there (this was the
   bot's "mission ends at 10 s" in round 1), and the ending is good only while chr 10 lives.

### Bunker 2: keys and doors (stock)

| Key flag | Opens | Holder |
| --- | --- | --- |
| 0x1 | cell door idx 75 (Bond's start cell), exit door idx 72 | floor pickup in room 39 (the cell corridor) |
| 0x2 | cell door idx 77 (Natalya) | chr 18, spawn pad 100 |
| 0x4 | doors 64/65, 66/67, the triple blast doors 80-85 | chr 34, pad 105 (west wing) |
| 0x8 | doors 58-61, 68-71 (the east wing's antechambers and vault doors) | chr 26, pad 81 |
| 0x10 + 0x20 | the safe door idx 102 (needs both bits) | chr 11 (pad 10) and chr 6 (pad 5) |

Six cameras (idx 87-97), no alarm boxes, 37 monitors (25 in room 6, the big hall), a safe with three linked items, two
prison-cell doors, three autoguns in the west wing. The room graph and how the fork's heist maps tiers onto it:
[`designs/bank-heist-bunker2.md`](../designs/bank-heist-bunker2.md) and [`designs/bank-heist.md`](../designs/bank-heist.md)
section 9.

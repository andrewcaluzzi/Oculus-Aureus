# Player bot: working notes

Lab-style notes for whoever picks the player bot up next. User guide: `modding/bot.md`.

## Files

| File | What |
|---|---|
| `port/src/port_playerbot.c` | the bot: observe (result line, exit), drive (plan step -> fight / navigate / interact -> pad + look), doors, keys, loot |
| `port/src/port_playerbotlogic.c` | pure: angles, the plan parser, the stuck detector |
| `port/src/port_playerbotgraph.c` | pure: the graph, heap A*, components, the funnel (string pulling) |
| `port/include/port_playerbot*.h` | their headers |
| `port/src/inputrec.c` | the hooks: the bot writes pad 0, the look, the digital move and the port actions just before they are recorded |
| `port/luavm/port_luavm_bot.c` | the Lua `bot` library |
| `src/game/file.c` (`portMissionEndLog`), `port/src/port_mode.c` | the mission-end hooks |
| `port/src/libultra_shim.c` | `GE_CLOCKSCALE` |
| `tools/bot/plans/*.plan`, `tools/bot/heist_bot.lua`, `tools/bot/objectives.py` | plans, the heist driver, the plan-writing aid |
| `tests/bot/*.sh`, `tests/playerbot_test.c` | harness (level, campaign, dam1, dev loop, gdb hang dump, screenshots) and unit tests |

## Invariants

- **Input only.** The bot computes once per sim tick (keyed on inputrec's tick counter) inside the pad poll and writes
  `OSContPad` + the direct-mode look (+ the port action "next inventory" for gadgets); exactly those values are what
  `GE_INPUTREC` records. Every recorded run so far replays to the same outcome, mission time and objectives without the
  bot, completed or not, and inputrec's DRIFT check never fired (the level harness checks this on every run).
- **Engine queries leave no trace.** `stanTestLineUnobstructed` writes scratch globals (`stanSavedColl_*`,
  `D_800413BC..`); the bot saves and restores them around every call. Nothing else it calls writes state.
- **Off = untouched.** `GE_PLAYERBOT` unset: every hook returns at once. dam1 stays IDENTICAL (453 lines).
- **A run is repeatable** with `GE_FIXEDTICK=1` (otherwise a slow frame makes a 2-field tick and the sim depends on the
  machine) and a fixed `GE_SEED`. Seeds are the retry knob: the campaign uses seed = attempt.

## Gotchas found the hard way

- **`include/math.h` is an IRIX header with its prototypes under `#if 0`.** `fabsf`, `atan2f`, `fmodf`, `sqrtf` were
  implicit-int calls returning garbage. `port_playerbotlogic.h` declares them. (The heist branch later made implicit
  declarations an error in `port/`.)
- **The game's own `atan2f` (src/game/math_atan2f) returns [0, 2pi), not (-pi, pi].** Pitch came out as 358 degrees;
  `pblAtan2` normalises.
- **Bond's prop sits at eye height**, ~150 above the floor; chr props ~110 above theirs. Use the tile under Bond
  (`bondGroundY`) for "same floor" checks, never `prop->pos.y`.
- **Door `openPosition` runs 0..`maxFrac`**; `frac` is the door's size (a 620-unit gate, a -158 door).
- **`bossReturnTitleStage` is skipped for modes** (they own win/lose): modes need the `portModeEndMission` hook.
- **A only cycles guns** (`bondinvCycleForward` stops at `ITEM_BOMBCASE`). Gadgets come round with the port action
  `PORTACT_NEXT_INV` (index 8), which the bot presses through the actions hook, so it is recorded too. The watch menu is
  driven from draw code: do not use it (sim-only ticks and replays would differ).
- **Before Bond spawns** `vv_theta` is garbage (8e9): `pblWrap180`'s subtract loop never ended. The bot waits for a sane
  theta and a stan tile.
- **The guards' waypoint graph is in pieces** (Dam: 2 components). The stan tiles (1-3k per level) are what Bond walks
  on; linked tiles share their edge points, so there are no hidden step heights (`GE_PLAYERBOT_MAXSTEP` cut 0 links).
- **Paired gates (Dam)**: one opens only when its sibling is shut (`DOORSTATE_WAITING`); both are worked by wall
  consoles (`PROPDEF_SWITCH`: `g_LevelLoadPropSwitch`, first = switch prop, second = door prop). A switch's own tile
  can be on the far side of the wall, so the bot walks to a point 100 short of it, and keeps the shut door itself out of
  the route to its switch.
- **Padlocks** (`g_LevelLoadPropLockDoor`) can lie on the floor (Surface's vent hatch has four): aim at the lock's
  origin, at any range; do not back off as from explosive objects.
- **Gas tanks (Facility)** leave gas that drains health; routes near burst ones are made expensive. Not enough yet.
- **The ROM's AI macros store flags and pads byte-swapped**: `objective_bitfield_set_on(0x00100000)` is flag 0x1000,
  `if_bond_in_room_with_pad(0x4a01, ...)` is pad 330. `tools/bot/objectives.py` converts (its "difficulty" column shows
  256 for some entries: the low byte is the difficulty).
- **Keys** can sit on a guard (a child prop of the chr's prop): the bot waits near the door for him if he is behind it.
- **The docker container cannot ptrace-attach**, but gdb can launch the game: `tests/bot/gdbhang.sh` interrupts a run
  after N seconds and prints every thread's stack. That found the wrap loop.
- **Git Bash mangles `-v /mnt/c/...:/home/...`** for docker: run docker through PowerShell (`wsl -d Ubuntu -- docker`).
- **Do not rebuild a build dir while a campaign uses it**: `build_dev` (a second configure of the same tree) is the dev copy.

## Speed

`GE_CLOCKSCALE=20` + `GE_FIXEDTICK=1` + `GE_NOSLEEP=1` + sim-only ticks (render 1 in 60): Dam's 210 s mission plays in
about 12 s; record + replay ~25-35 s. A 900 s timeout costs ~75 s, so a full 3-retry campaign of mostly failing stages
takes over an hour.

## Per-level notes (Agent)

- **Dam**: completes (180-250 s). Fragile spot: the tunnel gates - from some approaches the bot cannot reach either
  console of gate 1; another seed gets through. The padlock on the way is shot off.
- **Facility**: objectives 0 and 2 done (key fetched from its carrier, Dr Doak met), then dies in the bottling room
  (guards on the catwalks + gas). Needs: shoot the tanks from outside the room; fight from cover.
- **Runway**: completes in ~70 s (key, plane), losing ~0.9 health.
- **Surface 1**: completes (~250 s): the console, then the vent hatch's four padlocks, then down.
- **Bunker 1**: key copied (key analyser via the next-inventory action); the main-screen photo is not taken - the bot
  cannot find a spot with the screen on camera.
- **Silo**: dies at ~390 s (after the photo) in fights.
- **Frigate**: the tracker bug is planted on the helicopter at 43 s (build after cbb9cb7); the hostage objective times
  out (captors the hunt cannot reach).
- **Surface 2, Bunker 2, Archives**: time out (no route to the plan's goal / no plan worth the name).
- **Statue**: the mission ends as failed at 10 s, every seed. Not investigated; the bot's first move is toward a door at
  -19514,1143,4991 while chasing the collect objective (tag 1), and something there ends the mission (a guess: an
  out-of-bounds / leaving-the-park trigger).
- **Streets / Depot / Cradle / Egypt / Jungle**: die in fights; Train / Control / Caverns / Aztec: time out. None has a
  plan yet (default `auto` + `exit`).

## Campaign, Agent, 2026-10-25

Commit 5447104 (campaign2 for Dam..Train with 3 attempts and a 900 s cap, on the build before the Frigate fixes;
campaign3 for Frigate and Streets..Egypt with 1 attempt and a 600 s cap, on 5447104). Every one of the ~50 runs replayed
to the same outcome without the bot.

| Stage | Result | Attempts | Mission time | Health lost | Objectives | Failure point |
|---|---|---|---|---|---|---|
| Dam | **pass** | 1 | 211 s | 0.06 | 1/1 | - |
| Facility | fail | 3 | 464 s | 1.30 | 3/5 | died in the bottling room (gas + catwalk guards) |
| Runway | **pass** | 1 | 72 s | 0.88 | 2/2 | - |
| Surface 1 | **pass** | 1 | 248 s | 0.00 | 2/2 | - |
| Bunker 1 | fail | 3 | 900 s | 0.06 | 1/2 | timeout: no camera spot on the main screen (room 17) |
| Silo | fail | 3 | 390 s | 1.10 | 1/2 | died (room 35) |
| Frigate | fail | 1 | 600 s | 0.00 | 1/2 | bug planted; hostage captors unreachable (room 23) |
| Surface 2 | fail | 3 | 900 s | 0.30 | 0/2 | stuck (room 32) |
| Bunker 2 | fail | 3 | 900 s | 0.00 | 0/2 | stuck before the cell objective (room 40) |
| Statue | fail | 3 | 10 s | 0.00 | 0/5 | mission ends at 10 s |
| Archives | fail | 3 | 106 s | 1.01 | 0/3 | died in the interrogation room (starts unarmed) |
| Streets | fail | 3 | 127 s | 1.01 | 0/2 | died (room 7) |
| Depot | fail | 3 | 103 s | 1.01 | 0/5 | died (room 7) |
| Train | fail | 3 | 900 s | 0.19 | 0/3 | timeout (room 26) |
| Jungle | fail | 1 | 263 s | 1.02 | 1/4 | died (room 39) |
| Control | fail | 1 | 600 s | 0.00 | 0/3 | stuck (room 83) |
| Caverns | fail | 1 | 600 s | 0.00 | 0/1 | timeout (room 62) |
| Cradle | fail | 1 | 79 s | 1.01 | 1/2 | died (room 17) |
| Aztec | fail | 1 | 600 s | 0.00 | 0/2 | timeout (room 2) |
| Egypt | fail | 1 | 45 s | 1.04 | 0/2 | died (room 19) |
- **Heist (Bunker 2, GE_MODE=heist_bot)**: card fetched, loud at 48 s, dies at ~3 minutes after 89 kills.

## Ideas, in order of payoff

1. Combat: use cover (back off round a corner when hurt), prefer the guard that is shooting, pick up armour first.
   Most failures are deaths.
2. Plans for the remaining levels (`tools/bot/objectives.py` makes this quick), and "avoid" areas (Statue's gate).
3. Heist: knock-outs (unarmed Z from behind, twice) and searching (B on the body) instead of shooting carriers; a quiet
   route; then the crack phases (written, never reached).
4. Ladders (stan tiles with the ladder flag) - not handled; none blocked a level tried so far.

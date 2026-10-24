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
| `tools/bot/plans/*.plan`, `tools/bot/heist_bot.lua`, `tools/bot/heist_bot_cells.lua`, `tools/bot/objectives.py` | plans, the heist drivers, the plan-writing aid |
| `tests/bot/*.sh`, `tests/playerbot_test.c` | harness (level, campaign with PAR, heist seeds, replay-only, dam1, dev loop, gdb hang dump, screenshots) and unit tests |

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
- **The photo objective is not render-free.** `objective_status.c` judges a picture from the object's render matrices
  (`objGetOnscreenRenderBounds`: `model->render_pos`), which only sim+draw ticks refresh. A record run that drew 1 tick in
  60 and a replay that drew none drifted (Bunker 1, Silo, both right after photos). `level.sh` now replays drawing the same
  ticks (`REPLAYSIM` overrides). The bot's own photo aim reads the same matrices (`photoAim`): read-only, guarded against
  a model with no `render_pos` (NaN aims before the guard).
- **Strafe direction**: GE's world is mirrored. Forward is (-sin t, cos t); C-right moves Bond along -(cos t, sin t)
  (measured: `strafeCalibrate` logs it once per stage). A heading `rel` degrees counter-clockwise of the view is
  forward(t) - rel * (cos t, sin t), i.e. to the right.
- **Chr origins sit ~110 above the floor** (a guard on a pad at y 65 has its prop at y 108); the head is +52 over it.
  A/B on the heist: +52 gave 2-3x the head hits of +15..38 per shot (`hits=head/body/limb` in the RESULT line comes from
  the game's own shot registers).
- **How a guard hits Bond** (chraction.c ~6780): per fire event `shotbondsum += 0.16 x cadence(3) x min(1, 300/d) x
  (1 + accuracy/10) x difficulty mods (x2 for semi-automatics)`; at 1.0 Bond takes a hit. Nothing about Bond's movement;
  none while Bond's damage flash shows. So range (and few guards with a line) is everything; strafing does nothing.
  Damage per hit falls as Bond's health falls (0.06 -> 0.03 at Agent).
- **A chr's AI "sees" Bond only inside its 220-degree cone** (chrCanSeeBond), at a random rate that falls with distance
  and with the angle off its facing. A contact that waits for that (Statue's Valentin) never starts if Bond walks up
  behind: `meet` now stands in front.
- **Heist staff vs stock civilians**: `portHeistCivilianState()` >= 0 only for a heist mode's civilians. Robbing at
  gunpoint is heist-only; a stock civilian key carrier (Facility scientist) is still shot (only SA+ counts it).

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
- **Bunker 1**: completes (round 2). The main-screen photo needed a tile with a clear line to the screen (it hangs on a
  wall; its own tile is behind it) and the screen's box centred; the way out is the room of pad 100 (ai_16). Its replay
  can miss the photo (see the gotcha on render matrices).
- **Silo**: dies at 120-390 s in fights (rooms 65-68).
- **Frigate**: the tracker bug is planted on the helicopter at 43 s (build after cbb9cb7); the hostage objective times
  out (captors the hunt cannot reach).
- **Surface 2, Archives**: time out / die (no plan worth the name; Archives starts unarmed: the bot now punches, 2 kills).
- **Bunker 2 (the stage)**: completes on some seeds (round 2). Bond starts unarmed in a locked cell; the magnet watch
  (`equip watchmagnetattract`, face the key, hold Z) pulled the floor key in on one seed, others got out when a guard
  opened up; punches down a guard for a gun; Natalya's cell (door tag 3) is opened with the key 0x2 from guard chr 18.
  One completed run's replay crashed in the audio mixer (alAdpcmPull, src/libultra/audio/load.c:68) - an engine bug.
- **Statue**: the 10 s end was the bot walking into the room of pad 22 (room 10): ai_42 fades out and exits the level
  the moment Bond is there. The plan avoids it. Valentin (chr 1, AI list 0x412) only starts talking when he sees Bond
  (line of sight inside his cone); met from the front, objective 0 completes. Then the mission fails at ~130-180 s on
  objective 1 (Janus) - not planned yet.
- **Streets / Depot / Cradle / Egypt / Jungle**: die in fights; Train / Control / Caverns / Aztec: time out. None has a
  plan yet (default `auto` + `exit`). The `exit` step now fights (it used to stand still and get shot).

## Campaign, Agent, round 2 (2026-10-25)

`PAR=10 RETRIES=3 MAXSECS=900 sh tests/bot/campaign.sh 0` on the build of the round-2 combat commit (before the last
commit's Statue/avoid fix: Statue now lives to the timeout with objective 0 done instead of failing at ~140 s).
6 passes (round 1: 3). Replays: 18 of 20 `same`; Bunker 1's replay misses the photo (render-dependent objective, see
gotchas); Bunker 2's replay crashed in the audio mixer near the end (engine bug, alAdpcmPull).

| Stage | Result | Attempts | Mission time | Health lost | Objectives | Failure point |
|---|---|---|---|---|---|---|
| Dam | **pass** | 1 | 157 s | 0.12 | 1/1 | - |
| Facility | fail | 3 | 468 s | 1.00 | 2/4 | died at the bottling tanks (room 68); other seeds stuck at the key door (carrier far) |
| Runway | **pass** | 1 | 60 s | 0.51 | 2/2 | - |
| Surface 1 | **pass** | 1 | 349 s | 0.00 | 2/2 | - |
| Bunker 1 | **pass** | 1 | 222 s | 0.61 | 2/2 | - (replay misses the photo) |
| Silo | fail | 3 | 143 s | 1.03 | 1/5 | died (room 67) |
| Frigate | fail | 3 | 900 s | 0.00 | 0/2 | hostage captors unreachable |
| Surface 2 | **pass** | 3 | 445 s | 0.11 | 2/2 | - |
| Bunker 2 | **pass** | 2 | 222 s | 1.06 | 2/2 | - (replay crashed in audio) |
| Statue | fail | 3 | 140 s | 0.06 | 1/5 | Janus / Natalya / recorder not planned |
| Archives | fail | 3 | 140 s | 1.01 | 0/4 | died (room 14), starts unarmed |
| Streets | fail | 3 | 900 s | 0.00 | 0/2 | no plan |
| Depot | fail | 3 | 136 s | 1.01 | 0/5 | died (room 7), no plan |
| Train | fail | 3 | 900 s | 0.00 | 0/3 | no plan |
| Jungle | fail | 3 | 140 s | 1.01 | 0/3 | died (room 26) |
| Control | fail | 3 | 900 s | 0.00 | 0/3 | stuck (room 83), no plan |
| Caverns | fail | 3 | 900 s | 0.00 | 0/1 | no plan |
| Cradle | fail | 3 | 113 s | 1.03 | 0/2 | died (room 17) |
| Aztec | fail | 3 | 900 s | 0.00 | 0/2 | no plan |
| Egypt | fail | 3 | 44 s | 1.02 | 0/2 | died (room 19) |

Heist (10 seeds each, replay `same` 20/20): cells job 0/10 (best: 61 kills, armour taken, died in the central corridor
at 178 s; alarm off for good in 3), vault job 0/10 (GREEN card 10/10 - the head teller robbed at gunpoint or the
captain taken down; BLUE never reached; knock-outs tried, every one spotted or timed out).

## Campaign, Agent, round 1 (2026-10-25)

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

## Bank Heist (round 2)

`GE_MODE=heist_bot` (the vault job) and `GE_MODE=heist_bot_cells` (the small job: armour, floor key, cell cash, the dock),
`tests/bot/heist.sh`. Heist mode itself unchanged (merged from `heist` at 071f202). No win yet.

What happens, every seed: Bond walks the north tunnel; the hall's guards (16 posted, 2 alarm raisers, the GREEN carrier,
8 staff) see him through the glass blast doors; the raiser posted just inside (pad 62) runs for the panel at pad 64, three
steps away, with a civilian standing on it. The hall fight costs 0.3-0.6 health and kills 1-3 staff in the guards'
crossfire. Then the central corridor (9 posted + a raiser whose panel is on the route) and the west wing (14 posted, 2
raisers, and - 120 s after the alarm - the armoured response squads arriving at the loading dock, which is the cells
job's exit). Bond has 1.0 health (heist guards are twice as accurate as stock: accuracy 10 at Agent); the west wing's
body armour (pad 48, +1.0) is the only other health on the route.

Measured (10 seeds each, Agent, cells job): the original fight-everything bot ~40 kills / life and the alarm at ~25 s in
9 of 10; with raisers first and shot past civilians behind them, the alarm stays off in about 3 of 10 and comes later
(50-150 s) in the rest; head aim tripled head hits; backing away from close guards raised survival from ~125 s to
~165 s; armour is reached in ~half the runs. The best runs die in the west wing after the armour (2.0 health spent,
45-60 kills) - just short of the floor key. Civilian deaths: 1-4 per run (the limit is 4; 5 fails the job in 1-2 runs
of 10), mostly guards' bullets aimed at Bond.

Tried and dropped (numbers in the commit history): holding the entrance tunnel (a shooting gallery: guards hit from
2000 down it), holding the blast-door passage and provoking (0/12; room 8 overlooks it), shooting raisers through the
closed glass (the line test fails before the doors open), `rush` (walk while shooting: no better), shooting only guards
that have noticed Bond (no better).

Replays: the heist's `math.random` was seeded from the clock in solo, so a recorded heist replayed with other guards in
other places (two replays of one recording disagreed). `port_luavm.c` now seeds it from `GE_SEED` when set (solo only;
co-op keeps the session seed, players without GE_SEED keep the clock). Heist runs replay `same` since.

Next, in order: (1) an ambush at each area's alarm panel (raisers always run to it; the bot could hold a spot that sees
the panel); (2) choose where to fight in the hall (a corner that sees one approach, no staff behind the guards); (3) the
vault job's middle (BLUE chief in the offices: knock-out from behind is written but never reached unaware; the
console; RED manager robbed at gunpoint). The cracker was started once (the first run after the merge reached the
antechamber at 99 s through RED doors that chasing guards had opened, started it, and died at 112 s); the jam restart,
the looting and the exits have still not run in a real game.

## Ideas, in order of payoff

1. Combat is still the blocker. What the hit model says (gotchas): fewer guards with a line on Bond at once, at longer
   range, killed before they have aimed long. Not built yet: choosing where to fight (a spot that sees one doorway, not a
   room), peeking (step out, shoot, step back), and pre-aiming a corner guards come round.
2. Plans for the remaining levels (`tools/bot/objectives.py` makes this quick; `avoid pad N` for rooms that end the
   mission). Statue next (Janus, Natalya, the recorder), then Streets/Depot.
3. Heist: see "Bank Heist" below.
4. Ladders (stan tiles with the ladder flag) - not handled; none blocked a level tried so far.

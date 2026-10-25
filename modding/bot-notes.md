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
| `tools/bot/routes/*.route`, `tests/bot/speedrun.sh`, `port/src/port_snap.c` (solo savestates) | round 6: speedrun routes, the optimiser driver, savestates |

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

## Round 7 (2026-10-25): savestate fidelity (relevant to netplay rollback), trials that reproduce, Bunker 2

Round 6's optimiser choices did not reproduce in a fresh process on Surface 2, Statue, Frigate and Bunker 2. The suspect
(audio) was one of six causes. Full write-up with the method and the netplay relevance: **`modding/snapshot-fidelity.md`**.

### The fidelity test

`GE_SNAPSOLOTEST=T,N,1` (`port_snap.c`): save at T, run N ticks hashing every tick's state in 4 KB chunks, restore, run
again (a replay, or the bot deciding again), compare per tick and byte for byte; it names the first chunk to differ.
Helpers `GE_SNAPTESTAT`, `GE_SNAPTESTOUT` (writable data outside the snapshot), `GE_TEXLOADLOG`. A difference that only
shows for long N comes from something pass 1 did LATER: bisect N to one pass-1 tick, diff the outside memory of N and
N+1 (Frigate: pass-1 tick 985, a door opening, read back by pass 2's tick 600). Scripts: `E:\midas-temp\bot\fid\`.

### What was outside the snapshot (all fixed; details in snapshot-fidelity.md)

1. **Audio** (netplay: yes). The sim read the sound player's playing state, held its `ALSoundState` pointers, and the
   engine cleared the sim's owner words - all on the wall-clock audio clock (no device in the container: `SDL_GetTicks`,
   so with `GE_CLOCKSCALE=20` a guard's line lasted 20x as many ticks as at real speed). Now the sim holds tokens whose
   life is computed from the sound's envelope in sim fields (`port/src/port_simsnd.c`, `GE_SIMSND=0` = old).
2. **The texture bank** (`assets/oddtextures.c`), rewritten in place on a texture's first use (netplay: yes).
3. **The VI settings and projection** the sim's camera model reads (`src/fr.c`, a presentation file).
4. **The portal table**: doors write its control bytes in place in the served bg file, in the ROM arena (netplay: yes).
5. **Polls per tick** in the harness (wall-clock pump count): one poll per tick under `GE_FIXEDTICK` + `GE_NOSLEEP`.
6. **The bot's scratch** (cover-search stamps, A* scores: live now) and its graph costs (an extent, always).

Result: a save, N ticks, a restore and the same N ticks are bit-exact on all nine stages (bot playing: 400-1899, and
600-3599 where the mission lasts; replays of Surface 2 / Frigate / Bunker 2: 600-6599). Netplay's re-simulation path:
Frigate 16 of 70 rollback compares differed before, 0 after.

### Trials that reproduce: the step's first poll

With the snapshot exact, Runway's trial and clean run were still 4 ticks apart. The optimiser asks for a step's
savestate in the poll where the step begins, and the save is taken at the next tick's start - after that poll's pad
sample. So every trial (and the kept variant's replay) began with one tick of variant 0's input, and the clean run with
the chosen variant's. Now the poll a step begins in plays variant 0 in every mode (`optVariant`). Found by hashing every
tick inside one optimiser process (`GE_SNAPHASH`) and comparing the first pass over a tick with the last: the only
difference before the step-4 save was the model texture pool's unused padding (a dyn extent is not zeroed beyond its
restored end, unlike the memp pools: harmless, noted). Cross-process hash compares do not work (heap addresses).

### Bunker 2 (speedrun mode): the cell, robustly

The magnet watch pulls the floor key in only when Z is RELEASED (holding fire resets `magnetattracttime` to 0) and
then from 60 to 250 fields after: anything within 350 whose floor line from Bond is clear. Round 6 held Z for 3 s and
walked off toward pad 99 (outside the cell, never reached); by the door the line runs into the wall. Standing at
-3215,4700 after a tap the key came in on every seed (1-6). After the cell the speedrun movement died on 2 of 6 seeds in
rooms 61 / 6; the normal bot's movement (`slow`): 5 of 5 complete, 127-212 s. `slow` steps are not optimised (their
variants are speedrun knobs). Route: `tools/bot/routes/bunker2.route` (the robust one). The speedrun movement after the
cell, optimised, where it survives: seed 3 110 s, seed 5 144 s (test route `E:\midas-temp\bot\fid\routes_b2a`).

### Wider search (`GE_PLAYERBOT_OPTWIDE=1`)

`via` waypoints also tried 160 off (14 variants). Dam: 104 trials instead of 76, 4826 ticks instead of 4830 (still
73 s). The other three stages (Runway, Bunker 1, Depot) have no `via` steps, so nothing to widen there; a two-step
lookahead was not built (6x6 rollouts per pair; the trial cost is a whole rollout to the exit).

### Results (Agent, round 7 binary, seed 1, Frigate seed 2; every time replay-verified in a fresh process without the bot)

Recordings and logs: `E:\midas-temp\bot\speedrun7\r7b\<stage>\` (Bunker 2's 110 s: `r7b2A3\bunker2\`; binary
`E:\midas-temp\bot\speedrun7b-bin`; videos `E:\midas-temp\render\videos-speedrun\`). "Trial =
clean": the optimiser's in-process final run and the fresh-process clean run end on the same tick with the same health.

| Stage | Old best (r6) | Round 7 | Agent WR | Idle | Trial = clean | Replay |
|---|---|---|---|---|---|---|
| Dam | 73 s | **73 s** (4830 ticks; wide search 4826) | 0:52 | 393 / 4830 (8%) | yes | same |
| Runway | 35 s | **35 s** | 0:21 | 254 / 2200 (12%) | yes (r6: 4 ticks off) | same |
| Surface 1 | 166 s | **166 s** | 1:01 | 2335 / 10074 (23%) | yes | same |
| Bunker 1 | 59 s | **59 s** | 0:16 | 656 / 3614 (18%) | yes | same |
| Frigate | 318 s (base; optimised: timeout) | **314 s** optimised | 0:21 | 7794 / 18956 (41%) | yes (r6: no) | same |
| Surface 2 | 224 s | **217 s** | 0:46 | 4196 / 13136 (32%) | yes (r6: 308 vs 224) | same |
| Bunker 2 | fail | **110 s** (seed 3, speedrun movement after the cell, optimised; the robust route: 127 s, seed 1) | 0:20 | 1790 / 6688 (27%) | yes | same |
| Statue | 220 s (base; optimised 231) | **199 s** optimised | 2:17 | 4526 / 12012 (38%) | yes (r6: 207 vs 231) | same |
| Depot | 47 s | **47 s** | 0:23 | 378 / 2900 (13%) | yes | same |

The optimiser now finds the same numbers round 6 did wherever round 6's trials happened to be exact (Dam, Surface 1,
Bunker 1, Depot); the gains are where they were not (Frigate, Surface 2, Statue) and Bunker 2, which now finishes. Still
open: one segfault in a trial (Bunker 1, `bgTestHitOnObj` after a restore, as in round 6) - the optimiser restarts past
it; the rest of the gap to the WRs is routes (see round 6, "What limits further gains").

## Round 6 (2026-10-25): speedrun mode, solo savestates, the segment optimiser; the Frigate replay divergence

AC after the videos: "speed runners never stop moving unless they have to". This round: an opt-in speedrun mode, routes,
a savestate optimiser, and the round-5 replay divergence chased down. Normal mode is unchanged (speed level 2 re-run on
the final build gives round 5's times to the second: Dam 107, Runway 72, Surface 1 752, Bunker 1 109, Surface 2 423,
Statue 229, Depot 90).

### The Frigate replay divergence: a wall-clock test in the port's input latches (engine finding, fixed)

Not a render->sim dependency. `joy_port.c` latches a one-shot port action (next inventory, reload, ...) at the poll and
drops it if no tick took it within **0.25 s of `osGetTime()`**. `osGetTime` runs `GE_CLOCKSCALE` times fast, so under the
harness that is 12.5 ms of wall time, and whether a tick came that soon depends on the machine's load. The bot cycles to a
gadget (Frigate's tracker bug) with exactly that action: a slow tick dropped the press in one run and not in another.
Evidence: four replays of the round-5 recording (`campaign.r5s2/frigate/a3.rec`) all die identically at 231 s (drift at
tick 6277) - the replay is deterministic; two record runs of the same seed on the same binary first differ in an `A`
(action) record at tick 5364 (`100` vs `0`) with Bond's position identical up to tick 6154. Also checked and ruled out:
lost pad samples (new `inputrec: LOST` diagnostic: none), the bot running mid-tick (new `g_PortTickPhase`: every poll lands
after the tick, phase 3), render state (record and replay draw the same ticks).
Fix (`joy_port.c`, `inputrec.c portInputRecTickClock`): with a recording, a replay or `GE_FIXEDTICK=1` the staleness is
15 sim ticks (the netplay lockstep rule); interactive play keeps the wall-clock rule. After it six parallel record runs of
that seed make the same recording (bar duplicate polls) and all replay `same`. Upstream-worthy: any recorded or
deterministic run that uses port actions had this. dam1 IDENTICAL.

Also found: the normal bot dropped a dead target at once only at `GE_PLAYERBOT_LOG=2` (combatTick) - the log level changed
its play. Left as it is for the normal bot (its results stand); speedrun mode always drops it.

### Speedrun mode (`GE_PLAYERBOT=speedrun`)

Speed level 2 (speed-strafe, fire on the move, no loot detours) plus:
- **No turning on the spot**: `pblDigitalPick` picks, of the eight digital moves, the one that carries Bond along the route
  from the view as it is after this tick's turn (forward 0, diagonals +-38 measured, strafe 90, back 180, back+strafe 142;
  the fast diagonals win ties); the view turns 45 deg a tick toward the speed-strafe offset meanwhile. Strafe-run measured
  12.6 u/tick on Dam's road.
- **Doors on the move**: from 450 out the view goes onto the door while the digital move follows the route; B as soon as
  the use test can take it (within 170 and 15 deg, or 110 and 30); Bond runs on while it swings and holds only if a leaf
  still in its swing is within reach; after 2 failed presses or a stall, the careful round-5 way. Doors the setup links to a
  switch go to the switch at once (Dam's tunnel gate); a paired gate waiting for its sibling: walk up and hold. A nearer door
  that stalls mid-swing with Bond in it: step back (Frigate deadlocked at 63/90 for good). A route door left 700+ behind
  is forgotten (it blocked the door finder: Surface 1 sat under its hatch).
- **Progress = running along the steer**: Dam's road and wall tiles take more than 1.5 s to cross, so the tile count
  called a full-speed run "stuck" and sent it on 45-105 deg detours (Dam 108 -> 83 s from this alone). On the route only:
  at its end the distance to the goal still counts (Frigate's helicopter: the gadget step must act from where it is).
- **Ladders**: normal steering on and next to ladder tiles (the 38 deg offset never climbs), and a padlocked hatch near a
  ladder has its locks shot first (the door finder only looks 300 up/down: Surface 1 climbed into the shut hatch for 2 min).
- Quicker presses for `use` steps and switches (turn 45/tick, press when facing, every 8-12 ticks).
- Diagnostics: LOG=2 logs every stop of 15+ ticks (where, why, stuck level) and stuck events with position and goal.
  Idle counts now cover play ticks only (the intro camera was counted as standing still).

### Routes (`tools/bot/routes/<stage>.route`)

The plan language plus `via pad N [r]` / `via X Y Z [r]` (pass through at full speed) and the step prefixes `side=l|r`,
`fast` (speed-strafe through threats) and `slow` (the normal bot's movement for that step). Community route data: the
Elite wiki pages (wiki.the-elite.net, e.g. Dam_Agent) only return a bot-check page to WebFetch - **not fetched**; TASVideos
1721S was fetched (an 00 Agent TAS; level notes, no usable paths). So the routes are the plans' steps (setup-derived:
`tools/bot/objectives.py`) plus, on Dam, waypoints at the bends of the bot's own completed runs; the order of objectives is
the plans'. Bunker 2's cell steps are `slow` (the guard's visit wants Bond within 25 of pad 99 - ai_4).

### Savestates (`port_snap.c portSnapSoloRequest`) and the segment optimiser

The rollback snapshots save and restore a full solo state: `portSnapSoloRequest(1|2)` saves / restores at the next tick
begin (where the ring saves), plus `port_simonly.c`'s tick count (which ticks draw). The bot's own statics are in the
snapshot (port_playerbot.c is unclassified -> snapshotted), its graph costs are added (`portSnapAddRange`), the optimiser's
state is `portSnapLive`. A save is ~5-6 MB; a restore copies back only the differing pages (300-700 KB). Cheap: Dam's 76
trials (each a rollout of the rest of the mission) ran in one process in ~4 minutes.

Optimiser (`GE_PLAYERBOT_OPT=1`, `tests/bot/speedrun.sh <stage> [seed]`): for each movement step of the route, every
variant (speed-strafe side left/right, `fast` toggled, and for `via` steps the waypoint moved 80 along x or z) is played
from the step's savestate **to the start of the exit sequence** (the end camera / bungee: the mission time is set) and
scored by that tick + 1000 per 1.0 health lost; the earliest is kept and replayed to the step's end, then the next step.
(Scoring each step at its own end was tried first: greedy, and on Dam the result was 2 s slower than doing nothing.) A trial
whose mission ends without an exit sequence (Bunker 1) or whose process dies (a guard's hit test crashed after a restore on
Bunker 1, `bgTestHitOnObj`) exits / is restarted from `opt.state` (`start`/`try`/`fix` lines; the prefix is replayed by
playing the kept variants). Then the clean run: the chosen variants without savestates, recorded and replayed without the
bot.

**Fidelity**: the in-process trial and the clean run agree exactly on Dam, Depot, Bunker 1 and Surface 1; Runway is 4 ticks
off; on Surface 2 (308 vs 224 s), Statue (207 vs 231 s), Frigate (exit reached vs timeout) and Bunker 2 (complete vs dead)
they part. So something the sim reads is not in the snapshot. Leading suspect (not verified): **audio state** - audio is
outside the snapshot, yet the sim asks it whether a sound still plays (`chrai.c:239` the AI sfx slots,
`chr.c:1847` / `chraction.c:6033` chr speech, `bondview2.c` hands / vehicles); after a restore the voices of the abandoned
trial are still playing. The clean run is the only number reported, so the optimiser is a search heuristic there; on
those stages the faster of the base (variant 0) and optimised clean runs is kept. (Round 7 found six causes - audio was
one - and fixed them: `modding/snapshot-fidelity.md`.)

### Results (Agent, final build, every time replay-verified without the bot)

Recordings, logs, `opt.state` and `RESULT.txt` per stage: `E:\midas-temp\bot\speedrun\<stage>\final\` (binary:
`E:\midas-temp\bot\speedrun\bin14`). World records: wrs.the-elite.net rankings (Agent), fetched 2026-10-25.

| Stage | Old best (r2-r5) | Speedrun | | Agent WR | Idle before (speed 2) | Idle after | Replay |
|---|---|---|---|---|---|---|---|
| Dam | 107 s (r5 speed 2) | **73 s** | optimised (base 86) | 0:52 | 1083 / 6839 (16%) | 393 / 4830 (8%) | same |
| Runway | 61 s (r4) | **35 s** | optimised (base 38) | 0:21 | 848 / 4426 (19%) | 254 / 2200 (12%) | same |
| Surface 1 | 204 s (r4 speed) | **166 s** | optimised (base 295) | 1:01 | 11328 / 45264 (25%) | 2335 / 10074 (23%) | same |
| Bunker 1 | 96 s (r5) | **59 s** | optimised (base 94) | 0:16 | 1377 / 6637 (21%) | 656 / 3614 (18%) | same |
| Frigate | 356 s (r5; 206 s unverified) | **318 s** | base, seed 2 (optimised: timeout) | 0:21 | 10158 / 22503 (died) | 7167 / 19170 (37%) | same |
| Surface 2 | 423 s (r5 speed 2) | **224 s** | optimised (base 315) | 0:46 | 9665 / 25504 (38%) | 4244 / 13528 (31%) | same |
| Bunker 2 | 222 s (r2) | fail | seed 1: never out of the cell; seed 3: dies on the way to pad 51 | 0:20 | 19821 / 54001 (timeout) | - | - |
| Statue | 229 s (r5 speed 2) | **220 s** | base (optimised 231) | 2:17 | 4679 / 13824 (34%) | 5609 / 13314 (42%) | same |
| Depot | 78 s (r5) | **47 s** | optimised (base 55) | 0:23 | 910 / 5500 (17%) | 381 / 2900 (13%) | same |

8 of 9 faster than before (Bunker 2 fails in speedrun mode; the normal bot still passes it on seed 3). Idle fell most
where movement was the waste (Dam 16% -> 8%, Depot, Runway); Statue's idle is the scripted contacts (3.9k ticks of
stand-still steps) and its fights; Surface 1 and 2 still lose minutes to stuck episodes in rooms 14 / 32.

### What limits further gains

- **Routes**: no community route data reached the bot (the wiki is behind a bot check); WRs use orders and skips the
  plans do not know (Bunker 1 16 s, Frigate 21 s, Bunker 2 20 s). The routes are A* on the stan tiles between the plans'
  objectives. Hand-written waypoints and orders per stage (from someone who knows the runs) are the next big lever.
- **Fixed costs**: scripted contacts (Statue), paired gates (Dam's tunnel: ~8 s waiting for one gate to shut and the other
  to open), doors' swing time.
- **Savestate fidelity** (above): 4 of 8 stages cannot use the optimiser's choices as found. Putting the audio engine's
  voice state into the snapshot (or answering the sim's "is it playing" from sim-side bookkeeping) is the fix to try.
- **Search breadth**: 6-10 variants a step, one step at a time. A wider variant set (corner offsets on every step, door
  press timing, weapon choice) is cheap to add now that a trial is a rollout from a savestate.
- **Chaos**: small behaviour changes swing a run by 10% (Dam's base went 79 / 83 / 86 s across builds) - fights decide it.
- **Bunker 2** in speedrun mode: the cell exit depends on the guard's visit timing.

## Round 5 (2026-10-25): doors by doorway, stacked doors, captor hunt; Frigate and Depot complete

**The "nav mesh gaps" were not gaps.** Caverns' stan mesh is one component (1436 tiles); Aztec's has 31 but Bond starts
in the 1469-tile main one. Round 4's "no route" on both came from having no plan (`auto` + `exit`): with a plan
(`goto pad 369`, the exit room's pad) Caverns walks to room 50 in 200-400 s, Aztec to its fights. Frigate's captors are on
the mesh too: the hunt picked its quarry by straight-line distance (y x 3), so as Bond moved it flip-flopped between
decks (chr 28 / 20 / 22 / 1 every few seconds) and never got anywhere; and it called a guard "reached" within 250 -
Bond stood on the deck above one for 400 s. What did block routes, all door logic:

- **Depot: a locked door in a side wall.** The key-8 door (-384,-80,510, rooms 37|35; no key with flag 8 exists in the
  level - the setup's only key has flags 2) stands beside the only way to the train. Locked doors penalised every tile
  within 90 of them, and `routeDoor` took any shut door near the route, so the corridor itself looked locked: the bot
  went for a key that does not exist, gave up, routed "round" and came back. `GE_PLAYERBOT_LOCKCLOSED=1` (closing those
  tiles) gives no route at all; `=2` (ignore keyed doors) walks straight past it. Fix: a locked door counts only on the
  stan links that go through its doorway - from one of the rooms the door prop is in (`PropRecord.rooms`, -1 ended) to
  another - and those links get the 20000 penalty (taken back before the next plan; unit test). For `routeDoor` an
  unlocked door also counts when the route passes right over it (within 60 + frac/2): Surface's vent hatch's room list
  does not name the rooms its route crosses (the mid-round campaign, link test only, timed out there), and the plain
  round-4 "near the route" test for unlocked doors lost Frigate on its side doors.
- **Caverns' stacked doors** (AC: the only map with them). `GE_PLAYERBOT_DOORS=1` dumps them: nine doorways are two
  unlinked doors with a one-tile room between (e.g. tag 27 at 3484,-2906,1461 rooms 13|14 and tag 18 at 3251 rooms
  14|16; outer maxFrac 42, inner 21: swinging). Two things in the engine make them a trap. `propFindForInteract` walks the
  on-screen props near to far and `doorTestForInteract` takes the first door within 200 whose edges straddle the view
  within +-20 deg (door7F0555F8) - the NEAREST door Bond faces, not the one he means. And `doorActivateWrapper` toggles:
  B on an opening, waiting or open door closes it. So aiming at the far door with the near one open in front closes the
  near one. Fix: `routeDoor` returns the first shut door along the route (then the nearest); while a nearer open door is
  within 200 in front (`doorInTheWay`), no B press (doorTick too) - follow the route through it first; a nearer shut one
  becomes the target.
- **Caverns' key doors stand open at the start and close later** (pos = max at load; state 2 = closing seen at 900 s).
  Bond's tile was already the far side's while the closing door held him, so the route had "passed" it: `routeDoor` now
  also looks two tiles back when the door is within 150.
- **Linked leaves**: a locked leaf whose linked partner needs no key is opened through the partner (`doorWorkable`).
  Written for Depot's train door (setup tags 6/7: the tag-7 leaf's setup record has key 8) - but at run time both leaves
  report key 0, so it has not been exercised yet.

**Frigate complete** (206 s, replay same). The setup (u/UsetupdestZ.c): on Agent ai_11 sets three of the five hostage
flags at the start, so two escapes are needed. A hostage (ai_1, 0x402) waits until his captor (ai_2, 0x403) is gone, then
runs ai_3: to a random pad, and when he is there with *no line of sight to Bond* he fades out and sets the next flag (with
a line he picks another pad). A captor shoots his hostage 3 s after he first has a line on Bond -
`if_guard_and_bond_within_line_of_sight`, which ignores facing - unless Bond has hit him by then. New `hunt ai 0x403`
(only that list, shot before anyone else), the hunt's quarry kept and picked by route length, "reached" only with a
line. The exit (ai_10): after leaving the rooms of pads 153/149, re-entering pad 153's room ends the mission.

**Depot complete** (82 s, replay same) with the side-door fix. **Bunker 2**: with round 5's door logic the route out of
the cell takes the key-1 door; without the key the guard's visit comes when Bond stands by pad 99 (ai_4), which round 4's
route happened to pass - the plan now says it. **Control**: Boris (chr 0) is armed and the bot shot him at 6.5 s;
ai_23 fails objective 0 the moment chr 0 is gone - Boris (and jungle Natalya) are allies now. Natalya then never
appeared in 200 s at pad 220: what starts her (ai_1 / ai_2) is not worked out. **Archives**: Natalya is met (133 s), then
died in Bond's own line of fire (allies now count as bystanders for holding fire), then fell behind - she trails 250-400
behind (her AI stops within 2.5 m, runs after Bond beyond 4 m) and the exit needs 0x400: new `escort ai 0x408 650`.
Bond is at ~0.2 health by the meeting, so the walk back mostly dies. **Train**: seed 1 sticks at x -2600 (room 9 -> 10,
no door there: an obstacle the tiles do not know); seeds 2-3 reach the last carriages (room 51) and die. **Cradle**: plan
`auto` (console) then go for chr 0 (Trevelyan). **Aztec**: plan written (the floppy is ITEM_EXPLOSIVEFLOPPY in this
enum: walk there armed, then equip + use tag 19); its guards (accuracy 0x32) kill Bond before the computer.

**Combat**: targets are weighted by the guard's `shotbondsum` (his aim at Bond, 0..1; at 1 Bond is hit): x(1 - 0.6 aim),
so the one about to land a hit goes first (`GE_PLAYERBOT_NOAIMSCORE=1` for A/B). Tried and dropped: counting attackers
out to 2400 (Caverns' hits come from 1400-2400) - Surface 2 got slower. Caverns' guards respawn (chr_try_spawning_at_pad
loops): its plan uses `rush 1`; it then lives the full 900 s once (room 46, near the end) but dies in room 50 on most
seeds. A close object the line test cannot see is shot anyway for 3 s (Train's brake units sit in the wall).

**Speed mode safety**: speed-strafe only while no armed hostile is within 1000 with a line (1800 if he has noticed Bond)
and Bond has not been hurt for 1 s; then 2 s of plain running looking ahead.

**Logs**: the rooms of each route (LOG=2), "civilians dead: N", the hunt's quarry with its route length, and the door dump.

### Campaign, Agent, round 5

Full campaigns (`PAR=6 RETRIES=3 MAXSECS=900 NOSHOTS=1`, normal and `GE_PLAYERBOT_SPEED=1`) on the build of round 5's
third commit ("bin6"); then the door rule was changed once more (unlocked doors: doorway link *or* the route
passing right over the door - bin6's plain "near the route" for unlocked doors cost Frigate every attempt in "will not
open" on its side doors) and the nine stages that pass were re-run on the final build ("bin7"): all nine pass in normal
mode. Replays: 96 of 97 attempts `same` in the two full campaigns; the exception, and one of the re-run's, are both
**Frigate speed runs** (drift 123 and 67 events; the replay dies at 231-289 s where the recording lived) - something in
Frigate is not reproducible under load (suspect: a B press resolved through `propFindForInteract`, which only sees
props with PROPFLAG_ONSCREEN, a flag only drawn ticks set). Not chased.

| Stage | Normal (final) | Att. | Time | Speed-strafe (bin6) | Speed level 2 (final) | Round 4 | Failure point (normal) / note |
|---|---|---|---|---|---|---|---|
| Dam | **pass** | 1 | 157 s | **pass** 108 s | **pass** 107 s | pass 157 s | |
| Facility | fail | 3 | 496 s | fail | - | fail | dies in the bottling hall after 9 of 10 tanks (gas: below) |
| Runway | **pass** | 1 | 95 s | **pass** 63 s | **pass** 72 s | pass 69 s | |
| Surface 1 | **pass** | 1 | 481 s | **pass** 244 s | **pass** 752 s | pass 349 s | (349 s on bin6) |
| Bunker 1 | **pass** | 3 | 96 s | **pass** 159 s | **pass** 109 s | pass 294 s | |
| Silo | fail | 3 | 179 s | fail | - | fail | rooms 62/67 fights |
| Frigate | **pass** | 2 | 356 s | fail | UNVERIFIED 545 s | fail | new; 206 s in a dev run |
| Surface 2 | **pass** | 1 | 502 s | **pass** 624 s | **pass** 423 s | pass 623 s | |
| Bunker 2 | **pass** | 3 | 534 s | fail | fail | pass 248 s | the cell: pad 99 (speed: never out) |
| Statue | **pass** | 1 | 258 s | **pass** 273 s | **pass** 229 s | pass 373 s | |
| Archives | fail | 3 | 62 s | fail | - | fail | dies punching in the interrogation wing (room 14) on seeds 1-3; a dev run met Natalya |
| Streets | fail | 3 | 318 s | fail | - | fail | one attempt reached the exit without objective 2 (FAILED_OBJECTIVES) |
| Depot | **pass** | 1 | 78 s | **pass** 181 s | **pass** 90 s | fail | new |
| Train | fail | 3 | 397 s | fail | - | fail | seed 1 stuck at x -2600; seeds 2-3 die in room 50/51 |
| Jungle | fail | 3 | 167 s | fail | - | fail | fights |
| Control | fail | 3 | 900 s | fail | - | fail | Boris spared now; Natalya never comes (start room) |
| Caverns | fail | 3 | 209 s | fail | - | fail | room 50 (respawning guards); one dev run lived 900 s to room 46 |
| Cradle | fail | 3 | 586 s | fail | - | fail | console done; Trevelyan chase dies |
| Aztec | fail | 3 | 900 s | fail | - | fail | fights / room 12 |
| Egypt | fail | 3 | 45 s | fail | - | fail | room 19 at 45 s, no plan |

**9 of 20 on Agent** (round 4: 7; Frigate and Depot are new). Speed-strafe with the safety finishes 8 of the 9
(Bunker 2 and Frigate not); speed level 2 (below) 7 + Frigate unverified. Speed is still **not** safe enough to be the
default: Bunker 2 never leaves the cell in speed mode on seeds 1-3 (a different first move changes when the guard comes).

**Facility's gas is level-wide** (propobj.c `handle_gas_damage`): the first burst tank starts one timer; 30 s later Bond
loses 0.125 health every 225 ticks *wherever he is*, for good. So from the first tank there are ~60 s (+ armour) to shoot
the other nine and reach pad 309. The bot arrives at ~0.6 health after ~415 s and dies 80 s after the first tank with 9
of 10 down. `auto` now takes the nearest tank (it crossed the hall in tag order); not enough. Next: arrive healthier, shoot
the tanks from one spot, no armour detour once the gas runs.

### For the speedrun round: where the bot stands still

New `ge: playerbot: idle: N of T ticks (fighting, at doors, stand-still steps, moving steps)` before every RESULT
(ticks Bond moved under 1 unit). Final build, seed of the passing attempt:

| Stage | Normal: idle / ticks | fighting | doors | stand-still steps | moving steps | Speed 2: idle / ticks | fighting | doors |
|---|---|---|---|---|---|---|---|---|
| Dam | 2233 / 9862 | 242 | 512 | 0 | 1479 | 1233 / 6839 | 4 | 410 |
| Runway | 2106 / 5770 | 833 | 71 | 304 | 898 | 1012 / 4426 | 27 | 13 |
| Surface 1 | 7558 / 28978 | 155 | 36 | 556 | 6811 | 11489 / 45264 | 0 | 35 |
| Bunker 1 | 2000 / 5850 | 928 | 372 | 70 | 630 | 1524 / 6637 | 399 | 165 |
| Frigate | 8476 / 21480 | 422 | 4617 | 766 | 2671 | 12389 / 32814 | 482 | 9149 |
| Surface 2 | 11593 / 30240 | 1183 | 1222 | 9 | 9179 | 9816 / 25504 | 1034 | 375 |
| Bunker 2 | 11540 / 32112 | 1862 | 1427 | 424 | 7827 | (fails) | | |
| Statue | 5291 / 15562 | 1044 | 53 | 4053 | 141 | 4834 / 13824 | 535 | 34 |
| Depot | 1055 / 4792 | 93 | 425 | 482 | 55 | 1099 / 5500 | 16 | 215 |

Readings: a fourth to a third of every run is standing still. "Moving steps" idle (Surface 1, Surface 2, Bunker 2: 7-19k
ticks) is stuck detection, detours and turning on the spot - the biggest single cost, and the place where a known route
pays most. Doors cost 0.5-1 s each (walk up, face, press, wait for the swing; Frigate 4.6-22k ticks: its pairs of side
doors and "will not open" retries). Statue's 4k stand-still ticks are the scripted contacts (Valentin, Janus - fixed
costs). Fighting idle drops to almost nothing with `rush` (Dam 242 -> 4, Runway 833 -> 27).
Failed attempts show the waste plainly: Bunker 1 a1/a2 spent 49k of 54k ticks "fighting" one target it never hit;
Frigate a1 50k ticks at one door.

`GE_PLAYERBOT_SPEED=2` (new) = speed-strafe + fire on the move (`rush`) + no detours for guns/ammo while the gun has
30+ rounds. Not done (left for the speedrun round): pressing B on the move (doorHandle walks to 120, turns to face, then
presses at 260 - a door that opens away could be pressed from range while walking), and blending step boundaries
(each step's `navReset` re-plans from scratch). Known routes per level plus the deterministic replay (every passing run
above is a recording that replays exactly; Frigate excepted) are the obvious base for route optimisation.

## Round 4 (2026-10-25): real strafing, speed-strafe, Statue complete, exposure-scored cover

**The C buttons never moved Bond.** In the port's pad path (bondview2.c ~7770, not an N64 pad) the strafe is the Move
stick's x (`analogStrafe = stick x`, `canTurnTank` forced on) and the C buttons' digital steps are cleared. Every combat
"strafe", civilian side-step and walk-while-aiming heading since round 1 pressed C-left/right and stood still (measured
< 1 u/tick). They now use stick x (+x moves Bond toward yaw theta + 90, i.e. against r1 = (cos t, sin t)). The periodic
combat dodge is opt-in (`GE_PLAYERBOT_DODGE=N` ticks per side). Bunker 2, seeds 1-12 (no replay): standing **4/12**
(health lost 1.22 a run), 45-tick dodge 4/12 (1.45), 120-tick dodge 1/12 (1.68) - standing stays the default.

**Movement, measured** (`GE_PLAYERBOT_SSTEST=1`: the first 25 s of a stage run each input along a line, view held; Dam's
start road, the "out" leg): stick forward 8.4 u/tick; stick forward + right 9.0 u/tick 40 deg off the view (the bot's
own |x|+|y| <= 130 gate cuts the diagonal to 65/65); **the port's digital move forward + right 11.0 u/tick, 38 deg off
the view (+31%)**. Source: bondview2.c scales the walk animation's forward root motion by speedforwards / maxspeed
(maxspeed = max(forward, sideways x 0.8, turn): a full strafe does not cut the forward) and bondhead.c adds the strafe on
top. The digital strafe ramps (speedstrafe +-delta a tick: a side flip costs ~2 ticks); the analog strafe is instant.
`GE_PLAYERBOT_SPEED=1` speed-strafes on the digital move (`portPlayerBotDigMove`, recorded like the pad) with the view
38 deg off the travel direction, the side kept through bends under 50 deg.

**How a guard's shot lands** (chraction.c ~6790): the per-shot sum (0.16 x cadence x min(1, 300/d) x ...) only grows while
Bond is inside the guard's aim cone, measured from its body facing + `aimsideback`: 14 deg under 200, 8.6 deg to 400,
4.3 deg to 800, 2.2 deg to 1600, 1.1 deg beyond (`chrlvGetAimLimitAngle`). `aimsideback` eases toward the aim at Bond
over `aimendcount` = 10 ticks (chr.c `chrUpdateAimProperties`), so a Bond crossing the guard's line lags the aim by
about (lateral speed x 10 ticks) / d rad - wider than the cone at every range for 8 u/tick. On paper lateral motion
should dodge; the Bunker 2 sweep did not show it (strafing also walks Bond out of cover and into other guards' lines).

**Stale Lua in round 3.** CMake copies `tools/mode_heist.lua` and `tools/bot/heist_bot*.lua` into `Data/Scripts/modes`
only as a POST_BUILD step of `ge`: an up-to-date build (no relink) leaves the old copies. Round 3's "leave the vault
early" and the vault-rooms fix never ran in its batches. Re-measured with them: tune 2 **9/10** (seed 4, shut in before,
now wins), tune 3 **1/10** (5 of the 9 losses: the 5th civilian killed in the guards' crossfire at 54-134 s, before the
vault). The stale-Lua batch of the same build had tune 2 7/10, tune 3 4/10 (10 seeds is noisy). Replays 40/40 `same`.

**Statue complete (5/5, replay same).** The whole chain, from the setup's AI (lists run as 0x401 + N):
Valentin (meet in front of him); Janus spawns within 800 of pad 59 and checks after 2 s that Bond holds ITEM_FIST -
flag 0x2000 ("Janus walks off", objective 1 fails) comes from a shot near him or his guards, Bond > 1400 from pad 59
(the old `meet` walked Bond there in 3 s), or a gun out once 0x4000 is set; Natalya (ai_21) spawns at pad 6 with a 180 s
countdown, which drops to 15 s once Bond is within 500 of the aircraft at pad 24 (next to her), and the blast at its end
kills her if she is still there; her captors and the exit-scene chr (lists 0x419, 0x41c, 0x41d) shot or killed set
0x04000000 and ai_39 kills her, even after objective 3 completed; the exit (pad 22's room) gives the good ending only
while chr 10 lives. New plan step `spare ai ID`.

**Other fixes**: the `exit` step tapped A every 30 ticks - A is "next gun" - through fights and ended on the fist (the
Streets/Depot/Archives deaths with 0-4 kills); it now taps only after 5 s with nobody in sight. `equip unarmed` clears a
wanted item; `peace 1` keeps the wanted item in hand (Janus: the fist). Stage-flag changes are logged (`flags +0x... at
t=`): plan writing reads which AI flag fired straight from a run. Cover picks the tile with the lowest exposure
(`pblExposure`: sum of min(1, 300/d) over guards that see it; 0.25 for none) when 2+ guards have a line, moving only for a
clear gain (`GE_PLAYERBOT_OLDCOVER=1`: the round-3 rule). Plans: Statue, Streets (pad 248), Depot (train door, pad 271),
Archives (pad 152, Natalya 0x409, pad 208).

### Campaign, Agent, round 4

`PAR=10 RETRIES=3 MAXSECS=900 NOSHOTS=1 sh tests/bot/campaign.sh 0` on commit 3 of round 4 (default profile: dodge off,
speed off). **7 passes** (round 2: 6; Statue is new). 59 of 60 attempts replayed `same`; Bunker 1's first completion
drifted at tick 6025 in its replay (the photo objective missed, then the game diverged); the re-run of the same seed made
the identical 294 s run and its replay matched - the photo test (PROPFLAG_ONSCREEN, zDepth, the screen projection: all
set by drawing) is still not reliably deterministic under load (10 games at once). campaign.sh now retries such a run
with the next seed and reports `UNVERIFIED` if no attempt verifies.

| Stage | Result | Att. | Mission time | Health lost | Obj. | Failure point / note | Round 2 |
|---|---|---|---|---|---|---|---|
| Dam | **pass** | 1 | 157 s | 0.12 | 1/1 | | pass 157 s |
| Facility | fail | 3 | 487 s | 1.46 | 1/4 | died at the bottling tanks (room 66/67); a2 stuck at the key door (900 s) | fail |
| Runway | **pass** | 1 | 69 s | 0.61 | 2/2 | | pass 60 s |
| Surface 1 | **pass** | 1 | 349 s | 0.00 | 2/2 | | pass 349 s |
| Bunker 1 | **pass** | 1 | 294 s | 1.57 | 2/2 | replay verified on the re-run (above) | pass 222 s |
| Silo | fail | 3 | 181 s | 1.00 | 1/5 | died in rooms 67/68 every attempt | fail |
| Frigate | fail | 3 | 900 s | 0.00 | 0/2 | bug planted (a1, a3); hostage hunt: no route (room 18) | fail |
| Surface 2 | **pass** | 1 | 623 s | 0.99 | 2/2 | | pass 445 s |
| Bunker 2 | **pass** | 3 | 248 s | 1.31 | 2/2 | | pass 222 s |
| Statue | **pass** | 1 | 373 s | 1.50 | 5/5 | new plan | fail |
| Archives | fail | 3 | 61 s | 1.01 | 0/4 | a1 escaped (obj 0) and lived 475 s, never met Natalya | fail |
| Streets | fail | 3 | 260 s | 2.02 | 1/3 | died near pad 248's approach (room 43); big hits (0.3-0.4) from 2000+ away | fail |
| Depot | fail | 3 | 628 s | 1.01 | 0/5 | the route to the train door crosses a key-8 door the bot cannot open | fail |
| Train | fail | 3 | 900 s | 0.00 | 0/3 | no plan (room 9) | fail |
| Jungle | fail | 3 | 156 s | 1.01 | 1/4 | died (room 33) | fail |
| Control | fail | 3 | 900 s | 0.00 | 0/3 | stuck (room 83), no plan | fail |
| Caverns | fail | 3 | 900 s | 0.00 | 0/1 | no route (room 62), no plan | fail |
| Cradle | fail | 3 | 133 s | 1.03 | 1/2 | died (room 17) | fail |
| Aztec | fail | 3 | 900 s | 0.00 | 0/2 | no route (room 2), no plan | fail |
| Egypt | fail | 3 | 44 s | 1.02 | 0/2 | died (room 19) | fail |

### Speedrun profile (`GE_PLAYERBOT_SPEED=1`), Agent, seeds 1-3, every run replayed `same` (42/42)

| Stage | Best time, normal | Best time, speed-strafe | Completed (normal / speed) |
|---|---|---|---|
| Dam | 157 s | **108 s** (-31%) | 3/3, 3/3 |
| Runway | **61 s** | 63 s | 3/3, 3/3 |
| Surface 1 | 305 s | **204 s** (-33%) | 3/3, 3/3 |
| Bunker 1 | 294 s | **106 s** | 1/3, 2/3 |
| Surface 2 | **623 s** | - | 1/3, 0/3 |
| Bunker 2 | **248 s** | - | 1/3, 0/3 |
| Statue | 311 s | **244 s** (-22%) | 2/3, 2/3 |

Speed-strafing wins the time on four of the five stages both finish, and loses Surface 2 and Bunker 2 (on Surface 2 all
three speed runs time out at 900 s, on Bunker 2 all three die): the 38-degree view offset turns Bond's look off the way
he goes, so guards ahead are seen late, and the digital diagonal is not the stick's (no slow approach). Not yet the
default; next: drop to the stick within ~600 of a known threat, and through doors and tight bends.

### What blocks the rest (round 4 view)

- **Combat** is still the first cause of failure (Facility, Silo, Jungle, Cradle, Egypt, Streets, Archives a3, Depot).
  The exposure-scored cover rarely triggers on these (most hits come from 1-2 guards with a line, or guards the tile line
  test calls hidden); Streets' 0.3-0.4 hits from 2000+ away are not rifle fire (explosions or rockets - not looked at).
- **Frigate**: the captors (ai_2) shoot their hostage 3 s after they notice Bond unless Bond hits them first; a freed
  hostage (ai_3) counts only when he reaches his pad *out of Bond's line of sight*; the last two hostages' captors are
  off the nav mesh from where the hunt stands. Needs captor-specific targeting and stepping out of the hostage's view.
- **Depot**: a door with key flags 8 on the route to the train door; find which key and where.
- **Train, Control, Caverns, Aztec**: no plans (escort / timed objectives; Caverns and Aztec start somewhere the nav
  mesh does not route out of).
- **Heist**: tune 3 loses on civilians killed in the crossfire before the vault (round 3's finding stands).

**Disk**: batch output goes to `E:\midas-temp\bot` (`BOTOUT=/scratch/bot` with `-v /mnt/e/midas-temp:/scratch`; `OUT=` for
campaign.sh); `NOSHOTS=1` now works for level.sh and campaign.sh too. Batches run a copy of the binary
(`BUILD=../../../scratch/bot/binX`, a snapshot of build_pc's ProjectMidas + Data) so build_pc can be rebuilt meanwhile.

## Round 3 (2026-10-25): the full heist loop, a difficulty curve, the audio crash

**Heist easy tunings.** `tools/bot/heist_bot.lua` sets `HEIST_SETTINGS` before `include("heist")` (the mode's defaults are
untouched) from `GE_HEIST_TUNE1..5`, and wraps `spawn.chr` to give every armed chr the heist spawns one accuracy rating
(stock 0; the mode's own is 10 at Agent = 2x stock). `GE_HEIST_CIVOK=1` lifts the civilian limit. `tests/bot/heist.sh`
takes `TUNE=n`, `CIVOK=1` and `NOSHOTS=1`.

| Tune | Posted / raisers / vault / crack squads / response | Accuracy | Vault-job wins (10 seeds, Agent) | Failures |
|---|---|---|---|---|
| 1 | 0 / 0 / 0 / 0 / off (carriers only), jams 50% | stock | **10/10** (twice: before and after the heist merge) | - |
| 2 | 20 / 3 / 2 / 1 / 300 s | stock | **8/10** | 1 civilians (5), 1 shut in the vault (mode bug below) |
| 3 | 30 / 4 / 3 / 1 / 240 s | stock | not measured (the disk filled mid-run) | |
| 4 | 45 / 6 / 4 / 2 / 180 s | stock | 0/10 | 6 civilians (5 killed), 4 died (one after looting 24 bars) |
| 5 | defaults (90 / 12 / 8 / 2 / 120 s) | stock | 0/10 | 10 died (89-311 s, 32-90 kills) - measured as "tune 4" before the renumbering |
| - | the defaults | 10 | 0/10 | 8 died (72-192 s), 1 civilians, 1 timeout (room 3, GREEN leg) |

Tune 2-5 and the defaults ran on the merged build (waves really spawn now); every run replayed `same` (70/70).
The loop, as seen at tune 1: GREEN (teller robbed / captain knocked out or shot) -> BLUE (chief) -> console (cameras off)
-> RED (manager robbed) -> crack at pad 85 with 0-4 jams, every one restarted -> vault open 60 s, 11-30 bars -> exit B ->
`WIN`. Scores $1.55M-$4.5M. No quiet win: the time lock always raises the alarm when the vault shuts, so a quiet bonus
needs the crew out within `vault_time` (60 s) of the opening - AC's call.

What stops the harder tunings: (1) **guards' crossfire kills the staff.** At 45 posted, hall guards open fire at Bond from
1100-1700 units across the hall (through the glass, Bond still in the blast-door passage, t = 15-20 s) and the first two
civilian deaths come from their bullets (`killed (by -1)`); 6 of 10 runs end on the 5th death, most by guards. (2)
Attrition: ~2.0 health (1.0 + the west-wing armour) buys 35-90 kills; the bot dies on the BLUE leg (offices / central
corridor) or at the crack. The alarm comes from raisers (no camera in 19 of 20 alarms at tune 4/5).

Bot fixes this round: `robTick` walked to "within 200" of a civilian carrier through a shut door and stood there until the
key detour gave up (2 of 4 timeouts at tune 1); it now walks right up when there is no line. Untested (WSL went down
before they ran): leave the vault 28 s before it shuts (`HEIST_BOT.leave_at`), and re-crack if shut in on the way out.

**Heist mode bug (fixed in `tools/mode_heist.lua`, needs merging back):** `in_vault` used circles that miss the west end
of room 56; a Bond there when the time lock fired was "sealed" in, not "trapped" - no re-crack, locked in for good (tune 2
seed 4: a 900 s timeout with 13 bars). It now also counts the vault rooms 55-58 and 47. Untested in a run.

Other heist findings (not changed): a use press anywhere in the 300-unit console zone with BLUE re-runs the camera switch
(5-9 "security console used" lines a run; the off timer restarts each time). The security wing's shutters (doors 78/79,
vertical, unkeyed, not `PROPFLAG_CANNOT_ACTIVATE`) never open for B, so the bot loses ~12 s at room 18 on the way to the
console each run before routing through the BLUE door.

**Engine: the `alAdpcmPull` crash (fixed, 53ff7c9, shared code - worth upstreaming).** Root cause: the synthesizer's
update pool runs dry under the fast-forward (many sim ticks of sound events per audio frame; the audio frames follow the
wall clock, so it is random - 3 crashes in ~9 replays of one recording, none under gdb); `alSynStartVoice(Params)` then
drop the START, and a later volume / pan / stop update makes GE's env mixer (`_pullSubFrame` tests only `outCount`)
pull a load filter that never got a wavetable: `table` NULL at load.c:68 (a crash-time dump of the filter: table 0,
first 1, sample 0). Fix (`#ifndef TARGET_N64`): `_pullSubFrame` returns unless the voice is playing (the stock libultra
test), and the steal path of `alSynAllocVoice` checks its `__allocParam`. 14 replays after: no crash; instrumented runs
showed dropped STARTs in 5 of 8 and no table-less pull. dam1 IDENTICAL.

**Photo objective vs render**: stock behaviour - `objective_status.c` judges the picture from `model->render_pos`, which
only drawn ticks refresh (on the N64 every tick draws, so it never shows there). Replays must draw the same ticks.

**Disk space, the hard way.** Each run writes a BMP every 30 s (~20 MB a heist run); a 100-run ladder plus old rounds
filled C: (0 bytes free), and the WSL distro's disk image then threw I/O errors (docker hung, WSL needed a restart).
Batch runs: `NOSHOTS=1` (heist.sh deletes the run's BMPs), convert what you keep to PNG (`tests/bot/shots2png.sh`), and
check free space before a ladder.

Statue: the plan now meets Janus (ai_36 spawns him at pad 54 when objective 0 is done and Bond is near pad 59; ai_19 sets
0x400 after 10 s of talk unless Bond shoots): `goto pad 59`, `peace 1`, `meet ai 0x413`. Not run yet. The campaign was not
re-run this round.

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
other places (two replays of one recording disagreed). Round 2 patched `port_luavm.c`; the heist branch since seeds it
from `GE_SEED` in `portLuaGameRandomSeed` (`port_lua.c`), and the bot's patch was dropped in the round-3 merge (heist
recordings from before the merge do not replay on it).

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

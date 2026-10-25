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

## Round 4 (2026-10-25): real strafing, speed-strafe, Statue complete, exposure-scored cover

**The C buttons never moved Bond.** In the port's pad path (bondview2.c ~7770, not an N64 pad) the strafe is the Move
stick's x (`analogStrafe = stick x`, `canTurnTank` forced on) and the C buttons' digital steps are cleared. Every combat
"strafe", civilian side-step and walk-while-aiming heading since round 1 pressed C-left/right and stood still (measured
< 1 u/tick). They now use stick x (+x moves Bond toward yaw theta + 90, i.e. against r1 = (cos t, sin t)). The periodic
combat dodge is opt-in (`GE_PLAYERBOT_DODGE=N` ticks per side): Bunker 2 seeds 1-6 won 1/6 with a 45-tick dodge against
3/6 standing (see the dodge sweep below).

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

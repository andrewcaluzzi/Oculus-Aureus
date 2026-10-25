# Determinism, co-op and netplay

The game is deterministic by design: Rare's attract demos are just recorded pad input, and the RNG and checksum guards
fade a desynced demo back to the title. Everything the fork built on top (co-op, lockstep netplay, rollback, recorded
bot runs, the heist's determinism test) depends on keeping it that way. Co-op, netplay and rollback are **fork-only and
experimental**; the demo oracle and the rules below apply to upstream too.

## What must be equal, and what may differ

- **Sim state**: the three RNG seeds (`g_randomSeed`, `g_tlbRandomSeed`, `g_chrObjRandomSeed`, `port/src/n64_random.c`)
  and their draw counts, every chr, prop, door, projectile, objective, flag, the clock, the players.
- **Presentation** may differ freely: audio, the window, the HUD layout, what is drawn.
- Two runs are only comparable with **the same inputs, `GE_SEED`, `GE_FIXEDTICK=1` and `GE_TICKHZ`**. Without
  `GE_FIXEDTICK` a slow frame is a two-field tick and the sim depends on the machine.

## The demo oracle (upstream behaviour, fork baselines)

The stock Dam attract demo must reproduce exactly:

```
GE_DEMO=dam1 GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 ./ProjectMidas | grep '^ge: demo tick' > dam1.txt
cmp dam1.txt tests/baseline/dam1.a.txt        # 453 lines; facility1.a.txt has 419
```

- The baselines are the `ge: demo tick` lines (positions, RNG, counters) of the unmodified build at upstream commit
  6be5920 (`tests/baseline/README.md`). Two runs were byte-identical. On Windows `tests/demo_diag.ps1` does the run.
- On Linux run it in the build container with a real Xvfb on `:99` (`DISPLAY=:99`, recipe in
  [tooling-and-testing.md](tooling-and-testing.md)); that reproduced the Windows baseline exactly.
- **`xvfb-run` gives false mismatches.** A commit note (b620f97) records that dam1 under `xvfb-run` differs from the
  baseline from tick 185, for HEAD and the change alike. Compare like with like, or use the `:99` recipe. (Why
  `xvfb-run` differs is not established; its default screen is small and 8-bit, and the demo's viewport rect decides
  which rooms are entered, which feeds the RNG: **(unverified)**.) Note `tests/arena_crowd/dam1.sh` still uses
  `xvfb-run`.
- Run it after every change to shared code. "dam1 IDENTICAL" is the line most fork commits end with.

## Tools for finding a divergence

| Tool | What it gives |
| --- | --- |
| `GE_HASHLOG=<file>` (fork, `port/src/port_coop.c`) | the state hash and its 12 field hashes every sim tick, solo or co-op, no netplay needed. `GE_HASHLOG_DUMP=<from>-<to>` adds a text dump of the world (players' floats bit-exact) |
| `GE_NET_RNGTRACE=1` | a ring of the last ~2000 ticks of RNG draws (tick, draw number, caller, value), written at the first DESYNC or at exit; `tests/net_rngdiff.sh` aligns two traces and names the first differing caller |
| `GE_COOP_STATEDUMP=<from>-<to>` | the world dump around a tick in a replay |
| `GE_NET_LOG=1` + `GE_NET_REPLAY=<log>` | record a netplay session (inputs + per-tick fingerprints) and replay it offline, either view; prints the first mismatching tick and field |
| `GE_INPUTREC` / `GE_INPUTPLAY` | pad 0 record/replay (the player bot's harness replays every run without the bot) |

The state hash (`portCoopStateHash`) has 12 sub-hashes so a mismatch names its area: `rng` (seeds **and draw counts**),
`players`, `chrs`, `chrai`, `objs`, `proj`, `doors`, `props`, `flags`, `onscreen` (render-derived state the sim reads),
`clock`, `settings`. To extend it, add a field there and its name in `sStateFieldNames`.

**Host and guest agreeing is not proof of determinism**: two machines can share a hidden input (their speed). Replaying
a live log offline is the independent check; it is how the blood-animation clock (below) was found.

## Every nondeterminism cause we found, and the fix

| # | Cause | Where | Fix |
| --- | --- | --- | --- |
| 1 | `GE_SKIPINTRO` tapped A on a **poll** count; polls per tick follow the wall clock, so first-person play, `stage_start` and every spawn began on a different tick each run | `port/src/input_pc.c` ~7917 | counts sim ticks (`g_PortSimTickNo`, ~8822) and stops once Bond is in the player's hands. `cctv.test_press` had the same bug (6 polls -> 4 sim ticks) |
| 2 | A solo mode's Lua `math.random` was **seeded from the clock** | `port/src/port_lua.c` `portLuaGameRandomSeed` ~318 | with `GE_SEED` set a solo run takes that seed (co-op uses the session seed). Without it solo play still gets a fresh seed |
| 3 | The CCTV feed's posed-chr draw took numbers from the **sim RNG** (a gun's muzzle flash, `model.c`) on the watching machine only | `port/src/port_cctvview.c` | both generators and their draw counts are put back |
| 4 | That draw **allocated from the frame's linear pool**, shifting every later allocation of the sim's own draw on one machine (Bond 1's autoaim parted in its last bits at tick 3469) | `src/game/dyn.c` | the feed draws from its own corner at the top of the pool and puts the pointer back |
| 5 | Heist waves never came in: every hall entrance pad had a guard posted on it, so reinforcement spawns failed (found by the determinism test: 0 waves) | `tools/heist_lib.lua` | try a busy pad a step aside first |
| 6 | **Window-dependent sim** (netplay playtest 2): the onscreen flags, hit models, autoaim and room boxes used each machine's window shape and FOV; a shot hit a guard on one machine and the wall behind it on the other (+4 RNG draws for the bullet impact) | `src/game/fr.c`, `bg.c` | the canonical sim view: a fixed 320x240, 16:9, vanilla-FOV camera per player for everything the sim reads; the window's view only for drawing |
| 7 | **Door vertices read from a recycled pool** (playtest 7, tick 6411): the door tick copied the clipped shape built at `doorInit` out of the per-frame vertex pool frames later, by when the pool held whatever that machine had drawn | `src/game/propobj.c` `doorInit` | keep the shape at once, in the frame it was built |
| 8 | **Monitor collision from a rewound display list** (soak seed 32): a shot at a multi-monitor walked `rwdata->gdl`, a branch into the frame's display list; with the other Bond's watch open that list had been overwritten on one machine | `propobj.c` `propobjFindHit` | in co-op, shots test the monitor screens' static collision data |
| 9 | **Wall-clock blood animation**: the dead Bond's blood sequence (and his death and gun states) ran at the machine's speed | `blood_animation.c` `portBloodFrameDue` | every second sim tick under lockstep |
| 10 | **Live inputs in the sim**: Escape opened the watch on one machine only (a Bond with an open watch takes no damage: "invulnerable on one screen, dead on the other"), Return-as-START, the console, the Classic Mode toggle, the watch's mouse layer, key rebinding | `bondview2.c`, `options.c`, `joy_port.c` | carried in the input record (`PORT_NETF_ESC`, `PORT_NETF_RETSTART`) or blocked under lockstep |
| 11 | `chrTick` ran **once per player pass**: every guard ticked twice a frame in co-op | `src/game/chr.c` | only the sim pass (the first in the RNG-shuffled order) advances a chr; the other pass runs with zero clocks |
| 12 | Rewinding the non-local pass's vertex/matrix memory changed what the sim read (a guard's shot direction by 1-2 deg between views) | `src/game/lv.c` | the display list is rewound, the vertex memory is not |
| 13 | `objTick` portal/window toggles ran only in the local pass; they feed `PROPFLAG_ONSCREEN` | `propobj.c` | run in both passes |
| 14 | A gun dropped by a dying guard floated in co-op: the US projectile owner rule gave physics to "the pass of the player who fired it", and no Bond's pass is a guard's | `propobj.c` `objTick` | the EU/JP rule (owner must be a real player) |
| 15 | Photo objective vs render: it reads matrices only drawn ticks refresh, so a replay that draws different ticks differs | `objective_status.c` | replays draw the same ticks ([objectives-and-setup.md](objectives-and-setup.md)) |

Upstream already had one of these classes fixed for high frame rates: guard aim accumulates per fire event with a
fixed cadence (`GUARD_AIM_CADENCE`), not per sub-field delta ([guards-and-ai.md](guards-and-ai.md)).

## Patterns to remember

- A sim read of memory the renderer or a per-frame pool owns (`model->render_pos`, `dynAllocate*` buffers, `rwdata->gdl`,
  room geometry load state, the live room-walk arrays) is machine-dependent even when every input is equal.
- Presentation code must not draw from the sim RNG or allocate from the sim's pools.
- Anything keyed on polls, frames or wall time instead of sim ticks breaks replays.
- Anything that reads the local player, the window or a device directly in sim code breaks netplay.
- Lua: `math.random` is session-seeded; iterate arrays by index (`pairs` order on string keys is fixed, on table keys
  it is not); never read the local player.

## Co-op (fork)

`GE_COOP=1` puts two Bonds in a solo stage (`getPlayerCount()` stays 1; `portAllPlayerCount()` is 2). Rules that keep it
deterministic: guards target their nearest Bond (index order, ties to the lower), any-Bond room and distance tests for
level AI, keys pooled (or per player in the heist), friendly fire 0.5, death drops placed by ring order with no RNG,
respawn spot chosen with no RNG. Test: `tests/coop_determinism.sh` (view 0 twice and view 1 must give identical
per-tick fingerprints).

## Lockstep netplay (fork)

- One 20-byte record per player per tick: buttons, stick, port action bits, digital move, look in milli-degrees. Tick
  *t* runs when the partner's record *t* has arrived. Input delay default 3 (minimum 2, maximum 8). ENet transport.
- **While lockstep is on, the sim reads exactly two things about a player: that player's record for the tick, and
  deterministic game state.** The leak test (`tests/net_lockstep.sh` phase 7) throws real X events at the guest's window
  for a whole session; nothing may change.
- Every 30th tick each side sends `portCoopStateHash` on the reliable channel; a mismatch logs
  `DESYNC at tick N ... differing fields` and writes `desync_<N>.txt` with both sides' field hashes and a world dump.
  `GE_NET_TEST_DESYNC=<tick>` flips one RNG bit to prove the check fires.
- The sim-settings that affect play (ini keys no record carries) are compared in the handshake.
- Soak harness: `tests/net_soak.sh` + `net_soak_gen.py` (seeded random input, many seeds, stages and fake lag). The first
  matrix: 337,659 ticks per side, 97 PASS, 24 END, 5 CRASH and 2 DESYNC, all fixed since (rows 8 above and a Flatland
  mission-end crash).

## Rollback snapshots (fork, R1/R2)

- A snapshot is the game's `.data`/`.bss` plus the used part of the memp pools (~4.8 MB on co-op Dam). Save about 0.5
  ms, restore 0.7-0.9 ms (page compare + copy of the 80-170 KB that differ), re-simulate 0.24-0.5 ms a tick: a depth-8
  rollback costs about 3-4 ms in one frame.
- The sim-statics table is embedded in the exe after the link (`scripts/port/snap_embed.py` reads the linker map). Files
  are classified SIM (every `src/` file by default, so a new game global is covered), PRESENT (audio, scheduler,
  renderer, input devices, network) or LIVE ranges inside SIM files. `tests/snap_table_check.sh` fails on an
  unclassified `port/` file.
- **A sim-only tick** (`GE_SIMONLY_TEST=<start>,<n>[,<every>]`) runs the sim step with no render step. It still runs the
  world build inside each pass, because the next tick's shots read the matrices it builds; only where the display list
  goes and the render tail differ. It waits for no gfx task in flight.
- Leaks fixed for rollback: converted model display lists moved from the never-freed high arena to a sim heap
  (`portAllocSimHigh`) whose offset is sim state; the Lua VM lives in a 20 MB fixed block with a deterministic
  allocator.
- Not solved yet: audio handles held in sim state; re-simulated ticks re-trigger sounds; snapshots hold raw pointers, so
  only the same exe on the same layout can share them.

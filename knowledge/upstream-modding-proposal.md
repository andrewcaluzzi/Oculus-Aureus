# Proposal to upstream: modding support, built from the fork's work

**Status:** a draft for AC's first conversation with the Project Midas team. Nothing has been sent yet.
**Measured on:** fork `heist` at 1e881be, `arena` at 06ff16a, upstream `origin/main` at 9062f9f (Project Midas 1.0.2b). The
audio fix is on the `bot` branch (53ff7c9). Line numbers are `~N` on `heist` at that commit; they drift, so grep the
function name.

This page records what the fork could offer upstream, how big it is, and in what order it could be offered. It is a
conversation starter. Upstream decides its own mod model, and nothing here assumes they will take any of it.

## 1. Context

The fork (Oculus-Aureus) has built on top of Project Midas:

- a Lua 5.5 scripting layer and a small C event bus that scripts and C modes both use;
- runtime chr spawning (guards, companions, crowds) on stock stages;
- experimental local co-op, lockstep netplay over ENet, and rollback snapshots;
- two game modes: the Colosseum arena and the Bank Heist (stealth, cameras, knock-outs, civilians, loot), on stock
  stages and on generated levels;
- a player bot (the `bot` branch, not merged into `heist`).

AC wants to ask upstream whether there is appetite for modding support, and to share details and code. Upstream has no
mod system yet, and we do not know whether they will want Lua mods, C "engine mods", or both. So the proposal is split
into layers that each stand on their own:

1. **Tier 1:** engine improvements that are useful and testable with no mod system at all.
2. **Tier 2:** hook points and events that a C engine mod and a script layer would both need.
3. **Tier 3:** an optional scripting layer on top of tier 2.

The precedent is [murkantor/Project-Midas#83](https://github.com/murkantor/Project-Midas/pull/83) (`roomGetProps`,
[performance.md](performance.md)): one function in `src/game/chrprop.c`, 22 lines inside `#ifndef TARGET_N64`, the same
list in the same order, dam1 identical, useful on every stage. Every PR proposed here should have that shape.

### What upstream has already said

Upstream's `CONTRIBUTING.md` (on `origin/main`) lists work in progress:

| Area | Upstream status |
| --- | --- |
| Co-op | In development |
| Netplay | In development |
| Mod loading (xdelta patches) | In development |
| Lua modding | Planned |

It also says that once Lua modding is released, small gameplay changes should be Lua mods rather than PRs; that a PR is
one focused change, builds on Linux and Windows in CI, is tested in game, keeps the decompiled names, and keeps
comments to at most two plain sentences; that no ROM data or extracted assets are committed; and that AI tools are fine
for code (disclosed in the PR) but AI-generated images, models, sound or music are never accepted.

What that means for this proposal:

- **Co-op and netplay are upstream's own work.** The fork's co-op and netplay code is not offered as code. Its findings
  (the nondeterminism table in [determinism-and-netplay.md](determinism-and-netplay.md)) can be offered as notes if they
  want them.
- **Lua is planned upstream.** The fork's Lua layer is offered as one possible consumer of tier 2, not as a replacement
  for whatever they design. Tiers 1 and 2 are useful whatever they choose.
- **Fork comments are long.** Most `PORT:` blocks run to several lines. Every extracted PR needs its comments cut to the
  two-sentence rule.
- "Mod loading (xdelta patches)" sounds like data patching, which is a different thing from code hooks. We have not
  seen its code (it is not on `main`), so whether the two overlap is **(unverified)**.

## 2. How big the heist is, by layer

Measured with `git -C <fork> diff --numstat arena...heist` (what the heist adds on top of the arena branch) and
`origin/main...heist` (everything the fork adds over upstream). The numbers are lines added; deletions are small and
shown where they exist. Line counts measure size, not quality.

### Heist only (on top of `arena`)

| Layer | Files | Lines added | Earlier estimate |
| --- | --- | --- | --- |
| Lua rules: `tools/mode_heist.lua` 1,009, `tools/heist_lib.lua` 75 | 2 | 1,084 | ~1.1k (matches) |
| PC C, heist files: `port/src/port_heist.c` 2,048, `port/src/port_cctvview.c` 806, Lua bindings `port/luavm/port_luavm_heist.c` 627 + `port_luavm_cctv.c` 271, `port/src/port_probe.c` 401 | 5 | 4,153 | |
| PC C, small edits to existing fork files (`port_lua.h` 108, `port_console.c` 120, `port_coop.c` 92, `port_luavm.c` 78, `port_luavm_arena.c` 76, `port_blocks.c` 61, `port_arenamode.c` 52, `input_pc.c` 40, `joy_port.c` 39, ...) | 26 | 804 (-29) | |
| **PC C total** | 31 | **4,957** (-29) | ~4.6k (higher) |
| Hooks in `src/` (largest: `bg.c` 121, `propobj.c` 56, `chraction.c` 43, `bondview2.c` 38, `bgfog.c` 25, `fr.c` 19) | 19 | 403 (-14) | ~420 in ~20 files (matches) |
| Level generator `tools/gen_level.py` | 1 | 1,034 | ~1k (matches) |
| Generated level data (`port/levelgen/*`, `tools/levelgen/*.json`) | 8 | 4,306 | |
| Tests (`tests/heist` 1,633, `tests/levelgen` 433, `tests/README.md` 1) | 19 | 2,067 | |
| Docs (`designs/bank-heist*.md` 511, `modding/levelgen.md` 183, `modding/lua.md` +159) | 4 | 853 | |
| **Tests + docs** | 23 | **2,920** | ~2.5k (higher) |
| Other: `CMakeLists.txt` 33, `include/string.h` 26 + `include/math.h` 11 (the build-hygiene fix in tier 1, not heist-specific), `tools/mode_probe.lua` 94, `tools/mode_levelgen_test.lua` 27, a launcher 11, `snap_embed.py` 4 | 7 | 206 | |

### Fork foundations the heist depends on (none of it is upstream)

| Foundation | Files | Lines added |
| --- | --- | --- |
| Lua 5.5.1, vendored unmodified (`port/lua`) | 59 | 30,675 |
| Lua runtime, C event bus, mode level reset: `port/luavm/port_luavm.c` 1,995, `port/src/port_lua.c` 394, `port_event.c` 130, `port_mode.c` 183, their headers | 7 | 3,030 |
| Arena Lua bindings (`port_luavm_arena.c`) | 1 | 834 |
| Runtime spawning and mode building blocks: `port_spawn.c` 305, `port_arenamode.c` 1,414, `port_faction.c` 732, `port_blocks.c` 1,074, headers | 7 | 3,675 |
| AI changes: `src/game/chraidata.c` +321 (new AI lists), `chrai.c` +258 -32 (mostly co-op: any-Bond tests, the shared mission end), `chraction.c` +467 -7 | 3 | 1,046 (-39) |
| Co-op (`port_coop*`, `port_cooplogic*`) | 5 | 4,341 |
| Netplay (`port_net*`, `port_netcmd`, `port_nethash`, `port_netlock`, `port_netmsg`, `port_netstats`, `port_netio`) | 13 | 4,841 |
| Rollback snapshots and sim-only ticks (`port_snap*`, `port_simonly`, `port_simheap`, `port_simcfg`) | 7 | 2,220 |
| ENet, vendored (`port/enet`) | 19 | 6,509 |
| All other `src/` changes (co-op, the sim/draw split, raised limits, diagnostics) | 44 | 3,921 (-256) |

The earlier estimate of "co-op + netplay ~10k including ENet" was low: co-op and netplay alone are about 9.2k lines,
11.4k with rollback and sim-only ticks, and 17.9k with ENet. In total the fork changes 47 files in `src/`
(4,967 lines added).

## 3. Rules every proposed change follows

- **N64 safety.** `src/` still builds a byte-matching N64 ROM. Every `src/` change goes inside `#ifndef TARGET_N64`. Where
  a shared line has to change, it is written so the N64 preprocessor output stays the same. Example: `dyn.c` swaps
  `getPlayerCount()` for `portAllPlayerCount()` on shared lines and adds `#define portAllPlayerCount() getPlayerCount()`
  under `TARGET_N64` (`src/game/dyn.c` ~10). The check used for PR #83: preprocess each touched `src/` file with the
  N64 defines (`gcc -E -P -DTARGET_N64 ...`) before and after the change, and compare the token streams, ignoring
  whitespace. The exact command line (include paths, other defines) is not recorded in the repo **(unverified)**. It
  has to match upstream's N64 build. A matching-ROM build is the stronger check, if upstream runs one.
- **Determinism.** The stock attract demos are the oracle. The `ge: demo tick` lines of dam1 (453 lines) and facility1
  (419 lines) must match `tests/baseline` (recipe in [determinism-and-netplay.md](determinism-and-netplay.md)). Every
  hook does nothing unless something is registered or a mode is on, and modes never run in demos, recordings or
  multiplayer (`port/include/port_mode.h`).
- **Presentation never touches sim state.** Presentation code must not draw from the sim RNG or allocate from the sim's
  pools ([determinism-and-netplay.md](determinism-and-netplay.md) "Patterns to remember").
- **Upstream's PR bar.** Both platforms build in CI, no new warnings, tested in game on named levels, decompiled names
  kept, short comments, one change per PR, AI use disclosed.

## 4. Tier 1: standalone engine improvements

Each of these is useful with no mod system. They are listed smallest first.

### T1.1 Audio: the `alAdpcmPull` NULL-table crash

- **What it does.** `_pullSubFrame` (`src/libultrare/audio/env.c` ~374) returns at once unless the voice is
  `AL_PLAYING`. This is the stock libultra test; GE's copy only checks `outCount`. The steal path of `alSynAllocVoice`
  (`src/libultra/audio/synallocvoice.c` ~66) checks the result of `__allocParam()` before writing through it.
- **Mechanism.** When the synthesizer's update pool runs dry, `alSynStartVoice` drops the START and returns. A later
  volume, pan or stop update on that voice makes the env mixer pull a load filter that never got a wavetable, and
  `alAdpcmPull` reads `table->waveInfo.adpcmWave.book` through NULL (`load.c:68`). A dump at the crash showed `table=0`.
  With the fix a dropped START is just a silent sound.
- **Why upstream wants it.** This is shared engine code. It was found on a fast-forwarded replay (`GE_CLOCKSCALE` with
  sim-only ticks: many ticks of sound events per audio frame). It crashed 3 times in about 9 replays of one recording,
  and 0 times in 14 replays after the fix. Whether normal-speed play can run the pool dry is **(unverified)**.
- **N64:** both changes are inside `#ifndef TARGET_N64`. **Determinism:** audio is presentation; the commit reports dam1
  identical. **Size:** 15 lines in 2 files. **From:** `bot` commit 53ff7c9 (`git -C <bot worktree> show 53ff7c9`); it is
  not on `heist` yet. See [audio.md](audio.md).

### T1.2 `GE_SKIPINTRO` counts sim ticks, not polls

- **Upstream today.** `port/src/input_pc.c` (upstream ~7710) taps A through the intro cameras on a counter that goes up
  once per input **poll**. How many polls happen per sim tick depends on the wall clock, so in two runs of the same
  headless test the intro ends on different ticks, and everything after it is shifted.
- **The fix.** The window counts `g_PortSimTickNo`, which goes up once per sim tick in `portInputMouseTickConsume`
  (fork `input_pc.c` ~8822; the function itself exists upstream and is called from `joy.c`). The taps also stop once
  `portP1ControlsLive()` says Bond is in the player's hands, so they cannot cycle his weapons (fork `input_pc.c`
  ~7917-7941). Both helpers exist upstream.
- **Why upstream wants it.** It makes any headless test that uses `GE_SKIPINTRO` with `GE_FIXEDTICK` start play on the
  same tick every run. Without that, two runs cannot be compared tick by tick.
- **N64:** `port/` only. **Determinism:** only the test harness's fake input changes; the demos do not use it.
  **Size:** about 20 lines. **From:** fork `input_pc.c`. Row 1 of the nondeterminism table.

### T1.3 Door collision vertices kept when the door is built

- **What it does.** In `doorInit` (`src/game/propobj.c` ~13726), `doorBuildClippedVertices` writes the door's clipped
  shape into the per-frame vertex pool (`dynAllocateVertices`). The stock door tick copies it into `door->unkcc` on the
  door's first tick, frames later, when that pool may already hold whatever was drawn in between. The fork copies the
  shape at once (~13777-13795).
- **Why upstream wants it.** It is a read of recycled memory, so a door's collision can depend on render history. The
  fork found it as a netplay desync: a shot hit Dam's start gate on one machine and missed on the other (playtest 7,
  found with the RNG trace). Upstream's own netplay would hit the same thing. Whether it has caused a visible bug in
  solo play is **(unverified)**.
- **N64:** inside `#ifndef TARGET_N64`. For upstream, drop the fork's test switch (`GE_NET_TEST_DOORVTXOLD`) and check
  whether the call to `portPartCacheVerticesFreed` (a fork cache) is needed there. **Determinism:** the fork reports
  dam1 identical. **Size:** about 20 lines. **From:** `propobj.c`, row 7 of the nondeterminism table.

### T1.4 Determinism diagnostics: a state hash and `GE_HASHLOG`

- **What it does.** `portCoopStateHash` (`port/src/port_coop.c` ~2831) hashes the sim state into 12 sub-hashes (`rng`
  seeds and draw counts, `players`, `chrs`, `chrai`, `objs`, `proj`, `doors`, `props`, `flags`, `onscreen`, `clock`,
  `settings`), with an optional text dump of the world. `hashLogTick` (~339) writes one line per sim tick to
  `GE_HASHLOG=<file>` in solo runs, and `GE_HASHLOG_DUMP=<from>-<to>` adds the dump. Two runs with the same inputs must
  write the same file. `GE_NET_RNGTRACE=1` adds a ring of recent RNG draws with their callers.
- **Why upstream wants it.** Netplay is in development upstream, and a desync detector with named fields is the first
  tool it needs. A solo hash log finds nondeterminism with no network at all, which is how most of the fork's
  fixes were found.
- **Where it would come from.** Today it lives in `port_coop.c` and is tied to co-op (`sCoopOn`, the session tick
  `sPupTick`) and to the heist (`portHeistStateHash` is called by name inside the `flags` field, ~3079). A standalone
  version would be its own file, with a registration call for extra state (see tier 2).
- **N64:** `port/` only. **Determinism:** read-only; the dump buffer comes from `malloc`, not a sim pool. **Size:**
  about 400 lines (the hash function is ~300). The findings table itself can be offered as a document whether or not
  the code is wanted.

### T1.5 Build hygiene: implicit function declarations are errors in `port/`

- **Upstream today.** `CMakeLists.txt` passes `-w` to everything (`add_compile_options`, ~32). The C files build with
  `-ffreestanding` against the repo's shadow libc headers, so a call to an undeclared function silently becomes
  `int f()`. That happens to work for int and pointer returns on i386, and returns garbage for floats.
- **The fork's change (commit a7989cc).** `-w` moves to the decompiled game, engine, gu/audio, loose-asset and fast3d
  sources; `port/` sources get `-Werror=implicit-function-declaration -Wno-format-truncation` (fork `CMakeLists.txt`
  ~457-460). `include/string.h` gains 16 prototypes (`memset`, `memcmp`, `strcpy`, ...), `include/math.h` gains
  `fabsf fmaxf fminf tanf`, and a few files get forward declarations. The sweep found 107 sites in `port/` and 16 on
  fork lines in `src/`. Two were real bugs, both in fork files: `fmaxf`/`fabsf` in `port_crowd.c` and
  `atan2f`/`fabsf` in `port_heist.c`.
- **Why upstream wants it.** Several of the fixed sites are in files upstream also has (`input_pc.c`, `video.c`,
  `main.c`, `audio_pc.c`, `port_profiles.c`). How many sites upstream `main` has on its own is unknown until the sweep is
  run there **(not done)**.
- **N64:** the commit calls the header additions "PC C only". Whether the N64 build reads those headers is
  **(unverified)**. **Determinism:** only changes behaviour where a garbage float return gets fixed. **Size:** 80 lines in
  17 files in the fork; the upstream version would differ.

### T1.6 Runtime chr spawning with reserved slots

- **Stock mechanism.** Chr slots are the setup's guards + 10 (`alloc_init_GUARDdata_entries`, `src/game/initguards.c`
  ~30). Animated-model slots are `numanimated + ANIM_MODEL_SPARE_SLOTS` and object-model slots `numobjs +
  MODEL_SPARE_SLOTS` (`modelmgrAllocateAnimModelSlots` ~66 and `modelmgrAllocateModelSlots` ~32,
  `src/game/initunk_005520.c`). Level scripts spawn through `chrSpawnAtCoord` (`src/game/chraction.c` ~11670).
- **What the fork adds.** `port/src/port_spawn.c` (`portSpawnChr`, `portSpawnChrAt`, `portSpawnChrAtPad`) wraps
  `chrSpawnAtCoord` and never calls `chrAllocate`, which takes the first free slot with no NULL check. It keeps
  `PORT_SPAWN_MIN_FREE` (4) slots free so the level's own spawn scripts keep their margin (~144), finds a clear spot
  with the same `stanTestLineUnobstructed` / `stanTestVolume` tests as the stock ring search (~59), applies a descriptor
  (body, head, AI list, max damage, flags, chrnum), and emits `PORT_EV_CHR_SPAWNED` (~205). Extra slots are added at the
  three allocation sites by `portArenaExtraSlots`, `portFlatlandExtraSlots` and `portHeistExtraSlots` (which reads
  `GE_MODE_SLOTS`, capped at 3000; `port_heist.c` ~779), plus `portHeistExtraObjSlots` for guns, keys and loot (~796).
  `dynInitMemory` (`src/game/dyn.c` ~76-91) grows the display-list and vertex pools by 1536 bytes per extra slot.
- **Generic version.** One query, called at the three allocation sites, returning extra chr, anim and object slots for
  the stage (0 by default), plus the spawn API as it is. That replaces three mode-named functions.
- **Why upstream wants it.** Companions in co-op, test harnesses (spawn a guard on a pad and test its AI), and any mode.
  With many chrs, the raised stock limits belong with it (props 600 to 2400, model hit entries 600 to 32000, and others;
  [performance.md](performance.md) "Stock limits").
- **N64:** all inside `#ifndef TARGET_N64`. **Determinism:** zero extra slots by default, so allocation is unchanged and
  dam1 is identical. **Size:** about 350 lines (`port_spawn.c` and its header) plus about 30 hook lines.

### T1.7 Dormant chrs (performance)

- **What it does.** A dormant chr's move test checks the level only, not props (`cdtypes` 0 instead of 0x1f in
  `sub_GAME_7F01F614`, `src/game/chr.c` ~1305), and a dormant chr in `ACT_DEAD` runs no AI list (`chrlvActionTick`,
  `chraction.c` ~10233). [performance.md](performance.md) also records that the move test is skipped when the chr has
  not moved, and that spectators get the stock no-collision flag `CHRFLAG_00010000`; those two were not re-checked for
  this page.
- **Measured.** Arena benchmark phase D (crowd 1000 + 250 corpses): 241 ms down to 1.22 ms sim per frame, together with
  the `roomGetProps` fix. The trade-off: nothing collides with a dormant chr's body.
- **Today** it is keyed on arena state (`portArenaChrDormant`, `port_arenamode.c` ~230: chrnum at or above the arena
  base, and a mode is on). **Generic version:** a per-chr flag that whoever spawned the chr can set. Nothing sets it in
  stock play, so the stock game is unchanged. Stock levels have few chrs, so it would not speed them up much.
- **Size:** about 30 lines in `src/` plus the flag.

### T1.8 Camera-view render override (presentation only)

- **What it does.** One player's pass is drawn from another eye (a camera's position, look, FOV and room), while the sim
  keeps Bond's own camera for everything it reads. In the fork this is the heist's security feed,
  `port/src/port_cctvview.c`. Three render-side things change in that pass:
  1. `bgRoomVisibilityRelated` (`src/game/bg.c` ~1447) runs a portal walk from the camera before the sim's walk
     (`bgCctvWalk` ~1495, `bgCctvListBuild` ~1521);
  2. after the sim has built and used its matrices, `portCctvPassMatrices` (called from `lvlRender`, `src/game/lv.c`
     ~1363) rewrites the two projection matrices the display list loads, so Bond-camera space maps to the camera's
     (`P_cam * V_cam * V_bond^-1`);
  3. the gun and crosshair lists are dropped and the per-room scissors are off (`bondview2.c` ~13259, ~13477).

  Chrs posed for the feed are drawn from a reserved corner of the vertex pool (`portCctvDrawChrs`, `portCctvPoolExtra`
  in `dynInitMemory`), and both RNGs and their draw counts are put back afterwards. Those are rows 3 and 4 of the
  nondeterminism table: the feed used to pull a muzzle-flash random number and shift later allocations on the watching
  machine only.
- **Why upstream wants it.** A spectator view for a dead co-op or netplay player, a free or debug camera, cutscene
  tools, monitors that show another room.
- **A dependency.** It sits on the fork's sim/draw split (the "canonical sim view": `viSimViewMatchesDraw` in `fr.c`,
  `bgSimRoomsSnapshot` and `bgRenderListSwapIn` in `bg.c`). Upstream `main` has none of these (grep: 0 hits). So this
  is not a small PR, and it would follow upstream's netplay work. If their netplay has its own split, the override
  should sit on theirs.
- The sim half (which camera a Bond watches, and his input being swallowed while he watches; `portCctvViewTick` from
  `bondviewMovePlayerUpdateViewport`, `bondview2.c` ~11668) is gameplay. It stays with the mode or goes through tier 2.
- **N64:** all inside `#ifndef TARGET_N64`. **Determinism:** presentation only by design, and checked by the heist
  determinism test with a feed open. **Size:** about 800 lines in `port_cctvview.c` (some of it heist-specific: camera
  records, names, cycling) and about 200 in `src/`. A generic version might be about 500 lines **(estimate)**.

## 5. Tier 2: hook points and events

These are the calls that a C engine mod and a script layer would both use. Without a listener, each hook is a bounds
check and a return, which is why the demos stay identical.

### What exists in the fork

**Lifecycle**

| Hook | Fork code | Site |
| --- | --- | --- |
| Stage load (before setup) | `portModeStageStart`, `portModifierStageStart`, `portLuaStageStart` | `lvlStageLoad`, `src/game/lv.c` ~805-824, before `proplvreset2` |
| Stage start (first tick every Bond is in first person) | `PORT_EV_STAGE_START` | `portBlocksTick`, `port/src/port_blocks.c` ~1055 |
| Per tick | `portBlocksTick` (returns at once unless a mode is on, a listener exists or Lua has a tick handler) | `chrlvAllChrTick`, `chraction.c` ~10436, before the chrs run |
| Stage end | **none on the bus.** The nearest are `portModifierStageEnd` (`lvlUnloadStageTextData`, `lv.c` ~2466) and the mode's own `portModeEndMission` (`port_mode.c`) | proposed new |

**The event bus** (`port/include/port_event.h`, `port/src/port_event.c`): `portEventOn` / `portEventOff` /
`portEventEmit`, up to 8 listeners per type, payload four ints. Emit sites:

| Event | Emitted from |
| --- | --- |
| chr died (`CHR_DIED`, victim and killer) | `deathsTick` in `port_blocks.c` polls `chrIsDead` per slot. The killer comes from a port ring filled in `handles_shot_actors` (`chraction.c` ~2538, `portModeNoteHit`), because the engine records no attacker |
| damage (`DAMAGE`), melee included | chrs: `handles_shot_actors` (~2736; Bond's fists arrive as `ITEM_FIST`, so slaps are covered) and `chrlvExplosionDamage` (~2827); Bond: `record_damage_kills` (`bondview2.c` ~13789) |
| pickup (`ITEM_PICKUP`) | `propPickupByPlayer` (`propobj.c` ~11595, ~11819) |
| use press (`USE`) | `bond_interact_object` (`chrprop.c` ~1796, and ~1823 for a press at nothing), through `portHeistNoteUse`. A door interaction is a `USE` whose target is `PROP_TYPE_DOOR`; there is no separate door event |
| gadget used | the gadget fire site, `gunfire.c` ~4915 (`portHeistNoteGadget`) |
| alarm start / stop (`ALARM`) | `alarmActivate` / `alarmDeactivate` (`propobj.c` ~16021, ~16042) |
| objective change | `display_objective_status_text_on_status_change` (`objective_status.c` ~394) |
| camera spotted Bond | the CCTV branch of the object tick (`propobj.c` ~5596-5690, through `portHeistCctvAllBonds` / `portHeistCctvSpotted`) |
| chr spawned | `portSpawnChr` |
| heist only: body found, knocked out, woken, searched, civilian surrendered / robbed / killed, camera view | `port_heist.c`, `port_cctvview.c` |

**Doors and keys** (`port/src/port_heist.c`): `portHeistDoorList` / `Get` / `Move` / `Keyflags` / `OneWay` / `Snap` /
`TryDoor` (~171-305), `portHeistSpawnKey` and `portHeistKeyState` (~398, ~434). Per-player keys in co-op go through
`bondinv.c` ~773.

**Cameras and alarm:** `portHeistCctvList` / `Get` / `Enable` / `SetAlarmTime` (`port_heist.c` ~478-532),
`portCctvSeesPoint` and `portCctvSeesChr` (`port_cctvview.c` ~729, ~762), `portHeistAlarm` and `portHeistAlarmActive`
(~454, ~474).

**AI decision points** (details in section 7): the alarm with no alarm box (`chrTryStartAlarm`, `chraction.c` ~11877),
"saw someone die" for bodies (writes the stock `chrseedie` field, `port_heist.c` `bodyScan` ~893), the hold-fire
predicate (`chrlvTriggerFireWeapon` ~7489), and the knock-out state (`handles_shot_actors` ~2525, `chrlvActionTick`
~10235).

### What is wrong with the current shape

- Generic things carry mode names: `src/` calls `portHeistNoteUse`, `portHeistSlap`, `portHeistHoldFire`, and the heist's
  state is folded into the hash by name.
- `chrlvActionTick` (`chraction.c` ~10229-10237) asks five fork predicates in a row before running a chr's AI list:
  the bot, hostages, arena dormant chrs, heist knock-outs, the AI governor. One "who drives this chr" hook would
  replace all five.
- Extra slots come from three mode-named functions (T1.6).
- The bus has no stage-end event, eight listeners per type, and int-only payloads.

### A sketch of the C API

This is a sketch, not code that exists. The point is that it is small and has no Lua in it.

```c
/* lifecycle: all on the sim thread, in sim order */
typedef void (*PortStageFn)(int stage, void *ud);
typedef void (*PortTickFn)(int ticks, void *ud);            /* ticks = g_ClockTimer for this step */
int  portHookStageLoad(PortStageFn fn, void *ud);           /* lvlStageLoad, before proplvreset2 */
int  portHookStageStart(PortStageFn fn, void *ud);          /* first tick every Bond is in first person */
int  portHookStageEnd(PortStageFn fn, void *ud);            /* lvlUnloadStageTextData, before the cleanup */
int  portHookTick(PortTickFn fn, void *ud);                 /* chrlvAllChrTick, before the chrs run */

/* events: port_event.h as it is, plus STAGE_END and DOOR_STATE */
int  portEventOn(int type, PortEventFn fn, void *ud);
void portEventEmit(int type, int a, int b, int c, int d);

/* slots: asked once per stage at the three allocation sites; all 0 by default */
typedef struct { int chrs, anims, objs; } PortStageSlots;
int  portHookStageSlots(void (*fn)(int stage, PortStageSlots *add, void *ud), void *ud);

/* who drives a chr this tick */
enum { PORT_BRAIN_STOCK, PORT_BRAIN_SKIP, PORT_BRAIN_HANDLED };
int  portHookChrBrain(int (*fn)(ChrRecord *chr, void *ud), void *ud);              /* chrlvActionTick */

/* AI predicates: return -1 for "no opinion" and the stock code runs */
int  portHookChrMayFire(int (*fn)(ChrRecord *chr, PropRecord *target, void *ud), void *ud); /* chrlvTriggerFireWeapon */
int  portHookAlarmNoBox(int (*fn)(ChrRecord *chr, s32 pad, void *ud), void *ud);           /* chrTryStartAlarm */
int  portHookMeleeHit(int (*fn)(ChrRecord *chr, int player, void *ud), void *ud);          /* handles_shot_actors, ITEM_FIST */

/* sim state a mod owns: folded into the state hash and the desync dump */
int  portStateHashAdd(const char *name, unsigned (*hash)(void), void (*dump)(char *buf, int cap));

/* queries and actions, by setup index */
int  portDoorGet(int setupidx, PortDoorInfo *out);
int  portDoorMove(int setupidx, int open);
int  portCctvGet(int setupidx, PortCctvInfo *out);
int  portCctvSees(int setupidx, const coord3d *pos);
int  portAlarmActive(void);
void portAlarmSet(int on);
ChrRecord *portSpawnChr(const PortSpawnDesc *d);             /* T1.6 */
```

Rules for callers: callbacks run in sim order and must be deterministic (no wall clock, no local-player reads, no
window reads). Presentation hooks (HUD, camera override) are a separate list and must not change sim state.

**Size of a first tier-2 PR** (lifecycle hooks, the bus, about six emit sites, no Lua): roughly 300-400 lines
**(estimate)**. The fork already has a C unit test for the bus (`tests/event_test.c`, 97 lines).

## 6. Tier 3: an optional scripting layer

- **What it is.** Lua 5.5.1 vendored unmodified (`port/lua`, 59 files, 30.7k lines), with the glue in `port/luavm/`
  (`port_luavm.c` 1,995 lines plus bindings) and `port/src/port_lua.c`. Scripts are listeners on the tier-2 bus, and the
  bindings call tier-2 queries. How it hooks the engine: [lua-modding.md](lua-modding.md). The API:
  [`modding/lua.md`](../modding/lua.md).
- **Where scripts load from.** `Data/Scripts/boot/*.lua` once, `Data/Scripts/*.lua` at each solo mission start,
  `Data/Scripts/modes/<name>.lua` when `GE_MODE=<name>` or the Mods page picks it.
- **Determinism rules**, as built:
  - `math.random` is re-seeded at each stage start from the session seed; a solo run takes `GE_SEED` when set
    (`portLuaGameRandomSeed`, `port_lua.c` ~318);
  - no `io`, `os`, `debug` or `package`, and no wall clock;
  - the VM lives in a fixed arena with its own deterministic allocator (`LUA_ARENA_SIZE` ~54, `portLuaAlloc` ~157 in
    `port_luavm.c`), so it can be snapshotted for rollback; the 16 MB cap is from [lua-modding.md](lua-modding.md) and
    was not re-checked here;
  - an instruction limit of 50 million per call (`LUA_INSN_LIMIT` ~34, a count hook ~1738);
  - a stable `table.sort`, and arrays iterated by index;
  - script state that matters goes into the state hash through `mode.state(text)`: its FNV hash is folded into the
    `flags` sub-hash and the text into the desync dump (`portHeistModeState`, `port_heist.c` ~1973);
  - in netplay the handshake hashes every script, so both machines run the same files.
- **Presentation-only APIs** (HUD text, sounds, the camera feed, `bond.rooms_drawn`) never change sim state.
- **If upstream designs its own Lua layer**, the fork's API is a record of what an arena and a heist needed, and the
  modes would be ported to theirs. **If upstream wants C engine mods only**, the heist's 1.1k lines of Lua rules would be
  rewritten in C against tier 2 **(estimate: similar size)**, or the mod keeps its own Lua.
- **Licence.** Lua and ENet are MIT, listed in the fork's `tools/THIRD_PARTY_NOTICES.txt`.

## 7. Gameplay features that could become generic AI features

These are sim features with a flag each, off by default, so the stock game is unchanged. All of their state is in the
heist's part of the state hash (`portHeistStateHash`, `port_heist.c` ~1993). Today they only run while a mode is on:
`portHeistAiTick` is called from `portBlocksTick` only in a mode (`port_blocks.c` ~1040).

| Feature | Mechanism (fork) | Hook site | Off by default? |
| --- | --- | --- | --- |
| Body discovery | Every 8 ticks `bodyScan` (`port_heist.c` ~893) checks each body (dead, or knocked out) against each living non-civilian guard: within `min(visionrange * 100, 1500)` units, within 150 units in height, in the same room (`chrlvMaybeSameRoom`); no facing test. It sets the stock `chrseedie` field, so the guard's own AI list reacts as if it saw a death. The first finder fires `body_found`. Cost: slots x slots every 8 ticks | none in `src/`; runs from the mode tick | `sBodyCheck` defaults on, but the scan only runs in a mode |
| Knock-outs | `portHeistSlap` (~1410), called from `handles_shot_actors` for `ITEM_FIST` (~2525): counted slaps get the stock hit reaction with zero damage; the last one poses the chr (a held KO animation) and it runs no AI list (`portHeistChrNoAi`, from `chrlvActionTick` ~10235). A guard who finds it walks over and wakes it | `handles_shot_actors`, `chrlvActionTick` | yes: `sSlapsToKo` is 0 |
| Civilians and surrender | Chrs a script registers (`portHeistCivilianAdd` ~958). A Bond aiming at one within 800 units for 0.5 s makes it surrender, 2 s of threat makes it drop what it carries, then it cowers (4 s); kills are counted (`civTick` ~1223) | none in `src/`; the mode tick | yes: nothing is registered |
| Hold fire | Before a guard fires at the current pass's Bond, `portHeistHoldFire` (~1047) projects each civilian onto the guard-to-target line; one within the hold radius, short of the target and on the same floor holds the shot | `chrlvTriggerFireWeapon` (`chraction.c` ~7489) | yes: `sHoldFire` is 0 |
| Alarm with no box | In a mode, a guard told to raise the alarm at a pad with no alarm object still plays the alarm animation and the command succeeds (Bunker 2 has no alarm boxes) | `chrTryStartAlarm` (`chraction.c` ~11877) | only in a mode |

As generic engine features they would become per-stage or per-chr options behind the tier-2 hooks (`portHookChrBrain`,
`portHookChrMayFire`, `portHookAlarmNoBox`, `portHookMeleeHit`), with the tuning numbers as parameters. More on the AI
side: [guards-and-ai.md](guards-and-ai.md).

## 8. What stays in the mod regardless

- **The heist's rules:** phases, the safe crack and its jams, the vault time lock, loot, reinforcement waves,
  difficulty scaling, win and fail (`tools/mode_heist.lua`, `tools/heist_lib.lua`).
- **Maps:** the Bunker 2 layout choices ([`designs/bank-heist-bunker2.md`](../designs/bank-heist-bunker2.md)) and the
  generated levels (`tools/gen_level.py` and its layouts, on the cut stage slot LEE: `proplvreset2`, `prop.c` ~1972).
- **Tuning:** guard counts, civilian timers, slaps to knock out, camera names and alarm times, HUD text.
- **Heist-specific behaviour:** the camera console and monitor controls, per-player keys, the alarm-on-spot rule.
- **The level probe** (`port_probe.c`), a read-only dump of a stage's doors, keys, cameras and pads for mode authors.
  It could be a debug tool upstream, but nothing needs it.
- **Assets:** none. The generated levels write only texture **numbers**; the texels are read from the ROM at run time
  (`gen_level.py` header, ~18). No art or audio was AI-generated.

## 9. Suggested first steps with upstream

1. **Open an issue or discussion.** Summarise what the fork has, say plainly that co-op and netplay are theirs, link a
   cleaned copy of this page, and ask the open questions below. Ask before sending code.
2. **Offer the smallest standalone PRs first**, one change each:
   1. the audio crash fix (T1.1, 15 lines);
   2. `GE_SKIPINTRO` on sim ticks (T1.2, about 20 lines);
   3. door vertices at `doorInit` (T1.3, about 20 lines), if they agree it is a bug for them too;
   4. the implicit-declaration errors for `port/` (T1.5), after running the sweep on upstream `main`;
   5. reserved spawn slots and the spawn API (T1.6, about 400 lines), if they want runtime spawning.

   Each gets the same checks as PR #83: Linux and Windows build in CI; dam1 and facility1 identical to the baselines;
   N64 preprocess output identical for every touched `src/` file; no new warnings; tested in game on named levels;
   comments cut to two sentences; AI use disclosed.
3. **Then a tiny tier-2 PR:** stage-load, stage-start, tick and stage-end hooks plus the event bus with a few emit
   sites. No Lua. It ships with the C unit test and the demo check.
4. **Only then** talk about tier 3 and the AI features, once their mod model is known.

## 10. Open questions for the upstream team

- **Mod model:** Lua scripts, C engine mods compiled into the exe, loadable libraries, or a mix? Is "mod loading (xdelta
  patches)" data-only, or will it also carry code?
- **ABI and stability:** should a C hook API stay stable across releases? Versioned? Who may call what?
- **Where mods load from:** `Data/Scripts`, a mods folder, per user or per install, and how a player turns one on or off.
- **Netplay and co-op:** must mods be deterministic and part of the state hash? Is upstream's netplay lockstep like the
  fork's, or something else? Should a session refuse a peer with different mods (the fork hashes every script in the
  handshake)? Do they want the fork's nondeterminism findings?
- **Vendored libraries:** is vendoring Lua (MIT, unmodified) and ENet (MIT) acceptable, or would they rather fetch
  them?
- **Asset rules for mods:** do "no ROM-derived content" and "no AI-generated art or audio" apply to mods shipped
  separately too? Is generated geometry that refers to the ROM's textures by number acceptable?
- **Style:** how they want `PORT:` blocks in `src/` written and named, beyond the two-sentence comment rule.
- **Tests:** would they take test harnesses upstream (the hash log, scripted headless tests), or keep those in forks?

## 11. Not verified for this page

- Whether PR #83 is still open ([performance.md](performance.md) says it was at the time of writing).
- The exact `gcc -E` command line used for the N64 preprocess check.
- Whether the N64 build reads `include/string.h` and `include/math.h`.
- Whether the audio crash can happen at normal game speed.
- Whether the door-vertex read has caused a visible bug in solo play.
- How many implicit-declaration sites upstream `main` has on its own.
- "Every fork change to `src/` is inside `#ifndef TARGET_N64`" is the fork's convention ([README.md](README.md)). It was
  not re-checked hunk by hunk here, and `dyn.c` shows the macro-shim pattern used where a shared line changes.
- The sizes given for generic extracted versions (T1.8, tier 2, a C rewrite of the heist rules) are estimates.
- The Lua VM's 16 MB cap, and the dormant-chr details from [performance.md](performance.md) that are not cited to a line
  above.

## Related

- [determinism-and-netplay.md](determinism-and-netplay.md): the demo oracle, the state hash, every nondeterminism fix.
- [performance.md](performance.md): `roomGetProps` (PR #83), dormant chrs, stock limits.
- [lua-modding.md](lua-modding.md): how the Lua layer and modes hook the engine.
- [rendering.md](rendering.md): the camera-feed render walk and the sim/draw split.
- [audio.md](audio.md): the `alAdpcmPull` crash.
- [guards-and-ai.md](guards-and-ai.md): alarms, deaths, surrender, knock-outs, spawning.

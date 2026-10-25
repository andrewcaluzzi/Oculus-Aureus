# Pitfalls: bugs we hit and why

Each entry: what happened, why, what to do. Most cost someone hours; the fix is usually one line.

## Build and toolchain

**Undeclared float functions returned garbage.** The C code builds `-ffreestanding` against the repo's shadow headers,
and `include/math.h` is an IRIX header with most prototypes under `#if 0`; the tree also built with `-w`. So `fabsf`,
`atan2f`, `fmaxf`, `fmodf` were implicit `int f()` calls: the result came back in the wrong register (x87 `st0` vs
`eax`) and the argument went as a double. Hits: `port_heist.c`'s civilian aim test ("robbed with nobody aiming"),
`port_crowd.c`'s `fmaxf`/`fabsf`, the bot's pitch, the co-op bot's knife throw (an angle-wrap loop that never ended).
Fix (fork, a7989cc): `port/` builds with `-Werror=implicit-function-declaration`, `include/math.h` declares `fabsf
fmaxf fminf tanf` for PC C, `include/string.h` the string functions. Use the game's own `atan2f`
(`game/math_atan2f.h`). Upstream still builds with `-w`, so this can bite there.

**The game's `atan2f` is not libm's.** `src/game/math_atan2f` returns [0, 2 pi), not (-pi, pi]. A pitch came out as
358 deg. Normalise, or use `__builtin_atan2f`.

**A "Debug" build is `-O2`.** The optimisation flag is global (`CMakeLists.txt` ~29). Falling off the end of a non-void
function is live UB at `-O2`: audit by swapping a TU's `-w` for `-Wreturn-type`.

**`errno` is `#undef`'d** by `include/PR/os.h` in C files. Use `port_errno`.

**PowerShell 5.1 splits `-DFOO=a.b` at the dot**; quote `"-D..."`. **Git Bash mangles docker `-v /mnt/c/...`**; run
docker from PowerShell.

**Stale Lua.** Mode scripts are copied to `Data/Scripts/modes` only as a POST_BUILD step of `ge`. An up-to-date build
(no relink) keeps the old copies. A whole round of bot measurements ran stale heist Lua (tune 2 measured 7/10 stale vs
9/10 real). Copy by hand or force a relink after editing a `tools/mode_*.lua`.

**32-bit `readdir` on a 64-bit-inode filesystem fails** (a Docker bind mount): `Data/Scripts` looked empty in the
Linux container. The fork builds the affected files with `_FILE_OFFSET_BITS=64`.

## Engine surprises

**The C buttons do not move Bond on PC.** The port's pad path takes the strafe from the Move stick's x and clears the C
buttons' digital steps (`bondview2.c` ~7777-7794). Strafe with the stick or the digital move ([player.md](player.md)).

**Bond's prop is at eye height** (~150 above the floor); a chr's origin is ~105-110 above its floor. Use the stan tile
for "same floor" tests.

**Before Bond spawns, `vv_theta` is garbage** (~8e9). A subtract-360 wrap loop on it never ends. Wait for a sane theta
and a stan tile under Bond.

**Pad centres sit on tile edges.** A Bond warped exactly onto a pad centre is invisible to `chrCanSeeBond` (the ray
must end on his tile) and cannot pick up an item lying there (zero-length stan line). Offset warps by ~20 units.

**Setup `.c` edits do nothing** in a default build: setups come from the ROM. FALLBACK build + `GE_ROMSETUP=0`, or change
it at run time.

**AI script arguments are byte-swapped** in the setup macros: `objective_bitfield_set_on(0x00100000)` is flag 0x1000,
`0x4a01` as a pad is 330.

**The setup's guard "health" field is hearing**, and `ReactionTime` is vision range (`expand_09_characters`).

**`IFISeeBond` and `IFICouldSeeBond` are different tests**: the first has the 220-deg cone and a random roll, the second
is only a line ([guards-and-ai.md](guards-and-ai.md)). A contact waiting for the first never starts behind his back.

**Stock AI lists with bugs**: `m_RunToBondPersistent` stops at first sight at any range and re-issues the run every tick
(Rare's "shuffles uselessly" comment); `GAILIST_AIM_AT_BOND` aims and never fires.

**Re-issuing a chr's route mid-walk makes it walk away** (each plot starts from its nearest waypoint). Re-plot when it
stopped or the target moved far.

**`chrAllocate` has no NULL check on the slot; `chrSpawnAtCoord` needs 3 free slots.** Gate on `chrGetNumFree()`.
**Head 0 is a whole body** (194 anim records), which overflows the 140-record spare anim slots: the model is NULL. Use
head -1.

**The ModelHitEntry pool (600 stock) runs dry** with roughly 25-40 chrs drawn (each takes one entry per model node,
about 15-25): chrs late in the draw order turn partial,
then invisible, then `chrTestHit` crashes on a NULL hit list. The fork raised it to 32000.

**`stanTestLineUnobstructed` writes scratch globals** (`stanSavedColl_*`). Code outside the sim that calls it (the bot,
debug tools) must save and restore them, or it changes the sim.

**A key's `pad` field is a chrnum** when `PROPFLAG_ASSIGNEDTOCHR` is set.

**A cut stage with no briefing row crashed the mission-end screen** (`strcmp(NULL)`), and `language.c` looped forever on
an unknown level. Both bite every new stage slot ([levels-and-levelgen.md](levels-and-levelgen.md)).

**Audio under fast-forward crashed in `alAdpcmPull`** (a NULL wavetable after a dropped voice START). Fixed on the bot
branch only ([audio.md](audio.md)).

**Restoring part of the audio state hangs** in `alSynAllocVoice`. Audio is wholly outside snapshots.

## Determinism

**Wall-clock-dependent intro skip.** `GE_SKIPINTRO` counted polls (which follow the wall clock), so every test run
started play on a different tick. Count sim ticks. The same applies to any test hook: key everything on
`g_PortSimTickNo`, never on polls or frames.

**Clock-seeded Lua.** A solo mode's `math.random` was seeded from the clock, so a recorded heist replayed with other
guards in other places. Set `GE_SEED` (the fork seeds solo Lua from it).

**Free-running runs are not repeatable.** Without `GE_FIXEDTICK=1` frame timing feeds the sim.

**Presentation that touches the sim**: a draw that takes sim RNG numbers (a muzzle flash in `model.c`) or allocates from
the frame's linear pool (`dyn.c`) on one machine only. Give presentation its own RNG and its own memory.

**The sim reads render output**: `model->render_pos`, door vertices copied from a recycled per-frame pool, a monitor's
display-list pointer. Replays that draw different ticks differ (the photo objective). See
[determinism-and-netplay.md](determinism-and-netplay.md) for the full list.

**Host and guest agreeing proves nothing**: they can share a hidden input (machine speed). Replay a log offline.

## Tests and harnesses

**`xvfb-run` gives false oracle mismatches** (dam1 differs from the baseline from tick 185 for every build). Use a real
Xvfb on `:99` and `DISPLAY=:99`, or compare runs made the same way.

**Screenshots under Xvfb are black** (HUD only) without `GE_FULLHEIGHT=0 GE_EDGEINSET=1`.

**Screenshots filled the disk.** BMP batches (~20 MB a run) took C: to 0 bytes and broke WSL. `NOSHOTS=1`, convert to
PNG, write batches elsewhere ([tooling-and-testing.md](tooling-and-testing.md)).

**`GE_CONCMD` runs before the stage loads**, so a `spawn` in it is dropped.

**`GE_COOP_KILL` under invincibility** does not kill but leaves `bondhealth` 0, which ends the mission.

**Things that end a stage by themselves** in a soak: Z or use while the watch is open (the Abort page), a Return-only
START, Dam's bungee waypoints.

**The bot must not use the watch menu**: it is driven from draw code, so sim-only ticks and replays would differ. A
only cycles guns; gadgets come with the "next inventory" port action.

**The nearest pad by x/z can be on another floor.** On a stacked map (bank v2: the roof lies over the hall), a test that
picks "the pad nearest this point" from `pads.far_from(x, z, ...)` can get a pad 1400 units up. The hold-fire test put
its civilian on the roof that way, and the y check rightly ignored it. Filter by `y` from `pads.pos` (2026-10-26).

**An on-screen guard never opens a door locked to AI**, and an off-screen guard hops through any shut door. A door
test that passes off screen proves nothing about on screen ([guards-and-ai.md](guards-and-ai.md), routing).

**Small samples are noisy**: the bot's heist ladders use 10 seeds per setting, and its own notes call that noisy. Use
more seeds before concluding from a difference of a few wins.

## Lua and modes

**`Data/Scripts/*.lua` runs inside modes too**: a leftover weapon script (a "crazy KF7") made a mode's guards strong.

**Doors are all open in a mode by default** (the UNLOCK DOORS cheat at one second). Call `mode.keep_locks()` at load.

**On Bunker 2 with locks kept, Bond starts locked in a cell** (pad 49, room 40, behind a key-0x1 door with the key
outside). A mode must open it or move him.

**Zone and room tests by circles miss odd-shaped rooms**: the heist's `in_vault` circles missed the west end of room 56,
so a Bond there when the vault shut was "sealed in" for good. Test `bond.room` against the room list.

**Modifier percent knobs start at 100**, filled from each default on first use (`modInit`); forgetting that made
`enemy_damage` zero after any set.

**`math.acos` returns NaN in the game's Lua** (the gadgets' tape-loop aim, 2026-10-26: every camera came out `-nan` degrees off).
`math.sin`, `math.cos`, `math.atan` (one or two arguments) and `math.sqrt` are fine; take an angle from `math.atan(|cross|, dot)`.

**A test mode must not share a name with a module it includes**: `GE_MODE=heist_gadgets` loaded the test file, which then
included `heist_gadgets` (itself) instead of the gadgets module. The gadgets test runs as `heist_gadgets_test`.

**Moving Bond after `MoveBond` does not move his camera that frame**: the view is built inside `MoveBond`. The piton line
places him before it (`port_gadgets.c`, from the hook ahead of `MoveBond` in `bondview2.c`), with `field_70` (his feet) at the
line's height and `field_7C` (fall speed) 0: the fall `MoveBond` then applies is a fraction of a unit per tick.

**`pads.far_from` ignores height**: on the bank a "nearest pad" to a hall point can be on the gallery above it. Filter by `y`
(the gadgets test's `warp_xz(p, x, z, y)`).

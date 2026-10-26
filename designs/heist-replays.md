# Heist replays: record, scrub, watch from any guard's eyes (design)

Status: design only, nothing built. Branch `designs` (from heist b389df2). Claims about existing code cite the file and function they come from. Guesses are marked **(guess)**. Things to check before building are marked **verify**.

## 1. The pitch

Every heist records itself. Afterwards you can:
- watch it back from a free camera, from behind any guard, or **through a guard's eyes**, with his awareness meter, his sight cone and what he heard: "why did he see me?";
- jump straight to the moment you were spotted;
- scrub back and forth;
- send the file to a friend;
- race a ghost of your best run.

It works because the sim is already deterministic and three recorders already exist. This design picks the right one, wraps it in a versioned file, and adds presentation-only cameras on top.

## 2. What already records today

| Recorder | Where | What it holds | Use for replays |
|---|---|---|---|
| **Input recorder** | `port/src/inputrec.c` (`GE_INPUTREC` / `GE_INPUTPLAY`) | Player 1's input as the sim consumes it: `P` pad 0 after merge, `A` the Port actions, `M` the mouse-look sample (`%.9g` floats, exact for f32), `D` digital move, and the `T` check (Bond's facing and position, compared on replay as `DRIFT`). Every record is keyed by **sim tick**, not call order (`inputRecNext`). Unknown lines are skipped (`default: continue`), so extra header lines are forward-compatible | **The solo stream.** Proven by `tests/bot/level.sh` / `replay.sh` (a bot run replays without the bot) |
| **Netplay session log** | `port_net.c` (`netLogOpen`, `GE_NET_LOG`, `GE_NET_REPLAY`) | A header: `V` proto/role/run/delay, `B` build, `G` stage/diff/seed/**lua script-set hash**, `M` modifiers, `O` both players' settings blocks, `K` the sim-config list, `E` the sim-affecting env. Then per tick `T`: both 20-byte `PortNetInput` records plus that tick's fingerprint (hash and 8 fields), plus session commands. Replay runs offline as local co-op and compares every tick (`net_replay_log.sh`) | **The co-op stream, and the model for the header** |
| **Attract demo** | stock ramrom (`ramromreplay.c`), oracle `tests/bot/dam1_check.sh` | N64 demo format | Not reusable: co-op and modes switch off under ramrom (`port_coop.c portCoopStageBegin` checks `get_is_ramrom_flag`). It stays as the determinism oracle |

Determinism support already in place:
- `GE_HASHLOG` (`port_coop.c`): the state hash and its 12 fields every tick, solo or co-op. The fields are `rng, players, chrs, chrai, objs, proj, doors, props, flags, onscreen, clock, settings`, and `fpDiffNames` names the ones that differ.
- `GE_FIXEDTICK` / `portInputRecTickClock`: latched presses are judged in sim ticks while recording.
- `port_simsnd.c`: sim-side sound lifetimes.
- The snapshot system (`port_snap.c`): ranges from `snap_embed.py`'s SIM/PRES classification, page-compare restore, and solo savestates (`portSnapSoloRequest`). A save is about 5-6 MB on Dam; a restore copies back 300-700 KB of differing pages (`modding/bot-notes.md`).

## 3. The file format (`.mrep`)

One text file, deflated as a whole when shared. A text core keeps it diffable and makes it a superset of the two existing logs, so the existing loaders keep working.

```
# Project Midas replay                   # '#' lines: comments to both existing loaders
U mrep 1                                 # format version; bumps on any incompatible change
B <build string>                         # port_buildinfo, as SETUP's build[]
R <rom crc1> <crc2> <region>             # data is ROM-served (setup, models): the ROM is part of the sim
G stage <s> diff <d> seed <u> lua <hex>  # as the session log; lua = netLuaHash() (Data/Scripts, boot/, modes/)
L <layout hash> <randomiser seed>        # generated bank: hash of the generated port/levelgen data + the randomiser's params
W mode <name> settings <hash of the HEIST_SETTINGS text> tickhz 60
M <modifier text>                        # netModsText
K <simcfg list>                          # portSimCfgCollect
O <n> <hex> <text>                       # per-player settings blocks (PortNetPlayerOpts)
E NAME=VALUE                             # sLogEnvNames that are set
I <profile name> <date> <result> <time> <score>   # display only, never checked
Q solo | Q lockstep <players>            # which stream follows
(solo)     P/A/M/D/T lines exactly as inputrec.c writes them
(lockstep) T tick rec0 rec1 hash f0..f7, S / X lines and the session-command lines, exactly as the session log
H <tick> <hash> <12 fields>              # every 30 ticks (both streams): the desync check (section 7)
J <tick> <kind> <args>                   # bookmarks (section 8), written as they happen
Y <tick> <x> <y> <z> <theta> <p>         # ghost track, every 6 ticks per Bond (section 9)
Z <ticks> <result>                       # end of file
```

The line letters are chosen not to clash with either loader: inputrec reads `P A M D T` and skips anything else; the session log uses `V B G M O K E T S X`. **verify:** the session-log loader skips letters it does not know, as inputrec's does. If it doesn't, the new letters go behind a `#` prefix for that stream.

**Versioning policy.**
- `U mrep` (the version) must match exactly.
- `B`, `R`, `G` (with its lua hash), `L`, `K` and `O` must match to play in the sim. A mismatch refuses with the differing keys named, the way the net handshake names a differing sim-config key (`portSimCfgDiff`).
- A **lenient** mode (`replay force`) plays anyway and stops cleanly at the first `H` mismatch: "this replay diverged at 04:12 (chrai)".
- The ghost track (`Y`) and bookmarks (`J`) **never need the sim**. They work on any build as long as `L` matches, so ghosts survive game updates.

**Size (guess).**
- Solo: about 4 short lines per tick at 60 Hz, so a 15-minute heist is about 6-8 MB of text. Held inputs repeat, so it deflates to well under 1 MB.
- Lockstep: about 150 bytes per tick, about 8 MB raw.
- **verify:** whether a deflate library is already linked (SDL2 / libpng often bring zlib); if not, run-length the repeated `P`/`A`/`D` lines.

**Where recording happens.** Recording is automatic in a heist:
- Solo: the inputrec hooks with a file opened at stage tick 1.
- Netplay: the session log's lines, written once by the host, plus the extra chunks.

The file goes to `Data/Replays/<date>_<stage>_<result>.mrep`, and the last 20 are kept. C: is nearly full, so the keep-count is a setting and screenshot batches are never part of this.

## 4. Playback

`GE_REPLAY=<file>` (and a front-end "Replays" list later) loads the header, applies it the way `GE_NET_REPLAY` does (stage, diff, seed, mods, settings blocks, env), and drives the input from the file:
- the solo stream through `inputrec.c`'s play path;
- the lockstep stream through `port_net.c`'s replay path (offline local co-op, `GE_NET_REPLAY_VIEW` picks the Bond).

The player's own devices drive **only the presentation**: the replay camera and the UI. They never reach the sim. The pattern is the INTEL action, which `joy_port.c portJoyPortPollCommit` takes off the poll before the sim and the recorder see it. In replay, every live action goes that way.

### 4a. Cameras (all presentation)

The CCTV feed already solves "draw the world from a camera the sim doesn't own" (`port_cctvview.c`, section 10):
- a portal walk from the camera's room and eye before the sim walk;
- the two projection matrices rewritten after the sim used them (`P_cam * V_cam * V_bond^-1`);
- chrs only the camera sees posed into fresh matrices and put back byte for byte;
- the RNG and the frame pool restored (the two §11 determinism fixes).

Replay cameras generalise `portCctvPassBegin` from "a `CCTVRecord`" to "an eye, a look direction, a room and a fov" supplied by presentation:

| Camera | Eye and look | Room |
|---|---|---|
| **Follow Bond** (default) | Bond's own camera: no pass at all | - |
| **Free cam** | flown with the stick or mouse; collision-free, clamped to the level bounds | the stan room under the eye (**verify:** a room lookup by point; `portCctvBondRoom` uses Bond's stan) |
| **Follow guard** | 250 units behind and 80 above his head, looking at him | `portCctvChrRoom(chr)` |
| **Guard POV** | his head; yaw from `portAiFace(chr)` (the AI's facing), pitch 0; fov is the cover cone (`portArConfig`'s 75°) | `portCctvChrRoom(chr)` |
| **Camera POV** | a CCTV camera: the existing feed, unchanged | as today |

Cycling: next/previous weapon cycles guards (`portAiGuardChr(i)`). Use toggles follow and POV. Reload toggles the free cam. The INTEL board still works in a replay (PRES).

### 4b. The guard-POV overlay ("why did he see me?")

Everything it needs is already exported, and reading it is PRES:
- **Awareness meter:** `portAiAwareness(chr, &state, &level, &lastx, &lastz)` gives the state (unaware → combat) and a bar; `lastx/z` is drawn as "where he thinks you are".
- **Cover suspicion and the reason:** `portHeistCoverState(chr, &susp, &witness, &reported, &sees, &role, &why)`. `why` indexes `sCoverWhy` (`armed, loot, act, restricted, staff, running, close, heard, hurt, script`), shown as plain text: "He saw you **armed** in a **restricted** room".
- **Sight cone:** the diagnostics layer's floor fan (`port_arhud.c`, F3) drawn from the replay camera. **verify:** the AR projects through the local Bond's `field_10CC` and `projmatrixf`. It needs the active pass camera's matrix when a replay or CCTV pass is on.
- **What he heard:** `PORT_EV_NOISE` and `portNoiseLast` give only the latest noise. A PRES-side **listener** keeps a 10 s ring per guard of the noises that reached him (position, kind, loudness), drawn as fading rings with labels ("coin", "door", "slap"). It must be a listener, not new sim fields (below).
- **His sub-state:** `portAiInfo(chr, &sub, &subName, ...)` gives "searching: going to the noise", "giving up", "back to post".

**Rule: replay diagnostics never add sim state.** Any new field in the hash changes every hash, and every old replay would then fail its `H` checks. Diagnostics are PRES listeners and readers. If something sim-side is truly needed (say a per-guard "last thing heard" in `port_ai.c`), it is added **before** replays ship, or with an `mrep` version bump.

## 5. Scrubbing

**Seek to tick T** = restore the nearest keyframe at or before T, then run sim-only up to T. `port_simonly.c`'s `g_PortSimOnly` skips the render step, and the fingerprints must match a normal run (the R1 guarantee).

**Keyframes** are `port_snap.c` saves (slot saves at `portSnapTickBegin`, where the rollback ring and the solo savestates already save). They are taken on the first playback pass, never stored in the file: they hold pointers and are only valid for this process and build.
- **Spacing:** every 10 s of sim by default.
- **Memory (guess):** the bank's state is larger than Dam's, maybe 10-15 MB, so 90 full keyframes for 15 minutes would be too much. Each keyframe stores **only the pages that differ from the previous keyframe**. The restore already compares page by page, so this matches its granularity. At a guessed 1-3 MB per 10 s, that is 100-300 MB for a long heist, with a cap that thins old keyframes to every 30 s.
- **Fast-forward cost (guess):** the sim tick is a few ms on a real CPU (the bench says the sim cost is small; GL dominates), so up to 600 sim-only ticks per seek is well under a second.

**First open:** the file plays at speed (×8 sim-only with a progress bar) to build the keyframes and the bookmark list. Alternatively, keyframes are built lazily as the viewer watches.

**What a seek must also reset (PRES):**
- all audio voices (the engine is outside the snapshot; `port_simsnd.c`'s tokens are in it);
- the renderer caches (`gfx_invalidate_ranges` already happens on restore);
- the AR bubbles and rings, the INTEL view, and the noise-history ring (cleared, then refilled as you play).

A restore needs no gfx task in flight (`port_snap.c` header: boss.c waits for `pendingGfx == 0`).

**Controls:** pause, ×0.25 / ×0.5 / ×1 / ×2 / ×4, a step of one tick while paused, ±5 s, and next / previous bookmark. A timeline bar shows the bookmarks as ticks.

## 6. Sharing

A replay is one file. Sharing means sending it, for example on Discord. Nothing is uploaded and there is no service.
- On open, a mismatching `B` says which build made it ("made with build xyz; you have abc"). Ghosts and bookmarks still load.
- On write, test-only env knobs (`GE_COOP_KILL`, `GE_LUA_TEST`, ...) mark the replay as "test". It still plays.
- The profile name is the only personal data.

## 7. Determinism risks and desync detection

**Detection.**
- The `H` line every 30 ticks is compared on playback (the hash and its 12 fields).
- The solo stream also keeps inputrec's per-tick `T` drift check.
- At the first mismatch, the replay pauses with a banner: "Diverged at 04:12.3: fields chrai, chrs". It offers "keep watching (non-canonical)". The log gets the fields (`fpDiffNames`), and `GE_COOP_STATEDUMP`-style dumps of that tick are available for a bug report.

**Known risks and their guards:**

| Risk | Guard |
|---|---|
| Wall-clock reads in the sim (latched presses, polls per tick) | `portInputRecTickClock` is forced on while recording; one poll per tick under `GE_FIXEDTICK` (`pcsched.c`). **verify:** a normal, non-fixed-tick human session records correctly. Today's recordings are made from harness runs |
| Lua `math.random` seeded from the clock in solo without `GE_SEED` (`port_lua.c`, section 11 item 2) | Record **the seed actually used** in `G`, and seed from it on playback |
| Ini values the sim reads | `K` (sim-config) and `O` (settings blocks) are checked |
| Presentation leaking into the sim (the CCTV feed's RNG and pool lessons) | The replay cameras use the feed's isolation (RNG save/restore, its own pool corner). The proof test in section 10 |
| The audio clock | `port_simsnd.c` (`GE_SIMSND=1` must be on when recording) |
| Inputs lost in ticks with no poll (`inputrec: LOST`) | Under fixed-tick this cannot happen. Otherwise it is logged, and a replay with LOST lines is marked "may diverge" |
| Float differences across CPUs or compilers | The same build only (`B`). Cross-machine replay is the same claim netplay makes (**verify** with one replay made on AC's PC and played in the container) |
| Stage start drift (intro taps) | Recording starts at stage tick 1; `GE_SKIPINTRO` already counts sim ticks |
| Mods or scripts edited after recording | the lua script-set hash in `G` refuses |

## 8. Bookmarks and the killcam

Bookmarks come from **sim events**, so they are the same on every playback. They are written into `J` lines as they happen, and a fresh scan of an old file rebuilds them.

| Kind | Source | Default camera on jump |
|---|---|---|
| **Spotted** | `PORT_EV_COVER_BLOWN` (chr, why) | that guard's POV, from 5 s before |
| Camera report | `PORT_EV_CCTV_SPOTTED` (cam, p), `PORT_EV_COVER_REPORT` | that camera |
| Alarm | `PORT_EV_ALARM` | follow the raiser |
| Body found | `PORT_EV_BODY_FOUND` | the finder's POV |
| Knockout, civilian down, death | `PORT_EV_CHR_KNOCKED_OUT`, `PORT_EV_CIVILIAN_KILLED`, `PORT_EV_BOND_DIED` | follow Bond |
| Objective, vault open, loot | `PORT_EV_OBJECTIVE_CHANGED`, the heist's phase in `mode.state` | follow Bond |
| Crew ping (`coop-heist-roles.md`) | `PORT_EV_CREW_PING` | follow Bond |

**"The moment you were spotted" (the killcam).**
1. On a Spotted bookmark at tick T, find the **lead-up**: the first tick before T where that guard's awareness left 0. A PRES sampler records `portAiAwareness` per guard every 4 ticks during the first pass.
2. Seek there (keyframe + fast-forward) and play at ×0.5 from his POV with the overlay.
3. The banner reads "Guard 41 · started noticing you at 03:58 (heard: door) · saw through you at 04:03 (act: lockpicking)".

The debrief screen (in flight on another branch) gets a "Watch how you were spotted" button per blown cover.

## 9. Ghost runs

A ghost is your best run's Bond, drawn as a translucent figure next to you.
- **Data:** the `Y` track (position and facing every 6 ticks, interpolated). No sim needed, so ghosts stay valid across builds while `L` (the layout and randomiser seed) matches.
- **Draw:** an impostor through the crowd atlas path (`port_crowd.c`; section 5 calls it "no chr, no prop, its own RNG and clock"). PRES, never a chr, so the guards can't see it.
- **Splits:** the `J` objective bookmarks give split times ("GREEN card -0:12 vs best").
- **Which run counts as "best"** comes from the debrief records (in flight): the fastest clean run per layout and difficulty (**guess** at that interface: the debrief stores a path to the replay file per record).

## 10. Test plan

- **Logic tests** (`tests/mrep_test.c` in `run_logic_tests.sh`): the header parse and write round-trip, the version and mismatch refusals naming the right key, lenient mode, the bookmark and ghost chunk parse.
- **Round trip, solo:** record `tests/heist/heist_determinism.lua` with `GE_HASHLOG`, play the `.mrep`, and the hash log must be identical tick for tick. Also the heist bot: record a `tools/bot/heist_bot.lua` run, play it without the bot (`GE_PLAYERBOT` off), and the same `RESULT` line and no `DRIFT` (as `tests/bot/replay.sh`).
- **Round trip, lockstep:** a netplay host/guest pair writes one `.mrep`, which plays offline from either view with identical hashes (extends `net_replay_log.sh`).
- **Presentation proof:** the same playback four ways: follow Bond, free cam flown by `GE_JOYPORT_AUTO`, guard POV cycling every 2 s with the overlay, and INTEL open. All hash logs must be identical (as `run_ar.sh proof`).
- **Seek:** seeking to T against playing straight to T must give the same hash at T and for the next 300 ticks (like `GE_SNAPSOLOTEST`), at 20 random T per run. The run is also repeated after seeking backwards.
- **Desync detector:** a forced extra RNG draw at tick N during playback (a replay twin of `GE_NET_TEST_EXTRADRAW`) must stop at the first `H` at or after N with field `rng`.
- **Old-file guard:** a replay from the previous build is refused by name, and its ghost still loads.
- **Regression:** `dam1_check.sh` stays IDENTICAL, and the heist suites keep their counts.
- **Disk:** the replay keep-count is honoured, and no screenshots are taken in automated runs except two (the POV overlay and the timeline).

## 11. Phased delivery

1. **P1, the slice (§12).**
2. **P2, the format and auto-record:** `.mrep` (the header, `H`, `J`, `Y`), solo auto-record on stage tick 1, `GE_REPLAY` playback, the round-trip and desync tests.
3. **P3, the cameras:** generalise the CCTV pass to a PRES camera (free, follow, POV), the AR projecting through the pass camera, guard cycling, the presentation proof.
4. **P4, the overlay:** awareness, cover why, cone, the noise-history listener, the lead-up sampler, the killcam jump.
5. **P5, scrubbing:** keyframes with page deltas, seek, speeds, the timeline UI, the seek tests.
6. **P6:** lockstep `.mrep` from netplay, ghosts and splits (after debrief lands), the front-end Replays list, sharing polish.

## 12. Smallest fun slice (a day)

"Watch your last heist through the guard who caught you." It needs no new format.

1. In heist mode, set `GE_INPUTREC` by default to `Data/Replays/last.rec` (solo). Prepend `#` header lines (seed, stage, settings). `inputrec.c` already skips unknown lines.
2. Play back with `GE_INPUTPLAY=last.rec` and a console command `replay pov <chr>` / `replay pov next`. It reuses `portCctvPassBegin` with the eye taken from the chr's head and `portAiFace` instead of a `CCTVRecord`, a small branch in `port_cctvview.c`.
3. Turn on the F3 diagnostics (`diag on`), which already draw the awareness, cone and hearing ring. They are slightly off-camera until P3, which is fine for a day.
4. Add a console line on `cover_blown`: "spotted by chr N at tick T (why)". Then `replay pov N` and watch.

There is no scrubbing: restart the replay to see the moment again. That is enough to learn whether "seeing it from his side" is fun.

## 13. Open questions for AC

1. Should every heist record automatically (about 1 MB each after compression, the last 20 kept), or only when you turn it on? Is C:'s free space a worry?
2. Is a refusal on a different build OK (with ghosts still working), or do you want "watch anyway until it diverges" as the default?
3. Guard POV: show the plain facts (his cone, his meter, the "why" text), or a dramatised killcam (slow-mo, a red flash at the moment)?
4. Should ghosts be your own best only, or also a friend's file (racing a shared replay)?
5. Should the debrief branch own "best run" and store the replay path, or should replays keep their own index?
6. Should replays include netplay sessions from the start, or solo first?

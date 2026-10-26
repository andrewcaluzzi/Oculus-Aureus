# Tooling and testing

## Building (Docker, through WSL on Windows)

The build runs in the repository's Docker image (`goldeneye`: Ubuntu 24.04, gcc-multilib, mingw-w64 i686, 32-bit SDL2).
Upstream's `readme.md` has the full steps; these are the forms that work from Windows PowerShell, one command per call
(example paths are AC's):

```powershell
wsl -d Ubuntu -- docker run --rm -v /mnt/c/workspace/project-midas:/home/dev/project goldeneye cmake --build build_win -j 12
wsl -d Ubuntu -- docker run --rm -v /mnt/c/workspace/project-midas:/home/dev/project goldeneye cmake --build build_pc -j 12
```

- `docker` is not on the Windows PATH: run it through `wsl -d Ubuntu -- docker ...`.
- **PowerShell 5.1 splits `-DFOO=a.b` at the dot** when passing it to a native exe. Quote every `"-D..."` argument. If a
  configure fails, delete the half-made build dir first.
- **Git Bash mangles `-v /mnt/c/...:/home/...`** volume arguments: run docker from PowerShell.
- New `.c` files in `src/game`, `src/` or `port/src` need no re-configure (`CONFIGURE_DEPENDS` globs).
- Configure Windows with `"-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake"`, Linux with `"-DSDL2_LINUX_ROOT=..."`.
  `CMAKE_BUILD_TYPE` does not change the optimisation: everything is `-O2` ([engine-overview.md](engine-overview.md)).
- `GE_ROMDATA_FALLBACK=ON` compiles Rare's data in (needed only to test edited setup `.c` files, with `GE_ROMSETUP=0`).
- **Mode scripts are copied only on relink** ([lua-modding.md](lua-modding.md) "Gotchas").
- Linking while `ProjectMidas.exe` is running (locked): rewrite `CMakeFiles/ge.dir/link.txt` to `_next` names, run
  `cmake -E cmake_link_script` in the build dir, then `scripts/port/snap_embed.py --soft --exe ..._next.exe --map ..._next.map`.
- Do not rebuild a build directory a running batch is using: batch runs should use a copy of the binary and `Data/`.

## Running

- The Windows exe is a GUI-subsystem program: launched from Explorer its log goes to `Data/Logs/ge_direct.log`. Run it
  from Git Bash with a pipe to see stdout (a PowerShell `Start-Process` redirect lost lines).
- A locked workstation stops the Windows exe (`No displays available`). Use the container recipe below.
- The Linux build cannot run in plain WSL (no i386 SDL2/libGL); it runs inside the build container.

### Headless in the container (Xvfb)

```powershell
wsl -d Ubuntu -- docker run -d --name midas-test --user root -v /mnt/c/workspace/project-midas:/home/dev/project goldeneye sleep 14400
wsl -d Ubuntu -- docker exec midas-test apt-get update -qq
wsl -d Ubuntu -- docker exec midas-test apt-get install -y -qq xvfb libegl1:i386
wsl -d Ubuntu -- docker exec -d midas-test Xvfb :99 -screen 0 1280x720x24 +extension GLX
wsl -d Ubuntu -- docker exec --user 1000 -e DISPLAY=:99 -e GE_STAGE=dam -e GE_SKIPINTRO=1 -e GE_FRAMES=300 midas-test sh -c "/home/dev/project/build_pc/ProjectMidas > /home/dev/project/build_tests/run.log 2>&1"
wsl -d Ubuntu -- docker rm -f midas-test
```

- Run as `--user 1000` so repository files are not root-owned. Put a copy of the ROM in `build_pc/Data/`.
- The container cannot `ptrace`-attach, but gdb can launch the game: `tests/bot/gdbhang.sh` (bot branch) interrupts a
  run after N seconds and prints every thread's stack.
- For screenshots set `GE_FULLHEIGHT=0 GE_EDGEINSET=1` ([rendering.md](rendering.md)).
- Do not run tests that open windows on a machine someone is playing on.

### Debug keys and the diagnostics overlay (`port/src/port_arhud.c`, designs/bank-heist.md section 20)

| Key / command | What |
| --- | --- |
| **F3** / `diag [on\|off]` | the diagnostics layer: per chr number, action, AI list, awareness, suspicion, sight cone, hearing ring, go-to line; noise rings; door key and rule labels; camera timers; a panel with tick, state hash, FPS / frame ms, chr count, alarm / power / cover, your room and zone; the radar |
| **Shift+F3** / `ar [on\|off]` | the AR specs (on by default in the heist) |
| `ar layers`, `ar <layer> on\|off` | suspicion, speech, keys, points, doors, cameras, radar |
| `diag freeze [on\|off]` | stops every AI list (changes the sim: solo only, refused in co-op / netplay) |
| `GE_DIAG=1`, `GE_AR_OFF=1` | the heist starts with the diagnostics on / without the AR wiring at all (the presentation proof) |
| `GE_CONCMD="diag on"` | any console command at the first frame (headless screenshots) |

## Test harnesses

| What | Where | Notes |
| --- | --- | --- |
| Demo oracle (dam1 453 lines, facility1 419) | `tests/baseline/`, `tests/demo_diag.ps1` | run after any shared-code change; use Xvfb on `:99`, not `xvfb-run` ([determinism-and-netplay.md](determinism-and-netplay.md)) |
| Pure-logic C unit tests | `tests/*_test.c`, `tests/run_logic_tests.sh` | bot logic, events, mod parser, co-op rules, net messages, lockstep, state hash; needs `gcc -m32`, so run in the image |
| Lua scenario scripts | `tests/lua/` | `GE_LUA_TEST=<file>`, `tests/solo_modes.sh` |
| Heist suites | `tests/heist/run.sh` (bindings), `run_guards.sh`, `run_ko.sh`, `run_winpath.sh` (win path, trapped, civilian fail), `run_cctv.sh`, `run_determinism.sh`, `run_shots.sh` | Bunker 2, Xvfb `:99`. Last counts: bindings 38, guards 34, ko 26, winpath 50 / trapped 16 / fail 5, cctv 40, determinism 12/12 |
| AR specs and diagnostics | `tests/heist/run_ar.sh [api\|proof]` | `api`: the `ar.*` / `diag.*` API and the heist wiring (13 checks; `GE_AR_SHOTS=1` for the showcase shots); `proof`: AR off vs AR + diag on, state hashes identical |
| Crosshair styles | `tests/crosshair/run.sh [api\|proof]` | `api`: the `crosshair.*` API, the pack and the draw hook as a mode on Dam (16 checks; `GE_XHAIR_SHOTS=1` takes one shot per pack style, `sheet.py` tiles them); `proof`: no style vs the `tactical` style + a hook, state hashes identical (3473 ticks) |
| Generated level | `tests/levelgen/run.sh` | culling, walking through doorways, guard routing |
| Arena presets and soaks | `tests/arena_a5/`, `tests/arena_a6/`, `tests/arena_crowd/` | copy a preset into `Data/Scripts/modes/`; one preset per scenario (Lua has no `os`) |
| Co-op / netplay / rollback | `tests/coop_determinism.sh`, `coop_drops.sh`, `net_lockstep.sh` (phases), `net_soak.sh`, `net_replay*.sh`, `snap_resim.sh`, `simonly_solo.sh` | each script's header says how to run it; some need recorded input files the generators write |
| Player bot (bot branch) | `tests/bot/level.sh <stage> <diff>`, `campaign.sh`, `heist.sh`, `replay.sh`, `dev.sh` | every run is recorded with `GE_INPUTREC` and replayed without the bot; the result line has `replay same` when it matched |

The player bot's speed settings (bot branch): `GE_CLOCKSCALE=20 GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_SIMONLY_TEST=2,59,60`
plays Dam's 210 s mission in about 12 s; record + replay 25-35 s.

## `GE_*` environment variables worth knowing

There are hundreds; `port/src/port_console_env.h` lists them and the console's `harness` command shows which are set.

| Variable | Effect |
| --- | --- |
| `GE_STAGE=<name>`, `GE_DIFF=<0-3>` | jump straight into a stage (`dam`, `bunker2`, `colosseum`, `flatland`, `levelgen`, ...) and difficulty |
| `GE_SKIPINTRO=1` | tap through the intro (counts sim ticks since the fork's fix) |
| `GE_FRAMES=<n>` | run n pumps and exit 0; suppresses message boxes |
| `GE_TICKHZ=<hz>` | sim rate; 60 is vanilla-identical |
| `GE_FIXEDTICK=1` | exactly one field per tick: required for repeatable runs |
| `GE_NOSLEEP=1` | no frame pacing |
| `GE_SEED=<n>` | seed the RNG (and, in the fork, a solo mode's Lua `math.random`) |
| `GE_NOINISAVE=1`, `GE_WINDOW=WxH` | no ini/profile writes (either turns off all writes) |
| `GE_DEMO=<name>`, `GE_DEMODIAG=1` | play an attract demo; log per-tick fingerprints |
| `GE_INPUTREC=<f>`, `GE_INPUTPLAY=<f>` | record / replay pad 0 (same stage, seed, spawn and tickhz) |
| `GE_SHOT=<n>` | a BMP every n frames into the working directory |
| `GE_FULLHEIGHT=0`, `GE_EDGEINSET=1` | vanilla viewport and edge inset (needed for Xvfb screenshots) |
| `GE_CHEATS=invuln,guns,...`, `GE_MODS=name=value,...` | cheats and modifiers at boot |
| `GE_SPAWNXYZ=x,y,z,heading` | move Bond's start |
| `GE_CONCMD="a;b"` | console commands at boot (before the stage loads) |
| `GE_SNDSCALE=<1-4>` | audio pool scale (default 2) |
| `GE_AIDBG`, `GE_LOSDBG=1` | AI debug output; Bond's sight checks (logs `TILE MISMATCH`) |
| `GE_ROMSETUP=0` etc. | keep a data class compiled in (FALLBACK builds only); `GE_ROMDBG=1` logs ROM loads |
| `GE_TEST_ALLOBJ=1` | every objective reads complete (test a level's success exit) |
| **fork** `GE_MODE=<name>`, `GE_MODE_SLOTS=<n>`, `GE_LUA_TEST=<file>` | run a mode; extra chr slots; run a script |
| **fork** `GE_COLOSSEUM_SLOTS=<n>` | Colosseum chr/anim slots (240..4000) |
| **fork** `GE_HASHLOG=<f>`, `GE_HASHLOG_DUMP=<a>-<b>`, `GE_NET_RNGTRACE=1` | per-tick state hash, world dump, RNG trace |
| **fork** `GE_SIMONLY_TEST=<start>,<n>[,<every>]`, `GE_SIMONLY_STATS=1` | sim-only ticks and their cost |
| **fork** `GE_COOP=1`, `GE_COOP_VIEW=1`, `GE_SNDLOG=1`, `GE_HUDLOG=1` | local co-op; play as Bond 2; audio / HUD logs |
| **bot** `GE_PLAYERBOT=1`, `GE_CLOCKSCALE=<n>`, `GE_HEIST_TUNE1..5` | the player bot; run the N64 clock n times faster; easier heists |

## Disk space: the lesson

Each bot heist run wrote a BMP every 30 s (about 20 MB a run). A 100-run ladder plus older rounds filled the C: drive to
0 bytes free; the WSL distro's disk image then threw I/O errors, Docker hung, and WSL had to be restarted by hand
(`wsl --shutdown`). Since then:

- batch runs set `NOSHOTS=1` (the bot's `level.sh`, `campaign.sh`, `heist.sh` delete the run's BMPs);
- keep only a few screenshots and convert them to PNG (`tests/bmp2png.py`, `tests/bot/shots2png.sh`);
- send batch output to another disk (the bot's `BOTOUT` / `OUT`, mounted with `-v /mnt/e/...:/scratch`);
- check free space before a long ladder.

## Other tooling

- `tools/bot/objectives.py <setup.c>` (bot branch): objectives, tags, and the AI lines that set each completion flag,
  with flags and pads un-swapped.
- `scripts/port/ai_lint.py`, `rawword_lint.py`, `romtable_lint.py`: AI list, setup word and ROM table checks.
- `tests/bmp_sheet.py`, `bmp_stat.py`: contact sheets and comparisons of screenshots.
- The Bunker 2 probe mode (`GE_MODE=probe GE_STAGE=bunker2`, `tools/mode_probe.lua`) logs every door, key, camera,
  monitor, safe and pad with its room.

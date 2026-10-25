# Tests

Most of these were written to check the fork's own work. Run them from the repository root; anything that starts the game runs inside the Docker image (see `README-FORK.md`) against `build_pc`, with a virtual X server (Xvfb) for the window. Do not run them on a machine where you are playing: they open windows.

| What | Where | How |
| --- | --- | --- |
| Demo oracle (the stock Dam attract demo must reproduce exactly) | `baseline/dam1.a.txt`, `baseline/facility1.a.txt`, `demo_diag.ps1` | `GE_DEMO=dam1 GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 ./ProjectMidas \| grep '^ge: demo tick'` and diff with the baseline. See `baseline/README.md`. |
| Pure-logic C unit tests (bot, events, mod parser, co-op rules, net messages, lockstep, state hash, net stats) | `*_test.c` | `sh tests/run_logic_tests.sh` (needs `gcc -m32`, so run it in the image) |
| Lua scenario scripts | `lua/` | Run with `GE_LUA_TEST=<file>` (see the headers of the files and `solo_modes.sh`). |
| Arena presets and soaks | `arena_a5/`, `arena_a6/` | Copy a preset into `Data/Scripts/modes/` and run `run.sh` / `runpath.sh` in the container. |
| The generated level (rooms, portal culling, walking and guard routing through doorways) | `levelgen/` | `sh tests/levelgen/run.sh` in the container (see `modding/levelgen.md`). |
| Experimental co-op, netplay and rollback harnesses | `coop_*.sh`, `net_*.sh`, `snap_*`, `simonly_solo.sh`, `net/*.awk` | Each script's header says how to run it. Several expect recorded input files (`net/recA.txt`, `recB.txt`, `recEA.txt` and so on) that are not shipped; the `net/*.awk` and `net_soak_gen.py` generators write the synthetic ones. |
| Screenshot helpers | `bmp2png.py`, `bmp_sheet.py`, `bmp_stat.py` | Convert and compare the BMPs `GE_SHOT=<n>` writes. |

The baselines are the `ge: demo tick` lines of the unmodified simulation (state fingerprints: positions, RNG, counters), taken at upstream commit `6be5920`. They contain no game data.

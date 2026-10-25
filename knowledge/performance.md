# Performance

The stock game was written for a few dozen chrs. The fork's arena (1000+ spectators, hundreds of corpses) and heist
(100+ guards) found where it breaks. Remember that **every build is `-O2`** (even `CMAKE_BUILD_TYPE=Debug`,
[engine-overview.md](engine-overview.md)), so numbers from any build are real; but GL numbers under Xvfb/llvmpipe are
software rendering and mean nothing. Sim numbers do.

## `roomGetProps` was quadratic (fixed in the fork; upstream PR #83)

`roomGetProps` (`src/game/chrprop.c` ~3129) fills a list with the props in a set of rooms. It removed duplicates by
scanning the list it was building, so it was O(props^2) per call. Every chr's per-frame move test
(`chrUpdateAnim` > `subcalcpos` > `sub_GAME_7F01F614` > `stanTestLineUnobstructed` / `stanTestVolume`) calls it twice,
so a frame cost O(chrs x props^2). In the Colosseum (one room, ~1300 props) it was 48% of all samples (`perf record
-g` on the Linux build, benchmark phase D: crowd 1000 + 250 corpses, sim 259 ms a frame).

Fix: a mark per propnum (`static u8 sSeen[MAX_PROPS]`), cleared again before return. It returns the same list in the
same order, so no sim state changes and the demo oracle is identical. It applies everywhere. Proposed upstream as
murkantor/Project-Midas#83 ("make roomGetProps linear and bound its prop list"; open at the time of writing).

## Dormant chrs (fork, arena only)

After the fix the cost was every chr running its move test every frame, standing still or not, and kept corpses still
running their guard list (`IFISeeBond` every tick, 7% of the main thread). `portArenaChrDormant`
(`port/src/port_arenamode.c`; false outside a mode and for chrnums below 8000) marks spectators and dead arena enemies:

- the move test checks only the level, not props (`chr.c` ~1305, cdtypes 0 instead of 0x1f), and is skipped when the
  chr has not moved in x/z;
- a dormant chr in `ACT_DEAD` runs no AI list (`chraction.c` ~10223);
- spectators get `CHRFLAG_00010000` (the stock "no collision box" flag), so live chrs' route tests stop walking 1000
  spectators' collision diamonds (`chrUpdateCollisionBounds`, `doSegmentsIntersect`).

Trade-offs: nobody can collide with spectators (knives and grenades pass through them; bullets still hit, because hits
use the model hit entries); corpses do not collide with props while they lie still (a dead chr had no collision box
anyway).

Linux sim ms per frame, the arena benchmark, before -> after both fixes (commit cb22801):

| Phase | Content | Before | After |
| --- | --- | --- | --- |
| A | crowd 100 | 0.45 | 0.30 |
| B | crowd 100 + 250 corpses | 20.8 | 0.78 |
| C | crowd 300 + 250 corpses | 43.0 | 0.93 |
| D | crowd 1000 + 250 corpses | 241 | 1.22 |
| E | D + 18 live enemies | 307 | 3.49 |

Spectators still animate every frame; animation was not a cost.

## How many live enemies

- Live-enemy benchmark (`Arena Benchmark (Live).bat`, `arena-bench-live`): about **75 live, fighting chrs are free at
  144 fps**, about **180 before the frame drops under 60** (AC's machine, i7-8700K + RTX 2080). The arena raised its
  live cap per wave to 16, +1 every 2 waves, max 50.
- Most heist guards are idle on post, which is cheap.
- Real spectators (frame cost on the same machine, 60 Hz sim, 4K window, Bond looking at the stands; measured at arena
  drop A3, before the dormant-chr fix): none 1.5 ms of
  work, 60 about 6 ms, 120 about 8.5 ms, 200 about 10-11 ms. The impostor crowd behind them is one GL draw.
- Co-op sim step (sim-only ticks, Windows, two Bonds): Dam 0.30 ms average, 1.76 ms max; Facility 0.25 / 1.60. The
  dam1 demo's sim step alone is 0.06 ms.

## Stock limits that cost or crash at scale

| Limit | Where | Note |
| --- | --- | --- |
| ModelHitEntries 600 | `objecthandler.c`, `initunk_005450.c` | each drawn chr takes ~15-25; when it runs dry chrs draw partly, vanish, then crash `chrTestHit`. Fork: 32000 |
| Props 600, on-screen list 500, prop lookup 512, room prop blocks 256 | `chrai.h`, `chrprop.c` | fork: 2400 / 2500 / 2600 / 512 (the lookup was written unchecked) |
| Weapon slots `g_WeaponSlots[30]` (held + dropped) | `propobj.c` `weaponCreate` | recycles the oldest loose weapon; the fork never recycles one within 350 units of a Bond in a mode, and heist modes get a port pool of 320 weapon records once the stock 30 are held (guard 31+ was unarmed) |
| "10 guards saw Bond recently" path cap | `chraction.c` (`PORT_SEEN_BOND_CAP` ~100) | lifted in a fork mode; otherwise big fights freeze routing |
| Chr slots = setup guards + 10 | `initguards.c` | fork reserves more per stage ([guards-and-ai.md](guards-and-ai.md)) |
| Frame pools (display list, vertices) | `dyn.c`, Colosseum | fork grows them 1536 B per slot above 240 |
| Stock dead-chr fade 1.5 s | `chrlvTickDead` | fine for slots; the arena can keep every body (`corpse_keep = -1`, valve `corpse_limit` 400) |

## Measuring

- `Arena Benchmark.bat` / `Arena Benchmark (Live).bat` write `Data/Logs/arena_bench.txt`: frame time percentiles,
  render/sim split, draw calls, chr/prop/hit-entry pool use per phase ([`modding/arena.md`](../modding/arena.md)
  "The benchmark").
- `GE_SIMONLY_STATS=1` logs the sim-only tick cost by section; `GE_SHOWFPS`, `GE_HITCHMS` (hitch log threshold).
- `perf record -g` on the Linux build inside the container works for profiling the sim.

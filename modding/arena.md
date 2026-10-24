# The Colosseum arena

A wave-survival mode in a generated gladiator pit. The map is a C data set built by `tools/gen_colosseum.py`; the mode is one Lua file, `tools/mode_arena.lua` (the build ships it as `Data/Scripts/modes/arena.lua`). Everything about balance is in the settings block at the top of that file.

## Launching

| Launcher | What it does |
| --- | --- |
| `Play Arena.bat` | `GE_MODE=arena`: 20 waves, a boss every 5, then the mission is won. |
| `Play Arena (Endless).bat` | `GE_MODE=arena-endless`: waves keep coming and keep getting harder; your wave, kills and time are logged when you fall (`ge: arena: OVER wave ...`). |

| `Play Arena (Test Keys).bat` | `GE_MODE=arena` with the wave-jump test keys (below) and `GE_CHEATS=invuln,guns`. |

All set `GE_STAGE=colosseum GE_SKIPINTRO=1` too. To make your own variant, copy `Data/Scripts/modes/arena-endless.lua` (three lines: it sets `ARENA_SETTINGS`, then `include("arena")`) and run it with `GE_MODE=<your file name>`. For example `ARENA_SETTINGS = { waves = 10, boss_every = 3, crowd = 30 }`. Add `GE_CHEATS=invuln` to watch without dying.

## The map

An octagonal pit 9000 units across with a 350-unit wall, four gates (E, N, W, S) and 12 tiers of stands (up to about 3000 units high). Four pillars stand in front of the gates and four low walls form a pinwheel in the diagonals; enemies come in through the gates. 256 spawn pads are on a 500-unit grid, and 1200 seat pads on the stands hold the crowd. The level takes over the cut multiplayer stage slot `LEVELID_EAR` (stage name `colosseum` or `ear`), borrows the Surface text bank and uses a fogless sky. It draws with two stock Surface textures tinted per vertex, so close up it looks soft. The generator constants (`HALF`, `CUT`, `TIERS`, `PILLARS`, `LOWWALLS`, colours) are at the top of `tools/gen_colosseum.py`; `python3 tools/gen_colosseum.py` rewrites `port/colosseum/`.

## How a wave runs

GET READY (`start_delay`), then wave 1. Enemies pour out of the gates two per quarter second, up to `max_alive` at once (the rest of the wave trickles in as they die). A wave is cleared when the queue is empty and none are alive. Then comes a breather (`breather` seconds) and the next wave. Corpses vanish 4 seconds after death to keep chr slots free. The HUD shows `WAVE 4/20  THE KNIFE SISTERS` on line 1, kills / enemies left / time on line 2 and announcements on line 3. In fixed mode the last wave ends with `mission.win`; if Bond dies, the stock mission-failed screen takes over.

Enemies fight from range, not just in melee: they open fire as soon as they can see you within their weapon's effective range (rifles 6000 units, SMGs and shotguns 3500, pistols 2500) and run straight at you across the open floor when out of range.

### Wave types

Ordinary waves cycle through these seven in order (wave 1 is always soldiers). A repeated type gets a numeral: THE MARKSMEN II, THE AMBUSH III.

| Wave | What you get |
| --- | --- |
| The Soldiers | Ten soldier skins with AK47, D5K, AR33 or ZMG, chasing; accuracy rises with the wave. |
| The Knife Sisters | Natalya, Xenia, Mayday, Rosika, Sally, Mandy and Vivien with PP7s. Every shot while attacking becomes a thrown knife, from up to 4500 units, with an arc that is solved properly at range. From wave 14 this becomes **The Turbo Knife Sisters**: everyone animated at 1.6 to 1.8 times speed. |
| The Horde | Eight or more weak, fast runners (35% health): civilians, scientists, terrorists, bikers with Klobbs, PP7s and ZMGs. |
| The Ambush | Rushers from the two gates farthest from you. |
| The Marksmen | Two to six snipers parked in the gate mouths, plus a few soldiers. |
| Wild Card | Any of the 70 skins, any of 16 weapons, 0.6 to 3 times health, random accuracy and speed, one in five throws knives. |
| Iron Jaws | Jaws with an RC-P90, seven times health (more by wave), plus escorts. |

### Bosses

Every `boss_every` waves (5) a boss wave replaces the ordinary one. The four bosses cycle, each with a few escorts (3 to 6 elite troops):

1. **The Baron**: Baron Samedi with the Golden Gun (ten times base health).
2. **The Traitor**: Trevelyan with a rocket launcher (12 times).
3. **Iron Jaws**: Jaws as a mega-tank (26 times).
4. **The Triumvirate**: all three at once.

Boss health is multiplied by `boss_hp_mult` (2.5) and given armour worth `boss_armour` (0.5) of their health on top, and each later cycle is `boss_cycle_mult` (0.35) tougher.

### Tiers, elites, turbo

- **Tiers.** Every enemy rolls a toughness tier from a table that shifts with the wave: light (0.6 times health), normal, armoured (1 times health plus armour, about 2.5 times as tough) and heavy (1.5 times health plus lots of armour, about 5 times). Armoured enemies are not staggered by hits.
- **Elites** from wave `elite_from` (8): the black Spetsnaz and Arctic Commandos from the Bunker 2 alarm and the Caverns' Janus Special Forces, always armoured or heavy, a growing share of soldiers and ambushers.
- **Turbo** from wave `turbo_from` (6): a growing share of enemies move, aim and throw at 1.25 to 1.6 times animation speed.

## The economy

- **No free guns.** Bond re-arms from the weapons enemies drop. (`give_guns = true` brings back a rotating gun gift each breather; `give_ammo = true` tops up the ammo of the guns he carries.) Loose guns on the floor are capped by `max_drops`.
- **Centre ammo box.** Every `ammo_every` seconds (40) one real MP ammo box with PP7, KF7 and shotgun rounds (`ammo_rounds`) appears on a random pad near the middle of the pit. At most `ammo_max_center` (3) lie uncollected (a new one replaces the oldest), and each disappears after `ammo_ttl` (90). It is an ammo box, not a gun: it never rotates through weapons. Note that solo play doubles box ammo.
- **Centre armour.** One body-armour pickup after every boss wave, and with `armour_chance` (0.25) after other waves, gone after `armour_ttl` (120 s).
- **Health per kill.** No health between waves; each kill heals the Bond who got it, by a fraction of a full bar set per tier in `health_per_kill` (3% light and normal, 4% armoured, 6% heavy, 25% for a boss).
- **The crowd.** `crowd` (100) harmless, invincible spectators fill the stands during GET READY. They fidget on their own timers and react: dance, laugh or raise their hands when a wave is cleared, look startled when a boss arrives. Each costs a little frame time; lower `crowd` on a weaker machine. Zero removes them.

## Settings

Set them by editing table `S` at the top of `arena.lua`, or in a preset file with `ARENA_SETTINGS = {...}` before `include("arena")`. The header comment of `tools/mode_arena.lua` has the authoritative list; the main ones:

| Setting | Default | Meaning |
| --- | --- | --- |
| `mode` | `"fixed"` | `"fixed"` (win after `waves`) or `"endless"` |
| `waves` / `boss_every` | 20 / 5 | last wave in fixed mode / a boss wave every N (0 = none) |
| `breather` / `start_delay` | 12 / 6 | seconds between waves / before wave 1 |
| `max_alive`, `max_alive_every`, `max_alive_cap` | 12, 6, 18 | enemies alive at once, growing by 1 every 6 waves up to the cap |
| `hp_growth` / `acc_growth` | 0.10 / 2.5 | enemy health and armour grow by this fraction per wave / accuracy points gained per wave |
| `crowd` | 100 | spectators |
| `supplies` | true | false turns off every pickup and gift between waves |
| `pickup_mode` | `"boss+random"` | health/armour gifts: `"boss"`, `"random"`, `"boss+random"`, `"every"`, `"none"` (the A6 economy turns the health gift off with `health_gifts = false` and replaces the armour gift with centre armour) |
| `ammo_every`, `ammo_max_center`, `ammo_ttl`, `ammo_rounds`, `centre_radius` | 40, 3, 90, 50/60/16, 800 | the centre ammo box |
| `armour_center`, `armour_chance`, `armour_amount`, `armour_ttl` | true, 0.25, 1.0, 120 | the centre armour |
| `health_per_kill` | by tier | fraction of a full bar per kill |
| `give_guns`, `give_ammo`, `ammo_frac`, `max_drops`, `drop_keep_radius` | false, false, 0.30, 20, 400 | gun drops and gifts |
| `knife_range`, `knife_cooldown`, `sight` | 4500, 1.2, 15000 | knife throwers' range and cooldown (seconds); how far every enemy sees (units) |
| `boss_hp_mult`, `boss_armour`, `boss_cycle_mult` | 2.5, 0.5, 0.35 | boss toughness |
| `elite_from`, `elite_share0`, `elite_share_inc`, `elite_share_max` | 8, 0.25, 0.05, 0.75 | elite troops |
| `turbo_from`, `turbo_share0`, `turbo_share_inc`, `turbo_share_max`, `turbo_lo`, `turbo_hi0`, `turbo_hi_inc`, `turbo_hi_max`, `turbo_knife_from`, `turbo_knife_lo`, `turbo_knife_hi` | see file | turbo enemies and the turbo knife wave |

Below `S` are the tables `TIERS` (health and armour multipliers), `TIER_MIX` (which tier mix applies from which wave), `ELITES` (the elite bodies and `ELITE_GUNS_*`), the skin pools and `TITLES`. Per-wave-type counts and base health are in the `mk_*` functions.

Test hooks: `first_wave = N` starts at wave N; `debug_track = true` logs every live enemy's distance to Bond and action every 2 seconds; `debug_clear_after = s` kills each enemy after s seconds. Enemy toughness and speed scale with the wave, so a long endless run gets very hard.

## Test keys

For testing, the wave can be jumped. They work when the setting `test_keys` is true or `GE_ARENA_TESTKEYS=1` is set, and never in co-op or netplay.

| Key | Console | Action |
| --- | --- | --- |
| PageDown / PageUp | `arena next` / `arena back` | remove the live enemies (no kills, drops or health) and start the next / previous wave now |
| Shift+PageDown / Shift+PageUp | `arena next+` / `arena back+` | start the next / previous wave now and leave the live enemies and the queued spawns alone |

The wave counter, titles, bosses and difficulty follow the wave you land on (it never goes below 1). In fixed mode, "next" past the last wave wins. A Lua mode can listen with `on("test_key", function(name) ... end)`; `name` is `"next"`, `"back"`, `"next+"` or `"back+"`.

## Big crowds and keeping every body

The stage reserves 600 chr slots by default (`GE_COLOSSEUM_SLOTS=<n>` overrides it, 240 to 4000; stage init runs before Lua, so it is an environment variable). That is the crowd of 100 + 400 kept corpses (`corpse_limit`) + up to 18 live enemies + the Bonds, with about 80 to spare because a removal lands a frame late. A 20-wave run (about 173 enemies, every body kept) needs only about 290. If slots run low anyway, the arena removes the oldest corpses first before a wave spawn, so a wave never waits for slots. The PC build's fixed pools are sized for about 1400 chrs: 2400 props, 32000 model hit entries, 2500 on-screen props. They cost memory (about 1 MB) in every stage and do not change behaviour below the old limits. Settings: `corpse_keep` (seconds before a corpse is removed; `-1`, the default, keeps every body, `4` is the stock fade-out), `corpse_limit` (when more are kept, the oldest go, 400), `crowd_rate` (spectators spawned per second, 200). The mode ticks four times a second, so that is 50 a tick.

## The benchmark

`Arena Benchmark.bat` (next to the exe) runs the preset `arena-bench` (`tools/mode_arena_bench.lua`) with `GE_COLOSSEUM_SLOTS=1400` and `GE_CHEATS=invuln`, and quits by itself after about 4 minutes. Five phases of 30 s steady state: crowd 100 / 100 / 300 / 1000 / 1000, 0 / 250 / 250 / 250 / 250 corpses (real kills, kept to the end) and, in the last, 18 live enemies shooting at Bond. Bond stands in the middle and turns at 24 degrees a second, alternating half turns looking at the stands and at the floor. `Data/Logs/arena_bench.txt` gets the machine lines, then per phase the frame time (avg, p50, p95, p99, max, fps; overall and for the stands and floor views), render/sim split and draw calls, spectator / corpse / enemy counts, and pool use (chr slots, props, hit entries with their peak, stage memory left). The Lua side is the `bench` table (`port/luavm/port_luavm_arena.c`).

`Arena Benchmark (Live).bat` runs the preset `arena-bench-live` (`tools/mode_arena_bench_live.lua`, which sets `BENCH_LIVE` and includes `arena-bench`) with `GE_COLOSSEUM_SLOTS=600`: no crowd, no corpses, and phases L0 (nobody, the baseline), L25, L50, L100 and L200 live soldiers (the arena `chase` behaviour) fighting the invulnerable Bond, who never shoots. The live count is topped up through each phase. Each phase block adds how many were attacking, moving or doing something else (sampled every 2 s) and a sim cost per live enemy, (sim avg - L0 sim avg) / live, in microseconds. Same frame, render and pool lines as above.

What it found (perf on the Linux build): 98% of the sim was every chr's per-frame move test (`chrUpdateAnim` > `sub_GAME_7F01F614` > `stanTestLineUnobstructed` / `stanTestVolume`), which gathers the room's props with `roomGetProps`. That function removed duplicates by scanning its own list, so with ~1300 props in the one big room it was quadratic, and it ran twice per chr per frame (48% self time in phase D). Now it marks each propnum instead, which gives the same list. Spectators and kept corpses test only against the level, not against props, and skip the test when they have not moved. Kept corpses run no AI list. Spectators have no collision box. Linux sim ms per frame, before and after: A 0.45 / 0.30, B 20.8 / 0.78, C 43.0 / 0.93, D 241 / 1.22, E 307 / 3.49. Spectators still animate every frame at full rate.

## Under the hood

The mode relies on the Lua spawning API (`spawn.chr`, `spawn.item`, `spawn.anim`, `bond.give`, `bond.heal`; see `lua.md`) and on C-side additions that are active only for arena enemies: AI lists with ranged fire and direct routes, a bigger model hit-entry pool, per-chr knife throwing, and a weapon pool that protects drops near Bond. The stock game, the attract demos and the rest of the port are not affected; the demo oracle in `tests/baseline/` verifies this.

Arena soak and scenario scripts are in `tests/arena_a5/` and `tests/arena_a6/`; copy a preset into `Data/Scripts/modes/` and run it with `GE_MODE=<preset>` (see the scripts).

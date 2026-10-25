# Lua modding: how it fits the engine

The API reference is [`modding/lua.md`](../modding/lua.md) (events, every table, the C side of adding a binding). This
page explains how the Lua layer and the mode system hook into the game, so you know what a script can and cannot
change. All of it is **fork-only**: upstream is writing its own Lua support, and scripts written here may not run there.

## Where scripts run

| Path | When |
| --- | --- |
| `Data/Scripts/boot/*.lua` | once, the first time Lua is needed (title screen); for `menu.add_*` |
| `Data/Scripts/*.lua` | every solo mission start, alphabetically, each under its own `pcall` |
| `Data/Scripts/modes/<name>.lua` | when `GE_MODE=<name>` or the Mods page's Game Mode row picks it, after the level reset |

- Lua 5.5.1, vendored unmodified (`port/lua`); the glue is `port/luavm/` and `port/src/port_lua*.c`.
- Scripts load at stage load, **before Bond exists**: register handlers with `on(...)` and set up in `stage_start`
  (the first tick Bond is in first person; in co-op it waits for every Bond).
- `tick` fires every 0.25 s of game time. Every listener is dropped at each stage load.
- A preset is a tiny script that sets a settings table and includes the real mode:
  `ARENA_SETTINGS = {...}; include("arena")` (same for `HEIST_SETTINGS`, `HOSTAGE_SETTINGS`). The mode's defaults stay
  untouched.
- `GE_LUA_TEST=<file>` runs a script from anywhere; the console has `lua <code>` and `lua_reload` (refused in netplay).

## What a mode changes in the engine

`GE_MODE` turns on the level reset (`port/src/port_mode.c`; hooks marked `PORT: mode`):

- `prop.c` skips the setup's guards (slots are still allocated, for spawning) and objectives; `deb_loadallmodels.c`
  skips the background AI chrs, so level scripts never run.
- Objects stay and keep working (doors, keyed locks, cameras that raise the alarm, safes, pickups).
- The UNLOCK DOORS cheat goes on at one second unless the script called `mode.keep_locks()`.
- The stock mission end is suppressed; `mission.win` / `mission.fail` end it (`portModeEndMission`).
- Not in demos, recording, multiplayer or the title stage (same predicate as the bot: one player, no ramrom).
- In a mode the "10 guards saw Bond" routing cap is lifted, and modes named `heist*` (or `GE_MODE_SLOTS`) reserve 250
  extra chr and anim slots.

## How events reach Lua

The C event bus (`port/src/port_event.c`, `port/include/port_event.h`) is emitted from small hooks in `src/`:

| Event | Emitted from |
| --- | --- |
| `chr_died`, `bond_died` | `port_blocks.c` death polling (per slot `chrIsDead`, `bonddead`), once per edge |
| `damage` | `record_damage_kills` (Bond, `bondview2.c` ~13789) and the chr damage path (`chraction.c` ~2736, ~2827) |
| `zone_enter` / `zone_leave` | `port_blocks.c` zones (XZ circles, +-250 in y) |
| `alarm` | `alarmActivate` / the alarm stop (`propobj.c` ~16026, ~16047) |
| `cctv_spotted` | the CCTV tick (`propobj.c` ~5596-5690, through `port_heist.c`) |
| `use`, `gadget_used` | `portHeistNoteUse` from the use-press site (`chrprop.c` ~1796, ~1823: also with nothing in reach) and `portHeistNoteGadget` from the gadget fire site (`gunfire.c` ~4915) |
| `item_pickup` | the pickup code (`propobj.c` ~11595, ~11819) |
| `objective_changed` | the objective status-change check (`objective_status.c` ~394) |
| `body_found`, KO events | the heist's chr hooks (`port_heist.c`) |

With no listener an emit is a bounds check and a return, which is why the hooks cost nothing in normal play and the
demo oracle stays identical. Kill credit: the engine records no attacker, so the port keeps its own ring
(`portModeNoteHit`, from `handles_shot_actors` and the faction hit note).

## Determinism rules for scripts

A mode script runs identically on every machine in co-op and netplay, and in solo replays:

- `math.random` is a deterministic generator re-seeded at each stage start from the session seed (solo: from `GE_SEED`
  when set, else the clock; `port_lua.c` `portLuaGameRandomSeed`).
- `table.sort` is a stable merge sort; string-key `pairs()` order is fixed. Do not iterate tables keyed by tables or
  functions, and iterate arrays by index (`for id = 1, #t`).
- No `io`, `os`, `debug`, `package`; no wall clock. A 16 MB allocator cap (a 20 MB fixed arena with a deterministic
  allocator, so it can be snapshotted for rollback) and a 50-million-instruction limit per call.
- Never read the local player or the window. Use explicit Bond indexes (`bond.pos(p)`, `CHR_BOND = -2` is player 0,
  -3 player 1).
- State that matters for the sim goes into the co-op state hash through `mode.state(text)` (the heist puts its phase,
  crack progress, jams, loot per Bond and waves there), and shows in desync dumps.
- In netplay the handshake hashes every script in `Data/Scripts`, `boot/` and `modes/`; both machines need the same
  files. (A 32-bit Linux `readdir` fails on a 64-bit-inode filesystem such as a Docker bind mount, which made the
  directory look empty; the fork builds those files with `_FILE_OFFSET_BITS=64`.)

## Gotchas

- **The build copies mode scripts only as a POST_BUILD step of the `ge` target** (`CMakeLists.txt` ~543-580). If
  nothing relinks, edited `tools/mode_*.lua` files are not copied and the game runs the old ones. Copy by hand or force
  a relink. A whole round of bot measurements ran stale heist Lua this way.
- `Data/Scripts/*.lua` also runs inside modes: a leftover weapon-tuning script changes a mode's balance.
- An error disables only that handler (logged once); errors are never fatal. Check `ge: lua: error:` in the log.
- Lua cannot see the window, so a script cannot know what is on screen. `bond.rooms_drawn(p)` exists for tests and is
  presentation, not sim state.

## Related

- The modes shipped: hostage rescue, arena (`modding/arena.md`), heist (`designs/bank-heist.md` sections 9-11),
  levelgen_test.
- The player bot's Lua `bot` library (`bot.push(line)` feeds it plan steps) is on the bot branch: `modding/bot.md`
  there.

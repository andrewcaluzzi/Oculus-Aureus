# Lua modding reference

Project Midas embeds **Lua 5.5.1** (vendored unmodified in `port/lua`). Scripts can change weapon stats, and a **mode** script can turn a mission into something else: wipe the level, spawn characters and pickups, watch events, draw HUD text and win or lose the mission.

> This is the fork's own API. It is not upstream's, and upstream is writing its own; scripts written here may not run there.

## Where scripts live and when they run

All paths are relative to the folder holding the exe (`build_win/`, `build_pc/`).

| Path | Runs |
| --- | --- |
| `Data/Scripts/boot/*.lua` | Once, the first time Lua is needed (the title screen at start-up). Use for `menu.add_*`. |
| `Data/Scripts/*.lua` | At every solo mission start, alphabetically, each under its own `pcall` (an error is logged and the next file still runs). |
| `Data/Scripts/modes/<name>.lua` | When `GE_MODE=<name>` (letters, digits, `_`, `-`), after the level reset and after the regular scripts. Also pickable in-game: Options > Gameplay > Mods > Game mode. |

The build copies `tools/scripts_example.lua` to `Data/Scripts/example.lua` (all commented out: a tour of the `weapon` table) and the shipped modes (`arena`, `arena-endless`, `hostage`, `hostage-flatland`) into `Data/Scripts/modes/`. Edit them in place or write your own.

A mode **removes the stage's guards, objectives and level AI scripts**; the level's **objects are kept** (doors, keyed locks, padlocks, cells, CCTV cameras, monitors, safes, props; confirmed by `probe.level()` on Bunker 2: all 38 door records and 6 cameras are live props), and the stock object code still runs (cameras raise the alarm after 300 ticks of unbroken sight). The one thing the reset adds is the UNLOCK DOORS cheat one second into the stage, so every door opens for Bond; call `mode.keep_locks()` at script load to prevent that (`GE_MODE=reset` is just the reset) and runs only in a single-player mission: not in demos, recording, multiplayer or the title stage. Scripts run at stage load, before Bond exists, so register handlers with `on(...)` and do the setup in `stage_start`.

Handy: pick the stage with `GE_STAGE=dam` (or `colosseum`, `flatland`, ...), skip the intro with `GE_SKIPINTRO=1`, run a script from anywhere with `GE_LUA_TEST=<file>`. The in-game console has `lua <code>` (an expression prints `=> value`) and `lua_reload`. Both are refused during netplay. Logs go to `ge: lua: ...`; `print` goes there too.

Errors are logged (`ge: lua: error: ...`) and never fatal. A handler that errors is disabled and logged once. Every listener is dropped at each stage load.

## Sandbox

Libraries: base, string, table, math, utf8, coroutine. There is no `io`, `os`, `debug` or `package`; `dofile`, `loadfile` and `collectgarbage` are nil. 16 MB allocator cap. A single top-level call that runs over 50 million instructions errors with "script ran too long".

For co-op and netplay, a mode script runs identically on every machine, so: `math.random` is a deterministic generator re-seeded at each stage start from the session seed (`math.randomseed()` with no argument is ignored in co-op); `table.sort` is a stable merge sort; string-key `pairs()` order is the same everywhere. Do not iterate tables keyed by tables or functions, do not use `tostring(table)` as data, and never read the local player or the window.

## A minimal mode

Save as `Data/Scripts/modes/minimal.lua` and run with `GE_MODE=minimal GE_STAGE=colosseum GE_SKIPINTRO=1`. Kill five guards that run at you; if Bond dies the game's own mission-failed screen takes over.

```lua
local TARGET = 5
local alive, killed = {}, 0

on("stage_start", function()
  local bx, _, bz = bond.pos()
  local ids = pads.far_from(bx, bz, 2500, 99999, true)  -- walkable pads at least 2500 units away
  for i = 1, TARGET do
    local c = spawn.chr(ids[math.random(#ids)],
      { weapon = "ar33", behaviour = "chase", health = 1.5 })
    if c then alive[c] = true end
  end
  hud.set(1, "Kill them all")
  hud.set(2, "0 / " .. TARGET)
end)

on("chr_died", function(victim, killer)
  if alive[victim] then
    alive[victim] = nil
    killed = killed + 1
    hud.set(2, killed .. " / " .. TARGET)
    if killed >= TARGET then mission.win("cleared") end
  end
end)
```

## Events: `on(event, fn)`

Registers a handler (at most 16 per event; an unknown event name raises).

| Event | Handler arguments |
| --- | --- |
| `stage_start` | `(stage)`: the first tick Bond is in play (waits for every Bond in co-op) |
| `tick` | `(dt)`: every 0.25 s, `dt` in seconds |
| `chr_died` | `(victim, killer)`: chr ids; `killer` is `CHR_BOND`, another chr, or `CHR_NONE` |
| `bond_died` | `(killer [, player])` |
| `zone_enter`, `zone_leave` | `(zone, chr)`: `chr` is `CHR_BOND` for Bond |
| `hostage_follow`, `hostage_wait` | `(chr [, player])` |
| `damage` | `(victim, attacker, amount, weapon)`: damage actually applied (float); weapon is an item id or -1 |
| `chr_spawned` | `(chrnum)`: chrs spawned by the port (the bot, mode scripts) |
| `item_pickup` | `(objtype, id, qty)`: `objtype` is the `PROPDEF_*` number (4 key, 7 single magazine, 8 weapon, 19 multi-ammo box, 20 armour) |
| `objective_changed` | `(index, status)`: 0 incomplete, 1 complete, 2 failed |
| `test_key` | `(name)`: a test hotkey, `"next"` (PageDown), `"back"` (PageUp), `"next+"`, `"back+"` (with Shift), or the console `arena <name>`. Never from the keyboard in co-op or netplay. |

Chr ids are chrnums. Globals: `CHR_BOND = -2` (player 0; `bond.id(p)` gives the id of other Bonds) and `CHR_NONE = -1`. `damage`, `chr_spawned`, `item_pickup` and `objective_changed` also fire on an ordinary solo stage, not only in a mode.

## Reference

### Mode and pads

| Call | Notes |
| --- | --- |
| `mode.name()` | The `GE_MODE` string, `""` outside a mode. |
| `mode.env_flag(name)` | True when the environment variable `name` (it must start with `GE_`) is set and not `0`. |
| `mode.keep_locks([on])` | Call at script top level (before the 1 s mark). Skips the level reset's UNLOCK DOORS cheat, so keyed doors and padlocks keep working. |
| `probe.level()`, `probe.pads()` | Log the setup records (doors with key flags, padlocks, cells, CCTV, alarms, monitors, safes, keys, guard records) and the pad table with rooms, as `ge: probe: ...` lines. Read-only. `tools/mode_probe.lua` uses them. |
| `probe.cctv()`, `probe.alarm()`, `probe.doors_unlocked()` | Live camera state (sight timer, sweep, line of sight to Bond) logged as lines; alarm siren ticks left; whether the unlock cheat is on. |
| `include(name)` | Runs `Data/Scripts/modes/<name>.lua` from inside a mode script. `arena-endless.lua` sets `ARENA_SETTINGS` and then `include("arena")`. |
| `pads.count()` | Number of pads (0 before the stage is set up). |
| `pads.pos(i)` | `x, y, z`, or nil. |
| `pads.far_from(x, z, mindist [, maxdist [, waypoint_only]])` | Array of pad ids whose XZ distance from (x, z) is in range; `waypoint_only=true` keeps pads on the AI waypoint graph (known walkable). Up to 2048. |

### Spawning characters

| Call | Notes |
| --- | --- |
| `spawn.chr(pad, opts)` | Returns a chrnum, or nil (no room, bad pad, model failed). `opts`, all optional: `skin` (a name from `spawn.skins()`, the 70-character roster), or `body` / `head` (names from `spawn.bodies()`, or ids; `head = "random"` or nil gives a random head) and `sunglasses = true`; `weapon` (a name from `spawn.weapons()`, or `"none"`); `health` (multiplier on the stock 4 damage points); `armour` (extra armour, in units of the base 4 damage; hits eat it first and an armoured chr is not staggered); `accuracy`, `speed` (0..100 ratings); `anim_speed` (0.5..3, the "Fast Animation" cheat on one chr); `behaviour` (below); `knives = true` (every eligible shot becomes a thrown knife); `knife_range`, `knife_cooldown`, `sight`, `engage` (units; see arena.md); `face = {x, z}`. Raises on an unknown skin or weapon. |
| `spawn.remove(chr)`, `spawn.alive(chr)`, `spawn.kill(chr)` | Remove, test, kill. |
| `spawn.free()`, `spawn.pools()` | Free chr slots; weapon and prop pool usage. |
| `spawn.where(chr)` | `x, z, actiontype`. |
| `spawn.anim(chr, name [, force])` | One-shot idle animation: yawn, flies, scratch_leg, scratch_butt, adjust, sneeze, chat, listen, cough, laugh, startled, surrender, dance, look. Without `force` it starts only on an idle chr. |
| `spawn.hostage(pad)` | A hostage chrnum (max 16 a stage). |
| `spawn.guard(pad [, {patrol = path_id, weapon = "ak47"}])` | A guard that holds position (max 40 a stage). |
| `spawn.skins()`, `spawn.bodies()`, `spawn.weapons()` | The accepted names. |

`behaviour` is `"chase"` (default: run at Bond, stop and shoot in range), `"rush"`, `"hold"`, `"snipe"` (stand, fire from range, walk in when it loses sight), `"standard"` (stock guard AI) or `"spectator"` (harmless, invincible, untargetable: crowd decoration). Spawned chrs share a limited pool of chr slots; check `spawn.free()` before big waves.

### Pickups

`spawn.item(pad, item [, {ammo = , amount = }])` places a real pickup on the floor, collected by walking over it. Returns a handle, or nil. `item` is a name from `spawn.weapons()` (the pickup carries that gun and its ammo), `"armour"` (`amount` 0..1), or `"ammo"` (an MP ammo box; `ammo = {pp7 = 50, kf7 = 60, shotgun = 16}`). Solo play doubles box ammo. Then `spawn.item_alive(h)`, `spawn.item_remove(h)`, `spawn.item_pos(h)`. Health has no stock pickup: use `bond.heal`.

### Bonds

`p` is a 0-based player index (0 = the host's Bond in co-op). **A missing `p` is always player 0, never the local player.**

| Call | Notes |
| --- | --- |
| `bond.count()` | 1 solo, 2 in co-op, 0 outside a mode. |
| `bond.pos([p])`, `bond.dead([p])` | `x, y, z` (nil if dead or not in play); dead flag. |
| `bond.nearest(x, y, z)` | `player, distance`. |
| `bond.all_down()` | True when nobody can respawn. |
| `bond.id(p)`, `bond.player(chr)` | Chr id of Bond p, and back. |
| `bond.give(p, item [, ammo])`, `bond.give_ammo(p, frac)` | Give a gun; top up ammo of the guns he carries. |
| `bond.heal(p, health, armour)` | Adds, as fractions 0..1 of a full bar. |
| `bond.vitals(p)` | Health and armour. |
| `bond.ammo(p, weapon)`, `bond.take_drop(p [, radius])`, `bond.warp(p, pad [, dx, dz])`, `bond.walk(p, x, z)`, `bond.walk_stop()` | Test helpers. |

### Zones, HUD, hostages, mission

| Call | Notes |
| --- | --- |
| `zone.add(name, x, y, z, r)` | Id or nil (max 32). XZ circle, half height 250. Bond always counts. |
| `zone.move(id, x, y, z)`, `zone.remove(id)`, `zone.watch(chr)`, `zone.contains(id, chr)`, `zone.bonds(id)`, `zone.name(id)` | `watch` makes zone events fire for that chr too. |
| `hud.set(line, text [, player])`, `hud.clear([player])` | Lines 1..3, up to 63 characters. Use lines 1 and 2: line 3 is used by the hostage flash. |
| `hostages.list()`, `hostages.state(chr)`, `hostages.set_state(chr, "follow"\|"wait" [, player])`, `hostages.extract(chr)` | Hostage control; extraction is the script's job (use a zone). |
| `mission.win(msg)`, `mission.fail(msg)` | Ends the mission on the stock report screen. Returns true on the first call. No progress is saved. |

### Weapons, modifiers, music, menus

- `weapon.set(item, field, value)`, `weapon.get(item, field)`, `weapon.reset()`, `weapon.items()`, `weapon.fields()`. Items are lowercase `ITEM_*` names (`ak47`, `wppk`, `goldengun`, `throwknife`, ...) plus `pp7 pp7sil dd44 klobb kf7 zmg ar33 rcp90`. Fields: integers `magsize firerate singlerate penetration sound ammotype`; numbers `damage inaccuracy zoom crosshairspeed aimlockspeed sway recoilback recoilup boltrecoil loudnessmin loudnessmax noiseshot impact`. Edits last until the next stage start.
- `mods.get(name)`, `mods.set(name, v)`, `mods.list()`: the modifier registry (the Mods menu page rows: invincibility, damage scales, one-hit kills, enemy knives, level music, ...). `mods.set` is not written to `midas.ini`.
- `music.set(name)`, `music.play(name)`, `music.list()`, `music.current()`: choose the level music. `set` must be called from a top-level script, before the music starts; from `stage_start` use `play`.
- `menu.add_toggle(label, get, set)`, `menu.add_number(label, min, max, step, get, set)`: add a row to the Mods page (max 24, labels 39 characters). Use from `Data/Scripts/boot/*.lua`.

The stage-touching tables (`bond`, `spawn`, `zone`, `hud`, `mission`, `hostages`, `pads`) are inert outside a mode (nil, 0 or false) and raise in the menus. `mode`, `mods`, `weapon` and `menu` work anywhere.

## Adding a binding (C side)

1. If it needs game types, add a plain-C helper to `port/src/port_lua.c` (or `port_arenamode.c`) and declare it in `port/include/port_lua.h`.
2. In `port/luavm/port_luavm.c` (or `port_luavm_arena.c`) write a `static int portLuaXxx(lua_State *L)`, validate arguments with `luaL_check*`, and register it in a `luaL_Reg` table.
3. Gate anything that changes the simulation on `portLuaGameAllowed()` and never call Lua from a render path.

Lua is its own static library (`ge_lua`) because the game's headers shadow libc; that is why the glue is split in two halves.

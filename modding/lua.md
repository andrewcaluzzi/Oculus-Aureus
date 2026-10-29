# Lua modding reference

Project Midas embeds **Lua 5.5.1** (vendored unmodified in `port/lua`). Scripts can change weapon stats, and a **mode** script can turn a mission into something else: wipe the level, spawn characters and pickups, watch events, draw HUD text and win or lose the mission.

> This is the fork's own API. It is not upstream's, and upstream is writing its own; scripts written here may not run there.

Every function below is registered in `port/luavm/*.c` (the `luaL_Reg` tables). This page was checked against those tables on 2026-10-27, after the API clean-up described in "Conventions" and "Renamed and removed". `tests/lua/api_layout_test.lua` checks the names.

## Where scripts live and when they run

All paths are relative to the folder holding the exe (`build_win/`, `build_pc/`).

| Path | Runs |
| --- | --- |
| `Data/Scripts/boot/*.lua` | Once, the first time Lua is needed (the title screen at start-up). Use for `menu.add_*`. |
| `Data/Scripts/*.lua` | At every solo mission start, alphabetically, each under its own `pcall` (an error is logged and the next file still runs). |
| `Data/Scripts/modes/<name>.lua` | When `GE_MODE=<name>` (letters, digits, `_`, `-`), after the level reset and after the regular scripts. Also pickable in-game: Options > Gameplay > Mods > Game mode. |

The build copies `tools/scripts_example.lua` to `Data/Scripts/example.lua` (all commented out: a tour of the `weapon` table), `tools/crosshair_pack.lua` to `Data/Scripts/mods/crosshair_pack.lua`, and the shipped modes and their modules (`arena`, `arena-endless`, `hostage`, `hostage-flatland`, `probe`, `heist` with `heist_lib` and the `heist_*` / `job_*` modules, `levelgen_layout`) into `Data/Scripts/modes/` (the copy list is in `CMakeLists.txt`). Lua edits reach `Data/Scripts` only when the exe relinks. Edit them in place or write your own.

A mode **removes the stage's guards, objectives and level AI scripts**; the level's **objects are kept** (doors, keyed locks, padlocks, cells, CCTV cameras, monitors, safes, props), and the stock object code still runs (cameras raise the alarm after 300 ticks of unbroken sight). The reset also turns the UNLOCK DOORS cheat on one second into the stage, so every door opens for Bond; call `mode.keep_locks()` at script load to prevent that (`GE_MODE=reset` is just the reset). Scripts run only in a single-player mission (`getPlayerCount() == 1`, solo or co-op): not in demos, recording, split-screen multiplayer or the title stage. They run at stage load, before Bond exists, so register handlers with `on(...)` and do the setup in `stage_start`.

`include(name)` (a global) runs `Data/Scripts/modes/<name>.lua` from inside a mode script (nesting depth 8, names up to 40 characters). There is no `require`: scripts share state through globals (`HEIST`, `HEIST_SETTINGS`, `ARENA_SETTINGS`, `HEIST_BEST`).

Handy: pick the stage with `GE_STAGE=dam` (or `colosseum`, `flatland`, `levelgen` (generated rooms, `modding/levelgen.md`), ...), skip the intro with `GE_SKIPINTRO=1`, run a script from anywhere with `GE_LUA_TEST=<file>`. The in-game console has `lua <code>` (an expression prints `=> value`) and `lua_reload`. Both are refused during netplay. Logs go to `ge: lua: ...`; `print` goes there too.

Errors are logged (`ge: lua: error: ...`) and never fatal. A handler that errors is disabled and logged once. Every listener is dropped at each stage load.

## Sandbox

Libraries: base, string, table, math, utf8, coroutine. There is no `io`, `os`, `debug` or `package`; `dofile`, `loadfile` and `collectgarbage` are nil. 16 MB allocator cap. A single top-level call that runs over 50 million instructions errors with "script ran too long".

For co-op and netplay, a mode script runs identically on every machine, so: `math.random` is a deterministic generator (SplitMix64) re-seeded at each stage start from the session seed (`math.randomseed()` with no argument is ignored in co-op; `math.randomseed(n)` is deterministic); `table.sort` is a stable merge sort; string-key `pairs()` order is the same everywhere. Do not iterate tables keyed by tables or functions, do not use `tostring(table)` as data, and never read the local player or the window. Calls marked **presentation** below may return different things on each machine: never branch the game on them.

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

## Conventions

These hold for every function on this page. Where a function breaks one, its row says so.

**Argument order.**

1. **A required player index comes first.** When a call is about what Bond `p` does or has, `p` is the first argument, then the thing he acts on, then amounts, then an options table: `door.try(p, door)`, `cctv.view(p, cam)`, `gadget.lift_key(p, chr)`, `cover.blow(p, chr)`, `cover.suspicion(p, chr)`, `ai.trait_bribe(p, chr [, cost])`, `bond.give(p, item [, ammo])`, `bond.heal(p, health, armour)`, `test.slap(p, chr)`. This was already the order of most such calls (every `bond.*`, `gadget.*`, `door.try`, `cctv.view`, `ko.search`, `cover.carrying`, `shield.*`); the four that put the chr first were changed to match.
2. **Otherwise the subject comes first.** The chr, door, camera, zone, interlock group or handle the call is about, then what it acts on, then amounts, then options: `spawn.go(chr, pad [, run])`, `zone.contains(zone, chr)`, `cctv.sees(cam, chr)`, `door.lock(door, flags)`, `interlock.lockdown(group, secs)`, `ai.send(chr, x, y, z [, run [, text]])`.
3. **Optional arguments go last, and that includes an optional player or camera.** A trailing optional `p` is a scope or a credit, with a default: `hud.set(line, text [, p])` (default every player), `spawn.ko(chr [, p])` (default "a script"), `hostages.set_state(chr, state [, p])` (default player 0), `test.obj_damage(i, amount [, p])`, `combat.los(chr [, p])` (default the nearest Bond). The same goes for `cctv.set_alarm_time(seconds [, cam])` (default every camera).
4. **Positions** are `x, y, z` in world units, or `x, z` where height does not matter; they always come in that order and never as a table, except point lists (`gadget.reel`, `intel.poly`).

**Players and ids.**

- `p` is a 0-based player index (0 is the host's Bond in co-op). **A missing `p` on a Bond reader (`bond.pos()`, `bond.dead()`, `bond.vitals()`) is player 0, never the local player.** A missing `p` on the HUD (`hud.set`, `hud.clear`, `hud.panel`) means every player. For `bond.pos`, `bond.dead`, `bond.id`, `hud.set`, `hud.clear` and `hostages.set_state` a `p` outside 0..3 raises; elsewhere a bad `p` is treated as "no such Bond" (nil or false).
- A chr is its chrnum. A Bond appears as a chr id in events and zone calls as `bond.id(p)` (`CHR_BOND - p`: -2, -3, ...); `bond.player(chr)` maps back. `CHR_NONE = -1`.
- Doors, cameras and other objects are **setup-record indices** (what `probe.level()` and `door.list()` / `cctv.list()` give). Pads are pad ids. Zones, interlock groups, AR points, key, item and loot handles are numbers this stage hands out. **No id survives a stage load.**

**Returns.**

- `nil`: there is no such thing (no such chr, door, Bond, camera or handle), or a reader has nothing to report.
- `false`: the thing exists, but the call was refused or had nothing to do.
- `true`: done. Readers return several values (`x, y, z`; `fired, held`) or a table, and `nil` when there is nothing.
- `x([v])` setters set when given an argument and return the setting (`cover.on`, `cover.alert`, `mode.hold_fire`, `mode.guard_doors`, `mode.slaps_to_ko`, `combat.stage`). Plain setters return nothing (`mode.keep_locks`, `mode.per_player_keys`, `mode.body_check`, `mode.guards_wake`, `power.set`).

**Errors.**

- A wrong argument type raises (`bad argument #2 to 'give' (string expected, got nil)`).
- An unknown **name** raises: a weapon, skin or body, an item for `bond.equip`, an animation, an event, an AR layer, a trait, a noise kind, a `test.press` button, a `shield.tune` key. Tune tables of the AI layers (`ai.tune`, `noise.tune`, `combat.tune`, `ai.trait_tune`) skip names they do not know and return how many they took.
- A call that touches the stage raises `<name> needs a live stage (not available in the menus)` when no stage is live (a `boot/` script, the menus). In a live stage outside a mode, the stage-touching tables (`bond`, `spawn`, `zone`, `hud`, `mission`, `hostages`, `pads` and the heist tables) are inert: nil, 0 or false. `mode`, `mods`, `weapon`, `menu`, `music`, `sound` and `crosshair` work anywhere.

**Presentation only.** These draw or play on one machine and are not sim state: `sound.*`, `ar.*`, `diag.*`, `intel.*`, `plan.view` and the plan layer, `debrief.set/show`, `crosshair.*`, `hud.panel`, `gadget.bar/marker/xray/dial`, `cover.radio`, `crowd.*`. Their results may differ per machine.

**The test table.** Calls that fake input or events (a stick push, a button press, a slap, a shot's noise, a click) and readers of one machine's view or of counters only a test needs live in the global table `test`, not in the library they act on. A shipped mode must not call them: they skip the game's own input path, several read presentation state, and the table may be gated behind a test switch later. See "The test table" for the list.

## Renamed and removed (2026-10-27)

There is no compatibility shim: the old names are nil, so an old script fails with `attempt to call a nil value (field '...')` at the first call. Every script in this repository was updated.

**Argument order changed** (the player first, rule 1):

| Before | Now |
| --- | --- |
| `cover.blow(chr, p)` | `cover.blow(p, chr)` |
| `cover.suspicion(chr, p)` | `cover.suspicion(p, chr)` |
| `ai.trait_bribe(chr, p [, cost])` | `ai.trait_bribe(p, chr [, cost])` |
| `ai.trait_observe(chr [, p])` | `ai.trait_observe(p, chr)` (`p` is now required) |

**Renamed in place:** `cover.test_act(p)` is now `cover.act(p)`. The shipped gadgets and the interlock call it, so it is not a test hook.

**Moved to the test table:**

| Before | Now | Before | Now |
| --- | --- | --- | --- |
| `spawn.heard` | `test.heard` | `intel.count` | `test.intel_count` |
| `spawn.fire_stats` | `test.fire_stats` | `intel.test_show` | `test.intel_show` |
| `bond.test_gadget` | `test.gadget_used` | `intel.view` | `test.intel_view` |
| `bond.face` | `test.face` | `plan.test_input` | `test.plan_input` |
| `bond.test_noise` | `test.shot_noise` | `plan.test_mouse` | `test.plan_mouse` |
| `bond.rooms_drawn` | `test.rooms_drawn` | `plan.count` | `test.plan_count` |
| `bond.take_drop` | `test.take_drop` | `plan.tabs` | `test.plan_tabs` |
| `bond.walk` | `test.walk` | `plan.has` | `test.has_item` |
| `bond.walk_stop` | `test.walk_stop` | `crosshair.draws` | `test.crosshair_draws` |
| `ko.test_slap` | `test.slap` | `crosshair.test_hit` | `test.crosshair_hit` |
| `obj.test_damage` | `test.obj_damage` | `debrief.is_open` | `test.debrief_open` |
| `cctv.render_info` | `test.cctv_info` | `debrief.record_info` | `test.debrief_record_info` |
| `cctv.test_press` | `test.press` | `debrief.records_path` | `test.debrief_path` |
| `cctv.test_shot` | `test.screenshot` | `shield.grab` | `test.grab` |
| `cctv.test_use` | `test.use` | `shield.face` | `test.chr_face` |
| `hud.panel_lines` | `test.hud_panel` | `diag.freeze` | `test.freeze` |
| `hud.line` | `test.hud_line` | `gadget.test_fire` | `test.fire` |

**Kept where they were** although older docs called them test helpers: `bond.warp` (the heist's co-op start and planning entries use it), `spawn.kill` (the arena's benchmark corpses), `bond.ammo` and `door.guard_stats` (plain sim readers, like `ko.count`). The diagnostic libraries `probe`, `bench` and `simprof` stay as they are: each is already its own table.

**Fixed with it:** the error for an unknown event listed only 26 of the 30 events; it is now built from the event table.

## Events: `on(event, fn)`

Registers a handler (at most 16 per event; an unknown event name raises and lists the known ones).

| Event | Handler arguments |
| --- | --- |
| `stage_start` | `(stage)`: the first tick Bond is in play (waits for every Bond in co-op) |
| `tick` | `(dt)`: every 0.25 s, `dt` in seconds |
| `chr_died` | `(victim, killer)`: chr ids; `killer` is `CHR_BOND`, another chr, or `CHR_NONE` |
| `bond_died` | `(killer, player)`: `killer` is always `CHR_NONE` for now; `player` is the index of the Bond that died (co-op: per Bond; it fires in the death cam too) |
| `zone_enter`, `zone_leave` | `(zone, chr)`: `chr` is `bond.id(p)` for a Bond |
| `hostage_follow`, `hostage_wait` | `(chr, player)`: the Bond that leads it / told it to wait |
| `damage` | `(victim, attacker, amount, weapon)`: damage actually applied (float); weapon is an item id or -1 |
| `chr_spawned` | `(chrnum)`: chrs spawned by the port (the bot, mode scripts) |
| `item_pickup` | `(objtype, id, qty, player)`: `objtype` is the `PROPDEF_*` number (4 key, 7 single magazine, 8 weapon, 19 multi-ammo box, 20 armour, 3 loot from `spawn.item "goldbar"/"money"`); `id` is the weapon / ammo type, or the **key flags** for a key; `player` is the 0-based index of the Bond who picked it up |
| `objective_changed` | `(index, status)`: 0 incomplete, 1 complete, 2 failed |
| `alarm` | `(on)`: 1 the alarm started, 0 it stopped (any cause: a camera, a guard, `alarm.start/stop`, the siren timing out) |
| `cctv_spotted` | `(camera, player)`: a camera saw Bond `player` for its full alarm time (`camera` is the setup index). The stock alarm also goes off unless `cctv.alarm_on_spot(false)` |
| `cctv_view` | `(player, camera, why)`: Bond `player` now watches `camera` on the feeds, or `camera` is -1 when he stopped. `why`: 0 a script (`cctv.view` / `view_off`), 1 he cycled the cameras, 2 he left (B / use), 3 he was hit, 4 he went down |
| `use` | `(player, proptype, idx, objtype)`: Bond `player` pressed use on a door or object. `proptype` is `PROP_TYPE_*` (1 object, 2 door, 4 weapon), `idx` the target's setup index (-1 for a prop made at run time), `objtype` its `PROPDEF_*` number. Fires before the interaction runs, locked or not. A use press with nothing in reach fires too, with `proptype`, `idx` and `objtype` all -1 ("use here" places: screens, a console) |
| `gadget_used` | `(player, item, idx, objtype)`: Bond `player` used a stock gadget (`item` is the `ITEM_*` number: door decoder 38, bomb defuser, data thief, ...) once, on the first frame of its use animation; `idx`/`objtype` as in `use` (-1 if nothing) |
| `body_found` | `(guard, body, ko)`: a guard found a body; `ko` is 1 for a knocked-out chr, 0 for a dead one |
| `chr_knocked_out` | `(chr, player)`: the Bond whose slap knocked it out (-1 a script) |
| `chr_woken` | `(chr, waker)`: the guard that woke it (-1 a script) |
| `chr_searched` | `(chr, player)`: a Bond searched a knocked-out chr; it dropped what it carried |
| `civilian_killed` | `(chr, player)`: the killer's player index (-1 not a Bond) |
| `civilian_surrendered`, `civilian_robbed` | `(chr, player)`: the Bond aiming at it |
| `cover_blown` | `(guard, player, why)`: a cover guard saw through Bond `player`'s disguise; `cover.why_name(why)` names `why` |
| `cover_report` | `(guard, player)`: that witness was still up after the radio time: he reports it |
| `noise` | `(x, y, z, radius, kind, src)`: a noise the guards may hear (`kind`: `slap` `step` `run` `door` `shot` `thud` `glass` `coin` `alarm`; `src` a chr, `-2 - p` for Bond p, -1 the world) |
| `guard_bark` | `(chr, kind, text)`: a guard says something (`kind`: `huh` `hey` `investigate` `lost` `found_body` `radio` `backup` `surrender`) |
| `test_key` | `(name)`: a test hotkey, `"next"` (PageDown), `"back"` (PageUp), `"next+"`, `"back+"` (with Shift), or the console `arena <name>`. Never from the keyboard in co-op or netplay |
| `draw_crosshair` | `(x, y, ctx)`: every frame the sight could show (not in netplay). Presentation only: draw with `ctx`, change nothing else. See "Crosshair styles" |
| `ui_tick` | `(dt)`: every game tick (sim, like `tick` but without its 0.25 s step): for menus that must answer a key press at once (the arena's lobby). Keep the handler cheap |

**`amenu` (the arena's start menu and post-game; `port/luavm/port_luavm_arenamenu.c`, modding/arena.md "Start menu and post-game"):** `amenu.launch{mode=, settings=, preset=, label=, lobby=, coop=, slots=}` ends this stage and loads the Colosseum again with that mode and `ARENA_SETTINGS` text (no mission report); `amenu.arm{...}` the same for when the stage ends by itself; `amenu.disarm()`; `amenu.quit()` ends the stage and goes to the Files screen; `amenu.exit()` closes the game (the start menu's EXIT); `amenu.current()` what this stage was launched with (nil = a plain launch); `amenu.retry([t])` the last run's own launch; `amenu.result([text])` the last run's result; `amenu.prefs()` / `amenu.save_prefs{...}` `Data/Saves/arena_menu.ini`; `amenu.view{...}` the menu to draw (presentation); `amenu.escape_back(on)` Escape is the board's BACK while the controls are held; `amenu.netplay()`. MODE tier.

**`ui` (the generic mode menu layer: any mode's lobby, settings page and debrief in the Arena's look; `port/luavm/port_luavm_ui.c`, `port/src/port_uimenu*.c`, Lua half `tools/ui_menu.lua`; the heist's start menu is built on it):** `ui.launch{mode=, settings=, preset=, label=, lobby=, difficulty=, env={GE_X="v"}}` ends this stage and loads the same stage slot again with that mode, settings text (a Lua table constructor), difficulty (0..3) and up to 8 variables from an allowlist with value rules (`GE_LEVELGEN` a level name, `GE_SEED` digits, `GE_COOP` / `GE_DIAG` 0 or 1, `GE_COOP_P2` idle / walk / follow; anything else is refused; a variable set for one launch and not the next goes back to what it was); the mode name must be letters, digits, `_` and `-`; `ui.arm{...}` the same for when the stage ends by itself; `ui.disarm()`; `ui.quit()` to the Files screen; `ui.exit()` closes the game (the lobby's EXIT); `ui.current()` what this stage was launched with (+ `seq`: launches so far; nil = a plain launch); `ui.retry([t])`; `ui.result([text])`; `ui.prefs(name)` / `ui.save_prefs(name, t)` a named file `Data/Saves/ui/<name>.ini` (name `[a-z0-9_]{1,32}`, keys `[A-Za-z0-9_]`, values printable ASCII; `save_prefs` returns false when anything was refused; `GE_UIMENU_DIR` read at process start, `GE_NOMENUSAVE`; `{}` in netplay); table texts are read back with `UI_MENU.parse`, data only (no calls, methods or functions); `ui.view{layout = "lobby" | "settings" | "debrief", page =, cursor =, items = {{kind = "header" | "card" | "choice" | "button" | "primary", label, value, sub, right, tag, col, medal, chosen, dim, tint}}, title =, kicker =, subtitle =, desc =, footer =, info = {{label, value, rgba, flag}}, info_title =, info_sub =, banner =, banner_rgba =, medal =, medal_name =, medal_rgba =, medal_new =, news =, cells = {{label, value, rgba, flag}}, checks = {{label, state, value}}, checks_title =, chips = {{label, on}}, chips_title =, bars = {{v, kind, label}}, chart =, legend =, best_chip =, new_medal =}` the menu to draw (presentation); `ui.escape_back(on)`; `ui.netplay()`; `ui.state()`. **`levelgen`:** `levelgen.current()` the generated level on the levelgen slot this stage (`"bank"`, `"casino"`), `levelgen.default()`, `levelgen.has(name)`. MODE tier. `UI_MENU.new{...}` (`include("_ui_menu")`) is the menu engine: rows, pages, the cursor, the board events, the saved choices.

`damage`, `chr_spawned`, `item_pickup` and `objective_changed` also fire on an ordinary solo stage, not only in a mode, while a listener exists. The heist events (`alarm`, `cctv_spotted`, `use`, `gadget_used`) fire wherever the game does the thing, but a script only runs in a single-player mission.

## Reference

### Mode, pads, probe

| Call | Notes |
| --- | --- |
| `mode.name()` | The `GE_MODE` string, `""` outside a mode. |
| `mode.env_flag(name)` | True when the environment variable `name` (it must start with `GE_`; any other name is false) is set and not `0`. |
| `mode.keep_locks([on])` | Call at script top level (before the 1 s mark). Skips the level reset's UNLOCK DOORS cheat, so keyed doors and padlocks keep working. Default `true`. |
| `mode.state(text)` | The mode's own sim state as one string, set every tick (the heist: phase, crack progress and jams, the vault clock, loot, waves, ...). Its hash is part of the co-op / netplay state hash and the text is in the desync dump, so a script-state desync is caught on the tick and named. |
| `include(name)` | See above. |
| `pads.count()` | Number of pads (0 before the stage is set up). |
| `pads.pos(i)` | `x, y, z`, or nil. |
| `pads.far_from(x, z, mindist [, maxdist [, waypoint_only]])` | Array of pad ids whose XZ distance from (x, z) is in range; `waypoint_only = true` keeps pads on the AI waypoint graph (known walkable). Up to 2048. |
| `probe.level()`, `probe.pads()` | Log the setup records (doors with key flags, padlocks, cells, CCTV, alarms, monitors, safes, keys, guard records) and the pad table with rooms, as `ge: probe: ...` lines. Read-only. `tools/mode_probe.lua` uses them. |
| `probe.cctv()`, `probe.alarm()`, `probe.doors_unlocked()` | Live camera state logged as lines; ticks of siren left (0 none, 0 outside a mode); whether the unlock cheat is on. |

### Spawning characters

| Call | Notes |
| --- | --- |
| `spawn.chr(pad, opts)` | Returns a chrnum, or nil (no room, bad pad, model failed). `opts`, all optional: `skin` (a name from `spawn.skins()`, the 70-character roster), or `body` / `head` (names from `spawn.bodies()`, or ids; `head = "random"` or nil gives a random head) and `sunglasses = true`; `weapon` (a name from `spawn.weapons()`, or `"none"`); `health` (multiplier on the stock 4 damage points); `armour` (extra armour, in units of the base 4 damage; hits eat it first and an armoured chr is not staggered); `accuracy`, `speed` (0..100 ratings); `anim_speed` (0.5..3, the "Fast Animation" cheat on one chr); `behaviour` (below); `knives = true` (every eligible shot becomes a thrown knife); `knife_range`, `knife_cooldown`, `sight`, `engage` (units; see arena.md); `face = {x, z}`; `offset = {dx, dz}`; the heist options below. Raises on an unknown skin, body, weapon or behaviour. |
| `spawn.remove(chr)`, `spawn.alive(chr)` | Remove (true if it was there); test. |
| `spawn.kill(chr [, "head"])` | Lethal damage through the damage path: a normal death, with drops and events. `"head"` counts it as a headshot. True if it died. |
| `spawn.free()`, `spawn.pools()` | Free chr slots; `loose, held, free_weapon_slots, free_props` (the 30-slot weapon pool and the 600-prop pool). |
| `spawn.where(chr)` | `x, z, actiontype`, nothing when gone. |
| `spawn.room(chr)` | The bg room a chr is in, nil when gone. |
| `spawn.anim(chr, name [, force])` | One-shot idle animation: yawn, flies, scratch_leg, scratch_butt, adjust, sneeze, chat, listen, cough, laugh, startled, surrender, dance, look, fists, cross_arms. Without `force` it starts only on an idle chr. True when started; raises on an unknown name. |
| `spawn.idle(chr)` | True when standing, or a one-shot animation has run out; nothing when gone or dead. |
| `spawn.headshot(chr)` | True when the arena enemy's last damaging hit was to the head (ask in `chr_died`). |
| `spawn.keep_bodies(on)` | Dead enemies stay (no stock fade-out and removal) until `spawn.remove`. |
| `spawn.trim_drops(max [, keep_radius])` | Frees loose dropped weapons beyond `max`, oldest first, none within `keep_radius` (400) of a Bond. Returns how many it freed. |
| `spawn.knife_only(chr [, on])` | Arena: the chr throws knives and fires no bullet (KNIVES ONLY). True when set. |
| `spawn.scale(chr, factor)` | Draw the chr `factor` (0.5..2) times its size, as the Tiny Bond / DK cheats scale chrs. |
| `spawn.heal(chr, frac)`, `spawn.health(chr)` | Heal wounds by `frac` of full health (not armour); `health_left, armour, damage, maxdamage` (armour in units of the base 4 damage), nothing when gone. |
| `spawn.explode(chr [, kind [, p]])` | A stock explosion at the chr's feet: `kind` `"small"`, `"medium"` (default), `"grenade"`, or a type 1..20 (others are refused: false). `p` credits the blast to Bond `p` (the crowd favour). |
| `spawn.disarm(chr)` | The guns the chr holds fall to the floor and it switches to the melee chase list. True when it held one. |
| `spawn.last_hit(chr)` | The last hit a Bond's shot or blast landed on an arena enemy: `part, weapon, player, distance, age_seconds, blast_serial`, nothing when none. |
| `spawn.owned_guns(chr)` | The names of the guns the arena enemy held (one per hand), kept after a disarm or its death drop; nothing when none is known. The crowd's THEIR OWN GUN compares the killing gun with these. |
| `spawn.hit_events()` | `{ {kind = "disarm" | "crowd", chr =, player =}, ... }` since the last call (a 32-entry queue), then how many were lost. |
| `spawn.hostage(pad)` | A hostage chrnum (max 16 a stage), or nil. |
| `spawn.guard(pad [, {patrol = path_id, weapon = "ak47"}])` | A guard that holds position (max 40 a stage), or nil. Raises on an unknown weapon. |
| `spawn.skins()`, `spawn.weapons()` | Arrays of the accepted names. `spawn.bodies()` returns two arrays: body names, head names. |

`behaviour` is `"chase"` (default: run at Bond, stop and shoot in range), `"rush"`, `"hold"`, `"snipe"` (stand, fire from range, walk in when it loses sight), `"standard"` (stock guard AI), `"spectator"` (harmless, invincible, untargetable: crowd decoration) or `"civilian"` (see "Heist guards, bodies and civilians"). Spawned chrs share a limited pool of chr slots; check `spawn.free()` before big waves.

`offset = {dx, dz}` spawns that far from the pad centre: the floor is walked from the pad's stan tile to the point (a wall, a ledge over 60 units or a gap refuses it: nil), and the pad centre itself may be taken, so several chrs can share one pad. Without it a pad with a chr standing on it refuses the spawn.

### Pickups

`spawn.item(pad, item [, {ammo = , amount = }])` places a real pickup on the floor, collected by walking over it. Returns a handle, or nil when there was no room; raises on an unknown item or a pad with no floor. `item` is a name from `spawn.weapons()` (the pickup carries that gun and its ammo; `ammo` sets the rounds), `"armour"` (`amount` 0..1, default 1), or `"ammo"` (an MP ammo box; `ammo = {pp7 = 50, kf7 = 60, shotgun = 16}`). Solo play doubles box ammo. Then `spawn.item_alive(h)` (still on the floor), `spawn.item_remove(h)` (true if it took it), `spawn.item_pos(h)` (`x, z`, nothing when gone). Health has no stock pickup: use `bond.heal`.

Markers (the arena, presentation only): `spawn.markers(on [, {fade, near, drops, knives, drop_near, merge, edge, max, legacy}])` turns the pickup markers on (modding/arena.md, Pickup markers); `spawn.marker_sync()` publishes the live pickups (call it a few times a second); `spawn.item_marker(h, "ammo" | "armour" | "gift" | "item" [, label])` sets an item's marker class and label (7 characters at most).

Loot: `spawn.item(pad, "goldbar" | "money" [, {offset = {dx, dz}}])` places the stock beta pickup model (`PchrgoldbarZ` / `PchrmoneyZ`) as a plain prop, from its own pool of 160 (the 12 world pickups above and the 30 weapon slots are untouched). A living Bond within 90 units of it collects it: it goes, the stock ammo pickup sound plays and `item_pickup(3, id, 1, player)` fires (`objtype` 3 = `PROPDEF_PROP`, `id` 79 gold bar / 78 money). It never enters the inventory; the mode counts it. The handles (above 0x1000000) work with `spawn.item_alive / item_remove / item_pos`. It has no line-of-sight test: do not put it within 90 units of a wall you mean to keep Bond behind.

### Bonds

| Call | Notes |
| --- | --- |
| `bond.count()` | 1 solo, 2 in co-op, 0 outside a mode. |
| `bond.pos([p])` | `x, y, z`, or nil if dead or not in play. |
| `bond.dead([p])` | Dead flag; nil when there is no such Bond. |
| `bond.nearest(x, y, z)` | `player, distance` of the nearest Bond in play, or nil. |
| `bond.all_down()` | True when every Bond is dead (co-op: nobody left who can respawn). |
| `bond.id(p)`, `bond.player(chr)` | Chr id of Bond p (raises outside 0..3), and back (nil for a chr that is not a Bond). |
| `bond.room(p)` | The bg room Bond `p` stands in, or nil. |
| `bond.give(p, item [, ammo])` | Give a gun (it goes in the inventory, not in his hand). Names as `weapon.set` takes them, any case. True when given. |
| `bond.give_ammo(p, frac)` | Ammo for the guns he carries (+`frac` of each type's maximum), no gun. Returns how many ammo types it topped up. |
| `bond.equip(p, item)` | Draw `item` into his right hand (the stock weapon switch, about a second; given first if he lacks it; names as `bond.give`, plus `"fist"`). Raises on an unknown item. |
| `bond.weapon(p)` | The `ITEM_*` id in his right hand (0 none, 1 slappers, 5 silenced PP7, 38 door decoder), nil no such Bond. |
| `bond.ammo(p, weapon)` | `carries, reserve`: whether he carries that weapon and the reserve ammo of its type; nothing when there is no such Bond or weapon. |
| `bond.heal(p, health, armour)` | Adds, as fractions 0..1 of a full bar (either may be omitted: 0). |
| `bond.vitals([p])` | `health, armour`, or nil. |
| `bond.shots(p)` | `fired, hits`: the stock per-player shot counters. |
| `bond.warp(p, pad [, dx, dz])` | Puts Bond `p` on a pad (plus an offset from its centre). True when moved. |
| `bond.has_key(p, flags)`, `bond.keys(p)` | True when Bond `p` himself carries every bit of `flags`; the bitmask of all the key flags he carries. Never pooled, even in stock co-op. |
| `bond.use_key(p)` | Inside a `use` handler: true when the press is the **use action** (the Port use bind), false for the B button (which is use *and* reload). |

### Zones, HUD, hostages, mission

| Call | Notes |
| --- | --- |
| `zone.add(name, x, y, z, r)` | Id or nil (max 32). XZ circle, half height 250. Bonds always count. |
| `zone.move(zone, x, y, z)`, `zone.remove(zone)` | Move it; drop it. |
| `zone.watch(chr)` | Zone events fire for that chr too. True if it was added. |
| `zone.contains(zone, chr)` | True when the chr (or `bond.id(p)`) is inside. |
| `zone.bonds(zone)`, `zone.name(zone)` | Array of the player indices inside (last tick); its name, or nil. |
| `hud.set(line, text [, p])` | Lines 1..3 (raises otherwise), up to 63 characters; `text = nil` clears the line. Without `p` every player's HUD. Use lines 1 and 2: line 3 is used by the hostage flash. |
| `hud.clear([p])` | All three lines of every player, or of one. |
| `hud.panel(lines [, p])` | Up to 8 lines (63 characters each) in a box at the top right; `nil` or `{}` clears it. With `p` only that Bond's machine draws it; a Bond with a panel of his own sees only his. Presentation. |
| `hud.countdown(seconds)`, `hud.countdown_left()` | The stock mission countdown clock, set and running; `hud.countdown(nil)` (or `false`) hides and stops it. `countdown_left()` gives the seconds left, or nil when it is not running. The clock keeps counting below 0 (shown as a positive number) if you do not stop it. |
| `hostages.list()` | `{ {chr, state, leader}, ... }`; `state` `"wait"`, `"follow"`, `"extracted"`, `"dead"`; `leader` only while following. |
| `hostages.state(chr)` | That state, or nil. |
| `hostages.set_state(chr, "follow" \| "wait" [, p])` | `p` is the Bond who leads it (default player 0). Raises on another state. |
| `hostages.extract(chr)` | Mark it extracted (extraction is the script's job: use a zone). |
| `mission.win([msg])`, `mission.fail([msg])` | End the mission on the stock report screen. True on the first call (false once it has ended, or outside a mode). No progress is saved. |
| `mission.difficulty()` | 0 Agent, 1 Secret Agent, 2 00 Agent, 3 007. |

There is no save API for modes: a mode that wants a best score keeps it in a global (the Lua state survives stage reloads within a session; `heist` uses `HEIST_BEST`) and logs it. The heist's records file is written by `debrief.record`, and its contents never come back to the script.

### Doors, keys, objects, alarm, CCTV

Door and camera numbers are the **setup-record indices** that `probe.level()` logs (`ge: probe: DOOR idx=75`, `CCTV idx=95`). All of it runs identically on every machine in co-op. Under a mode the level reset opens every door for Bond: call `mode.keep_locks()` first (top level) if keys are to matter. Locking a door does not stop a one-way door (flags2 bits 0x08000000 / 0x10000000) opening from its free side, as in the stock game.

| Call | Notes |
| --- | --- |
| `door.list()` | Array of door setup indices. |
| `door.get(i)` | `{idx, state, open, keyflags, x, y, z, room, live, autoclose, linked, cell, padlocked}` or nil. `state` is `"closed"`, `"opening"`, `"open"` (open, or waiting to auto-close) or `"closing"`; `open` is 0..1; `autoclose` in ticks; `linked` the sibling door of a pair (-1 none). |
| `door.open(i)`, `door.close(i)` | Start the move whatever the lock says. True started, false already that way, nil no such door. |
| `door.lock(i, flags)`, `door.unlock(i [, flags])`, `door.set_keyflags(i, flags)` | Set / clear (all, when `flags` is omitted) / replace the key bits a Bond must hold to open it. A door with `keyflags == 0` is unlocked. |
| `door.try(p, i)` | Bond `p` uses door `i` as the use key would: the stock key check against **that Bond's** keys (plus the `use` event). True the door moved, false refused, nil no such door or Bond. |
| `door.one_way(i [, false])` | Whether door `i` is a stock one-way door. With `false` both one-way bits are cleared first (it returns the state before), so only the key opens it from either side. |
| `door.snap(i, open)` | Put door `i` and its linked doors fully open (`true`) or shut (`false`) at once, even with a chr in the doorway. The portal opens / closes as a stock move would. Nil no such door. |
| `door.guard_access(i, rule)`, `door.guard_stats()` | See "Guard door rules". |
| `obj.get(i)` | Any setup object by index: `{idx, type, model, pad, live, room, x, y, z, health, damage, destroyed}` or nil. `type` is the `PROPDEF_*` record type (1 door, 3 prop, 4 key, 5 alarm, 6 camera, 10 monitor, 43 safe). |
| `spawn.key(chr, flags [, model])` | Gives a spawned chr a key (a stock key assigned to the chr, so it drops where he dies and is picked up like any guard's key). `model` defaults to 86 (the swipe card; the stock keys use 86, 243 and 244). A handle, or nil (no such chr, or it has no model yet; call it right after `spawn.chr`). |
| `spawn.key_at(pad, flags [, model])` | A floor key on a pad. |
| `spawn.key_alive(h)` | `alive, held, x, z` (or just `false`): whether the key is still in the world, whether a chr carries it, and where it is. A key a Bond picked up is no longer alive. Up to 64 keys per stage. |
| `mode.per_player_keys([on])` | Co-op: a key opens doors only for the Bond who picked it up (stock pools every Bond's keys). Off by default, reset at each stage start; call it at top level or in `stage_start`. |
| `alarm.start()`, `alarm.stop()`, `alarm.active()` | The global alarm (siren and the guards' alert state). The `alarm` event fires on every change. `alarm.stop()` also ends the alarm animation of any raiser past frame 60 (it would re-fire the siren). |
| `cctv.list()` | Array of camera setup indices. |
| `cctv.get(i)` | `{idx, x, y, z, room, face, timer, alarm_time, disabled, destroyed, live, lookpad}`. `timer` is the seconds of unbroken sight so far; `alarm_time` the override in seconds (0 = stock: 10 s on Agent, 5 s above); `disabled` is true for a shot camera too, `destroyed` only for those. |
| `cctv.disable(i)`, `cctv.enable(i)` | Switch a camera off / on (it stops seeing and sweeping). `enable` does not revive a shot camera (returns false). |
| `cctv.set_alarm_time(seconds [, cam])` | Sight time before a camera fires, for one camera or all; 0 or nil restores the stock time. |
| `cctv.alarm_on_spot(on)` | With `false` a camera that spots a Bond only fires `cctv_spotted` and the script decides what happens (default true). |
| `cctv.view(p, cam)`, `cctv.view_off(p)`, `cctv.viewing(p)` | Camera feeds, below. |
| `cctv.name(cam [, text])` | The label the feed shows (default `CAMERA n`); set it with `text`. Returns the label. |
| `cctv.sees(cam, chr)` | True when a working camera has the chr (or `bond.id(p)`) inside its live cone (45 degrees either side of where the head points now, a clear stan line). It reads only; the sight timer is untouched. The line walks from the camera's own floor, so a chr straight above or below the cone is not seen. |
| `power.set(on)`, `power.on()` | While the power is out (mode only) no alarm box, raiser, camera or script raises the alarm and the siren cannot sound. Darkening the cameras is the script's job (`cctv.disable`). In the hash. |

Co-op notes. With more than one Bond a camera tests **every** living Bond, each on his own sight timer on that camera; `cctv_spotted(cam, p)` names the Bond whose own time ran out, and `cctv.get(i).timer` shows the longest. The state these calls set (key mode, camera overrides and switch-offs, the per-Bond sight timers, the countdown, each Bond's cards) is folded into the co-op state hash. A test mode that exercises all of the above is `tests/heist/heist_bindings.lua`.

### CCTV view (camera feeds)

`port/src/port_cctvview.c`, `port/luavm/port_luavm_cctv.c`. A Bond watching a camera sees the world from it: full room geometry (a portal walk from the camera), the props the game drew this frame, and every chr in the camera's rooms posed for the feed. He stands where he is, at the mercy of the guards: his stick, buttons, mouse look and Port binds do nothing else, and being hit throws him out.

| Call | Meaning |
| --- | --- |
| `cctv.view(p, cam)` | Bond `p` watches camera `cam`. An off camera (shot, or `cctv.disable`) shows static and "NO SIGNAL". False: no such camera, or he is down. |
| `cctv.view_off(p)` | Stop watching; true if he was. |
| `cctv.viewing(p)` | The camera he watches, or nil. |

Controls while watching (GoldenEye's own buttons): **A** or **C-right** next camera, **C-left** previous camera (the stick pushed left / right too: on PC, the strafe keys), **B** leave. Port binds: next / previous weapon or inventory = next / previous camera, use or menu back = leave. The order is `cctv.list()`'s. The gun, crosshair and ammo counter are hidden; the feed shows the camera's name and its place in the list, a blinking REC dot, the mission clock, and the controls.

Which camera a Bond watches is sim state: Lua sets it, his own synchronized input cycles and leaves it, it holds him, and it is in the heist state hash. The picture is presentation only, on the watching Bond's machine; the feed's own draw puts back the sim RNG, the chrs' model state and the frame pool's allocations, so a machine with a feed open stays bit-identical to one without (`tests/heist/run_determinism.sh`, `run_netcctv.sh`). Limits: the camera's lens is a fixed 62-degree fov; the room's static lighting still follows Bond's camera (cosmetic).

### Heist guards, bodies and civilians

Test mode: `tests/heist/heist_guards.lua` (run with `tests/heist/run_guards.sh`).

**More `spawn.chr` options.**

| Option | Effect |
| --- | --- |
| `alarm_pad = pad` | The stock alarm raiser list: the guard stands still until it sees or hears Bond (or sees a guard shot or die), runs to `pad`, plays the alarm animation and sounds the alarm, then runs at Bond. Works on stages with no alarm box. The pad should be on the waypoint graph. |
| `hearing = n` | `chr->hearingscale`. A shot of loudness L alerts the guard when Bond is closer than `hearing * L * 100` units. Default 1. |
| `vision = units` | Sight range (alias of `sight`). Default 15000. |
| `post = true` or `post = {x, z}` | Stand guard at the spawn pad (stock simple guard AI), facing `{x, z}` if given. Same as `behaviour = "hold"` plus `face`. An explicit `behaviour` wins. |
| `patrol = path_id` | Walk a stock patrol path of the stage's setup. A missing path logs one line and the guard holds. |
| `behaviour = "civilian"` | See below. |

**Chr slots.** Stage init runs before any script, so the slot count comes from the environment: `GE_MODE_SLOTS=n` (any mode) or any mode whose name starts with `heist` (250 extra slots on top of the stage's own guards + 10). A heist mode (or `GE_MODE_SLOTS`) also gets 410 more object model slots and, once the 30 stock weapon slots are all held, a port pool of 320 weapon records, so guard 31 onwards is armed.

**Reinforcement waves.** `include("heist_lib")` (`tools/heist_lib.lua`) gives `heist.reinforce{pads = {...}, interval = 25, per_wave = 3, max_alive = 16, opts = {weapon = "ak47", behaviour = "chase"}, first = 0, reserve = 12, on_spawn = fn(chr)}`: while `alarm.active()` (or your own `active = function() ... end`) it spawns `per_wave` guards every `interval` seconds at the entrance pads (round robin; a busy pad is tried a step aside first), never above `max_alive` alive and never when `spawn.free()` is below `reserve`. Returns `r` with `r.alive()`, `r.spawned`, `r.waves`, `r.is_wave(chr)`, `r.stop()`, `r.wave_now()`. `opts` may be a function returning a fresh `spawn.chr` table per guard.

**Bodies.** Every ~8 ticks each living, non-civilian chr in the same room as a dead chr and within its sight range (capped at 1500 units, and within 150 units in height) notices it: its stock `chrseedie` is set, so its own AI list reacts. The first one to notice a body fires `body_found(guard, body, ko)`, once per body. Bodies must still be there: stock removes corpses about 1.5 s after death, so call `spawn.keep_bodies(true)`. A knocked-out chr counts as a body too (`ko` = 1).

**Civilians.** `spawn.chr(pad, {behaviour = "civilian", body = "Civilian_1_Female"})`: unarmed, idles at its post, in the hostage faction (guards and bots never target it, but a shot that hits it hurts it), counted by the stock casualty counter. Aimed at by a Bond with a gun or a knife (40 units to the aim range ahead of it, his view ray within about a body width of it, a line of sight) for `aim` seconds, it surrenders (`civilian_surrendered`); still aimed at for `threat` seconds it drops everything it carries (`civilian_robbed`, once; a key from `spawn.key(civ, flags)` falls where it stands). Nobody aiming for 3 s: it lowers its hands. Gunfire it hears makes it cower for `cower` seconds. Death: `civilian_killed(chr, player)`. A knock-out is not a death.

| Call | Notes |
| --- | --- |
| `civilian.state(chr)` | `"idle"`, `"surrendered"`, `"cowering"`, `"knocked_out"`, or nil (not a civilian). |
| `civilian.killed()` | Civilians killed this stage. |
| `civilian.tune{range = 800, aim = 0.5, threat = 2, cower = 4}` | Aim distance in units, and the three times. Unset keeps the current value. |
| `mode.body_check([on])` | The body scan (default on). |
| `bodies.found()` | Bodies found this stage. |
| `spawn.down(chr)` | Whether a chr counts as a body (dead or knocked out). |
| `mode.hold_fire([on])` | Set and return. On: a guard holds a shot while a bank civilian is within 40 units of his line of fire to his target, short of it; he fires when the line clears. Off by default; the heist turns it on. |

The civilian table, found-body list and counters are in the co-op state hash.

### Knock-outs

Test mode: `tests/heist/heist_ko.lua` (`tests/heist/run_ko.sh`).

In a mode whose name starts with `heist`, Bond's slappers knock a chr out on the second slap. The first slap plays the stock hit reaction but does **no damage**; the second makes the chr fall (the stock soft death animation) and hold the last frame. Guns, knives and blasts are never slaps. Hostages, the co-op bot, invincible chrs and the Bonds cannot be knocked out. Knocked out is not dead: no `chr_died`, `spawn.alive` stays true, no AI list, no collision, no auto-aim; it **never wakes by itself**; a gun still kills it. What it carries stays on it until a Bond presses use within about 1.3 m (`chr_searched`, once per knock-out). With `mode.guards_wake(on)` (default on) the first guard to find it walks over, looks it over for 1.5 s and wakes it (`chr_woken(chr, guard)`).

| Call | Notes |
| --- | --- |
| `spawn.ko(chr [, p])` | Knock a chr out now (as if Bond `p` slapped it; omitted = a script, the event's player is -1). False if it cannot be knocked out. |
| `ko.is(chr)` | True while it is knocked out. |
| `ko.count()` | `down, total, woken`: knocked out now, knock-outs this stage, wakes this stage. |
| `ko.slaps(chr)` | Slaps counted toward its knock-out (back to 0 when it wakes). |
| `ko.wake(chr)` | A script wakes it (no guard; `chr_woken(chr, -1)`). |
| `ko.waker(chr)` | The guard on its way to wake it, or nil. |
| `ko.search(p)` | Bond `p` presses use where he stands: the chr searched, or nil. Same code as the use key. |
| `mode.slaps_to_ko([n])` | Set and return the slap count; 0 = slaps are stock hits. Default 2 in a `heist*` mode, 0 otherwise; reset each stage. |
| `mode.guards_wake([on])` | A guard who finds a knocked-out chr comes to wake it (default true). |

The knock-out table and both options are in the co-op state hash.

### The banker cover, guard door rules, scripted walks

`port/src/port_heist.c` "cover"; test mode `tests/heist/heist_cover.lua` (`tests/heist/run_cover.sh`).

**The disguise.** With `cover.on(true)`, a guard registered with `cover.guard(chr)` does not see a Bond whose cover holds with him: the stock sight test asks the port first and gets "not seen". Instead each cover guard has a suspicion meter per Bond (0..1), filled while he *could* see that Bond (within `range`, inside `cone` degrees either side of where he faces, or anywhere within 100 units; the stock stan line) by what the Bond is doing: loot carried or an act seen (a slap, a robbery, a body searched, a gun fired, `cover.act`) fills it at once; a gun or knife in hand in `armed` seconds; a restricted room in `restricted`, a staff room in `staff`; running close in `run`; lingering close in `close_time`. It drains in `decay` seconds. Full: that guard alone sees through him (`cover_blown(guard, p, why)`) and attacks. A guard who hears a Bond fire or is hit sees through the shooter at once. After `radio` seconds, if he is still up, `cover_report(guard, p)` fires once. `cover.alert(true)` ends every disguise. With the cover on, a camera times only a Bond who looks wrong to it.

| Call | Notes |
| --- | --- |
| `cover.on([on])`, `cover.alert([on])` | Set and return. Both reset at a stage start (off). |
| `cover.guard(chr [, "security"])` | Register a guard (up to 384); the role is for the mode. True when registered. |
| `cover.room(room, "public" \| "staff" \| "restricted")`, `cover.room(room)` | A bg room's class (default public); returns the class. Raises on another class name. |
| `cover.carrying(p, on)` | Bond `p` carries loot. |
| `cover.act(p)` | Bond `p` did something wrong in view: for `act` seconds every cover guard (and camera) that sees him sees through him, as if he had slapped someone. False when the cover is off. (Was `cover.test_act`.) |
| `cover.blow(p, chr)` | A script makes guard `chr` see through Bond `p` now (`why` "script"). False when the cover is off or `chr` is not a cover guard. |
| `cover.tune{range = 1800, cone = 75, armed = 0.4, restricted = 1, staff = 5, run = 4, run_speed = 450, run_range = 400, close = 150, linger = 2, close_time = 4, decay = 12, radio = 5, act = 1.5}` | Any subset. |
| `cover.state(chr)` | `{susp, witness, reported, sees, role, why}` or nil: the highest meter, bit masks by player, the role, the reason of the first witness. |
| `cover.suspicion(p, chr)` | Guard `chr`'s meter on Bond `p`, 0..1, or nil (not a cover guard). |
| `cover.meter(p)` | `state, level, guard`: `"off"`, `"unseen"`, `"watched"`, `"suspicious"`, `"blown"`, `"alert"`; the highest meter of a guard still up, and that guard (or nil). |
| `cover.seen(p)` | Some cover guard that is up has Bond `p` in sight now. |
| `cover.count()`, `cover.speed(p)`, `cover.why_name(n)` | Guards registered; Bond `p`'s speed at the last scan (units/s); `"armed" "loot" "act" "restricted" "staff" "running" "close" "heard" "hurt" "script"`. |
| `cover.radio(chr [, "relay"])` | The radio sound at that guard, at a volume and pan from this machine's Bond. Presentation. |

**Guard door rules.** The stock route opens any door a chr walks into. `mode.guard_doors([on])` (set and return; default off, reset each stage) makes a door with a rule open only for a guard whose role is allowed: `door.guard_access(i, "any" | "never" | role | {roles})` (`"any"` removes the rule), `spawn.role(chr, role | {roles})` (the roles are the script's own names, up to 32 a stage). A refused guard waits at the shut door. `door.guard_stats()` gives `refused, opened`. In the hash.

**Scripted walks.** `spawn.go(chr, pad [, run])`: the chr walks (runs) to the pad on the stock waypoint route; its own list keeps running, and the arena's direct route does not re-aim it on the way (up to a minute). True when sent.

### Guard hearing and awareness: `noise.*`, `ai.*`

designs/bank-heist.md section 18. The heist turns it on; any mode can: `ai.enable(true)`, then `ai.guard(chr)` per guard.

| Call | Notes |
| --- | --- |
| `noise.emit(x, y, z, radius, kind [, src])` | A noise the guards may hear. `kind` a name (`slap` `step` `run` `door` `shot` `thud` `glass` `coin` `alarm`; unknown raises) or its number; `src` a chr, `-2 - p` for Bond p, -1 (default) the world. A guard hears it inside `radius x his hearing scale x the room factor`. Returns how many tracked guards heard it. |
| `noise.radius(kind)` | The built-in radius, or nil. |
| `noise.tune{slap = 700, portal = 0.6, door_factor = 0.25, wall = 0.1, door_forced = 1100, ...}` | Returns how many keys it took. |
| `noise.factor(room_a, room_b)` | The hearing factor between two rooms (1 the same room, x 0.6 per open portal, x 0.25 per shut door, never under 0.1). |
| `noise.last()` | `{x, z, radius, kind, heard, count}` or nil. |
| `ai.enable(on)`, `ai.on()` | The layer. |
| `ai.guard(chr [, kind])` | Track a guard. `kind` is the mode's number (the heist: 1 post, 2 raiser, 3 key-card carrier, 4 investigator, 5 squad, 6 vault, 7 wave, 8 response); carriers hold their posts. True when tracked. |
| `ai.patrol(chr, {pad, ...})` | A loop (up to 16 pads) he walks while unaware. |
| `ai.awareness(chr)` | `state, level, lastx, lastz` (0 unaware, 1 suspicious, 2 searching, 3 alert, 4 combat; level 0..1; the last noise point), nil for an untracked chr. `ai.state_name(state)` names a state. |
| `ai.info(chr)` | `{state, name, level, sub, x, z, post_x, post_z, looks, down, heard_ago, face}` or nil. `sub` is the step (`turn`, `go`, `look`, `return`, `patrol`). |
| `ai.send(chr, x, y, z [, run [, text]])` | Send him to search a point (a radio line with `text`). True when sent. |
| `ai.nearest_free(x, y, z [, range [, exclude]])` | The nearest tracked guard free to go, or nil. |
| `ai.backup(chr)` | That guard calls for backup now, once. Returns how many answered (key-card carriers never answer). |
| `ai.bark(chr, kind, text)` | A guard says something (fires `guard_bark`). |
| `ai.tune{turn = 1.2, look = 2.5, search_time = 30, checkin = 25, backup = 2, lure = 0.25, ...}` | Returns how many keys it took (`tools/heist_ai.lua` lists them). |
| `ai.counts()` | `{barks, searches, giveups, checkins, backups, cam_acts, cam_bodies}`. |

From C: `port/include/port_noise.h` (`portNoiseEmit`, `portAiAwareness`); declare them `__attribute__((weak))` if your code must build without them.

### Guard personalities: `ai.trait*`

designs/bank-heist.md section 32 (`port/src/port_traits.c`, `tools/heist_traits.lua`). Added to the `ai` table.

| Call | Notes |
| --- | --- |
| `ai.trait_enable(on)`, `ai.trait_on()` | The layer. |
| `ai.trait(chr [, name])` | The guard's trait (`"none"`, `"rookie"`, `"veteran"`, `"book"`, `"bribable"`); with a name, set it (true when set; raises on an unknown trait, and outside a live stage). |
| `ai.traits()` | `{ {chr, trait, observed, seen_by, bribed, skips, clocked [, skip_pad]}, ... }`: every traited guard, in deal order. `seen_by` is a bit mask of players. |
| `ai.trait_val(chr, field)` | `hear`, `susp`, `turn`, `checkin`, `retreat`, `surrender`, `pads`, `backup`, `full` (the neutral value when the layer is off). Raises on another field. |
| `ai.trait_stations({pad, ...})` | The watchclock stations (a veteran may skip them). Returns how many it took. |
| `ai.trait_observe(p, chr)` | Bond `p` has had a look at guard `chr` (a camera feed; direct sight is scanned in C). True for a traited guard, false otherwise. |
| `ai.trait_bribe(p, chr [, cost])` | `"ok"` paid off, `"refused"` (he is suspicious, or the alarm is up), `"no"` (not bribable), `"already"`. Raises outside a live stage. |
| `ai.trait_stats()` | `{skips, clocked, bribes, bribe_cost, radios, observed, none, rookie, veteran, book, bribable}`. |
| `ai.trait_tune{skip = 0.5, radio_cooldown = 20, radio_range = 1600, acc_rookie = -40, acc_veteran = 10}` | Returns how many keys it took. |

### Combat: `combat.*`

designs/bank-heist.md section 24 (`port/src/port_combat.c`, `tools/heist_combat.lua`): guards take cover, peek, flank and retreat.

| Call | Notes |
| --- | --- |
| `combat.enable(on)`, `combat.on()` | The layer (`enable` returns true). |
| `combat.guard(chr [, kind])` | Register a guard (`kind` as `ai.guard`, plus 9 police). True when registered. |
| `combat.tune{react = 1.5, hide_min = 1.2, flank = 1, ...}` | Numbers or booleans; returns how many keys it took. |
| `combat.info(chr)` | `{state, name, role, cover_x, cover_z, peek_x, peek_z, pair, peeks, covers, flanks, retreated}` or nil. `name`: idle, to_cover, hide, to_peek, shoot, flank; `role`: none, suppress, flank. |
| `combat.counts()` | `{covers, peeks, flanks, retreats, moves, releases, barks, searches, fails}`. |
| `combat.los(chr [, p])` | Bond `p` (default the nearest) has the layer's line to him. |
| `combat.under_fire(chr)` | As a near miss. |
| `combat.pair(flanker, partner)` | Pair two guards for a flank. |
| `combat.stage([n])` | Set and return the heat stage (0 none, 1 police, 2 SWAT; the mode sets it, the hash keeps it). |

### Hostages and the human shield: `shield.*`

designs/bank-heist.md section 31 (`port/src/port_shield.c`, `tools/heist_hostages.lua`). Any mode may use them. A Bond grabs a civilian with a use press at nothing behind it; a gun to its head holds guards' fire.

| Call | Notes |
| --- | --- |
| `shield.enable(on)`, `shield.on()` | The layer (`enable` returns true; `on()` is false outside a mode). |
| `shield.tune{dist = 45, reach = 120, speed = 0.5, drop = 0.35, line = 34, flank_after = 2, front = 0.5, flank = 1}` | Raises on an unknown key. |
| `shield.held(p)` | The hostage Bond `p` holds, or nil. |
| `shield.holder(chr)` | The player holding it, or nil. |
| `shield.release(p [, why])` | `why`: `"use"`, `"hurt"`, `"two_hands"`, `"weapon"`, `"script"` (default). True if he held one. |
| `shield.last(p)` | `what, serial`: what his last use press did (`"none"`, `"grab"`, `"nogun"`, `"front"`, `"nobody"`, `"release"`), and a counter that changes with each press. |
| `shield.info(chr)` | `{by, held, secs, hurt, standoff, clean, takes, why}` for a chr ever taken this stage, or nil. |
| `shield.list()` | Every hostage taken this stage. |
| `shield.standoff(chr)` | Mark the police stand-off used for it; true when newly marked. |
| `shield.counts()` | `{grabs, releases, refused, held_shots, clear_shots, drops, flanks, barks}`. |

### Door interlocks: `interlock.*`

designs/bank-heist.md section 29: `port/src/port_interlock.c`, used by `tools/heist_interlock.lua` (the bank's mantrap and sally port). Any mode may define groups. Of a group's doors only one may stand open: every open order (a Bond's use, a guard's door finder, an AI list, `door.open`) is checked; a refused one is queued and the group opens it once every door is shut and the hold is over. Each door closes itself. In the state hash and the rollback snapshot. Test: `tests/heist/run_interlock.sh`.

| Call | Notes |
| --- | --- |
| `interlock.define{doors = {i, ...}, hold = 3, close = 4, fail = "safe" \| "secure"}` | 2-4 door indices (more raises; a double door's leaves count as one). `hold`: seconds every door stays shut after the last one shuts; `close`: the doors-closing timer; `fail`: with the power out, `"safe"` drops the interlock, `"secure"` freezes every door. Returns the group id, or nil. |
| `interlock.get(g)` | `{phase, doors, open, pending, last, by, hold, hold_len, lock, cycles, refused, violations, queued, fail}` or nil. `phase`: `"ready" "cycle" "verifying" "lockdown" "nopower" "released"`; `by`: the chr that opened the cycle (-2 a Bond or a script, -1 none). |
| `interlock.of(door)` | `group, slot` (slot 1-based), or nil. |
| `interlock.clear(g)` | Remove a group (plain doors again). |
| `interlock.lockdown(g [, secs])` | Hold every door shut (default 10 s; 0 releases). |
| `interlock.override(door [, secs])` | That door ignores its group (default 10 s). |
| `interlock.label(door)` | `text, rgba`: the AR word (`INTERLOCK`, `WAIT`, `WAIT (QUEUED)`, `VERIFYING`, `LOCKDOWN`, `CYCLE`, `NO POWER`, `RELEASED`, `OVERRIDE`), or nil. |

### Gadgets: `gadget.*`

designs/bank-heist.md section 21: `tools/heist_gadgets.lua` (the piton gun, lockpicks, coins, the tape loop, sleep gas, the thermal imager, the pickpocket, disguises, vents) on `port/src/port_gadgets.c`. Test mode `tests/heist/heist_gadgets.lua` (`tests/heist/run_gadgets.sh`).

| Call | Notes |
| --- | --- |
| `gadget.items({item, ...})` | The `ITEM_*` ids that are gadgets (up to 16). With one in a Bond's right hand his fire button is the script's: counted, and never passed to the stock gun code. Returns how many it took. |
| `gadget.fire(p)` | `presses, held, ticks`: fire presses since the last call (taken), whether fire is held now, for how many ticks. Read from the Bond's own synchronized buttons. |
| `gadget.now([p])` | `tick, press_tick`: the stage's sim tick, and the tick of Bond `p`'s last gadget fire press. |
| `gadget.aim(p)` | `yaw, pitch, x, y, z, feet`: the view in degrees (`vv_theta`, forward is `(-sin, cos)`), the eye position and the feet height; nothing when there is no such Bond. |
| `gadget.reel(p, {{x, y, z}, ...}, secs, from_pad, to_pad)` | Carry Bond `p` along up to 4 feet points in `secs` (held: no stick, no buttons), then set him down on `to_pad`. Two equal points hold him in place (a vent crawl). True when started. |
| `gadget.reeling(p)` | `on, frac, done`: on a line now, how far along, lines finished this stage. |
| `gadget.floor_point(p, yaw, dist)` | `x, y, z`: where a throw along `yaw` lands (the floor walked from Bond, short of walls and drops). |
| `gadget.chr_yaw(chr)` | `degrees, y`: where a chr faces (`atan2(dx, dz)`), and its height. |
| `gadget.lift_key(p, chr)` | The pickpocket: the chr's keys (not its gun) go to Bond `p` through the stock pickup. Returns the key flags (0: none). |
| `gadget.disguise(p [, kind])` | Set (0 none, 1 staffer's clothes, 2 guard's uniform) and return it. The cover reads it. |
| `gadget.bar(p, frac [, rgba])`, `gadget.marker(p, x, y, z)`, `gadget.xray(p, on)`, `gadget.dial(p, start, period, center, width)` | Presentation for that Bond's machine: a work bar (`frac = nil` hides it), a bracket on a point (no point hides it), the thermal imager's boxes on every chr within 35 m, the safe's dial (no `start` hides it). |

The trigger counts, the lines, the disguises and the item list are in the co-op / netplay state hash and the rollback snapshot.

### The planning board: `plan.*`

designs/bank-heist.md section 27 (`port/src/port_planning.c`, the board in `port/src/port_intel.c`; `tools/heist_planning.lua`). The hold and the input are sim state; the board is presentation, built from the plan's sim state and never read back.

| Call | Notes |
| --- | --- |
| `plan.hold(on)`, `plan.held()` | Hold the start: no Bond moves or fires, his buttons drive the board. |
| `plan.input(p)` | Bond `p`'s board events since the last call, taken, as a mask (1 up, 2 down, 4 left, 8 right, 16 select, 32 back, 64 start). |
| `plan.pointer(p)` | Bond `p`'s next board click, taken (a session command, so sim input on every machine): `a, b` with `a = page * 64 + row`, `b` 1 select, 2 back, 3 tab, 4 row up, 5 row down; nothing when there is none. |
| `plan.view(on [, page [, floor [, footer]]])` | The board up on that page and floor with that footer, or put away. |
| `plan.clear()`, `plan.at(page)` | Drop the plan's pages; the next `intel.rect` / `text` / `marker` ... go on the plan layer of that page. |
| `plan.pages(first, n)` | The plan's own tabs: only they show while planning; after it, only the pages before them. |
| `plan.hit(page, row, x0, y0, x1, y1)` | A clickable row of a plan page (board units; cleared by `plan.clear`). |
| `plan.strip(p, item)` | Take that item out of Bond `p`'s inventory (sim: the stage's own start gun). Raises on an unknown item. |

### The INTEL board: `intel.*`

designs/bank-heist.md section 19 (`port/src/port_intel.c`; the heist's content is `tools/heist_intel.lua`). Presentation only: the board is opened by this machine's player (the `intel` bind: Tab, Back on a pad), and its view never reaches a script's logic. Build the content on the script's own clock, never "when the board is open". Board units are 640 x 480 (x right, y down), centred on the screen; colours are `0xRRGGBBAA` (default white).

| Call | What it does |
|---|---|
| `intel.enable(on)` | This mode has a board: the `intel` key opens it (and wins Tab over Classic Mode, Back over Objectives). |
| `intel.clear([live])` | `live = true`: drop the live layer; otherwise drop everything (pages, floors, both layers). |
| `intel.title(text)`, `intel.page(i, title)` | The caption; page `i` (1 to 8) and its tab. |
| `intel.map_page(i)`, `intel.floor(k, name, wx1, wz1, s, bx0, by0)` | The page drawn per floor (up / down picks the floor); floor `k`'s transform: world `(x, z)` to board `(bx0 + (wx1 - x) s, by0 + (wz1 - z) s)`. |
| `intel.bond(p [, k])` | Bond `p` is on floor `k` (0 / omitted = none): the map draws him there, live. |
| `intel.at(page [, floor [, live]])` | Where the next items go: the page, the floor (0 = every floor), the live layer (`true`) or the static one. |
| `intel.rect(x0, y0, x1, y1 [, rgba])`, `intel.frame(x0, y0, x1, y1 [, rgba [, thick]])`, `intel.line(x0, y0, x1, y1 [, rgba [, thick]])`, `intel.tri(x0, y0, x1, y1, x2, y2 [, rgba])`, `intel.poly({x1, y1, ...} [, rgba])` | Filled rectangle; outline; line; filled triangle; filled convex polygon (up to 32 points). |
| `intel.text(x, y, text [, rgba [, scale [, wrap [, font]]]])` | Text; `wrap` is a width in board units (0 = none), `"\n"` breaks, `font` 1 is the big face. |
| `intel.marker(x, y, kind [, rgba [, size [, label]]])` | `dot ring x pin check box tri diamond cam stairs door star`. |
| `intel.briefing()` | Open the board once on this machine as the start-of-heist briefing. Skipped in automated runs (`GE_FRAMES`, `GE_INTEL_NOBRIEF`). |

`midas.ini [input] intel_hold=1` shows the board only while the key is held. `GE_INTEL_AUTO="<frame>:open|close|page<n>|floor<n>,..."` drives the view in a headless run.

### The DEBRIEF panel: `debrief.*`

designs/bank-heist.md section 26 (`port/src/port_debrief.c`; content from `tools/heist_debrief.lua`). Presentation only: a full-screen panel, this machine's, up until its player dismisses it.

| Call | What it does |
|---|---|
| `debrief.set{ kicker, rating, rating_rgba, flavour, payout, payout_note, title, rows = {{label, value [, rgba]}}, medals = {{name, desc, earned}} }` | Replace the panel's content. |
| `debrief.record{ difficulty, win, payout, time, rating, result }` | Count the run in `Data/Saves/heist_records.json` (next to the exe), save it, put NEW BEST / BEST TIME and the best line on the panel, log `HEIST RECORD {json}`. The file's contents never come back to the script. Call after `set`. |
| `debrief.show([on])` | Open (default) or close the panel on this machine. |
| `debrief.log(text)` | One log line of up to 2000 characters (`print` stops at 511). |

### The AR overlay and diagnostics: `ar.*`, `diag.*`

Presentation only (`port/src/port_arhud.c`; designs/bank-heist.md section 20). World markers drawn over the frame through the local Bond's camera. Colours are `0xRRGGBBAA` or a name (`red green blue gold white cyan yellow orange`). Without a mode calling `ar.on`, nothing is drawn; another script should guard calls with `if ar then`. The heist's wiring is `tools/heist_ar.lua`.

| Call | Notes |
| --- | --- |
| `ar.on([on])`, `ar.enabled()` | The specs (default on when called with no argument; Shift+F3 / console `ar` toggle them). |
| `ar.layer(name [, on])` | `suspicion`, `speech`, `keys`, `points`, `doors`, `cameras`, `radar`: set and return; raises on another name. |
| `ar.sound(on)` | Bubbles with or without their blip. |
| `ar.config{ range = 1800, cone = 75 }` | The sight cone the diagnostics draw. |
| `ar.mark(chr, opts \| label)` | A tag under the chr: `opts = { label, rgba, bracket }`. |
| `ar.say(chr, text [, secs [, kind]])` | A speech bubble (3 s default) with a ROM blip; `kind` `"hey"`, `"radio"`, `"huh"`, ... or 0..3. |
| `ar.outline(chr [, rgba])` | Bracket corners and a glow; no colour clears it. |
| `ar.clear([chr])` | The chr's mark, outline and bubble (no chr: every one). |
| `ar.point(x, y, z [, label [, rgba]])`, `ar.unpoint([label])` | A waypoint with its distance (the same label moves it), returns its id or nil; remove one, or all. |
| `ar.noise(x, y, z, radius [, kind])` | An expanding ring. |
| `ar.reset()` | Forget every mark, point, ring and label. |
| `diag.on()`, `diag.off()`, `diag.enabled()` | The diagnostics layer (F3, console `diag`). |
| `diag.door(idx [, label [, rgba]])`, `diag.room(id [, label])`, `diag.text(n [, text])` | A door's rule label; a room's name in the panel; panel lines 1..4. |

### Crosshair styles: `crosshair.*`

Presentation only (`port/src/port_crosshair.c`, `port/luavm/port_luavm_crosshair.c`). A style replaces the game's sight sprite with a shape drawn in code; with no style, no hide and no draw hook the game's own sight draws exactly as before. Works on any solo stage, in a mode or not.

There are two layers. A **script's** style (`crosshair.style`) lasts until the next stage start. The **player's** style (`crosshair.use`, the console, `GE_CROSSHAIR`) lasts for the session. The script's style wins while it is set.

| Call | Notes |
| --- | --- |
| `crosshair.style(opts)` | The script's style. `opts` is a table (below) or the name of a pack style. `nil` (or `false`) drops it. Raises on an unknown shape, colour, weapon or pack name. |
| `crosshair.use([name])` | The player's style, by pack name. `"off"` (the default) or `"classic"` returns to the game's sight. Returns false for an unknown name. |
| `crosshair.list()`, `crosshair.shapes()` | The pack's style names; the shape names. |
| `crosshair.hide([bool])`, `crosshair.hidden()` | No sight at all (`true` when called with no argument). Dropped at the next stage start. |
| `crosshair.reset()` | Drops the script's style and the hide. |
| `crosshair.current()` | `shape, who`: the shape drawn now (`"classic"` when no style is set) and whose style it is (`"script"`, `"player"` or nil). |
| `crosshair.spread()` | The dynamic gap last drawn, in HUD pixels. |

Style fields (all optional). Units are **HUD pixels**: the game's 240-line frame, so 1 is 3 window pixels at 720p, before the HUD Scale setting.

| Field | Default | Notes |
| --- | --- | --- |
| `shape` | `"cross"` | `"classic"` (the game's sprite), `"none"`, `"dot"`, `"cross"` (or `"plus"`), `"t"`, `"x"`, `"circle"` (or `"ring"`), `"chevron"`, `"brackets"` |
| `size` | 4 | arm length; the circle's radius; the dot's diameter |
| `gap` | 2 | space between the aim point and the arms |
| `thickness` | 0.75 | line width |
| `color` | `0xFFFFFFE6` | `0xRRGGBBAA`, a name (`white red green blue cyan yellow orange gold magenta black`) or `{r, g, b [, a]}` (0..255) |
| `dot`, `dot_size` | false, 1 | a centre dot as well; `dot = 0.8` sets its size |
| `outline`, `outline_color`, `outline_thickness` | false, `0x000000A0`, 0.4 | a dark rim under every part; `outline = <colour>` picks its colour |
| `dynamic` | false | the gap opens with the gun's real spread, movement and firing. A number sets `spread_scale` |
| `spread_scale`, `move_bloom`, `fire_kick` | 1, 3, 1.5 | x the weapon's spread; pixels at full run speed; pixels per shot, fading over 0.25 s |
| `hit_marker`, `hit_color`, `headshot_color`, `hit_time`, `hit_size` | false, white, red, 0.25, 3 | a short X when one of your shots hits someone, red for a headshot |
| `always` | true | also while not aiming. The game shows its own sight only in aim mode |
| `draw` | | `function(x, y, ctx)`: called every frame after the shape (as `draw_crosshair` below) |
| `weapons` | | `{ <weapon> = { ...fields } }`: per-weapon overrides (names as `weapon.items()`) |

**The draw hook.** `on("draw_crosshair", function(x, y, ctx) ... end)` runs every frame the sight could show (not during a damage flash or a cutscene, not in the menus, not after death), after the style. `x, y` is the aim point in HUD pixels from the top left. Draw with `ctx.line(x0, y0, x1, y1 [, thickness [, color]])`, `ctx.rect(x0, y0, x1, y1 [, color])`, `ctx.circle(x, y, r [, thickness [, color]])` (thickness 0 fills it), `ctx.dot(x, y [, size [, color]])`; `ctx:line(...)` works too. At most 256 calls a frame. Info fields: `ctx.spread`, `ctx.since_hit` (seconds since your last hit; nil before one), `ctx.headshot`, `ctx.aiming`, `ctx.weapon`, `ctx.player`, `ctx.w`, `ctx.h`.

Rules for draw code: it runs on the frame, not the tick, so its call count depends on the frame rate. Never change game state, never call `math.random`, and keep it short. An erroring hook is disabled and logged once. Hooks do not run during netplay.

```lua
-- Data/Scripts/my_crosshair.lua: runs at every solo mission start
crosshair.style{ shape = "cross", size = 3, gap = 1.5, color = "green", outline = true, dynamic = true, hit_marker = true,
  weapons = { sniperrifle = { shape = "dot", size = 1.2, dynamic = false } } }
on("draw_crosshair", function(x, y, ctx)
  if ctx.since_hit and ctx.since_hit < 0.15 then ctx.circle(x, y, 7, 0.5, "red") end
end)
```

**The pack.** `Data/Scripts/mods/crosshair_pack.lua` (from `tools/crosshair_pack.lua`) returns an array of styles, each a style table with a `name`. Pick one in the console with `crosshair <name>` (`crosshair` alone lists them; `crosshair off`, `hide`, `show`, `reload`), with `GE_CROSSHAIR=<name>`, or from a script with `crosshair.use(name)`. Tests: `tests/crosshair/run.sh`.

### Weapons, modifiers, music, menus, sounds

- `weapon.set(item, field, value)`, `weapon.get(item, field)`, `weapon.reset()`, `weapon.items()`, `weapon.fields()`. Items are lowercase `ITEM_*` names (`ak47`, `wppk`, `goldengun`, `throwknife`, ...) plus `pp7 pp7sil dd44 klobb kf7 zmg ar33 rcp90`. Fields: integers `magsize firerate singlerate penetration sound ammotype`; numbers `damage inaccuracy zoom crosshairspeed aimlockspeed sway recoilback recoilup boltrecoil loudnessmin loudnessmax noiseshot impact`. Range-checked: a bad item, field or value raises. Edits last until the next stage start.
- `mods.get(name)`, `mods.set(name, v)`, `mods.list()`: the modifier registry (the Mods menu page rows). `mods.list()` gives `{name, label, kind, value, min, max, step, default, desc}` rows. An unknown name raises. `mods.set` is not written to `midas.ini`.
- `music.set(name [, volume])`, `music.play(name [, volume])`, `music.list()`, `music.current()`: choose the level music. `set` must be called from a top-level script, before the music starts; from `stage_start` use `play`. An unknown track raises. The optional `volume` is 0..1 of full scale (clamped; NaN raises; 1.0 = 0x7FFF) and replaces that track's own default level (the game's per-track table, e.g. 0.8 = 0x6665 = the level tracks' default) until the next stage start; omitted, the track plays at its normal default. It survives the game's fades and never changes the vanilla table for later stages.
- `menu.add_toggle(label, get, set)`, `menu.add_number(label, min, max, step, get, set)`: add a row to the Mods page (max 24, labels 39 characters). Use from `Data/Scripts/boot/*.lua`. A callback runs on the machine that clicks it.
- `sound.play_file(name [, volume [, duck]])` plays `Data/Sounds/<name>.wav` over the game's audio and returns a voice id (nil when the file is missing). `sound.play_any(base [, volume [, duck]])` plays a random one of `base.wav`, `base1.wav` ... `base16.wav` and returns its file name. `sound.variants(base)`, `sound.file_exists(name)`, `sound.stop(id [, fade_seconds])`, `sound.playing()`. Presentation only. See arena.md, Crowd sounds.
- `sound.play_sfx(name_or_id [, volume [, pitch [, delay [, pan]]]])` plays one of the game's own 261 sound effects and returns a handle (nil when the name is unknown or the sound was dropped). `sound.sfx_burst(names, count [, opts])` plays `count` (a number or `{lo, hi}`) of the sounds in `names`, staggered (`opts`: `vol`, `pitch = {lo, hi}`, `gap = {lo, hi}`, `delay`, `pan`), with the C side's own RNG; returns how many it queued. `sound.sfx_names()`, `sound.stop_sfx(handle)`, `sound.sfx_playing()` (`playing, queued`). They never take a game sound's place. `GE_SOUNDLOG=1` logs each start and drop.

### Mode helpers: the player bot, the arena crowd, the benchmark, the profiler

- `bot.active()` (0 off, 1 driving, 2 watching), `bot.push(line)` (true, or `nil, message`: one plan step in the language of `modding/bot.md`), `bot.clear()`, `bot.step()` (`index, count`). A mode feeds the co-op player bot its plan.
- `crowd.impostors({x, y, z, ...}, {skin, ...})` (`{}` removes them), `crowd.cheer(seconds [, fraction])`: the Colosseum's flat spectators, presentation only (modding/arena.md).
- Arena render settings, presentation only (`port/src/port_perfcat.c`): `crowd.lod([units])` (real spectators beyond it drawn as impostors; returns the value in force), `crowd.batch([on])` (the chr batch), `crowd.bake([on])` (baked corpses). Benchmark: `bench.hide({"spectators", "corpses", "live", "objects", "fx", "impostors"})` (built, not drawn; `{}` = all), `bench.summary()` (the closing table), `bench.shot()` (a screenshot of the next frame).
- `arena.fog(far [, r, g, b])` (a grey fog with the far plane at `far`; `<= 0` = the level's own sky; `arena.fog()` reads the current far distance), `arena.strike(who, dx, dz [, explosion [, fuse]])` (the THUNDERBALL lightning, modding/arena.md: who = a chrnum or `{ bond = p }`; the point is walked along the floor (dx, dz) from the character's feet; after `fuse` seconds (0.6) a stock explosion, `"small" | "medium" | "grenade"` (default) `| "remote"` or an EXPLOSION_DEF number, goes off there with the flash and the bolt; returns x, y, z, serial or nil), `arena.debrief({lines})` (up to 8 lines on the Colosseum's report page; printable ASCII is kept and a run of 30+ characters without a space is broken; `{}` clears), `arena.sfx(id [, pitch])` (one stock sound effect by id), `arena.hud(on)` (the styled arena HUD in place of the plain mode-HUD rows), `arena.hud_layout(scale [, "top_left" | "top_right" [, margin]])` (the wave panel's size, 0.25..1.5 of the console font, corner and gap in console units), `arena.panel{wave, waves, title, mutator, kills, left, elites, time, phase = "delay" | "fight" | "breather" | "over", crowd (0..100, -1 off), tier (1..5), tier_name, toast, double_secs}` (the left wave panel), `arena.callout(text [, level [, sub [, secs]]])` (a centre callout, level 1..6; the newest replaces the last). All three are presentation (`port/src/port_arenahud.c`).
- `scores.get(key)`, `scores.set(key, v)`, `scores.all()`, `scores.enabled()`, `scores.path()`: the arena's `arena_scores.ini` (solo only: in co-op and netplay get finds nothing and set does nothing).
- `coop.rules{on, bleed, revive, radius, revive_health, grace, bleedout = "wave" | "run" | "end", debug, bot_skill, bot_reaction, bot_aim_error, bot_turn, bot_range}` (the partner bot's skill: `bot_skill` 1..5, default 5 = the bot as built, presets the four knobs (seconds, degrees, degrees a second, units); a knob given wins; all clamped, see modding/arena.md, Co-op), `coop.active()`, `coop.state(p)` (`"up" | "down" | "dead" | "out"`, seconds left, revive fraction), `coop.release(p)`, `coop.counts()` (`downs, revives, bleed_outs`), `coop.bot(on)` (local co-op only; false in netplay), `coop.bot_goal(p [, x, z])`, `coop.bot_info(p)` (`{target, act, ammo, weapon, shots, dry, blocked}`: dry = empty-gun clicks, blocked = ticks it held fire with another Bond in the line of fire): arena co-op (modding/arena.md, Co-op).
- `bench.*` (`run_start`, `note`, `begin`, `tag`, `pools`, `finish`, `look`, `sweep`, `sweep_off`, `last`, `exit`): the arena frame-time benchmark (modding/arena.md, `tools/mode_arena_bench.lua`).
- `simprof.phase(name)`, `simprof.count(key)`: only when `GE_PROFILE_SIM` is set (32-bit Linux builds); otherwise the table is nil (`tests/heist/run_simprof.sh`).

### Arena Whacky: `chrfx.*` (presentation) and `whacky.*` (sim)

`port/luavm/port_luavm_whacky.c`, `port/src/port_chrfx.c`, `port/src/port_whacky.c`; used by `tools/arena_whacky.lua` (modding/arena.md, Whacky).

- `chrfx.set(chr, {tint = {r, g, b} | name, amount = 0..1, alpha = 0..1, anim = "none" | "cycle" | "pulse" | "pulse2" | "flicker" | "shimmer", speed = cycles a second, lo = 0..1, tint2 = {r, g, b} | name})`: a colour wash over the chr (composed into its stan colour, the blender's fog colour; `amount` 1 = solid colour) and translucency (`alpha` < 1 draws it in the translucent pass, the stock fade-out path). `pulse` swings the wash between `lo` x amount and amount, `pulse2` between `tint` and `tint2`, `cycle` turns the hue wheel, `flicker` is a ghost (alpha sways, now and then drops to `lo`), `shimmer` is glass. Names: gold black shadow white ghost glass red orange yellow green cyan blue purple pink steel. `chrfx.clear(chr)`, `chrfx.clear_all()`, `chrfx.count()`. Presentation only: the sim never reads it, and it may differ per machine. Bond's own hands and gun are never drawn through it.
- `whacky.anim_rate([r])` (the Fast / Slow Animation cheats' global rate, 0.2..4, applied relative to each chr's own speed; reset at stage load), `whacky.linemode([on])` (the Line Mode cheat; returns the old state), `whacky.give_double(p, "rocket" | "grenade" | "rcp90" | "throwing_knife" | "knife" | "laser")` (a 2x cheat's pair, any Bond), `whacky.scale(chr, f)` (0.1..3: model and collision box).
- The minion launcher: `whacky.launcher(p, shots [, {count = 3, scale = 0.25, life = 15, health = 0.3, anim = 3, hostile = false, cap = 12}])` arms Bond p: each of his next `shots` rockets bursts into `count` tiny turbo knife throwers on his side (or, `hostile`, against him), invincible for 2.5 s while they hatch, gone after `life` seconds (they fade out). `whacky.launcher_shots(p)`, `whacky.minions_at(p, x, z [, n])` (n minions at a floor point Bond p can walk to), `whacky.minions()` (chrnums), `whacky.is_minion(chr)`, `whacky.minion_stats()` (`spawned, expired`), `whacky.minions_fade()`. Minions have chrnums 28000..31999 (a `chr_died` killer in that range is a minion) and are never arena enemies. All `whacky.*` change the sim: call them from deterministic mode code.

### The test table

The global `test` holds the hooks a test needs and a mode must not use. They work only where the library they came from works (most need a live stage and raise in the menus).

| Call | What it does |
| --- | --- |
| `test.walk(p, x, z)`, `test.walk_stop()` | Steer Bond toward `x, z` with the real stick for this tick (call it every tick); returns the distance left. Player 0's pad only. `walk_stop` releases the stick. |
| `test.face(p, x, z)` | Turn Bond `p`'s view onto a point. |
| `test.take_drop(p [, radius])` | Bond picks up the nearest loose weapon within `radius` (400) through the real pickup: `item, ammo_before, ammo_after` (`item` 0 = none near). |
| `test.press(button)` | Hold raw N64 `"a"`, `"b"`, `"z"`, `"cl"`, `"cr"` on pad 0 for 4 sim ticks (the real input path). Pad 0 is the LOCAL Bond in co-op. Raises on another button. |
| `test.use([p])` | One use-action tap in Bond `p`'s next tick (default 0), as the Port use key gives it; the same on every machine. |
| `test.fire(p, ticks [, taps [, delay]])` | Hold gadget fire for `ticks` sim ticks, or give `taps` presses (one every other tick) after `delay` ticks. |
| `test.slap(p, chr)` | Bond `p`'s slappers hit `chr` through the stock hit path, wherever he is. True if the hit landed. |
| `test.shot_noise(p, loudness)` | Bond `p` "fires" a shot of that loudness (the same code as a real shot: alerts every chr within `hearing * loudness * 100` units). |
| `test.gadget_used(p, name)` | Fires `gadget_used` for Bond `p` as if he used `doordecoder`, `bombdefuser`, `datathief`, `safecracker` or `lockexploder`. |
| `test.obj_damage(i, amount [, p])` | Object `i` takes `amount` damage through the stock damage path, as Bond `p`'s hit (default 0). True, or nil (no such object). |
| `test.grab(p [, chr])` | As a use press at nothing: Bond `p` takes a hostage (only `chr`, if given and it qualifies). |
| `test.freeze(on)` | No AI list runs. Changes the sim: refused (false) in co-op and netplay. |
| `test.plan_input(p, mask)` | Planning-board events for Bond `p` as if pressed (sim side). |
| `test.plan_mouse(x, y [, button])` | The board cursor to a point; button 1 / 2 clicks, 4 / 5 wheel notches. Returns `page, row` under it. |
| `test.intel_show(open [, page [, floor]])` | Set this machine's board view (tests and screenshots). |
| `test.crosshair_hit([head])` | A hit marker now. |
| `test.screenshot()` | A `GE_SHOT` capture of the next presented frame (none in a headless build). |
| `test.heard(chr [, clear])` | The chr is in the "heard Bond" state (a shot within its hearing in the last half second); `clear` resets it. Nil no such chr. |
| `test.fire_stats(chr)` | `fired, held`: shots the chr fired and shots it held for a civilian in the line this stage (the first 64 chrs that tried to shoot). |
| `test.has_item(p, item)` | Bond `p` carries that item (a `bond.give` name). Raises on an unknown item. |
| `test.hud_line(n [, p])` | The text of HUD line `n` Bond `p` sees (his own line, else the shared one), `""` none. |
| `test.hud_panel()` | The lines this machine's panel shows. |
| `test.rooms_drawn([p])` | The bg rooms view `p` drew last frame on this machine (presentation). |
| `test.cctv_info()` | This machine's last feed: `{cam, start, rooms = {...}, geom_only, chrs, bonds, frames [, offline, label, index]}` or nil. |
| `test.intel_count([live])`, `test.intel_view()` | Items in a board layer; this machine's view (`open, page, floor`). |
| `test.plan_count()`, `test.plan_tabs()` | Items on the plan layer; the tabs the board shows now (`first, n`). |
| `test.crosshair_draws()` | Frames a custom sight has drawn. |
| `test.debrief_open()`, `test.debrief_record_info([difficulty])`, `test.debrief_path()` | Whether the panel is up; `new_best, runs, wins, best_payout, saved` for a difficulty (default 1); the records file's path. |
| `test.chr_face(chr)` | The way a chr faces, degrees (-1000 none). |
| `test.hurt(chr, frac)` | Arena: a wound of `frac` of full health that never kills (blasts always kill a chr). |
| `test.shoot(chr, part, weapon [, p])` | Arena: a real shot by Bond `p` (default 0) landing on the chr's `part` through the stock hit path. Raises on an unknown weapon. |
| `test.coop_down(p)`, `test.coop_kill(p)` | Arena co-op: put Bond `p` down now; a real death past the downed rule. |

The heist mode's own test window is the `HEIST` global (`HEIST.force_jam()`, `HEIST.camera_switch(p)`, `HEIST.intel_lines(p)`, ...), with `HEIST_SETTINGS = { test = true }` for extra log lines.

### The heist mode

`tools/mode_heist.lua`, shipped as `Data/Scripts/modes/heist.lua` (designs/bank-heist.md). It runs on the generated Janus bank on the levelgen stage: `GE_MODE=heist GE_STAGE=levelgen GE_SKIPINTRO=1` (Windows: `Play Heist.bat`); on any other stage it sets nothing up. The second job, Le Grand Casino Janus (designs/job2-casino.md), uses the same mode on a build whose levelgen slot holds the casino (`-DGE_LEVELGEN_DIR=port/levelgen_casino`, `build_job2`; Windows: `launchers/Play Casino Job.bat`). Every count, timer and chance is in the settings table `S` at the top; a preset or test sets `HEIST_SETTINGS` before `include("heist")`. The layers (cover, intel board, AR, gadgets, interlocks, planning, combat, hostages, personalities, the debrief) are in designs/bank-heist.md sections 16-32. The player bot's heist driver is `tools/bot/heist_bot.lua` (`tests/heist/run_botheist.sh`). Tests: `tests/heist/run_*.sh` (each header says what it checks).

## Adding a binding (C side)

1. If it needs game types, add a plain-C helper to `port/src/` and declare it in `port/include/port_lua.h` (the luavm files cannot include the game's headers).
2. In a `port/luavm/port_luavm_*.c` file write a `static int` binding, validate arguments with `luaL_check*`, and register it in a `luaL_Reg` table. Follow "Conventions": the player first when the call is about a Bond, optional arguments last, nil for "no such thing", raise on an unknown name. A test hook goes in the `test` table: `portLuaAddTo(L, "test", reg)` (port_luavm.c).
3. Gate anything that changes the simulation on `portLuaGameAllowed()` and never call Lua from a render path. (The one exception is `draw_crosshair`: solo and co-op only, never in netplay, and its handlers may only draw.) New sim state goes into the co-op / netplay state hash and the rollback snapshot.
4. Document it here, and add it to `tests/lua/api_layout_test.lua` if it goes in the test table.

Lua is its own static library (`ge_lua`) because the game's headers shadow libc; that is why the glue is split in two halves.

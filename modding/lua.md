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
| `item_pickup` | `(objtype, id, qty, player)`: `objtype` is the `PROPDEF_*` number (4 key, 7 single magazine, 8 weapon, 19 multi-ammo box, 20 armour); `id` is the weapon / ammo type, or the **key flags** for a key; `player` is the 0-based index of the Bond who picked it up |
| `alarm` | `(on)`: 1 the alarm started, 0 it stopped (any cause: a camera, a guard, `alarm.start/stop`, the siren timing out) |
| `cctv_spotted` | `(camera, player)`: a camera saw Bond `player` for its full alarm time (`camera` is the setup index, see `cctv.*`). The stock alarm also goes off unless `cctv.alarm_on_spot(false)` |
| `use` | `(player, proptype, idx, objtype)`: Bond `player` pressed use on a door or object. `proptype` is `PROP_TYPE_*` (1 object, 2 door, 4 weapon), `idx` the target's setup index (-1 for a prop made at run time), `objtype` its `PROPDEF_*` number. Fires before the interaction runs, locked or not |
| `gadget_used` | `(player, item, idx, objtype)`: Bond `player` used a gadget (`item` is the `ITEM_*` number: door decoder 38, bomb defuser, data thief, floppy, data tape, safe cracker case, lock/door exploder, credit card, gas keyring) once, on the first frame of its use animation; `idx`/`objtype` as in `use`, for what the use prompt points at (-1 if nothing) |
| `objective_changed` | `(index, status)`: 0 incomplete, 1 complete, 2 failed |
| `body_found` | `(guard, body, ko)`: a guard found a body; `ko` is 1 for a knocked-out chr, 0 for a dead one (see "Heist guards") |
| `chr_knocked_out`, `chr_woken`, `chr_searched` | `(chr, player)`, `(chr, waker)`, `(chr, player)`: see "Knock-outs" |
| `test_key` | `(name)`: a test hotkey, `"next"` (PageDown), `"back"` (PageUp), `"next+"`, `"back+"` (with Shift), or the console `arena <name>`. Never from the keyboard in co-op or netplay. |

Chr ids are chrnums. Globals: `CHR_BOND = -2` (player 0; `bond.id(p)` gives the id of other Bonds) and `CHR_NONE = -1`. `damage`, `chr_spawned`, `item_pickup` and `objective_changed` also fire on an ordinary solo stage, not only in a mode. The heist events (`alarm`, `cctv_spotted`, `use`, `gadget_used`) fire wherever the game does the thing, but a script only runs in a mode.

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

### Heist bindings: doors, keys, alarm, CCTV, countdown

Generic building blocks for a heist-style mode (designs/bank-heist.md). Door and camera numbers are the **setup-record indices** that `probe.level()` logs (`ge: probe: DOOR idx=75`, `CCTV idx=95`); designs/bank-heist-bunker2.md lists them for Bunker 2. All of it runs identically on both machines in co-op; where a Bond is involved the call takes his player index `p`. Under a mode the level reset opens every door for Bond: call `mode.keep_locks()` first (top level) if keys are to matter. Locking a door does not stop a one-way door (flags2 bits 0x08000000 / 0x10000000, e.g. the keyed pairs on Bunker 2) opening from its free side, as in the stock game.

| Call | Notes |
| --- | --- |
| `door.list()` | Array of door setup indices. |
| `door.get(i)` | `{idx, state, open, keyflags, x, y, z, room, live, autoclose, linked, cell, padlocked}` or nil. `state` is `"closed"`, `"opening"`, `"open"` (open, or waiting to auto-close) or `"closing"`; `open` is 0..1; `autoclose` in ticks; `linked` the sibling door of a pair (-1 none). |
| `door.open(i)`, `door.close(i)` | Start the move whatever the lock says. `true` started, `false` already that way, nil no such door. |
| `door.lock(i, flags)`, `door.unlock(i [, flags])`, `door.set_keyflags(i, flags)` | Set / clear (all, when `flags` is omitted) / replace the key bits a Bond must hold to open it. A door with `keyflags == 0` is unlocked. |
| `door.try(p, i)` | Bond `p` uses door `i` as the use key would: the stock key check against **that Bond's** keys (plus the `use` event). `true` the door moved, `false` refused, nil no such door or Bond. Handy for scripted NPC use and tests. |
| `spawn.key(chr, flags [, model])` | Gives a spawned chr a key (a stock key assigned to the chr, so it drops where he dies and is picked up like any guard's key). `model` defaults to 86 (the swipe card; the stock keys use 86, 243 and 244). Returns a handle, or nil (no such chr, or it has no model yet; call it right after `spawn.chr`). |
| `spawn.key_at(pad, flags [, model])` | A floor key on a pad. |
| `spawn.key_alive(h)` | `alive, held, x, z`: whether the key is still in the world, whether a chr carries it, and where it is. A key a Bond picked up is no longer alive. Up to 64 keys per stage. |
| `bond.has_key(p, flags)`, `bond.keys(p)` | True when Bond `p` himself carries every bit of `flags`; the bitmask of all the key flags he carries. Never pooled, even in stock co-op. |
| `mode.per_player_keys([on])` | Co-op: a key opens doors only for the Bond who picked it up. The stock game pools every Bond's keys (`bondinv.c`); this is off by default and reset at each stage start. Call it at script top level or in `stage_start`. |
| `alarm.start()`, `alarm.stop()`, `alarm.active()` | The global alarm (siren and the guards' alert state, as a camera or a guard raising it). The `alarm` event fires on every change. |
| `cctv.list()` | Array of camera setup indices. |
| `cctv.get(i)` | `{idx, x, y, z, room, face, timer, alarm_time, disabled, destroyed, live, lookpad}`. `timer` is the seconds of unbroken sight so far; `alarm_time` the override in seconds (0 = stock: 10 s on Agent, 5 s above); `disabled` is true for a shot camera too, `destroyed` only for those. |
| `cctv.disable(i)`, `cctv.enable(i)` | Switch a camera off / on (it stops seeing and sweeping). `enable` does not revive a shot camera (returns false). |
| `cctv.set_alarm_time(seconds [, i])` | Sight time before a camera fires, for camera `i` or all cameras; 0 or nil restores the stock time. |
| `cctv.alarm_on_spot(on)` | With `false` a camera that spots a Bond only fires `cctv_spotted` and the script decides what happens (default true: the alarm also goes off). |
| `hud.countdown(seconds)` | Shows the stock mission countdown clock, set to `seconds` and running; `hud.countdown(nil)` hides and stops it. `hud.countdown_left()` gives the seconds left, or nil when it is not running. The clock keeps counting below 0 (shown as a positive number) if you do not stop it, so test `countdown_left()` yourself. |
| `bond.test_gadget(p, name)` | Test hook: fires `gadget_used` for Bond `p` as if he used `doordecoder`, `bombdefuser`, `datathief`, `safecracker` or `lockexploder`. |

Co-op notes. Stock cameras test only the Bond whose pass is running and keep one timer per camera, so two Bonds in view share the timer; the event names the Bond whose pass tipped it over. The state these calls set (key mode, camera overrides and switch-offs, the countdown) is folded into the co-op state hash. A test mode that exercises all of the above is `tests/heist/heist_bindings.lua`.

### Heist guards, bodies and civilians

Job 3 and 4 of designs/bank-heist.md. Test mode: `tests/heist/heist_guards.lua` (run with `tests/heist/run_guards.sh`).

**More `spawn.chr` options.**

| Option | Effect |
| --- | --- |
| `alarm_pad = pad` | The stock alarm raiser list (`GAILIST_SIMPLE_GUARD_ALARM_RAISER`): the guard stands still until it sees or hears Bond (or sees a guard shot or die), looks surprised, runs to `pad`, plays the alarm animation and sounds the alarm, then runs at Bond. Works on stages with no alarm box (a mode-only change to `chrTryStartAlarm`: no box at the pad is fine). The pad should be on the waypoint graph (`pads.far_from(..., true)`); it must be reachable. |
| `hearing = n` | `chr->hearingscale`. A shot of loudness L alerts the guard when Bond is closer than `hearing * L * 100` units (silenced PP7 first shot L = 1, unsilenced first shot 3, unsilenced at the cap 12; see designs/bank-heist-bunker2.md section 6). Default 1. |
| `vision = units` | Sight range (alias of `sight`). Default 15000. |
| `post = true` or `post = {x, z}` | Stand guard at the spawn pad (stock simple guard AI), facing `{x, z}` if given. Same as `behaviour = "hold"` plus `face`. An explicit `behaviour` wins. |
| `patrol = path_id` | Walk a stock patrol path of the stage's setup (`g_CurrentSetup.patrolpaths`, the ids `spawn.guard` takes). Applied a few ticks after the spawn. A missing path logs one line and the guard holds. Bunker 2's setup has none that the probe lists; check the stage you use. |
| `behaviour = "civilian"` | See below. |

**Chr slots.** Stage init runs before any script, so the slot count comes from the environment, not from Lua: `GE_MODE_SLOTS=n` (any mode) or any mode whose name starts with `heist` (250 extra slots, on top of the stage's own guards + 10; Bunker 2 gets 297 free). The display list and vertex pools grow with the slots above 60 (`dyn.c`, same as the Colosseum). The engine caps nothing else for `spawn.chr` (the 40 cap is `spawn.guard`'s); the limits are slots, the 600-prop pool and frame time (about 75 live chrs are free at 144 fps, idle ones cost almost nothing).

**Reinforcement waves.** `include("heist_lib")` (`tools/heist_lib.lua`, copy it next to the mode scripts) gives `heist.reinforce{pads = {...}, interval = 25, per_wave = 3, max_alive = 16, opts = {weapon = "ak47", behaviour = "chase"}, first = 0, reserve = 12}`: while `alarm.active()` it spawns `per_wave` guards every `interval` seconds at the entrance pads (round robin, a busy pad is skipped), never above `max_alive` alive and never when `spawn.free()` is below `reserve`. Returns `r` with `r.alive()`, `r.spawned`, `r.waves`, `r.is_wave(chr)`, `r.stop()`. The alarm event restarts the first-wave timer. The latch (the siren times out after about 30 s) is the mode's job: call `alarm.start()` again, or run waves from your own flag.

**Bodies.** Every ~8 ticks each living, non-civilian chr that is in the same room as a dead chr and within its sight range (capped at 1500 units, and within 150 units in height) notices it: its `chrseedie` is set, the stock "I saw someone die" field, so its own AI list reacts (simple guard and alarm raiser: run to Bond or to the alarm pad; the arena lists already hunt). The first one to notice a body fires `body_found(guard, body)`, once per body. Stock only alerts guards while the victim is still dying (and only setup guards, `get_numguards()`); this covers corpses and spawned guards. No facing test. Bodies must still be there: stock removes corpses about 1.5 s after death, so call `spawn.keep_bodies(true)` (or keep the body yourself). `mode.body_check(on)` turns the scan off (default on), `bodies.found()` counts bodies found this stage, `spawn.down(chr)` says whether a chr counts as a body. A knocked-out chr counts as a body too (`chrIsBody` in `port_heist.c`): `body_found(guard, body, 1)`, and see "Knock-outs" below for the guard who comes to wake it.

**Civilians.** `spawn.chr(pad, {behaviour = "civilian", body = "Civilian_1_Female"})`: unarmed (any `weapon` is ignored), idles at its post, in the hostage faction (guards and bots never target it, but any shot or knife that physically hits it hurts it), flagged `CHRFLAG_COUNT_DEATH_AS_CIVILIAN` (the stock casualty counter, `get_civilian_casualties()`, counts it too). Behaviour, driven per sim tick for every Bond (never the local player):

- Aimed at: a Bond who holds a gun and whose view ray passes within 90 units of it, within the aim range (default 800 units), for `aim` seconds (0.5) makes it surrender (hands up, the `surrender` animation, not the stock `GUARD_SURRENDERS` which would drop its items at once). Event `civilian_surrendered(chr, player)`.
- Still aimed at for `threat` seconds (2) while surrendered: it drops everything it carries (`chrDropItems`, the stock `CHR_DROP_ALL_CONCEALED_ITEMS`): a key from `spawn.key(civ, flags)` falls where it stands and is picked up like a guard's. Event `civilian_robbed(chr, player)`, once. Nobody aiming for 3 s: it lowers its hands.
- Gunfire nearby (any shot whose noise reaches it, the stock hearing check) makes it cower for `cower` seconds (4): `"cowering"`, the `startled` animation held. Aiming at a cowering civilian surrenders it as above.
- Death: `civilian_killed(chr, killer_player)` (-1 when the killer was not a Bond), and `civilian.killed()` counts them. Failing after N is the mode's job. A knock-out is not a death (below); killing a knocked-out civilian is.

| Call | Notes |
| --- | --- |
| `civilian.state(chr)` | `"idle"`, `"surrendered"`, `"cowering"`, `"knocked_out"`, or nil (not a civilian). |
| `civilian.killed()` | Civilians killed this stage. |
| `civilian.tune{range=, aim=, threat=, cower=}` | Aim distance in units, and the three times above. Unset keeps the current value. |
| `bond.test_noise(p, loudness)`, `spawn.heard(chr [, clear])`, `bond.face(p, x, z)` | Test helpers: Bond `p` fires a shot of that loudness (same code as a real shot); whether the chr heard a shot in the last half second; turn Bond's view onto a point. |

Known limits. `bond.face` is a test helper: turning the view onto a point and then away again hung the Xvfb test build once (it was not investigated; the test avoids it). An `alarm_pad` or `patrol` chr keeps the stock waypoint routes; every other spawned enemy still uses the arena's straight route toward Bond (so only those two kinds can reach a pad). Spawning needs a clear spot: a busy pad (a held guard still standing on it) makes `spawn.chr` return nil. A civilian's "holds a gun" test counts the slappers (`ITEM_FIST`) as armed, so aiming bare fists at one also makes it surrender.

`alarm.stop()` sticks: the stock raiser re-fires the siren every tick from frame 60 of its alarm animation to the end, so `alarm.stop()` ends that animation for any raiser past frame 60 (the stock end-of-animation step; its list then runs at Bond as it would have) and logs `ge: heist: alarm.stop: raiser chr N stopped at frame F`. A raiser still before frame 60 has not raised it yet, and still will.

Events: `body_found(guard, body, ko)`, `civilian_killed(chr, player)`, `civilian_surrendered(chr, player)`, `civilian_robbed(chr, player)`. The civilian table, found-body list and counters are in the co-op state hash.

### Knock-outs

Job 5 of designs/bank-heist.md. Test mode: `tests/heist/heist_ko.lua` (run with `tests/heist/run_ko.sh`).

**Slaps.** In a mode whose name starts with `heist`, Bond's slappers (`ITEM_FIST`, the punch that goes through the stock hit path, `handles_shot_actors`) knock a chr out on the second slap. The first slap plays the stock hit reaction but does **no damage**; the second does not hit at all: the chr falls (the stock `death_forward_face_down_soft` animation, about 4 s, which carries the body about 100 units forward) and holds the last frame. `mode.slaps_to_ko(n)` sets the count (0 = slaps are ordinary stock hits; the default is 2 in a `heist*` mode and 0 in any other mode, so the arena keeps stock slaps); reset at each stage start. Guns, knives and blasts are never slaps. Hostages, the co-op bot, invincible chrs (spectators) and the Bonds' bodies cannot be knocked out.

**Knocked out.** Not dead: no `chr_died`, no `civilian_killed`, not counted by `civilian.killed()` or the stock casualty counter, `spawn.alive` stays true and the chr keeps its slot. It runs no AI list, has no collision box (Bond walks over it) and no auto-aim. It **never wakes by itself**. A further slap does nothing; a gun still kills it, and that is an ordinary death (a killed knocked-out civilian counts). A hit reaction that gets it up is undone the next tick (it goes straight back to the lying pose). `spawn.down(chr)` is true, so `heist.reinforce` does not count it as alive.

**What it carries** (a key from `spawn.key`, its gun) stays on it. A Bond who presses use within about 1.3 m of it searches it: it drops what it carries (`chrDropItems`, as a robbed civilian) and `chr_searched(chr, player)` fires, once per knock-out. The use press is consumed only when it searched someone (hostages take the press first).

**Found and woken.** The body scan treats it as a body: the first guard to see it fires `body_found(guard, body, 1)`. With `mode.guards_wake(on)` (default on) that guard walks over (stock route, never the arena's route to Bond), stops about 90 units away, looks it over for 1.5 s and wakes it: it blends back up to standing, both go on alert (the stock "saw someone die" reaction of their own lists; a woken civilian cowers instead) and `chr_woken(chr, guard)` fires. A guard already fighting (attack actions) does not go. If the waker is killed, knocked out or removed on the way, the body can be found again by the next guard; if it is shot at, or walks 20 s without arriving, it gives up and goes on alert instead. Every other guard who sees the body reacts as to a corpse. With `mode.guards_wake(false)` a knocked-out chr stays down until a script wakes it.

| Call | Notes |
| --- | --- |
| `spawn.ko(chr [, p])` | Knock a chr out now (as if Bond `p` slapped it; omitted = a script, the event's player is -1). False if it cannot be knocked out. |
| `ko.is(chr)` | True while it is knocked out. |
| `ko.count()` | `down, total, woken`: knocked out now, knock-outs this stage, wakes this stage. |
| `ko.slaps(chr)` | Slaps counted toward its knock-out (back to 0 when it wakes). |
| `ko.wake(chr)` | A script wakes it (no guard; `chr_woken(chr, -1)`). |
| `ko.waker(chr)` | The guard on its way to wake it, or nil. |
| `ko.search(p)` | Bond `p` presses use where he stands: the chr searched, or nil. Same code as the use key. |
| `ko.test_slap(p, chr)` | Test hook: Bond `p`'s slappers hit `chr` through the stock hit path, wherever he is. |
| `mode.slaps_to_ko([n])` | Set (and return) the slap count; 0 = off. |
| `mode.guards_wake(on)` | A guard who finds a knocked-out chr comes to wake it (default true). |

Co-op: every slap names the Bond whose punch landed; the knock-out table (slaps, state, searched, waker and its timer) and both options are in the co-op state hash. Limits: the fall is always the one stock soft death animation, with none of the stock death code's wall checks, so a chr knocked out facing a wall may clip into it (not seen in the test); the woken chr blends up from lying (there is no stock get-up animation); the waker's "look it over" is the stock look-around animation, not a kneel.

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

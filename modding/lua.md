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

The build copies `tools/scripts_example.lua` to `Data/Scripts/example.lua` (all commented out: a tour of the `weapon` table) and the shipped modes (`arena`, `arena-endless`, `hostage`, `hostage-flatland`, `heist` with its `heist_lib`) into `Data/Scripts/modes/`. Edit them in place or write your own.

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
| `item_pickup` | `(objtype, id, qty, player)`: `objtype` is the `PROPDEF_*` number (4 key, 7 single magazine, 8 weapon, 19 multi-ammo box, 20 armour, 3 loot from `spawn.item "goldbar"/"money"`); `id` is the weapon / ammo type, or the **key flags** for a key; `player` is the 0-based index of the Bond who picked it up |
| `alarm` | `(on)`: 1 the alarm started, 0 it stopped (any cause: a camera, a guard, `alarm.start/stop`, the siren timing out) |
| `cctv_spotted` | `(camera, player)`: a camera saw Bond `player` for its full alarm time (`camera` is the setup index, see `cctv.*`). The stock alarm also goes off unless `cctv.alarm_on_spot(false)` |
| `cctv_view` | `(player, camera, why)`: Bond `player` now watches `camera` on the feeds, or `camera` is -1 when he stopped. `why`: 0 a script (`cctv.view` / `view_off`), 1 he cycled the cameras, 2 he left (B / use), 3 he was hit, 4 he went down. See "CCTV view" |
| `use` | `(player, proptype, idx, objtype)`: Bond `player` pressed use on a door or object. `proptype` is `PROP_TYPE_*` (1 object, 2 door, 4 weapon), `idx` the target's setup index (-1 for a prop made at run time), `objtype` its `PROPDEF_*` number. Fires before the interaction runs, locked or not. A use press with nothing in reach fires too, with `proptype`, `idx` and `objtype` all -1 ("use here" places: screens, a console) |
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
| `spawn.anim(chr, name [, force])` | One-shot idle animation: yawn, flies, scratch_leg, scratch_butt, adjust, sneeze, chat, listen, cough, laugh, startled, surrender, dance, look, fists, cross_arms. Without `force` it starts only on an idle chr. It returns to the idle pose. |
| `spawn.idle(chr)` | True when standing, or a one-shot animation has run out; nil when gone or dead. |
| `spawn.headshot(chr)` | True when the arena enemy's last damaging hit was to the head (ask in `chr_died`). `spawn.kill(chr, "head")` counts as one (test helper). |
| `spawn.hostage(pad)` | A hostage chrnum (max 16 a stage). |
| `spawn.guard(pad [, {patrol = path_id, weapon = "ak47"}])` | A guard that holds position (max 40 a stage). |
| `spawn.skins()`, `spawn.bodies()`, `spawn.weapons()` | The accepted names. |

`behaviour` is `"chase"` (default: run at Bond, stop and shoot in range), `"rush"`, `"hold"`, `"snipe"` (stand, fire from range, walk in when it loses sight), `"standard"` (stock guard AI) or `"spectator"` (harmless, invincible, untargetable: crowd decoration). Spawned chrs share a limited pool of chr slots; check `spawn.free()` before big waves.

`offset = {dx, dz}` spawns that far from the pad centre: the floor is walked from the pad's stan tile to the point (a wall, a ledge over 60 units or a gap refuses it: nil), and the pad centre itself may be taken, so several chrs can share one pad. Without it a pad with a chr standing on it refuses the spawn.

### Pickups

`spawn.item(pad, item [, {ammo = , amount = }])` places a real pickup on the floor, collected by walking over it. Returns a handle, or nil. `item` is a name from `spawn.weapons()` (the pickup carries that gun and its ammo), `"armour"` (`amount` 0..1), or `"ammo"` (an MP ammo box; `ammo = {pp7 = 50, kf7 = 60, shotgun = 16}`). Solo play doubles box ammo. Then `spawn.item_alive(h)`, `spawn.item_remove(h)`, `spawn.item_pos(h)`. Health has no stock pickup: use `bond.heal`.

Loot (heist job 6): `spawn.item(pad, "goldbar" | "money" [, {offset = {dx, dz}}])` places the stock beta pickup model (`PchrgoldbarZ` / `PchrmoneyZ`) as a plain prop, from its own pool of 160 (the 12 world pickups above and the 30 weapon slots are untouched). A living Bond within 90 units of it collects it: it goes, the stock ammo pickup sound plays and `item_pickup(3, id, 1, player)` fires (`objtype` 3 = `PROPDEF_PROP`, `id` 79 gold bar / 78 money). It never enters the inventory; the mode counts it. `offset` works as for `spawn.chr`. The handles (above 0x1000000) work with `spawn.item_alive / item_remove / item_pos`. It has no line-of-sight test: do not put it within 90 units of a wall you mean to keep Bond behind (a safe's contents should appear when its door opens).

### Bonds

`p` is a 0-based player index (0 = the host's Bond in co-op). **A missing `p` is always player 0, never the local player.**

| Call | Notes |
| --- | --- |
| `bond.count()` | 1 solo, 2 in co-op, 0 outside a mode. |
| `bond.pos([p])`, `bond.dead([p])` | `x, y, z` (nil if dead or not in play); dead flag. |
| `bond.nearest(x, y, z)` | `player, distance`. |
| `bond.all_down()` | True when nobody can respawn. |
| `bond.id(p)`, `bond.player(chr)` | Chr id of Bond p, and back. |
| `bond.give(p, item [, ammo])`, `bond.give_ammo(p, frac)` | Give a gun (it goes in the inventory, not in his hand); top up ammo of the guns he carries. |
| `bond.equip(p, item)`, `bond.weapon(p)` | Draw `item` into his right hand (the stock weapon switch, about a second; given first if he lacks it; names as `bond.give`, plus `"fist"`); the `ITEM_*` id in his right hand (0 none, 1 slappers, 5 silenced PP7, 38 door decoder). |
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
| `mission.difficulty()` | 0 Agent, 1 Secret Agent, 2 00 Agent, 3 007. |

There is no save API for modes: a mode that wants a best score keeps it in a global (the Lua state survives stage reloads within a session, `heist` uses `HEIST_BEST`) and logs it.

### Heist bindings: doors, keys, alarm, CCTV, countdown

Generic building blocks for a heist-style mode (designs/bank-heist.md). Door and camera numbers are the **setup-record indices** that `probe.level()` logs (`ge: probe: DOOR idx=75`, `CCTV idx=95`); designs/bank-heist-bunker2.md lists them for Bunker 2. All of it runs identically on both machines in co-op; where a Bond is involved the call takes his player index `p`. Under a mode the level reset opens every door for Bond: call `mode.keep_locks()` first (top level) if keys are to matter. Locking a door does not stop a one-way door (flags2 bits 0x08000000 / 0x10000000, e.g. the keyed pairs on Bunker 2) opening from its free side, as in the stock game.

| Call | Notes |
| --- | --- |
| `door.list()` | Array of door setup indices. |
| `door.get(i)` | `{idx, state, open, keyflags, x, y, z, room, live, autoclose, linked, cell, padlocked}` or nil. `state` is `"closed"`, `"opening"`, `"open"` (open, or waiting to auto-close) or `"closing"`; `open` is 0..1; `autoclose` in ticks; `linked` the sibling door of a pair (-1 none). |
| `door.open(i)`, `door.close(i)` | Start the move whatever the lock says. `true` started, `false` already that way, nil no such door. |
| `door.lock(i, flags)`, `door.unlock(i [, flags])`, `door.set_keyflags(i, flags)` | Set / clear (all, when `flags` is omitted) / replace the key bits a Bond must hold to open it. A door with `keyflags == 0` is unlocked. |
| `door.try(p, i)` | Bond `p` uses door `i` as the use key would: the stock key check against **that Bond's** keys (plus the `use` event). `true` the door moved, `false` refused, nil no such door or Bond. Handy for scripted NPC use and tests. |
| `door.one_way(i [, false])` | Whether door `i` is a stock one-way door (flags2 0x10000000 / 0x08000000: it opens from its free side whatever the key). With `false` both bits are cleared first (returns the state before), so only the key opens it from either side. Bunker 2's keyed pairs 58-61 and 68-71 and the blast doors 80-85 are one-way. |
| `door.snap(i, open)` | Put door `i` and its linked doors fully open (`true`) or shut (`false`) at once, even with a chr in the doorway (a blocked door in the stock game just stops where it is). The portal opens / closes as a stock move would. |
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

Co-op notes. With more than one Bond a camera tests **every** living Bond (the stock test read only the current player, the sim pass's Bond), each on his own sight timer on that camera (`port_heist.c` `portHeistCctvAllBonds`; solo keeps the stock code); `cctv_spotted(cam, p)` names the Bond whose own time ran out, and `cctv.get(i).timer` shows the longest. The state these calls set (key mode, camera overrides and switch-offs, the per-Bond sight timers, the countdown, each Bond's cards) is folded into the co-op state hash. A test mode that exercises all of the above is `tests/heist/heist_bindings.lua`.

| More (P3) | |
| --- | --- |
| `bond.use_key(p)` | Inside a `use` handler: true when the press is the **use action** (the Port use bind), false for the B button (which is use *and* reload). The heist opens its camera feeds only on the use action. |
| `mode.state(text)` | The mode's own sim state as one string, set every tick (the heist: phase, crack progress and jams, the vault clock, loot, waves, ...). Its hash is part of the co-op / netplay state hash ("flags") and the text is in the desync dump, so a script-state desync is caught on the tick and named. |
| `cctv.test_use([p])` | Test hook: one use-action tap in Bond `p`'s next tick (default 0), as the Port use key gives it; the same on every machine. |
| `hud.line(n [, p])` | Test reader: the text of HUD line `n` Bond `p` sees, `""` none. |

### CCTV view (camera feeds) and the HUD panel

designs/bank-heist.md P1 (port/src/port_cctvview.c, port/luavm/port_luavm_cctv.c). A Bond watching a camera sees the world from it: full room geometry (a portal walk from the camera), the props the game drew this frame, and every chr in the camera's rooms posed for the feed. He stands where he is, at the mercy of the guards: his stick, buttons, mouse look and Port binds do nothing else, and being hit throws him out.

| Call | Meaning |
| --- | --- |
| `cctv.view(p, cam)` | Bond `p` watches camera `cam` (a setup index from `cctv.list()`). An off camera (shot, or `cctv.disable`) shows static and "NO SIGNAL". False: no such camera, or he is down. |
| `cctv.view_off(p)` | Stop watching; true if he was. |
| `cctv.viewing(p)` | The camera he watches, or nil. |
| `cctv.name(cam [, text])` | The label the feed shows (default `CAMERA n`); set it with `text`. |
| `cctv.sees(cam, chr)` | True when a working camera has the chr (or `bond.id(p)`) inside its live cone (the stock test: 45 degrees either side of where the head points now, a clear stan line). It reads only; the stock sight timer is untouched. |
| `cctv.render_info()` | This machine's last feed: `{cam, start, rooms = {...}, geom_only, chrs, frames, offline, label, index}` (tests). `start` is the room the feed's walk began in (the camera's), `geom_only` the rooms drawn without their props, `chrs` the chrs posed for the feed. |
| `cctv.test_press(button)`, `cctv.test_shot()` | Test helpers: hold raw N64 `"a"`, `"b"`, `"z"`, `"cl"`, `"cr"` on pad 0 for 4 sim ticks (the real input path; it counted polls, which made the press land on a different tick from run to run); a GE_SHOT capture of the next frame. Pad 0 is the LOCAL Bond in co-op. |
| `hud.panel(lines [, p])` | Up to 8 lines (63 characters each) in a box at the top right; `nil` or `{}` clears it. With `p` only that Bond's machine draws it; a Bond with a panel of his own sees only his. `hud.panel_lines()` gives what this machine draws (tests). |
| `bond.room(p)`, `spawn.room(chr)` | The room Bond `p` stands in, the room a chr is in (nil when gone). |

Controls while watching (GoldenEye's own buttons): **A** or **C-right** next camera, **C-left** previous camera (the stick pushed left / right too: on PC, the strafe keys), **B** leave. Port binds: next / previous weapon or inventory = next / previous camera, use or menu back = leave. The order is `cctv.list()`'s. The gun, crosshair and ammo counter are hidden; the feed shows the camera's name and its place in the list, a blinking REC dot, the mission clock, and the controls. The panel stays; the heist clears its own HUD lines while a Bond watches (they would sit over the picture) and puts them back when he leaves.

How it works, and why it is safe for co-op. Which camera a Bond watches is sim state: Lua sets it (both machines run the same script), his own synchronized input cycles and leaves it, it holds him, and it is in the heist state hash. The picture is presentation only, on the watching Bond's machine: the sim keeps Bond's canonical camera, and his pass computes everything (visibility, prop and chr matrices, onscreen flags, autoaim) from it as always. Three things change after the sim is done with them: the room list the world draw uses (the camera's own portal walk, run before the sim walk, with rooms the sim did not reach drawn as geometry only); the two fixed-point projection matrices the display list loads (rewritten so Bond-camera space lands in the camera's, `P_cam * V_cam * V_bond^-1`); and the gun and HUD display lists (dropped). The chrs only the camera sees are posed into fresh matrices and drawn, and their Model struct and rw data are put back byte for byte; that draw also puts back the sim RNG (a gun's muzzle flash draws from it) and takes its matrices and vertices from a corner at the top of the frame's pool, so the sim's own draw allocates exactly as on a machine with no feed (each of the three was a co-op desync, found by `tests/heist/run_determinism.sh`). Rooms the camera sees get loaded like the co-op render walk's margin rooms. Limits: the camera's lens is a fixed 62-degree fov, tilted onto the pad it watches and turning with its live sweep; a chr standing right under a camera can fill the screen; the room's static lighting and environment mapping still follow Bond's camera (cosmetic).

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

**Reinforcement waves.** `include("heist_lib")` (`tools/heist_lib.lua`, copy it next to the mode scripts) gives `heist.reinforce{pads = {...}, interval = 25, per_wave = 3, max_alive = 16, opts = {weapon = "ak47", behaviour = "chase"}, first = 0, reserve = 12}`: while `alarm.active()` it spawns `per_wave` guards every `interval` seconds at the entrance pads (round robin; a busy pad is tried a step aside, `offsets`, default 0 and 110 units each way, before the next pad: the heist's hall pads all have guards posted on them, and its waves never came in before this), never above `max_alive` alive and never when `spawn.free()` is below `reserve`. Returns `r` with `r.alive()`, `r.spawned`, `r.waves`, `r.is_wave(chr)`, `r.stop()`. The alarm event restarts the first-wave timer. The latch (the siren times out after about 30 s) is the mode's job: call `alarm.start()` again, or run waves from your own flag: `active = function() return hot end` replaces `alarm.active()` as the "waves run" test. `opts` may also be a function returning a fresh `spawn.chr` table per guard (mixed bodies and guns), and `r.wave_now()` spawns a wave at once.

**Bodies.** Every ~8 ticks each living, non-civilian chr that is in the same room as a dead chr and within its sight range (capped at 1500 units, and within 150 units in height) notices it: its `chrseedie` is set, the stock "I saw someone die" field, so its own AI list reacts (simple guard and alarm raiser: run to Bond or to the alarm pad; the arena lists already hunt). The first one to notice a body fires `body_found(guard, body)`, once per body. Stock only alerts guards while the victim is still dying (and only setup guards, `get_numguards()`); this covers corpses and spawned guards. No facing test. Bodies must still be there: stock removes corpses about 1.5 s after death, so call `spawn.keep_bodies(true)` (or keep the body yourself). `mode.body_check(on)` turns the scan off (default on), `bodies.found()` counts bodies found this stage, `spawn.down(chr)` says whether a chr counts as a body. A knocked-out chr counts as a body too (`chrIsBody` in `port_heist.c`): `body_found(guard, body, 1)`, and see "Knock-outs" below for the guard who comes to wake it.

**Civilians.** `spawn.chr(pad, {behaviour = "civilian", body = "Civilian_1_Female"})`: unarmed (any `weapon` is ignored), idles at its post, in the hostage faction (guards and bots never target it, but any shot or knife that physically hits it hurts it), flagged `CHRFLAG_COUNT_DEATH_AS_CIVILIAN` (the stock casualty counter, `get_civilian_casualties()`, counts it too). Behaviour, driven per sim tick for every Bond (never the local player):

- Aimed at: a Bond who holds a gun or a knife (`ITEM_KNIFE` .. `ITEM_ROCKETLAUNCH`; the slappers, bare hands and gadgets are no threat), 40 units to the aim range (default 800) ahead of it, whose view ray passes within 30 units + 4% of the distance of it (about a body width: standing next to one with a gun out, or pointing past one, does not count), with a line of sight (same room, or the stock guard "can see Bond" stan line: walls and closed doors block it), for `aim` seconds (0.5) makes it surrender (hands up, the `surrender` animation, not the stock `GUARD_SURRENDERS` which would drop its items at once). Event `civilian_surrendered(chr, player)`.
- Still aimed at for `threat` seconds (2) while surrendered: it drops everything it carries (`chrDropItems`, the stock `CHR_DROP_ALL_CONCEALED_ITEMS`): a key from `spawn.key(civ, flags)` falls where it stands and is picked up like a guard's. Event `civilian_robbed(chr, player)`, once. Nobody aiming for 3 s: it lowers its hands.
- Gunfire nearby (any shot whose noise reaches it, the stock hearing check) makes it cower for `cower` seconds (4): `"cowering"`, the `startled` animation held. Aiming at a cowering civilian surrenders it as above.
- Death: `civilian_killed(chr, killer_player)` (-1 when the killer was not a Bond), and `civilian.killed()` counts them. Failing after N is the mode's job. A knock-out is not a death (below); killing a knocked-out civilian is.

| Call | Notes |
| --- | --- |
| `civilian.state(chr)` | `"idle"`, `"surrendered"`, `"cowering"`, `"knocked_out"`, or nil (not a civilian). |
| `civilian.killed()` | Civilians killed this stage. |
| `civilian.tune{range=, aim=, threat=, cower=}` | Aim distance in units, and the three times above. Unset keeps the current value. |
| `bond.test_noise(p, loudness)`, `spawn.heard(chr [, clear])`, `bond.face(p, x, z)` | Test helpers: Bond `p` fires a shot of that loudness (same code as a real shot); whether the chr heard a shot in the last half second; turn Bond's view onto a point. |

Known limits. An `alarm_pad` or `patrol` chr keeps the stock waypoint routes; every other spawned enemy takes the arena's straight route toward Bond when the line is clear and the stock waypoint route otherwise. Spawning needs a clear spot: a busy pad (a held guard still standing on it) makes `spawn.chr` return nil unless you give an `offset`. Before job 6, `atan2f` and `fabsf` were undeclared in `port_heist.c` (the shadow `include/math.h` declares only `sinf`/`cosf`/`sqrtf`, and the build runs with `-w`), so `bond.face` set a garbage view angle and the civilian aim test read garbage: that was the "civilians robbed with nobody aiming" report and probably the old `bond.face` hang. Both are fixed.

**Crowd limits (job 6).** A heist mode (or `GE_MODE_SLOTS`) also gets 410 more object model slots (stock: the setup's objects + 30; every gun a spawned chr holds, every key and every loot pickup takes one) and, once the 30 stock weapon slots are all held, a port pool of 320 weapon records (`weaponCreate`), so guard 31 onwards is armed instead of empty-handed (logged every 25th as `ge: heist: extra weapon slot N of 320`).

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

### The heist mode

Job 6 of designs/bank-heist.md: `tools/mode_heist.lua`, shipped as `Data/Scripts/modes/heist.lua`. Run it on Bunker 2: `GE_MODE=heist GE_STAGE=bunker2 GE_SKIPINTRO=1` (Windows: `Play Heist.bat`). The Janus front bank in St Petersburg: about 105 guards on post on Agent (more on harder difficulties), alarm raisers at their area's panel, bank staff, four card carriers (GREEN: the head teller or the lobby captain, chosen at random; BLUE: the security chief; RED: the branch manager; GOLD: the vault captain), Bunker 2's doors re-keyed as tiers, the stock cameras, a cracker with jams and a time-locked vault, gold bars and cash, two exits. Every count, timer and chance is in the settings table `S` at the top; a preset or test sets `HEIST_SETTINGS` before `include("heist")`. The table `HEIST` is its test window (phase, loot, carriers, zones, `HEIST.force_jam()`, `HEIST.camera_switch(p)`, `HEIST.watch(p [, cam])`, `HEIST.intel_lines(p)`). The banking hall (room 6) is the control room: the use action (the Port use bind, not B, which is also reload) at one of its screens (the monitor wall at its west side, the two desk screens) puts that Bond on the camera feeds, and while he is in room 6 or on a feed the HUD panel shows the intel (cameras up, alarm, the vault, where a working camera last saw each card carrier, guard counts in the areas a working camera covers, "?" elsewhere). The security console (BLUE card) still switches every camera off for 90 s. Tests: `tests/heist/run_winpath.sh` (win path, trapped-in-the-vault path, civilian-limit fail path), `tests/heist/run_cctv.sh` (the feeds and the intel); screenshots: `tests/heist/run_shots.sh`. The flow and the map are written up at the top of the script and in designs/bank-heist.md section 9.

### Weapons, modifiers, music, menus

- `weapon.set(item, field, value)`, `weapon.get(item, field)`, `weapon.reset()`, `weapon.items()`, `weapon.fields()`. Items are lowercase `ITEM_*` names (`ak47`, `wppk`, `goldengun`, `throwknife`, ...) plus `pp7 pp7sil dd44 klobb kf7 zmg ar33 rcp90`. Fields: integers `magsize firerate singlerate penetration sound ammotype`; numbers `damage inaccuracy zoom crosshairspeed aimlockspeed sway recoilback recoilup boltrecoil loudnessmin loudnessmax noiseshot impact`. Edits last until the next stage start.
- `mods.get(name)`, `mods.set(name, v)`, `mods.list()`: the modifier registry (the Mods menu page rows: invincibility, damage scales, one-hit kills, enemy knives, level music, ...). `mods.set` is not written to `midas.ini`.
- `music.set(name)`, `music.play(name)`, `music.list()`, `music.current()`: choose the level music. `set` must be called from a top-level script, before the music starts; from `stage_start` use `play`.
- `sound.play_file(name [, volume [, duck]])` plays `Data/Sounds/<name>.wav` over the game's audio and returns a voice id (nil when the file is missing or unreadable). `sound.play_any(base [, volume [, duck]])` plays a random existing one of `base.wav`, `base1.wav` ... `base16.wav` and returns its file name. `sound.variants(base)` counts (and loads) them; `sound.file_exists(name)`, `sound.stop(id [, fade_seconds])`, `sound.playing()`. Volume is 0..1 times the game's sound effects volume; `duck` (0..1) lowers the other playing sounds to that level for 1.5 s. Any WAV format, converted to the output's 22050 Hz stereo; 6 heard at once. Presentation only and different on every machine (each has its own files): never steer the game with what these return. Works anywhere. See arena.md, Crowd sounds.
- `sound.play_sfx(name_or_id [, volume [, pitch [, delay [, pan]]]])` plays one of the game's own 261 sound effects and returns a handle (nil when the name is unknown or the sound was dropped). The name is the `SFX_ID` enum name in `src/bondconstants.h`, with or without `_SFX`, any case (`"GET_HIT_MALE3"`, `"evil_laugh"`), or its number (the enum order, `GET_HIT_MALE0` = 134). Volume 0..1 times the game's sound effects volume; pitch is a speed multiplier (1 = as recorded, 0.8 lower and slower, 1.3 higher and faster); delay in seconds (timed on the audio clock, not the mode's tick); pan -1 (left) .. 1 (right). `sound.sfx_burst(names, count [, opts])` plays `count` (a number, or `{lo, hi}` for a random count) of the sounds in `names`, each once before any repeats, in a random order, staggered; `opts`: `vol` (1), `pitch = {lo, hi}` (0.85..1.25), `gap = {lo, hi}` seconds between starts (0.03..0.15), `delay` before the first (0), `pan` spread (0.8). It returns how many it queued. The picks, pitches, pans and gaps use the C side's own RNG, so a burst never touches `math.random`. `sound.sfx_names()` lists the names (index = id); `sound.stop_sfx(handle)`; `sound.sfx_playing()` returns the playing and the queued count. These go through the game's sound player, but never take a game sound's place: when the player is full (8 x `GE_SNDSCALE` sounds, 16 by default) a new one is dropped rather than cutting gunfire, a game sound cuts one of these first, and at most that many of these are under way at once (the rest are dropped). Looped sounds (alarms, engines) loop until `stop_sfx`. Presentation only: never steer the game with them. `GE_SOUNDLOG=1` logs each start and drop.
- `menu.add_toggle(label, get, set)`, `menu.add_number(label, min, max, step, get, set)`: add a row to the Mods page (max 24, labels 39 characters). Use from `Data/Scripts/boot/*.lua`.

The stage-touching tables (`bond`, `spawn`, `zone`, `hud`, `mission`, `hostages`, `pads`) are inert outside a mode (nil, 0 or false) and raise in the menus. `mode`, `mods`, `weapon` and `menu` work anywhere.

## Adding a binding (C side)

1. If it needs game types, add a plain-C helper to `port/src/port_lua.c` (or `port_arenamode.c`) and declare it in `port/include/port_lua.h`.
2. In `port/luavm/port_luavm.c` (or `port_luavm_arena.c`) write a `static int portLuaXxx(lua_State *L)`, validate arguments with `luaL_check*`, and register it in a `luaL_Reg` table.
3. Gate anything that changes the simulation on `portLuaGameAllowed()` and never call Lua from a render path.

Lua is its own static library (`ge_lua`) because the game's headers shadow libc; that is why the glue is split in two halves.

# Bank Heist: design

Status: design only. Nothing here is built. Every "stock" claim below cites the code it was read from, so it can be checked. Where something was read but not yet run, it says **verify in P0**.

---

## 1. The pitch and the core loop

**Get in, get the money, get out.** A bank full of guards, cameras and nervous staff. You start outside with a silenced pistol and a cracker. Done quietly, it is a stealth run: you use the cameras, slap or threaten staff for keycards, and kill only the guards you must. Done loudly, the alarm goes off, the siren wails, and squads keep pouring in through the front doors until you leave. Solo first. Co-op is the goal: one Bond watches the cameras while the other works.

**Core loop (one run, 10 to 20 minutes):**

1. **Case the place.** Lobby, staff and patrols. Find the security room.
2. **Climb the keycard ladder.** Each gate needs the next card. Cards come from a **specific guard** (kill him and it drops) or a **specific employee** (aim at them until they hand it over, or slap them out).
3. **Use the control room.** The monitors show who carries which card, and a switch blinds the cameras.
4. **Crack the vault.** Plant the cracker at the vault door and stay near it while it works. Guards come at you during the crack.
5. **Smash and grab.** The vault door opens on a timer. Grab bullion from the cage inside before the door closes again.
6. **Make like bandits.** There are several exits, each with a catch.

**Fail states:** every Bond is down; you kill a civilian (the exact rule is an open question); you are trapped in the vault when it shuts (also open).

**Pressure:** the alarm escalation (section 4). A quiet run is fast and tense. A loud run is a siege.

---

## 2. Mechanic by mechanic

Effort: **S** about a day of agent work, **M** a few days, **L** a week or more. Risk covers playability bugs, crashes and co-op desync.

> **Headline: the stock game already has most of this heist.** The CCTV, alarms, keycard doors, guards who run to alarms, hearing radius, surrendering staff, dropped items, safes, padlocks, the door decoder and a civilian-death counter are all real code that ships with the game. On top of that, **a Lua mode keeps the level's objects**: only guards, objectives and level AI are skipped (`src/game/prop.c:2352` guards, `prop.c:2656` objectives, `port/src/port_mode.c:90` logs "no chrs, objectives or level AI"). So on a stock stage the doors, keyed locks, cameras, alarm boxes, monitors and safes are all still there and still work. `modding/lua.md` line 19 says objects are wiped too. The code says otherwise, so that line should be fixed once P0 confirms it.

| Idea | Stock support found | What to build | Effort | Risk |
|---|---|---|---|---|
| **CCTV cameras that spot you** | Fully stock and ticked by the object system, not by level AI, so they keep working in a mode. Each camera sweeps, sees in a **45° half-cone** (`src/game/propobj.c:5647`) out to a range from setup, does a stan line-of-sight test, and adds up seen time. At **300 ticks** (about 5 s, `propobj.c:162`) it calls `alarmActivate()` (`propobj.c:5660`). Shooting the glass does 100× damage (`propobj.c:10570`) and sets `PROPFLAG_CCTV_DISABLED` (`propobj.c:10219`). Bunker 2 has 6 cameras and Bunker 1 has 4 (setup record counts). | Lua: `cctv.list()`, `cctv.disable(id or all, seconds)` (set or clear the flag), and an event `cctv_spotted(cam, player)` at the alarm moment. Co-op fix: the check reads only `getCurrentPlayerProp()` (`propobj.c:5590`) and must test every Bond. | S | Low (solo) / M (co-op fix) |
| **Monitors in a control room** | Stock monitor objects (`PROPDEF_MONITOR`, `PROPDEF_MULTI_MONITOR`, hanging racks) run a small animated-texture bytecode (`process_monitor_animation_microcode`, `propobj.c:7423`) with a stock image bank (`monitorSetImageByNum`, `propobj.c:7251`). Level AI swaps screens with `TV_CHANGE_SCREEN_BANK` (`src/aicommands.def:9944`; Facility uses it, `assets/obseg/setup/UsetuparkZ.c:3664`). Bunker 2 has 37 single monitors and Bunker 1 has 31. | A `use` event on objects (below) so pressing use at a console does something. A `monitor.set(tag, screen, image)` binding to drive screens from Lua. | S | Low |
| **Camera feeds you can watch** | **Not stock as a live feed.** What exists: the fixed-camera cutscene machinery (`CAMERA_SWITCH`, `aicommands.def:10189`; `CAMERA_LOOK_AT_BOND_FROM_PAD`, `:10143`), but it is the end-of-mission `CAMERAMODE_POSEND`, which pauses mission time. The port can render a model into an FBO once (the crowd atlas, `port/fast3d/gfx_opengl.cpp:3084-3167`, `port/src/port_crowd.c`). | Three tiers, cheapest first: **T1 intel overlay**: at a console, HUD text names each camera's room, guard count and who carries a card. Pure Lua plus a `chrs in room` query. **T2 "security feed" view**: a full-screen, render-only camera at the CCTV position. It draws geometry plus impostor markers for chrs from the crowd atlas (green-tinted, like a thermal feed), not real props. **T3** live render-to-texture onto an in-world monitor quad. See the two-worlds note in section 5. | T1 S, T2 M-L, T3 L | T1 low, T2 M, T3 high |
| **Alarms; guards running to sound them** | Global alarm `alarm_timer` (`propobj.c:179`). `alarmActivate` (`:15976`) starts it, and the siren self-clears after `CHROBJ_GAS_TIMER` = 1800 ticks, about 30 s (`src/game/propobj.h:21`, `propobj.c:16229`). Alarm boxes are shootable (the guard's attempt needs `objIsHealthy`, `src/game/chraction.c:11821`) and **Bond can toggle one with use** (`propobj.c:10733-10743`). Stock AI list **`GAILIST_SIMPLE_GUARD_ALARM_RAISER`** (`src/game/chraidata.c:477`): on seeing or hearing Bond it runs to `padpreset1`, plays the alarm animation, and the alarm fires at animation frame 60 (`chrlvTickStartAlarm`, `chraction.c:5912`). Bunker 1 has 2 alarm boxes; Bunker 2 has none. | `alarm.on/off/active()` and an `alarm` event (hook `alarmActivate`). The mode latches "alarm raised" itself, because the stock siren times out. `spawn.guard(pad, {alarm_pad = N})` selects the stock raiser list and sets the pad preset. On a stage with no alarm box (Bunker 2), add a tiny `chrStartAlarmAt(chr, pad)` that skips the object check. | S | Low |
| **Reinforcements filing in** | Stock `CHRFLAG_CLONE`: "clone on heard gunfire" (`src/bondconstants.h:242`; `TRY_SPAWNING_CLONE`, `aicommands.def:9162`). It spawns next to the original, not at a door. The port has wave spawning from chosen pads (`spawn.chr` with `chase`/`rush`, the arena's wave queue). | Lua waves on the `alarm` event: spawn at the entrance pads, cap the number alive, escalate over time (section 4). Keep the stock clone flag off, or the guard count gets out of hand. | S | M (chr slots, below) |
| **"So damn many" guards** | Chr slots on a stock stage = setup guard count + 10 (`src/game/initguards.c:30`), plus 24 with `GE_ARENA` (`port/include/port_arena.h:12`). Bunker 2 gets about 47. `spawn.guard` caps at 40 a stage (`lua.md`). The arena's pool and perf work (`roomGetProps` fix, live-enemy bench, `modding/arena.md` "The benchmark") shows 100 to 200 live chrs are feasible. | A slot reservation for modes on stock stages (stage init runs before Lua, so an env var or a mode-declared count, like `GE_COLOSSEUM_SLOTS`, `port/src/port_flatland.c:39`). Raise the `spawn.guard` cap. Keep corpses on a limit (`corpse_keep`). | S | M (pools, frame time) |
| **Silenced weapons, guard hearing** | **Stock and data-driven.** Each shot adds the weapon's `NoiseIncreasePerShot`, capped at `LoudnessMax`, and it decays toward `LoudnessMin` (`gunTickNoise`, `src/game/gunfire.c:5132`; fields `src/game/gun.h:162-190`). Every guard closer than **`hearingscale × noise × 100`** is alerted to Bond's position (`chrCheckGuardsHeardSound`, `src/game/chr.c:3998`, called per firing Bond at `src/game/bondview2.c:8257`). `hearingscale` defaults to 1 (`chr.c:1742`) and is settable (`SET_HEARING_SCALE`, `aicommands.def:6846`). The loudness values are ROM data, not source. **Silenced is not invisible:** a guard who sees another get shot or die still reacts (`IF_GUARD_SEE_ANOTHER_GUARD_SHOT` / `_DIE`, `aicommands.def:2910/2954`). | Nothing for the core. P0 logs `weapon.get("wppksil","loudnessmax")` against `"wppk"` to confirm the numbers. Lua can retune any gun (`weapon.set ... loudnessmin/max/noiseshot`). Add `hearing`/`vision` options to `spawn.guard` (vision: `aicommands.def:6890`). | S | Low |
| **Keycards and locked doors** | **Stock.** A door has a 32-bit `keyflags` (`src/bondtypes.h:2915`). It opens if the Bonds' held key props cover those bits (`propdoorInteract`, `propobj.c:15890-15906`). **In co-op, keys are already pooled across Bonds** (`bondinvCheckHasKeyFlags`, `src/game/bondinv.c:768`). One-way doors (flags2 bits, same function). **Padlocked doors**: `LockDoorRecord` (`bondtypes.h:3582`) keeps a door shut until its lock object is shot (`doorIsPadlockFree`, `src/game/chrprop.c:3312`). Level AI can lock and unlock doors (`DOOR_SET_LOCK`/`UNSET`, `aicommands.def:5439/5479`). | `door.list()` (pos, keyflags, state), `door.open/close/lock(keyflags)/unlock`. A `spawn.key(chr or pad, keyflags [, model])` that builds a `KeyRecord` the way `spawn.item` builds its armour records. | S-M | Low |
| **"Kill the right guard" for the card** | **Stock and already in Bunker 2:** five keys with distinct flags **2, 4, 8, 16, 32** are carried by guards (chrs 6, 11, 18, 26, 34) through `PROPFLAG_ASSIGNEDTOCHR` (`assets/obseg/setup/UsetupsevbZ.c:729-812`; flag `src/bondconstants.h:365`). A sixth key (flag 1) sits in the safe (`:347`). An assigned prop is reparented to its chr (`domakedefaultobj`, `src/game/prop.c:221-238`) and drops when the chr dies. In a mode those keys are skipped, because their chrs never spawn, but **the doors stay locked**. | `spawn.key(chr, flags)` re-attaches a card to our own spawned guard, using the same `ASSIGNEDTOCHR` path. Give it a coloured key or keycard model (`PROP_SWIPE_CARD2`, `bondconstants.h:3272`). | S | Low |
| **Threatening an employee** | **Stock pattern in Facility:** `ai_14` checks `IF_GUARD_IS_TARGETED_BY_BOND`, then calls `GUARD_SURRENDERS` (`UsetuparkZ.c:2365-2390`). `chrTrySurrender` (`chraction.c:10910`) throws down held items with the surrender animation. `CHR_DROP_ALL_CONCEALED_ITEMS` (`aicommands.def:4940`; used at `UsetuparkZ.c:2394`) drops hidden items: a concealed keycard is the hand-over. The targeted test is a crosshair-on-screen-box check (`chraction.c:10956`). The port has a use-press-on-a-chr system (hostages, `port/src/port_hostage.c:483`, hooked at `bondview2.c:8051`). | A port behaviour `"civilian"`: idles or wanders, cowers on nearby gunfire, and **surrenders when any Bond aims at it for about 1 s within about 8 m**. It then drops its concealed card (or hands it over on a use press, reusing the hostage hook). It must test every Bond, not the current player. | M | M |
| **Slapper knock-out (2 slaps, stays lying down)** | **Not stock** (GE has no KO; PD's `ACT_DRUGGEDKO` is commented out, `bondconstants.h` ACT enum). Parts that exist: the `damage` Lua event with the weapon id (unarmed is `ITEM_UNARMED`); soft death animations such as `ANIM_death_forward_face_down_soft` (`bondconstants.h:839`); the arena's "dormant chr" idea, where a chr skips prop collision and AI (`portArenaChrDormant`, `port/src/port_arenamode.c:230`) and bodies are kept (`:207`). | A KO state in C: count unarmed hits per civilian (default 2, ignore gun damage). Then play a soft fall animation and hold the last frame; the chr stops its AI and becomes untargetable and non-colliding. It is not dead: no `chr_died`, it is not counted as a civilian casualty, and it keeps its slot. Emit `chr_ko(chr, by)`. Optional: wake after N s; a guard who sees a KO'd body goes on alert. | M | M (pose glitches, AI edge cases) |
| **No killing civilians** | **Stock counter:** `CHRFLAG_COUNT_DEATH_AS_CIVILIAN` (`bondconstants.h:261`), `get_civilian_casualties()` (`gunfire.c:7928`), `IF_KILLED_CIVILIANS_GREATER_THAN` (`aicommands.def:11628`). Facility uses it to fail "minimise scientist casualties" at more than 2 deaths (`UsetuparkZ.c:3612-3640`). | Trivial in Lua: the mode knows its civilians (`chr_died` with victim in the set, then fail or penalty). Also set the stock flag on them so the stock stats agree. | S | Low |
| **Vault cracker: plant it and stay near** | **Stock basis: the Facility door decoder.** Equip a gadget (`ITEM_DOORDECODER`, `DATATHIEF`, ...) and press fire facing an object, and it sets `PROPSTATE_ACTIVATED` on that object (`gunfire.c:4920-4935`). Level AI checks `IF_BOND_USED_GADGET_ON_OBJECT` (`aicommands.def:4762`), runs a timer and opens the door (`UsetuparkZ.c:3585-3610`, `ai_48`). Note: `ITEM_SAFECRACKERCASE` is excluded from that gadget list (`gunfire.c:4945`), so use the door decoder item, or add the case to the list. The plant/deposit objective types also exist (`PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM`, `bondconstants.h:4412`). | A `gadget_used(obj, player, item)` event from that one site. In Lua: start a progress timer, a zone around the console, progress only while some Bond is inside the zone, HUD progress, optional jams that need a re-press. Guard waves run during the crack. | S (C) + M (Lua) | Low |
| **Vault "too long inside" timer** | Stock HUD countdown (`HUD_COUNTDOWN_SHOW/SET/START`, `aicommands.def:8631-8869`); stock `DOOR_CLOSE` and lock. | `hud.countdown(seconds)` binding (or plain HUD text, as hostage `time_limit` does). On expiry: alarm, door closes and relocks. | S | Low |
| **Loot: lots of pick-uppable money and bullion** | **Real loot props exist in the ROM:** `PROP_CHRGOLDBAR` and `PROP_CHRMONEY` (beta pickups, `bondconstants.h:3452-3453`; models `PchrgoldbarZ` and `PchrmoneyZ` in `assets/obseg/file_resource_table.inc.c:292/313`); items `ITEM_GOLDBAR`/`ITEM_MONEY` (`bondconstants.h:4269-4270`). Statue's flight recorder is `PROP_CHRBLACKBOX` / `ITEM_BLACKBOX` (`:3460`, `:4253`). `spawn.item` places real pickups but only **12 at once** (`ITEM_N`, `port_arenamode.c`), and only guns, armour or ammo. Pickups fire `item_pickup` (`lua.md`). | Extend `spawn.item` with `"goldbar"`/`"money"` (a collectable record with that model and item) and raise the pool to about 128. Count bars in Lua through `item_pickup`. A second bar probably merges into the same inventory entry (`bondinvAddWeaponByProp`, `bondinv.c:465`): **verify in P0** that it is still removed from the floor. Fallback: a Lua-collected static prop (walk over it). | M | M (beta models untested) |
| **Locked cage around the loot** | Stock cell doors (`PROP_DOORPRISON1`, `bondconstants.h:3368`; mesh gate `PROP_DAMCHAINDOOR`, `:3366`) with keyflags. **Safes:** `PROPDEF_SAFE` and `PROPDEF_SAFE_ITEM` keep items unpickable until the safe door is over half open (`objCanPickupFromSafe`, `chrprop.c:3335`); Bunker 2's safe holds 3 linked items (`UsetupsevbZ.c:481-500`). | Nothing new. The cage is a keyed cell door; its key is on the vault captain, or the vault console unlocks it. | S | Low |
| **Interacting with consoles** | `propobjInteract` (`propobj.c:10728`) is the stock use-press handler; the alarm box uses it. The port hostage hook already intercepts the use press (`bondview2.c:8051`). | A Lua `use(player, objtag or pad)` event from `propobjInteract`, plus a "use near pad" fallback for places with no object. | S | Low |
| **Multiple exits** | Lua zones (`zone.add`); co-op exit rules already exist (`coop_exits`, `port/src/port_coop.c:38-44`). Padlock doors suit a "blow the lock" exit. | Exit zones with conditions (loot quota, a padlock shot, a key). | S | Low |

**Summary:** everything except the **KO**, the **camera-feed rendering** and the **multi-room generator** is either stock or a small Lua binding over stock code.

---

## 3. The map: options and recommendation

### Option A: prototype on a stock level (recommended first)

As hostage rescue did on Dam, prove the mechanics where the geometry, doors and objects already exist. Counted from the setup records:

| Stage | CCTV | Alarm boxes | Keys | Monitors | Safe | Doors | Fit |
|---|---|---|---|---|---|---|---|
| **Bunker 2** (`UsetupsevbZ.c`) | **6** | 0 | **6** (5 on guards, flags 2-32; 1 in safe) | 37 + 2 racks | **yes, 3 items** | 38 | **Best.** A real keycard ladder, cameras, a safe, a cell block for the cage, monitor rooms. |
| Bunker 1 (`UsetupsevbunkerZ.c`) | 4 | **2** | 2 | 31 + 1 multi | no | 17 | Good for testing alarm boxes and the control room (the main video screen). Small. |
| Facility (`UsetuparkZ.c`) | 0 | 0 | 2 | 16 | no | 46 | Has the decoder door and scientist AI, but no cameras. |
| Archives (`UsetuparchZ.c`) | 0 | 0 | 2 | 0 | yes | 35 | Office feel but no cameras or monitors. (The brief expected cameras in Archives; the setup has none.) |
| Depot (`UsetupdepoZ.c`) | 0 | 0 | 1 | 4 + 1 | yes | 24 | The warehouse heist from `mode-ideas.md` C4. |

**Pick Bunker 2.** It is a bunker, not a bank, but for a mechanics prototype that does not matter. Plan: map its keyed doors to tiers (P0 starts with a probe that logs every door's keyflags and position), put our own guards at the stock key carriers' spots with `spawn.key`, use its cameras as they are, use a cell as the bullion cage, and treat its biggest monitor room as the control room. With no alarm boxes there, alarm guards use `chrStartAlarmAt` on chosen "panel" pads. Bunker 1 is the backup and the alarm-box testbed.

### Option B: a generated bank (the real level)

Today the generators make **one room**. `gen_flatland.py:179-210` writes one entry in `room_data_table`, an **empty portal table** and global visibility `0x10000`. Every stan tile carries room 1 (`gen_flatland.py:271`). Colosseum reuses those helpers. A bank needs:

1. **Several BG rooms**, each with its own point table and display list (same 1172-deflate path), and a room id on each stan tile.
2. **Portals**: one quad per doorway in `portal_data_table` (struct at `assets/obseg/bg/bg_all_p.h:26`: polygon, room1, room2, control bytes), plus the global visibility commands. This is what makes room culling, guard "on screen" checks and door blocking work. Get it wrong and rooms go black (netplay notes have a history of exactly this).
3. **Stan links across rooms**, so the doorway tiles join up.
4. **Setup objects**, which the generators do not emit yet: doors (model, bound pad, `keyflags`, door type), CCTV (look pad, range), alarm boxes, monitors, cell doors, a safe with linked items, keys, padlocks. Stock record layouts are in `bondtypes.h`; the compiled setups (`assets/obseg/setup/*.c`) are worked examples.
5. **A waypoint graph through the doorways**, so guards path room to room. The current generator builds a grid.
6. **A cut stage slot.** `LEVELID_SHO` is Flatland and `LEVELID_EAR` is the Colosseum (`port_flatland.c`). `LEE`, `LIP`, `RIT`, `WAX`, `PAM` and `ELD` are still free (`bondconstants.h:1732-1746`).

Path: a `tools/gen_bank.py` that reads a **room/door layout description** (JSON: rectangles, doorways, heights, textures, object placements) and emits bg, stan and setup C. Prove it first on a two-rooms-one-door test box (portal, culling and door blocking), then build the bank. Textures come from the stock set (as Colosseum does), so a "marble bank" look is limited to what Archives, Statue and Streets offer. That is acceptable.

### Option C: hand-crafted later

There are two realistic tool paths:

- **The same JSON layout, drawn by hand.** Option B's generator input doubles as a hand-editable format. A small 2D editor or a Blender script that exports rooms, doorways and pads to that JSON keeps the one emitter. This is the cheapest and fits B.
- **A community GoldenEye level editor** that writes ROM-format bg, stan and setup binaries, then `tools/bg_binary_to_c.py` (and the same for stan and setup) to compile them onto a cut slot. The port already parses ROM-format bg and setup (`port/src/romfile_bg.c`, `romfile_setup.c`). It does not load modded ROMs, though (`port/src/romdata.c` header: "stock cart only"; that work lives on the mod-loader branch), so the output must be **converted to compiled-in C**, not served. Converters for stan and setup would need checking or writing.

### Recommended order

**A, then B, then C.** The Bunker 2 prototype answers every gameplay question (stealth tuning, guard counts, card flow, vault pacing, KO feel) before anyone builds geometry. Then the generator builds the bank around a design that is already proven. Hand-crafting replaces the generated layout only once the format is stable.

---

## 4. Level flow (the generated bank; Bunker 2 maps onto it loosely)

```
          [ STREET / VAN ]  (start; Exit A: van, needs loot quota)
                 |
          [ LOBBY + TELLERS ]  civilians, 2-3 guards, CCTV x2      (no card)
            |            |
   [ STAFF OFFICES ]  [ LOADING DOCK ] (Exit B, needs GREEN)
     GREEN card        |
            |          |
   [ SECURITY / CONTROL ROOM ]  monitors, camera switch, alarm panel   (BLUE)
            |
   [ MANAGER'S OFFICE ]  manager (employee) carries RED; safe with the cracker (BLUE)
            |
   [ VAULT ANTECHAMBER / STAIRS DOWN ]  guards, CCTV, vault console   (RED)
            |
   [ VAULT ]  door opens on a timer; vault guards; BULLION CAGE (GOLD key)
            |
   [ MAINTENANCE TUNNEL ]  Exit C: padlocked hatch, shoot the lock
```

**Keycard tiers**

| Tier | Opens | Who carries it | How to get it |
|---|---|---|---|
| GREEN (flag 2) | Staff offices, loading dock | Lobby guard captain **or** the head teller | Kill the captain, or threaten or slap the teller |
| BLUE (flag 4) | Security room, manager's office | Security chief (guard, patrols the offices) | Kill him; the camera intel shows his route |
| RED (flag 8) | Vault antechamber | Branch manager (employee) | Threaten him (he hands it over) or slap him out; he panics and runs for the alarm if he sees a gun first |
| GOLD (flag 16) | The bullion cage | Vault captain (inside the vault) | Kill him during the grab |

Keycard holders are drawn at random from a candidate list per run (seeded RNG, so they match in co-op). The camera intel tells you who.

**The control room**

- **Consoles:** a use press on one gives the T1 intel. Each camera's room is named with its guard count, and the room of each card holder is marked "card carrier here".
- **Camera switch:** blinds every camera for 90 s (or for good if the room is held). Destroying the cameras one by one also works.
- **Alarm panel:** an alarm box. A guard who reaches it raises the alarm; Bond can switch it off once, which delays the next wave timer.

**The vault sequence**

1. RED opens the antechamber. Two guards and a camera are on the console.
2. **Get the cracker.** It is either in Bond's start loadout (easy) or in the manager's safe (hard, behind BLUE). It is the door decoder item.
3. **Plant it:** equip, then press fire at the console (the stock gadget use). The HUD shows `CRACKING 0%`.
4. **Hold the zone (90 s).** Progress runs only while a Bond is within about 5 m. Every 30 s there is a chance it jams ("CRACKER JAMMED, press use"). The crack sets off a vault-area alert: two squads come down the stairs even in a quiet run.
5. **The door opens. A 60 s grab window** runs on the HUD countdown. Vault guards wake up (`hold` until the door opens, then `chase`).
6. **The cage:** GOLD key from the vault captain, or shoot the padlock if the cage uses one (designer's choice per run).
7. **At T-15 s** the klaxon sounds. **At 0** the alarm goes off (if it is not already), and the vault door closes and relocks. Anyone inside is trapped (see the open questions).

**Alarm escalation (latched by the mode; the stock siren only lasts about 30 s)**

| Level | Trigger | World reaction |
|---|---|---|
| 0 Calm | none | Patrols, cameras sweeping, staff working. Guards use the stock simple/standard lists: they hear loud shots in a radius and react to bodies. |
| 1 Suspicious | A loud shot heard, a body seen, a camera tracking you (timer filling) | The stock investigate behaviour. HUD: `SUSPICION`. A nearby alarm-raiser guard heads for the panel. |
| 2 Alarm | A camera's 5 s, a raiser reaching a panel, Bond tripping the alarm box, the vault timer running out | Siren. **Lockdown:** the lobby shutters drop (doors close and lock). **Waves** come through the main entrance every 25 s, capped at about 16 alive, and later through the loading dock too. Staff flee or cower. |
| 3 Response | Alarm + 2 min, or the vault opened during an alarm | Elite squads (arena elites: Spetsnaz or Janus SF), armoured, from both entrances. Snipers at the street exit. |

**Exits**

| Exit | Condition | Catch |
|---|---|---|
| A: front van | Loot ≥ quota | Through the lobby: the main wave entrance during an alarm |
| B: loading dock | GREEN card | A quieter route, with a guard post at the dock |
| C: maintenance hatch | Shoot the padlock (`LockDoor`) | Close to the vault, but a long tunnel run. Each Bond can carry fewer bars here (optional weight rule) |

**Win:** all living Bonds in exit zones with team loot ≥ quota. **Score:** loot value, time, alarm level reached, civilians KO'd versus harmed.

---

## 5. Co-op notes

The rule from the netplay work (`lab/notes/netplay.md` on the `lab` branch, "two worlds that must never mix"): **sim state is read from canonical, per-player sim views and recorded inputs, never from the window or the local player. Presentation is free.** Mode scripts already run identically on both machines (`lua.md` "Sandbox"). Per mechanic:

| Mechanic | Co-op status | Needed |
|---|---|---|
| Keycards and doors | **Already co-op:** keys pool across Bonds (`bondinv.c:768`). | HUD per Bond showing which cards the team holds. Shared cards are the right default. Per-Bond cards would be an opt-in later. |
| Alarm | Global; already in the state hash (`port_coop.c:2992`). | None. |
| CCTV | Reads only the current player (`propobj.c:5590`). | Test every Bond; the timer is per camera (S). |
| Hearing | Called per firing Bond in its own pass (`bondview2.c:8257`), distance to that Bond. | Check that "distance to Bond" there is the shooter (it looks right). |
| Threaten (civilians) | The stock targeted check is the current player's screen box. | The port behaviour loops over every Bond's canonical aim (the canonical sim view makes the aim machine-independent, job 3c). |
| KO, civilian table, cracker progress, loot carried | New sim state. | Every new port side table goes into `portCoopStateHash` (`port_coop.c:2720` says how to extend it). Lua state is fine as it is. |
| Loot pickup | `item_pickup` has no player argument. | Add `player` so per-Bond carry limits and the HUD work (S). |
| Gadget / use events | The pickup and use run in the acting Bond's pass, so the current player is the actor. | Pass the player index in the events. |
| **Camera feed** | **Must be presentation-only.** Drawing props from another viewpoint rebuilds their matrices, and the sim reads them back (`model->render_pos`), which is the documented reason props never follow the window. | T1 (HUD text from sim state) is safe. T2 draws **geometry plus impostor markers** (crowd atlas path: no chr, no prop, its own RNG and clock) from a render-only camera, so it never touches prop matrices or the sim room walk. T3 needs the same restriction. Opening or closing the feed is a per-Bond view toggle, like the watch gate (per Bond, `netplay.md`). |
| Roles | Not built. | A good co-op hook: "Overwatch" sits in the control room calling out cameras, while "Inside" climbs the cards. Both feeds stay HUD-side. |
| Guard counts | Chr slots and frame time, both machines. | The same slot reservation on both sides (host SETUP carries mode settings already). |

---

## 6. Phased build plan

Each "job" is one agent dispatch with its own test. Counts are rough.

**P0: prototype on Bunker 2 (6 jobs). Goal: a playable solo heist-lite.**

1. **Probe** (S): log Bunker 2's doors (keyflags, position, type), cameras, safe, monitors and pads in a mode. Confirm objects survive the mode reset. Log the silenced-gun loudness numbers. Fix `lua.md` line 19.
2. **Bindings batch** (S-M): `spawn.key`, `door.*`, `alarm.*` plus the `alarm` event, `cctv.list/disable` plus `cctv_spotted`, `use` and `gadget_used` events, a `player` argument on `item_pickup`, `hud.countdown`.
3. **Guards** (S): `spawn.guard` options `alarm_pad` (stock raiser list), `hearing`, `vision`; `chrStartAlarmAt`; mode chr-slot reservation; raise the 40 cap.
4. **Civilians** (M): `"civilian"` behaviour (idle, cower, aim-to-surrender for any Bond, drop or hand over a concealed card), civilian flag and counter.
5. **KO** (M): 2 slaps to KO, hold the pose, dormant, `chr_ko` event; hashed.
6. **The mode** (M): `tools/mode_heist.lua`: tiers on Bunker 2's doors, card carriers, cracker hold-zone, vault timer, waves, loot (`spawn.item "goldbar"`, pool raised), exits. Plus a headless win-path test (as `tests/` has for hostage).

**P1: intel (2 jobs).** T1 control-room intel on the HUD (S). T2 security-feed view, solo first (M-L).

**P2: generated bank (5 jobs).** (1) Multi-room bg + portals + stan emitter, proven on a two-room test box (L). (2) Setup object emitter: doors, CCTV, alarms, monitors, cells, safe, keys, padlocks (M). (3) Waypoints through doorways and the bank layout JSON plus textures (M). (4) Port the mode to the bank and tune it (M). (5) Perf pass with the full guard count (S-M).

**P3: co-op hardening (3 jobs).** Per-Bond CCTV and threaten checks; hash entries for KO, civilians and the cracker; `net_lockstep` phases for a scripted heist (alarm, waves, vault, exits) with two different windows.

**P4: later (2-3 jobs).** T3 render-to-texture on in-world monitors; the hand-crafted map path (Option C); Overwatch roles.

**Total:** about 18-19 jobs. **P0 alone delivers the game loop.**

---

## 7. Open questions for AC

1. **Civilian deaths:** instant fail on the first, or a limit (stock Facility fails at more than 2)? Do they count KOs at all?
2. **KO'd staff:** stay down for good, or wake after a minute and raise the alarm? Should a guard who finds a KO'd body go on alert?
3. **Vault door closing on you:** fail if trapped, re-crack from inside (slower), or just the alarm plus a heavy squad?
4. **Loot rules:** a quota to win, or score only? A carry limit (a bag of N bars and several trips, Payday style) or carry everything?
5. **Camera feeds:** is HUD intel plus a stylised "security feed" (level geometry with marker silhouettes) good for v1? Or do you want real video on the monitors (T3, the expensive and risky one)?
6. **The cracker:** start with it, or have to find it in the manager's safe? Should it jam?
7. **Co-op:** shared keycards (stock behaviour, simplest) or per-Bond? Do you want the Overwatch / Inside role split?
8. **Exits:** must all Bonds get out (the `coop_exits` setting already covers this)? Should exits differ in what they demand (quota, card, padlock)?
9. **Prototype stage:** is Bunker 2 as a stand-in bank fine for P0?
10. **Scale:** what does "so damn many" mean in numbers: about 40 guards at once, or 100+? That sets the slot reservation and the perf target.
11. **Flavour:** GoldenEye-era (a Janus front bank in St Petersburg, Russian guards) or something else? This picks the skins and the text.

---

## 8. AC's answers (2026-10-24). These override the defaults above.

1. **Civilian deaths:** fail after N, as in the stock hostage/civilian levels (Frigate etc.). **KOs do not count:** knocking someone out is the way to avoid killing them.
2. **KOs never wake on their own.** Guards **are alerted when they find a body** (KO'd or dead). Stretch idea AC likes: a guard who finds a KO'd body could **go and wake them up**.
3. **Vault door closes on you:** re-crack from the inside to get out. That triggers the alarm and a heavy squad.
4. **Loot:** no quota. It's a **high score** to chase.
5. **Camera feeds: real video from the start.** Using a monitor **moves the player's view to that camera**; a shot or offline camera shows static/black. *Note:* a view switch (render the scene from the camera's position instead of Bond's) is much simpler than render-to-texture on the in-world monitor. It must stay a presentation-only change: the sim keeps using Bond's canonical sim view (the two-worlds rule), so it's safe for co-op too. In-world monitor textures (T3) can come later.
6. **The cracker:** the player starts with it.
7. **Co-op keycards:** per player, not shared.
8. **Exits:** all players must get out.
9. **P0 stage:** Bunker 2, approved.
10. **Guards:** "tonnes across the facility". Plan for 100+ placed guards plus alarm waves, so raise the slot reservation accordingly. Live-enemy perf (fork benchmark): ~75 alive at once is free at 144 fps, ~180 before dropping under 60; most will be idle on post, which is cheap.
11. **Flavour:** a Janus front bank in St Petersburg, Russian guards. Approved.

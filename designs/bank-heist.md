# Bank Heist: design

Status: P0 (the Bunker 2 prototype) is built: the playable mode is section 9; P1 (camera feeds, intel) is section 10; P3 (co-op hardening, determinism) is section 11; P2 job 1 (the multi-room generator, proven on a two-room box and a three-room L) is section 12. The banker cover, the security room's camera link, the power cut and the guard door rules are section 16. The rest is still design. Every "stock" claim below cites the code it was read from, so it can be checked. Where something was read but not yet run, it says **verify in P0**.

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

Further answers (2026-10-25):

12. **Loot: carry everything** (no bag limit, no trips). Score only, no quota: a high score to chase.
13. **The cracker jams sometimes.** A jam stops it; the player must walk back into the hold zone and restart it.
14. **Co-op: no role split for now** (no Overwatch / Inside).

AC delegated, Claude's call 2026-10-25, revisit:

15. **Difficulty scales the force.** Agent: about 30 posted guards at stock accuracy; Secret Agent about 45 at 1.5x; 00 Agent about 70 at 2x; 007 the full force (90 posted, 100+ placed) at 2x. Raisers, vault guards, the wave cap, the crack squads, the heavy squad and the response scale the same way (`S.difficulty[d].force`); per-wave counts stay 2 / 3 / 4 / 4. Why: the player bot won 10/10 with only the card carriers, 8/10 with 20 posted, 0/10 with 45 and with the old defaults, so Agent should be clearly winnable by a careful human. All of it is in the mode's `S` table.
16. **The quiet bonus is reachable.** The vault's time lock closing on an EMPTY vault no longer raises the alarm (the klaxon's siren is switched off again unless the heist is already hot). Only a Bond trapped inside raises it, and that brings the heavy squad (at the trap now, not at the re-crack). A clean run that leaves the vault before it shuts keeps the +25 % (`S.timelock_alarm_empty = true` restores the old rule).
17. **Guards hold fire near staff.** At Agent's ~30 guards most of the bot's losses were guards' crossfire killing the 5th bank civilian before the vault. A heist guard now holds a shot while a bank civilian (standing, cowering or knocked out) is within 40 units of his line of fire to his target and short of it (30 units); he keeps his stance and aim and fires when the line clears. The civilian-death count is unchanged. `mode.hold_fire(on)` (the heist sets it from `S.hold_fire`); off, and in every other mode, the stock behaviour is untouched (the hook sits in `chrlvTriggerFireWeapon`, `#ifndef TARGET_N64`; dam1 identical). It adds no sim state: the check reads positions and the civilian table, so both machines drop the same shots (the determinism test covers it). A civilian standing right in the line already blocks the stock sight test, so the hold matters for the near misses. Not done: walking to a clear line (the guard waits instead).

---

## 9. P0 job 6: the heist mode on Bunker 2 (built)

`tools/mode_heist.lua` (shipped as `heist.lua`; `Play Heist.bat`). Every number below is a setting in its `S` table.

**The map, read from the stage.** The BG portal table (`assets/obseg/bg/bg_sevb_all_p.c`) and the waypoint graph (`UsetupsevbZ.c`) give the room graph; the keyed doors sit on it like this: the north tunnel (68-4-3-2-1) enters the hall (6) through the blast doors 80-85; the hall opens south into the central corridors (5, 16, 13, 14, 59, 18). The offices (11, 12) hang off 13 behind door pairs 46-49; the security wing (43, 44, 46) hangs off 14 and 18 behind 50-53. The east wing is a loop 48-49-50-51-56(vault)-...-47-54-53-52-48: **the stock key-0x8 doors 58/59 and 60/61 are the two vault antechambers' outer doors** and 68/69, 70/71 are the vault's own doors (rooms 51/56 and 54/47). The vault is rooms 55, 56, 57, 58, 47 (the crate store). The west wing loops round to the records rooms (33, 30, 25, 27), the manager's safe (26) and the cell block (39-41).

**Flow.** Street (pad 51) → front door 72 (unlocked) → blast doors (unlocked; on the alarm they close and take BLUE: exit B is always open) → banking hall (tellers, customers, guards, camera 95) → GREEN doors 46-49: offices (camera 89) → BLUE doors 50-53: security wing (cameras 87, 93; the camera console: a use press there with BLUE blinds every camera for 90 s and names who still carries a card) → RED doors 58-61: the antechambers → the cracker at vault door A (pad 85).

| Card | Flag | Carrier (spawn.key) | Opens |
|---|---|---|---|
| GREEN | 0x2 | the head teller (civilian) or the lobby captain (guard), random per run | offices 46-49, deposit cell 77 |
| BLUE | 0x4 | the security chief (Russian_Commandant, offices) | security wing 50-53 (and the stock 0x4 side doors 64-67); the blast doors in a lockdown |
| RED | 0x8 | the branch manager (civilian, security wing) | the antechambers 58-61 |
| GOLD | 0x10 | the vault captain (inside the vault) | the manager's safe 102 (its 3 bars spill out on the floor when it opens) |
| (floor key) | 0x1 | stock, lies in the cell corridor | deposit cell 75 |
| VAULT | 0x40000000 | nobody: only the cracker | vault doors 68-71 (one-way bits cleared with `door.one_way`) |

Cards come off a carrier when he is robbed (a civilian: aim until it surrenders and drops it), knocked out and searched, or killed. Keys are per player (`mode.per_player_keys`).

**Guards.** The full force is 90 posted guards spread over nine areas by share (hall 14, east 14, west 14, south 10, central 9, offices 7, cells 7, security 5, safe 1 parts), 12 alarm raisers who run to their area's "panel" pad, and the carriers; each difficulty takes a share of it (section 8 answer 15: Agent ≈30 posted at stock aim, 007 the full force, 100+ placed). The vault guards (8 at full force) wake when it opens. Several share a pad through `spawn.chr{offset=}`. The guards' guns come from the port weapon pool past the stock 30 (`weaponCreate`), and the stage gets 410 more object model slots.

**Alarm.** Stock cameras (10 s Agent, 5 s above) and raisers. Once the alarm goes off the heist is **hot for good**: the siren is re-raised whenever it times out, the hall locks down, waves of 2-4 come in at the hall's north end every 25 s (16 alive at most, none while 120 armed chrs are alive), and 120 s later an armoured Spetsnaz / Janus SF response from the hall and the loading dock.

**The crack and the vault.** The cracker (the door decoder: fire it at the door, or press use there) starts in the antechamber zone (220 units round pad 85) and runs only while a Bond is in it (the stock countdown clock stops and resumes). Every 15 s of progress it may jam (30%, `math.random`, so co-op agrees); a jam needs a Bond back in the zone to restart it. Starting it sends a squad, half way a second. At 90 s the vault doors open for 60 s (held open; a door blocked by a chr in the doorway is snapped open after 1.5 s, `door.snap`). T-15 s the klaxon (the siren, no response yet); at 0 the doors close and relock (snapped shut if blocked). An empty vault seals quietly (section 8 answer 16). A Bond inside (the vault's rooms 55-58 and 47, or its circles) is trapped: the alarm goes hot, an armoured heavy squad comes, and a re-crack from the inside (45 s, at either door) opens the vault for good. (Before 2026-10-25 the circles missed the west end of room 56: a Bond standing there was "sealed" in with no re-crack. The bot branch found it; fixed with the room test.)

**The security console** switches the cameras off once per activation: a use press while they are off does nothing (it used to restart the 90 s each press, 5-9 times a run). **The shutters 78/79** (room 46, between the security rooms 43 and 44): see section 9b.

**Loot and score.** 30 gold bars in the vault ($150,000 each), 3 in the safe, 8 cash bundles ($25,000) in the deposit cells; carry everything. Score = loot + 25% for a run that never went loud − $100,000 per civilian killed. The best this session is kept in `HEIST_BEST` (modes have no save API) and logged as `ge: heist: HIGH SCORE`.

**Exits and fails.** Exit A the van (pad 51), exit B the loading dock (pad 116, west wing). Every living Bond must stand in one, carrying something (`coop_exits=first`: one is enough). Fail: every Bond down, or more civilians killed than Frigate allows (`IF_KILLED_CIVILIANS_GREATER_THAN` in `UsetupdestZ.c`: Agent 4, Secret Agent 2, 00 Agent / 007 1; Facility uses 2). Knock-outs never count.

**Tests.** `tests/heist/run_winpath.sh`: the win path (every card by knock-out and search, the tiered doors, the console, the crack with a forced jam, the zone rule, the vault, the safe, a cell, the exit), the trapped path, and the civilian-limit fail path. Screenshot tour: `tests/heist/run_shots.sh`.

**Known gaps.** The gold bar and cash models are the beta pickups as they are (small textured blocks). Wave guards come in from the hall's north end even when the blast doors are locked behind them (they spawn inside). The loot pickup has no line-of-sight test (none is placed near a wall that matters). Cards drop where the carrier stands; a robbed civilian's card can land under a desk. (The intel box and the camera feeds came in P1, section 10.)

### 9b. The security-wing shutters 78/79 (checked 2026-10-25)

The player bot reported that doors 78/79 (stock vertical shutters in room 46, between the security rooms 43 and 44: unkeyed, `PROPFLAG_CULL_BEHIND_DOOR`, 25 s auto-close, a narrow 65-unit opening whose centre sits high, y 304 against Bond's 172) never open for B. A probe on the heist build (keep_locks on) says they do: a real B press (`cctv.test_press("b")`) from room 43, from room 44 and from inside 46 opens each one fully (0.95), `door.try` does too, and Bond walks through. So the route 44-46-43 works for a player and the mode needs no key or switch for it; nothing changed. The bot's miss is its own: the stock interact test (`doorTestForInteract`) wants Bond within 200 units of the door's centre and his view between the door's two edges (measured from 22 units ahead of him, ±20 degrees), which a narrow door makes tight; the bot should face the door's centre from close by before pressing B, or go through the BLUE doors 52/53 into room 43 directly.

## 10. P1: the camera feeds (T2) and the control-room intel (T1) (built)

**Feeds (AC's answer 5, real video).** A use press at one of the banking hall's screens (room 6: the monitor wall at x -477, the two desk screens; a use press with nothing in reach now fires `use` with target -1) puts that Bond on the camera feeds (`cctv.view`). His screen shows the world from the camera: room geometry from a portal walk that starts in the camera's room at its eye, the props the game drew this frame, and every chr in the camera's rooms posed for the feed with the gun in hand. A camera that is off shows static. A / C-right next camera, C-left previous (the stick left / right too), B leave; Port binds: next / previous weapon, use. He stands at the screens while he watches: being hit throws him out (`cctv_view` why 3). The gun, crosshair and ammo counter are hidden; the overlay shows the camera's name, CAM n/6, a blinking REC dot and the mission clock. The security console (BLUE) keeps its P0 job (cameras off 90 s).

**Two worlds.** Who watches which camera is sim state (set by the script, cycled and left with the Bond's own synchronized input, in the state hash). The picture is presentation on his machine only: the sim keeps Bond's canonical camera and computes everything from it, then the world draw is redirected. The camera's walk runs first (the sim walk runs last, so every flag and list the sim reads is its own); rooms only the camera reached draw geometry only; the two fixed-point projection matrices the display list loads are rewritten after the sim used their float sources (props: `P_cam * V_cam * V_bond^-1`, bg: `P_cam * V_cam` relative to the room origin), so every matrix the sim built is untouched and the GPU sees it from the camera. Chrs only the camera sees are posed into fresh matrices (Bond-camera space) and drawn, and their Model structs are put back byte for byte. The portal walk's side test also reads the camera's eye (`bg.c`, two sites). dam1 is identical.

**Intel (T1).** While a Bond is in room 6 or on a feed, the HUD panel (`hud.panel`) shows: cameras up, the alarm, the vault (time-locked, cracking n%, jammed, open n s, sealed), each card carrier as "live" / "n s ago" where a working camera last saw him (`cctv.sees`, the stock cone and stan line) or "unknown" / "down" / "card not on him", and the guard count of each area a working camera covers ("?" when its camera is off; west, records and cells have none). Coverage is respected for the carriers and the guard counts.

**Limits.** The lens is a fixed 62-degree fov tilted onto the camera's look pad, turning with its live sweep. A chr standing right under a camera can fill the frame. Static room lighting and environment mapping follow Bond's camera (cosmetic). The guard counts are per area the guard was posted to, not where he is now. Rooms the camera sees are loaded as the co-op render walk's margin rooms are. Not yet: in-world monitor textures (T3).

**Tests.** `tests/heist/run_cctv.sh` (35 checks at P1: a real B press at the screens, the walk starting in the camera's room, the hold, A / C-left / C-right, posed chrs, static, the damage kick-out, B out and no re-entry, the API, the intel box in and out of room 6 and on a feed, coverage) and its screenshots in `build_pc/heist_cctv_shots/`.

**After P1 (playtest fixes).** Only the use action (the Port use bind) at a hall screen opens the feeds: B is use *and* reload, and a reload next to the screens kept opening them (`bond.use_key(p)` in the `use` handler; B still leaves a feed). The mode's own HUD lines (JANUS BANK $... / the objective / the flash line) are cleared while a Bond watches and come back when he leaves; the feed overlay and the intel box stay.

## 11. P3: co-op hardening and determinism (built)

**Determinism first.** Two identical headless runs used to part by the time the setup finished. Causes found and fixed, in the order the test met them:

1. **`GE_SKIPINTRO` counted polls, not sim ticks** (`input_pc.c`). Its A taps through the intro landed on a wall-clock-dependent tick, so the stage's first-person play (and with it `stage_start`, the spawns, everything) began on a different tick from run to run. It counts sim ticks now (`g_PortSimTickNo`), and stops for the stage once Bond is in the player's hands (a camera feed takes the controls away, and a later tap there cycled the cameras). `cctv.test_press` had the same bug (6 polls); it holds for 4 sim ticks now. Test-harness bugs, not netplay ones (netplay feeds the input per tick), but they made every test run differ.
2. **A solo mode's `math.random` was seeded from the clock** (`port_lua.c`). With `GE_SEED` set (every headless test) a solo run takes the seed from it, as co-op takes the session's. Without `GE_SEED` solo play still gets a fresh clock seed.
3. **The camera feed drew from the sim RNG.** The posed chrs' draw (`port_cctvview.c`) runs the model draw, which takes numbers from the SIM RNG for a gun's muzzle flash (`model.c`). It happens only on the watching Bond's machine: in co-op / netplay the two machines' RNGs parted the moment a guard fired on the feed (view 0 vs view 1 at tick 1052). The feed now puts both generators and their draw counts back.
4. **The feed's allocations moved the sim's.** The posed chrs took their matrices and vertices from the frame's linear pool (`dyn.c`), so every allocation the sim's own draw made after them landed elsewhere on the watching machine, and that pool's layout is read back through stale pointers: Bond 1's autoaim differed in its last bits (tick 3469). The feed now draws from its own corner at the top of the pool (512 KB more pool in a heist mode) and puts the pointer back, and also restores the posed models' rw data.

The test: `tests/heist/run_determinism.sh` runs the scripted heist `tests/heist/heist_determinism.lua` (the feeds opened with the use action, cycled and left, two real slaps and a `spawn.ko`, the alarm with its lockdown and waves, the crack with a forced jam and the restart, the vault open, loot, the vault sealing, a feed kept open while the waves come) with `GE_TICKHZ=60 GE_FIXEDTICK=1 GE_SEED=7` and `GE_HASHLOG` (new, `port_coop.c`: the state hash and its 12 fields every sim tick, solo or co-op; `GE_HASHLOG_DUMP=<from>-<to>` adds the text dump, and `GE_NET_RNGTRACE` now works in such a local run). Cases: solo twice; local co-op twice and with the local player switched (`GE_COOP_VIEW=1`, the netplay condition); lockstep netplay, a host and a guest process on loopback. Each pair must be hash-identical tick for tick for over 3000 ticks (netplay: no DESYNC either).

**Co-op rules.**

| Mechanic | Now |
|---|---|
| CCTV | With more than one Bond a camera tests every living Bond, each on his own sight timer (`portHeistCctvAllBonds`, propobj.c's stock test stays for solo); `cctv_spotted` names the Bond whose time ran out. |
| Threaten | Each Bond's aim at a civilian has its own timer (`aimP` / `threatP`): the first Bond to aim long enough makes it surrender, and the first to keep it up robs it. Two Bonds half-aiming do not add up. |
| Cards | Per player (`mode.per_player_keys`, unchanged); each Bond's cards are in the state hash. |
| Exits | Every living Bond must stand in an exit, whatever `coop_exits` says (AC's answer 8). |
| State hash | Besides the KO table, the civilians (now with each Bond's timers), loot, the countdown, camera overrides and the feed state (which camera, the held and edge buttons, ticks on the camera, the hit baseline), the script's own state goes in through `mode.state(text)`: phase, crack progress and jam schedule, who cracks, the vault clock, the trap, loot per Bond, squads, the wave and response counters, the camera switch-off, the spawn queue. The text is in the desync dump. |
| Waves | `heist.reinforce` tries a busy pad a step aside before the next one: every hall pad has a guard posted on it, so the waves never came in at all (the determinism test found it: 0 waves). |

## 12. P2 job 1: the multi-room generator (built)

`tools/gen_level.py` turns a JSON layout (`tools/levelgen/*.json`: box rooms, doorways, materials, lights, named pads, the start) into the bg, stan and setup C of a level plus a Lua layout table, on the cut slot `LEVELID_LEE` (`GE_STAGE=levelgen`). It builds on what Flatland and the Colosseum already do (their deflate and display-list helpers, the stan and setup emitters, the compiled-in slot); what is new is several bg rooms with a portal per doorway, the stan grouped and room-tagged per room and linked through the doorways, and waypoint groups per room. The format, the textures found and the mechanism are in `modding/levelgen.md`.

**Proven** (`tests/levelgen/run.sh`, 11 checks on the three-room L, 9 on the two-room box): a room behind the camera is not drawn, a doorway shows the next room and not the one two walls further; Bond walks through a doorway on stick input and `bond.room` changes A to B; an unarmed guard spawned two rooms away routes to Bond through both doorways (`spawn.room` C, B, A, about 8 s). Screenshots: `build_pc/levelgen_shots/`. The regression pass that WSL cut off ran afterwards on 4fd3545: bindings 38, guards 34, ko 26, winpath 50 / trapped 16 / civ-fail 5, cctv 40, determinism 12, coop determinism 0 failed, levelgen 11, dam1 identical.

**Next (P2.2 to P2.5).**

1. **P2.2 setup objects.** The generator writes `propDefs` empty. It needs door records (model, the doorway's pad as the bound pad, `keyflags`, door type: a door sits in the doorway's opening, so the generator knows its frame), CCTV (a pad high on a wall, look pad, range), alarm boxes, monitors, a cell door for the bullion cage, the safe with linked items, keys, padlocks. A door flagged `PROPFLAG_CULL_BEHIND_DOOR` takes the portal between the rooms on its two sides (`bgGetPortalBetweenRooms`, `prop.c`) and closes it while shut (`propobj.c`), so a door set in a generated doorway culls the next room for free. Layout keys: per door `"object": {"model": ..., "keyflags": ...}`, per room `"objects": [...]`.
2. **P2.3 the bank layout and textures.** Section 4 as a layout (street, lobby with tellers, offices, loading dock, security room, manager's office, vault antechamber, vault, tunnel), with stairs or a ramp down to the vault: the generator supports one floor height per doorway today, so it needs ramp rooms (sloped floor tiles and walls). Doorway frames, a counter (box props or bg boxes with stan holes), columns, pads for every post, patrol and panel. The material table in `modding/levelgen.md` is the start of the texture choice.
3. **P2.4 port the mode.** `mode_heist.lua` reads Bunker 2's door, camera and pad numbers; on the bank they come from `LEVEL` (`include("levelgen_layout")`). The areas (hall, offices, security, vault ...) map to rooms by name.
4. **P2.5 perf.** 100+ guards on post over the bank's rooms; the slot reserves 200 chr slots (`port_flatland.c`), and the dyn pools grow only above 240 (`portColosseumBigSlots`): check frame time and the pools with the full count.

**Limits now.** Box rooms only; the same floor height on both sides of a doorway; open doorways (no frame, no door; doors came in P2.2, section 13); per-material texture scale set by hand; one generated level on the slot at a time (the layout's `name` stays `levelgen`); baked vertex-colour lighting.

## 13. P2 job 2: setup objects for generated levels (built)

`tools/gen_level.py` writes the setup objects a layout asks for as the stock records word for word (the same `_mkword` layout and tails as the compiled stock setups), so the stock setup code builds them: a door in a doorway (sliding or a shutter; key flags; an optional padlock + `LockDoor`; `PROPFLAG_CULL_BEHIND_DOOR` on a bound pad just inside the passage, so `setupDoor` finds the doorway's portal and closes it while the door is shut), CCTV cameras, alarm boxes and monitors on wall bound pads (the stock wall mount), keys on floor pads, Bunker 2's safe (body + swinging door), generic props, and named loot pad lists. Monitors and alarm boxes get a stock Tag record: an untagged object is not a use target. The layout keys, the model ids and `LEVEL.objects` are in `modding/levelgen.md` ("Objects"). New bindings: `obj.get(i)`, `obj.test_damage(i, amount [, p])`.

**Proven** on `tools/levelgen/objects.json` (the L plus an office D behind a GREEN door and a vault E behind a vault door and a padlocked shutter), `tests/levelgen/run_objects.sh`, 33 checks: shut doors cull and open ones show the room; the GREEN door blocks Bond until he holds the card from its pad; the camera spots him and raises the alarm; a use press on the monitor names it and opens the feed; a raiser sounds the alarm box, and a shot-out box no longer works for the next one; the safe opens with GOLD; the padlock blocks the shutter until shot off; no card opens the vault door, the cracker's `gadget_used` names it, `door.open` opens it (the mode's crack), the loot pads take gold, `door.snap` shuts it. The walk test still passes on this layout.

**What P2.3 needs.** (1) Ramp rooms (sloped floor tiles, walls cut to the slope, a portal at each end) so the vault can be a floor down: the generator refuses doorways between different floor heights today. (2) The bank layout itself (section 4): street, lobby with tellers, offices, loading dock, security room with its console and monitors, manager's office with the safe, antechamber, vault with a bullion cage (a `cell` door with GOLD, or a padlock), maintenance tunnel; posts, patrol and panel pads for every area (named pads and loot lists are what the mode will read). (3) Furniture: a teller counter and desks as box props or bg boxes with stan holes (the generator has no floor holes yet), doorway frames. (4) Double doors (stock pairs link by `linkedDoorOffset`) for the lobby and the vault. (5) The "marble bank" look from the material table. P2.4 then swaps `mode_heist.lua`'s Bunker 2 tables for `LEVEL.*`.

## 14. P2 job 3: the Janus bank layout (built)

**Generator features** (`tools/gen_level.py`, the format in `modding/levelgen.md` "Ramps, stairs, furniture, frames"):

- **Ramps and stairs.** A room's floor may slope along x or z (`ramp: {axis, to}`); the ceiling and side walls follow it; doorways sit on its level end walls. `stairs: n` draws n treads and risers over the slope while the stan stays the smooth ramp (the stock way). So the vault is a floor down (y -340) by a 17-step stair, and the escape is a smooth service ramp.
- **Furniture blocks** (`blocks`): a box with a top and sides, and a stan hole under it. The teller counter (two blocks with a 150-wide flap between them), the hall's four pillars and the records shelving are blocks; desks, cabinets, crates and the van are stock props (`prop`, collision on).
- **Doorway frames** (`frame`): posts and a lintel round the opening on both faces, the reveals in the same material (wood 1530 in the public rooms, steel 478 on the RED, blast and vault doors).
- **Double doors** (`double`): two leaves at the jambs, linked the stock way (`linkedDoorOffset` +1 / -1, as Bunker 2's pairs). **See-through doors** (`see_through`: `PROPFLAG_NO_PORTAL_CLOSE`, for the windowed office doors). **Open-top rooms** (the street and the alley under the night sky), a lamp height, an `area` tag per room, every object's x, y, z in `LEVEL.objects`, and rooms on different floors may share x, z.
- A doorway's grid lines are made the same in both rooms (the hall's door into the office corridor met a grid line from the corridor's other door, and the two floors did not join until this). Flat layouts generate byte for byte as before (checked on `objects.json`).
- `tools/levelgen_plan.py`: the plan as SVG and PNG, no dependencies.

**The bank** (`tools/levelgen/janus_bank.json`, x east, z north; ground floor y 0, vault level y -340):

| Room | Area | Size | What is there |
| --- | --- | --- | --- |
| street | street | 4900 x 800, open top | Bond's start, the van (a stock supply truck, exit A), street lamps, the bank's stone front |
| alley | street | 300 x 2860, open top | the quiet way round to the dock's padlocked shutter |
| vestibule | hall | 800 x 340 | the front double door from the street (unlocked); two blast shutters into the hall (they start open; the lockdown closes them) |
| hall | hall | 2000 x 1280, 520 high | white marble, a coffered wood ceiling, a stone floor; the teller counter (wood, marble top) with its flap at the east end; 4 pink marble pillars; the wall of 8 security screens (west wall, customer side); 3 teller desks; 2 cameras; the tellers' alarm box |
| office_corr, office_a, office_b | offices | 300 x 1280, 1020 x 680, 1020 x 560 | the GREEN double door from the teller side, a GREEN door to the staff corridor; desks, cabinets; a camera and an alarm box in office A |
| back_corr | corridor | 3560 x 310 | the staff spine: the staff door to the teller side, records, the manager, the RED stairs, the dock; a camera at the dock end looking down it; an alarm box |
| records | records | 1200 x 910 | three rows of shelving (blocks); no camera; the floor key (0x1) at the back |
| manager | manager | 860 x 710 | a BLUE door; wood panelling, carpet; the safe (GOLD), a desk, a bookshelf |
| sec_office, guard_room | security | 920 x 600, 920 x 640 | the BLUE double door from the teller side; the camera console, 2 screens, a camera, an alarm box; the guard room behind |
| dock | dock | 1160 x 770, 420 high | exit B; the alley shutter (padlocked); the RED shutter to the service ramp; crates; a camera, an alarm box |
| stairs | vault_stairs | 400 x 1110 | the RED double door (Frigate steel) at the top; 17 steps down to y -340 |
| ante_a | vault_ante | 800 x 660 | the cracker zone (pad `crack`) in front of vault door A; the vault-approach camera; an alarm box |
| vault | vault | 1200 x 960 | between vault doors A and B (doubles, the VAULT key: only the cracker); 8 loot pads, crates |
| deposit | deposit | 760 x 800 | vault door B; two cells, cell_green (GREEN) and cell_floor (the floor key), 3 cash pads each; the foot of the service ramp |
| service_ramp | service_ramp | 400 x 1060 | a smooth ramp up 340 to the RED shutter into the dock |

**Routes.** *Loud, front:* van → front door → vestibule → blast shutters → the customer side under two cameras, between the pillars → the counter flap. *Quiet, back:* the alley → shoot the dock shutter's padlock (a silenced PP7 shot is heard within 100 units) → the dock (camera, alarm) → the staff corridor (its camera looks down it from the dock end) → the staff door onto the teller side, or on to records (no camera) and the RED stairs. *Cards:* GREEN (head teller or lobby captain, in the hall) → the offices (security chief: BLUE) → the security wing and the manager's office (the branch manager: RED) → the stairs; the vault captain's GOLD opens the safe on the way out. *Out:* vault door B → the deposit hall (cells) → up the service ramp → the RED shutter → the dock: exit B. It is a loop, so waves and squads can come from both ends.

**Cameras (7):** cam_hall_s (customer side), cam_hall_n (tellers), cam_offices, cam_corridor, cam_security, cam_dock, cam_vault (the cracker zone). **Alarm boxes (6):** hall (teller side), offices, corridor, security, dock, vault antechamber. **Monitors:** 8 in the hall, 2 in the security office. Every keyed door carries its tier's flags in the layout (the plan colours them).

**Sight lines.** The counter is a stan hole, so the stock line of sight (guards and cameras) stops at it whatever its height: each hall camera covers one side (the south-east one the customers, the north-west one the tellers), and the flap is the only way through. The pillars break the customer side into lanes. Records and the guard room have no camera: the quiet rooms.

**Pictures.** The plan: `designs/janus_bank_plan.png` / `.svg` (`python3 tools/levelgen_plan.py tools/levelgen/janus_bank.json designs/janus_bank_plan`). Screenshots (on AC's machine in `E:\midas-temp\heist\p23\`, not in the repo, as they show the ROM's textures): `shots_look1/` (16 spaces), `shots_bank/` (the test's). Looked at: the street is an open night street between brick walls with the bank's stone front and the wood-framed front door; the vestibule shows the open blast shutters (caution-striped lintels) onto the hall; the hall reads as a bank hall (pale marble walls, a dark coffered wood ceiling, a beige stone floor, pink marble pillars, a long wood counter with a marble top and its flap, desks behind it, the black screen wall, the red alarm box, a camera); the offices are carpeted beige rooms with grey desks and a camera; the staff corridor is a long white-tiled passage with framed doors; records are grey slat shelving rows; the manager's office is wood-panelled with the safe; the security office is grey with the BLUE double door; the dock is black corrugated metal with the RED shutter and crates; the stairs drop between stone walls under a sloping ceiling; the antechamber and the vault are Frigate steel with the double vault doors (caution stripes) in heavy steel frames; the deposit hall is pink granite with two barred cells; the service ramp climbs between concrete walls to the RED shutter. The monitors are black (no image, as on the P2.2 test level).

**Tests.** `tests/levelgen/run_bank.sh` (`levelgen_bank.lua`): the layout's counts, the six double doors' leaves linked, the heist map resolving (`tools/heist_bank_map.lua`), a shut double door culling the stairs and one leaf opening both, the counter and a shelf stopping Bond and the flap letting him through, the full route on stick input (every room in order; his height on the stairs and the ramp within 16 units of the slope), and three unarmed guards routing between areas (records → staff corridor → the hall's customer side through the flap; up the service ramp; up the stairs). Run 2026-10-25: bank 14 of 14; with `objects.json` on the slot, levelgen walk 11 and objects 33; the heist suites (bindings 38, guards 37, ko 26 alone, winpath 51 / trapped 17 / civ-fail 5 / quiet 10 / count 2, cctv 40, determinism 12), `tests/coop_determinism.sh` 0 failed, dam1 identical.

**P2.4: the mode's Bunker 2 tables → the bank.** `tools/heist_bank_map.lua` builds these from `LEVEL` by name (`include("heist_bank_map")` at the script's top level, `heist_bank_map()` once the stage is up); the mode keeps its logic and swaps the tables.

| mode_heist.lua (Bunker 2) | The bank (LEVEL names) |
| --- | --- |
| `DOORS.front` {72} | `front_door` (double) |
| `DOORS.blast` {80-85} | `blast_w`, `blast_e` (shutters, start open) |
| `DOORS.green` {46-49} | `green_hall` (double), `green_back` |
| `DOORS.blue` {50-53} | `blue_hall` (double), `manager_door` |
| `DOORS.red` {58-61} | `red_stairs` (double), `red_ramp` (shutter) |
| `DOORS.vault_a` {68, 69} / `vault_b` {70, 71} | `vault_door_a` / `vault_door_b` (doubles) |
| `DOORS.safe` 102 | `mgr_safe` |
| `DOORS.cell_green` 77 / `cell_floor` 75 | `cell_green` / `cell_floor` (its key: `floor_key`, in records) |
| (none) | `dock_shutter` (padlocked; new) |
| `AREAS` hall / central / offices / security | hall (+ vestibule) / corridor (back_corr) / offices (3 rooms) / security (sec_office, guard_room) |
| `AREAS` west / south / east / cells / safe | dock (+ alley) / records / vault_ante (ante_a, stairs) / deposit (+ service_ramp) / manager |
| `AREAS[].panel` (pads 64, 67, 74, 82, 104, 92, 86, 99) | the alarm boxes' bound pads: `alarm_hall`, `alarm_corridor`, `alarm_offices`, `alarm_security`, `alarm_dock`, `alarm_corridor` (records has none), `alarm_vault` (antechamber and deposit) |
| `VAULT_ROOMS` {55-58, 47} / `VAULT_PADS` / `VAULT_CRATE_PADS` | room `vault` / its pads / `LEVEL.loot.vault_gold` |
| `CIV_PLACES` hall / offices / security / south / east | `teller_1-3`, `cust_1-5`, `teller_mid`, `teller_back` / `office_a_1`, `office_a_2`, `office_b_1`, `office_b_2` / `sec_1`, `sec_2` / `rec_1-3` / `mgr_1`, `mgr_desk` |
| carriers: teller {58, 59, 65}, captain {54, 55, 62}, chief {72-76}, manager {78, 79, 83}, vault captain {9, 18} | `teller_1-3`; `hall_mid`, `hall_post_sw`, `hall_post_se`; `office_a_1`, `office_a_mid`, `office_b_1`, `ocorr_mid`; `mgr_desk`, `mgr_1` (his own office, behind BLUE); `vault_captain`, `vault_g3` |
| `P.start` {51, 118-120} | `start`, `start_1-3` (the street) |
| `P.exit_a` 51 / `P.exit_b` 116 | `van` / `dock_exit` |
| `P.crack` 85, `P.crack_xz` | `crack` and its position |
| `P.inside` | `vault_in_a`, `vault_in_b` |
| `P.vault` (circles) | not needed: the vault is one room |
| `P.console` 82 | `console` |
| `P.monitors` (the hall wall at x -477) | `screen_1-8` (`LEVEL.objects[...].x, z`) |
| `P.waves_front` {60, 61, 53, 57} / `P.waves_dock` {102, 103, 105} | `hall_entry_w`, `hall_entry_e`, `vest_mid` / `dock_exit`, `dock_mid`, `dock_2` |
| `P.squad` {86, 87, 13, 80} / `P.heavy` {85, 81, 86, 80} | `stair_top`, `back_3`, `back_4` / `ante_1`, `ante_2`, `dep_1`, `dep_2` |
| `P.safe_loot` {12} / `P.cell_loot` | `LEVEL.loot.safe_loot` / `cell1_cash`, `cell2_cash` |
| `S.intel_room` 6 | room `hall` |
| `CAM_LABEL` / `CAM_AREA` (cameras 87-97) | the 7 cameras by name: BANKING HALL, TELLERS, OFFICES, STAFF CORRIDOR, SECURITY, LOADING DOCK, VAULT ANTECHAMBER |
| `GUARD_AREAS` | hall, offices, security, corridor, dock, vault_ante |

P2.4 also has to change: the stage (`GE_STAGE=levelgen`); the start (Bond starts in the street, not in a cell); exit B (the dock is reached from inside without a card, from outside through the padlock); the safe (GOLD alone; Bunker 2's needs 0x30); the room ids in the vault and intel tests.

**Limits.** A ramp runs along one axis with doorways only on its two end walls; stairs are drawn over a smooth stan ramp (feet sink or float by up to half a step, 10 units here). A block is a stan hole, so it blocks the stock line of sight however low it is (the counter is full cover for sight: a guard or camera cannot see across it, though Bond could see over it); blocks sit on level floors only. Furniture props are placed by hand (nothing checks that a prop stands in a walking line; the route and the guards' waypoint links avoid them in this layout). No swinging room doors, no glass. The monitors show black (fixed in P2.4, section 15). The textures' scale is set by hand. The slot holds one level: running `tests/levelgen/run.sh` / `run_objects.sh` needs `objects.json` generated and built first. Not yet measured: frame time with the full guard count in the bank (P2.5, section 15).

## 15. P2.4 / P2.5: the heist on the Janus bank (built)

**The heist is the bank only** (AC, 2026-10-25). `tools/mode_heist.lua` has no Bunker 2 tables any more: at stage start it takes every map table from `heist_bank_map()` (`DOORS`, `AREAS`, `AREA_LABEL`, `VAULT_ROOMS`, `VAULT_PADS`, `CIV_PLACES`, `P`, `CARRIERS`, `CAM_LABEL` / `CAM_AREA`, `GUARD_AREAS`, `intel_room`, `MAP`) and runs only on the generated slot (`GE_STAGE=levelgen`, stage 52) with the bank on it; anywhere else it sets nothing up. `Play Heist.bat` launches the bank (the Bunker 2 launcher is gone). Sections 9 to 11 describe the rules, which did not change; their room and pad numbers are Bunker 2's and are history now (`designs/bank-heist-bunker2.md` keeps the probe's map). The vault test is by room only (`VAULT_ROOMS`; Bunker 2's circles are gone).

**What changed for the bank** (section 14's list): the start is the street (`start`, `start_1-3`); exit A the van, exit B the loading dock (from inside through the staff corridor's unlocked door; from the alley through the padlocked shutter; from the deposit hall up the service ramp and its RED shutter); the safe takes GOLD alone; the branch manager (RED) sits in his own office behind BLUE (`CARRIERS.manager`: `mgr_desk`, `mgr_1`); the card carriers stand on named pads; the camera feeds open at the hall's screen wall (8 screens; `P.monitors`, `P.screens_at`); waves come in at the hall's two entry pads and the vestibule, the response also at the dock; crack squads at the stair top and the staff corridor; the heavy squad at the antechamber and the deposit hall; the vault's 30 bars are stacked round its 8 loot pads (`LEVEL.loot.vault_gold`, `P.vault_loot`), the safe's on `safe_loot`, the cells' cash on `cell1_cash` / `cell2_cash`. The intel box names the bank's areas and says records and the deposit hall have no camera.

**Guards per difficulty** (unchanged rule, section 8 answer 15): Agent 30 posted + 4 raisers + the carriers (37 placed), 007 89 posted + 12 raisers + the carriers (104 placed). The areas' shares: hall 14 (3 raisers), staff corridor 9, offices 7, security 5, dock 12 (2), records 8, antechamber and stairs 14 (2), deposit hall and ramp 7, the manager's office 1. Posts come from each area's named pads first, then its grid pads, minus any grid pad within 70 of a furniture block, 120 of a prop or the safe, or 140 of a doorway (a guard there blocks a door or stands in a counter; `M.dropped` counts them). Raisers run to their area's alarm box (its bound pad). **The cameras matter:** the vault-approach camera watches the cracker's spot and the 90 s crack is far longer than its 10 s; a quiet run has to switch the cameras off at the BLUE console first (the quiet test does), or shoot it out. The counter is a stan hole, so each hall camera covers one side of it.

**Hold fire, widened** (section 8 answer 17). In the bank's hall the screenshot tour stood an invincible Bond among the tellers with the alarm up: guards' misses killed all four in about 10 s (killer -1). The rule held a shot only for a civilian within 40 units of the line and short of the target; a teller right beside Bond did not count, and a miss lands round him. Now (`port_heist.c`): within 60 of the line, or within 90 of the target on any side. The win path checks it: Bond among the tellers for 15 s with the alarm up, 0 killed, 695 shots held, 0 fired. The trade: staff beside a Bond shield him (the guards keep their aim and fire when he steps away). One civilian still died in the win path while Bond was warping round the hall (a shot at him from far off; not by the line); not chased.

**The generator** (`modding/levelgen.md`): a doorway-clearance check (a prop's footprint from its stock model's bounding box, or a block, within 60 of a doorway's opening and approach on either side refuses the layout; it found a teller desk across the staff door's jamb, AC's report, and the dock's boxes in front of the alley shutter: both moved); monitors default to a stock picture (image 16) instead of 51 (black), and the bank's 10 screens each have one.

**Tests** (all on the bank; the runners set `GE_STAGE=levelgen` and copy `levelgen_layout.lua` and `heist_bank_map.lua`; `GE_FIXEDTICK=1`; `GE_NOSLEEP` is no use here: it pumps frames faster than the boot uses them, and `GE_FRAMES` runs out first; run the suites in parallel instead): see the status line below. New: `tests/heist/run_bank_shots.sh` (6 shots), `tests/heist/run_bench.sh` (P2.5). Gone: Bunker 2's `run_shots.sh` tour.

The other suites moved to the bank's hall by pad name (bindings: `cell_floor`, `cam_hall_s` and its look pad, the RED stairs door and a key guard in the staff corridor; guards and ko: `hall_mid`, `cust_*`, `hall_entry_*`, `hall_post_*`, `vest_mid`; cctv: the cameras by name, the hall as the intel room, the dock as "elsewhere"). What Bunker 2 hid and the bank showed: a stock alarm raiser sounds the alarm only at an alarm box (a floor pad as its `alarm_pad` gets him there and nothing more; the mode's raisers use the boxes' bound pads, the tests do now); `pads.pos` takes floor pads, not bound ones; the hall's cameras see a test Bond and raise the alarm unless switched off; a card dropped by a search lands under Bond and he picks it up at once.

**Status 2026-10-25 (all on the bank):** win path 59 (the crossfire check included), trapped 17, civilian fail 5, quiet 11, 007 count 2 (104 guards: 89 posted, 12 raisers, the carriers); bindings 38, guards 37, ko 26, cctv 40; determinism 12 (solo twice, local co-op twice and view-switched, netplay host / guest, no DESYNC); `tests/coop_determinism.sh` 0 failed; dam1 identical; levelgen bank 14.

**P2.5: frame time with the full force** (`tests/heist/run_bench.sh`: the arena bench's `bench` API; Bond turning at 24 deg/s; headless, so the GL column is Mesa's software renderer and the frame rate is not a GPU number; "sim" is the game code's longest stretch per frame, display-list building included):

| Difficulty, phase | Guards alive | Frame avg / p95 ms | Sim avg / max ms | GL avg ms | Draws avg / max | Display list / vtx pool peak |
| --- | --- | --- | --- | --- | --- | --- |
| Agent, hall (screens) | 36 | 23.1 / 41.6 | 22.2 / 66.2 | 13.5 | 573 / 1060 | 73 / 31 KB |
| Agent, vault level | 36 | 18.0 / 25.4 | 14.3 / 82.9 | 5.5 | 204 / 727 | 44 / 27 KB |
| Agent, alarm + waves (hall) | 36 + waves | 19.0 / 27.6 | 16.1 / 121.4 | 8.3 | 549 / 1444 | 93 / 46 KB |
| 007, hall | 103 | 19.7 / 30.5 | 16.7 / 45.5 | 10.1 | 711 / 1866 | 114 / 61 KB |
| 007, vault level | 103 | 17.4 / 22.5 | 13.2 / 37.5 | 5.4 | 305 / 1272 | 80 / 53 KB |
| 007, alarm + waves (hall) | 103 + waves | 21.8 / 37.3 | 21.3 / 85.1 | 11.5 | 688 / 1610 | 161 / 95 KB |

Read: the full force costs little over Agent (sim 16-21 ms either way under llvmpipe, which shares the CPU with the game; the Agent hall phase was the slowest, so the noise is bigger than the guard count's cost). No pool comes near its cap (chr slots 141 / 460, props 384 / 2400, hit entries 1261 / 32000, the master display list 161 / 485 KB, the vertex pool 95 / 897 KB, the stage mempool 57 MB free). The bank's hall is the heaviest view (about 700 GL draws: 2060 vertices in 535 display-list commands, the pillars, counter and coffered ceiling, plus the chrs). No generated-level hotspot stood out above the noise, so nothing was changed for P2.5. A real-GPU measurement needs a visible window on AC's machine (`GE_PROFILE` or the arena bench launcher pattern with `GE_MODE=heist_bench`).

## 17. Bank v2: the big bank (built, branch `bank2`)

(Section 16, the stealth layer, comes after this one: the two were written side by side and merged.)

AC (2026-10-25): "The bank map is AWESOME already but I'd love it to be HUGE, like much much bigger than this. Many more offices, corridors, negative space, and a much bigger underground, lots more verticality, etc. It feels claustrophobic atm." v1 was 20 rooms on two levels. v2 is **107 rooms on six levels**, written by `tools/levelgen/build_janus_bank.py` (edit it and rerun it; `janus_bank.json` is its output), generated by `tools/gen_level.py` with the new features in `modding/levelgen.md` ("Openings, stair cores, zones, big layouts"). Plans: `tools/levelgen_plan.py` writes a sheet per level, an elevation and the set (AC's copies sit next to the launchers in `build_win`).

### 17a. The building (x east, z north; footprint 8800 x 6260 above ground, the underground reaches 13 m further north, the canal 14 m east)

| Level (y) | What is there |
| --- | --- |
| G (0) | **The banking hall**: 2400 x 3820, **13.2 m high** (1320), white marble, a coffered ceiling, six stone pillars to the ceiling, writing tables. Its west and east walls are **arcades** (four arches each, 440 wide) onto the public aisles (customer desks); above the arches the galleries' balcony openings; above those the second floor's windows. Its south wall: the two blast shutters from the vestibule, the screen wall (8 monitors), the south gallery's openings. Its north end: two teller enclosures behind counters with flaps (`flap_s/n`, `flap2_s/n`), GREEN west to the offices, BLUE east to the security office, staff doors north, and between them the wide arch to the **grand stair**. West wing (offices, GREEN): the office corridor, offices A and B, a meeting room, the typing pool (open plan, 1000 x 2980), records. East wing (security, BLUE): the security office (the console, three screens), its corridor, the guard room, the armory, the **barracks**, the cash office, and the **double-height garage** (an armoured van, a truck, a shutter to the alley). North range: the staff corridors either side of the stair's foot, the canteen and kitchen, the manager's office (safe), the staff lounge, the lockers, the RED stairs down, the loading dock, the rear corridor. Outside: the street (11.6 m long, the van), the alley, the embankment steps down to the canal. |
| F1 (480) | **The galleries**: west, east and south, ringing the hall's void, each looking down into it through balcony openings (a 100-high rail); the north gallery corridor with two more; the **mezzanine** at the top of the grand stair. Two long staff corridors behind them (6260 long); loan offices, a meeting room, the open-plan floor (1000 x 4260), training, the conference room and the training room behind the stair, advisers' offices, the IT office, a file archive; the east stair up to F2. Facade windows onto the street. |
| F2 (960) | The corridors round the hall's top with **windows into it** (the loggia on the north side has two 600-wide ones), facade windows; open-plan offices west (1520 x 4260, partitions) and east, accounts (over the garage), the **server room** (racks, restricted), IT, an archive, the **library** (3600 x 1200, stacks), comms (restricted), the IT store. |
| F3 (1440) | The executive corridor (8800 long), the secretary, the **director's office** (a second safe, loot), the boardroom, the executive lounge (a bar), and the **roof** (8800 x 4260, open to the night sky, a skylight, plant boxes) through a BLUE door. |
| B1 (-420) | The **vault level**: the RED stairs land at a checkpoint lobby (a gate), the vault corridor (5760 long) runs west to east: the antechamber (`crack`) and vault door A, the **vault** (2000 x 1200, 8 gold pads, cages), vault door B to the deposit hall and its two cells, the cash-processing room and the service ramp up to the dock (RED), the safe-deposit boxes (the floor key, loot), the counting room, and the plant gate (RED) down to B2. |
| B2 (-840) | The **plant level**: the plant corridor, the **power room** (`power`: dynamos, a transformer, switchgear, the main **`breaker`**, 6 m high), the **`generator`** room behind it, the deep archive vault, the **maintenance stair** up to the canteen (GREEN at the top), and the **maintenance tunnel** (6000 long in two lengths, a padlocked grate between them) out to the **canal**, an open-air cut 8.4 m below the street under the night sky, whose embankment steps climb to the street: a third way in. |

**Stairs and verticality.** The grand stair (24 steps, 1000 wide) from the hall's north arch to the mezzanine; the west **stair core** (a switchback: flight, landing, flight per storey) from G to F3; the east stair F1 to F2; the RED stairs G to B1; the plant stair B1 to B2; the maintenance stair B2 to G (2400 long); the service ramp B1 to the dock; the embankment steps street to canal. 13 ramp rooms in all. Sight lines: from the hall floor you see three tiers (arches, balconies, windows) and the coffered ceiling 13 m up; from a balcony the far balcony and the whole floor; from the loggia down into the hall; from the south gallery out through the facade to the street. (Guards and cameras do not see through openings: the stock sight test walks the floor tiles.)

### 17b. Zones, routes and the stealth layer (section 16)

Every room has `zone` (public / staff / restricted) and an `area` tag on section 16c's cover classes (`AREA_CLASS` in `heist_bank_map()` repeats the zones per tag). **Public** (17 rooms): the street, alley, embankment, canal, vestibule, hall, aisles, galleries, mezzanine, grand stair. **Staff** (61): the offices on every floor, the staff corridors, records and archives, the canteen, the manager, the dock and garage, the executive floor and roof, the stair core. **Restricted** (29): the security wing (office, corridor, guard room, armory, barracks, cash office, lockers), the server room and comms, the RED stairs and the whole vault level, the plant level and the tunnels. The plan tints the three.

**Routes.** *Loud:* the van, the front door, the blast shutters, the hall. *Staff shortcuts:* the hall's staff doors behind the counters into the staff corridors, the GREEN aisle door into the office corridor, the stair core between every floor, the rear corridor behind the north rooms. *Restricted only through keys or maintenance:* BLUE into the security wing (from the hall, or the back door from the east staff corridor), RED down the stairs or the service ramp; the maintenance stair (GREEN, from the canteen) goes straight down to the plant level, where the power room (RED) and the plant gate up into the vault corridor (RED) are; the canal tunnel (shoot the grate's padlock) reaches the plant level from outside. *Out:* the van, the dock, the canal.

**The security room** (`sec_office`): BLUE from the hall's east teller enclosure, BLUE back door from the east staff corridor, open to the security corridor (the armory, the guard room, the barracks); the `security` area's posts (7 shares: the office, its corridor, the guard room, the armory, the cash office) are its staff, the camera link of section 16. **The barracks** (`barracks`, off the security corridor; its door `guard_access = "role:barracks,squad,wave,response"`: the reinforcements come out, the wing's posted guards are not posted inside): `P.squad` (the crack squads: the stair top and two barracks pads) and `P.barracks`. **Doors** carry `guard_access` (16c's values; a role is a guard area of `heist_bank_map()` or `squad`, `vault`, `wave`, `response`, `barracks`): `never` the vault doors, the safes, the cells, the padlocked dock shutter and tunnel grate; GREEN `role:` every staff area plus squads, waves, response; BLUE `role:security,offices,upper1,upper2,executive,wave,response`; RED `role:vault_ante,deposit,vault,plant,squad,response`; the power room's door `any` (the investigators open it); every other door `any`.

**The power** (section 16c names, all provided): room `power` (area `power`, restricted, no camera; the plant corridor outside has one), `breaker` (prop model 30 between the switchgear), `breaker_front`, `power_mid`, `power_way` (the plant corridor, where investigators come from), `generator` (prop 48 in the `generator` room behind the power room) and `generator_front` (both fronts within `power_reach` 150 of their props).

### 17c. The engine's limits, tests, performance, what is left

**Limits used** (read off the code; `modding/levelgen.md` has the table, `gen_level.py` checks them): 107 rooms of 148 (`MAXROOMCOUNT` 150), 182 portals of 200 (`PORTMAX`; 154 doorways + 28 openings), 12344 stan tiles of 16380 (u16 links), 4356 + 135 pads of 10000, coordinates within +-10000, 27 cameras of 32 (co-op's per-Bond camera timers), a portal is drawn at most 15 portals deep. The slot got its own memory row in `boss.c` (`-ma1000`: 2 MB of room geometry; it used to inherit the last stage's, doubled). Biggest rooms: the street 3056 vertices, the hall 7900 (its floor carries every doorway's and block's grid lines; the ceiling is coarse inside), the roof 2872.

**Tests (2026-10-25, bank2 after the merge of heist 34d74b9):** `tests/levelgen/test_gen_level.py` 11/11; `tests/levelgen/run_bank.sh`: static 14/14, route A (the whole building on foot, 53 rooms in order, 10 ramp rooms, |y - slope| < 0.2) 2/2, route B (the plant level, the tunnel to the canal, the maintenance stair) 3/3, guards 4/4 (records to the hall round the counter, up the service ramp through the RED shutter, up the RED stairs, down the stair core three storeys); heist win path 59, trapped 17, civilian fail 5, quiet 11, 007 count 2; bindings 38, cctv 40, ko 26; cover 11 of 12 runs clean (hall 8, gun 7, loot 5, restricted 4, staff 7, ko 6, cam 7, camdown 9, power 12, interrupt 5, generator 8; doors: see below); guards 34-35 of 37 (see below).

**Open (honest):** (1) *cover `doors`*: an on-screen security guard sent through the BLUE double door stops against it shut at (1188, 2666) and never opens it; the hall guard is refused and the barracks squad (off screen) comes out as it should. On v1 the same guard was off screen (the stock pad hop). The door now stands in the wall's middle on its waypoint pad (that fixed the same stop at the RED service-ramp shutter in the bank test), but not this one: the on-screen route to a ruled (AI-locked) double door needs a look in `chraction.c` (the door finder's line to the next pad). (2) *guards: hold fire*: with a civilian 25 off the shooter's line 70 out, the guard does not hold (68 fired, 0 held); the same check passes on v1; the civilian is registered (idle) and the geometry is inside `HOLD_RADIUS`; not found. (A related real bug was fixed on the way: a guard spawned on a removed civilian's chr number stayed a "civilian" for the hold-fire and target tests, `portHeistCivilianForget`.) (3) *guards: body found*: the witness noticing a body is a random roll, now given 6 s and Bond 9 m away; still failed 1 run in 3. (4) Guards and cameras do not see through openings (the stock sight test walks floor tiles): a guard on a balcony does not see Bond in the hall below.

**Performance** (`tests/heist/run_bench.sh`, headless llvmpipe on a machine shared with two other agents' containers using about 8 cores, so the absolute times are inflated): hall phase draws avg 565 (v1: 573), vault 139 (v1: 204); frame avg 63 ms hall / 34 ms vault / 54 ms alarm+waves at Agent, 59 / 35 / 50 ms at 007 (v1 on a quiet machine: 23 / 18 / 19 ms). A sample of the game thread (30 backtraces) was almost all in GL submit and the swap, not game code: the bank's sim cost is small; the GL cost per view is about v1's (the draw counts match). Pools: master display list peak 154 / 485 KB, vertex pool 83 / 997 KB, chr slots 142 / 460, props 610 / 2400. A real-GPU number needs AC's machine.
## 16. The banker cover, the security room and the power (built)

### 16a. AC's answers (2026-10-25)

1. **Banker cover (disguise).** The crew starts as bank staff (a banker in a suit), gun holstered. Guards and cameras see Bond but treat him as staff until his cover is blown. Later, not now: taking a uniform or disguise from a knocked-out staff member, and choosing loud or quiet at the van.
2. **Cover blowers.** Drawing or firing a weapon where seen (shooting is an instant blow; going loud is a choice); **carrying gold is an instant blow when seen**; robbing or slapping staff in view; being seen in a **restricted** area (strict: the security wing and the whole vault level are restricted; the offices, records and the staff corridor are staff-only: suspicion builds; the hall, the vestibule and the street are public: nothing); running or lingering close to a guard: suspicion. A body found, or the alarm: everyone is alert and the disguise no longer protects.
3. **Discovery.** Only the guard who sees through you turns hostile; he needs a few seconds to radio or reach an alarm panel: knock him out or kill him first and the cover survives. A suspicion meter per guard (sim state, hashed), a HUD indicator (an eye and meter for the most suspicious guard watching you), cover per player in co-op.
4. **The security room controls the cameras.** A camera spotting something does not raise the alarm by itself: it reports to the security room, whose staff (the guards posted in the security wing) raise the alarm after a short delay. With everyone in the security wing dead or knocked out, cameras cannot raise alarms (they still see; the feed intel still works). The BLUE console's 90 s camera switch-off stays as it is.
5. **Cut the power to disable alarms entirely: an arduous job.** A plant room with a main breaker; cutting it takes a long hold (tune 45-60 s, interrupted if seen or hurt, with a first step: opening the cabinet). While the power is out the alarm panels and the siren do not work and the cameras go dark (static on the feeds). A backup generator restores the power after a while unless it is disabled too, and guards are sent to investigate the outage: a meaningful choice, every number in the settings table.

### 16b. How it works

**The disguise** (`port/src/port_heist.c` "cover"; Lua `cover.*`, `modding/lua.md` "The banker cover"). The mode registers the bank's own guards (posted, raisers, card carriers, the outage investigators; not squads, waves or vault guards, who come for a known crew) with `cover.guard`. The stock sight test every guard list uses (`chrCheckTargetInSight`, the `IFISeeBond` of the simple guard and the alarm raiser lists) asks `portHeistCoverHides` first: a Bond whose cover holds with that guard is "not seen", before the stock random roll (so the sim RNG is drawn less in a cover run; dam1 never registers a guard and is identical). Instead the port keeps a meter per guard and per Bond, scanned every 4 ticks: while the guard *could* see the Bond (1800 units, 75 degrees either side of his facing or anything within 100 units, 350 in height, the stock guard-to-Bond stan line as `chrCanSeeBond` draws it), the Bond's conduct fills it:

| Conduct (in the guard's sight) | Full in | Setting (`S.cover_tune` / `cover.tune`) |
| --- | --- | --- |
| loot carried (any bar or cash picked up: `cover.carrying`) | at once | |
| an act: a slap (counted or the knock-out), a civilian robbed, a body searched, a gun fired | at once (the act counts for 1.5 s) | `act` |
| a gun or a knife in hand (the slappers, the door decoder are fine) | 0.4 s | `armed` |
| a restricted room (security wing, stairs, antechamber, vault, deposit hall, service ramp, plant room) | 1 s | `restricted` |
| a staff-only room (offices, staff corridor, records, manager's office, loading dock) | 5 s, added | `staff` |
| moving faster than 450 units/s within 400 of him (full stick is about 500: dashing right past a guard) | 4 s, added | `run`, `run_speed`, `run_range` |
| within 150 units of him for more than 2 s | 4 s, added | `close`, `linger`, `close_time` |
| nothing wrong, or out of his sight | drains in 12 s | `decay` |

The room classes come from the layout's room `area` tags (`S.area_class`). Full meter: **"Hey!"** (`cover_blown(guard, p, why)`, a flash on that Bond's HUD saying why). Only that guard: the hook stops hiding that Bond from him, his `lastseetarget60` is set so his next sight test goes straight to the stan line, and his own list attacks. A guard who hears a Bond fire (the stock hearing check: a silenced PP7 reaches 100 units) or is hit sees through the shooter at once. **The radio:** a witness still up (not dead, not knocked out) 5 s later (`radio`) reports it (`cover_report`) and the mode raises the alarm; a knocked-out witness reports nothing, but his body can still be found. **The alarm or a body found** puts everyone on alert (`cover.alert(true)`): the hook hides nobody from then on.

**The eye** (top left of the HUD, clear of the intel box, `port_console.c` `coverEye`, presentation only, the local Bond): closed and grey, COVER: no cover guard has him in sight; open, WATCHED: someone does, calmly; a bar filling white, yellow, orange, SUSPICIOUS: the highest meter of a guard still up; red, HEY! COVER BLOWN: a guard who is still up saw through him; red, ALERT: NO COVER. Hidden on a camera feed.

**The cameras and the security room.** With the cover on, a camera (solo too: the per-Bond path that co-op used) times only a Bond who looks wrong to a camera: a gun, loot, an act, a restricted room, or the alert; otherwise its timer runs back down. A spot does not raise the alarm (`cctv.alarm_on_spot(false)`): it goes to the security room, and `security_delay` (6 s) later the alarm goes up if a guard posted in the security wing is still up (the camera report dies if they are all down by then, or the power is out). With every security guard down a camera still sees (`cctv.sees`, the intel box) and raises nothing. The BLUE console's 90 s switch-off is unchanged.

**The power** (the plant room, `power`, off the staff corridor; restricted, no camera). A use press at the main breaker starts the work: first the cabinet (`power_cabinet` 8 s), then straight on to the main (`power_cut` 50 s): 58 s in all. The work stops when the worker leaves the panel (150 units, `power_reach`), a cover guard has him in sight, or he is hurt; it keeps what was done less `power_setback` (10 s) and goes on with the next use press. Cut (`power.set(false)`): `alarmActivate` does nothing (no alarm box, raiser, camera or script raises the alarm; the siren cannot sound), every camera is switched off (static on the feeds; the security console has nothing to switch), a camera report on its way dies. A witness who reports while the power is out finds the alarm dead: he calls it in when the power comes back, if he is still up. If the heist is already hot it stays hot (the waves are on their way), but the siren is silent until the power returns. **The way back:** the backup generator restores the power `generator_after` (90 s) after the cut unless it was disabled first or after (a use press at it, `generator_time` 20 s of the same kind of work); and `investigate_after` (10 s) after the cut `investigators` (2 at full force, at least 1) guards come from the staff corridor (`power_way`) and walk (`spawn.go`) to the breaker; one standing there for `repair_time` (15 s) puts the power back. They are cover guards: they find a crew in a restricted room. A new cut after a restore needs the main again (the cabinet stays open). Emergency lighting is not done (the bank's lighting is baked vertex colour).

**State.** Every meter, linger time, witness and report bit, each Bond's speed and act timer, the room classes, the power and the walks are in the heist state hash; the mode's text state (`mode.state`) adds the alert, blown and report counts, the camera report on its way, and the power's step, work, saved progress, cut time, generator and investigators.

### 16c. The names the map must have (for the redesign)

The mode works by name. On top of section 14's names the cover and the power need:

| Name | Kind | Used for |
| --- | --- | --- |
| room `area` tags | every room | the cover classes (`S.area_class`): `street`, `hall` public; `offices`, `corridor`, `records`, `manager`, `dock` staff-only; `security`, `vault_stairs`, `vault_ante`, `vault`, `deposit`, `service_ramp`, `power` restricted. A new area is public unless added to the table. |
| room `power` (area `power`) | room | the plant room: restricted, no camera, off a staff-only space |
| `breaker` | prop object (here model 30, the console with switches) | the main breaker: a use press within 150 units |
| `generator` | prop object (here model 48, a mainframe) | the backup generator |
| `breaker_front` | pad | in front of the breaker: where the investigators walk to (and the tests stand) |
| `generator_front` | pad | in front of the generator (tests) |
| `power_mid` | pad | inside the plant room, off the panel (tests) |
| `power_way` | pad | where the investigators come from (here the staff corridor, outside the plant room) |
| the security wing's posts | the `security` area's pads | its posted guards are the security room's staff (the camera link) |

The power objects are optional in `tools/heist_bank_map.lua` (a layout without `breaker` has no power cut); the cover needs only the area tags.

**Door classes for guards** (`guard_access`; AC's playtest, 16d). Each door the redesign places should say who among the guards may open it:

| `guard_access` | Meaning | On this map (by `DOORS` group, `S.door_access`) |
| --- | --- | --- |
| `"any"` | stock: any guard on his route opens it | `front`, `blast`, and every door without a rule (offices, records, dock, staff door, plant room) |
| `"never"` | no guard opens it, ever | `vault_a`, `vault_b`, `safe`, `cell_green`, `cell_floor`, `dock_shutter` |
| `"role:<role>[,<role>...]"` | only a guard whose role is listed: his post's area (`hall`, `offices`, `corridor`, `security`, `dock`, `records`, `vault_ante`, `deposit`, `manager`), or `squad` (crack / heavy squads), `vault` (vault guards), `wave`, `response` | GREEN `green`: the staff areas, waves, response, squads; BLUE `blue`: `security`, `offices`, waves, response; RED `red`: `vault_ante`, `deposit`, `vault`, squads, response |
| `"barracks"` | only the reinforcements that come out of it (role `barracks`) | none yet: tag the guard room's door if the redesign gives it one, and spawn its squads behind it |

Until the layout carries the tag, the mode's `S.door_access` table (by `DOORS` group) is the source; a tag in the layout would replace it.

### 16d. AC's playtest feedback (2026-10-25)

1. **Guards opened the vault door.** Guards who heard Bond inside the time-locked vault walked over and opened vault door A: the stock route (`chraction.c`, the waypoint follower, every 10 ticks) opens any door a chr walks into, keyed or not (`doorActivate`, no key test). Now, with `mode.guard_doors(true)` (the heist; off everywhere else, stock unchanged), a door with a rule (`door.guard_access(i, "never" | "any" | roles)`) asks `portHeistChrMayOpenDoor` first: a guard whose role (`spawn.role(chr, name)`) is not allowed does not open it, and he waits at the shut door. Two stock details had to be closed for that to hold: an on-screen chr's move test ignores ordinary doors (it expects to open them) and collides only with doors "locked to AI", so a ruled door also gets the stock AI-lock bit (flags2 `0x20000000`, `CDTYPE_DOORSLOCKEDTOAI`) while the rules are on; and an off-screen chr hops from pad to pad with no collision at all (`chrlvTravelTickMagic`), so a hop that passes within 90 units of a shut ruled door he may not open is refused (`portHeistHopBlocked`; he falls back to walking and the door stops him). An allowed guard off screen still hops through a shut door without moving it, as stock. The mode gives every guard a role at spawn (his post's area; `squad`, `vault`, `wave`, `response`) and every door group its rule (`S.door_access`, table above). A guard stuck outside the vault has already seen or heard Bond, so the cover's witness and radio decide whether the alarm goes up.
2. **The radio.** When a witness radios it in, the radio message sound that Caverns' level AI plays at its radio (`RADIO_MESSAGE_SFX`, 80: `UsetupcaveZ.c` `sfx_play(0x5000, 0)`) plays at the guard: through `port_gamesfx.c` (presentation priority, its own slots and RNG), its volume falling from 1 at 300 units to 0.25 at 3000 and panned by where he stands relative to this machine's Bond. When the security room relays a camera's report, a short burst: two `RADIO_SFX` squelches and the message, from a security guard (`cover.radio(chr, "relay")`).

### 16e. Tests and limits

`tests/heist/run_cover.sh` (`tests/heist/heist_cover.lua`, one scenario per run, `GE_FIXEDTICK=1`, on the bank), 87 checks, all passing on the final build:

| Run | Checks | What it shows |
| --- | --- | --- |
| hall | 8 | holstered, Bond walks the customer side at full stick (about 500 units/s) past 8 hall guards: the eye opens (watched), the highest meter 0.18, no "Hey!", no alarm, no hall guard fires a shot |
| gun | 7 | in a hall guard's sight, holstered: nothing; the PP7 drawn: that guard sees through him (`armed`), the guards that did not see it do not know; 5 s later the witness radios and the alarm goes up |
| loot | 5 | a gold bar picked up in sight: blown at once (`loot`) |
| restricted | 4 | in the security office: blown 0.5 s after a guard first had him in sight (`restricted`) |
| staff | 7 | in the offices: the meter at 0.42 after 1.5 s, the eye says suspicious, "Hey!" (`staff`) after 2.8 s |
| ko | 6 | a witness knocked out 2 s after "Hey!": no report, no alarm, no alert 8 s later |
| cam | 7 | the hall's guards removed, the customer-side camera: holstered it times nothing; armed it spots him; no alarm at the spot; the alarm 4 s later (the security room) |
| camdown | 9 | the same with both security guards knocked out: no alarm, the camera still sees him |
| power | 12 | a use press at the breaker, 2 s cabinet + 6 s main (test times): power out, all 7 cameras dark, `alarm.start()` does nothing, a feed shows static; the investigator comes and resets the breaker; cameras and alarm work again |
| interrupt | 5 | walking off the panel stops the work and does not cut; back at it, it finishes |
| generator | 8 | the generator restores the power after its delay; disabled (3 s), a new cut stays out |
| doors | 9 | a hall guard sent to the security office stops at the shut BLUE door, a security guard gets through; a barracks door (the plant room's, tagged for the test) keeps a corridor guard in and lets its own squad out; a loud noise inside the time-locked vault: both vault doors stay shut, 261 refusals, 5 guards waiting at door A |

**Regression on the final build:** win path 59, quiet 11, trapped 17, civilian fail 5, 007 count 2; bindings 38, guards 37, ko 26, cctv 40; heist determinism solo and local co-op (twice and view-switched) hash-identical over 4365 ticks with every step reached; netplay host / guest hash-identical over every tick both ran, scenario notes identical, every step reached, no DESYNC, but short of the runner's tick count (2497 ticks at the default frame budget, 3644 at `GE_FRAMES=8000`: the guest spends about half its frames held waiting for the host on loopback under llvmpipe; not compared against HEAD); `tests/coop_determinism.sh` 0 failed; levelgen bank 13 of 14 (`bank_walk`, real-time stick input, wobbles at a different seam each run on a loaded machine: records, the vestibule, the street/alley seam; the rest pass, the layout counts now 21 rooms and 24 doorways).

The older suites run with the cover where they test it and without where they test something else: `heist_winpath.lua` sets `cover = false` (it warps an armed Bond through restricted rooms; its quiet run went hot through a witness in the antechamber with the cover on). Screenshots in `E:\midas-temp\heist\cover\` (AC's machine, not in the repo).

**dam1.** The committed baseline (`tests/baseline/dam1.a.txt`) no longer matches at HEAD 5b7a138 itself: built and run alone, HEAD differs from it at tick 185 (one more RNG draw per tick there, and its live hash then matches the demo's recording, which the baseline's did not). This build's dam1 tick log is byte-identical to HEAD's own, so nothing here changes dam1; the baseline needs re-cutting by whoever owns it.

**Limits and open tuning.** Running counts only within 400 units (full stick is how keyboard players always move). The meter is per guard per Bond; there is no shared "the guards talk to each other" suspicion. The witness's radio and the security room do not need a line to a panel (he just radios). No emergency lighting (the bank's light is baked). An allowed guard off screen hops through a shut door without opening it (stock). Not yet: a disguise taken from a knocked-out staff member, choosing loud or quiet at the van (AC: later).

## 19. Intel board and hints (built, branch `intel`)

AC, 2026-10-26: "the game is really fun so far but I have absolutely no idea how to play it. I don't know where to get the right keys, how to crack the safe, etc etc. need much more info, maybe some sort of intel view (new button?) to read the plan ... a big pre-planned set of notes/schematics like you'd see in a heist, hilariously brought out mid heist lol."

### 19a. How to use it (players)

- **Tab** (keyboard) or **Back / Select** (controller) opens the crew's planning board. Press it again to put it away. The game does **not** pause: the bank keeps moving while you read, so find a quiet corner (or don't, and enjoy the guard's face).
- **Pages:** the mouse wheel, `,` and `.`, or the bumpers (LB / RB) flip between the five pages. While the board is up those keys flip pages instead of guns.
- **Floors** (on SCHEMATICS): the Up / Down arrows, or the gadget keys (`Q` / `[`, the gadget buttons on a pad).
- At the start of the heist the board comes up once by itself as the **briefing**; press the INTEL key to close it and get to work.
- `midas.ini [input] intel_hold=1` shows it only while the key is held. The key is rebindable (`[controls] intel=...`, a pad's `padN_intel`, or the Controls page: "Heist Intel").

The five pages:

1. **THE PLAN**: the job in six steps (get in, the keys, cameras or power, crack the vault, the gold, get out), the quiet way and the loud way side by side. The checkboxes tick themselves as the crew does each step; the three card boxes under step 2 fill in as the crew gets GREEN, BLUE and RED; the next step is framed in red ("NEXT"); the bottom line is the live status (phase, alarm, power, cameras, loot).
2. **SCHEMATICS**: the bank a floor at a time (B2, B1, G, F1, F2, F3), rooms tinted by stealth zone (blue public, amber staff only, red restricted). Markers: you (yellow, live) and your crew (cyan, live); the exits (van, dock); the vault and its gold, the safes, the crack spot; the breaker and the generator; the camera console and the hall's screens; every camera with its view cone (grey while the cameras are off); every locked door coloured by the card it needs (GREEN, BLUE, RED, GOLD for the safes, purple for the floor key, black for the time-locked vault doors, grey for padlocks); stairs (and the outline of a stairwell that arrives from another floor); the card holders' posts (pins: "GREEN? HEAD TELLER", "GREEN? LOBBY CAPTAIN" - the plan doesn't know which of the two has it - "BLUE: SECURITY CHIEF", "RED: MANAGER", "GOLD: VAULT CAPTAIN"); and a throbbing red ring on the next step's place (the front door, the next card's holder, the crack spot, the vault, the exits). Guards: their **posts** are on the map from the start (orange rings: the crew cased the place); a guard's **live** position appears (red dot, then fading with its age) only when a Bond watching a camera feed sees him. Knowledge is the reward for using the cameras.
3. **THE KEYS**: a card per key (GREEN, BLUE, RED, GOLD, the floor key, and a "no key" card for the time lock, the padlocks and the lockdown): what it opens (the rooms behind its doors, counted from the layout), who carries it and where they're posted, how to get it, and the last time a camera feed showed the carrier. A big red cross and "GOT IT: BOND n" once the crew has it.
4. **PERSONNEL**: dossiers: tellers, the branch manager, the lobby captain, the security chief, guards, alarm raisers, the security room, vault guards, squads and waves. What they do, what they carry, how they react.
5. **NOTES**: the rules in plain words, written from the live settings `S` so they stay true: your cover, cameras and the security room, the power cut, holding fire near civilians, the alarm and lockdown, the time lock, scoring, the screens, and "this board doesn't pause anything".

**Hints.** One line in your own HUD at the right moment: at a locked door without its card ("This door needs BLUE - the security chief carries it (INTEL)"), at the vault door ("Use the door decoder on the vault door (equip it, fire)"; "Cracker jammed: ..."), at the breaker, at the camera console (with or without BLUE), at the hall's screens, and the first time you walk into a restricted room. At most one every 8 s, the same one at most once a minute, never over a live warning (the cover's "HEY!", an alarm flash). `S.hints = false` turns them off; `S.briefing = false` skips the briefing; `S.intel = false` drops the board altogether.

### 19b. How it works

- **Content in Lua, a generic renderer in C.** `tools/heist_intel.lua` builds every page from the mode's live data (`S`, `H`, `PW`, `heist_bank_map()`'s tables, `LEVEL`'s rooms / doors / objects / cameras, the card carriers, the objective progress) and hands it to `port/src/port_intel.c` through `intel.*` (`port/luavm/port_luavm_intel.c`; modding/lua.md "The INTEL board"): pages of rects, frames, lines, triangles, text and markers, a static layer (the map, the notes: built once) and a live layer (ticks, crosses, cameras, seen guards: rebuilt every 0.5 s). The C side owns only the chrome (the cork frame, the blueprint paper and grid, the tabs, the floor list, the footer), the live crew markers, and the drawing: 2D fill rectangles and the stock GE fonts (Bank Gothic, Zurich for headings) in the console's 2D space, into two display lists of its own (`portAllocHigh`, alternating), so a text-heavy page never eats the frame's master list. No new art.
- **The map** comes from the layout: the floors are the room heights with at least two rooms (B2 -840, B1 -420, G 0, F1 480, F2 960, F3 1440); a landing between floors goes to the floor below; the street (and the canal) is drawn on G. Each floor is fitted to its rooms (G keeps the street in front of the bank). The board's x runs against the world's x (board = `(wx1 - x) s`, `(wz1 - z) s`), which is how the 3D view looks with +z up: checked in a screenshot from the hall facing south, where the screen wall's west end is on the left as it is on the map (`intel_view_south.png`).
- **Presentation only.** Nothing on the board is sim state. The content is built on the script's own clock whether anyone looks or not (never "when the board is open", which is one machine's input), and only reads the mode's state; the one thing that feeds back is the hint line (`flash_p`), from sim state alone. The view (open, page, floor) is per machine in C. The `intel` action is taken off the poll in `joy_port.c portJoyPortPollCommit` before the sim, the input recorder (`GE_INPUTREC`) and the netplay window ever see it, and while the board is open the page / floor keys are swallowed there too (so the wheel flips pages, not guns). `port_intel.c` is classed presentation for the rollback snapshot (`scripts/port/snap_embed.py`). The briefing never opens in an automated run (`GE_FRAMES` set).
- **Keys shared on purpose.** Tab was Classic Mode's key and Back is Objectives on the Pro pad layouts. The `intel` bind takes the same keys; in a mode with a board (`intel.enable`), a press of the shared key goes to the board and the other job gives way (`video.c` for the keyboard's Tab edge, `joy_port.c` for a pad's Objectives and Classic Mode). Everywhere else they work as before. A new-default clash with classic_toggle is allowed for this one bind (`video.c videoPortBindNewDefaults`), so an existing `midas.ini` with `classic_toggle=Tab` still gets INTEL on Tab.
- **Files.** `port/src/port_intel.c` (new), `port/luavm/port_luavm_intel.c` (new), `tools/heist_intel.lua` (new), and small hooks: `joy_port.c` (the action, the poll filter, `intel_hold`), `input_pc.c` (the pad row, Back by default), `video.c` (Tab), `port_console.c` (one render call), `port_luavm.c` (one register call), `port_watchpages.c` (the rebind row), `tools/gamecontrollerdefault.txt` (`intel=back`), `tools/mode_heist.lua` (an include, `S.hints` / `S.briefing`, two lines to create and tick it), `CMakeLists.txt` (the copy).

### 19c. Tests

`tests/heist/run_intel.sh` (inside the build container, Xvfb on :99):

| Case | Checks | What it shows |
| --- | --- | --- |
| content | 23 | `tests/heist/heist_intel_test.lua` on the real mode (bank v2, no posted guards so nothing interrupts the hints): the board builds; six floors B2..F3 (6 / 11 / 38 / 26 / 19 / 7 rooms), all 107 rooms on a floor; all 28 keyed doors and safes on a key card (GREEN 5, BLUE 15, RED 4, GOLD 2, floor key 2), none unlisted; all 27 cameras on the map; the static layer ~580 items, the live ~70; no briefing in an automated run; the plan ticks "get in" in the hall (and the next step becomes the keys) and "crack the vault" once it opens; nothing known before watching a feed, the hall cameras then put the head teller on THE KEYS; the BLUE-door hint names its card and the security chief; no second hint inside 8 s; the vault-door hint; one screenshot per page, B1's schematic and a handedness shot |
| det | 2 | the board is presentation only: `heist_determinism.lua` (the camera feeds, knock-outs, the alarm, waves, the crack with a jam, the vault open and sealed) twice with seed 7, once untouched and once with the board opened, flipped through every page and every floor, closed, reopened (`GE_INTEL_AUTO`) and toggled twice by the intel action itself (`GE_JOYPORT_AUTO=intel:...`): the per-tick state hashes are identical over 4366 ticks; the key toggled the board |
| nointel | 1 | the content builder and the hints change nothing either: the same scenario with `S.intel = false` (no board at all) against the untouched run: 4366 ticks hash-identical |

Screenshots (AC's machine, not in the repo): `E:\midas-temp\intel\shots\` `intel_1.png` .. `intel_5.png` (the five pages), `intel_2_b1.png` (the vault level), `intel_view_south.png` (the handedness check).

**Regression on this branch** (2026-10-26, the machine shared with four other streams, load average about 45 on 12 threads): win path 59, trapped 17, civilian fail 5, quiet 11, count 2; bindings 38, cctv 40, ko 26; heist determinism solo and co-op (twice and view-switched) hash-identical over 4365 / 4362 ticks, every step reached; netplay host / guest hash-identical over every tick both ran, scenario notes identical, every step reached, no DESYNC, but short of the runner's tick count under the load (1964 ticks at the default budget, 2799 at `GE_FRAMES=9000 NETTICKS=4100`; the same environmental limit as section 16e); cover 11 of 12 (`doors` fails as on the base branch); guards 35 of 37 twice (`witness went on alert` and `hold fire`: unseeded runs, and each of the two also fails on other streams' builds tonight; `heist_guards.lua` doesn't load the heist mode, and nothing in this branch touches the AI); levelgen bank `static` 14 of 14 (the walking parts not run: they don't load the mode); dam1 identical (453 lines); `tests/run_logic_tests.sh` all pass.

**Limits and next.** The board is drawn with fill rectangles (lines and cones as runs of them), so diagonal strings and big cones cost a few hundred rects; fine at this size. Text is the stock Bank Gothic (all capitals) and Zurich; no handwriting face without new art. Bond doesn't lower his gun while reading (that would be sim state). Pickpocketing is not on THE KEYS yet (the gadgets stream may add it). The mode's own control-room intel box (`H.seen`, section 10) still reports a carrier "seen" by a camera on another floor (`cctv.sees` tests the cone and the stan line in x/z only); the board's own feed scan checks the floor and doesn't have that problem.

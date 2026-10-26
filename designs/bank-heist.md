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

**Fixed (2026-10-26), root causes of open items (1) and (2) below.** (1) *An allowed guard on screen never opened a ruled door.* The stock door finder in `chrlvTravelTick` (`chraction.c`, every 10 ticks) skips a door whose `flags2` has bit `0x20000000`; the decomp names it `PROPFLAG_DOOR_OPENTOFRONT` there, but in `flags2` it is `PROPFLAG2_20000000`, "AI cannot operate", the very bit `doorRuleLock` (`port_heist.c`) puts on every ruled door so the move test stops refused guards. The finder found the BLUE leaves fine (within 60 units), then dropped them on that bit, for every guard. On v1 the guard was off screen, and the pad hop takes an allowed guard through a shut door, so the bug never showed (the barracks squad on v2 came out the same way, with its door never moving). Fix: `portHeistChrMayOpenDoor` now returns 2 for "the rule allows him" (1 = no rule, stock; a door locked to AI in the layout itself still returns 1), and the finder opens the door on 2 despite the bit. Both the security guard at the BLUE door and the barracks squad now open their doors (0.95). (2) *Hold fire* was a test bug. The civilian was placed on the pad nearest the line by x and z only; on v2 that pad is on the roof (F3), right over the hall, at y 1548 against the guard's 109, and the hold test rightly ignores a civilian on another floor. `heist_guards.lua` now picks a pad on the guard's floor (|dy| < 150): the guard held 21 shots and fired none.

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

## 18. Guard AI: hearing, awareness, barks (built, branch `ai`)

AC (2026-10-26): "enemy AI really needs to be greatly improved. I know GE AI is pretty weak, you should be able to hear a person get slapped behind you even if you're not facing that way." Stock guards hear only Bond's gunfire, as a straight-line radius through any wall, and then run straight at him. Now the bank's guards hear the world, react in steps, search, give up, talk to each other on the radio, and the cameras see what Bond does.

### 18a. What a player notices

- **Slaps are loud.** Slapping or punching someone is heard about 7 m away in the same room (x the guard's hearing: 0.8 at Agent, 1.5 at 007), wherever the guard faces. A body hitting the floor (a knock-out or a kill) is nearly as loud. Through an open doorway a noise carries 60 %, through a shut door 25 %, through walls 10 %: **shut the door before you knock someone out.**
- **Footsteps.** Full stick is a run, heard about 3 m off (2.6 m at Agent); walking (Left Shift or half stick) is heard only right behind a guard; crouched and slow is silent. Sneak up walking, not running.
- **Doors and glass.** A door you open is heard (4 m); breaking glass carries 9 m. Gunshots use the gun's own noise (a silenced PP7: 1 m on the first shot, 5 m sustained; a KF7 20 m), now muffled by doors and walls on this layer (the stock straight-line check still runs too).
- **What a guard does.** He stops and turns to the noise ("Huh?"), walks over (runs if it was a slap, a thud, a shot or glass: "Hey! What was that?"), looks round, checks a pad or two nearby, then gives up ("Must be nothing.") and walks back to his post and his facing. If he sees you on the way, his own stock list takes over: the cover decides as before (a staffer is fine, an armed Bond is not).
- **The cover.** A noise near a staffer makes the guards who heard it suspicious of him (their meter fills), it does not blow the cover. A slap or a thud the guard can pin on you (you are in his line of sight, any facing, within 6.5 m, when he turns) blows it at once. Arriving at the noise point and finding a staffer there adds suspicion too.
- **Radio check-ins.** A guard who goes down misses his check-in (35 s later at Agent, 15 s at 007) and the nearest free colleague is sent to his post ("Post N isn't answering. Going to look."). Hide the body, or be gone.
- **Bodies.** A guard who finds a body shouts and radios it ("Man down!"): everyone goes on alert (the stock `body_found`, as before) and stays alert (they search harder and do not calm down).
- **Backup.** A guard who goes into combat calls backup ("Contact! Need backup!"): the nearest free guards (1 at Agent, 3 at 007, within 16 m) run to him, each to a different side (spread, not a bunch).
- **Patrols.** Some posted guards walk loops (the hall, the office corridor, the rear corridor, the security corridor, the vault corridor, the west gallery). A layout can name its own routes: pads `patrol_<route>_<n>`.
- **Cameras see what you do.** A camera that sees a slap, a robbery, a search of a body or a shot reports it to the security room at once; one that sees a knocked-out or dead body lying in its view reports that too. The security room raises the alarm `security_delay` (6 s) later, as for any camera report, and nothing happens with the security staff down or the power out.

### 18b. How it works

**Hearing** (`port/src/port_noise.c`, `port/include/port_noise.h`). `portNoiseEmit(x, y, z, radius, kind, srcChr)` (Lua `noise.emit`): every guard the mode tracks hears it when his distance (height counted x 1.5) is under `radius x his hearing scale x the room factor`. The room factor is the best path through the BG portal table from the noise's room to his, up to 4 portals deep: x `hearing_portal` (0.6) per open portal, x `hearing_door_factor` (0.25) per portal whose door is shut (the door record's open position under 0.3), never below `hearing_wall` (0.1). Then the `noise` event fires (`x, y, z, radius, kind, src`; `src` is the chr, `-2 - p` for Bond p, -1 the world). Built-in sources: a slap or a punch (`portHeistSlap`), a body falling (a knock-out or a death, from the C event bus), Bond's steps and runs (his sim speed every 4 ticks: over 120 units/s a step, over 380 a run, one every 0.5 s), a door a Bond within 2.5 m starts opening (guards' own doors are not news), a gunshot (the gun's noise x 100, at most one per 10 ticks per Bond), shattering glass (`glass.c`). Gadgets call `noise.emit` (a coin, a forced door: `noise.tune{door_forced = 1100}` is the radius the forced door should use).

**Awareness** (`port/src/port_ai.c`, Lua `ai.*`, `portAiAwareness`). Per tracked guard: a state (0 unaware, 1 suspicious, 2 searching, 3 alert, 4 combat), a level 0..1 and the last noise point. A noise adds `loudness x weight[kind]` to the level (weights: slap 1.2, thud 1.2, shot 1.5, glass 0.9, coin 0.8, run 0.6, door 0.5, step 0.45, alarm 2); it drains in `decay` (25 s; to 0.6 when alert). Over `susp_level` (0.15): suspicious, he stops and turns to it (a compass turn), "Huh?". After `turn` (1.4 s at Agent .. 0.8 s at 007): over `search_level` (0.3) he walks to the noise point, else he looks round where he stands. A slap or a thud louder than 0.15, a shot or glass: straight to searching, the shorter `alert_turn`, running. Searching: walk to the point, look round (`look`, 2.5 s, the stock look-around), walk to `search_pads` (2) pads within `search_radius` (450) of it, look again; then "Must be nothing", back to his post pad and his facing (and his patrol), unaware again (or still alert). The whole search is capped at `search_time`. A louder noise takes over the search point. **His own AI list keeps running all along**: the stock simple guard's detection loop moves nobody, so the layer steers him only while `chr->ailist` is the list he was spawned with; once it jumps (he saw Bond through the cover's rules, heard a stock gunshot, saw someone hit or killed) the layer lets go: combat while he has had Bond in sight in the last 3 s (and 10 s after), alert after. A knock-out waker or a scripted walk (`spawn.go`) is never steered.

**The cover** (`portHeistCoverNoise`). A guard who hears a noise other than steps, runs, shots (the stock path does those) and the alarm adds `cover_noise x loudness x weight` to his meter on every staffer Bond within `near` (500) of the noise point. A slap or a thud he can pin on a Bond (the source, or a Bond within 150 of the point, in his stan line within `attribute` 650, any facing) blows that Bond's cover with him at once (`heard`). Reaching the noise point with a staffer in sight within 350 adds 0.35.

**Barks** (`guard_bark(chr, kind, text)`; kinds `huh`, `hey`, `investigate`, `lost`, `found_body`, `radio`, `backup`, `surrender`). Fired from the sim at the moment of the state change (at most one per guard per 0.5 s, except hey, found_body and backup). The sound is presentation only (`port_gamesfx.c`, panned and faded from where the guard stands relative to this machine's Bond, silent past 26 m). GoldenEye's guards have no "What was that?" lines, so a bark plays stock ROM sounds by id: huh a throat-clear (`COUGH2_SFX`), hey a grunt (`GET_HIT_MALE5_SFX`), investigate a radio squelch (`RADIO_SFX`), lost a cough, found_body a shout and the radio message (`RADIO_MESSAGE_SFX`, the Caverns radio), radio the squelch and the message, backup two squelches and the message. `ai.bark(chr, kind, text)` lets a script bark.

**Teamwork.** *Check-ins:* a tracked guard who has been down (knocked out or dead) for `checkin` seconds, not yet found and with no alarm, misses his check-in: the nearest free guard (up, his own list idle, not in combat, not searching) is sent walking to the downed guard's post, where the stock body scan finds the body if it is still there. *Bodies:* `body_found` (port_heist.c, unchanged) now also makes the finder bark and puts every tracked guard on alert. *Backup:* the first time a guard goes into combat he calls; up to `backup` free guards within `backup_range` (1600) run to points 220 units round him, 120 degrees apart. Not done: cover-seeking in combat (the stock lists' cover and flank commands are not wired in).

**The cameras** (the cover's gap: a slap only counts as an act for 1.5 s and a camera needs its full alarm time of unbroken sight, so a slap in front of a camera went unreported, and only guards found bodies). Every 15 ticks, with the cover on and no alarm: a camera that sees a Bond whose act window is open reports it once per act (`cctv_spotted(cam, p)`), and a camera that sees a dead or knocked-out chr reports it once per body (`cctv_spotted(cam, -1)`). The sight test is the stock camera's: its 45-degree cone about its facing and sweep, its range, not while it moves, a stan line, and nothing while disabled (shot, the console's switch-off, the power cut). The mode routes both through the security room like any spot: `security_delay` later the alarm, unless the security staff are all down or the power is out.

**Patrols and the watchclock.** `ai.patrol(chr, {pads})`: while unaware and not searching he walks the pads in a loop (`chrGoToPad`, the stock route and its doors). `tools/heist_ai.lua` takes the routes from, first that has any: the map's guard tours (`heist_bank_map()` `M.PATROLS`, route -> pad ids in walking order: bank v3's 8 tours), the layout's `patrol_<route>_<n>` pads, else six named loops on bank v2 (the hall, the office corridor, the rear corridor, the security corridor, the vault corridor, the west gallery). Each route goes to the posted guard nearest its first pad. A guard back from a search walks to his post, then picks his loop up again. **Watchclock stations** (`M.STATIONS`, bank v3): a patrol guard punches each station of his tour as he passes it (within 150 units); one who is down `station_late` (45 s) after his last punch, or still up but has punched none for three times that (held up somewhere, or a long tour), has missed his round and the nearest free guard is sent to the station he should have punched next ("The <route> round missed a station. Checking."; `ai.nearest_free`, `ai.send`). Bank v2 has no stations; the test gives a tour one.

**State.** Every guard's state, level, step, timers, noise point and target, the route table, the Bonds' step timers and last positions, the camera report flags, the door states and the hearing counters are in the heist state hash (`portAiHash`, from `portHeistStateHash`); `port_noise.c` and `port_ai.c` are SIM files for the snapshot (`scripts/port/snap_embed.py`). No RNG draw: the search pads are picked from the chr number and the look count.

### 18c. Settings and API

In `S` (or `HEIST_SETTINGS`): `ai` (true), `patrols` (true), `patrols_named` (true), `station_late` (45), `hearing_slap` 700, `hearing_thud` 650, `hearing_step` 60, `hearing_run` 330, `hearing_door` 420, `hearing_glass` 900, `hearing_coin` 450, `hearing_alarm` 3500, `hearing_portal` 0.6, `hearing_door_factor` 0.25, `hearing_wall` 0.1, `ai_tune` (any `ai.tune` name), `ai_difficulty` (per difficulty `ai.tune` values; the defaults in `tools/heist_ai.lua`: Agent turn 1.4 s, search 25 s, check-in 35 s, backup 1; 007 turn 0.8 s, search 40 s, check-in 15 s, backup 3). The guards' own hearing scale (`S.hearing` x the difficulty's `hearing`: 0.8 at Agent .. 1.5 at 007) multiplies every radius.

Lua: `noise.emit(x, y, z, radius, kind [, src])` -> the number of guards that heard it; `noise.radius(kind)`, `noise.tune{...}`, `noise.factor(room_a, room_b)`, `noise.last()`; `ai.enable(on)`, `ai.on()`, `ai.guard(chr [, kind])`, `ai.patrol(chr, pads)`, `ai.awareness(chr)` -> state, level, lastx, lastz; `ai.state_name(s)`, `ai.info(chr)` (state, name, level, sub, x, z, post_x, post_z, looks, down, heard_ago, face), `ai.tune{...}`, `ai.bark(chr, kind, text)`, `ai.counts()`, `ai.nearest_free(x, y, z [, range [, exclude]])`, `ai.send(chr, x, y, z [, run [, radio text]])`. Events: `noise(x, y, z, radius, kind, src)`, `guard_bark(chr, kind, text)`. C: `portNoiseEmit`, `portAiAwareness` (`port/include/port_noise.h`; other streams call them through weak declarations).

### 18d. Tests, screenshots, limits

`tests/heist/run_ai.sh` (`tests/heist/heist_ai.lua`, one scenario per run, the real mode on the bank, Agent, `GE_FIXEDTICK=1`; `GE_AI_SHOTS=1` adds screenshots), 56 checks, all passing:

| Run | Checks | What it shows |
| --- | --- | --- |
| hear | 8 | a test guard in the emptied hall facing north; a slap noise 300 behind him: heard (1 guard), he turns to it (face 180, the noise at 180), walks to it (114 from it, searching) and barks ("Hey! What was that?", "Over there! Checking it out.") |
| slap | 8 | the same with a real slap (the stock hit path, `ko.test_slap`) on a teller 300 behind him, Bond out in the street: the noise is where she stands, he turns and goes |
| door | 7 | a test guard in the security office 300 inside the shut BLUE door, his back to it: a slap 600 away in the hall is not heard, nor one 300 away right behind the door (factor 0.25); the door opened, 300 away: heard |
| giveup | 5 | searching, he looks round, gives up after 7.8 s ("Must be nothing."), unaware, back on his post (32 units) and his facing |
| camko | 5 | a knocked-out teller where the hall camera looks: reported in 0.2 s; the alarm 4.0 s later (`security_delay` 4 in the test) |
| camact | 6 | holstered at the counter in a camera's view: nothing; a slap there: reported at once (an act), then the alarm |
| camdown | 6 | the security staff knocked out first (cameras report their bodies too): the teller's body is reported, no alarm 6 s later |
| checkin | 5 | a records guard knocked out where nobody sees: 6 s later (test time) "Post N isn't answering. Going to look.", and the colleague walks to within 250 of his post |
| patrol | 6 | 6 tours handed out on v2, every tour guard walks his loop (614 to 3291 units from his start in 25 s); a tour given a watchclock station (v2 has none) and its guard knocked out: missed after `station_late`, a colleague sent |

Regression on the final build (2026-10-26, see the report for the table): win path 59, trapped 17, civilian fail 5, quiet 11, 007 count 2; cover 12 of 12 runs (the `gun` run's last check now accepts the alarm from a camera that saw a civilian killed in the guards' crossfire: before this, the camera did not report bodies, so only the witness's radio could raise it; the `ko` and `camdown` runs turn `mode.body_check` off, and with it now the cameras' body reports); cctv 40; guards 37; determinism solo and co-op hash-identical (the scenario now also notes the AI's counts after its knock-outs: 3 searches, 7 barks, 36 guards aware); `tests/run_logic_tests.sh` passes; dam1 identical (453 lines).

**On bank v3** (after the merge of `heist` d95e05a): the AI suite again, 9 of 9 runs, 56 checks (the door run finds the doorway's axis from the BLUE door's leaves and the security office's rect instead of v2's wall x = 1220; the camera-act run uses `cust_1`, which the hall camera covers on both banks). v3's 8 guard tours (`M.PATROLS`, 7 handed out at Agent) and its 10 watchclock stations are picked up from the map's tables. Regression on v3: win path 59, trapped 17, civilian fail 5, quiet 11, 007 count 2; determinism solo (4364 ticks) and co-op (twice and view-switched) hash-identical with every step reached; dam1 identical. The quiet run once failed on v2 before the doorway rule (a backup guard looking round in vault door A's doorway: the door stayed at 0.49); search pads and backup points now keep 160 units off any door. The arhud layer draws every bark as a speech bubble over the guard (`ai_turn_v3.png`: "Over there! Checking it out.").

Screenshots (AC's machine, not in the repo): `E:\midas-temp\ai\shots\ai_turn.png` (v2: a hall guard turned to a slap behind him, running to it), `ai_search.png` (v2: the same guard looking round at the spot), `ai_turn_v3.png` and `ai_search_v3.png` (v3, with the arhud bubbles).

**Limits.** Guards in combat do not take cover or flank (the stock attack lists run as before; only the backup is spread). Hearing does not pass through floors except by the 3D distance and the wall floor (no stair or void propagation beyond the portal walk). A guard does not tell a colleague's slap from Bond's (no guard slaps). The stock gunshot hearing still runs alongside (straight line, through walls). The barks use stock grunts and the radio, not words. The search pads are any waypoint pads near the noise, so in an open hall he may check a spot behind a pillar and miss one in front. Bank v2 has no watchclock stations (v3 has 10).

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

## 20. Augmented interface and diagnostics (built, branch `arhud`)

AC (2026-10-26): "much more diagnostics/debugging, some for testing and some actually for realsies like an augmented interface"; "3d space
translated to 2d overlay, indicators above enemies who are suspicious, speech (with a sound) near their faces, a glow or outline if
they've got a certain key". Two layers, both drawn by `port/src/port_arhud.c` after the frame in the console's 2D space (480 lines by the
window's width). World points go through the local Bond's camera: the world-to-view matrix (`field_10CC`) times the chrs' projection
(`projmatrixf`), the product `port_crowd.c` uses, then the player's viewport (`c_screenleft/top/width/height`) and the wide or
pillarboxed frame. Points behind the camera are clipped at the near side (`w > 1`); a marker off screen becomes an arrow on the edge.

**Presentation only.** The overlay reads sim state and writes only its own statics (marks, bubbles, waypoints, rings, labels, toggles),
which the sim never reads; the file is classed PRES in `scripts/port/snap_embed.py` (never rewound). Its Lua calls are ignored while a
rollback re-simulates a tick. `tools/heist_ar.lua` only reads the heist's state (no `math.random`, nothing in `mode.state`). Proof:
`tests/heist/run_ar.sh proof` runs the scripted determinism heist with the AR wiring not even attached (`GE_AR_OFF=1`) and with the
specs, the diagnostics and the radar all on (`GE_DIAG=1`): the per-tick state hashes must be identical. The one exception is the test
switch `diag freeze` below (solo only, off by default).

### 20.1 The Q-Branch specs (the AR layer)

On by default in the heist (`S.ar = true`); the specs are part of the crew's kit, so there is no inventory item to fiddle with. Toggle with
**Shift+F3** or the console (`ar`, `ar off`). Layers (`ar <layer> on|off`, `S.ar_layers = { radar = true }`, `ar.layer(name, bool)`):

| Layer | What it draws |
|---|---|
| `suspicion` | over each cover guard: a pixel "?" that fills from the bottom as his meter toward you rises (yellow, orange past half), a blinking red "!" once he has seen through you ("HEY!") or everyone is on alert. With the ai stream's `portAiAwareness` linked, its states count too (suspicious / searching fill the "?", alert / combat are "!"). Faded with distance (full to 12 m, gone at 32 m); hidden when the game did not draw the chr this frame (`PROPFLAG_ONSCREEN`: the room portals and the frustum, no per-pixel test) |
| `speech` | speech bubbles near the face, with a short stock ROM sound (the radio squelch for radio calls, the camera beep for "HEY!", a quiet beep otherwise), panned from where he stands. One bubble per chr; it pops in and fades out |
| `keys` | a chr carrying a key (its child props that are key objects): bracket corners in the key's colour round its projected bounds, a faint pulsing tint (the glow), a key icon and GREEN / BLUE / RED / GOLD. `ar.outline(chr, rgba)` does the same for any chr |
| `points` | waypoint diamonds with distances in metres: VAULT (GOLD once it opens), BREAKER (while the power is on, quiet, casing), VAN and DOCK (bright once you carry loot or it went loud) |
| `doors` | a keyed, shut door within 18 m: bracket corners and OPEN in its colour when your keys open it; a dim key icon in its colour when they do not |
| `cameras` | a camera icon on each live camera within 30 m, red with a blinking REC while it is timing you, and its heading as a faint floor cone (only in the room you are in: there is no occlusion test). Nothing with the power out |
| `radar` | off by default: a 25 m top-down disc at the bottom right, your heading up: guards (white, yellow suspicious, red alert), staff (blue), key holders (their key's colour), knocked out (grey), waypoints at the rim |

The heist's own moments become bubbles over the guard who says them (`tools/heist_ar.lua`): "HEY!" (`cover_blown`), "Control!
Intruder!" (`cover_report`, the witness radioing in), "Camera n: we've got someone" (the security room's relay on `cctv_spotted`), "Man
down!" / "Body!" (`body_found`), "ALARM!" (a raiser when the siren starts), "Huh?" (a guard's meter passing half way), "Don't shoot!" /
"Take it!" (civilians), "Ugh... my head" (`chr_woken`). The ai stream's `guard_bark(chr, kind, text)` and `noise(x, y, z, radius, kind)`
events are drawn when they exist (hooked through `pcall`, so the heist works without them).

### 20.2 The diagnostics layer (F3)

**F3** or the console `diag` (`diag on|off`; `GE_DIAG=1` turns it on at the heist's start). Over every chr the game drew within 22 m:
its chr number, action (`stand`, `gopos`, ...), AI list (`G`lobal / `L`ocal id + offset), the ai stream's awareness state and level,
its cover suspicion, alertness and distance; within 16 m its sight cone as a floor fan (cover.tune's range and cone: the edges at full
range, the arc at 7 m), its hearing ring (hearingscale x 500: about a silenced shot), and a line to its go-to target. Noise events are
expanding rings (one second). Doors within 18 m show `D<idx> k<keyflags>` and their guard rule from `S.door_access` / the layout's tags
(green any, red never, amber a role list). Cameras show their timer toward you and their alarm time. The panel (left, under the HUD):
tick, the co-op state hash (read every half second), FPS and frame ms, chrs up / placed, alarm / power / cover, the room you stand in
with its name, area and cover class, your position, then the heist's phase, loot, guards, civilians, blown / reports. The radar is on
while the layer is.

**`diag freeze`** (console only) stops every AI list (chrs finish their current action and stand). It changes the sim, so it is refused
in co-op and netplay and is off at every stage start (`ar.reset`). Use it to look at a scene, walk round a guard, or line up a
screenshot.

### 20.3 The Lua API (`port/luavm/port_luavm_ar.c`; any mode may call it, guarded with `if ar then`)

| Call | |
|---|---|
| `ar.on(bool)`, `ar.enabled()` | the specs (off until a mode turns them on) |
| `ar.layer(name [, bool]) -> bool` | the layers above; an unknown name is an error |
| `ar.mark(chr, { label =, rgba =, bracket = })` or `ar.mark(chr, "text")` | a tag under the chr, optional brackets |
| `ar.say(chr, text [, secs [, kind]])` | a bubble; kind `"hey"` (red), `"radio"` (cyan), `"huh"` (yellow) or any `guard_bark` kind |
| `ar.outline(chr, rgba or nil)` | brackets and glow in that colour (nil clears) |
| `ar.clear([chr])` | the chr's mark, outline and bubble (no chr: all) |
| `ar.point(x, y, z, label [, rgba]) -> id`, `ar.unpoint([label])` | a waypoint (the same label moves it) |
| `ar.noise(x, y, z, radius [, kind])` | an expanding ring |
| `ar.config{ range =, cone = }`, `ar.sound(bool)`, `ar.reset()` | the diagnostics' cone; bubble sounds; forget everything |
| `diag.on()`, `diag.off()`, `diag.enabled()` | the diagnostics layer |
| `diag.door(idx, label [, rgba])`, `diag.room(id, label)`, `diag.text(n, text)` | labels for doors, rooms, and panel lines 1..4 |
| `diag.freeze(bool) -> ok` | the AI freeze (refused in co-op / netplay) |

Colours are `0xRRGGBBAA` or a name (`red green blue gold white cyan yellow orange`).

### 20.4 Tests

`tests/heist/run_ar.sh` (container, Xvfb `:99`): `api` runs `tests/heist/heist_arhud.lua` (13 checks: the API, the heist wiring, a
carrier with a key, a guard's meter filling with a drawn gun and his "HEY"); `GE_AR_SHOTS=1` takes the four showcase shots (a key
holder's brackets, the diagnostics over the hall, the "?" filling, the "!" and the "HEY!" bubble). `proof` is the presentation proof
above.

**Results on the arhud build (2026-10-26):** `run_ar.sh` api 13/13, proof 4365 ticks hash-identical (AR off vs AR + diagnostics +
radar, and the log shows the layer drew); heist determinism with `GE_DIAG=1` (the diagnostics on in every run): solo 3/3, co-op 5/5,
netplay (run alone, `GE_FRAMES=8000`) host / guest hash-identical over all 3179 ticks both ran, scenario notes identical, every step
reached, no DESYNC, but short of the runner's tick count (the known loopback-under-llvmpipe shortfall of section 16: 11 of 12 by the
script's count, no difference anywhere); bindings 38, win path 59, trapped 17, civilian fail 5, quiet 11, count 2, cctv 40, ko 26, cover 11 of 12 runs (`doors`
fails as expected), dam1 identical (453 lines), logic tests pass, levelgen bank `static` 14 (the walking parts not run: they do not
load the heist). **guards 33 of 37, not from this branch:** the guards test sets no `GE_SEED`, so each run is a different game; with
`GE_SEED=7` the develop baseline (aa33cf0) and this branch both score 33 (body found, bodies.found, the witness's alert, hold fire) and
their state hashes are identical for all 5864 ticks; an unseeded develop run scored 36.

**Limits.** No true silhouette outline: the renderer has no cheap stencil pass, so key holders get bracket corners and a tint over their
projected box. Occlusion is the game's own chr visibility (rooms and frustum), so a guard behind a pillar in a room you can see still
shows his "?". The head marker sits over the standing height (a crouching guard's floats a little). The camera cone is the camera's
heading, not its exact sight test. The diagnostics' text overlaps when many chrs stand close together.

## 21. Gadgets and spycraft (built, branch `gadgets`)

AC (2026-10-26): "new concepts, new cool ideas, gadgets, even a piton to scale a wall lmao and get to the top, spycraft shit. seriously, go hard and make this as fun as possible." Earlier: "disguise switch/obtain would be great later".

### 21a. What the crew carries, and the keys

Every Bond starts with the gadgets in his inventory, next to the PP7 and the door decoder. **Cycle to one with next / previous weapon (or inventory)**: each shows its own name (the stock items they borrow are renamed in a mode); the HUD's third line says what it does and what it is pointing at. **Fire** uses the gadget in hand; **use** does the hands-free tricks. Holding a gadget is not "armed": the cover does not mind it and staff do not surrender to it.

| Gadget | In hand (the stock item it borrows) | How | What it does |
| --- | --- | --- | --- |
| **Piton gun** | the piton gun (36, its own model) | stand at a grapple point's foot (a green bracket marks the hook), face it, **fire** | the line carries you up over about 2 s (a dotted line to the hook) and sets you down on top; at the top, **fire** again to abseil down. Four lines on the bank: the street up the facade to the **roof**; the hall floor to the **west gallery**; the alley up to the **archive's window** (F1, a new window); the canal up to the **embankment** top. Climbing in a cover guard's view is an act he sees through; a working camera in the room you climb from or to that has you for half a second reports you to the security room. |
| **Lockpicks** | the gas keyring (54) | at a locked keyed door, **hold fire** | the bar fills: GREEN 6 s, the floor key 8 s, BLUE 10 s, RED 14 s (others 12). Let go and the progress stays (per door). **Tap fire** to rush it: each tap is 0.6 s of work but rattles (a `"door"` noise: a guard nearby comes to look). The door then stays unlocked for everyone. Not the vault doors, not a `never` door (safes, the cells, the padlocked shutter and grate), not a padlock. Picking in a cover guard's view is an act. |
| **Stethoscope** (the lockpicks at a safe) | the gas keyring (54) | at a safe (the director's on F3, the manager's): **fire** to listen, then **fire** when the sweeping needle is in the green window | three tumblers; each click moves the window; a press outside it rattles the door (a small `"door"` noise). The third click opens the safe: the manager's spills its bars as with the GOLD card; the director's holds two gold bars and two cash bundles (its `director_loot` pads). Listening at a safe in a guard's view is an act. |
| **Coins** (4) | the credit card (53) | **fire** | a coin flies (further the higher you look) and lands with a clink: a `"coin"` noise. With the AI layer's `noise.emit` (the ai stream) it goes there; without it the two nearest cover guards within 900 units walk over (`spawn.go`), look for 12 s and walk back to where they stood. |
| **Tape loop** (2) | the data thief (55) | aim at a camera within 14 m, **fire** | that camera sees nothing for 60 s (its feed's label says `[LOOP]`), then it is back. |
| **Sleep gas** (1) | the bomb defuser (39) | **fire** | the canister lands like a coin; 3 s later everyone (guards and staff) within 330 units is knocked out. Bodies are bodies: a guard who finds one raises the alert. |
| **Thermal imager** | the spy camera (40) | **fire** | 10 s of every chr within 35 m outlined through the walls (red: armed, white: unarmed); then 30 s to recharge. Presentation only: it changes nothing the guards do. |
| **Pickpocket** | anything | behind a card carrier (more than 100 degrees off where he faces), not on to you (not alert, his meter under half, not a witness), within 120 units: **use** | his keys are yours ("Lifted: the BLUE card"). Facing you, or on to you: he catches your hand: a guard sees through you at once (`cover.blow`). |
| **Disguise** | anything | at a knocked-out guard or staffer: **use** once (the stock search), **use** again and stay with him 4 s unseen | you wear his uniform. A **guard's**: the restricted and staff rooms count as public for you (the security wing, the vault level), but a guard you linger next to looks twice as fast ("close"); loot, a drawn gun or an act still give you away. A **staffer's**: the staff rooms are yours. Interrupted if you move off or a cover guard sees you at it (that is an act). One uniform per body. |
| **Vents** | anything | at a vent grille (pads `vent_<id>_a` / `_b`): **use** | you crawl through (3 s, held still) and come out at the other end. Two on the bank: the east staff corridor into the **security office**, the staff lounge into the **lockers** (next to the RED stairs). |

The loadout is the mode's `S.gadgets` (nil = all of the above); a table overrides any part, e.g. `HEIST_SETTINGS = { gadgets = { coins = 0, gas = 0 } }` or `{ piton = false }`, and every timer and radius in `tools/heist_gadgets.lua`'s `DEFAULT` (`grapple_reach`, `pick_time`, `pick_hurry`, `coin_noise`, `loop_time`, `gas_radius`, `disguise_time`, ...).

### 21b. How it works

- **The trigger** (`port/src/port_gadgets.c`, a hook in `bondview2.c` before `MoveBond`, every Bond's own tick): with one of the script's gadget items (`gadget.items`) in the right hand, the fire button (Z) is taken there: counted (press edges, the held level) for the script and never passed on, so the stock gun code never sees it (the piton gun's stock code fires a grenade-launcher round; the gadgets' stock use animations never run). It reads the Bond's synchronized buttons: the same on every machine.
- **The line** (`gadget.reel`): Bond is held (no stick, no buttons) and placed on a path of up to four points (his feet: up the wall at his own spot, over the edge at the top, onto the pad) at a constant speed, every tick before his move (so his camera is there), on the start pad's floor tile and the end pad's for the last leg; at the end he is set down on the end pad (`portTeleportBondToPad`). The vents use the same with a path that does not move.
- **The lift** (`gadget.lift_key`): the chr's `PROPDEF_KEY` children (not his gun or hat) are dropped (`propobjSetDropped`) and picked up by that Bond at once (`propPickupByPlayer`): the stock key pickup, the `item_pickup` event, per-player keys.
- **The uniform** (`gadget.disguise`): `port_heist.c`'s cover scan and its camera test read the room class through `portGadgetsDisguiseClass` (a guard's uniform: every room public; a staffer's: staff rooms public) and scale the "close" fill by `portGadgetsDisguiseCloseScale` (2 in a guard's uniform).
- **The overlay** (`portGadgetsOverlay`, from `portConRender`; presentation only, this machine's Bond): the dotted line from his hand to the hook, the green bracket on a hook in reach, the work bar (the lock, the uniform).
- **The rules** (`tools/heist_gadgets.lua`, included at the end of `tools/mode_heist.lua`, its own handlers after the heist's): the grapple points and vents come from the layout's pad names; the noises go through `noise.emit` when the AI layer defines it (`rawget(_G, "noise")`), else the cover guards are walked over and back; a camera's report uses the heist's own security-room path (`HEIST.report`).
- **State.** The C side (each Bond's trigger counts, the line, the disguise, the gadget item list) is in the heist state hash (`portGadgetsHash`, folded in `portHeistStateHash`) and the rollback snapshot (`port_gadgets.c` is SIM in `scripts/port/snap_embed.py`); the script's (counts, the locks' progress, tasks, coins and gas in flight, loops, pulled guards, the stats) is appended to the heist's `mode.state` text every tick (hashed; in the desync dump) and lives in the Lua arena (snapshotted).

### 21c. Level pads

The gadgets find their points by pad name on any layout: `grapple_<id>_low` (where you stand) / `grapple_<id>_high` (where the line takes you), `vent_<id>_a` / `_b`. **Bank v3** (section 22) places the lines `facade`, `lantern_w`, `lantern_e`, `gallery`, `liftshaft` (the lift shaft from the rear corridor's landing down to the vault corridor: abseil down, climb back up) and the vents `records`, `lounge`, `server`, `vault`, `sewer`, `plant` (`heist_bank_map()` also lists them as `M.GRAPPLES` / `M.VENTS`). The v2 lines this stream first added (street -> roof, hall -> west gallery, alley -> an archive window, canal -> embankment; two vents) went with v2 at the merge. **The vault's ventilator** (`vent_vault`) can be crawled only while the power is out (its fans stop); with the power on it refuses: "The vault ventilator fans are running - cut the power first". The power cut is a second way into the vault (controller's ruling, 2026-10-26).

### 21d. Tests and limits

`tests/heist/run_gadgets.sh` (`tests/heist/heist_gadgets.lua` as the mode `heist_gadgets_test`; one run per gadget, `GE_FIXEDTICK=1`; `GE_GADGETS_SHOTS=1` for screenshots). Results on **bank v3** after the merge of heist d95e05a (all also passed on v2 before it):

| Run | Checks | What it shows |
| --- | --- | --- |
| piton | 17 | the real fire button fires the facade line (no grenade): up to its high pad (0 units off), back down; unseen, no cover blown; the gallery line in a posted hall guard's view: an act, he sees through Bond (and the tellers' camera reports him); the lantern_e, lantern_w and liftshaft lines each to their high pads |
| lockpick | 13 | the BLUE back door of the security office picked by holding fire in 10.0 s, it opens, no noise; the GREEN maintenance door rushed by taps in well under its 6 s, with a door noise |
| pocket | 8 | the security chief's BLUE card lifted from behind, unnoticed; a posted guard facing Bond: no card, he sees through Bond |
| coin | 9 | a coin 5 m from a hall guard: he walks over, then back to his post; throwing is not suspicious |
| loop | 6 | the camera in the loop's aim; looped: an armed Bond in its view for 5 s, not seen; when the loop (8 s in the test) runs out it spots him |
| disguise | 9 | a knocked-out hall guard searched (first use), changed into (second use, 4 s); in the security wing in a guard's sight for 5 s: no suspicion; lingering next to him: "close" |
| vent | 9 | v3's records, lounge, plant, server and sewer vents each crawled to the far pad; the vault ventilator refuses with the power on |
| gas | 4 | the canister thrown 4 m from a hall guard: 3 s later he is out cold |
| thermal | 6 | on for 10 s, then recharging; a press while recharging does nothing |
| safe | 6 | the director's safe: listening starts the dial; a press outside the window is a miss and rattles; three presses in the window open it (its loot appears) |
| det | 4 | `run_gadgets.sh det`: a scripted run through the piton, coins, lockpicks and a vent, solo twice and in local co-op twice (the second view-switched: the netplay condition): 3841 / 3856 ticks hash-identical, the scenario's notes identical |

Regression on v3: win path 59, trapped 17, civilian fail 5, quiet 11, 007 count 2; heist determinism solo 3/3 and co-op 5/5 (hash-identical, every step reached). On v2 before the merge also: bindings 38, cctv 40, ko 26, guards 36/37 (hold fire, the known open one), cover 11 of 12 runs (doors, known), dam1 IDENTICAL (453), logic tests all passed, levelgen bank static 14 with the window counted, guards 4; netplay host / guest hash-identical with no DESYNC but short of the tick count under CPU contention (2339 ticks; run it alone).

**Limits.** The line is a straight path (up the wall at your spot, over the edge, onto the pad), not physics; you pass through a parapet or a rail for a frame. The uniform changes the cover's rules, not Bond's model (a partner in co-op still sees a banker). Pickpocket targets the bank's card carriers and any guard or staffer the heist placed; it needs no animation. The coin's fallback (no `noise.emit`) walks the nearest cover guards over with `spawn.go`: they do not search, they look and go back. The tape loop sets the camera's alarm time out of reach (its feed still shows live video with `[LOOP]` on its label): the intel box still reads it. The thermal imager draws every chr's box, whatever is between. Without the ai stream there is no hearing for coins beyond the walk-over. The test runs find the lines and vents by name; they name the bank's `sec_back` and `maint_door` doors, the `director_safe` and a few hall pads (v2 and v3 both have them).

## 22. Bank v3 (stream `bank3`, 2026-10-26)

AC asked for a deep dive into real bank designs and a v3 built on informed views. The research is in [`bank-v3-research.md`](bank-v3-research.md), with 42 sources: halls, vaults, the day gate, the time lock, dual-key boxes, mantraps, guard tours, staffing, and seven real heists. The design is in [`bank-v3.md`](bank-v3.md): the room programme, the security layers, the staffing roster, guard tours, the vault, procedures as gameplay, heist approaches and the interfaces.

**The level** comes from `tools/levelgen/build_janus_bank_v3.py`, which writes `tools/levelgen/janus_bank_v3.json`. It is now the compiled bank on this branch (`port/levelgen/`). v2's builder and JSON stay. It has 119 rooms, 186 portals and 31 cameras, and keeps every name the mode and its tests use.

The new spaces:
- the hall's 17.6 m lantern, seen from three roof terraces;
- the officers' platform;
- the safe-deposit department, down its own stair from the west aisle, with coupon booths and a box vault whose door needs GREEN **and** the floor key;
- the round door's vestibule and a barred day gate;
- the vault's inspection ring;
- the goods-lift shaft;
- the sally port;
- the staff entrance mantrap.

**How to play it:** launch the heist as before (`Play Heist.bat`, `GE_STAGE=levelgen`). New ways in:
- **The safe-deposit stair** (GREEN grille), then the box vault (GREEN plus the floor key from records).
- **The lift shaft**, from the rear corridor's landing down to the vault corridor, which skips the RED stairs. It needs the gadgets stream's grapple.
- **The ring** behind RED `ring_gate`.
- **The dig** from the deep archive (the canal tunnel), which needs the gadgets stream's vents.
- **The roof**, then a rappel into the hall (grapple).

**Interfaces:** the pads `grapple_<id>_low/_high` (`lantern_w`, `lantern_e`, `gallery`, `facade`, `liftshaft`) and `vent_<id>_a/_b` (`records`, `lounge`, `server`, `vault`, `sewer`, `plant`). There are 36 guard-tour pads, `patrol_<route>_<n>`, on 8 routes, with 10 watchclock stations. `heist_bank_map()` returns them as `M.GRAPPLES`, `M.VENTS`, `M.PATROLS` and `M.STATIONS`. `gen_level.py` passes a layout's `script` key through as `LEVEL.script`, and `heist_bank_map.lua` takes v3's area and camera tables from `LEVEL.script.heist`.

**Tests (2026-10-26, on v3, headless, two containers):**

| Check | v2 | v3 |
| --- | --- | --- |
| `tests/levelgen/run_bank.sh` static / route A / route B / guards | 14 / 2 / 3 / 4 | **16 / 2 / 3 / 5** |
| `tests/levelgen/test_gen_level.py` | 11 | **12** |
| win path / trapped / civilian fail / quiet / 007 count | 59 / 17 / 5 / 11 / 2 | **59 / 17 / 5 / 11 / 2** (007: 104 guards, 89 of 90 posted, 12 raisers) |
| bindings / cctv / ko | 38 / 40 / 26 | **38 / 40 / 26** |
| guards | ≥36 of 37 | 36 of 37 (the witness check) |
| cover | 11 of 12 runs | 11 of 12 runs; `doors` fails as on v2 (§17c item 1) |

The levelgen counts change because the test now covers more.
- **Static** adds two checks: `v3_interfaces`, which checks that every patrol, station, vent and grapple pad resolves, in the right room; and `roof_sees_hall_lantern`, which checks that the hall is drawn through the clerestory.
- **Route A** walks down the safe-deposit stair and through the booth, the box vault, the roof terraces, the day gate and the staff entrance.
- **Route B** walks the ring and the lift shaft.
- **Guards** adds a guard climbing the safe-deposit stair.

The guards suite failed once in 3 runs on `body_found` and once on the witness check. Both are the random rolls noted as flaky in §17c item 3.

Not run: the dam1 oracle, the logic tests, and determinism. This stream changes nothing in `src/` or `port/src/`. Determinism hashes the level's sim, so it is worth a run on v3.

**Screenshots** (`E:\midas-tempank3\shots\`): the roof terrace looking down through the clerestory into the hall, the hall looking up to the lantern, the officers' platform, the safe-deposit reception with the box vault's round door, the round door open onto the day gate, the inspection ring, looking down the lift shaft, and the sally port. The new mode `tests/levelgen/levelgen_bank_shots.lua` picks v3's list when the layout has the ring.

**Open:**
- Guards do not walk the tours yet. `M.PATROLS` and `M.STATIONS` are ready for the ai stream.
- The mantrap and sally-port interlocks are not enforced.
- `vent_vault` (the emergency ventilator) bypasses the crack. AC needs to decide whether the gadgets stream lets gold out through it.
- The time-lock clock and the drill-the-wall alternative (Hatton Garden) are designed (`bank-v3.md` §6, §7) but not built.

## 23. Overnight integration (2026-10-26)

The five overnight streams merged on `heist` (0ae10ad): guard AI (§18), intel board (§19), AR specs and diagnostics (§20), gadgets
(§21) and bank v3 (§22, the compiled level). Then the full suite ran on the merged build, headless in four containers. The netplay
case ran alone at the end. Playable Windows build: `build_win_next\Play Heist (overnight).bat` (`GE_STAGE=levelgen GE_MODE=heist`).

### 23a. Fixes (merge interactions)

- **Intel board, v3's two-card door** (990b81a). The board matched a door's keyflags to one card exactly. As a result v3's box vault
  door (`boxes_door`, GREEN + FLOOR KEY = 0x3) was on no card, and the content test failed (32 keyed, 31 listed). Now a keyed door goes
  on every card whose bit it has. The map draws it in its first card's colour, and the door hint names the card the Bond still lacks.
  The test counts each door once and checks every card bit, including the box vault on GREEN and the floor key.
- **The control room's intel box saw carriers through floors** (a88f2e6). `cctv.sees` tests the cone and the stan line in x/z only.
  As a result `intel_scan` reported a card carrier as seen by a camera on another floor. It now applies the board's feed rule: the
  chr's room must be on the camera's floor (the layout's levels). On v3, half the raw sightings in 5 s of sweeps were on another floor:
  405 on the same floor, 401 on another. The new cctv check compares the mode's rule with the board's rule. The gadgets' camera report
  was already filtered by room.
- **The coin and the AI layer** (b4ec18f). With ai merged, the gadgets hand the coin (and the lock and safe rattles) to `noise.emit`.
  But `make_noise` returned -1, so the thrower got no feedback ("Clink. N guards heard it" now). The coin test, written without the AI
  layer, waited for the gadgets' own pull, which no longer happens. The test now takes the AI branch: the coin goes through
  `noise.emit`, the hall guard ahead turns ("Huh?", the AR bubble), walks to where it fell (262 -> 129 units), searches, gives up
  ("Must be nothing.") and walks back to his post, and the cover holds. The sleep gas's knock-outs thud through the AI layer's
  `CHR_KNOCKED_OUT` hook as designed: guards near the cloud come to look.
- **The guards suite is seeded** (dcdf70d). `run_guards.sh` set no `GE_SEED`, so every run was a different game. The suite now runs on
  `GE_SEED` 7. The stock body check's roll repeats while the body is in sight, so the test waits up to 20 s for `body_found` instead of
  6 s. With seed 7 the body is found after 9.75 s, which the old window missed. The test also polls up to 8 s for the alerted witness
  to close on Bond, instead of reading his position once. Both checks still fail if the event never happens. Two seeded runs gave
  identical results.

### 23b. The merged test table

This table is from the merged build plus the fixes above. Up to 12 games ran at once on 12 threads. Everything is on v3.

| Check | Expected | Got |
| --- | --- | --- |
| dam1 (`tests/bot/dam1_check.sh`) | identical | identical (453 lines) |
| `tests/run_logic_tests.sh` | pass | pass |
| `test_gen_level.py` | 12 | 12 |
| `run_bank.sh` static / route A / route B / guards | 16 / 2 / 3 / 5 | 16 / 2 / 3 / 5 |
| winpath | 59 | 56 on the first run (the GREEN card dropped on the floor and was not picked up, so the two GREEN door checks failed with it), then 59 on a rerun. Not seeded (see 23c) |
| trapped / civ / quiet / count | 17 / 5 / 11 / 2 | 17 / 5 / 11 / 2 |
| bindings / ko | 38 / 26 | 38 / 26 |
| cctv | 40 | 41 (plus the floor-rule check) |
| guards | 37 | 37 (seeded). An unseeded run timed out in the 150-guard spawn with 36 passed |
| cover | 12 runs | 12 of 12 runs, 87 checks (`doors` passes now) |
| `run_ai.sh` | 56 | 56 (9 runs) |
| `run_intel.sh` | content 23 + det + nointel | content 25 (22 + 1 fail before 990b81a), det 4299 ticks identical, nointel 4142 ticks identical |
| `run_ar.sh` | api 13 + proof | api 13, proof 4366 ticks identical |
| `run_gadgets.sh` | all runs | piton 17, lockpick 13, pocket 8, coin 10, loop 6, disguise 9, vent 9, gas 4, thermal 6, safe 6 (88 checks; disguise and gas after 821d3d1) |
| `run_gadgets.sh det` | solo, co-op | solo 3863 and co-op 3859 ticks identical (4 of 4) |
| determinism solo / co-op | pass | 3 of 3 (4366 ticks) / 5 of 5 (4363 ticks, view-switched too) |
| determinism net (run alone, default budget) | pass | 4 of 4, clean: host and guest 3802 ticks identical (NEED is 3000), notes identical, no DESYNC, every step reached. It was short of ticks all night only because of the load |

Two more gadget tests had to change for the AI layer (tests only, 821d3d1). In **disguise**, searching the knocked-out guard is an act, and the
AI layer's hall cameras report acts. The alarm followed and brought the security guards out of their office (2 failures), so the
scenario now turns the cameras off. In **gas**, the hall guard it aimed at was a patrol guard who had walked off, so the scenario now
picks a fixed post. The check is now that every guard standing where the gas lands is knocked out: where it lands depends on the line
of the throw.

Screenshots of the merged build on v3 are on AC's machine, not in the repo, in `E:\midas-temp\final\shots\`:
- `piton_facade_top.png`: the rope up the facade line;
- `thermal_through_wall.png`: guards boxed through the wall;
- `lockpick_progress.png`: picking the BLUE lock at 45%;
- `intel_schematics_G.png` and `intel_schematics_B1.png`: the SCHEMATICS page;
- `coin_guard_heard.png`: a guard's "Huh?" bubble at a coin, with "Clink. 1 guard heard it";
- `coin_guard_investigates.png`: the same guard as he walks over.

### 23c. Remaining gaps

- **The win path is seeded now** (`run_winpath.sh`, `GE_SEED` default 7, 2026-10-26 polish). Seed 7 passes 59 with the card at Bond's
  feet ("0 apart"). The earlier miss (the GREEN card dropped and not picked up within 4 s, after the lobby captain had answered a backup
  call) was not reproduced, so its cause is not known. Suspect the test's `warp_xz` (nearest pad in the body's room plus an offset)
  before the game: the stock pickup took every card it was put on. A sweep over other seeds was cut for time (`GE_SEED=n RUNS=win`).
- **The co-op bot is stale.** `tools/bot/heist_bot.lua` still targets the retired Bunker 2 heist (`GE_STAGE=bunker2`). modding/lua.md's
  heist paragraph (Bunker 2, room 6, `run_shots.sh`) carries a stale note now.
- ~~`cctv.sees` is x/z only~~ **Fixed (branch `polish-ai`).** `cctv.sees` (port_cctvview.c) and the AI layer's camera check for acts
  and bodies (port_ai.c `camSees`) are floor-aware now. The stan line walks from the camera's own floor, so it must end on the target's
  tile, or at least in his room. A chr straight above or below the cone is not seen. The Lua floor filters (the mode, the board, the
  gadgets) stay as a second check; they agree.
- ~~A coin's pull depends on the distance~~ **Fixed (`polish-ai`).** A coin is a lure now (`ai.tune{ lure = 0.25 }`). A guard who hears
  it at least 0.25 loud walks over to look. Loudness is 1 at the coin and 0 at the edge of 900 x his hearing x the rooms and doors
  between: about 6.7 m in the open, 4 m through an open doorway, 1.7 m through a shut door. Fainter, he only turns ("Huh?") and looks
  from where he stands. Test: `run_ai.sh` `lure`.
- **Fixed (`polish-ai`): backup calls pulled key-card carriers off their posts.** A carrier (ai kind 3) never answers a backup call;
  the nearest other free guards do. He still calls backup himself when he is the one in combat. Test: `run_ai.sh` `backup`
  (`ai.backup(chr)` calls it from a script). **Since 23f** the missed check-in and the watchclock (`ai.nearest_free`) skip carriers
  too. Test: `run_ai.sh` `holdcard`.
- **Fixed (`polish-ai`): the camera rule is legible.** When a Bond is within reach of an unsearched knocked-out body or a standing card
  carrier (about to search or pickpocket), and a live camera sees him, the AR specs put a red **CAMERA SEES YOU** diamond on that
  camera ("CAMERA SEES P2" in co-op). A live camera is on, not on a tape loop, with someone in the security room. The diamond is
  presentation only, on the points layer, so Shift+F3 hides it. Test: `run_ai.sh` `camwarn`. Body dragging has no mechanic yet, so it
  has no warning.
- **The Windows exe was not smoke-run on Windows**, because the port has no hidden-window option. The same source booted the heist
  headless in the Linux build. `build_win_next`'s Lua scripts were refreshed after the fixes (they are copied only on a relink).

### 23d. The first-play guide (2026-10-26 polish)

AC: "the game is really fun so far but I have absolutely no idea how to play it". The board answers that if you open it. These changes
tell a new player the next step without the board. All of it is in `tools/heist_guide.lua`, with three small hooks in `mode_heist.lua`
(`refresh_hud`, the stage start, an include). It is presentation only: HUD text, nothing in `mode.state`, no `math.random`. With the AR
specs off it still shows, because it is the mode's own HUD lines. `S.guide = false` brings back the old lines.

- **HUD line 1** ends in `TAB: INTEL` when it fits.
- **HUD line 2, the objective, always names the next concrete step while casing.** In order:
  - "Next: walk in the bank's front door, gun holstered" (until you are inside);
  - "Next: GREEN card - head teller or lobby captain (hall)";
  - "BLUE card - the security chief (<his room now>)";
  - "RED card - the branch manager (<room>)";
  - "the vault - down the RED stairs, a floor down".

  The cracking, open, trapped and escape phases keep the mode's own lines.
- **HUD line 3, when nothing else uses it.** The cover's warnings, the board's hints and the gadget lines all win.
  - **Key strip.** After the opening line (8 s), four tips for 8 s each: TAB / BACK for the board, next / prev weapon for the gadgets,
    Shift+F3 for the AR specs, and what USE does.
  - **How-to line.** After the strip, and for 10 s whenever the step changes: how to do the step, e.g. "Teller: aim a gun at her.
    Captain: slap him twice, USE to search".
  - **Prompts.** When you are near something you can use: a knocked-out body that still has a card ("press USE to search"), a card on
    the floor, a vent, a piton point.
- The opening line is short enough to fit the 63-character HUD line now. Before, it was cut to "...no restricted areas. S".
- **Test:** `tests/heist/run_firstplay.sh`, the real mode with seed 7 and a few shots: 6 checks (line 1, the front-door step, the
  opening line fits, the strip's TAB tip, the GREEN step inside, the search prompt by a knocked-out carrier). The test turns off guards
  waking bodies. With waking on, the teller knocked out in the busy hall was up again 5.5 s later, probably woken by a hall guard (not
  confirmed). So a player in the hall has to search fast.
- **Screenshots** (AC's machine): `polish-before-*.jpg` and `polish-after-*.jpg` in the overnight `img` folder. Before, line 2 said only
  "GREEN card: head teller or lobby captain", with no hint of how to get it or what Tab does.

### 23e. The player bot plays the bank (2026-10-26, branch `bot-bank`)

The heist driver for the player bot, `tools/bot/heist_bot.lua`, targeted the retired Bunker 2 heist. It is rewritten for the bank:
`GE_MODE=heist_bot GE_STAGE=levelgen GE_PLAYERBOT=1 GE_PLAYERBOT_HOLSTER=1`. Bond 0 is the bot. It moves on the stan tiles (its own
A*), opens doors with B, and every act is a button press; no warps. The script reads the mode's state (`HEIST`, `HEIST.M`, `LEVEL`),
which is how it knows where the card carriers are, and pushes plan steps (modding/bot.md). The mode is unchanged.

**The route.** Holstered and at peace while the banker cover holds (`holster 1`, `peace 1`). GREEN, BLUE, RED in turn: a guard
carrier's pocket is picked from behind (85 units behind him, face him level, B with nothing else in reach; the bot presses only when he
is the nearest standing chr, Bond is 110 or nearer and at least 110 degrees off his facing). The head teller, and any carrier after 6
misses or 120 s or once the run is hot, goes to the bot's own `key` step (staff held at gunpoint until they hand it over; a guard knocked
out from behind, or shot, then searched). Then the security console, the crack spot (face vault door A, press B), holding the zone with
`peace 0` (the vault level is restricted), restarting jams, the bars nearest first until 25 s before the time lock, the nearer exit. A
blown cover or the alarm turns the fighting on at once. A watchdog drops a step that runs 30 s (a goto onto a spot the tiles cannot
reach).

**Bot changes (port_playerbot.c, input only).** A `holster 1|0` plan step: the slappers in hand while not fighting, reached with the port's
previous-weapon action (A from the PP7 never got there in the heist). With `GE_PLAYERBOT_HOLSTER=1` set the stuck recovery's blind B
press is skipped. The robbery's line test counts the same room as a line, as the heist's own aim test
does: the tellers stand behind the raised counter and the bot walked into it for good.

**The test.** `tests/heist/run_botheist.sh [seed ...]` (PAR, MAXSECS, EXTRA, SHOTS) runs headless with the bot's fast-forward
(`GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_CLOCKSCALE=20`, one tick in 60 drawn). `GE_FRAMES` is out of reach: with `GE_NOSLEEP` it counts
pumps, which is why the heist suites ran out of frames; the run ends on the mission's end or the bot's `MAXSECS`. It prints a PASS or
FAIL per checkpoint (`ready inside green blue console red vault_level cracking vault_open gold out win`; `out` is Bond in an exit zone)
and the reach rate over the seeds.

**Results (Agent, default settings, `MAXSECS=600`, seeds 7 8 11 12):** ready 4/4, inside 4/4, GREEN 4/4, BLUE 4/4, console 3/4, RED
4/4, vault level 3/4, cracking 2/4, vault open 2/4, gold 2/4 (all 30 bars both times), out 1/4, **win 1/4** (seed 7: 386 s, 56 kills,
one civilian). Every run went loud in the first 20 s; the end is mostly the bot's fighting, not the route. Earlier builds of the same
driver: seeds 9 and 10 died in the hall at ~50 s; with `GE_HEIST_TUNE1` (carriers only) seeds 8 and 11 died because the bot never drew
the gun (below), so that tuning is not measured with the final build.

**Findings (not fixed):**
- **A use press with nothing in reach is a pickpocket on the nearest chr within 120.** A press at a door just out of reach, next to a
  guard who faces you, is "caught" and blows the cover. The bot did exactly that (its stuck recovery's blind B press). A player pressing
  use near a guard for any other reason risks the same. Worth a design look (e.g. only from behind, or only with the pickpocket aimed).
- **Drawing the gun from the opening slappers.** `advance_through_inventory` goes to the hand's *previous* weapon when the cycle's next
  item is a gadget. With the slappers equipped from the start and never anything else, A did not reach the PP7 for the bot (25 s of
  presses). The bot now draws once on the street before holstering, which keeps the previous weapon the PP7. Not checked with a human on
  a pad; if it reproduces, the mode's start loadout (equip the PP7, then the slappers) is the place, but that attempt alone did not fix
  the bot, so the cause is not fully known.
- **The pickpocket press often does nothing.** Behind a carrier, facing him, B (3 ticks) frequently fired no `use` event at all, so no
  pickpocket ran (seed 8: 6 tries, no event). Cause not found (the B tap path in bondview2.c `btap` / `field_D0`). One clean lift seen
  (seed 8, an earlier build: the lobby captain's GREEN at 8 s, no cover lost).
- **The crowded hall.** Once loud in the hall, the bot often cannot shoot: a civilian is in every line of fire, so it holds and is shot
  (seeds 8 / 9 / 10 / 11 deaths at 50-120 s in earlier builds). The teller's robbery and the captain's knock-out are both seen by the
  hall guards, so the GREEN card goes loud on most seeds.
- A crew with no loot stands in the exit for ever (the mode needs something carried); the bot leaves at 25 s left even with 0 bars.

**Left:** the quiet route (pickpocket presses that fire; taking GREEN without the hall seeing it; the tape loop or console before the
crack), fighting in the crowded hall, the exit under the waves (2 of 2 looted runs died or won on the way out), a sweep over more seeds,
and the record-and-replay check (`tests/bot/heist.sh` is still the Bunker 2 harness).

### 23f. Seed sweep (2026-10-26, branch `soak`)

Every suite had only ever run on `GE_SEED` 7. A sweep ran them across other seeds to find flakes and seed-dependent bugs.

**How:** `tests/heist/seed_sweep.sh` (new). Each (suite, run, seed) job is one game, fast-forwarded the bot harness's way:
`GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_CLOCKSCALE=20`, with an unbounded `GE_FRAMES`. The script kills the game once it prints `HEIST DONE`.
`GE_SIMONLY_TEST=2,59,60` renders one tick in 60 (port_simonly.c: the sim is the same). The win path takes 14 s instead of 219 s, and
seed 7 gives the same 59 checks either way. The suites' own `run_*.sh` scripts stay paced in real time, because `GE_FRAMES` counts
pumps, and those run out under `GE_NOSLEEP` before the scenario ends. `seed_sweep.sh all 1 2 3` runs the list 6 at a time, and
`seed_sweep.sh table` sums it up. `run_ai.sh`, `run_gadgets.sh`, `run_cover.sh` and `run_firstplay.sh` honour `GE_SEED` now (default 7).

**Before the fixes** (b389df2, seeds 1–12; a seed counts as clean only if every run of the suite passed on it):

| Suite | Clean seeds | Failing (seed: run) |
| --- | --- | --- |
| winpath (5 runs) | 10 / 12 | 5, 11: quiet |
| guards | 9 / 11 | 1, 3 |
| ai (12 runs) | 11 / 11 | none |
| gadgets (10 runs) | 7 / 11 | 5: pocket, disguise; 8, 10, 11: pocket |
| cover (12 runs) | 5 / 11 | 2, 10: ko; 5, 6, 9, 11: staff |
| firstplay | 7 / 11 | 3, 6, 9, 10 |

**Fixes.** Every failure reproduced on its own seed, because the sim is deterministic per seed. Two were game bugs:
- **A key-card carrier left his post for a missed check-in or a missed watchclock station** (afd2430; it was the known gap in 23c).
  `nearestFree`'s carrier rule now covers both dispatches as well as backup calls, and `ai.nearest_free` never names a carrier. The
  check-in also looked for the colleague at y 0, not on the downed guard's floor. Test: `run_ai.sh holdcard`, 12 checks. The old build
  fails 4 of them.
- **A guard or customer could stand on a carrier's pad** (cd59b06, 17891dc). The carriers' pads are random. The posts, raisers and
  staff placed after them cycled their area's pads, and spawn_at put one 110 units off the carrier. With seed 5, a posted guard stood
  next to the security chief, so the guard saw every pickpocket from behind the chief. This was gadgets `pocket` on 4 of 11 seeds. Now
  the chr whose pad a carrier took moves to the next pad in its list. Renumbering the whole list instead moved every post after it, and
  3 layout-bound checks failed on seeds 12–14, so the change is the narrow one.

The rest were test assumptions (9acd63a):
- **guards, witness.** The witness's stock reaction to a death is a roll: he runs at Bond, or he runs to the corpse and stands over it.
  Both now count as the alert. A witness who stays put still fails.
- **winpath quiet, vault door.** "Sealed" is the close order. A crack-squad guard in the doorway stops the door, and the time lock snaps
  it shut 1.5 s later. The check waited only 1 s; it now waits for the snap.
- **gadgets pocket.** The test guard's spot (`hall_mid`) can be taken, so he spawns a little off it.
- **gadgets disguise.** The guard heard something and walked off. Bond now follows him, as a player would.
- **cover staff.** The warp could land a floor away: it now takes pads on the guard's floor only, and tries nearer spots (all outside
  `close`).
- **cover ko.** Bond waits in the street, not in the office corridor where another guard saw him. The guards within earshot of the
  knock-out's thud are removed: with seed 5 the security chief heard it and caught Bond, a fair catch, but not the radio rule this check
  is about.
- **firstplay.** The cover's warning wins HUD line 3 by design, so the prompt check turns the cover off.

**After the fixes.**
- The final build (17891dc) passes every suite on seed 7. It also passes the runs that failed: pocket 5, 8, 10 and 11, disguise 5,
  staff 5, ko 5, quiet 11, guards 3, piton 12 and 13, and win 14.
- With cd59b06 (before 17891dc's narrowing), the failing suites passed on seeds 1–11: winpath 11 / 11, guards 11 / 11, gadgets pocket
  and disguise, cover ko and staff, firstplay 11 / 11, and ai checkin, patrol, holdcard and backup.
- The same build ran every suite on seeds 12–14. All were clean except the three layout-bound runs that 17891dc fixed.
- Seeds 15–30 were not run, for lack of time: `seed_sweep.sh all 15 16 ...` runs them.

**Bench** (`run_bench.sh` on v3, headless llvmpipe, with about 11 cores of other streams' games on the 12 threads). The sim column is
the longest game-code stretch per frame, including the display-list building and GL submission. Average / max, in ms:

| Phase | Agent (36 guards) | 007 (103 guards) |
| --- | --- | --- |
| hall | 63 / 249 (frame p95 151) | 92 / 988 (frame p95 205) |
| vault | 44 / 134 | 60 / 152 |
| hot | 81 / 221 | 108 / 447 |

All of these are over the 16.7 ms budget at 60 Hz. But this is software GL on a loaded machine, so the numbers measure the machine's
load, not the game, and there is no baseline from the same conditions. Nothing was optimised. The 007 hall's single 988 ms frame (sim)
is worth a look on a quiet machine with real GL: a spawn wave or a path plot that hitches. The pools have room: 141 of 460 chr slots,
610 of 2400 props, and 181 of 485 KB of display list at the peak.

**Determinism on the final build.**
- **dam1:** identical (453 lines).
- **Netplay** (`run_determinism.sh net`): run with none of this stream's games going, but with about 11 cores of other streams' load
  that this stream could not stop.
  - At the default budget, host and guest were hash-identical over 1731 ticks, with no DESYNC.
  - At `GE_FRAMES=12000 NETTICKS=4100`, they were hash-identical over 2794 ticks, the scenario notes were identical (24 lines), and there
    was no DESYNC.
  - Both runs were short of the 3500 ticks the check needs, so the late steps were not reached (the camera-view knock-out and the
    wave). This is the same environmental limit as in 16e and 23b, where a quiet machine got 3802 identical ticks.

- **Not reproduced: the GREEN card dropped and not picked up within 4 s** (23c). Every win-path seed above put Bond on the card ("0
  apart"), and every card was picked up. The likely cause: the card can fall outside the body's room (a carrier down in a doorway),
  while `take_card`'s `warp_xz` takes only pads in the body's room, so Bond lands away from the card. The one miss came right after the
  lobby captain had left his post on a backup call. Carriers never leave their posts for backup, check-ins or rounds now (above), and
  `take_card` falls back to the nearest pad by x, z when Bond lands more than 60 units from the card.


## 24. The LOUD route: combat and the heat (branch `combat`)

Before this, once a fight started a guard's stock attack list ran him straight at Bond and stood him there shooting; only the backup
call spread anyone out (§18d limits). Now the guards fight like a team, and an alarm keeps getting worse until the crew leaves.

### 24a. What a player notices

- **Cover.** A guard who is shot at (hit, or a near miss), or who has had you in sight for a moment in a fight (`react`: 2.2 s at
  Agent .. 0.9 s at 007), shouts "Taking fire! Get to cover!" (or "Into cover!") and runs to a spot near him (1.3 to 7 m) where you
  cannot see him crouched: behind a pillar, a door jamb, a counter or a desk. He kneels there.
- **Peek and shoot.** After a hide (1.8 to 3.2 s at Agent, 1 to 2 s at 007) he steps out a pace (1.5 m, across your line) and fires a
  burst, standing or kneeling, then ducks back. Shoot him while he is out, or rush him while he hides.
- **Flanked.** Walk round his cover so you can see him and he moves ("He's flanking us! Moving!") to new cover against where you are now,
  or shoots it out if there is nowhere to go. Two peeks with no sight of you and he finds cover against your new position.
- **Flanking pairs.** Two guards in the same fight (within 14 m of each other) pair up (each second, `flank_chance`: 0.15 at Agent ..
  0.45 at 007): one stays in cover with shorter hides and longer bursts ("Covering! Go!"), the other runs round you, about 80 degrees
  from his partner's side ("Cover me! I'm flanking!"), and fires from there. Watch your sides when one goes quiet.
- **Falling back.** A guard down to half his health (`retreat_hp`) falls back to cover further from you ("I'm hit! Falling back!") and
  hides longer.
- **Losing you.** No sight of you for 8 s: "Where'd he go?", and he goes back to his own list, which hunts your last known position.
- **The heat.** After the alarm: the bank's own reinforcements as before (`wave_first`, the hall); then **the police**, `police_at`
  seconds after it (45 s at Agent, 35, 30, 25 s at 007), in militia uniform (`Russian_Infantry`, KF7 or DD44), from the job's entrances
  in turn (the map's `P.police` pads if the job names them, else its front and dock wave pads: on the bank the street and the dock),
  `police_size` a wave every `police_interval` s (Agent 2 every 35 s, at most 6 alive .. 007 4 every 25 s, 12
  alive). The HUD flashes "POLICE at the street and the dock!" and the first officer in shouts the megaphone line over the radio
  ("POLICE! The building is surrounded - drop your weapons!"). Then **SWAT**: the mode's armoured response (`response_after`, 120 s)
  is announced ("SWAT is in the building"), and the vault-stair (RED) doors swing shut. A RED card still opens them, and the exits (the
  front doors, the dock door) never close or lock, so the escape is always possible: fight through, or leave another way. Every one of
  them fights with the combat layer.

The quiet route does not change: nothing here starts until a guard is in a fight (the cover still decides who sees you), and an alarm
raiser runs for his panel first.

### 24b. How it works

`port/src/port_combat.c` (Lua `combat.*`, `port/luavm/port_luavm_combat.c`) with three hooks in `port_heist.c` (the stage reset, the
AI tick, the state hash) and one each in `portHeistChrNoAi` and `portHeistChrWaking`. `tools/heist_combat.lua` sets it up and runs the
heat; `mode_heist.lua` has six one-line hooks (the include, `S.combat`, `note_guard`, the waves' and the response's `on_spawn`, the stage
start, the tick).

- **Who.** Every guard the mode places (`note_guard`) and every wave, police and SWAT chr is registered (`combat.guard(chr, kind)`).
  Every 4 ticks a registered guard who is up, armed, not surrendering and not on a scripted walk is checked: in a fight (the ai
  layer's combat state, the stock "saw Bond in the last 2 s", or an attack action) or under fire (a hit, the stock near-miss flag, a
  Bond's shot within 26 m that has the line to him). A hit is his `damage` going up; a shot is the hearing layer's last noise
  (`portNoiseLast`, polled each tick: only Bonds' shots are noises). No C event listener: the first version listened on
  `PORT_EV_NOISE` / `PORT_EV_DAMAGE` and the first-play guide's knock-out check then failed (a guard found the carrier; with the layer
  off it passed). A listener on `PORT_EV_NOISE` makes `port_noise.c` emit every noise, and each event type has 8 listener slots.
- **Taking him over.** While the layer drives a guard, `portHeistChrNoAi` says his AI list does not run (as for a knock-out waker), and
  `portHeistChrWaking` keeps the arena's direct route off him. Everything he does is a stock action: `plot_course_for_actor` (run),
  `check_if_able_to_then_kneel`, `actor_aim_at_actor` / `actor_kneel_aim_at_actor` with `TARGET_BOND` (fire), `chraiStopAnimation`.
  While his list is off the layer keeps his stock sight fields (`lastseetarget60`, `lastknowntargetpos`) true, so the ai layer still
  sees him in combat and his list picks up where Bond is when he is released.
- **Cover search.** Rings of 12 points at 1.3, 2.3, 3.4, 4.6, 5.8 and 7 m round him (each guard starts his ring at a different angle,
  from his chr number), nearest ring first. A point must be on his floor (a stand tile within 70 of his ground), not by a doorway
  (`portNoiseNearDoor`), not held by another guard (1.3 m), not much closer to Bond (no more than 2.5 m closer; a retreat: 1.5 m
  further), reachable straight (the stock side-step test, `chrlvCall7F0B0E24WithChrWidthHeight`: two lines a body apart through his
  height band, so low furniture refuses it), and hidden from Bond at a crouched head's height (90 above the floor; the stock stan line
  with objects, doors and path blockers). Its peek point is 1.5 m (or 0.9 m) to either side across Bond's line, a straight walk from
  the cover point, with Bond's line at standing height (140). The best point in the first ring that has any is the one nearest 8 m from
  Bond. At most 3 searches a scan, one a second per guard.
- **Pairing** (every second): two driven guards within `flank_range`, neither paired nor fallen back, with `flank_chance`; the healthier
  one flanks. His point: Bond's position, turned 80 degrees (and 60 / 100 / 120) either way from the partner's bearing, at the partner's
  distance (4.5 to 9 m, or 3/4 of it), on his floor, off doorways, at least 2.5 m from him and with Bond's line; he runs there on the
  stock route. After his burst the pair ends and he fights from cover like anyone.
- **Barks** go through `portAiBark` (the guard_bark event: the AR bubble and the ai layer's ROM grunt or radio sound by id: "hey" is
  `GET_HIT_MALE5_SFX`, "radio" `RADIO_SFX` + `RADIO_MESSAGE_SFX`, "backup" the double squelch); one line per guard per 3 s.
- **Determinism.** Every decision reads sim state; the random hides, bursts, kneels, pairings and flank sides draw `randomGetNext` (the
  sim RNG). All of it is in `portCombatHash` (folded into the heist state hash after `portAiHash`), and `port_combat.c` is a SIM file for
  the snapshot (`scripts/port/snap_embed.py`). The heat stage is kept in C (`combat.stage`) so it is hashed too; the police are spawned
  chrs (in the engine's own hash).

### 24c. Settings and API

`S.combat` (`false`: stock fighting, no police, no SWAT announcement). Keys (all optional): `tune` (any `combat.tune` name), `difficulty`
(per difficulty overrides of the table in `tools/heist_combat.lua`), `police` (true), `police_at`, `police_size`, `police_interval`,
`police_max`, `swat` (true), `shutters` (true), `megaphone` (true).

`combat.tune` names (defaults in C, then the difficulty's): `react` 1.5, `hide_min` 1.2, `hide_max` 2.4, `burst_min` 1.2, `burst_max` 2.2,
`cover_min` 120, `cover_max` 700, `peek_dist` 150, `retreat_hp` 0.5, `flank_chance` 0.35, `flank_range` 1400, `lose` 8, `cooldown` 3,
`kneel` 0.5, `engage` 2600, `cover` / `flank` / `retreat` (1 on, 0 off), `searches` 3.

| Difficulty | react | hide | burst | flank chance | retreat at | police at / size / every / alive |
| --- | --- | --- | --- | --- | --- | --- |
| Agent | 2.2 s | 1.8-3.2 s | 1.0-1.6 s | 0.15 | 45 % | 45 s / 2 / 35 s / 6 |
| Secret Agent | 1.6 | 1.5-2.8 | 1.1-1.9 | 0.25 | 50 % | 35 / 3 / 30 / 8 |
| 00 Agent | 1.2 | 1.2-2.4 | 1.2-2.2 | 0.35 | 50 % | 30 / 3 / 28 / 10 |
| 007 | 0.9 | 1.0-2.0 | 1.4-2.4 | 0.45 | 55 % | 25 / 4 / 25 / 12 |

Lua: `combat.enable(on)`, `combat.on()`, `combat.guard(chr [, kind])`, `combat.tune{...}`, `combat.info(chr)` -> {state, name (idle,
to_cover, hide, to_peek, shoot, flank), role (none, suppress, flank), cover_x, cover_z, peek_x, peek_z, pair, peeks, covers, flanks,
retreated}, `combat.counts()` -> {covers, peeks, flanks, retreats, moves, releases, barks, searches, fails}, `combat.los(chr [, p])` (Bond
p, default the nearest, has the layer's line to him), `combat.under_fire(chr)` (as a near miss), `combat.pair(flanker, partner)`,
`combat.stage([n])`. `HEIST_COMBAT.police` is the police controller (`heist.reinforce`: `spawned`, `waves`, `mine`).

### 24d. Tests

`tests/heist/run_combat.sh` (`tests/heist/heist_combat.lua`, the real mode on bank v3, Agent, seed 7, one scenario per run):

| Run | Checks | What it shows |
| --- | --- | --- |
| cover | 12 | a test guard on a hall pad 7.8 m from Bond, in his line: under fire he runs 4.8 m to a cover point (3 units from it) that breaks Bond's line, barks, peeks out and fires with the line, ducks back; Bond walks round to a spot that sees him in cover: he moves to new cover |
| flank | 8 | two guards in the hall: they pair, "Cover me! I'm flanking!" / "Covering! Go!"; the flanker runs 10.7 m round Bond (85 degrees from his partner's side) and fires from there while his partner holds his cover (moved 1 m) and fires |
| retreat | 5 | `retreat_hp` above his health: he falls back from 2.8 m to 5.8 m from Bond, "I'm hit! Falling back!", out of Bond's line |
| heat | 12 | the alarm; police on the timer (4 s in the test), 2 at once from the street and the dock, they take cover against Bond; the megaphone line; SWAT on the timer; the RED doors shut; the front doors and the dock door keep no card; the police keep coming (4); the crew gets out at the dock with the gold: "Heist complete" |

Regression on `combat` after the merge of `heist` 15db916 (headless, two containers, then one; Agent unless said):

| Check | Expected | Got |
| --- | --- | --- |
| `run_combat.sh` cover / flank / retreat / heat | new | 12 / 8 / 5 / 12, twice (before and after the listener fix) |
| determinism solo / co-op | pass | 3 of 3 (4358 ticks) / 5 of 5 (4364 ticks, view-switched too) after the fix; 3 / 5 before it too (3582 / 3289 ticks: the listeners changed the run, see 24b) |
| winpath win / trapped / fail / quiet / count | 59 / 17 / 5 / 11 / 2 | 59 / 17 / 5 / 11 / 2 |
| firstplay | 6 | 6 after the fix (5 + 1 fail with the listeners; 6 with `GE_COMBAT_OFF=1`) |
| guards (seed 7) | 37 | 37 (a first run under three-game load timed out in the 150-chr crowd with 36 passed, as noted in 23b) |
| cover | 12 runs | 12 of 12 (87 checks) |
| cctv / ko / bindings | 41 / 26 / 38 | 41 / 26 / 38 |
| ar | 13 + proof | 13, proof 4365 ticks identical (2 of 2) |
| gadgets | all runs | piton 17, lockpick 13, pocket 8, coin 10; the rest cut when the machine ran out of CPU |
| dam1 | identical | identical (453 lines) |
| ai | 56+ | not rerun to the end (cut for CPU) |

Cover, cctv, ko, bindings, guards, winpath and dam1 ran on the build with the event listeners (before the fix); the fix only removes
them.

### 24e. Limits and next

- The cover search samples points round him, not the level's geometry: in a big open room with nothing to hide behind he finds none
  and fights as before (his own list). Bank v3's generator could emit `cover_<n>` pads by pillars, counters and door jambs for a better
  pick (the layer would only need to add them to its candidates).
- `TARGET_BOND` is the stock "current player" target: in co-op every driven guard fires at the same Bond the stock lists would (the
  nearest Bond decides where he takes cover and where he flanks, not whom he shoots).
- No grenades, no suppressive blind fire, no stacking up at doors; SWAT have no shields. The police do not negotiate (no hostage talk).
- The megaphone is a radio bark and a HUD line: GoldenEye has no megaphone voice.
- No screenshots this stream (the time went into the tests).
## 26. The debrief (stream `debrief`, 2026-10-26)

AC's brief: an end-of-heist debrief that makes each run feel like a scored spy mission, a GoldenEye-style end screen with a heist flavour.

### 26a. What a player sees

When the crew gets out (or the job fails), a full-screen panel comes up in the INTEL board's style: the cork frame, the blueprint
paper, a manila case file and an index-card tab reading DEBRIEF. It shows:

- **The case file (left):** "HEIST COMPLETE - THE CREW GOT OUT" (or "HEIST FAILED - <why>"), the **rating** stamped in its colour, a
  line of flavour, the **CREW PAYOUT** (the mode's score: loot + quiet bonus - civilian fines; "a head" in co-op), how it adds up, and a
  red **NEW BEST** stamp (or BEST TIME) with the best on that difficulty.
- **The job in numbers (right):** result and exit, time, loot, the alarm (never / raised at m:ss), times spotted (cover guards who saw
  through a Bond + cameras that timed one), radio reports, bodies found by guards, cameras (looped, console, power cuts), knocked out
  (guards, staff), guards killed, civilians hurt / killed, gadgets used, locks picked, the **route** ("Front door > Lift shaft > Vault >
  Van"), and how each card was got (pickpocket / knock-out / intimidation / off a body / picked up).
- **Commendations (bottom):** 15 medals, earned ones lit gold.

| Rating | When |
| --- | --- |
| GHOST | out, no alarm, never spotted, no body found, nobody killed or hurt |
| PROFESSIONAL | out, no alarm, at most 2 guards killed, no civilian killed |
| COWBOY | out with the alarm up, no civilian killed, under 15 guards killed |
| CHAOS | out, but a civilian died or 15+ guards did |
| BUSTED | the job failed (every Bond down, or too many civilians dead) |

Commendations: **Never Seen** (no guard or camera made you, no alarm), **No Bodies** (nobody died), **Quiet Job** (no alarm),
**Pickpocket** (a card lifted), **Tumbler** (a safe opened by stethoscope), **Spider** (a piton line), **Rat Run** (a vent), **Locksmith**
(a lock picked), **Quick Change** (a uniform), **Lights Out** (the power cut), **Blind Spot** (a tape loop or the console), **Speedrun**
(out in under 6:00 on Agent, 7:00 Secret Agent, 8:00 00 Agent, 9:00 007), **Full Vault** (every vault bar), **Clean Hands** (no civilian
hurt), **Sandman** (five knock-outs, no kills).

**Dismiss:** USE, INTEL (Tab / Back), A or Start, after 0.75 s. The panel stays over whatever comes next (the stage's last frames, the
death cam, the stock mission screen) until then, and it takes the input while it is up, so the press that closes it does not also skip
the stock screen (the pads stay blank until every key is let go).

**Records:** the best per difficulty (payout, time, best rating, runs, wins, the last run) is kept in
`Data/Saves/heist_records.json` next to the exe (`GE_PROFILE` moves it with the saves). Delete the file to start the record book again.

**Log lines** (for tests and AC): `HEIST DEBRIEF {json}` (every number above, the route, the keys, the commendations; one line) and
`HEIST RECORD {json}` (difficulty, new_best, best_time, runs, wins, best_payout, saved, path).

`S.debrief = false` (or `HEIST_SETTINGS = { debrief = false }`) leaves it all out.

### 26b. How it works

- **The numbers** (`tools/heist_debrief.lua`, included at the end of `tools/mode_heist.lua`): handlers on the sim's own events
  (`chr_knocked_out`, `chr_died`, `damage`, `cover_blown`, `cctv_spotted`, `body_found`, `chr_searched`, `civilian_robbed`,
  `item_pickup` for cards) and a tick that reads the heist (`HEIST`), the gadgets' stats and reel ids (`GADGETS`), the Bonds' rooms and
  the console. It only reads: nothing goes into `mode.state`, nothing calls a sim API that changes anything, no `math.random`. A card's
  "how" is settled a tick after its pickup (a pickpocket's lift counts in the gadgets' stats after the stock pickup fires).
- **The end:** `finish()` calls `HEIST.debrief_end(win, msg, bonus, killed)` (one line, in a `pcall`) just before `mission.win/fail`:
  it rates the run, logs the line, fills the panel (`debrief.set{...}`), counts the run (`debrief.record{...}`) and opens it
  (`debrief.show()`).
- **The panel** (`port/src/port_debrief.c`, `port/luavm/port_luavm_debrief.c`): its own copy of the board's primitives (fill
  rectangles, triangles, the stock Bank Gothic and Zurich faces) into two display lists of its own, drawn from `portConRender` on every
  screen (not only in a mode). Hooks: `port_console.c` (one render call), `joy_port.c` (while the panel is up the poll's actions are
  cleared; use / INTEL / menu keys are its dismiss edge), `libultra_shim.c` (the pads read neutral; A / B / Start dismiss),
  `port_luavm.c` (one register call). PRES in `scripts/port/snap_embed.py`. In an automated run (`GE_FRAMES`) it draws but never takes
  the input (`GE_DEBRIEF_CAPTURE=1` forces it).
- **The records stay in C.** The file is this machine's, and the Lua arena is sim state (the rollback snapshot), so its contents never
  go back to the script: `debrief.record` takes the run, reads and writes the file, and fills the stamp and its line in C. The file name
  is fixed in C (nothing outside the game folder). A rollback that re-simulates the last tick runs the end again: it is counted once.

### 26c. Tests

`tests/heist/run_debrief.sh` (container, Xvfb `:99`; the records file is deleted first): **42 passed, 0 failed**.

| Case | Checks | What it shows |
| --- | --- | --- |
| quiet | 19 + 2 | `tests/heist/heist_debrief_test.lua` on the real mode: the security chief's BLUE card lifted from behind, the console, the crack, the bars, out before the time lock, the van. GHOST; no alarm, never spotted; Never Seen, No Bodies, Quiet Job, Clean Hands, Pickpocket, Blind Spot, Speedrun; BLUE "pickpocket"; payout = the mode's score with the quiet bonus; NEW BEST on the fresh file; the panel up. The `HEIST DEBRIEF` line parses (python `json`) with every field; the records file and the `HEIST RECORD` line count 1 run, new_best true. Screenshot |
| loud | 17 + 3 | the alarm up from the start, a civilian killed: CHAOS; the alarm counted (raised at 0:02); the death counted and fined; no quiet commendations; no NEW BEST (the quiet run paid more); the file counts 2 runs; the line parses. With `GE_DEBRIEF_CAPTURE=1` and INTEL presses every 300 polls: an INTEL press dismisses the panel ("ge: debrief: dismissed") |
| det | 1 | presentation only: `heist_determinism.lua` with the debrief in vs left out (`debrief = false`): 4365 ticks hash-identical |

**Regression on this branch** (2026-10-26, the machine shared with five other agents): heist determinism solo 3/3 (4366 ticks) and
co-op 5/5 (4363 / 4364 ticks, view-switched too); win path 59, trapped 17, civilian fail 5 (each now also prints its `HEIST DEBRIEF`
and `HEIST RECORD` lines: the win path rates COWBOY with every card "knock-out", the trapped run COWBOY out of the dock, the civilian
fail BUSTED), quiet 11, 007 count 2; firstplay 6; bindings 38; gadgets pocket 8, safe 6, piton 17; dam1 IDENTICAL (453 lines); `tests/run_logic_tests.sh`
all pass. Netplay determinism not run (the integration pass runs it alone).

Screenshot (AC's machine): `E:\midas-temp\debrief\shots\debrief_panel.png` (and `debrief-debrief_panel.jpg` in the overnight `img`
folder): the quiet run's GHOST panel.

### 26d. Limits and next

- **The route** is the way in (the first room off the street: "Front door" from the hall, else "Side way in (<area>)"), the lines and
  vents in order, the vault and the exit. A test that warps Bond shows "Side way in". It does not know the stairs or doors taken.
- **"Times spotted"** counts cover blows and cameras that timed a Bond (a camera that spots him after the alarm counts too). The AI
  layer's awareness states (suspicious, searching) are not counted.
- **Guards killed** are chrs a Bond killed that are not bank staff (posted guards, waves, squads). Kills by the sleep gas are knock-outs.
- **Lockpicked cards:** a picked lock is counted ("Locks picked") but is not a card got; THE KEYS rows say "-" for a card never held.
- **Dismissing in netplay** is per machine; the records are per machine (each machine its own file). A rollback that re-simulates the
  last tick counts the run once. A predicted end that a rollback then undoes would still have been counted (not seen in practice).
- **The input capture** was tested with INTEL presses in a headless run. The stock mission screen behind it was not checked by eye on
  Windows (no hidden-window option); AC should check that a press after dismissing reaches the stock screen as usual.
- Next: per-crew-member lines in co-op (who did what), a "case file" page on the INTEL board listing the best runs, more commendations
  from the AI layer (never searched for, a coin that pulled a guard off a camera's view).

## 30. Job 2: the casino, and the job table (stream `job2`, 2026-10-26)

The heist is no longer tied to the bank. A **job definition** (`tools/jobs/<job>.lua`, shipped as `job_<job>.lua`) holds everything the
mode, the map reader, the INTEL board and the first-play guide used to hard-code: the layout's names (doors, pads, rooms, areas,
cameras, civilian places, carriers), the card ladder (who carries which card, the body, health and post), and the job's HUD, guide and
board words. The layout picks its job with `LEVEL.script.heist.job` (default `bank`). The bank's values moved verbatim into
`tools/jobs/bank.lua`, so the bank behaves exactly as before.

The second job is **Le Grand Casino Janus**, a Monte-Carlo-style casino: the atrium, the Salle Renaissance and the Salle Europe;
the cage and its mantrap; the eye-in-the-sky catwalks behind one-way glass and the surveillance room; the Salons Privés with the
high-roller safe; and the house vault and count rooms below. The research, design and routes are in
[`job2-casino.md`](job2-casino.md). It is built by `tools/levelgen/build_casino.py` and played from its own build dir
(`-DGE_LEVELGEN_DIR=port/levelgen_casino`: the slot holds one layout per build). The test is `tests/heist/run_job2.sh`.

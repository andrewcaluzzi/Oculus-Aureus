# Guards and AI (chrs)

"Chr" is the engine's word for any character that is not a player: guards, scientists, Natalya, the fork's spawned
enemies and spectators. All of this is stock engine code unless marked fork.

## Lifecycle, slots and ids

- **Setup guards** are propdef type 9 (`GuardRecord`). `proplvreset2` -> `expand_09_characters`
  (`src/game/chraction.c` ~251-322) places each on its pad's stan tile, then `chrAllocate` (`chr.c`) takes a prop from
  `g_Props` and the first free `g_ChrSlots` entry **with no NULL check**. Never call `chrAllocate` directly.
- Fields the setup feeds that are not what their names say: the setup's **health** field sets **`hearingscale =
  health / 1000`** (`chraction.c` ~308), and its **`ReactionTime` field is `visionrange`** (~309). Dam's tower guards
  have health 1000 (hearing 1.0) and vision 100.
- Defaults for a chr made any other way (`chr.c` ~1740): `visionrange = 250`, `hearingscale = 1.0`, `maxdamage = 4 x
  007 health mod`.
- **Slots**: `g_NumChrSlots = setup guard count + 10` (`initguards.c`), so a stock stage has about 10 spare (Dam 10,
  Facility 11 measured). Fork reservations: Flatland 96, Colosseum 240..4000 (`GE_COLOSSEUM_SLOTS`, default raised to
  600 in the arena), modes named `heist*` or `GE_MODE_SLOTS` 250, levelgen 200 (`port/src/port_flatland.c`).
- **Anim slots**: a model loaded mid-level reuses a spare anim slot only if its record count fits; the spares hold
  **140 records**. A bare head 0 is a whole body (194 records) and comes back NULL: use head -1 (random).
- **Chrnums**: runtime spawns get 5000+ (`player1_guardID`), clones source + 10000. AI scripts pass chr numbers as a
  **u8**, so level scripts cannot address runtime chrs except through `CHR_PRESET` / `CHR_SELF`. Fork ranges: the co-op
  bot 4999, hostages 7200+, mode guards 7300+, arena and `spawn.chr` 8000+.
- A dead chr fades and is removed after about 1.5 s (`chrlvTickDead`); the arena can keep bodies (`spawn.keep_bodies`).
- Stage load (`lvlStageLoad`) wipes the STAGE pool: slots, models and body files. Your own static `ChrRecord *` must
  be cleared there too.

## The AI bytecode

- Commands are defined in `src/aicommands.def`, emitted by the macros in `src/bondaicommands.h` (and the generated
  `aicommands2.h`), interpreted by `ai()` in `src/game/chrai.c`.
- List ids: `<= 1024` global (`g_GlobalAILists`, `src/game/chraidata.c`, compiled in, so editable in any build);
  `0x401 + N` is a stage's `ai_N`; `>= 0x1000` background/object lists. `ailistFindById` checks the globals first, so a
  global list works for any chr on any stage. Adding one: write `u8 m_X[]` in `chraidata.c`, add it to the table
  before the terminator, add its `GAILIST_*` id in `bondaicommands.h`.
- **The setup macros store their arguments byte-swapped**: `objective_bitfield_set_on(0x00100000)` is stage flag 0x1000,
  `if_bond_in_room_with_pad(0x4a01, ...)` is pad 0x014a = 330, `guard_flags_set_on(0x40000000)` is chrflag 0x40.
  `tools/bot/objectives.py` (bot branch) converts them.
- `ai()` runs only when `chr->sleep` runs out; an idle standing chr re-arms it to 14-18 ticks, so it "thinks" about four
  times a second.
- Bond-centric by design: sight, hearing, `TRYRunToBond`, `IFMyDistanceToBond...` all use `getCurrentPlayerProp()`.
  In co-op the fork wraps `chrlvActionTick` and `chrlvTriggerFireWeapon` so the current player is that guard's nearest
  Bond (`port_cooplogic.c`).

## Sight

Two different tests, often confused:

| AI command | Function | What it checks |
| --- | --- | --- |
| `IFISeeBond` (first notice) | `chrCheckTargetInSight` (`chraction.c` ~4558) | Bond within **+-110 deg** of the chr's facing (a 220-deg cone) and within `visionrange x 100` units (or 200), inside the fog's far distance, then a random roll whose odds fall with distance and with the angle beyond 45 deg off the facing, scaled by the chr's speed rating; then the line test below |
| `IFICouldSeeBond` | `chrCanSeeBond` (~4202) | **Only** a stan line from the chr to Bond (`stanTestLineUnobstructed` over objects, doors, chrs, path blockers) that must **end on Bond's own tile**. No cone, no range, no roll |

Consequences:

- Stock "sniper" behaviour (Dam's tower guards shooting far down the map) is just `IFICouldSeeBond` + a fire command:
  no range limit anywhere; hit chance falls with range instead (below).
- A contact whose list waits for `IFISeeBond` (Statue's Valentin) never starts if Bond walks up behind it.
- **Tile-edge blindness**: a Bond standing exactly on a tile edge (a pad centre is on the diagonal of its two
  triangles) fails the "ray ends on Bond's tile" test and is invisible to every chr beyond a few units. Irrelevant for a
  walking Bond; test harnesses must offset him (`bond.warp(p, pad, dx, dz)`). `GE_LOSDBG=1` logs `TILE MISMATCH`.

## Hearing

`chrCheckGuardsHeardSound(noise)` (`chr.c` ~3998), called each tick a Bond fires (`bondview2.c` ~8265, `noise` = the
louder hand): every chr with **straight-line distance to that Bond < `hearingscale x noise x 100`** is alerted to his
position. Walls do not matter. Each shot adds the gun's `NoiseIncreasePerShot`, capped at `LoudnessMax`, decaying
toward `LoudnessMin` (`gunTickNoise`, `gunfire.c` ~5142). Unarmed adds nothing. `SET_HEARING_SCALE` retunes a chr.

Radii at hearing scale 1.0 (values read with `weapon.get` on the stock cart, Bunker 2 probe):

| Gun | At rest | After 1 shot | Sustained (cap) |
| --- | --- | --- | --- |
| PP7 | 100 | 300 | **1200** |
| PP7 silenced | 0 | 100 | **500** |
| KF7 / AK47, Uzi, MP5K, Spectre, M16, RC-P90, grenade launcher | 200 | 400 | **2000** |
| MP5K silenced, sniper rifle | 0 | 120 | 700 |
| Shotgun | 200 | 600 | 2500 |
| Rocket launcher | 200 | 1200 | 2500 |
| Knives | 0 | 100 | 100 |

Silenced is not invisible: a guard that sees another get shot or die still reacts (below).

## How a guard's shot hits Bond

A guard's bullet ray (`chrlvFireWeaponRelated`) does not test players at all. Hitting Bond is an accumulator,
`chrlvUpdateShotbondsum` (`chraction.c` ~6768):

1. **Aim cone.** The sum grows only while Bond is inside the guard's aim cone, measured from its body facing plus
   `aimsideback`. Half-angle by distance (`chrlvGetAimLimitAngle` ~6733):

   | Distance | < 200 | 200-400 | 400-800 | 800-1600 | > 1600 |
   | --- | --- | --- | --- | --- | --- |
   | Half-angle | 14.4 deg | 8.6 deg | 4.3 deg | 2.2 deg | 1.1 deg |

2. **Aim lag.** `aimsideback` eases toward the aim at Bond over `aimendcount` = **10 ticks** (set at ~6549/~6637,
   eased in `chr.c` `chrUpdateAimProperties` ~1924).
3. **Per fire event** (only while no damage flash is showing on Bond):
   `shotbondsum += 0.16 x cadence x min(1, 300 / d) x (1 + accuracy / 10) x 007 accuracy mod x g_AiAccuracyModifier`,
   x2 for a semi-automatic gun (automatic firing rate <= 0), x2 for the shotgun. **When the sum reaches 1.0, Bond is
   hit** and it resets. Damage: `0.125 x DestructionAmount x g_AiDamageModifier x 007 damage mod` (x3 shotgun).
4. **Cadence** is 3.0 in live play (`GUARD_AIM_CADENCE`, ~43, upstream PORT change: the N64 ran this at its ~20 fps
   tick; 1.0 at 60 Hz made guards three times less deadly). Demo playback uses the recorded cadence.

So at stock accuracy the chance per fire event is about `0.48 x min(1, 300/d)` (doubled for pistols): **range is
everything.** Inside 300 units a pistol guard hits almost every fire event; at 1200 about one in four (pistol) or one
in eight (automatic), once Bond is inside its cone. Bond's speed is
not in the formula. On paper a Bond crossing the guard's line outruns the 10-tick lag (lateral 8 u/tick x 10 ticks /
d is wider than the cone at every range), but the bot's Bunker 2 sweep did not show a gain: standing 4/12 wins, a 45-tick
dodge 4/12, a 120-tick dodge 1/12 (strafing also leaves cover and enters other guards' lines). The bot's working rule:
fewer guards with a line at once, at longer range, killed before they have aimed long.

Difficulty: Agent sets `g_AiHealthModifier = 2.0` (chrs take double damage from Bond), others 1.0 (`lv.c` ~1547-1627).

Chr-to-chr shots (`TARGET_CHR`) skip this accumulator and **hit every time**; damage to a chr applies only when the
shooter has `CHRFLAG_CAN_SHOOT_CHRS` (0x40) or the fork's faction layer allows it (which rolls its own 0.35 hit chance,
`port/src/port_faction.c`).

## Reacting to shots and deaths

- A chr that is shot always re-targets **Bond** at the end of its flinch (`chrlvTickArgh` -> `chrlvSetTargetToPlayer`).
  No attacker is recorded anywhere (`handles_shot_actors` has no attacker argument).
- `chrseeshot` / `chrseedie` hold the **victim's** chrnum and are cleared every tick; `IF_GUARD_SEE_ANOTHER_GUARD_SHOT`
  / `_DIE` read them. The alert sweep (`chrlvIterateGuardSeeShotDie`) alerts at most 4 chrs per call.
- Path cap: `chrGoToBond` / `chrGoToChr` / `chrGoToPad` refuse once 10 chrs have seen Bond recently
  (`g_SeenBondRecentlyGuardCount`); go-to-position chrs kneel instead. In a fork mode the cap is lifted
  (`PORT_SEEN_BOND_CAP`, `chraction.c` ~100), or a 20-guard fight froze every hostage.
- Fork: guards that find a body (dead or KO'd) fire `body_found`; a finder walks over and wakes a KO'd chr.

## Alarms

- `alarmActivate` (`propobj.c` ~16021) starts the global `alarm_timer`; the siren clears itself after
  `CHROBJ_GAS_TIMER` = 1800 ticks (30 s, US).
- **Alarm raisers** (`GAILIST_SIMPLE_GUARD_ALARM_RAISER`): on seeing or hearing Bond, run to `padpreset1`, play the
  alarm animation; the alarm fires at frame 60 (`chrlvTickStartAlarm` ~5939). Stock needs an alarm box there; the fork's
  `spawn.chr{alarm_pad=}` works without one.
- **Alarm boxes** can be shot, and Bond can toggle one with use (`propobj.c` ~10772).
- **CCTV cameras** (object code, so they keep working in a Lua mode): 45-deg half-cone about the camera's facing plus
  its live sweep, a stan line test, and a sight timer. The alarm fires at `CCTV_ALARM_FRAMES` (300, ~162) x
  `F_80030B14`, which is **2.0 at Agent (600 ticks = 10 s) and 1.0 above (300 ticks = 5 s)** (`lv.c` ~1547). Measured
  on Bunker 2 camera 95: the timer rose 60/s and the alarm rang at 600. The setup's range field reads -100 on all six
  Bunker 2 cameras: no range limit. Shooting the glass disables a camera (`PROPFLAG_CCTV_DISABLED`). Stock tests only
  the current player; the fork tests every living Bond with its own timer in co-op (`portHeistCctvAllBonds`).

## Surrender, threats, knock-outs

- Stock surrender: `IF_GUARD_IS_TARGETED_BY_BOND` (the crosshair over the chr's screen box) then `GUARD_SURRENDERS`
  (`chrTrySurrender`, throws down held items); `CHR_DROP_ALL_CONCEALED_ITEMS` drops hidden ones. Facility's scientists
  use this pattern.
- Fork civilians (heist): surrender when a Bond aims within a body-width cone (30 units + 4% of the distance) with a
  line of sight, hand over a concealed card after a sustained threat, cower on gunfire. Slappers, bare hands and gadgets
  do not count as armed. Each Bond has its own aim and threat timers.
- **No stock knock-out.** Fork (`heist*` modes): the slappers (`ITEM_FIST` through `handles_shot_actors`) count slaps
  per chr; the first is the stock reaction with no damage, the second knocks out: a soft face-down death animation held
  at its last frame, no AI list, no collision box, no autoaim. It is not a death (no `chr_died`, no counters); a gun
  still kills it. A use press next to it searches it (`chrDropItems`).
- Civilian deaths: `CHRFLAG_COUNT_DEATH_AS_CIVILIAN` and `IF_KILLED_CIVILIANS_GREATER_THAN`. Frigate's limits are
  Agent 4, Secret Agent 2, 00/007 1; Facility fails above 2.

## Factions

There are none in the stock game. Guards are only ever hostile to Bond; NPCs are safe only because guards aim at Bond.
Useful flags: `CHRFLAG_CAN_SHOOT_CHRS` (0x40), `CHRFLAG_INVINCIBLE`, `CHRFLAG_NO_AUTOAIM`,
`CHRFLAG_COUNT_DEATH_AS_CIVILIAN`. Prior art: Control's and Jungle's guards shoot Natalya with `TARGET_CHR` at
`CHR_PRESET`. The fork's faction layer (`port/src/port_faction.c`) redirects attacks, sight passes and retaliation; it
returns at once when inactive.

## Spawning at run time

- `chrSpawnAtCoord` (`chraction.c`, not in the header: declare it yourself) needs **at least 3 free slots**, picks a
  random head for `head < 0`, and tests a 20-unit volume, trying 8 directions at 60 units if blocked. Without spawn flag
  0x10 the spot must be off screen. Its volume test does not see the current player, so a spawn anchored on Bond lands
  inside him; the fork's `portSpawnChr` tries its own ring first.
- Runtime chrs draw normally; models load on first use. Clones (`CHRFLAG_CLONE`, "clone on heard gunfire") spawn next to
  the source, off screen.
- Fork Lua: `spawn.chr` and friends ([`modding/lua.md`](../modding/lua.md) "Spawning characters").
- Chr armour: `chr->damage < 0` is armour (`chrGetArmor`); hits eat it first and an armoured chr is not staggered.
  `model->playspeed` scales every animation and the movement code multiplies max speed and acceleration by it (the
  Fast Animation cheat sets 4.0 for all; the arena's `anim_speed` does it per chr).

## Routing

- Chrs route over **waypoints** (pads on the setup's path graph), not stan tiles. `plot_course_for_actor` takes the
  nearest waypoint to the chr and to the target, then `waypointFindRoute` goes **group by group** and crosses each
  group border at the **first** linked waypoint it finds (so small groups zig-zag). A route of fewer than 2 waypoints
  fails ("unreachable area"). A course keeps the target position from when it was plotted.
- While travelling, a chr checks every 10 ticks for a door on its path within 200 and opens it (`chrlvTravelTick`).
  It skips a door whose `flags2` has bit `0x20000000` (named `PROPFLAG_DOOR_OPENTOFRONT` at that line, but in `flags2`
  it is `PROPFLAG2_20000000`, "AI cannot operate"). That is the same bit the move test collides with
  (`CDTYPE_DOORSLOCKEDTOAI`), so an AI-locked door is never opened by an on-screen chr. The fork's heist door rules set
  the bit and let allowed guards past it (`portHeistChrMayOpenDoor` returns 2). An off-screen chr hops pad to pad and
  ignores doors entirely.
- The waypoint graph can be in pieces (Dam has 2 components); the stan tiles are what is really walkable.
- Re-issuing `chrGoToBond` while a chr is mid-route makes it march away (each plot starts from its nearest waypoint):
  re-plot only when it stopped or the target moved a lot.
- Stock `m_RunToBondPersistent` ends its run loop as soon as the chr *could* see Bond, at any range, then re-issues
  `TRYRunToBond` every tick while Bond is far (Rare's own "BUG - shuffles uselessly" comment). In open ground the chr
  stops and spins. The arena replaced it (`GAILIST_ARENA_CHASE` and the engage lists, `chraidata.c`). The stock
  `GAILIST_AIM_AT_BOND` is aim-only and never fires.
- Fork arena enemies take a straight line when the stock shortcut tests say it is clear (`portArenaDirect`).

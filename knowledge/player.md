# The player (Bond)

## Where Bond lives in the code

| What | Where |
| --- | --- |
| Per-tick input to movement (`moveData`), movement, collision, viewport | `src/game/bondview2.c` (the input function ~6500-8100, the move tick ~9700-10800) |
| Damage to Bond | `record_damage_kills` (`bondview2.c` ~13684), called through `bondviewCallRecordDamageKills` (~13944) |
| Inventory and keys | `src/game/bondinv.c` (`bondinvCheckHasKeyFlags`, `bondinvHasPropInInv`) |
| Pad 0 recording and replay | `port/src/inputrec.c` (`GE_INPUTREC`, `GE_INPUTPLAY`) |

Bond's **prop sits at eye height**, about 150 above the floor (`prop->pos.y` = floor + `eyeheight`). Use the stan
tile under him for "same floor" tests, never `prop->pos.y`. A guard's prop origin is its pelvis, about 105-110 above
its floor (55 kneeling); the head is about +52 over the origin. In solo first person Bond has **no chr**:
`bondviewRemovePlayerBody` frees it when the intro swirl ends and sets `prop->chr = NULL`.

## Input on the PC

The PC pad path is not the N64 controller path, and this changes how Bond moves (upstream behaviour):

- **The Move stick strafes.** In the port's pad branch (`bondview2.c` ~7769-7800) `analogStrafe = stick x`,
  `analogWalk = stick y`, `analogTurn = 0`, and `canTurnTank = 1` (despite its name it gates
  `speedsideways = analogStrafe / 70` on foot, ~8451).
- **The C buttons do not move Bond.** Their digital step flags are cleared (`digitalStepLeft = 0`, ~7794); they keep
  their aim-mode jobs (lean, zoom). Measured: C-left/right with or without forward gives under 1 unit/tick of strafe.
  (The player bot pressed C to strafe for three rounds and stood still the whole time.)
- **Look** comes from the mouse or the Look stick as direct-mode degrees for the tick, not as stick deflection. In
  co-op/netplay it travels as exact integer milli-degrees per tick (`portCoopLookSet/Take`, `port/src/port_coop.c`).
- **The port's digital move** (keyboard WASD, `portInputDigitalMove`, ~7811) sets the digital step flags: forward/back
  and left/right steps, like an N64's C buttons used to.
- Port actions (next weapon, reload, use, crouch, ...) are edge-latched in `port/src/joy_port.c`. Under lockstep their
  staleness is counted in ticks, not wall time.
- **A only cycles guns** (`bondinvCycleForward` stops at `ITEM_BOMBCASE`). Gadgets come round with the port action
  "next inventory" (`PORTACT_NEXT_INV`). B is both use and reload.

Direction conventions (GoldenEye's world is mirrored): with yaw `t` = `vv_theta`, forward is `(-sin t, cos t)`.
Positive stick x moves Bond toward yaw `t + 90 deg`, which is against `(cos t, sin t)`. Positive yaw increases
`vv_theta`. **The game's own `atan2f` (`src/game/math_atan2f`) returns [0, 2 pi), not (-pi, pi]**; normalise before
comparing angles, or use `__builtin_atan2f`.

## Movement speeds (measured)

Measured by the player bot's `GE_PLAYERBOT_SSTEST=1` (bot branch): for the first 25 s of a stage each input is held
along a line with the view fixed, on Dam's start road, Agent, 60 Hz, `GE_FIXEDTICK=1`.

| Input | Speed | Direction |
| --- | --- | --- |
| Stick forward | 8.4 u/tick | along the view |
| Stick forward + right (bot's |x|+|y| <= 130 gate: 65/65) | 9.0 u/tick | 40 deg off the view |
| C-left/right, with or without forward | < 1 u/tick sideways | (does not strafe, above) |
| **Digital move forward + right (speed-strafe)** | **11.0 u/tick (+31%)** | 38 deg off the view |

Why the diagonal digital move is faster (`bondview2.c`):

- The walk animation's forward root motion is scaled by `speedforwards / maxspeed` (the bot's reading of the move
  tick; the `maxspeed` part below is checked), where `maxspeed` is the largest of
  `|speedforwards|`, `|speedsideways| x 0.8` and `|speedtheta| x 0.8` (~10700-10730; `MAX_SPEED_FACTOR` 0.8, ~156). A
  full strafe therefore does not reduce the forward speed, and the strafe is added on top (`bondhead.c`).
- The digital strafe ramps: `speedstrafe` moves by `g_GlobalTimerDelta` per tick toward +-1 (~5195-5220), so flipping
  sides costs about 2 ticks. The analog strafe is instant.
- To speed-strafe, hold forward + one side on the digital move and turn the view about 38 deg away from the travel
  direction (`GE_PLAYERBOT_SPEED=1` in the bot does exactly this).

## Health, armour and damage

- `bondhealth` and `bondarmour` are 0..1 bars scaled by `actual_health` / `actual_armor`. Body armour pickups add to
  `bondarmour`.
- `record_damage_kills` (~13684) scales incoming damage by the handicap and (fork/upstream modifier) `enemy_damage`,
  quarters it in a tank, and applies it only when **all** of these hold: not the invincibility cheat, not dead, not
  `g_PlayerInvincible` (`BondDisableDamageAndPickups`), **no damage flash showing** (`damageshowtime < 0` in solo), and
  **the watch is not fully open or closing from open** (`watch_animation_state` 5 or 0xc: Rare's pause rule).
- Co-op (fork): a Bond's shot or blast on the other Bond does half damage; own blasts are full (~13706).
- How much a guard's hit does, and how often a guard hits: [guards-and-ai.md](guards-and-ai.md) "How a guard's shot
  hits Bond". Per hit: `0.125 x DestructionAmount x g_AiDamageModifier x 007 damage mod`, x3 for the shotgun and auto
  shotgun (`chraction.c` ~6891).
- The bot measured damage per hit falling from about 0.06 to 0.03 of the bar as Bond's health fell, at Agent
  **(unverified: mechanism not traced)**.
- Arena (fork): health per kill instead of health pickups (`modding/arena.md` "The economy").

## Autoaim and hitting things

- Bond's shots are screen-based: `chrTestHit` / `chrHandleBulletHit` (`src/game/chr.c`) test the crosshair ray against
  each on-screen chr's hit model, which exists only if the chr was drawn (it took ModelHitEntries that frame).
- Autoaim (`chrpropUpdateAutoaimTarget`, per player) reads the on-screen box. In co-op it uses the canonical 16:9
  sim view, so a chr just outside a 4:3 window can still be targeted.
- The game's own shot registers count head / body / limb hits; the bot reports them as `hits=h/b/l`. Aiming at a
  chr's origin + 52 gave 2-3x the head hits per shot of +15..38 (bot, heist A/B).

## Pickups

- `objTickPlayer` picks an item up when Bond is within 100 units horizontally and +-200 vertically, is not looking
  steeply down, the item is not still moving, **and there is a clear stan line from Bond's tile to the item's tile**.
  A zero-length line from a pad centre (which lies on a tile edge) fails that test, so a Bond warped exactly onto a pad
  centre never picks up an item lying there; 20 units off picks it up the same tick (arena A6 tests).
- Solo play doubles ammo from ammo boxes (`g_SoloAmmoMultiplier`).

# Co-op heist roles: Inside Man and Overwatch (design)

Status: design only, nothing built. Branch `designs` (from heist b389df2). Claims about existing code cite the file and function they come from. Guesses are marked **(guess)**. Things to check before building are marked **verify**.

## 1. The pitch

Two players, two jobs. The **Inside Man** walks into the Janus bank in a banker's cover (section 16 of `bank-heist.md`), lifts cards, plants taps, and keeps his cover. **Overwatch** sits in the van on the street with the camera feeds, the live INTEL board and a hacked line into the security system. He can't touch anything inside the bank except through what the Inside Man plants. Overwatch tells the Inside Man where to go. The Inside Man gives Overwatch his tools. Neither can finish the heist alone.

`bank-heist.md` section 5 already names the hook ("Overwatch sits in the control room calling out cameras, while Inside climbs the cards"), and P4 lists "Overwatch roles". This design makes it concrete.

## 2. What already exists (and what this design builds on)

| Piece | Where | What we reuse |
|---|---|---|
| Two Bonds, per-Bond views | `port/src/port_coop.c` (`portCoopActive`, `portLocalPlayer`, `portCoopSetLocalPlayer`, out-of-lives `sWatching`) | Overwatch is player 1 with a real body standing in the van |
| Lockstep input | `port/include/port_netmsg.h` `PortNetInput` (buttons, stick, `actions` bitmask, dig move, flags, `yawMilli`/`pitchMilli`), `port_net.c portNetTickInputs` | Everything Overwatch does is derived from his synchronized record. No new wire message |
| Camera feeds | `port/src/port_cctvview.c`: `sView[p]` is sim state, `portCctvViewTick(p, sx, sy, buttons)` eats the Bond's input in his own tick, the picture is a presentation pass (`portCctvPassBegin`, `portCctvPassMatrices`) | The van console's FEEDS tab is this, unchanged. While a feed is open, Z, R, C-up, C-down and stick Y are unused (`portCctvViewTick` reads only A, B, C-left, C-right and stick X) |
| Camera sight tests | `portCctvSeesChr`, `portCctvSeesPoint` | Picking a guard on a feed |
| Tape loop | `tools/heist_gadgets.lua` `loop_target` / `loop_plant` / `loops_tick` | A remote loop is `loop_plant` with the camera being watched |
| Dial minigame | `port_gadgets.c portGadgetsDial(p, on, start, period, center, width)`, the stethoscope in `heist_gadgets.lua crack_tick` | The door-hack minigame |
| Pickpocket, disguise, timed tasks | `portGadgetsLiftKey`, `portGadgetsDisguise`, `heist_gadgets.lua task_start` / `task_tick` | Planting a tap is a `task_start` with a new kind |
| AR overlay | `port/src/port_arhud.c` (PRES; `portArMark`, `portArOutline`, `portArPoint`; Lua calls are ignored while `g_PortResim`) | A new `crew` layer that draws pings |
| AI awareness | `port_ai.c portAiAwareness(chr, &state, &level, &lastx, &lastz)`, `portAiInfo` | A pinged guard shows his meter through walls |
| INTEL board | `port/src/port_intel.c` (PRES; content built by `tools/heist_intel.lua` on its own clock, live guards appear only once a feed has seen them) | The MAP tab is the board full screen with a sim cursor drawn over it |
| Security room report | `mode_heist.lua` `H.report` (`security_delay`) | A failed hack is a "trace" through the same path |

## 3. The roles

### 3a. Inside Man (player 0 by default)

He plays the heist as it is today: cover, cards, the cracker, the gold. Two things change.

- **Fewer tools of his own.** The Inside Man has no tape loops (Overwatch owns them) and the thermal imager is replaced by Overwatch's pings. He keeps pickpocket, disguise, lockpicks, coins and the piton.
- **New: he plants devices.** He carries 4 **taps** (a new gadget slot, a borrowed stock item as in `heist_gadgets.lua`'s table). With the tap in hand he holds fire at a plant point. That runs a 3 s `task_start` task, and a cover guard who sees it counts it as an act (`seen_act`). There are two kinds of plant point, both found by pad name, the same way grapples and vents are (section 21c):
  - `panel_<door>`: a door's access panel. Overwatch can then hack that door.
  - `junction_<zone>`: a floor's camera junction box. Overwatch can then loop that zone's cameras. Without a junction, a loop has to be planted by hand.

  **verify:** bank v3 needs these pads (an addition to `bank-v3.md` section 8's interface list; the generator places them).
- **Ack.** A new Port action `crew_ping`, default `F` on keyboard and D-pad up on a pad. Tapped, it answers Overwatch's last ping ("copy"). Held for 0.5 s at a camera or door, it asks for a loop or a hack there.

### 3b. Overwatch (player 1 by default)

He spawns on a `van_console` pad on the street. A use press there opens the **van console**: sim state per Bond, a sibling of `sView` (§5). While the console is open his body stands still. Getting hit throws him out, as on a feed (`portCctvViewTick` why 3). Police waves (another stream) can reach the van, so he may have to drive it away (§3d).

The console has two tabs:

1. **FEEDS**: the existing camera view (`portCctvView`), with three extra controls:
   - **Ping**: marks the chr nearest the feed's centre (see §6).
   - **Loop**: loops the camera he is watching. It needs a junction tap in that zone, and the feed then shows `[LOOP]` static. **Looping a camera blinds Overwatch too.** That trade-off is the point.
   - **Snap**: logs the chrs on the feed into the board's knowledge. This is what `heist_intel.lua` already does on its own for any feed. Here it is automatic.
2. **MAP**: the INTEL SCHEMATICS page full screen (`port_intel.c`, the radar layer on). A **sim cursor** sits over it:
   - Point ping: a waypoint the Inside Man sees as an AR diamond.
   - Guard ping: only on guards whose live position the board knows. Knowledge is the reward for watching feeds, as section 19 already says.
   - Door hack: on a tapped door, it opens the minigame.
   - **Getaway call**.

### 3c. The door hack (minigame)

At a tapped door, Overwatch starts a hack. The Lua runs the stethoscope dial (`portGadgetsDial`) with three windows, and the window width shrinks with the door's colour (GREEN wide, RED narrow). A press inside the window clicks a tumbler. A miss adds one **trace**. At 3 traces the security room "notices" through `H.report`: the alarm follows after `security_delay` unless the Inside Man reaches the security room first. A finished hack opens the door for 20 s through the heist's own `door.*`, not as a permanent unlock. Starting tunables go in `S.crew.hack` (window widths, traces 3, open time 20 s).

### 3d. The getaway call

From the MAP, Overwatch picks an exit (the van at the street, or the dock) and calls it. The van is a mode prop **(guess: a static prop or a pad marker; the van has no model of its own today)**. It takes 45 s to arrive and waits 60 s. The Inside Man's exit zone (`H.zones.exit_a`, `mode_heist.lua`) counts only while the van is there. If the call goes in too early the van has to circle, and a second call costs another 45 s. While Overwatch is driving to the dock, the console is closed. That ties into the police-waves stream: Overwatch sees on the feeds which exit is blocked.

## 4. What each player sees

| | Inside Man | Overwatch |
|---|---|---|
| World view | first person, as today | the van interior; the feed picture or the board when the console is open |
| AR | the heist's specs (section 20.1) plus the `crew` layer: pinged guards (cyan brackets and their meter through walls for the ping's lifetime), waypoint diamonds, "LOOP 0:42" over looped cameras, "HACKING…" over a door | the specs on the feed picture: the marks of chrs the feed shows (**verify** that the AR projects through the feed's camera and not Bond's; `port_arhud.c` projects through the local Bond's `field_10CC` today, so it needs the camera's `sW2S` when `portCctvPassActive()`) |
| INTEL board | as today (Tab), live guards only once seen | always live on MAP; radar on |
| HUD | a crew line under the objective: Overwatch's last ping, the van's state | traces 0/3, loops left, taps placed, van ETA |
| Sound | a radio squelch per ping (ROM sfx, as `ar.say` "radio" does) | the feed is silent (as today) |

## 5. Sim vs presentation (the netplay rule)

The rule from `bank-heist.md` section 5: sim state is read from canonical per-player state and recorded inputs, never from the window or the local player. Presentation is free.

| SIM: hashed, snapshotted, same on both machines | PRES: this machine only, never rewound |
|---|---|
| Who has which role (a session setting: the host's SETUP mods text `heist_roles=inside,overwatch`, `port_net.c netModsText`, never the local ini) | The feed picture, the board view, the van interior draw |
| Console open, tab, cursor (floor, x, z) | The AR `crew` layer drawing |
| The ping table (§6) | Radio sounds, HUD text layout |
| Taps planted, loops (charges, timers), hack state (dial phase, traces), the van (called, ETA, at exit) | The minigame dial's overlay (`portGadgetsDial` draws are PRES; its timing is sim) |

Where the state lives:

- **C**: a new `port/src/port_crew.c`, classed SIM in `scripts/port/snap_embed.py`, folded into `portHeistStateHash` the way `portGadgetsHash` is. It holds the console state, the cursor, the ping table, and the console's button edges.
- **Lua**: the rest (taps, loops, hack, van) in a new `tools/heist_crew.lua`, included after `heist_gadgets.lua`, and appended to `mode.state` like the gadgets' state.

**How input reaches it.** `portCrewConsoleTick(p, sx, sy, buttons)` is called in `bondview2.c` before `MoveBond`, next to `portCctvViewTick`, and is just as input-driven:
- The cursor moves by the stick, and by the record's look degrees (`yawMilli`/`pitchMilli`) when the mouse aims. **verify:** what the look fields carry in each mode.
- Buttons are N64 bits, so pad and keyboard map through the player's existing binds.
- Nothing reads the mouse position on the window.

**Rollback caveat.** `port_arhud.c` ignores Lua AR calls during `g_PortResim`. So the crew layer must **read the sim ping table every frame** rather than receive `ar.mark` calls from Lua events. Otherwise a ping made in a re-simulated tick would vanish from the screen.

**verify:** `portCctvViewTick` takes the feed's Port actions from `portJoyPortCctvTake()`, which has no player argument. In netplay that needs to be the ticking Bond's synchronized actions, not the local machine's. Check this before stacking more console controls on it.

## 6. The ping and mark protocol

Pings are created **in the sim from synchronized input**. They need no network message, they replay from any input log, and they rewind with a rollback.

```
typedef struct {            /* port_crew.c, 8 slots, hashed */
    u8  used, kind, from;   /* kind: GUARD, POINT, CAMERA, DOOR, ACK, ASK_LOOP, ASK_HACK */
    u8  toMask;             /* Bonds who see it (bit per player; Overwatch's own pings: everyone else) */
    s16 chr, obj;           /* GUARD: chrnum; CAMERA/DOOR: setup index; -1 none */
    f32 x, y, z;            /* POINT, or the anchor */
    u32 born, ttl;          /* sim ticks (portSimOnlyTickCount) */
    u16 seq;                /* per stage, for the AR's pop-in animation key and replay bookmarks */
} CrewPing;
```

**Rules:**
- At most 3 live pings per issuer. A fourth replaces the oldest.
- A GUARD ping lasts 8 s, a POINT ping 15 s.
- Overwatch can ping once every 1.5 s.
- Overwatch can only ping a guard he has knowledge of: on the open feed (`portCctvSeesChr(cam, chr)`) or in the board's live set. The live set is Lua, so the pick goes through a Lua call made in the same tick. Lua is sim (lock-step, its arena is snapshotted), so this is deterministic.

**Feed pick:** the guard whose bearing from the camera eye is closest to the feed's look direction, within 12°, among those `portCctvSeesChr` passes. It uses the camera record's position and its live sweep, which are sim, not the presentation `sEye`.

**Events:** a new `PORT_EV_CREW_PING(kind, from, chr/obj, seq)`. It feeds the radio sound, the HUD line, and replay bookmarks (`heist-replays.md`).

**Lua:** `crew.ping(kind, target, opts)` for scripts, the AI Overwatch (§9) and tests; `crew.pings()` to read the table.

**Considered and rejected: session commands.** `PORT_NETCMD_*` (`port_netcmd.c`, `portNetCmdIssue`) are live only in netplay (`portNetCmdLive`) and land at now + delay + margin. Input-derived pings work the same in solo, local co-op and netplay, and need no protocol bump (`PORT_NET_PROTO` stays 7). New Port actions (`crew_ping`) do ride the record's `actions` mask. It has 25 of 32 bits in use (`joy_port.c` enum), so there is room.

## 7. Input mapping

**Overwatch with the console open.** These are N64 bits after binds, so keyboard and mouse follow the player's own binds. The keyboard column shows the shipped defaults.

| Job | Pad | Keyboard / mouse |
|---|---|---|
| Next / previous camera | A / C-left, C-right, stick X (as today) | wheel (next/prev weapon bind, as today) |
| Leave console | B | E (use) |
| Ping (feed: centre target; map: under cursor) | Z (fire) | left mouse |
| Loop this camera / hack this door | R | right mouse (aim) |
| Switch FEEDS / MAP | C-up | R (reload) **verify:** reload is not already taken by the feed code |
| Map floor up / down | D-pad up / down | Up / Down (watch_up/down are free outside the watch) |
| Map cursor | stick | mouse |
| Minigame click | Z | left mouse |
| Call the getaway (map, on an exit) | hold Z 1 s | hold left mouse |

**Inside Man:** as today, plus `crew_ping` (F / D-pad up). The tap is cycled into the hand like any gadget.

**The INTEL key (Tab / Back) stays local-only** (`joy_port.c` strips it before the wire). The console's MAP is sim-driven by its own controls, so the board's view toggle and the console never fight.

## 8. Balancing

Overwatch must matter but must not trivialise the heist.

- **Knowledge is earned, never free.** Overwatch sees only what working cameras see. The west wing, records and cells have no cameras (section 10), so those are Inside-Man-alone zones by design. The power cut kills the feeds (`portHeistPowerOn`): taking the power (the second way into the vault) also blinds Overwatch.
- **The tools need the other player.** No hack without a panel tap. No remote loop without a junction tap. 4 taps for about 8 useful targets means planning, which is the planning phase's hook later.
- **Loops cost sight.** A looped camera is static for Overwatch as well (section 21's `[LOOP]`). There are 3 remote loops of 45 s each.
- **Pings decay and cap.** 3 live, 8 s on a guard. Overwatch cannot tag the whole hall.
- **Traces put the hack at risk.** 3 misses start the security room's clock, and the Inside Man can answer it by getting to the security room.
- **Overwatch is vulnerable.** At about 3 minutes into the alarm, a patrol checks the street (**guess**, a police-stream hook). If he is hit, the console closes.
- **Tuning target:** the quiet route should be about 20-30% faster with good Overwatch play than solo (**guess**, measured by the bot ladder of §10). Bad Overwatch play should never be worse than no Overwatch, except through traces.

**Both inside (no Overwatch):** both Bonds play as today. The van console is still there. Walking out to it is a legitimate tactic, and the hall screens keep their feed. Every tap still works. **Role swap:** both Bonds at the van, both press use: they swap kits (taps, loop charges) and the console seat. Bodies stay where they are, and the disguise stays on its body. Swapping which machine drives which Bond (`portCoopSetLocalPlayer` in netplay) changes record ownership. That is out of scope (open question 3).

## 9. Solo fallback

1. **AI Overwatch ("Q")**, the recommended default. A Lua director in `heist_crew.lua` with no body. It uses **the same knowledge gates**: it "watches" one camera at a time, round-robin every 4 s, through the same feed tests.
   - It auto-pings the nearest guard closing on the Inside Man (awareness rising: `portAiAwareness` state ≥ 1, or inside 10 m and facing him).
   - It answers an ask-loop or ask-hack after 3-5 s: the hack has no minigame but costs a trace at 30% odds.
   - It calls the van when the loot is in the bag.

   Everything is sim, so it is deterministic and replays.
2. **A second body driven by a bot** (tests only). Add a puppet source `GE_COOP_P2=overwatch` to `port_coop.c pupParse` (it has `walk`, `follow`, `spin`, `replay:`). It sits in the van and runs the AI director's decisions as console button presses, which exercises the real input path.
3. **Solo human Overwatch with a bot Inside Man** (later; needs `port_playerbot.c` to drive player 0 while the human is player 1).

## 10. Test plan

- **Logic tests** (`tests/crew_test.c`, added to `run_logic_tests.sh`): the ping table (cap, replace oldest, TTL, cooldown), the feed pick geometry, the cursor clamp per floor, the trace counter.
- **Determinism:** extend `tests/heist/heist_determinism.lua` with a crew segment: open the console, cycle, ping, loop, a hack with one miss, a van call. Run `tests/heist/run_determinism.sh`:
  - solo with the AI Overwatch twice;
  - local co-op twice and view-switched (`GE_COOP_VIEW=1`);
  - lockstep netplay host and guest.

  Each pair must be hash-identical tick for tick.
- **Presentation proof** (as `run_ar.sh proof`): the crew layer drawn vs `GE_AR_OFF=1` gives identical hashes.
- **Rollback:** `GE_SNAPSTRESS=8,<tick>` across a ping. The AR shows the ping after every re-sim (proves §5's caveat). `GE_SNAPCMP=1` is clean.
- **Bot-driven:** the heist bot (`tools/bot/heist_bot.lua`) as Inside Man with the AI Overwatch on the tune ladder (`GE_HEIST_TUNE1..5`). Measure the time to vault and blown covers against the solo bot (the §8 target). It also needs a bot step "plant tap at X".
- **Netplay soak:** a synthetic Overwatch input file (generator in the style of `net_soak_gen.py`) that opens the console and spams pings and loops, run against the bot Inside Man: no DESYNC.
- **Screenshots:** two at most (feed with a ping, the partner's AR). The disk is tight.

## 11. Phased delivery

1. **P1, the slice (§12).**
2. **P2, the console proper:** `port_crew.c` (state, hash, snapshot class), the FEEDS controls, the ping table and the AR crew layer reading it, the `crew_ping` action, determinism and the presentation proof.
3. **P3, the MAP tab:** the sim cursor, map pings gated by board knowledge, the getaway call and van states.
4. **P4, taps:** the pads on bank v3 (`gen_level` interfaces), the tap gadget, the remote loop, the door hack minigame and traces.
5. **P5, solo:** the AI Overwatch, the bot ladder, tuning.
6. **P6:** the role swap, Overwatch threats (the van patrol), planning-phase hooks (pre-placing taps).

## 12. Smallest fun slice (a day)

The goal is to playtest "a friend on the cameras calling out guards" over the existing netplay co-op.

1. **The van console:** in `mode_heist.lua`, a `van_console` zone on the street pad. A use press there calls `cctv.view(p, first_cam)`, the same code the hall screens use.
2. **One small C change** in `port_cctvview.c`: store the Z and R press edges per Bond while he is watching (the sim already has `sPrevBtn`), plus a binding `cctv.pressed(p) -> z, r`. About 20 lines, hashed with the view state.
3. **Z on a feed:** the Lua picks the guard nearest the camera's look direction among those `cctv.sees(cam, chr)` passes. It calls `ar.outline(chr, "cyan")` and `ar.say(chr, "Overwatch: this one", 3, "radio")` for 8 s.
4. **R on a feed:** `loop_plant(p, me, cam)` with 2 charges. No taps yet.

That is enough to learn whether calling out guards is fun. In this slice both machines draw the outline (the overlay is not filtered per Bond yet), and Lua AR calls are lost under rollback. Lockstep netplay doesn't re-simulate, so neither matters for the playtest.

## 13. Open questions for AC

1. Should Overwatch have a body in the van that can be attacked (stakes, but he can die), or be safe and bodiless (simpler, and solo is closer to co-op)?
2. Taps: are pads named by the generator (`panel_<door>`, `junction_<zone>`) fine, or should taps work on any door?
3. Role swap: swap kits at the van (cheap), or swap who controls which Bond (expensive in netplay)? Or skip it?
4. Should the Inside Man lose the tape loop and the thermal imager when an Overwatch is present? This design says yes, so the roles stay distinct.
5. Getaway: is a van that has to be called (and can come too early or too late) more fun than the fixed exits we have now?
6. Hack failure: should 3 traces start the security room's clock (this design), or trigger an instant alarm?
7. Should solo get the AI Overwatch by default, or only when switched on (`S.crew = "ai"`)?

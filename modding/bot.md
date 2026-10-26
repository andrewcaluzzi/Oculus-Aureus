# The player bot

An AI that plays solo missions **as Bond, through player 1's controller**. It is for full-level regression testing
(does every mission still complete?), and a base for speedrun experiments and for playing game modes such as Bank
Heist end to end.

It only produces **input**: pad buttons and stick in `osContGetReadData`, and the look as direct-mode mouse degrees
in the per-tick mouse sample. It never writes game state. So a run it plays is an ordinary input stream:
record it with `GE_INPUTREC` and `GE_INPUTPLAY` replays the same mission with the bot switched off. The level test
harness does exactly that for every completed run and checks the replay ends the same way.

With `GE_PLAYERBOT` unset every bot hook returns at once and the game is unchanged (the dam1 attract-demo oracle is
identical).

## Quick start

Inside the build container, with Xvfb on `:99` (working notes: `modding/bot-notes.md`):

```sh
sh tests/bot/level.sh dam 0          # one stage, Agent: play, record, replay, verify -> one result line
sh tests/bot/campaign.sh 0           # every stage in campaign order, retries, a table in build_tests/bot/campaign.d0/
sh tests/bot/dev.sh facility 900     # developer loop: one fast run, the bot's log lines, screenshots
sh tests/run_logic_tests.sh          # unit tests (graph search, funnel, plan parser, stuck detector, ...)
```

The result line is tab separated:

```
stage  diff  outcome  mission_time  ticks  health_lost  kills  objectives  replay  wall  failure_point
dam    0     COMPLETE 249s          15398  0.12         3      1/1         same    25s   -
```

`outcome` is `COMPLETE` (the stage's own mission end, all objectives done), `FAILED_DEAD`, `FAILED_OBJECTIVES`
(the mission ended with an objective open or failed), `TIMEOUT` (`MAXSECS` of mission time ran out) or `ABORTED`.
`replay` is `same` when the recording, replayed without the bot, ended with the same outcome, mission time and
objectives and with no position drift; `failure_point` says where it died or got stuck (room, position, plan step).

## Running it by hand

| Variable | Meaning |
|---|---|
| `GE_PLAYERBOT=1` | drive player 1 (`watch` = only report; implied when `GE_INPUTPLAY` is set) |
| `GE_PLAYERBOT_PLANDIR` | where the plans are (default `Data/BotPlans`, which the build fills from `tools/bot/plans`) |
| `GE_PLAYERBOT_PLAN` | one plan file for whatever stage loads |
| `GE_PLAYERBOT_MAXSECS` | give up after this many mission seconds (default 1200) |
| `GE_PLAYERBOT_EXIT=0` | do not exit the process after the result line |
| `GE_PLAYERBOT_LOG` | 0 quiet, 1 status every 5 s (default), 2 + door / stuck details, 3 + a line per tick |
| `GE_PLAYERBOT_NOLOOT` | do not detour for dropped guns, ammo and armour |
| `GE_PLAYERBOT_HOLSTER=1` | a heist's banker cover: `holster 1` from the first tick, and never the stuck recovery's blind B press (a use press with nothing in reach is the heist's pickpocket) |
| `GE_PLAYERBOT_AIMY` | aim height over a chr's origin (default 52, the head) |
| `GE_PLAYERBOT_NOCOVER`, `GE_PLAYERBOT_NOKITE`, `GE_PLAYERBOT_RUSH` | A/B switches: no cover, no backing away, `rush 1` from the start |
| `GE_PLAYERBOT_SPAREALL`, `GE_PLAYERBOT_GLASSTEST` | experiments (quiet: shoot only guards that noticed Bond; shoot raisers through closed doors) |
| `GE_PLAYERBOT_SPEED=1` | the speedrun movement: speed-strafe on the digital move (forward + sideways, the view 38 deg off the travel direction; +31% on a straight), dropped to plain running (view ahead) for 2 s whenever an armed hostile is within 1000 with a line (1800 if he has noticed Bond) or Bond is hurt |
| `GE_PLAYERBOT_DODGE=N` | strafe N ticks each way while shooting (default 0: stand) |
| `GE_PLAYERBOT_OLDCOVER=1` | the round-3 cover rule (nearest tile no threat sees) instead of the exposure score |
| `GE_PLAYERBOT_SSTEST=1` | experiment: the first 25 s measure each movement input (stick, stick diagonal, digital diagonal, C strafe) |
| `GE_PLAYERBOT_SPEED=2` | speed-strafe as above, plus fire on the move (`rush`) and no detours for guns and ammo while the gun has 30+ rounds |
| `GE_PLAYERBOT_NOAIMSCORE=1` | A/B: rank targets without the aim-progress weight |
| `GE_PLAYERBOT_DOORS=1` | at stage start, log every door (tag, rooms, key, link, flags) and the door pairs sharing a doorway |
| `GE_PLAYERBOT_LOCKCLOSED=N` | diagnosis: 1 doors Bond cannot open close their tiles (is there a way round?), 2 keyed doors are ignored, 3 the round-4 lock rule |
| `GE_PLAYERBOT=speedrun` | the speedrun mode (below): the stage's route, never stopping to turn, doors on the move |
| `GE_PLAYERBOT_ROUTEDIR` | where the routes are (default `Data/BotRoutes`, filled from `tools/bot/routes`) |
| `GE_PLAYERBOT_OPT=1` + `GE_PLAYERBOT_OPTSTATE=<file>` | speedrun: the segment optimiser (savestates); with only `OPTSTATE`, play the variants the file chose |
| `GE_PLAYERBOT_OPTWIDE=1` | optimiser: `via` waypoints are also tried 160 off (14 variants instead of 10) |
| `GE_SNAPSOLOTEST=T,N[,1]` | savestate fidelity test: save at T, run to T+N, restore, run again, compare (`modding/snapshot-fidelity.md`) |

## Speedrun mode

`GE_PLAYERBOT=speedrun` is opt-in; the normal bot stays the default for campaign testing. It plays
`tools/bot/routes/<stage>.route` (the plan if there is none): the plan language plus `via pad N [r]` / `via X Y Z [r]`
(a waypoint passed at full speed) and step prefixes `side=l|r` (speed-strafe side), `fast` (speed-strafe through
threats) and `slow` (the normal bot's movement for that step). On top of speed level 2 it never turns on the spot (the
digital move that points along the route from wherever the view is), opens doors on the move (B as soon as the game's
use test can take it, running on while the door swings; switch-worked doors go to their switch first) and counts
running along the route as progress (no false "stuck" detours on long tiles).

```sh
sh tests/bot/speedrun.sh dam          # optimise the route, then the clean run (recorded, replayed without the bot)
BOTMODE=speedrun sh tests/bot/level.sh dam 0     # one speedrun-mode run, no optimiser
```

The optimiser plays every variant of each route step (strafe side, `fast` toggled, `via` moved 80 units) from a solo
savestate (`port/src/port_snap.c`), each to the start of the exit sequence, keeps the earliest, and goes on to the next
step. Its choices land in `opt.state`; the clean run replays them without savestates and `level.sh` verifies the
recording without the bot. Since round 7 a trial in the optimiser's process and the clean run in a fresh process end on
the same tick on all nine stages (the snapshot holds everything the sim reads: `modding/snapshot-fidelity.md`; and the
poll a step begins in plays variant 0 in every mode). Details and results: `modding/bot-notes.md` "Round 6", "Round 7".

For speed and determinism the harness also sets `GE_FIXEDTICK=1` (one 60 Hz field per sim tick, so a run does not
depend on how fast the machine is), `GE_NOSLEEP=1` (no frame pacing), `GE_CLOCKSCALE=20` (the N64 clock runs 20x so
the main loop admits ticks that much sooner: a 4 minute mission plays in about 12 s) and
`GE_SIMONLY_TEST=2,59,60` (the sim runs without drawing except one tick in 60, which is what screenshots use).
`GE_SEED` fixes the RNG; the campaign uses seed = attempt number.

The bot exits the process with status 0 (completed), 1 (failed) or 2 (timeout) after printing
`ge: playerbot: RESULT ...`.

## How it plays

* **Navigation** runs A* over the stan tiles - the floor Bond actually walks on, thousands of small polygons linked
  edge to edge - not the guards' waypoint graph (which is in pieces on some levels). The route is string-pulled through
  the shared tile edges (a funnel, keeping Bond's radius off walls) and steered with the look yaw. Bond's own tile tells
  how far along the route he is; off the route for 1.5 s means plan again.
* **Doors**: a shut door on the route is walked up to and opened with B - the first one along the route (a B press works
  the nearest door Bond faces and closes it if it is open, so a door nearer than the target is walked past first:
  Caverns' stacked doors, two doors with a small room between). A locked door counts only if the route goes through its
  doorway (the door's room list), not past it in a side wall; a locked leaf of a linked pair is opened through its
  unlocked partner (Depot's train door). If B does nothing, the setup's switch links
  (consoles that work a door, e.g. Dam's gate controls) are tried in turn; padlocked doors get their padlock shot off;
  a door that needs a key sends the bot for the key - lying in the level, or carried by a guard it then waits for and
  takes down. A door that still will not open is avoided for 30 s.
* **Stuck**: no progress for 1.5 s -> back off and walk off at an angle (45, 75, 105 degrees, alternating sides); 4 s
  -> the link is made expensive and the route planned again; 12 s -> reported (`STUCK ...`) and counted against the
  step.
* **Combat**: every armed, non-civilian, non-ally chr in a straight line within 1600 units is ranked: alarm raisers
  first, then guards shooting at Bond (more so if they hurt him lately, and by how far each one's aim at Bond has built
  - his `shotbondsum`, at 1 Bond takes a hit), then the nearest; a guard with a civilian (or an ally)
  near the line of fire is not a target from where Bond stands. When Bond is hurt by someone the line test cannot see
  (catwalks, windows), the nearest guard in an attack action. It aims at the head (+52 over a chr's origin), fires
  when the yaw and the pitch are both on (semi-automatics are tapped), and:
  * keeps guards at range: a guard's aim builds per shot by 0.48 x min(1, 300 / distance) (chraction.c; Bond's own
    movement does not enter it), so it backs away from an alert guard inside 450 (700 when hurt);
  * with two or more guards with a line on him (or hurt), moves to the nearby tile (breadth first, same floor) where
    they hit him least - the sum of min(1, 300 / d) over the guards that see it, a hidden tile counting 0.25 - when it
    is clearly better than where he stands, and fights from there;
  * steps aside when a civilian stands in a guard's line of fire at Bond (the strafe is the Move stick's x: the port's
    pad path takes no strafe from the C buttons);
  * goes for lying body armour within 1500 when hurt (below 0.6);
  * with no gun at all (Bunker 2, Archives) closes in and punches, then picks up the guard's gun;
  * gives up on a target it has not had a shot at for 8 s (left alone for 10 s).
* **Weapons**: the best gun it has ammo for (RC-P90 ... PP7), cycled to with A; plan steps can ask for a gadget.
* **Pick-ups**: dropped guns, ammo boxes and body armour within reach are walked over.
* **Objectives**: `auto` handles every open destroy / collect / photograph objective by itself. Everything else
  (complete-on-flag objectives, which the level's AI sets when Bond does something) comes from the plan.

## Plans

`tools/bot/plans/<stage>.plan`, one step per line, `#` comments. Steps run in order; a step prefixed `obj=N` is
skipped once objective N is complete or not needed at this difficulty, `diff=A-B` limits it to difficulties A..B
(0 Agent .. 3 007). Fighting happens between and during steps (except while pressing buttons or taking photos).

| Step | Does |
|---|---|
| `goto pad N [r]` / `goto X Y Z [r]` / `goto obj T [r]` | walk there (within r, default 60) |
| `use obj T [r]` / `use pad N` | walk up, face it, press B three times |
| `collect obj T` | walk onto it until it is in the inventory |
| `destroy obj T` | get a line on it and shoot it (from range: many objects explode) |
| `photo obj T` | equip the camera, walk to a tile with a clear line to it (350-850 away), centre its box (the objective wants the whole box on screen), take pictures |
| `gadget <item> obj T` / `throw <item> obj T` | equip the item, face the object, fire |
| `meet chr N [r] [ticks]` / `meet ai ID [r] [ticks]` | walk up to a chr (by number, or by the AI list it runs) and stay |
| `equip <item>` | cycle to that item (`wppk`, `ak47`, `camera`, `keyanalysercase`, ... - the ITEM_ names in lower case) |
| `flag MASK [ticks]` | wait until the stage flag is set |
| `press <button> [ticks]` | hold a button (`a b z start l r cu cd cl cr du dd dl dr`) |
| `walk ticks stickY [stickX]` | raw stick |
| `face X Y Z` / `look pitch` | turn |
| `wait ticks` / `kill [radius]` / `peace 1|0` | wait / fight everything visible / stop shooting |
| `holster 1|0` | keep the slappers in hand while not fighting (a heist's banker cover reads a drawn gun); a step that wants an item still gets it. `GE_PLAYERBOT_HOLSTER=1` sets it from the first tick. Holstered, the stuck recovery's blind B press is skipped (with nothing in reach it is the heist's pickpocket, and a guard who sees it blows the cover) |
| `quiet 1|0` | stealth: silenced guns preferred; unaware guards facing away (beyond 250) are left alone; unaware guard key carriers are knocked out from behind (heist modes) |
| `key FLAGS` | fetch a key with any of these flags: lying (walk over it), on a guard (knock out and search, or shoot), on heist staff (hold a gun on it until it hands it over) |
| `rush 1|0` | keep walking the route while fighting (the stick and the C strafe buttons move, the look stays on the target) |
| `avoid pad N` | keep routes out of that pad's room from here on (a room that ends the mission early); `avoid pad -1` opens them all again |
| `spare ai ID` | never shoot chrs running that AI list (a hostage's captors whose death has the hostage killed); `spare ai 0` clears |
| `hunt` / `hunt ai ID` | walk to the hostile with the shortest route and fight, until none is left; `ai ID`: only chrs on that list, and they are shot before anyone else (Frigate's captors) |
| `escort ai ID [dist]` | from here on, gotos wait (fighting) while that chr is more than dist (400) behind, and walk back to it after 4 s; `escort ai 0` stops |
| `auto` | the generic objective handlers until none is left |
| `exit` | stand still and tap A until the mission ends (end cut-scenes wait for a button) |

`tools/bot/objectives.py assets/obseg/setup/Usetup<stage>Z.c` (run it on your extracted setup) lists a stage's
objectives, tags, and the AI lines that set each completion flag with the conditions before them - that is how the
plans were written. Note the AI macros store flags and pads byte-swapped (the tool converts them); a stage's AI list
`ai_N` runs as list id 0x401 + N. The bot logs every stage-flag change (`flags +0x... at t=`), so a run shows which AI
flag fired and when.

## Game modes (Lua) and the Bank Heist

With a mode active (`GE_MODE=...`), the bot loads `Data/BotPlans/<mode>.plan` if there is one; otherwise it follows the
steps the mode script feeds it through the Lua `bot` library:

| Function | Does |
|---|---|
| `bot.active()` | 0 off, 1 driving, 2 watching |
| `bot.push(line)` | append one plan step (same language as the plan files); `nil, message` on a syntax error |
| `bot.clear()` | drop the current and every later step (the situation changed) |
| `bot.step()` | the running step's number and the number of steps (`s > n` = idle) |

`tools/bot/heist_bot.lua` is the Bank Heist driver, on the generated Janus bank (since 2026-10-26; designs/bank-heist.md
23e): `GE_MODE=heist_bot GE_STAGE=levelgen GE_PLAYERBOT=1 GE_PLAYERBOT_HOLSTER=1`. It includes the real heist unchanged and
pushes, as the heist moves on: `quiet 1`, `peace 1`, `holster 1` (the banker cover: no gun in hand, no shots), then each
card - a guard carrier's pocket picked from behind (walk 85 units behind him, face him, B with nothing else in reach), the
head teller and any fallback by `key FLAGS` (staff held at gunpoint until they hand it over; a guard knocked out from
behind if unaware, else shot; then searched) - GREEN, BLUE, RED, the security console (cameras off 90 s), then the crack
spot: face vault door A and press B (starts the cracker), hold the zone and fight (`peace 0` from the vault level on),
restart it when it jams, loot the bars nearest first until 25 s before the time lock, out by the nearer exit. Once the
cover is blown or the alarm is up it fights. A mode's own `mission.win/fail` writes the RESULT line. From C,
`portPlayerBotSetPlanner(fn)` does the same job without Lua. `sh tests/heist/run_botheist.sh [seed ...]` plays seeds
headless and prints a PASS / FAIL per checkpoint (ready inside green blue console red vault_level cracking vault_open gold
out win) and the reach rate over the seeds. `tests/bot/heist.sh` (record + replay) is still the Bunker 2 harness.

Easy tunings for measuring it (the heist's defaults untouched): `GE_HEIST_TUNE1` (carriers and staff only, no jams),
`GE_HEIST_TUNE2` (30 posted at the full force, ~10 on Agent; one crack squad; no vault guards), `GE_HEIST_TUNE3` (no jams),
`GE_HEIST_CIVOK=1` (no civilian limit), `GE_BOTHEIST_NOPICK=1` (no pickpocketing). The Bunker 2 results (modding/bot-notes.md
"Round 3"/"Round 4": 10/10 with carriers only, 0/10 from 45 posted up) are history. Status of the campaign (9 of 20 stages on
Agent), the speedrun profiles and where the bot stands still: "Round 5".

## Known limits

See `modding/bot-notes.md` for the per-level status and the failure modes found so far.

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

Inside the build container, with Xvfb on `:99` (see `lab`-style notes in `docs/bot-notes.md`):

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
* **Doors**: a shut door on the route is walked up to and opened with B. If B does nothing, the setup's switch links
  (consoles that work a door, e.g. Dam's gate controls) are tried in turn; padlocked doors get their padlock shot off;
  a door that needs a key sends the bot for the key - lying in the level, or carried by a guard it then waits for and
  takes down. A door that still will not open is avoided for 30 s.
* **Stuck**: no progress for 1.5 s -> back off and walk off at an angle (45, 75, 105 degrees, alternating sides); 4 s
  -> the link is made expensive and the route planned again; 12 s -> reported (`STUCK ...`) and counted against the
  step.
* **Combat**: the nearest armed, non-civilian, non-ally chr in a straight line within 1600 units; when Bond is hurt
  by someone the line test cannot see (catwalks, windows), the nearest guard in an attack action. It turns to the
  target, fires when lined up (auto-aim does the rest; semi-automatics are tapped), and strafes a little.
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
| `photo obj T` | equip the camera, face it, take pictures |
| `gadget <item> obj T` / `throw <item> obj T` | equip the item, face the object, fire |
| `meet chr N [r] [ticks]` / `meet ai ID [r] [ticks]` | walk up to a chr (by number, or by the AI list it runs) and stay |
| `equip <item>` | cycle to that item (`wppk`, `ak47`, `camera`, `keyanalysercase`, ... - the ITEM_ names in lower case) |
| `flag MASK [ticks]` | wait until the stage flag is set |
| `press <button> [ticks]` | hold a button (`a b z start l r cu cd cl cr du dd dl dr`) |
| `walk ticks stickY [stickX]` | raw stick |
| `face X Y Z` / `look pitch` | turn |
| `wait ticks` / `kill [radius]` / `peace 1|0` | wait / fight everything visible / stop shooting |
| `auto` | the generic objective handlers until none is left |
| `exit` | stand still and tap A until the mission ends (end cut-scenes wait for a button) |

`tools/bot/objectives.py assets/obseg/setup/Usetup<stage>Z.c` (run it on your extracted setup) lists a stage's
objectives, tags, and the AI lines that set each completion flag with the conditions before them - that is how the
plans were written. Note the AI macros store flags and pads byte-swapped (the tool converts them).

## Game modes (Lua) and the Bank Heist

With a mode active (`GE_MODE=...`), the bot loads `Data/BotPlans/<mode>.plan` if there is one; otherwise it follows the
steps the mode script feeds it through the Lua `bot` library:

| Function | Does |
|---|---|
| `bot.active()` | 0 off, 1 driving, 2 watching |
| `bot.push(line)` | append one plan step (same language as the plan files); `nil, message` on a syntax error |
| `bot.clear()` | drop the current and every later step (the situation changed) |
| `bot.step()` | the running step's number and the number of steps (`s > n` = idle) |

`tools/bot/heist_bot.lua` is the Bank Heist driver: `GE_MODE=heist_bot GE_STAGE=bunker2 GE_PLAYERBOT=1`. It includes the
real heist unchanged and pushes: walk to the vault antechamber (the bot's door logic fetches the RED / BLUE / GREEN cards
from their carriers on the way), face vault door A and press use (starts the cracker), hold the zone and fight, restart
the cracker when it jams, loot the bars when the vault opens, leave by the van. A mode's own `mission.win/fail` writes the
RESULT line. From C, `portPlayerBotSetPlanner(fn)` does the same job without Lua.

Status (2026-10-25): it runs - card fetched, alarm at 48 s, 89 guards down - and dies at about 3 minutes in the loud
firefight, before the vault. Missing for a real heist: knocking card carriers out and searching them instead of shooting
them (staff carriers count as civilians), staying quiet (cameras, alarm raisers), and the crack/jam/loot phases have not
been reached yet, so they are untested.

## Known limits

See `docs/bot-notes.md` for the per-level status and the failure modes found so far.

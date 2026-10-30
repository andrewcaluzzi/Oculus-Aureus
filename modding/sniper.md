# The sniper duel (prototype)

A duel on the stock **Surface** stage (Severnaya: snow, long sightlines). Bond has a sniper rifle and a PP7 and starts on a high perch (pad 53); five rounds against named marksmen on the far slopes, 40 to 80 m off. The mode is `tools/sniper_defs.lua` (words, the ladder, the numbers, the perch lists), `tools/mode_sniper.lua` (the run) and `tools/mode_sniper_menu.lua` (its card and debrief, hosted by the main menu hub). The build ships them as `Data/Scripts/modes/_sniper_defs.lua`, `sniper.lua`, `sniper-menu.lua`.

## Launching

| Way in | What it does |
| --- | --- |
| `Play Oculus Aureus.bat`, the SNIPER DUEL card, BEGIN THE DUEL | `ui.launch{mode = "sniper", stage = "surface"}`; the debrief comes back on the hub's Control stage, MAIN MENU goes back to the cards |
| `Play Sniper Duel.bat` | `GE_MODE=sniper GE_STAGE=surface GE_NOINTRO=1 GE_STAGE_AFTER=1`; when the duel ends the hub loads on the debrief |

`SNIPER_SETTINGS = { key = value }` set before `include("sniper")` overrides `SNIPER_DEFS.S` (the tests use it). Best scores: `Data/Saves/ui/sniper_bests.ini` (`surface_rounds`, `_time`, `_runs`, `_wins`), their own file.

## How a round runs

- **No sweep, one warp.** Bond is warped once, at the start, onto his perch (`SNIPER_DEFS.BOND.pads`, ids only); a stand-in check of the listed perches (spectators, `combat.los`) takes a few ticks and shows nothing. The marksmen's perches are `SNIPER_DEFS.PERCH.pads`, hand-picked ids from a survey (`tests/sniper/geom.sh`): waypoint pads that BOTH the AI's line (the stan tiles, `combat.los`) AND the drawn geometry (`probe.los`: the background hit test a bullet uses, eye to head and to rifle, both ways) say see Bond's perch. A pad the stan line alone sees is a place a marksman could hit a Bond who cannot see him (94% of the stan-visible pads from the old perch, pad 158, were of that kind): not listed.
- **He is there to be found.** The marksman is a real `snipe` chr, drawn 1.5 times size (`spawn.scale`), on a perch for 10 to 20 s, aiming at Bond all the time. His shots are held (`spawn.hold`) until a glint. Props (chrs) are drawn only out to the stage's far fog (`probe.fog()`: 12500 units on Surface) and a man is a few pixels beyond 65 m, so the perches are 38 to 79 m off: visible in the scope, a speck unscoped.
- **The glint.** Every 4 to 9 s a glint: a marker (`ar.point`, an arrow on the screen edge when it is off screen), a chime and a centre callout `GLINT - 3 OCLOCK` with the distance. After the round's glint seconds (1.6 down to 1.0) the hold is lifted for one or two shots (`spawn.shots` counts them) and put back. Bond can kill him at any time on the perch.
- **Moving.** After the dwell he is replaced by a runner (the same man, a stock guard with his shots held and his sight and hearing off so nothing interrupts it) and `spawn.go(chr, pad, run)` runs him along the stock route to another listed perch 10 to 35 m off (about 6 s on foot, logged position by position: `RUN foe 1 at ...`); there he becomes the marksman again.
- **The shot.** Each glint tosses a coin (the round's `hit`, 35% to 65%): the hit marksman's `spawn.shotbond(chr, 1)` makes his first shot of the volley land (the stock accuracy build-up needs about 20 shots at this range). One hit takes `hit_damage` (42%) of a health bar on any difficulty (the rifle's damage is set at stage start).
- **Rushers** (rounds 3 and 5) arrive 28 to 55 m off, out of Bond's cone, a few seconds in, and run at him with SMGs.
- **The run.** A round is won when its marksmen are dead (rushers are removed); Bond is healed and re-armed. Bond dies and the run ends. The debrief shows rounds won, time, accuracy, headshots, shots, hits taken and who felled him.

## Tests

`tests/sniper/run.sh [frames] ["GE_X=y"]` (the ladder won; `"GE_SNIPER_LOSE=1 GE_CHEATS="` the loss path), `hubbegin.sh` (the hub card BEGINs), `geom.sh` (stan vs drawn geometry per candidate Bond pad), `vis.sh` (how far a man is drawn and seen), `ai.sh`, `matrix.sh`, `probe.sh`, `shots.sh`, `win_smoke.sh`.

## Known gaps

- The first appearance of a marksman is a pop-in on his perch; the later ones are runs.
- Bond's perch has no cover; Surface's own geometry is what it is. A map built for the duel is the point of the SEVERNAYA RIDGE level.
- A runner takes a stock route, so a long way round is possible; it is put on its perch after 25 s at most.
- No spotter; no Golden Gun finale.

## SEVERNAYA RIDGE (the duel's own level)

`tools/levelgen/build_ridge.py` writes `tools/levelgen/ridge.json`, and `python3 tools/gen_level.py tools/levelgen/ridge.json --out port/levelgen_ridge` generates it (`modding/levelgen.md`: box rooms; the textures are the stock Surface snow and rock numbers, read from the ROM at run time). It is the levelgen slot's EXTRA level (`-DGE_LEVELGEN_EXTRA=port/levelgen_ridge -DGE_LEVELGEN_EXTRA_NAME=ridge`, chosen by `GE_LEVELGEN=ridge`; one extra at a time, so the ridge builds have no casino job).

Five open-top rooms in a row along z: Bond's ridge (south, floor 7 m), a ramp down, the valley (60 m wide), a ramp up, the hunters' ridge: about 113 m lip to lip. Doorways as wide as the rooms join them, so the whole level is one view. Each ridge has a PARAPET along its lip (tall blocks with 5 m windows) and a second wall behind it with the windows the other way (a trench between): named pads `sp1..sp8` (Bond's windows; he starts on `sp4`) and `np1..np8` (the hunters'). The valley has ROCKS (blocks: three tall pillars block the view as well as the shot, the low ones only the stan line) and pads `vp1..vp6`. A block is bg geometry AND a stan hole, and a wall is both, so what the eye cannot see through the AI cannot shoot through; the smoke logs `SMOKE LOSAGREE` (perches seen by both lines, blocked by both, and the two kinds of disagreement; the stock Surface had 94% of one kind). The stan line is the blinder one: it treats every block as full height, so a marksman never sees over a low rock that Bond sees over (the safe direction).

The duel picks the stage on its card (`SNIPER_DEFS.STAGES`: the ridge by default, then Surface): `ui.launch{stage = "levelgen", env = {GE_LEVELGEN = "ridge"}, settings = {stage = "ridge"}}`. The perches are the named pads, all of them (no stand-in filter on a built level); a marksman glints only while `combat.los` says he has his line, otherwise he waits and after the dwell moves on. Test: `BUILD=build_pc_ridge STAGE=levelgen tests/sniper/run.sh 11000 "GE_LEVELGEN=ridge GE_SNIPER_RIDGE=1"`; `tests/sniper/ridge_shots.sh` takes the pictures.

## SNIPER DUEL v2 (the SEVERNAYA SNOWFIELD)

`GE_MODE=sniper-v2 GE_STAGE=levelgen GE_LEVELGEN=snowfield` (`launchers/Play Sniper v2.bat`; a build with `-DGE_LEVELGEN_EXTRA=port/levelgen_snowfield -DGE_LEVELGEN_EXTRA_NAME=snowfield`). The mode is `tools/mode_sniper_v2.lua`, its words and numbers `tools/sniper_v2_defs.lua` (`SNIPER2_DEFS`; `SNIPER2_SETTINGS = {...}` before `include("sniper-v2")` overrides `S`). A battle from afar: the rifle and binoculars against marksmen 250 to 510 m off.

- **The field** (`tools/levelgen/build_snowfield.py`): 400 x 550 m of play on a 7.5 m heightfield, 4 x 6 bg rooms joined by tall portals (rooms behind the camera are not drawn), Surface's sky, snow, rock and skyline strips by texture number. 550 m is the most the length can be: stan corners are 16-bit. Vertices are stored relative to their room's centre. The level's matrices pass the s15.16 range (a far room or marksman relative to Bond's room, the camera matrix at the scope's zoom), so on this level the renderer takes those matrices as floats (`matrixmath.c`, `bondview2.c`, `gfx_pc.cpp`, `portLevelgenFloatCamera`) and props are drawn past the stock 320 m cut (`posIsOnScreen`). Every other level keeps the fixed-point matrices.
- **The zoom bug** of the first prototype (zoomed in and looking up, the view filled with cloud and moved the wrong way): the camera matrix (projection x view) is stored in fixed point and clamped to +-32000. At the 7-degree scope (16x) with the eye ~2000 units above the origin the translation passed the clamp and the world was drawn off the screen, only the sky left. Fixed by the float camera matrix above.
- **Fog**: a default haze in metres (`S.fog = {180, 0}`: none nearer than 180 m, thickening with distance). `sniper.fog` moves it (the storms). The near plane is 0.5 m (at 0.1 m every distance past 100 m sat within one thousandth of depth and the fog could only be flat).
- **Arrival and relocation**: a marksman appears only on a perch Bond cannot see (outside his view cone at his field of view now plus `S.view_margin`, or hidden by the drawn ground), the farthest free one first, those with a line to Bond before the rest. He leaves only while unseen (with no line to Bond for 8 s, or after his dwell and a shot).
- **The shot**: a small white glint at his scope (`sniper.point`), then one shot. A coin decides it (the round's `hit`; inside `S.close_range`, 60 m, `S.close_hit`, 95%). A quiet marker stays where he fired from and fades (`S.marker_secs`). Bond takes exactly `1 / S.hits[difficulty] + 2%` of a bar per hit (`sniper.bond_hit`): 4, 3, 2, 1 hits on Agent, Secret Agent, 00 Agent, 007. No healing, no armour, no pickups.
- **Loadout**: the sniper rifle and the camera as binoculars (`gadget.items`: no photo, a dark lens frame, `S.binoc_min_fov` 3.5 degrees against the scope's `S.scope_min_fov` 7). The setup gives no PP7. The weapon cycle skips the bare hands (`sniper.no_unarmed`): they stay in the inventory, the stock fallback when every gun is empty, so the inventory code is untouched.
- **Presentation**: plain HUD lines and a small panel, no callouts, a silent glint (`S.glint_sfx`).
- **Detection** (phase 2): each marksman fills a meter on Bond (0..1) while he has a line to him, and glints and fires only when it is full (then it drops to `S.detect_after`). A second: `S.detect_rate` x stance (`S.detect_stance`: stand 1, crouch 0.7, squat 0.5, prone 0.2) x movement (still 0.5 .. running 2) x a raised rifle scope pointed within 20 degrees of him (x2: the lens glints to him; the binoculars never do) x distance (300 m / d, 0.5..2) x close range (x6) x weather x thermal. Hidden from him, it falls at `S.detect_decay`. `DETECT foe n on <perch>: meter, rate (the parts)` every 2 s, `SPOTTED` when it fills.
- **Prone** (the use key, E: nothing to use out on the snow): the eye 135 units under standing, crawl speed x0.22, gun sway x0.25 (`sniper.prone`, hooks in `bondview2.c` and `gun.c`), 1.2 s to get up.
- **Tagging**: the binoculars held within 1.2 degrees of a marksman for 1 s pin a tag on him until he moves or dies.
- **The snowstorm** (phase 3): calm 70..110 s, the fog rolls in over 10 s to `S.storm_fog` (30 .. 170 m), holds 25..40 s, rolls out. The marksmen spot x0.3 at full storm, but past half they use thermal (x2).
- **Thermal** (Q, the next_inventory key, claimed by the mode with `sniper.claim_q`: the mouse wheel still swaps rifle and binoculars): 8 s of the world dimmed and every chr Bond has a line to drawn as a warm shape (the drawn ground hides them, the fog does not), then 25 s to recharge.
- **The flush-out**: after a round with `flush = n`, n grunts start 60 to 130 m off, below Bond and out of his sight, and rush him; the next round starts when they are down (or after 90 s). Their rifles do their own damage (`sniper.bond_hit` is off for them).
- **The ladder** (`SNIPER2_DEFS.ROUNDS`): one row a round, `snipers` (how many hunt at once: 1, 1, 2, 1, 3 by default), `hit`, `glint`, `dwell`, `flush`.

Tests: `tests/sniper/win_smoke_v2.sh <build> [frames] ["GE_X=y"]` (the win path; `"GE_SNIPER_LOSE=1 GE_CHEATS="` the loss path, `GE_DIFF=n` the difficulty; `"GE_SNIPER_CLOSE=1"` close range; `"GE_SNIPER_SHOTS=1"` pictures). The test turns Bond's view every 3 s and checks each appearance and departure against its own copy of the visibility rule (`SMOKE VIOLATION`). `tests/sniper/scope_shots.lua` (the real scope on a marksman at 150 / 250 / 350 / 510 m), `tests/sniper/zoom_probe.lua` (the zoom bug), `tests/sniper/snowfield_shots.lua` (views, rooms drawn, the drawn line to every perch).

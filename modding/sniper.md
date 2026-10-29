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

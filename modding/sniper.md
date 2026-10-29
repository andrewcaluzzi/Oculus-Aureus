# The sniper duel (prototype)

A duel on the stock **Surface** stage (Severnaya: snow, long sightlines). Bond has a sniper rifle and a PP7 and starts on high ground above the Severnaya start; five rounds against named marksmen on the far slopes, 60 to 150 m off. The mode is `tools/sniper_defs.lua` (words, the ladder, the numbers), `tools/mode_sniper.lua` (the run) and `tools/mode_sniper_menu.lua` (its card and debrief, hosted by the main menu hub). The build ships them as `Data/Scripts/modes/_sniper_defs.lua`, `sniper.lua`, `sniper-menu.lua`.

## Launching

| Way in | What it does |
| --- | --- |
| `Play Oculus Aureus.bat`, the SNIPER DUEL card, BEGIN THE DUEL | `ui.launch{mode = "sniper", stage = "surface"}`; the debrief comes back on the hub's Control stage, MAIN MENU goes back to the cards |
| `Play Sniper Duel.bat` | `GE_MODE=sniper GE_STAGE=surface GE_NOINTRO=1 GE_STAGE_AFTER=1`; when the duel ends the hub loads on the debrief |

`SNIPER_SETTINGS = { key = value }` set before `include("sniper")` overrides `SNIPER_DEFS.S` (the tests use it; so does the lobby's launch text). Best scores: `Data/Saves/ui/sniper_bests.ini` (`surface_rounds`, `_time`, `_runs`, `_wins`), their own file.

## How a round runs

- **The sight sweep** (about 5 s at stage start, behind the get-ready callout). Bond is warped onto each of his candidate perches (`SNIPER_DEFS.BOND.pads`, hand picks: ids only) and a harmless stand-in is put on every waypoint pad in the 60 to 150 m band in turn; `combat.los` says which see him. The perch seen from the most pads is Bond's; those pads are the marksmen's perches (`PERCH.ban` / `prefer` in the defs adjust them). Nothing of the map is stored.
- **The glint.** A marksman who is due picks a perch out of the middle of Bond's view and a way round from his last one. A stand-in (invincible, harmless, holding the rifle) takes it, a marker (`ar.point`, an arrow on the screen edge when it is off screen) shows over the rifle, a chime plays and the HUD says where (the Arena's styled HUD, `arena.hud(true)`: a centre callout `GLINT - 3 O'CLOCK` over the distance, the round and the marksman's name on the plate at the top left; `hud.panel` at the top right has snipers and rushers left, time, shots and hits). After the round's glint seconds (1.6 down to 1.0) the stand-in is swapped for the real marksman (`behaviour = "snipe"`), who aims for about 1.2 s and fires.
- **Relocate.** `spawn.shots` says he has fired. A second later he is removed and, after 3.5 to 7 s, takes another perch. Bond can shoot him between the swap and the shot (the real marksman is a normal chr) and for the second after it.
- **The shot.** Each perch tosses a coin (the round's `hit`, 35% to 65%): a hit marksman starts with `shotbond = 1`, so his first shot lands (the stock accuracy build-up needs about 20 shots at this range). One hit takes `hit_damage` (42%) of a health bar on any difficulty; the rifle's damage is set at stage start to give that.
- **Rushers** (rounds 3 and 5) arrive 28 to 55 m off, out of Bond's cone, a few seconds in, and run at him with SMGs.
- **The run.** A round is won when its marksmen are dead (rushers are removed); Bond is healed and re-armed. Bond dies and the run ends. The debrief shows rounds won, time, accuracy, headshots, shots, hits taken and who felled him.

## Tests

`tests/sniper/run.sh [frames] ["GE_X=y"]` (the ladder won; `"GE_SNIPER_LOSE=1 GE_CHEATS="` the loss path), `ai.sh` (what a sniper does at which range), `matrix.sh` (the sight matrix behind the hand-picked perches), `probe.sh`. The hub test (`tests/hub/run.sh`) covers the card.

## Known gaps (the full SEVERNAYA RIDGE map)

- The relocation is a vanish and a reappearance, not a walk: a "snipe" marksman's list holds position, and a scripted walk would need an AI list.
- Bond's perch has no cover and no ridge to hug: it is whatever the stock Surface geometry gives at that pad. The map in the backlog (two ridges, bunkers with slits, a trench, a bell tower) would be a generated level.
- No scope: the sniper rifle's own zoom is the stock one. A bigger zoom, a glint flare drawn at the rifle and a wind or bullet drop are engine changes.
- No spotter yet (the backlog's "spotters reveal you").

# Job 2: Le Grand Casino Janus (Monte Carlo)

The heist's second job. It runs on the same mode (`tools/mode_heist.lua`) with the same mechanics as the bank: the cover, the card
ladder, the cracker and the time lock, the power cut, the console, the waves, the gadgets and the INTEL board. Only the building,
the people, the names and the words change. They come from a **job definition** (`tools/jobs/casino.lua`) and the layout
(`tools/levelgen/casino.json`), so the mode is no longer tied to the bank.

- **Built by** `tools/levelgen/build_casino.py`, which writes `tools/levelgen/casino.json`. Generate it with
  `tools/gen_level.py ... --out port/levelgen_casino`.
- **Plans:** `designs/casino_plan_{b1,g,f1}.png` / `.svg`, `casino_plan_elevation.png`, and `casino_plan.png` (all levels).
- **Counts:** 50 rooms (limit 140), 57 doorways and 11 openings (68 portals, limit 190), 29 cameras (limit 32), 10 alarm boxes,
  18 monitors, 1 safe, 1 padlock, 1661 pads.

## 1. Why a casino

I compared three candidates on fun and feasibility:

| Candidate | Fun | Feasibility on this engine |
| --- | --- | --- |
| **Casino** | Big public rooms full of punters. Ocean's Eleven is the reference everyone knows. A cage, a count room, a vault. The "eye in the sky": catwalks behind one-way glass over the gaming floor. Ready-made gadget routes (piton to the catwalk, a vent into the count room, a lift shaft to the vault). | High. Every heist system maps onto it one to one: cards, crack, time lock, cash and gold, a security room with dozens of cameras, a public floor where the cover matters. |
| Jewellery exchange (Hatton Garden) | A tight dig-and-drill job. | Small and samey. It is mostly the bank's safe-deposit vault again. The lift-shaft route already exists in bank v3. |
| Embassy (documents) | A non-gold objective. | Needs a new objective type (documents, not loot), changes to scoring and new exit rules. That is too much new mode code for a "prove it generalises" job, so it is left as a third job for later. |

The casino wins: it needs no new mechanics, it plays differently (an open public floor, then up into the surveillance spine or down
to the vault), and it exercises every name the mode used to hard-code.

## 2. Research (short)

| Finding | Source | In the casino |
| --- | --- | --- |
| Monte-Carlo's rooms run from the entrance inward: the atrium (glass roof, café), the **Salle Renaissance** (slots, the anteroom), the **Salle Europe** (the main gaming room: roulette, punto banco, trente et quarante), then the **Salons Privés** (Salle Blanche with its terrace, the "super privés"). Jackets are required in the salons in the evening. | Monte-Carlo SBM, [Salle Renaissance](https://www.montecarlosbm.com/it/casino-monaco/casino-monte-carlo/saloni-gioco/salle-renaissance), [Salon Européen](https://www.montecarlosbm.com/fr/casino-monaco/casino-monte-carlo/salon-jeux); [Rough Guides](https://m.roughguides.com/france/the-cote-dazur/monaco/); [Visit Monaco](https://www.visitmonaco.com/en/explore/monaco-s-12-must-sees/casino-de-monte-carlo) | The same order: atrium, Renaissance (slots), Europe (tables and the pit), up the grand stair to the Salons Privés (the Salle Blanche and its sea terrace, the Salon Privé with the high-roller safe). The salons' zone is *staff* (members only): the cover drains slowly there. |
| Before cameras, casino surveillance was **catwalks above the tables**, with staff looking down through **one-way mirrors**. Cameras came in the mid-1970s, and Nevada required them by the mid-1980s. Some old casinos still have both. | [Las Vegas Advisor 2013](https://www.lasvegasadvisor.com/question/2013-03-08/), [2009](https://www.lasvegasadvisor.com/question/2009-03-09/); [Vital Vegas: a walk along a catwalk](https://vitalvegas.com/take-a-walk-along-a-las-vegas-casino-surveillance-catwalk/); [Business History Conference, 1950-1980](https://thebhc.org/node/816) | The **eye in the sky**: catwalks (west, north, east) at F1 behind openings (the glass) high in the Salle Europe's walls, joined to the surveillance room. The piton goes from the floor up to the west catwalk. |
| Regulations require dedicated camera coverage of the **cage, the soft count room and the hard count room** (walls, doors, vaults, counting surfaces). The count runs with at least three people, in full view of the cameras. Opening a box or the count room takes two people from different departments. | [Miss. Code R. 3-6.4](https://www.law.cornell.edu/regulations/mississippi/13-Miss-Code-R-SS-3-6-4); [25 CFR 542.41](https://www.law.cornell.edu/cfr/text/25/542.41); [N.J.A.C. 13:69D-1.32](https://regulations.justia.com/states/new-jersey/title-13/chapter-69d/subchapter-1/section-13-69d-1-32) | Cameras in the cage (2), the cage windows, the mantrap, the soft count and the hard count. Two counters work in the soft count. The drop cage needs the count key, which hangs in the staff lockers. |
| The main cage, main bank and count room sit behind a **double-door mantrap**: the second door is held until the first is locked, and different departments control each door (in NJ the outer door is security's, the inner one accounting's). Cage workstations have silent alarms wired to surveillance. Opening the main vault takes two people. | [Ohio Adm. Code 3772-10-16](https://codes.ohio.gov/ohio-administrative-code/rule-3772-10-16); [N.J.A.C. 13:69D-1.14](https://www.law.cornell.edu/regulations/new-jersey/N-J-A-C-13-69D-1-14); [Fla. Admin. Code 75-14.072](https://www.law.cornell.edu/regulations/florida/Fla-Admin-Code-Ann-R-75-14-072); [ACGCS: inside the cage](https://www.acgcs.org/articles/inside-the-cage-how-internal-auditors-strengthen-cash-controls-and-financial-integrity) | The **cage mantrap**: an outer door (`cage_outer`) and an inner door (`cage_inner`), both GREEN, with the mantrap guard between them. The cage has an alarm box. The vault is time-locked, and only the cracker opens it. |
| The Bellagio, June 2000: a crew vaulted the cage counter for about $160k in cash and chips. Guards chased them out, and the getaway van gave them away. | [Las Vegas Sun](https://lasvegassun.com/news/2000/jun/05/three-men-sought-in-bellagio-robbery/); [Las Vegas Sun: five memorable heists](https://lasvegassun.com/news/2012/may/21/five-memorable-las-vegas-casino-heists/) | The cage windows face the Salle Renaissance (a camera watches the queue). The counter is a parapet: you see in but cannot climb through. The loud way into the cage is the mantrap. |
| A **lift shaft** into the vault level: Hatton Garden 2015 (already used in bank v3: [bank-v3-research.md](bank-v3-research.md) [7]). | bank v3 research | The **goods lift** from the kitchen's service landing down to the vault corridor. Its gate is rusted shut to guards, and the piton goes down the shaft. |

**Design choices with no source:** the headcounts; the vault sitting under the Salons Privés; the eye in the sky as F1 rooms beside the
hall (the engine's "above" is a room on the next floor whose shared wall has openings, as with bank v3's galleries); a
gold reserve in a casino vault (a game liberty: the cash is the chip bank, the drop cage and the cage); the vent routes.

## 3. The building by level

The place is to the south, the alley to the east and the sea garden to the north.

| Level (y) | Rooms |
| --- | --- |
| **F1 (480)** | `catwalk_w`, `catwalk_n` and `catwalk_e`: the eye in the sky, with one-way glass into the Salle Europe (8 openings). `surv_corr`, `surveillance` (BLUE: the console and 12 monitors), `exec_corr` and `director` (BLUE: the casino director, RED). `salons_lobby`, `salon_prive` (the high-roller safe, GOLD), `salle_blanche`, and `terrace` (open to the sky, over the garden). |
| **G (0)** | `place` (the limo, the start), `alley`, `garden`. `atrium` (the casino doors; two bronze grilles `grille_w`/`grille_e` into the Renaissance, which drop on the alarm). `salle_renaissance` (slots; the cage windows), `salle_europe` (11 m high: 5 tables, the pit podium, and on its SW wall the pit podium's 6 floor monitors, which are the camera feeds), `bar`. `cage` (windows, counter, `cage_cash`) and `chip_bank` (cell, GREEN). `cage_mantrap` (GREEN outer, GREEN inner). Back of house: `staff_corr`, `staff_lounge`, `lockers` (the count key), `pit_office` (GREEN: the shift manager, BLUE), `kitchen` (the goods-lift landing), `north_corr_w`, `stair_foot`, `grand_stair` (up to the salons), `north_corr_e`, `staff_canteen`, `red_stairs` (RED, down). Security: `security_corr` (BLUE from the Salle Europe), `guard_room`, `armory` (BLUE), `barracks` (the squads). `dock` (exit B; the alley shutter is padlocked). `core_e_*` (the security stair core, BLUE, up to F1). |
| **B1 (-420)** | `vault_corr` (the vault patrol), `vault_ante` (the crack point), `house_vault` (time-locked: `house_vault_door`; the house reserve, 8 loot pads), `hard_count` (through the second time-locked door `count_vault_door`), `soft_count` (two counters), `drop_cage` (cell: the count key), `goods_lift` (the shaft), `plant_corr` (RED gate), `power` (the main breaker), `generator`. |

## 4. Staffing and the cards

| Role | Card | Kind | Post | How to get it |
| --- | --- | --- | --- | --- |
| Head croupier **or** floor supervisor (chosen by the sim RNG) | GREEN | civilian / guard | the Salle Europe's tables / the pit | rob her at gunpoint / KO and search him |
| Shift manager | BLUE | guard, 1.5x health | the pit office (behind GREEN) | KO and search |
| Casino director | RED | civilian | his office on F1 (behind BLUE) | at gunpoint |
| Vault manager | GOLD | guard, armoured, 2x health | inside the house vault | once it is open |
| (nobody) | count key | key on the board | the staff lockers | walk in and take it |

Also on site: croupiers and punters (8 on the floor), 2 cashiers, 3 high rollers, 2 surveillance operators, 2 counters and the
secretary. At Agent, about 30 posted guards are spread over 11 areas: the floor 22, the vault level 14, back of house 10,
security 10, the cage 7, the dock 8, the salons 8, the catwalks 5, and the executive suite, pit office and plant 2 each. Four
raisers make for the alarm boxes. The guards in the `security` area (the security corridor, guard room, armoury, surveillance and
its corridor) are "the security room": with all of them down, a camera raises nothing.

## 5. Routes

| Route | The way | What it costs |
| --- | --- | --- |
| **Front, quiet** (the inside man) | Place, atrium, Renaissance, Salle Europe. GREEN from the croupier or the supervisor; back of house through the staff door; the pit office for BLUE; the BLUE core stair to F1 (console: cameras off 90 s); the director for RED; down the RED stairs off the north corridor; crack. | Suspicion in back of house. The surveillance spine and the vault level are restricted. Many cameras. |
| **Eye in the sky** (piton) | From the Salle Europe's floor (`grapple_catwalk_low`, by the west wall) up to the west catwalk, then along the catwalks into the surveillance room from behind. | A restricted zone with a patrol. You skip the core stair's BLUE door. |
| **The goods lift** (Hatton Garden) | The kitchen's service landing (`grapple_goodslift_high`) down the shaft to the vault corridor. | Skips the RED stairs and their camera. You land beside the vault patrol. |
| **The count room vent** | The staff lounge (`vent_count_a`) to the soft count (`vent_count_b`). | Two counters and a camera inside. You arrive on the vault level without RED. |
| **The sea terrace** | The garden (`grapple_terrace_low`) up to the terrace, then `vent_salon` into the Salon Privé, where the high-roller safe is. | GOLD is still needed for the safe. A public route at night. |
| **The emergency ventilator** | The power room (`vent_vault_a`) into the house vault (`vent_vault_b`). This is the gadgets stream's rule; it only works with the power on. | The plant gate is RED. |
| **Loud** | The grilles drop on the alarm (BLUE to pass). Waves come in at the Renaissance's entry and the dock; the response comes after 120 s. Exit B (the dock) never locks down. | |

Exits: **A** the limousine on the place, **B** the loading dock. The objective is the house reserve (bars), plus the high-roller safe
(gold), the chip bank (GREEN cash), the drop cage (count-key cash) and the cage's own cash pads (placed, not yet used by the mode).

## 6. The job table: how the mode generalises

The mode used to hard-code the bank's names in four places: `mode_heist.lua` (the card carriers, the HUD and flash lines, the
console's report, the stage check), `heist_bank_map.lua` (every door, pad, room, area, camera and civilian place), `heist_intel.lua`
(the keys, the plan, the personnel, the notes, the labels, the spots) and `heist_guide.lua` (the steps). All of them now come from one
**job definition**:

- `tools/jobs/bank.lua` -> `Data/Scripts/modes/job_bank.lua`. These are the bank's values, moved verbatim, with no change in
  behaviour for the bank.
- `tools/jobs/casino.lua` -> `job_casino.lua`. The same shape with the casino's values.
- `heist_bank_map.lua` includes both. `heist_job()` returns `HEIST_JOBS[LEVEL.script.heist.job or "bank"]`, and
  `heist_bank_map(job)` builds `M` from the job's names (plus `M.JOB`).
- The mode reads `JOB = heist_job()` at load. `JOB.settings` (the casino: `area_class = {}`, so that the rooms' zone tags decide
  the cover) is applied under `HEIST_SETTINGS`. The INTEL board and the guide get `ctx.job`.

The shape is documented at the top of `tools/jobs/bank.lua`: `map` (names), `cards` (the ladder: `either`, `kind`, `pads`, `body` /
`body_setting`, `health`, `armour`, `area`), `console_roles`, `text`, `guide`, `intel`, `settings`. A third job is a new layout
whose `script.heist.job` names it, plus a new `tools/jobs/<job>.lua` and one copy line in `CMakeLists.txt`.

**Fixed semantics a job keeps:** four cards GREEN/BLUE/RED/GOLD plus a floor key (key flags 0x2/0x4/0x8/0x10/0x1); one crack door
pair and a second time-locked pair; one GOLD safe; two cells; the guard area named `security` is the camera room's staff; the power
room's object names `breaker` / `generator` and pads `breaker_front`, `power_way`. Changing those is mode work (the embassy's
documents objective would be the first).

## 7. Playing it

The levelgen slot holds one layout per build, so the casino has its own build dir. `build_win` and `build_win_next` are untouched.

```powershell
py -3 tools/levelgen/build_casino.py
py -3 tools/gen_level.py tools/levelgen/casino.json --out port/levelgen_casino
# Linux (tests):
wsl -d Ubuntu -- docker run --rm -v /mnt/e/midas-wt/job2:/home/dev/project goldeneye cmake -S /home/dev/project -B /home/dev/project/build_job2 "-DCMAKE_BUILD_TYPE=Debug" "-DGE_VIDEO=ON" "-DSDL2_LINUX_ROOT=/home/dev/sdl2-linux-i686" "-DGE_LEVELGEN_DIR=port/levelgen_casino"
# Windows (to play):
wsl -d Ubuntu -- docker run --rm -v /mnt/c/workspace/project-midas:/home/dev/project goldeneye cmake -S /home/dev/project -B /home/dev/project/build_win_casino "-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake" "-DGE_VIDEO=ON" "-DGE_LEVELGEN_DIR=port/levelgen_casino"
```

`launchers/Play Casino Job.bat` goes next to `build_win_casino\ProjectMidas.exe` (the ROM in its `Data\`). It is not in CMake's
launcher list, so bank builds do not get it. Its env line is the same as Play Heist's, because the job comes from the level:

```bat
set GE_MODE=heist& set GE_STAGE=levelgen& set GE_SKIPINTRO=1& start "" ProjectMidas.exe
```

The keys are the bank's: TAB / Back for INTEL, next and previous weapon for the gadgets, USE to search, crawl and watch the feeds
(the pit podium's monitors on the Salle Europe's south-west wall), and Shift+F3 for the AR specs.

## 8. Tests

- `tests/heist/run_job2.sh` runs `heist_job2.lua` on `build_job2`. It checks the job pick, the INTEL board's content (keys, count
  key, notes, floors), the guide, guards and civilians per area, the four carriers, the 29 cameras, the door tiers, the interfaces,
  a stick-input **walk** from the place to the vault's crack point and up the core stair to the console and the catwalks, and the
  **win path**: GREEN, BLUE, RED, the console, the crack, the reserve, GOLD and the safe, the chip bank, then out at the limo.
- The bank's suites run unchanged on `build_pc` (the same names, the same RNG draws: the job's `either` draws `math.random(2)`
  exactly as the old code did).

## 9. Known gaps

- The mantrap interlock is not enforced: it is two GREEN doors in a row, as at the bank.
- Sight does not pass through openings (the stock sight walks floor tiles), so a guard on a catwalk does not see the floor below.
  The glass reads as glass, and it is the player who looks through it.
- The cage's own cash pads (`cage_cash`) are placed but the mode does not spawn loot there yet (the bank's `extra_loot` has the same
  status).
- There is no Windows build in this pass. The commands are above.

# Bank v3: the Janus bank, redesigned from real banks

v3 keeps v2's building: it is huge, vertical and has a lot of negative space, which AC liked. Each change below comes from the research in [`bank-v3-research.md`](bank-v3-research.md); bracketed numbers like [11] point to its source list. Where a choice has no source, it says so.

- **Built by** `tools/levelgen/build_janus_bank_v3.py`, which writes `tools/levelgen/janus_bank_v3.json`. v2's `build_janus_bank.py` and `janus_bank.json` stay in the repo.
- **Plans:** `designs/janus_bank_v3_plan_*.png` and `.svg`. There is one plan per level, plus an elevation and the full set.
- **The mode:** `tools/mode_heist.lua` runs unchanged. Every name it and its tests use is kept. The heist's area and camera tables now live in the layout (`LEVEL.script.heist`), and `tools/heist_bank_map.lua` reads them when they are present.

## 1. What the research changed (the short version)

| Real-world finding | v3 |
| --- | --- |
| A banking hall is the hero space, top-lit, with service rooms round it [26][28][31]. Soane lit the Bank of England from roof lanterns because the street wall was blank for security [31]. | The hall rises **17.6 m** into a **lantern**. The roof is now three open terraces round it, and each looks down into the hall through clerestory openings. The executive corridor has a window into the lantern 15 m above the floor. |
| A stair leads from the banking hall down to the safe-deposit vaults in the basement [42]. The safe-deposit and coupon rooms sit in the basement [26]. | A public **safe-deposit department** is reached by its own stair from the west aisle. It has a reception with the attendant's grille, two **coupon booths** and the **box vault**. |
| A safe-deposit box takes two keys: the bank's guard key first, then the renter's key [19]. | The box vault's door takes **both** GREEN (the custodian's guard key) and the floor key (the renter's key). The engine's key test is `(want & held) == want`, so a door can need two keys. |
| A day gate is a mesh door inside the vault door's frame. It is weak, and Antwerp's thieves forced theirs with a crowbar [3][11]. | Behind round door A there is a vestibule (the door's depth) and a **barred day gate** (`day_gate`). Guards in the vault role may open it. |
| The time lock defeats the "kidnap the keyholder" attack [6]. In the Baker Street robbery it even hid the break-in from the police [12]. | It stays the crack: vault door A is time-locked, and only the cracker opens it (v2's rule). |
| Breaches came from below or beside: a tunnel (Baker Street, Fortaleza), a sewer (Nice), a lift shaft (Hatton Garden) [12][9][14][7]. | Three alternatives to the RED stairs: the **goods-lift shaft**, the **dig** up from the deep archive into the **inspection ring**, and the **emergency vault ventilator**. |
| An emergency vault ventilator (UL 680) exists, and it is a weak point [32][33]. | `vent_vault`: a crawl from the ring into the vault. |
| A mantrap releases one door only when the other is locked [36]. | The **staff entrance** is a mantrap: an outer door (GREEN), the guard's post, then an inner door. The vestibule's two blast shutters are the public mantrap. |
| A bullion bay with its own gate, separate from the public entrance; Northern Bank's van was let in [16]. | The **sally port**: alley shutter, sally port, inner shutter, garage. The cash office hands over into it. |
| Guard tours use watchclock stations, with a numbered key at each stop. Rounds were hourly, and a missed station is the cue [22][23]. | **Guard tours** of 36 `patrol_<route>_<n>` pads on 8 routes, with 10 **watchclock stations**. |
| Dual control: no one person reaches the cash alone [18]. | The dual-key box vault. Section 6 has more for later. |
| Officers sat in the open behind a low rail (general knowledge, **not sourced** [research 1.1]). | An **officers' platform** on the hall's north-east customer side, with a desk and a gate in its rail. |

## 2. The building by level

The street is to the south (the front), the canal to the east and the alley along the east side. Rooms whose names start with `+` are new in v3, and those starting with `~` changed.

| Level (y) | Rooms (v3) |
| --- | --- |
| **Roof / F3 (1440)** | `~roof` (west terrace, roof door BLUE from the executive corridor), `+roof_s` (over the galleries), `+roof_east` (helipad). Clerestory openings into the lantern: 2 west, 2 east, 1 south. Executive corridor `corr3` with `corr3~lantern`, the secretary, the director (safe), the boardroom, the executive lounge. |
| **F2 (960)** | As v2: the corridors with windows into the hall, the loggia, open plan W and E, accounts, IT, the **server room** (restricted), the library, comms. Trimmed: the second doors into open plan W and E, the library's east door, IT's and accounts' north doors, the IT store's back door, the F2 facade windows. |
| **F1 (480)** | As v2: the galleries round the hall, the mezzanine, loan offices, the open-plan floor, training, advisers, the IT office, archive 1. Trimmed: each gallery's second staff door, the conference rooms' back doors. |
| **G (0)** | `~hall` (17.6 m, lantern; + the officers' platform), the vestibule (the public mantrap), `~aisle_w` / `aisle_e` (3 arches each; v2 had 4), `+deposit_stair` (down to B1), the offices (GREEN), records, the north range (canteen, kitchen, manager and safe, lounge, lockers, RED stairs, dock), `~back_corr_e` (now ends at the stairs), `+staff_entry` (the mantrap from the alley, a guard post, onto the dock and the corridor), the security wing (BLUE), `~garage` (shorter), `+sally_port`. Outside: the street, the alley, the embankment steps. |
| **B1 (-420)** | The RED stairs' `b1_lobby` and the vault corridor. `ante_a` (`crack`), vault door A (round, time lock), `+vault_vest`, `+day_gate`, `~vault` (moved 2.8 m north). `+ring_w` / `+ring_n`: the **inspection ring**, which runs round the vault's west and north faces through RED `ring_gate`. `+lift_shaft`: the goods lift's foot, its gate rusted shut to the guards (`lift_gate`, guard access `never`). The deposit hall (vault door B, two cells), cash processing, the service ramp up to the dock, the counting room, the plant gate down. West, under the offices: `+sd_reception` (the attendant, the grille `sd_gate` GREEN), `+booth_1` / `+booth_2`, `~deposit_boxes` (the box vault: dual key). |
| **B2 (-840)** | As v2: the plant corridor, the **power room** (now 4 m high, not 6 m, because the ring passes over it), the generator, the **deep archive** (now the dig's start, `vent_sewer_a`), the maintenance stair, and the tunnel with its padlocked grate to the canal. |

Counts: **119 rooms** (limit 140), **186 portals** (153 doorways and 33 openings; limit 190), **31 cameras** (limit 32), 14 alarm boxes, 11 monitors, 2 safes, 2 padlocks. For portals, v2 had 182; v3 trims 13 of v2's redundant second doors and windows, and adds 17 doorways and openings.

## 3. The security envelope (layers)

| Layer | Rooms | Who belongs | Gate between layers |
| --- | --- | --- | --- |
| 0 Street | street, alley, embankment, canal | anyone | the front door (unlocked), then the vestibule's two blast shutters (the mantrap, closed by the lockdown) |
| 1 Public | hall, aisles, galleries, grand stair, mezzanine | customers | the counter and its flaps (a stan hole: full cover, the bandit barrier); staff doors behind the teller line |
| 2 Staff | offices on every floor, records, corridors, canteen, manager, dock, garage, executive floor, roof, **the safe-deposit reception and booths** | staff (the banker cover holds; suspicion builds) | GREEN doors (the offices, the deposit grille, the staff entrance's outer door); BLUE doors to the executive and security |
| 3 Restricted | the security wing, the staff entrance, the sally port, the server room, comms, the RED stairs and the whole vault level, the box vault, the plant level | security staff, the vault custodian | BLUE (security), RED (stairs, ramp, ring gate, plant gate), the dual key (box vault) |
| 4 Vault | vault_vest, vault | nobody alive: the time lock | round door A (the crack), the day gate; door B (the crack from inside) |

## 4. The vault, modelled on a real one

- **The round door** (`vault_door_a`): a 50-ton class door like the Kansas City Fed's 1921 Mosler [4] or the Winona Diebold's 24 bolts [3]. It is time-locked, and the 90 s crack is the time lock in game terms. Its depth is the 240-unit vestibule `vault_vest`.
- **The day gate** (`day_gate`): bars, a cell-door model. It is unlocked but shut. Guards in the vault role may open it; others wait outside. In reality it keeps casual visitors out while the round door stands open by day [3].
- **The vault** (2000 x 1200, 4 m high): the gold on 8 loot pads, cages, crates. Door B leads to the deposit hall, which keeps v2's loop for the mode.
- **The inspection ring** (`ring_w`, `ring_n`, 4 m wide): a corridor round the vault's west and north walls. The night guard walks it on his watchclock tour (`patrol_ring_1..6`, three of them stations) under one camera (`cam_ring`). This walkaround is our design choice; the research found no source for it. It is the classic way to make a vault's walls inspectable, and in game terms it is where a tunnel or a drill comes in.
- **The emergency ventilator** (`vent_vault_a` in the ring, `vent_vault_b` in the vault) [32][33].
- **The safe-deposit suite**: the customer's vault, apart from the money vault, as at Winnipeg [26]. A stair goes down from the hall, past the attendant's grille, the coupon booths and the box vault. The box vault's door takes the custodian's guard key (GREEN) **and** the renter's key (the floor key, still in records), as a real box lock does [19]. Loot `box_loot` (4 pads).

## 5. Staffing roster and posts

This is a large city-centre head office. The headcounts are a **design estimate** (research section 4). The minimum branch of about 5 people [39] and the dual-control rules [18] set the floor.

| Role | Count | Post (pads) | In the mode |
| --- | --- | --- | --- |
| Branch manager | 1 | his office, `mgr_desk` / `mgr_1` | RED carrier, civilian |
| Officers (loans, accounts) | 2 | the officers' platform `officer_desk`, `platform_1`; loan offices F1 | 1 civilian on the platform (new), F1 civilians |
| Head teller and tellers | 4 | `teller_1-4`, `teller_mid`, `teller_back` | GREEN carrier among them, civilians |
| Safe-deposit attendant (vault custodian's deputy) | 1 | `sd_attendant` or a booth | civilian (new) |
| Cash counters | 2 | `count_1`, `count_2` | civilians (new): dual counting [40] |
| Vault custodian | 1 | `vault_captain` | GOLD carrier |
| Fixed-post guards | per area | the named posts first (`hall_post_*`, `entry_post`, `sec_*`, `ante_*`, `patrol_ring_2` ...) | the mode's posted guards (`share`) |
| Roving guards | 1 per tour | `patrol_<route>_<n>` | pads ready; not walked yet (section 8) |
| CCTV operator / security room | 2-3 | `sec_mid`, `sec_1`, `sec_2`, `console` | the security room's staff (the camera link) |
| Staff-entrance guard | 1 | `entry_post` | a security post (new) |
| Armoured-car crew (visiting) | 2-3 | the sally port `sally_mid`, `garage_4` | dock posts |
| Reinforcements | squads | the barracks | v2 (unchanged) |

The guard shares (Agent 30 posted, 007 89; the mode's rule) are spread over 15 areas. v2's 14 remain with their shares. The vault level gains the ring post. Security has 8 shares (the staff entrance). The new **safe_deposit** area has 3, 1 of them a raiser at `alarm_sd`.

## 6. Opening procedure as gameplay

| Real procedure | In v3 now | Next (not built) |
| --- | --- | --- |
| Dual control at the box vault: guard key, then renter's key [19] | The box vault door needs GREEN **and** the floor key | |
| The time lock, set at closing [6][40] | The crack (90 s) at round door A | Show "TIME LOCK: opens 08:30" on the door, with a crack time that depends on the mission clock |
| Day gate behind the round door [3] | A barred gate, opened only by the vault role | A crowbar or noise interaction |
| Mantrap: one door at a time [36] | Two doors either side of a guard post | Interlock logic: the inner door locked while the outer one is open |
| Guard tour and watchclock [22] | 8 routes, 10 stations | A missed station sends a supervisor to look (ai stream) |
| All-clear signal at opening [18] | | A morning "opening" variant where the crew rides in with the first keyholder |
| Hostage keyholder (Northern Bank) [16] | (the banker cover) | Force the manager to the RED door |

## 7. Heist approaches (grounded in real cases)

| Approach | Real case | The route in v3 | What it costs |
| --- | --- | --- | --- |
| **Front door, loud** | Knightsbridge 1987 (customers who drew guns) [17] | van, front door, vestibule, hall, flaps, BLUE, RED stairs, the crack | the hall's cameras and 14 guards; the lockdown shuts the blast shutters |
| **Inside man** | Northern Bank 2004, Antwerp's rapport with the guards [16][11] | the banker cover: staff areas are allowed, so take GREEN from a teller, walk the staff corridors, and take BLUE and RED from their carriers | suspicion in staff rooms; restricted rooms blow the cover |
| **Roof and lantern** | Soane's lantern; the rappel | the facade grapple to the F1 window, or the stair core to the roof, then **`grapple_lantern_*`** 15 m down into the hall | the roof camera; you land in the public hall |
| **Lift shaft** | Hatton Garden 2015 [7] | the rear corridor, the landing door, **`grapple_liftshaft_*`** down the shaft, `lift_gate`, the vault corridor (skips the RED stairs and their camera) | the vault corridor's camera and patrol |
| **Tunnel and sewer** | Nice 1976, Fortaleza 2005, Baker Street 1971 [14][9][12] | the canal, the padlocked grate, the tunnel, the plant level, the deep archive, **`vent_sewer`** up through the ring's floor, the ring, then `vent_vault` (the ventilator) **or** out of the ring to the antechamber | the ring patrol and its camera; the power room is next door (cut the power first) |
| **Cash in transit** | Northern Bank's bullion bay [16] | the alley, the sally port (BLUE), the cash office, or the service ramp's RED shutter | the sally port camera, the garage guards |
| **Customer's vault** | Baker Street and Antwerp went for the boxes, not the main safe [12][11] | down the safe-deposit stair (GREEN grille), then the dual-key box vault | 2 keys; a staff-zone camera |
| **Vents** | | `vent_records` (records to the manager's office), `vent_lounge` (staff lounge to the security corridor), `vent_server` (IT to the server room), `vent_plant` (plant corridor to the counting room) | the gadgets stream's rules |

## 8. Interfaces (the fixed pad names)

| Kind | Names (`LEVEL.pads`) | Also in `heist_bank_map()` |
| --- | --- | --- |
| Grapple points | `grapple_<id>_low` (where you stand) / `_high` (where you end up), for `lantern_w`, `lantern_e` (the hall floor and the roof terraces at the clerestory), `gallery` (the hall floor and the west gallery's balcony), `facade` (the street and the F1 facade window), `liftshaft` (the shaft floor on B1 and the rear corridor's landing on G). For a descent, start at `_high`. | `M.GRAPPLES[id] = { low, high }` |
| Vents | `vent_<id>_a` / `_b`: `records`, `lounge`, `server`, `vault` (the emergency ventilator), `sewer` (the dig), `plant` | `M.VENTS[id] = { a, b }` |
| Guard tours | `patrol_<route>_<n>`, a loop in walking order: `hall` 6, `gallery` 5, `exec` 3, `roof` 5, `perim` 4, `b1` 4, `ring` 6, `plant` 3 | `M.PATROLS[route] = { pad ids }`, `M.STATIONS` (the 10 watchclock stations) |
| Heist tables | `LEVEL.script.heist`: `areas`, `cams`, `guard_areas`, `area_label`, `civ_extra`, `patrols`, `stations`, `vents`, `grapples` | `heist_bank_map()` uses them over its v2 tables |

`tools/gen_level.py` now passes a layout's `script` key through as `LEVEL.script`. This is a generic JSON-to-Lua passthrough.

## 9. Limits and honest notes

- **Balance question for AC:** `vent_vault` lets a crew into the vault without the crack. Whether the gadgets stream allows it (one person, no gold back out?) is a rule to decide. The pads are there either way.
- **Sight:** guards and cameras still do not see through openings (stock sight walks the floor tiles). A guard on the roof does not see the hall below.
- **The officers' rail** is a stan hole, so it blocks sight like the counter does.
- The mantrap and sally-port interlocks are not enforced. They are two doors in a row.
- The power room is 4 m high now (v2: 6 m) because the inspection ring runs over its east end.
- No source was found for the inspection ring, the officers' platform or the headcounts. Those are design choices, labelled as such.

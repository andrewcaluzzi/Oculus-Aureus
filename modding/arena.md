# The Colosseum arena

A wave-survival mode in a generated gladiator pit. The map is a C data set built by `tools/gen_colosseum.py`; the mode is one Lua file, `tools/mode_arena.lua` (the build ships it as `Data/Scripts/modes/arena.lua`). Everything about balance is in the settings block at the top of that file.

## Launching

| Launcher | What it does |
| --- | --- |
| `Play Arena.bat` | `GE_MODE=arena-menu`: the arena's start menu (see Start menu and post-game). |
| `Play Arena (Endless).bat` | `GE_MODE=arena-endless`: waves keep coming and keep getting harder; your wave, kills and time are logged when you fall (`ge: arena: OVER wave ...`). |
| `Play Arena (Chaos).bat` | `GE_MODE=arena-chaos`: 20 waves with wave mutators and affix elites on (see Wave mutators, Affix elites). |
| `Play Arena (Chaos Endless).bat` | `GE_MODE=arena-chaos-endless`: endless, mutators and elites on. |
| `Play Arena (Co-op).bat` | `GE_COOP=1 GE_MODE=arena-coop`: two Bonds, you and the arena bot (see Co-op). |
| `Play Arena (Co-op Chaos).bat` | `GE_COOP=1 GE_MODE=arena-coop-chaos`: co-op with mutators and elites. |
| `Play Arena (Test Keys).bat` | `GE_MODE=arena` with the wave-jump test keys (below) and `GE_CHEATS=invuln,guns`. |

All set `GE_STAGE=colosseum GE_SKIPINTRO=1` too. The preset launchers skip the start menu and start their preset at once; when the run ends they come back to the post-game like a run started from the menu (Retry starts the same preset again; New assignment opens the menu on that preset). `GE_MODE=arena` on its own is the classic 20 waves.

## Start menu and post-game

Launching `Play Arena.bat` loads the Colosseum as a LOBBY (mode `arena-menu`, `tools/mode_arena_menu.lua`): the crowd fills the stands, the camera turns slowly from the middle of the pit, nobody fights, and the menu is drawn over it. Everything is Bond-themed (MI6 briefing, Q Branch, the debrief); every word is in the shared theme table, `ARENA_THEME.menu` in `tools/arena_theme.lua` (the medals are its `medals`), except the settings rows' own labels and help lines, which sit beside their values in `tools/arena_menu_defs.lua`.

- **The briefing (main page):** seven operations (presets) with their best wave or fastest clear and medal at the current difficulty (`arena_scores.ini`, solo only): OPERATION COLOSSEUM (20 waves), TOMORROW NEVER DIES (endless), SPECTRE PROTOCOL (20 waves with mutators and affix elites), CASINO ROYALE (endless chaos), QUANTUM OF SOLACE (10 waves, a boss every 3), DOUBLE-O SECTION (co-op with the arena bot), DOUBLE-O VS SPECTRE (co-op chaos). The difficulty (TRAINEE, FIELD AGENT = the arena as built, DOUBLE-O, SUICIDE MISSION), Q BRANCH (every setting), RETURN TO MI6 (the title menus) and BEGIN MISSION. The mission dossier on the right sums up the settings and the bests.
- **Q Branch (advanced page):** pacing (10 / 20 / 30 waves, endless), boss waves, difficulty, get ready, breather; wave mutators, affix elites, chaos intensity (mild, normal, wild, max), whacky effects; players (solo, a 00 partner played by the arena bot), the co-op downed rule; enemies at once, knife throwers' distance, bodies; loadout (scavenger, supplied, stocked), healing (per kill, gifts, both), centre drops; crowd size, back rows (impostors), crowd favour, crowd screams; pickup markers, dropped-gun dots. Changing a scored setting turns the operation into an UNSANCTIONED OPERATION with its own best scores (`custom_<pacing>`). Each row's effect on `ARENA_SETTINGS` is `ARENA_MENU_DEFS.settings` in `tools/arena_menu_defs.lua`.
- **The debrief (post-game):** MISSION COMPLETE or AGENT DOWN, the wave, the medal and any new best, then wave reached, confirmed kills, mission time, accuracy, headshots, best multi-kill, best streak, the crowd's peak favour, hits taken (and elites and mutators in chaos), and kills per wave as bars (gold a boss wave, purple a mutator wave, red the wave you fell in). AGAIN, 007? starts the same run again (same mode, settings and players), NEW ASSIGNMENT goes back to the briefing with the run's settings, RETURN TO MI6 goes to the title menus.
- **Controls:** up / down (stick, D-pad, W / S, the wheel) move, left / right change a setting (and jump between the lists and BEGIN MISSION), fire, A or Enter select, B, Escape or the use key go back. The mouse points (the highlight follows it) and clicks; a right click goes back or steps a setting backwards.
- **Remembered:** the last choices are in `Data/Saves/arena_menu.ini` (one `setting=CHOICE` line each, plus `preset=`), written when a run starts. Delete it for the defaults. `GE_ARENAMENU_FILE=<path>` uses another file, `GE_NOMENUSAVE=1` never writes it.
- **How it works:** a run is a fresh load of the Colosseum, exactly as a launcher starts one. BEGIN MISSION and AGAIN, 007? end the lobby stage and hand the next stage its mode, `ARENA_SETTINGS` (as a Lua table constructor), co-op and chr slots through `port/src/port_arenamenu.c`; `front.c` sees the hand-off where the stock mission report would open and loads the Colosseum again instead. In the run, `tools/arena_flow.lua` (included at the top of `mode_arena.lua`) applies those settings, keeps the stats, and arms the debrief: when the run ends (death, the win after 4 s of cheering, or an abort from the watch, which goes back to the briefing) the lobby loads again. The menu itself is sim state (the crew's synchronized buttons through `plan.hold` / `plan.input`, a `ui_tick` event every game tick); the drawing (`port/src/port_arenamenu_draw.c`, on the shared HUD helpers `port_hudstyle.c`) is presentation.
- **Off:** in netplay (the host's menus choose the next stage there; the menu flow is solo and local co-op only), in the benchmark presets, in automated runs (`GE_FRAMES`, `GE_PLAYERBOT`, `GE_INPUTPLAY`) unless `GE_ARENA_MENU=1`, and with `ARENA_SETTINGS.postgame = false`. Then a run ends on the stock report as before.
- **For mutator scripts:** the WHACKY EFFECTS row sets `ARENA_SETTINGS.whacky` = `false` (off) or an intensity `1` (mild), `2` (normal), `3` (wild).
- **Test:** `tests/arena_menu/run.sh` (the menu, a win, Retry, a fall, New assignment, Quit; 13 checks), `shots.sh` (its screenshots to JPG), `oracle.sh` (dam1 / facility1). To make your own variant, copy `Data/Scripts/modes/arena-endless.lua` (three lines: it sets `ARENA_SETTINGS`, then `include("arena")`) and run it with `GE_MODE=<your file name>`. For example `ARENA_SETTINGS = { waves = 10, boss_every = 3, crowd = 30 }`. Add `GE_CHEATS=invuln` to watch without dying.

## The map

An octagonal pit 9000 units across with a 350-unit wall, four gates (E, N, W, S) and 12 tiers of stands (up to about 3000 units high). Four pillars stand in front of the gates and four low walls form a pinwheel in the diagonals; enemies come in through the gates. 256 spawn pads are on a 500-unit grid, and 1200 seat pads on the stands hold the crowd. The level takes over the cut multiplayer stage slot `LEVELID_EAR` (stage name `colosseum` or `ear`), borrows the Surface text bank and uses a fogless sky. It draws with two stock Surface textures tinted per vertex, so close up it looks soft. The generator constants (`HALF`, `CUT`, `TIERS`, `PILLARS`, `LOWWALLS`, colours) are at the top of `tools/gen_colosseum.py`; `python3 tools/gen_colosseum.py` rewrites `port/colosseum/`.

## How a wave runs

GET READY (`start_delay`), then wave 1. Enemies pour out of the gates two per quarter second, up to `max_alive` at once (the rest of the wave trickles in as they die). A wave is cleared when the queue is empty and none are alive. Then comes a breather (`breather` seconds) and the next wave. Every body stays (see Big crowds and keeping every body). The HUD shows `WAVE 4/20  THE KNIFE SISTERS` (plus `+ <MUTATOR>`) on line 1, kills / enemies left / time (plus `ELITES n`) on line 2, announcements on line 3 and the crowd favour on line 4. In fixed mode the last wave ends with `mission.win`; if Bond dies, the stock mission-failed screen takes over.

Enemies fight from range, not just in melee: they open fire as soon as they can see you within their weapon's effective range (rifles 6000 units, SMGs and shotguns 3500, pistols 2500) and run straight at you across the open floor when out of range. Knife throwers keep their distance (`knife_keep`, `knife_keep_dist` 3300): while they see you 28-38 m away they stand, kneel, sidestep and throw; closer than 28 m they run back to a pit pad that still sees you (AI list `m_ArenaKnifeKeep`); `knife_keep = false` or `GE_ARENA_OLDAI=1` gives the old closing runs.

### Wave types

Ordinary waves cycle through these seven in order (wave 1 is always soldiers). A repeated type gets a numeral: THE MARKSMEN II, THE AMBUSH III.

| Wave | What you get |
| --- | --- |
| The Soldiers | Ten soldier skins with AK47, D5K, AR33 or ZMG, chasing; accuracy rises with the wave. |
| The Knife Sisters | Natalya, Xenia, Mayday, Rosika, Sally, Mandy and Vivien with PP7s. Every shot while attacking becomes a thrown knife, from up to 4500 units, with an arc that is solved properly at range. From wave 14 this becomes **The Turbo Knife Sisters**: everyone animated at 1.6 to 1.8 times speed. |
| The Horde | Eight or more weak, fast runners (35% health): civilians, scientists, terrorists, bikers with Klobbs, PP7s and ZMGs. |
| The Ambush | Rushers from the two gates farthest from you. |
| The Marksmen | Two to six snipers parked in the gate mouths, plus a few soldiers. |
| Wild Card | Any of the 70 skins, any of 16 weapons, 0.6 to 3 times health, random accuracy and speed, one in five throws knives. |
| Iron Jaws | Jaws with an RC-P90, seven times health (more by wave), plus escorts. |

### Bosses

Every `boss_every` waves (5) a boss wave replaces the ordinary one. The four bosses cycle, each with a few escorts (3 to 6 elite troops):

1. **The Baron**: Baron Samedi with the Golden Gun (ten times base health).
2. **The Traitor**: Trevelyan with a rocket launcher (12 times).
3. **Iron Jaws**: Jaws as a mega-tank (26 times).
4. **The Triumvirate**: all three at once.

Boss health is multiplied by `boss_hp_mult` (2.5) and given armour worth `boss_armour` (0.5) of their health on top, and each later cycle is `boss_cycle_mult` (0.35) tougher.

### Tiers, elites, turbo

- **Tiers.** Every enemy rolls a toughness tier from a table that shifts with the wave: light (0.6 times health), normal, armoured (1 times health plus armour, about 2.5 times as tough) and heavy (1.5 times health plus lots of armour, about 5 times). Armoured enemies are not staggered by hits.
- **Elites** from wave `elite_from` (8): the black Spetsnaz and Arctic Commandos from the Bunker 2 alarm and the Caverns' Janus Special Forces, always armoured or heavy, a growing share of soldiers and ambushers.
- **Turbo** from wave `turbo_from` (6): a growing share of enemies move, aim and throw at 1.25 to 1.6 times animation speed.

## The economy

- **No free guns.** Bond re-arms from the weapons enemies drop. (`give_guns = true` brings back a rotating gun gift each breather; `give_ammo = true` tops up the ammo of the guns he carries.) Loose guns on the floor are capped by `max_drops`.
- **Centre ammo box.** Every `ammo_every` seconds (40) one real MP ammo box with PP7, KF7 and shotgun rounds (`ammo_rounds`) appears on a random pad near the middle of the pit. At most `ammo_max_center` (3) lie uncollected (a new one replaces the oldest), and each disappears after `ammo_ttl` (90). It is an ammo box, not a gun: it never rotates through weapons. Note that solo play doubles box ammo.
- **Centre armour.** One body-armour pickup after every boss wave, and with `armour_chance` (0.25) after other waves, gone after `armour_ttl` (120 s).
- **Health per kill.** No health between waves; each kill heals the Bond who got it, by a fraction of a full bar set per tier in `health_per_kill` (3% light and normal, 4% armoured, 6% heavy, 25% for a boss).
- **The crowd.** `crowd` (150) harmless, invincible spectators fill the front rows during GET READY, with impostors behind them (see The impostor crowd). They fidget on their own timers, look startled when a boss arrives, and cheer and boo what happens in the pit (see Crowd reactions). Each costs a little frame time; lower `crowd` on a weaker machine. Zero removes them. A spectator is pinned to its seat (no movement collision test: about 80% of the crowd's sim cost was that test for chrs that never move; `GE_ARENA_CROWDCOLLIDE=1` restores it), and spectators added later never share a spot (`seat_gap`, 240 units).

## Pickup markers

`pickup_markers` (true): the good pickups (centre ammo box yellow, armour cyan, crowd and sponsor gifts pink, a gun placed by `spawn.item` orange) get a small diamond and a short label (`AMMO 12m`); dropped guns within `marker_drop_near` (10 m) get a faint dot; thrown knives nothing. A marker fades out `marker_fade` (8) seconds after its pickup appeared and comes back within `marker_near` (15 m). Markers close together on screen merge (`AMMO x3 12m`); at most `marker_max` (10) groups are drawn, good stuff and nearest first; off-screen good stuff gets a diamond on the screen edge. Presentation only: the sim publishes the live items (`port_arenamode.c`) and the console draws them (`port_console.c`); nothing in the sim or the netplay hash reads them. Settings live in `_arena_polish.lua` (`marker_fade`, `marker_near`, `marker_drops` "near" / false / "always", `marker_knives`, `marker_drop_near`, `marker_merge`, `marker_edge`, `marker_max`, `marker_style` "v2" or "a7" for the old beacons); `GE_MARKERLOG=1` logs the counts once a second.

## Crowd favour

A meter from 0 to 100 (start 50) on HUD line 4, with tiers HATED (0-19), RESTLESS, WATCHING, ROARING, ADORED (80-100). It drifts back to 50 at 0.25 a second, but never up while HATED. The module is `_arena_crowd.lua` (`tools/mode_arena_crowd.lua`); the C side (`port/src/port_arenacrowd.c`) records where each shot hit, with what and from how far, and who owns each explosion.

- **Gains, per kill:** headshot 3 (+1 per 15 m, at most +3); long shot 3 (30 m+); multi-kill (a 2nd kill within 2 s 3, then +2 each); streak (kills without being hurt, from the 3rd); close call 5 (under 20% health); bare hands or knife 5; THEIR OWN GUN 1 (the killing gun is the very gun model the victim carried, in either hand, even after it was knocked out of its hand; not merely the same ammo, and not Bond's start kit or a gift); BOOM (one blast, 2+ kills); DISARMED 4; REVENGE 3; GIANT SLAYER 10 (a boss), ELITE SLAIN 4, +6 WITH A PISTOL; CLEAN WAVE 6 (nobody hurt, at least one kill).
- **Losses:** HIT -1 (at most one per 0.4 s); CAMPING -2 (within 400 units for 10 s while enemies are alive, then every 5 s); SPRAY AND PRAY -3 (30 shots without a hit); SHOT THE CROWD -8 (and the spectators nearby panic).
- **Effects:** ROARING and up: a gift lands near a Bond every 25 s (a small ammo box or armour; real pickups). ADORED: a sponsor gift in the centre (Golden Gun, rockets, full armour, or 15 s of double damage; never while a mutator runs). HATED, once a wave: an extra pack of rushers now, or a shortened breather and a bigger next wave (`hated_penalty` "pack", "elite" or "off"). The emperor's thumb at the last boss kill of a boss wave: favour 60+ heals every Bond and gives a gift, under 40 skips the breather.
- **Presentation:** reactions ripple out from the stand nearest the action; the tier changes the spectators' fidgets; a few stock sounds by id (`arena.sfx`). This runs next to the cheers and boos below (Crowd reactions): both set animations on the same spectator records, so a later one replaces an earlier one, and the WAV / scream sounds are untouched.
- **Settings:** `crowd` may be a table: `crowd = { count = 150, favour = true, points = {...}, drift = ..., hated_penalty = "pack", ... }` (partial tables merge; every default is `ARENA_CROWD.DEFAULTS` at the top of the module). Off: `crowd = { favour = false }` or `crowd_favour = false`. One shared favour in co-op and netplay; it is sim state (Lua heap, the session RNG, real pickups).
- **Disarm:** a shot to a held gun knocks it out (`spawn.disarm`, the stock game only chips it) and the enemy goes for you with its fists; `disarm_drop = false` keeps only the points.

## The HUD and the callouts

`hud_style` (true): the arena draws its own HUD (`port/src/port_arenahud.c`, with the shared helpers in `port/src/port_hudstyle.c`; stock fonts only) in place of the plain mode rows. The wave panel sits in the top-left corner (`hud_scale`, 0.5 of the console font by default; `hud_anchor` "top_left" or "top_right"; `hud_margin` units from the edges): WAVE and the number big (it pops and the frame flashes when the wave changes), the wave's name and phase, the mutator as a purple tag, KILLS / LEFT / time / ELITES, the crowd favour as a bar in the tier's colour with the last toast, and this machine's message line as a strip below (co-op DOWN / REVIVING text included). At the upper centre, the callouts: kill chains (TWIN STRIKE, TRIPLE CROWN, QUAD FURY, BLOODBATH, MASSACRE, CARNAGE xN) and streaks without being hurt (THE CROWD STIRS at 5, CHAMPION OF THE PIT 10, BLOODED 15, UNBROKEN 20, IMMORTAL 25, LEGEND OF THE ARENA 30 and every 5 after), STREAK BROKEN AT n: big outlined text that pops in, pulses and fades, bigger and hotter with the tier; the newest replaces the one showing. Crowd settings `callouts`, `chain_names`, `chain_more`, `streak_names`, `streak_broken`. The mode still writes the HUD rows (tests read them); `hud_style = false` shows them as before.

## Wave mutators

`mutators` (false; on in the chaos presets): from wave `mutator_from` (3) a wave may roll one mutator (`mutator_chance` 0.30, +0.02 a wave, at most 0.60; bosses only with `mutator_bosses`). It ends with its wave and the module checks that every changed setting is back (log `reset ok`). KNIVES ONLY (every enemy throws knives and fires no bullet), PAINTBALL, ONE-HIT KILLS, TURBO, LOW VISIBILITY (Jungle-style fog, full at 2600 units), BIG HEADS, IRON SKIN (enemy health 200), BOTTOMLESS CLIPS (infinite ammo, half as many enemies again), THUNDERBALL (rain: everyone slips, see below). `mutator_list`, `mutator_pick` ("random" or "cycle"), `mutator_fog_far`. Module `_arena_mutators.lua`; C side `port/src/port_arenafx.c`, `port/src/port_arenarain.c`.

THUNDERBALL (`rain`; AC: "everyone slips") rolls in and out over 1.5 s. Bond's walk (and the co-op partner's) follows the stick with a time constant (`rain_slip`, 0.25 s at full rain), so he overshoots, stops late and swings wide in turns; a wall stops the slide. Landed grenades slide further (`rain_grip` 0.5 on the slide friction; 1 = dry). The wave's enemies move and act at `rain_enemy_speed` (0.85) of their animation speed, and one who turns hard or stops short while running may slip (`rain_stumble` 0.35 a time, at most once in 3 s; a stock leg-hit or butt-hit reaction; the mode's seeded RNG, so netplay agrees). The sky goes overcast, the picture dims, slanted streaks fall around the camera and rings splash on the sand (`port/src/port_arenarain_draw.c`, presentation), the rain patters (quiet stock water splashes; there is no rain sound in the game) and the crowd sulks (`rain_mood` -8, a boo). Enemies do not truly slide (their movement is the animation's own); they stumble. Tests: `tests/arena_a8/rain.sh` (Bond's coast after the stick is let go, dry vs wet; slips; screenshots), `rain-hash.sh` (two local co-op runs, the same state hash), `rain-net.lua` (netplay, rain every wave).

## Affix elites

`elites` (false; on in the chaos presets): from wave `affix_from` (3) an ordinary enemy (never a boss) may roll 1 or 2 affixes (`affix_chance` 0.04 +0.012 a wave, at most 0.25): armoured, swift, knives, explosive (its corpse blows up after 0.7 s), regen (5% a second), marksman. Elites have 1.3x health and armour, are drawn 1.15x bigger, are counted on HUD line 2 and announced when they die. Module `_arena_elites.lua`.

## Whacky

`GE_MODE=arena-whacky` (Windows: `Play Arena (Whacky).bat`; `Play Arena (Whacky Test Keys).bat` adds invincibility, all guns and the keys below). Nearly every wave from wave 2 rolls a mutator (`mutator_chance` 0.70 +0.03 a wave, at most 0.95, bosses too) from the Whacky set and the A7 set; affix elites glow in their affix's colour (armoured steel, swift yellow, knives green, explosive orange, regen pink, marksman red; two affixes pulse between both), Baron Samedi comes in gold, and after every boss wave the crowd throws Bond the NICK NACK LAUNCHER. The names are Bond-themed and live in `ARENA_THEME.whacky` (`tools/arena_theme.lua`):

| Key | Shown as | What it does |
|---|---|---|
| ghost | SPECTRE | enemies are translucent ghosts that flicker, now and then nearly invisible |
| chameleon | ADAPTIVE CAMOUFLAGE | every enemy cycles through the colours |
| shadow | NIGHTFIRE | near-black enemies, a little faster |
| glass | ICE PALACE | glassy, shimmering; one shot kills them, they do 2.5x damage |
| giants | TINY BOND | the Tiny Bond cheat, and the enemies are 1.3x bigger |
| fastfwd / slowmo | TURBO MODE / SLOW ANIMATION | the Fast / Slow Animation cheats (1.9x / 0.5x; slow-mo brings 40% more enemies) |
| rockets | LITTLE NELLIE | every third enemy carries a rocket launcher (low accuracy) |
| double | DOUBLE AGENT | Bond gets a 2x cheat pair (rockets, grenade launchers, RC-P90s or lasers); 50% more enemies |
| glitch | GOLDENEYE EMP | Line Mode bursts of 2 s every 6-12 s; the enemies' colours race |
| swap | Q'S WRISTWATCH | every 10 s the speed flips between 1.9x and 0.5x |
| crowd | Q-BRANCH AIRDROP | every 14 s the crowd throws a gift near Bond (a gun, armour, or the launcher) |
| minions | NICK NACK LAUNCHER | Bond gets a rocket launcher whose rockets each hatch 3 tiny turbo knife throwers on his side |
| mayhem | NICK NACK'S REVENGE | hostile tiny knife throwers burst in near Bond every 9 s |

**The NICK NACK LAUNCHER** (a Q-branch prototype): while armed (`whacky.launcher`), each of Bond's rockets bursts where it explodes into 3 chrs at 0.25 scale (collision box scaled too) running at 3x animation speed, which hunt the nearest enemy in sight and throw real knives. They are invincible for 2.5 s while they hatch from the blast, have 0.3 of a guard's health, last 15 s and then fade out; at most 12 live (a new one fades the oldest). They are never arena enemies: they do not count towards the wave or the kills, but a wave enemy they kill counts as a kill, with a NICK NACK STRIKES callout. Settings: `minion_shots` (10), `minion_count` (3), `minion_scale` (0.25), `minion_life` (15), `minion_health` (0.3), `minion_cap` (12), `minion_hostile` (false), `whacky_fx`, `whacky_reward`, `crowd_throw_every` (14).

Test keys (`test_keys` or `GE_ARENA_TESTKEYS=1`; solo only): F5 / F6 the next / previous effect NOW on the live enemies, F7 effect off, F8 the launcher with 20 rockets. Module `_arena_whacky.lua`; C side `port/src/port_chrfx.c` (presentation) and `port/src/port_whacky.c` (sim). Tests: `tests/arena_whacky/` (`run.sh`, `whacky-fx.lua` one effect per run with `GE_WFX_<NAME>=1`, `whacky-minion.lua` fire / hatch / kill / despawn, `oracle.sh`).

## Best scores and medals

Solo only: `arena_scores.ini` next to `midas.ini` keeps, per pacing (`endless_` or `fixed<waves>_`), the best wave, kills, time (endless: the longest; fixed: the fastest clear), medal, runs and clears. Medals: endless by the wave reached (5 / 10 / 20 / 30 = BRONZE / SILVER / GOLD / PLATINUM); fixed by the waves cleared (a quarter, half, all; all within 75 s a wave = PLATINUM). GET READY shows the best; the end HUD and the report page show the medal and any new best. `scores = false` turns it off; `GE_SCOREFILE=<path>` and `GE_NOSCORESAVE=1` are for tests. Module `_arena_scores.lua`, store `port/src/port_arenascores.c` (Lua has no io). The report-page lines (`arena.debrief`) are printable ASCII only, and a run of more than 29 characters without a space is broken (the stock word wrap has a 32-byte word buffer).

## Co-op

`GE_COOP=1` with `arena-coop` (or `ARENA_SETTINGS.coop = true`): two Bonds. Locally the second one is the arena bot: the co-op puppet Bond driven through the same pad and look input a person uses (`port/src/port_arenacoop.c`), so it collides, autoaims, reloads, cycles weapons and walks over pickups like a player. It targets the nearest visible enemy, strafes, loots better guns, fetches the centre ammo and armour when low, falls back when hurt, and revives you. In netplay the second Bond is a person and the bot is never used.

- **Down and revive:** a Bond whose health runs out goes DOWN while another is up (kneeling, no damage, the enemies ignore him). A partner standing within `coop_revive_radius` (170) for `coop_revive` (3 s) brings him back with `coop_revive_health` (0.35) and `coop_revive_grace` (2 s). After `coop_bleed` (30 s) he bleeds out: `coop_bleedout` "wave" (back at the start when the wave is cleared), "run" or "end". The last Bond standing dies for real, and the run ends.
- **Waves for two:** `coop_count_mult` 1.5 (bosses are not doubled), `coop_alive_mult` 1.35, `coop_hp_mult` 1.2, `coop_boss_hp_mult` 1.5.
- The rules are sim state (hashed for netplay only while on); the Colosseum gets 40 more spare object model slots so the centre pickups still fit with two Bonds' guns.

## Settings

Set them by editing table `S` at the top of `arena.lua`, or in a preset file with `ARENA_SETTINGS = {...}` before `include("arena")`. The header comment of `tools/mode_arena.lua` has the authoritative list; the main ones:

| Setting | Default | Meaning |
| --- | --- | --- |
| `mode` | `"fixed"` | `"fixed"` (win after `waves`) or `"endless"` |
| `waves` / `boss_every` | 20 / 5 | last wave in fixed mode / a boss wave every N (0 = none) |
| `breather` / `start_delay` | 12 / 6 | seconds between waves / before wave 1 |
| `max_alive`, `max_alive_every`, `max_alive_cap` | 12, 6, 18 | enemies alive at once, growing by 1 every 6 waves up to the cap |
| `hp_growth` / `acc_growth` | 0.10 / 2.5 | enemy health and armour grow by this fraction per wave / accuracy points gained per wave |
| `crowd` (or `crowd_real`) | 150 | real spectators, front rows first |
| `crowd_impostors` | 1000 | impostor spectators on the spare seats (PC; see below) |
| `crowd_lod` | 2500 | (PC, presentation) real spectators farther than this from the camera are drawn as impostors of their own skin and pose; 0 = always models; `GE_CROWD_LOD` overrides |
| `chr_batch` | true | (PC, presentation) spectators', corpses' and fighters' opaque triangles drawn once per draw state, not a draw per limb; `GE_CHR_BATCH=0/1` |
| `corpse_bake` | true | (PC, presentation) a body still for 1.5 s is captured once by the renderer and redrawn from that (not in the arena fog, not Whacky-tinted); `GE_CORPSE_BAKE=0/1` |
| `reactions`, `REACT`, `multikill_n`, `multikill_window`, `big_hit`, `low_health`, `react_gap`, `crowd_volume` | true, see below | the crowd's cheers and boos (see Crowd reactions) |
| `crowd_screams`, `scream_volume`, `SCREAMS` | false, 0.8, see below | the crowd screams the game's own hit yells (see The scream crowd) |
| `supplies` | true | false turns off every pickup and gift between waves |
| `pickup_mode` | `"boss+random"` | health/armour gifts: `"boss"`, `"random"`, `"boss+random"`, `"every"`, `"none"` (the A6 economy turns the health gift off with `health_gifts = false` and replaces the armour gift with centre armour) |
| `ammo_every`, `ammo_max_center`, `ammo_ttl`, `ammo_rounds`, `centre_radius` | 40, 3, 90, 50/60/16, 800 | the centre ammo box |
| `armour_center`, `armour_chance`, `armour_amount`, `armour_ttl` | true, 0.25, 1.0, 120 | the centre armour |
| `health_per_kill` | by tier | fraction of a full bar per kill |
| `give_guns`, `give_ammo`, `ammo_frac`, `max_drops`, `drop_keep_radius` | false, false, 0.30, 20, 400 | gun drops and gifts |
| `knife_range`, `knife_cooldown`, `sight` | 4500, 1.2, 15000 | knife throwers' range and cooldown (seconds); how far every enemy sees (units) |
| `boss_hp_mult`, `boss_armour`, `boss_cycle_mult` | 2.5, 0.5, 0.35 | boss toughness |
| `elite_from`, `elite_share0`, `elite_share_inc`, `elite_share_max` | 8, 0.25, 0.05, 0.75 | elite troops |
| `pickup_markers`, `knife_keep`, `knife_keep_dist`, `seat_gap` | true, true, 3300, 240 | markers, knife throwers' distance, spectator spacing (`_arena_polish.lua`) |
| `mutators`, `elites`, `scores` | false, false, true | wave mutators, affix elites, best scores (the chaos presets turn the first two on) |
| `coop` and `coop_*` | off | co-op rules and waves for two (the co-op presets) |
| `crowd_favour` / `crowd = {...}` | on | the crowd favour (see Crowd favour) |
| `turbo_from`, `turbo_share0`, `turbo_share_inc`, `turbo_share_max`, `turbo_lo`, `turbo_hi0`, `turbo_hi_inc`, `turbo_hi_max`, `turbo_knife_from`, `turbo_knife_lo`, `turbo_knife_hi` | see file | turbo enemies and the turbo knife wave |

Below `S` are the tables `TIERS` (health and armour multipliers), `TIER_MIX` (which tier mix applies from which wave), `ELITES` (the elite bodies and `ELITE_GUNS_*`), the skin pools and `TITLES`. Per-wave-type counts and base health are in the `mk_*` functions.

Test hooks: `first_wave = N` starts at wave N; `debug_track = true` logs every live enemy's distance to Bond and action every 2 seconds; `debug_clear_after = s` kills each enemy after s seconds; `debug_kill = s` kills each enemy s seconds after it spawns (a real death), and `debug_headshot = n` makes every n'th of those a headshot. `tests/arena_crowd/run-react.sh` runs the crowd reaction test. Enemy toughness and speed scale with the wave, so a long endless run gets very hard.

## Test keys

For testing, the wave can be jumped. They work when the setting `test_keys` is true or `GE_ARENA_TESTKEYS=1` is set, in solo and local co-op, never in netplay (the other machine would not see them). Pressed where they are off, the first one says "TEST KEYS ARE OFF". `GE_KEYLOG=1` logs every key that reaches the test-key handler; `GE_KEYINJECT="secs:scancode,..."` pushes real SDL key presses (tests: `tests/arena_menu/realkeys.sh` with xdotool, `tests/win/keyinject_win.py` in Windows hidden mode).

| Key | Console | Action |
| --- | --- | --- |
| PageDown / PageUp | `arena next` / `arena back` | remove the live enemies (no kills, drops or health) and start the next / previous wave now |
| Shift+PageDown / Shift+PageUp | `arena next+` / `arena back+` | start the next / previous wave now and leave the live enemies and the queued spawns alone |
| Home / Shift+Home | `arena callout` / `arena callout+` | show the next / previous kill callout of every tier |
| End | `arena fog` | the LOW VISIBILITY fog on / off now (as the mutator sets it; it fades in and out over 1.5 s) |
| Insert | `arena rain` | the THUNDERBALL rain mutator on / off now (all of it; slower enemies from the next spawns) |

The LOW VISIBILITY fog (`mutator_fog_far`, 2600) is stock Jungle's fog curve: it thickens from about half that distance to full at it, and everything past it (stands, crowd, corpses) is drawn in the fog colour; the far plane is past the whole Colosseum, so nothing is cut off. `port/src/port_arenafx.c` (the fade), `port/src/port_roomfog.c` (the room's display lists are re-patched for fog when it starts and stops); the impostor crowd and the pickup markers fog with the same curve. Test: `tests/arena_a8/fog-look.sh` (screenshots before, during and after).

The wave counter, titles, bosses and difficulty follow the wave you land on (it never goes below 1). In fixed mode, "next" past the last wave wins. A Lua mode can listen with `on("test_key", function(name) ... end)`; `name` is `"next"`, `"back"`, `"next+"`, `"back+"`, `"callout"`, `"callout+"`, `"fog"` or `"rain"`.

## Big crowds and keeping every body

The stage reserves 600 chr slots by default (`GE_COLOSSEUM_SLOTS=<n>` overrides it, 240 to 4000; stage init runs before Lua, so it is an environment variable). That is the crowd of 100 + 400 kept corpses (`corpse_limit`) + up to 18 live enemies + the Bonds, with about 80 to spare because a removal lands a frame late. A 20-wave run (about 173 enemies, every body kept) needs only about 290. If slots run low anyway, the arena removes the oldest corpses first before a wave spawn, so a wave never waits for slots. The PC build's fixed pools are sized for about 1400 chrs: 2400 props, 32000 model hit entries, 2500 on-screen props. They cost memory (about 1 MB) in every stage and do not change behaviour below the old limits. Settings: `corpse_keep` (seconds before a corpse is removed; `-1`, the default, keeps every body, `4` is the stock fade-out), `corpse_limit` (when more are kept, the oldest go, 400), `crowd_rate` (spectators spawned per second, 200). The mode ticks four times a second, so that is 50 a tick.

## The impostor crowd

On PC the seats the real spectators do not use get impostors: flat pictures of the crowd skins that turn to face the camera around the vertical axis. On the first frames of the stage `port/src/port_crowd.c` builds each of the 16 `CROWD_SKINS` as a private model (not a chr, not in the model slots) and renders it through the normal display list into a 1024x1024 atlas framebuffer, once idle and once arms up (`surrendering_unarmed`). The atlas is read back once, keyed on its magenta clear colour for alpha and mipmapped. Every frame, all impostors are one GL draw (`gfx_opengl.cpp`, `gfx_opengl_crowd_draw`): a static vertex buffer, billboarded in the vertex shader, alpha-tested and depth-tested against the level, drawn just before the world-end marker. The draw carries its own world-to-clip matrix and loads nothing into the RSP, and the GL state it changes is put back afterwards: the gun draws next, with whatever projection the world left loaded, so a leaked matrix once put Bond's gun on the floor in front of him (`tests/arena_crowd/gun-look.lua` checks it). They are presentation only: no chr, prop, collision or hit test, their own RNG and clock, and nothing the sim or the netplay hash sees.

Lua: `crowd.impostors({x, y, z, ...}, {skin names})` puts them up (`{}` removes them); `crowd.cheer(seconds [, fraction])` makes that share (1) raise their arms, each with its own small delay and length so it ripples. The arena cheers with them for kills, wave clears and boss kills (see Crowd reactions). Settings: `crowd` (real spectators, front rows first, 150) and `crowd_impostors` (1000). Limits: front view only, and the poses do not animate beyond the idle / cheer swap. Seats are the 1200 seat pads, so the crowd is no denser than the seats. `tests/arena_crowd/` has the look and smoke scripts.

## Crowd reactions

The crowd cheers kills and boos Bond's trouble. Each trigger has a level, and a reaction is skipped while its own cooldown runs, or while a reaction of the same or a higher level started less than `react_gap` (1 s) ago, so a run of kills does not turn into a wall of noise and a boss kill is not followed by a second, smaller wave-clear cheer. The defaults (`REACT` in `S`, every field tunable; `ARENA_SETTINGS = { REACT = { kill = { vol = 0.3 } } }` changes just that):

| Trigger | Level | Impostors with arms up | Real spectators reacting | Seconds | Sound | Volume | Cooldown |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `kill`: an arena enemy dies | 1 | 12% | 5% | 1.5 | `cheer_small` | 0.45 | 0.8 s |
| `headshot`: its last hit was to the head | 2 | 30% | 15% | 2.5 | `cheer_big` | 0.65 | 1.5 s |
| `multikill`: `multikill_n` (3) kills in `multikill_window` (2 s) | 3 | 50% | 30% | 3 | `cheer_big` | 0.80 | 3 s |
| `wave_clear` | 4 | 80% | 60% | 4 | `cheer_big` | 0.90 | - |
| `boss_kill` | 5 | 100% | 75% | 5 | `cheer_huge` (ducks the rest to 0.5) | 1.0 | - |
| `big_hit`: Bond loses `big_hit` (0.1 of a bar, health + armour; a squad of wave-3 shooters takes about 0.06 to 0.10 a second) inside 1 s | 2 | boo | 15% | 2 | `jeer` | 0.55 | 4 s |
| `low_health`: Bond's health drops under `low_health` (0.25) | 3 | boo | 35% | 3 | `boo` | 0.75 | 10 s, and only after it has been back above 0.40 |
| `bond_died` (each Bond in co-op) | 6 | boo | 85% | 5 | `boo_big` (ducks the rest to 0.25) | 1.0 | - |

The strongest kill trigger that is not cooling down wins: boss, then multi-kill, then headshot, then an ordinary kill. Intensity is the share of the crowd that reacts, how long it lasts, and the sound's volume.

- **The real spectators** each roll once: the share in the table plays an animation, a little staggered, then returns to the idle pose and its fidgets. Cheer: `surrender` (arms up, half of them), `dance` or `laugh`. Boo: `fists` (`ANIM_drop_weapon_and_show_fight_stance`, fists raised at the pit, the MP character-select taunt: only its stance, frames 248 to 360, about 4 s; over half of them), `cross_arms` (the guard's holster-and-fold-arms, unarmed) or `laugh` (`ANIM_laughing_in_disbelief`, mocking). These are the same one-shot animations as the fidgets (`spawn.anim`), so they cost what a fidget costs: the spectator stays dormant (no prop collision, no AI beyond its idle list, invincible). Spectators already animate every frame.
- **The impostors** raise their arms for a cheer (`crowd.cheer`). The atlas is full (16 skins x 2 poses in its 8 x 4 cells), so there is no boo pose: a boo lowers the arms of any still cheering.
- **The sounds** are WAV files you supply, below. With none, everything else still happens. `crowd_screams` adds (or swaps in) the game's own yells, below.

Co-op and netplay: the triggers are sim events (kills, the waves, Bond's vitals and death) timed on the mode's clock, and the spectator choices use the session's `math.random`, so every machine animates the same spectators the same way. The impostors and the sounds are presentation only: their own RNG on the C side, and nothing they return steers the game. Each machine plays whatever recordings it has.

### Crowd sounds

Put WAV files in `Data/Sounds/` next to the exe (the build creates the folder, with a README). Any sample rate, mono or stereo, 8, 16 or 32-bit or float: each file is converted once, when first used, to the game's output (22050 Hz stereo, so content above 11 kHz is lost); files over 60 s are cut. The arena loads them at stage start and logs `ge: arena: crowd sounds in Data/Sounds: cheer_small 2, cheer_big 1, ...`.

| Name | Played for | Falls back to |
| --- | --- | --- |
| `cheer_small` | a kill | - |
| `cheer_big` | a headshot, a multi-kill, a wave cleared | - |
| `cheer_huge` | a boss kill | `cheer_big` |
| `jeer` | a big hit on Bond | `boo` |
| `boo` | Bond's health low | - |
| `boo_big` | Bond dies | `boo` |

Each name can have up to 16 variants: `cheer_small.wav`, `cheer_small1.wav` ... `cheer_small16.wav` (gaps are fine). A random one plays each time, never the same one twice in a row when there is a choice. A missing file is skipped. The level follows the game's sound effects volume (and its pause fades) times `crowd_volume`. At most 6 crowd sounds are heard at once (a seventh fades the oldest out), and together they are scaled by 1/sqrt(the number playing).

**Licensing:** the project ships no crowd recordings. Anything you put in `Data/Sounds/` and pass on, or include in a release, must be licensed for redistribution (CC0 / public domain, or a licence whose terms you meet, with the credit it asks for in the release notes).

The Lua side is the `sound` table (`lua.md`): `sound.play_any(base, volume)` is what the arena uses.

### The scream crowd

The game has no crowd recordings, but it has 25 male hit yells, the girls' hits, a crushed yell and an evil laugh. `crowd_screams = true` layers them as a crowd on top of the WAV sounds; `crowd_screams = "only"` plays the yells instead of the WAVs (so it needs nothing in `Data/Sounds/`). Off by default. In a preset: `ARENA_SETTINGS = { crowd_screams = true }`; or, without editing a file, set `GE_ARENA_SCREAMS=1` before starting the game (`set GE_ARENA_SCREAMS=1` in a command prompt, then run `Play Arena.bat` from it).

| Trigger | Yells |
| --- | --- |
| `kill` | 2 to 4 male yells |
| `headshot`, `multikill` | 6 to 10 |
| `wave_clear` | 12 to 16 |
| `boss_kill` | an explosion, then all 25 male yells, the 4 girl hits and the crushed yell |
| `bond_died` | the evil laugh and 3 to 5 yells after it |

The other triggers (big hit, low health) scream nothing. Each yell starts 30 to 150 ms after the one before (30 to 100 for the boss chorus), at a random pitch (0.85 to 1.25) and pan, at `scream_volume` (0.8) times `crowd_volume`. A reaction's cooldown and level rules apply as for the WAVs. `SCREAMS` in `S` holds the layers per trigger, `{ set, count [, count_hi], vol =, pitch = {lo, hi}, gap = {lo, hi}, delay = }`, with the sets `male`, `girl`, `crushed`, `boom` and `laugh` (`SCREAM_SETS`) or a list of `SFX_ID` names; `ARENA_SETTINGS = { SCREAMS = {...} }` replaces the whole table.

They play through `sound.sfx_burst` (`lua.md`): the game's own sound player, so the game's sound effects volume applies, but they never cost a game sound. The player holds 16 sounds (8 x `GE_SNDSCALE`). A yell that finds the player full is dropped instead of cutting a shot, a shot that finds it full cuts a yell first, and at most 16 yells are under way at once (counting the ones about to start: the game starts each sound a little after it is asked for). On a quiet floor the full chorus plays about 28 of its 31 sounds, about 10 of them at once; on a firefight it loses more of its yells, never a gunshot. Like the WAVs they are presentation only: the C side picks the yells, pitches, pans and gaps with its own RNG, and the mode's `math.random` is not touched. `tests/arena_crowd/run-screams.sh` renders the bursts to a file and runs them over real gunfire; the result is the game's own audio, so it never goes in the repository.

## The benchmark

`Arena Benchmark.bat` (next to the exe) runs the preset `arena-bench` (`tools/mode_arena_bench.lua`) with `GE_COLOSSEUM_SLOTS=1400` and `GE_CHEATS=invuln`, and quits by itself after about 4 minutes. Phase AI, right after A, is A plus impostors on the other 1100 seats (they are removed again for B). Five phases of 30 s steady state: crowd 100 / 100 / 300 / 1000 / 1000, 0 / 250 / 250 / 250 / 250 corpses (real kills, kept to the end) and, in the last, 18 live enemies shooting at Bond. Bond stands in the middle and turns at 24 degrees a second, alternating half turns looking at the stands and at the floor. `Data/Logs/arena_bench.txt` gets the machine lines, then per phase the frame time (avg, p50, p95, p99, max, fps; overall and for the stands and floor views), render/sim split and draw calls, spectator / corpse / enemy counts, and pool use (chr slots, props, hit entries with their peak, stage memory left). The Lua side is the `bench` table (`port/luavm/port_luavm_arena.c`).

`Arena Benchmark (Live).bat` runs the preset `arena-bench-live` (`tools/mode_arena_bench_live.lua`, which sets `BENCH_LIVE` and includes `arena-bench`) with `GE_COLOSSEUM_SLOTS=600`: no crowd, no corpses, and phases L0 (nobody, the baseline), L25, L50, L100 and L200 live soldiers (the arena `chase` behaviour) fighting the invulnerable Bond, who never shoots. The live count is topped up through each phase. Each phase block adds how many were attacking, moving or doing something else (sampled every 2 s) and a sim cost per live enemy, (sim avg - L0 sim avg) / live, in microseconds. Same frame, render and pool lines as above.

What it found (perf on the Linux build): 98% of the sim was every chr's per-frame move test (`chrUpdateAnim` > `sub_GAME_7F01F614` > `stanTestLineUnobstructed` / `stanTestVolume`), which gathers the room's props with `roomGetProps`. That function removed duplicates by scanning its own list, so with ~1300 props in the one big room it was quadratic, and it ran twice per chr per frame (48% self time in phase D). Now it marks each propnum instead, which gives the same list. Spectators and kept corpses test only against the level, not against props, and skip the test when they have not moved. Kept corpses run no AI list. Spectators have no collision box. Linux sim ms per frame, before and after: A 0.45 / 0.30, B 20.8 / 0.78, C 43.0 / 0.93, D 241 / 1.22, E 307 / 3.49. Spectators still animate every frame at full rate.

## Under the hood

The mode relies on the Lua spawning API (`spawn.chr`, `spawn.item`, `spawn.anim`, `bond.give`, `bond.heal`; see `lua.md`) and on C-side additions that are active only for arena enemies: AI lists with ranged fire and direct routes, a bigger model hit-entry pool, per-chr knife throwing, and a weapon pool that protects drops near Bond. The stock game, the attract demos and the rest of the port are not affected; the demo oracle in `tests/baseline/` verifies this.

Arena soak and scenario scripts are in `tests/arena_a5/`, `tests/arena_a6/`, `tests/arena_crowd/`, `tests/arena_test/`, `tests/arena_a7/` (polish, mutators, elites, scores, co-op; `merge_sweep.sh polish|mut|coop` runs them in three lanes) and `tests/arena_a8/` (crowd favour and markers; `run.sh`); copy a preset into `Data/Scripts/modes/` and run it with `GE_MODE=<preset>` (see the scripts). Run them on a real `Xvfb :99` with `GE_FIXEDTICK=1`; netplay (`coop-net`, `merge-coopchaos-net`, `a8-net` through `tests/net_modes.sh`) alone.

The A7 / A8 work was built on the lab line (`arena-a7`) and ported onto heist for arena-v0.3; their test hooks are in the `test` table (`test.hurt`, `test.shoot`, `test.coop_down`, `test.coop_kill`; `lua.md`).

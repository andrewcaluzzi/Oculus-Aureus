# Snapshot fidelity: sim state outside the snapshot

**Relevant to netplay rollback.** The player bot's solo savestates (`port_snap.c portSnapSoloRequest`) and netplay
rollback's re-simulation (`portSnapRollbackTo`, `portBossResim`) save and restore the same snapshot. Anything the
simulation reads that the snapshot does not hold makes a restored run part from the run it rewound - for the bot, an
optimiser trial that a fresh process does not reproduce; for netplay, a rollback desync. Round 7 of the bot work
(`modding/bot-notes.md`) found and fixed six such causes; after them a save, a run of N ticks, a restore and the same
N ticks again is **bit-exact** on all nine bot stages.

## The test

`GE_SNAPSOLOTEST=T,N[,1]` (`port_snap.c soloTestTick`, solo, off by default): at the start of tick T save the whole
state; run T..T+N-1 (pass 1) while every tick's start state is hashed in 4 KB chunks; at T+N keep the state and restore
the save; run the same ticks again (pass 2: a replay `GE_INPUTPLAY`, or the bot deciding again from its restored state)
and compare each tick's chunks with pass 1's. It prints the first tick that differs, every chunk the first tick it
differs (`whatIs`: a static range, a pool offset or an extent), and at T+N a byte compare (`DIFF` lines). `,1` exits
after the result. Helpers:

| Env | What |
|---|---|
| `GE_SNAPTESTAT=<tick>` | pass 1 keeps the whole state of that tick too; pass 2 compares it byte for byte and says where in each differing chunk |
| `GE_SNAPTESTOUT=1` | the exe's writable data OUTSIDE the snapshot, word by word, at the save and at the restore: what pass 1 changed that pass 2 starts from (Linux) |
| `GE_SNAPCMPMAX=<n>` | how many `DIFF` lines a compare prints (40) |
| `GE_TEXLOADLOG=1` | every texture load with its tick and a backtrace (`src/game/image.c`, Linux) |
| `GE_SNAPHASH=a,b` | (existing) per-tick group hashes; the `rom` field is the ROM image + inflated data: it moves when the sim writes served data in place |

Method: run the test on a stage; at the first differing tick read the chunks; when the difference only appears for
long N (the cause is something pass 1 did LATER that pass 2's early ticks read), bisect N until one pass-1 tick is the
culprit and diff `GE_SNAPTESTOUT` of N and N+1. Scripts used (not in the repo): `E:\midas-temp\bot\fid\*.sh`
(`soltest.sh` replay, `soltestbot.sh` bot, `bisect_n.sh`, `outaddr.sh`, `mapobj.py` = address -> object file and
snapshot class from the link map).

## Findings

| # | Cause | Symptom | Fix | Netplay rollback |
|---|---|---|---|---|
| 1 | **Audio.** The sim asks the sound player whether a sound still plays (`sndGetPlayingState`: chrai.c AI sound slots and their sim-visible volume, chr.c / chraction.c a guard still talking, gun.c / gunfire.c free impact and casing slots, doors, trucks, alarms), holds the engine's `ALSoundState` pointers, and the sound player clears the sim's owner word when a sound ends (`sndUnlinkClearSound`). All on the audio clock: frames rendered by wall time (no device: `SDL_GetTicks`; with `GE_CLOCKSCALE=20` a sound lasts 20x as many sim ticks; with a device: the queue level). The engine is outside the snapshot, so a restore hands the sim the abandoned future's voices. | Handle values and slot choices differ between a run and its restore on every stage (Dam, Statue, Bunker 2...); Surface 2's in-process trial and clean run parted. | `port/src/port_simsnd.c` (SIM): the sim gets a **token** per sound it starts; its life (start, attack + decay, release; looped / retriggered: until stopped) is computed from the sound's envelope in sim fields; `sndGetPlayingState`, `sndDeactivate`, stop-by-flag and the owner-word clear (`portSimSndTick` at the start of every sim step) answer from it. `snd.c` maps a token to its voice through a presentation table a restore never touches. `GE_SIMSND=0` = the old behaviour. `g_ImpactSfxStates` are rewound again (were live). Test: `tests/simsnd_test.c`. | **Yes.** `GE_SNAPRESIM=600,30,97` on a Frigate replay: 16 of 70 rollbacks DIFFERENT before (casing / impact handles, owner words), 0 after. Also a lockstep risk: two machines' audio clocks differ. |
| 2 | **The texture bank** (`assets/oddtextures.c`, an asset object: FOREIGN, not snapshotted). `texLoad` / `texSelect` rewrite an id into a pointer into the stage texture pool the first time a texture is used, mid-stage too (bullet impacts, the gun). | After a restore the bank pointed at pool memory the restore emptied: the texture was never loaded again and every later pool allocation landed elsewhere (Surface 2, tick 1171). | `image_bank.c texReset`: the bank's bytes are a snapshot range. | **Yes** (any rolled-back window in which a texture is first used). |
| 3 | **The VI settings** (`src/fr.c`, presentation). The sim's camera model reads the back VI settings (`viGetFovY`, aspect, view size) and writes them every pass; the front/back pair flips at every drawn frame; the player keeps a pointer to `g_viProjectionMatrixF`. | A restore left the pair where the abandoned ticks put it: the next tick read the other entry (Frigate: a different room walk). | `fr.c viInitVideoSettings`: `g_ViDataArray`, the front/back pointers and indices, the VI mode ring, the projection matrix and perspective normal are snapshot ranges. | Solo rollback: yes. Co-op uses a canonical camera model (`portSimViewOn`), which removes most of the read. |
| 4 | **The portal table.** Doors and scripts open and close portals by writing control bytes (`PORTALFLAG_DISABLED`, fade counters) IN PLACE in the bg file, and a served bg file lives in the ROM arena (outside the snapshot; `GE_SNAPHASH`'s `rom` hash moved at the tick a Frigate door opened). | A restore kept the abandoned ticks' doors: the next room walk went through a portal the restored world had shut (Frigate tick 601, Bunker 2 tick 679: rooms loaded, visibility, then everything). | `bg.c load_bg_file`: the portal table is a snapshot extent (`bgportals`). | **Yes** (a door that toggles inside a rolled-back window). |
| 5 | **Polls per tick** (harness). The pump polls the controller once per pass, and with `GE_NOSLEEP` / `GE_CLOCKSCALE` the number of passes between two ticks is wall time; the sample ring and the poll counters follow. The input the sim reads is the same. | Only counters differ (11-32 bytes over 1500 ticks), but no run is bit-identical. | `pcsched.c`: one poll per in-stage tick under `GE_FIXEDTICK=1` + `GE_NOSLEEP=1` (`GE_ONEPOLL=0/1` overrides). | No (re-simulation already replays the recorded poll count). |
| 6 | **The bot's own scratch** (solo only). The cover search's visit stamps (`sCovStamp`, generation `sCovGen`) and the A* f-scores are heap buffers allocated on first use; the graph's mutable costs were snapshot ranges only with the optimiser on, and fixed ranges would name freed memory after a stage change. | A restore rewound the generation but not the stamps (a tile could read as already visited); pointers reallocated. | `port_playerbot.c`: the graph costs are an extent; the scratch pointers, sizes and generation are live (never rewound: each search reinitialises what it reads). `port_playerbot.c` / `port_playerbotgraph.c` are classified SIM. | No. |

Also found while testing (the tool, not the game): a chunk table without enough headroom read the next tick's row once a
pool grew (a false "Statue tick 5927"); fixed with 8 MB of headroom and a "not compared" line.

## Result

`GE_SNAPSOLOTEST`, bot playing (`GE_PLAYERBOT=speedrun`, the optimiser's case): Dam, Runway, Surface 1, Bunker 1,
Frigate, Surface 2, Bunker 2, Statue, Depot all BIT-EXACT over ticks 400-1899; over 600-3599 on every stage whose
mission lasts that long. Replays: Surface 2, Frigate, Bunker 2 bit-exact over 600-6599. Rollback re-simulation
(`GE_SNAPRESIM`, every tick sim-only): 70 of 70 bit-identical on Frigate and on Surface 2. dam1 IDENTICAL.

## Still outside the snapshot (watch list)

- **Compiled-in model files** (assets) are converted in place once per process (`sub_GAME_7F0762E0`: texture ids ->
  pointers into the model texture pool, node display lists -> buffers in the sim high heap), and both of those are
  rewound. With the ROM mandatory every model is a served file loaded per stage into rewound memory, so this did not
  show; a fallback build that converts a compiled-in model inside a rolled-back window would keep pointers into memory
  the restore freed.
- Served data edited in place: the portal bytes are now in the snapshot, but `GE_SNAPHASH`'s `rom` hash moves by design
  when a door toggles. A new in-place write elsewhere would show there first.
- Music and the presentation sounds (`portSndPlayPresent`, Lua `sound.*`) stay on the engine; nothing in the sim reads them.

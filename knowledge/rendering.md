# Rendering

## The path from game to GPU

1. The game builds an N64 display list (F3D-style GBI commands) during `lvlRender`, one pass per player.
2. `rspGfxTaskStart` (`src/boss.c`) -> `src/game/rsp.c` -> `schedSubmitTask` (`port/src/pcsched.c`), either
   synchronously or handed to the render thread (`videoSubmitAsync`, `port/src/video.c`).
3. `gfx_run` (`port/fast3d/gfx_pc.cpp`) interprets the list (`gfx_run_dl`), flushes, draws the sorted translucent pass,
   presents and swaps.

- There is no frame interpolation: one tick, one frame.
- Unknown opcodes call `sysFatalError` and dump the last DL jumps ("Unknown GBI opcode" also appeared when the
  Colosseum's master display list overran at ~1000 chrs before the fork grew it per slot).
- Extension opcodes live in `port/fast3d/gbiex_compat.h`. **0x4b is reserved for upstream's co-op branch.**
- New shader option bits must use bits 28-31 (`gfx_sp_tri1`'s `!use_alpha` mask clears 16-27). After changing the
  combiner or shader ids, rerun `scripts/port/record_shaders.sh`.
- With the render thread on, GL may only be touched on the render thread; per-frame presentation input must be
  snapshotted into `PortRtTask`.
- Host pointers: Fast3D's `seg_addr` treats any word below 0x10000000 as a segment reference
  ([engine-overview.md](engine-overview.md)).

## RSP matrix state is shared, and leaks

The RSP keeps a current projection and modelview that every later draw in the list inherits. Rare's code loads them
with `gSPMatrix` at the start of each phase (world, props, gun), and some phases deliberately inherit what the previous
one left. Anything you insert into the list must leave that state exactly as it found it.

**The crowd bug (fork, commit 412ef8f).** The impostor crowd (`port/src/port_crowd.c`) loaded the chrs' projection and
camera to draw its quads, then "restored" the projection to the bg's (`field_10E0` = projection x camera). The gun draws
next and, with Weapon FOV idle, keeps whatever projection the world left loaded (`gunWeaponFovBegin`). Its view-space
matrices then went through the camera a second time, and **Bond's gun was drawn in world space, on the floor near the
pit centre**. Found by bisecting `crowd_impostors` 1000 (no gun in view) vs 0 (gun in place) on one build.

Fix and lesson: the crowd frame carries its own world-to-clip matrix (computed in floats on the sim side) and **emits no
`gSPMatrix` at all**, so the RSP state the gun inherits is untouched. The GL side also restores the depth mask and
program and bumps `ge_state_serial` (the draw-state hoist cache) after the draw, and the atlas capture restores the
framebuffer it found and bumps `rsp_p_gen` / `roc_mtx_gen` when it restores the RSP. **Rule: a port-side draw must not
change RSP or GL state that later Rare draws inherit; if it must, restore exactly, including every cache key.**

## Rooms and portals

- Levels are split into BG rooms joined by **portals** (quads in `portal_data_table`). The world draw is a **portal
  walk** from the camera's room: a room is drawn when a portal into it is on screen, clipped to the screen box of the
  portals it was seen through (`bgDetermineVisibleRooms`, `src/game/bg.c`). A room behind a wall with no portal on
  screen is not drawn. The camera's room is the room byte of the stan tile under it.
- Doors flagged `PROPFLAG_CULL_BEHIND_DOOR` close the portal between their two rooms while shut; glass and windowed
  doors toggle portals by distance.
- **The walk is sim state.** It sets the rendered-room flags and `PROPFLAG_ONSCREEN`, which AI, spawn gates, autoaim,
  objectives and hit tests read. The full-height view and the one-pixel edge inset change which rooms are entered and
  so the RNG; demos therefore keep the vanilla viewport (`portFullHeightActive`, `bondview2.c` ~11290;
  `bgUpdateCurrentPlayerScreenMinMax`, `bg.c` ~6815). `GE_FULLHEIGHT=0` and `GE_EDGEINSET=1` force the vanilla ones always.

### The render-to-sim reads

| The sim reads | Built by | Consequence |
| --- | --- | --- |
| `model->render_pos` and model matrices | `chrpropRender`, room by room, in a per-frame pool | shots, explosions, the photo objective read the last draw; a tick that does not draw leaves them stale |
| `PROPFLAG_ONSCREEN`, rendered rooms, room 2D boxes | the room walk | AI "on screen" tests, spawn gates, autoaim |
| ModelHitEntries | each drawn chr takes one per model node | a chr that was not drawn has no hit model, so a bullet passes through it |
| Monitor `rwdata->gdl` / `Vertices` | `process_monitor_animation_microcode`, while drawn | a shot walks the monitor's last display list |
| Door clipped vertices | `doorBuildClippedVertices` at `doorInit` | were copied out of the per-frame vertex pool frames later (fixed: kept at once) |

These are why co-op/netplay needed a **canonical sim view and two walks** (fork): the sim walk runs from the player's
fixed canonical camera and is snapshotted for every sim read (`bgSimRoomsSnapshot`); a second **render walk** from the
window's real camera adds "margin" rooms drawn geometry only, never props (props build matrices the sim reads). With
the default window no render walk runs. Details: [determinism-and-netplay.md](determinism-and-netplay.md).

## Camera feeds (fork, heist P1)

A CCTV feed shows the world from a camera on one machine without changing the sim
(`port/src/port_cctvview.c`, `bg.c`, `lv.c`):

- The camera's portal walk runs **before** the sim walk, so every flag and list the sim reads is the sim walk's own.
  Rooms only the camera reached draw geometry only. The portal side test reads the camera's eye during that walk.
- No prop or room matrix is rebuilt. Instead the two fixed-point projection matrices the display list loads are
  rewritten after the sim used their float sources: props `P_cam x V_cam x V_bond^-1`, bg `P_cam x V_cam` relative to
  the room origin. The GPU just sees the same matrices from another eye.
- Chrs only the camera sees are posed into fresh matrices and drawn, then their `Model` structs are restored byte for
  byte. That draw consumed the **sim RNG** (a gun's muzzle flash in `model.c`) and allocated from the frame's **linear
  pool** (`dyn.c`): both desynced co-op until the feed restored both generators and their draw counts and drew from its
  own corner at the top of the pool (+512 KB in a heist mode).
- Per-room scissors are off for a feed. The lens is a fixed 62-deg FOV.

## World points on the 2D overlay (fork, the AR specs)

`port/src/port_arhud.c` (designs/bank-heist.md section 20) draws markers over the frame at world positions. The recipe, measured
against chrs' heads in 1280x720 Xvfb shots:

- **Matrices.** The local Bond's `player->field_10CC` (world to view, `camGetWorldToScreenMtxf`) times `player->projmatrixf` (the chrs'
  projection), row-vector convention (`p * M`; `port_crowd.c` builds the same product). Both are per-frame `dyn` allocations, still valid
  when `portConRender` runs at the end of that frame's list. Read them from `g_playerPointers[portLocalPlayer()]`, not `g_CurrentPlayer`.
- **Clip.** `w < 1` is behind (or at) the camera: clip a 3D segment there before dividing, or a line through the camera flips across
  the screen.
- **Viewport.** NDC to the player's viewport in 320x240 units (`c_screenleft + (x + 1) / 2 * c_screenwidth`, y flipped), then to the
  console's 2D space (480 lines, `portConsoleSpace` width): x `* W / 320` in the wide and stretched modes (the widened projection fills
  the window), `* 480 * 4/3 / 320` plus a centring offset when `portPillarboxFrame()`; y `* 2`.
- **Visibility.** `chr->prop->flags & PROPFLAG_ONSCREEN` is the game's own "drawn this frame" (room portals and the frustum); there is
  no cheaper occlusion test, and a stan line test from the render pass would touch sim scratch state.
- **Drawing.** Fill rects (`gDPFillRectangle`, `G_CC_PRIMITIVE`, `G_RM_XLU_SURF`) first, then text (the console's combiner), inside the
  `G_CONSOLE2D_EXT` bracket with `viSetXY` set to the console's size (the text and rect code clip to the VI size). Lines are runs of thin
  rects, one per step of the minor axis (at most 40). Gate every write on `dynGetFreeGfx2`.

## Pools that ran dry

| Pool | Stock | Symptom when full | Fork |
| --- | --- | --- | --- |
| `g_ModelHitEntries` (`objecthandler.c`, `initunk_005450.c`) | 600 | chrs late in the draw order get a partial body, then none (invisible), then `chrTestHit` reads a NULL hit list and crashes; depends on how many chrs are in view | 8000, then 32000; logs `ModelHitEntry pool exhausted` |
| Props `g_Props` | 600 | | 2400; on-screen list 500 -> 2500 |
| Master display list, vertex pool (Colosseum) | fixed | "Unknown GBI opcode" at ~1000 chrs | grow 1536 B per slot above 240 |

## Headless rendering (Linux build in Docker, Xvfb)

- The build image has i386 Mesa; a throwaway container adds `xvfb` and `libegl1:i386` and runs `Xvfb :99 -screen 0
  1280x720x24 +extension GLX`. It renders on llvmpipe (software): slow, but sim ticks still follow `GE_TICKHZ`. Recipe:
  [tooling-and-testing.md](tooling-and-testing.md).
- **Screenshots under Xvfb show only the HUD on black unless `GE_FULLHEIGHT=0 GE_EDGEINSET=1` are set** (every heist
  and levelgen screenshot script sets them). The same pair is the README's workaround for "levels draw as a few large
  blank triangles" on some Linux systems.
- `GE_SHOT=N` writes a BMP every N frames into the working directory. Convert and keep few
  (`tests/bmp2png.py`, `bmp_sheet.py`): BMP batches filled a disk once ([tooling-and-testing.md](tooling-and-testing.md)).
- GL frame times under llvmpipe are not representative; sim times are.

## Odds and ends

- Runtime-created *weapon* props from `GE_SPAWNWEAPON` draw nothing (the comment at `lv.c` ~472). Runtime chrs, thrown
  knives, the arena's `spawn.item` pickups and co-op death drops all draw.
- The impostor crowd (fork) is one GL draw: camera-facing quads from a 1024x1024 atlas captured once per stage from the
  real crowd skins. Presentation only: no chr, prop, collision or hit test; its own RNG and clock.
- `room_cache`: the help file says the default is 2; the PC code defaults to 0 (only the Xbox build uses 2).

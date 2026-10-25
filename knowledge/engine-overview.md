# Engine overview: how the N64 game runs on a PC

Project Midas compiles the decompiled game (`src/`) natively as **32-bit x86** and replaces libultra, the RSP/RDP and
the boot chain with a port layer (`port/`). It is not an emulator: `src/game/*.c` is ordinary C that calls port
functions where the N64 talked to hardware.

## Layout

| Path | What |
| --- | --- |
| `src/game/*.c` | Rare's game logic (globbed by CMake) |
| `src/*.c` | Engine layer (scheduler, memory, audio glue, sound player) minus a few N64-only files (`CMakeLists.txt` `ENGINE_EXCLUDE`) |
| `src/libultra/audio`, `src/libultrare/...` | The real AL synthesizer and the compressed-MIDI sequencer, compiled for PC |
| `port/src/*.c` | The port layer: video, input, audio mixing, ROM loading, console, and (fork) Lua, co-op, netplay, modes |
| `port/fast3d/*.cpp` | The renderer (a Fast3D display-list interpreter on OpenGL) |
| `include/` | Shadow libc headers (`math.h`, `string.h`) that the freestanding C build uses |

## One sim thread

- libultra "threads" are bookkeeping only (`port/src/libultra_shim.c`). Message queues are real FIFOs that never block:
  `OS_MESG_BLOCK` pumps events a few times and gives up. DMA is a `memcpy` followed by its completion message.
- The game runs on one thread. Optional extra threads: the render thread (`ge-render`, on when `tickhz != 60`) and a
  stall watchdog (`port/src/main.c`). Audio is pushed from the sim thread (`portAudioPump`, see [audio.md](audio.md)).
- Boot: `port/src/main.c` `main` -> ROM load (`romdataInit`) -> `mainproc` (`src/init.c`) -> `bossEntry` ->
  `while (1) bossMainloop()` (`src/boss.c`).

## The tick

- A sim tick is one pass of `lvlTick` plus the per-player render passes in `lvlRender` (`src/game/lv.c`). The render
  pass is part of the simulation (props tick, chrs animate, guns fire inside it); see "Two worlds" below.
- **`tickhz`** (ini key, or `GE_TICKHZ`, which wins) sets the sim rate. **60 is the classic path and must stay
  bit-identical to the N64.** Other rates are gated on `portTickHz() != 60` (`src/game/frametiming.c`). Per-tick logic
  must use `portTickDelta`; per-field duties use `portRetraceDutyDue`.
- Many game timers count in fields (`g_ClockTimer`, `g_GlobalTimerDelta`). At 60 Hz one tick is one field.
- **`GE_FIXEDTICK=1`** forces exactly one field per sim tick (`frametiming.c`). Without it a slow frame becomes a
  two-field tick and the sim depends on the machine's speed. Every repeatable test sets it.
- Frames per second equal ticks per second: there is no frame interpolation.

## Two worlds: sim and presentation

The single most important rule we learned (co-op, netplay, camera feeds, the crowd):

> **Sim state is computed only from inputs and other sim state. Presentation (what a window shows or plays) may read
> the sim, never the other way round.**

Why it is hard here: in this engine drawing *is* simulating.

- `chrpropRender` builds each model's matrices in a per-frame pool and stores them in `model->render_pos`; next tick's
  shots, the photo objective and hit tests read them back (`propobjFindHit`, `objGetOnscreenRenderBounds` in
  `src/game/propobj.c` ~12129).
- The room walk that decides what is drawn (`bgRoomVisibilityRelated`, `src/game/bg.c`) also sets
  `PROPFLAG_ONSCREEN` and the rendered-room flags, and guard AI reads those.
- Monitors set collision data (`rwdata->gdl`) while they are drawn; door shapes were copied out of the per-frame vertex
  pool frames after they were built.

So the fork keeps a **canonical sim view** per player (a fixed 320x240, 16:9, vanilla-FOV camera model that never reads
the window: `src/game/fr.c` `viSet*`), runs the sim's room walk from it, and runs a separate **render walk** for what a
window actually draws. Details in [rendering.md](rendering.md) and [determinism-and-netplay.md](determinism-and-netplay.md).
Solo play with the default window is unchanged.

Rules of thumb:

- Never call `randomGetNext()` from presentation code. Give it a private generator (the crowd, the CCTV feed, the
  watch's static effect all do).
- Never allocate from the frame's linear pools (`src/game/dyn.c`) from presentation code that runs on one machine only:
  it shifts every later allocation of the sim's draw.
- Never read `portLocalPlayer()`, the window size or the FOV setting in sim code.
- A sim read of anything the renderer owns (`render_pos`, `dynAllocate*` buffers, room geometry load state) is a bug
  waiting to happen.

## `TARGET_N64` and how port changes are marked

- `TARGET_N64` is defined only by the N64 Makefile, never by CMake. PC code lives in `#ifndef TARGET_N64` arms (about
  1,100 of them upstream; the fork adds more). The N64 build of `src/` still matches the cart byte for byte.
- `/* PORT: ... */` comments tag changes. `src/` usually declares port functions with a block-scope `extern` inside the
  function rather than including port headers.
- The CMake defines `NONMATCHING AVOID_UB BUGFIX_R0 LEFTOVERDEBUG ...` select existing decomp variants; they are not
  port markers.

## Build flags (CMakeLists.txt ~10-39)

- **All game code is built at `-O2` regardless of `CMAKE_BUILD_TYPE`.** The flag is in the global `add_compile_options`
  (~29), and CMake's Debug type only adds `-g`, so a "Debug" build is optimised (upstream and fork). Only two data-only
  animation files are kept at `-Og` (~309). Benchmarks from a "Debug" build are therefore real `-O2` numbers.
- Always on: `-m32 -msse2 -mfpmath=sse` (x87 excess precision broke game logic), `-fno-strict-aliasing -fwrapv
  -funsigned-char` (defuse decompiled-code UB), `-fno-omit-frame-pointer` (the crash walker needs it), `-g`,
  `-ffreestanding -fcommon` for C.
- Warnings: Rare's code, the engine, gu/audio, loose assets and fast3d build with `-w`. **In the fork, `port/` sources
  build with `-Werror=implicit-function-declaration`** (~457-460, commit a7989cc), because an undeclared float function
  silently returned garbage (see [pitfalls.md](pitfalls.md)). Upstream still builds everything with `-w`.
- Windows links at `--image-base=0x10000000 --disable-dynamicbase --stack,16777216` (~49-51); Linux links `-no-pie`.

## Memory and host pointers

- **`PORT_IS_HOST_ADDR(v)` is `v >= 0x10000000`** (`port/include/port_hostptr.h`). Fast3D's `seg_addr` treats any
  smaller word, with a segment installed, as an N64 segment reference. So **every pointer that can reach a display
  list must be at or above 0x10000000.** That is why the Windows image base is 0x10000000, the memp arena is mapped at
  0x20000000 and `portAllocHigh` (64 MB, never freed) at 0x30000000. Never `malloc` display-list-reachable memory on
  Linux (the heap there is low).
- The game heap is the memp arena (`src/memp.c`): bump-allocated banks, STAGE (almost all of it) reset at every stage
  load, PERMANENT small. Nothing is freed individually.
- ROM data is byte-swapped in place as it loads (`port/src/romfile_*.c`). Compiled-in data is little-endian.

## Data comes from the ROM

With the default `GE_ROMDATA_FALLBACK=OFF` the exe contains none of Rare's data: setups, stans, bg, models, text,
animations and music are read from the user's stock US cart at run time (`port/src/romdata.c`, CRC gate 0xB6330846).
Consequence for modders: **editing `assets/obseg/setup/*.c` does nothing in a default build.** Use a FALLBACK build with
`GE_ROMSETUP=0`, or change things at run time (Lua, C hooks). Generated levels (Flatland, the Colosseum, levelgen) are
compiled in on cut stage slots ([levels-and-levelgen.md](levels-and-levelgen.md)).

## Rules that must not break

1. Never block the sim thread; never drop the scheduler's DONE message.
2. Host pointers must be at or above 0x10000000.
3. The build is 32-bit, non-PIE, fixed-base.
4. `tickhz=60` stays bit-identical to the N64. Check with the demo oracle ([tooling-and-testing.md](tooling-and-testing.md)).
5. Anything new is off by default and touches no RNG or sim state while off.
6. Presentation never feeds the sim (above).

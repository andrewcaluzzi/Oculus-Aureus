# Study: an iOS / iPadOS build of Project Midas and the fork

**Status:** a feasibility study and plan. No code has been written for it.
**Measured on:** fork `heist` at 5b7a138, compiled in the `goldeneye` Docker image (gcc 13.3). Line numbers are `~N` on
that commit; they drift, so grep the function name. Web facts were checked on 2026-10-25 and are linked in section 7.

## 1. Summary

**Recommended approach, in five lines:**

1. Make the port **64-bit clean on desktop first** (Linux x86_64), keeping the 32-bit build and the `tickhz=60` demo oracle
   bit-identical. This means expanding ROM data into native-layout structs the way the Perfect Dark port does (`port/src/preprocess/`).
2. Move from SDL2 to **SDL3**, and bring up **macOS arm64** (Apple clang, Mach-O) as the stepping stone. It has the same
   compiler, ABI and Metal as iOS, and a debugger and a desktop window.
3. Add a **native Metal backend** to Fast3D by adapting libultraship's `gfx_metal.cpp` (MIT), whose Fast3D lineage is the
   one our `gfx_rendering_api.h` comes from. ANGLE (GLES3 on Metal) is the quick fallback, because a `gl_es` path already exists.
4. Write a thin **iOS shell**: a CMake `-GXcode` target, lifecycle (pause and save on background), a ROM import into the app's
   Files folder, touch controls feeding the existing port actions and direct-mode look, and gamepads through SDL3/GameController.
5. **Distribute by sideloading.** CI builds an unsigned IPA on a GitHub Actions macOS runner, and users re-sign it with
   AltStore/SideStore/Sideloadly. The App Store is not realistic (guideline 5.2.1), and TestFlight only works for a private group.

**Overall effort: about 22-42 person-weeks** (roughly 5-10 months for one developer). **About half of it is the 64-bit work,**
which does not depend on the platform. It benefits Linux x86_64 without multilib, Apple Silicon macOS and Linux arm64, and
it can go upstream.

**Biggest risk:** the 64-bit data-model change. Setup object records loaded from the ROM *are* the runtime objects and
carry pointer fields (`ObjectRecord.prop/model`, `src/bondtypes.h` ~2702). Model display lists are 8-byte big-endian words
in the ROM but 16 bytes on a 64-bit host. About 1,100 sites are flagged by the compiler, and an unknown number are silent
(explicit `(u32)(uintptr_t)` casts, `u32` arrays holding pointers). All of it has to change without moving one bit of the
`tickhz=60` sim. Second risk: **netplay between an iPhone and a PC does not work today** even between Windows and Linux,
because the libm results differ (`lab/notes/netplay.md` ~423, `lab` branch).

## 2. Where the codebase stands

### 2.1 The 32-bit lock (measured)

`CMakeLists.txt` ~4-5 says the build is "32-bit first, which defuses the codebase's pointer<->s32 casts and keeps Gfx 8
bytes". `-m32` is in the global options (~11), the link (~49, ~54), the asset-object flags (~179), and the Lua and ENet
libraries (~429, ~446). [docs/port/abi-and-portability.md](../docs/port/abi-and-portability.md) has the static analysis;
this section adds numbers.

**Method.** Every one of the 339 translation units in `build_pc/compile_commands.json` (a Linux `GE_VIDEO` build: game,
engine, port layer, fast3d, Lua, ENet) was re-run with `gcc -fsyntax-only`, once with `-m64` and once with `-m32`. `-w`
was dropped and `-Wpointer-to-int-cast -Wint-to-pointer-cast -Wint-conversion` were added. Results were de-duplicated by
(category, file, line), so a header counts once. The script is not in the repo (it lived in the session scratchpad); it
is about 60 lines of Python over `compile_commands.json`.

| Diagnostic (unique sites) | `-m64` | `-m32` | What it means on 64-bit |
| --- | ---: | ---: | --- |
| `-Wpointer-to-int-cast` (pointer cast to a 32-bit int) | **334** | 1 | truncates the address |
| `-Wint-to-pointer-cast` (32-bit int cast to a pointer) | **222** | 1 | rebuilds a pointer from a truncated value |
| `-Wint-conversion` (implicit pointer <-> int, mostly mismatched prototypes) | 516* | **541** | the 32-bit build tolerates these silently; every one is a truncation on 64-bit |
| hard errors | **60** | 0 | 59 in `src/game/propobj.c` (pointers inside `u32` static initialisers), 1 in fast3d |
| TUs that fail to compile | 3 | 0 | `propobj.c`, `gfx_pc.cpp`, `gfx_opengl.cpp` |

\* lower only because `propobj.c` stops at its errors; use the `-m32` figure.

**About 1,100 compiler-visible sites in 86 files.** Where they are:

- `src/game` (Rare's code): about 970. The biggest are `front.c` (163, nearly all text pointers passed to `s32` parameters
  of `frontPrintText`/`textRender`), `options.c` (98), `image_bank.c` (83), `propobj.c` (50 + 59 errors: monitor
  animation scripts are `u32[]` with embedded pointers, `MONJUMPTO(...)` ~2745), `chraction.c` (60), `bondview2.c`
  (72), `bg.c` (54) and `model.c` (46).
- `src/*.c` engine and libultra: about 80 (`music.c`, `ramrom.c`, `init.c`, `memp.c`, audio `load.c`).
- `port/src`: **30** (`audio_stubs.c` 11, `audio_pc.c` 8, `libultra_shim.c` 5, `pcsched.c` 3 at the fixed-address arenas
  ~162/230/265, `port_crowd.c` 2). The fork's Lua, co-op, netplay, bot and mode code adds almost none.
- `port/fast3d`: one real error. `system.h` ~12 `typedef int64_t s64` clashes with ultratypes' `long long` on LP64 (a
  one-line fix). The renderer already uses `uintptr_t` words (`seg_addr(uintptr_t)`, `gfx_pc.cpp` ~7133), inherited from
  the PD/libultraship lineage.

**Silent sites the compiler cannot see:**

- **24 explicit `(u32)(uintptr_t)` / `(unsigned int)(uintptr_t)` casts**, in `romfile_setup.c` (7, e.g. ~295),
  `romfile_stan.c` (4, ~77-84 write host pointers into 32-bit stan fields), `port_snap.c` (3), `romdata.c` (2, ~939-955
  text table), `romfile_model.c` ~419, `port_textures.c`, `bg.c` (3), `gunfire.c` (2) and `propobj.c`.
- **183 `osVirtualToPhysical`/`K0` address-arithmetic uses** in 35 `src/` files (`explosion.c` 20, `model.c` 20,
  `options.c` 22, `bondview2.c` 16). Most are identity maps on PC, but each does `u32` arithmetic on a pointer.
- **`PROMOTE`** in `modelPromoteNodeOffsetsToPointers` (`src/game/model.c` ~5940-5951) rebases pointer fields with
  `(u32)var + diff`.
- **`sizepropdef`** (`src/game/loadobjectmodel.c` ~50) walks the setup prop list with `sizeof(ObjectRecord)/4`-style word
  strides. On 64-bit, `sizeof` grows while the ROM bytes do not, so the walk breaks unless the list was expanded first.
- **About 119 struct types (about 300 pointer fields)** in `src/bondtypes.h` and `src/game/*.h` (a rough parse). Only the
  ones that overlay ROM bytes are a layout problem. The known ones are setup props/pads/paths/AI lists, stan tiles, bg rooms,
  model nodes and records, and audio banks (`audio_stubs.c` `alBnkfNew`). The rest only need correct types.
- `long`: 142 uses outside `long long` in `src/` and `include/`. On LP64 a `long` is 8 bytes; Windows x64 (LLP64) and
  Apple arm64 (LP64) differ here. The PD port spent commits on exactly this ("port: 64-bit: replace another long").
- The scaffolding exists but is unused. `IS_64_BIT` (`include/platform_info.h`) only gates the typed members of the `Gfx`
  union (`include/PR/gbi.h` ~1741). `DOUBLE_SIZE_ON_64_BIT` is defined and never used. `port_textures.c` ~367/778 refuses
  external textures when `sizeof(void *) > 4`.

### 2.2 Address-space conventions that change

- `PORT_IS_HOST_ADDR(v)` is `(unsigned int)v >= 0x10000000` (`port/include/port_hostptr.h` ~29). The memp arena is
  `VirtualAlloc`/`mmap`'d at the hint 0x20000000 (`pcsched.c` ~393) and the high arena at 0x30000000+ (~162). On 64-bit
  Apple platforms the arm64 `__PAGEZERO` reserves the whole low 4 GB (well known, not tested here). So "map the game under
  4 GB and keep 32-bit pointers" is not available. The good news: every host pointer is then above 0xFFFFFFFF, and a
  segment word (0x0Sxxxxxx) can never be mistaken for one. The test becomes `v > 0xFFFFFFFF` on a 64-bit word.
- **The compiled-in asset pipeline** (`CMakeLists.txt` ~153-280) compiles the kept assets (three menu models and the
  generated Flatland, Colosseum and levelgen levels) with `-malign-data=abi -fno-toplevel-reorder`. GNU `objcopy
  --add-symbol` then plants the file symbol, and `load_object_fill_header` finds data by N64 file offsets. On 64-bit the
  pointer fields widen, so those offsets no longer hold. On Mach-O the GNU `objcopy` step also has no direct equivalent.
  The simplest fix is to make the generators (`tools/gen_flatland.py`, `gen_colosseum.py`, `gen_level.py`) emit ROM-format
  big-endian blobs and load them through the same converter as ROM files. Then there is one path to keep 64-bit-correct.
- `port/src/segments.c` and `ramrom_data.c` use top-level `__asm__(".section .bss" ...)` blocks (about 6 outside
  `GE_ROMDATA_FALLBACK`) that Mach-O's assembler will not take. Replace them with aligned C arrays. This is small.
- **Rollback snapshots** hold raw pointers and the sim-statics table comes from GNU ld's map file
  (`scripts/port/snap_embed.py`). Apple's ld64 writes a different map format, so the script needs a second parser.
- The crash handler walks the EBP chain (x86 only, `port/src/main.c`). On iOS, use the system crash reports instead.
  The stall watchdog uses Win32 `CreateThread` (`main.c` ~468).

### 2.3 Renderer

- Fast3D interpreter (`gfx_pc.cpp`, 8,276 lines) plus one backend, `gfx_opengl.cpp` (3,345 lines). It requires GL 2.1 /
  GLSL 1.30 (~1518), but has **a working-looking GLES branch** (`gl_es`, ~130, ~1506, `precision mediump float;` headers,
  VAO use under core/ES), inherited from the PD port, which runs on GL 3.0 / ES 3.0. It has never been exercised in this
  repo (unverified).
- The backend interface is `port/fast3d/gfx_rendering_api.h`: a C function-pointer struct in libultraship's pre-2024 shape
  (`create_framebuffer`, `update_framebuffer_parameters`, `draw_triangles`, ...). It also has fork/upstream extensions:
  the **retained room cache** (`retained_*`) and **`resolve_n64_aa`**. Both are documented as "NULL on backends without
  it", so a new backend can start without them and fall back to the CPU path.
- `video.c` (5,892 lines) can run a render thread (`ge-render`, ~5106-5330) when `tickhz != 60`.
- SDL2 use: about 1,900 `SDL_*` references, mostly `input_pc.c` (1,043), `video.c` (514), `gfx_sdl2.cpp` (231),
  `audio_pc.c` (54), `port_soundfile.c` (43) and `raphnet.c` (27).

### 2.4 Platform layer facts that help

- **One sim thread with no blocking threads** (`libultra_shim.c` ~869: libultra threads are bookkeeping, queues pump
  events). A plain loop on the iOS main thread is fine, so the main-callback rewrite that SDL3 recommends can wait.
- **ROM handling is ready.** `romdata.c` accepts .z64/.v64/.n64 and checks the whole-image CRC32 0xB6330846 (~97-99). The
  exe carries no Rare data with `GE_ROMDATA_FALLBACK=OFF`. On iOS it only needs a new search location.
- **Paths:** 77 `portRootPath` calls put the ini, saves, logs and `Data/` next to the exe. iOS needs a split: read-only
  bundle resources (`Data/Scripts`, shaders) and a writable Documents / Application Support root.
- **Input:** look is already "direct-mode degrees per tick", carried as integer milli-degrees in netplay
  (`portCoopLookSet/Take`, [player.md](player.md)). Port actions (next weapon, reload, use, crouch, next inventory) are
  edge-latched in `joy_port.c`. Touch controls can feed both without touching the sim.
- **Lua 5.5** is a pure interpreter, so no JIT is needed. It runs in a 20 MB fixed block. ENet uses BSD sockets.

## 3. Options compared

| Option | What it is | Effort (person-weeks) | Risk | Modernity / maintenance | Fit with the fork (Lua, netplay, bot, modes) |
| --- | --- | --- | --- | --- | --- |
| **(a) 64-bit clean + SDL3 + native Metal backend** | the plan in section 4 | **22-42** | high in P1 (data model), moderate after | Metal is Apple's first-class API; SDL3 is current; libultraship already went SDL3 + Metal on iOS | everything carries over; all of it is in `src/`/`port/`, which stays |
| (b) 64-bit + ANGLE (GLES3 on Metal) | as (a) but ship `gfx_opengl.cpp`'s `gl_es` path on Google's ANGLE | 20-38 (saves 2-4 on P3) | low on the renderer, plus a heavy dependency (~10+ MB) and one more translation layer | ANGLE is maintained (it is WebGL in Safari and Chrome); native GLES on iOS is deprecated since iOS 12 but still present on 26.x | full; also the quickest first frame on a device; a reasonable *first* milestone, then Metal |
| (c) adopt libultraship wholesale | replace `port/fast3d` and the window/input layer with LUS (SDL3, Metal, DX11, OpenGL; iOS and Android build docs) | 30-50 | high: LUS expects its resource archive (.o2r/.otr) and its own `ResourceManager`, ImGui and console; the fork's renderer extensions (room cache, N64 AA, crowd, CCTV feeds) would be redone | best maintained of all, with an active community | Lua, netplay and modes survive but every renderer hook is re-ported; P1 is still needed because LUS does not make GE's `src/` 64-bit |
| (d) static recompilation (N64Recomp + RT64) | recompile the ROM's MIPS code to C; GE has a project ("GoldenRecomp", KholdFuzion and SonicDcer) | unknown for GE; the iOS port of RT64 itself is unstarted | very high: RT64 has Metal on macOS only, and iOS is unsupported; the recomp mod runtime may compile mods at load time (unverified, possible JIT problem on iOS) | modern and maintained for Zelda/Star Fox | **poor**: no 64-bit problem (guest pointers stay 32-bit offsets into RDRAM), but every fork feature is written against the decomp's `src/` and would be rewritten as recomp patches; Lua, netplay and rollback would start over |
| (e) run the 32-bit build under translation | no 32-bit ARM since iOS 11, no Rosetta on iOS, no JIT for App Store apps | impossible | - | - | - |
| (f) do nothing: an N64 emulator | users run the cart in an emulator (Delta and others on the App Store) | 0 | - | - | none of the fork's features |

**Why (a), with (b) as a stepping stone allowed:** Metal is the only API Apple keeps improving on iOS. libultraship
shows the Fast3D-on-Metal design works on iPhone (HarkinianPad, a community iOS port of Ship of Harkinian, renders through
it at 60 fps). An SDL3 GPU backend (Metal on Apple, Vulkan/D3D12 elsewhere) would be the most portable choice, but Fast3D
generates shaders at runtime per combiner. Under SDL_GPU that means emitting MSL text on Apple and SPIR-V elsewhere, so it
is "Metal backend plus more". It is worth it only if upstream wants one backend for every platform. MoltenVK (Vulkan on
Metal) would need a Vulkan backend first, and we have none.

## 4. Recommended plan

Estimates are for one developer who knows the codebase (AC with agents), and include testing. Each phase ends with
something that runs.

### P1: 64-bit clean on desktop (10-20 person-weeks, the biggest phase)

Target: a Linux x86_64 build (no multilib) that passes the demo oracle **bit-identical to the 32-bit build** at
`tickhz=60`. The 32-bit Windows build stays the default until then.

1. **Types and prototypes (2-4 weeks).** Fix the ~1,100 flagged sites. Most are mechanical: `s32` parameters that take
   pointers (`frontPrintText`, `textRender`), `u32` locals holding pointers become `uintptr_t` or the right pointer type,
   the 142 `long`s, and `propobj.c`'s monitor scripts (pointer slots in `u32[]`; make them a struct or a table of indices).
   Keep `-Wpointer-to-int-cast -Wint-to-pointer-cast -Wint-conversion` as errors in CI on both widths so nothing regresses.
2. **ROM data to native layout (4-8 weeks).** Follow the Perfect Dark port's `port/src/preprocess/` design
   (`filesetup.c`, `filemodel.c`, `filebg.c`, `filetiles.c`, `filepads.c`, `filelang.c`, `gbi.c`, `segaudio.c`,
   `segfonts.c`, `misc.c`, `common.c`). Each file reads the big-endian N64 record field by field and writes a host struct
   into a new buffer, so pointer fields widen, `sizeof` strides match, and display lists expand from 8 to 16 bytes per
   `Gfx`. Today's `romfile_setup.c`/`romfile_stan.c`/`romfile_model.c`/`romfile_bg.c` (about 1,300 lines) swap in place.
   They become expanders. The expanded buffers must come from the sim heap so rollback still covers them.
3. **Compiled-in levels and menu models (1-2 weeks).** Make the generators emit ROM-format blobs and drop the
   `objcopy --add-symbol` and file-offset contract (section 2.2).
4. **Address conventions (1-2 weeks).** A 64-bit `PORT_IS_HOST_ADDR`, arenas without fixed addresses, `port_snap.c`
   ranges in `uintptr_t`, a second linker-map parser for `snap_embed.py`.
5. **Hunting silent truncations (1-3 weeks, the uncertain part).** Run the 64-bit build under ASan/UBSan (the Linux
   `GE_SANITIZE` build already exists) through every stage's demo, the arena and heist tests, and the bot. A useful trick:
   in a test build, reserve the low 4 GB on Linux so any truncated pointer faults at once instead of aliasing.
6. **Determinism check (1 week).** The state hash and the demo oracle must match the 32-bit build. Fields that hash
   pointers, if any, need canonical values. AI lists and paths reached by pointer are a likely place to find them.

**Can go upstream:** all of P1. Upstream runs on "Windows and Linux", 32-bit only; 64-bit cleanliness gives them Linux
without multilib, Apple Silicon macOS and Linux arm64. Offer it in reviewable slices: types first (no behaviour change,
oracle identical), then each converter.

### P2: SDL3 and macOS arm64 (2-4 person-weeks)

- **SDL2 to SDL3** across the ~1,900 references (SDL ships a rename script and a migration guide; `sdl2-compat` can carry
  the old API during the switch). The changes that matter are events, gamepad naming, audio streams, and window sizes in
  points vs pixels (high DPI).
- **macOS arm64 with Apple clang.** Replace the asm `.section` blocks. Gate `-msse2 -mfpmath=sse` to x86, and **add
  `-ffp-contract=off`**: clang fuses multiply-adds on arm64 by default, which changes float results and would break the
  oracle. Build a `.app` bundle.
- On macOS, P2 can first run on the existing OpenGL backend (Apple's deprecated GL 4.1 core; the shaders need a
  `#version 150`/`330` header there instead of `130`). That gives a working Mac build before Metal exists.

**Can go upstream:** SDL3 and the macOS build.

### P3: Metal renderer (3-6 person-weeks; or 1-2 for ANGLE)

- Port libultraship's `src/fast/backends/gfx_metal.cpp` (about 1,000 lines, metal-cpp, by David Chavez, MIT) and
  `gfx_metal_shader.cpp` (the combiner-to-MSL generator) onto our C `GfxRenderingAPI` struct. Remove LUS's ImGui,
  spdlog and console-variable dependencies. Our combiner-feature struct (`gfx_cc.h`) is the older form, so the MSL generator
  is the part that needs real rework.
- Order: triangles and textures, then framebuffers (CCTV feeds, the watch, depth extraction), then `resolve_n64_aa`,
  then the retained room cache. Leave the last two NULL at first.
- **ANGLE alternative:** link ANGLE's Metal backend, create a GLES 3.0 context, and run `gfx_opengl.cpp`'s `gl_es`
  branch. It is quicker but adds a large binary, a second shader translator, and harder GPU debugging.

**Can go upstream:** the Metal backend (it helps macOS users too).

### P4: the iOS shell (6-10 person-weeks)

- **Build:** CMake `-GXcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0` (the same recipe as
  libultraship's README), SDL3 as an xcframework, an Info.plist and an asset catalog. A Mac with Xcode is needed for the
  build and for signing; a GitHub Actions macOS runner can stand in for one (section 6).
- **Lifecycle:** on `SDL_EVENT_WILL_ENTER_BACKGROUND`, pause the sim (open the watch / stop ticking), stop GPU work and
  write the save. `DID_ENTER_BACKGROUND` leaves about 5 seconds. Resume on `DID_ENTER_FOREGROUND`. Handle
  `SDL_EVENT_LOW_MEMORY`. Set the AVAudioSession category (ambient, so the user's music can keep playing, or playback).
- **ROM import:** expose the app's Documents folder to Files (`UIFileSharingEnabled` + `LSSupportsOpeningDocumentsInPlace`),
  the way HarkinianPad does, and also offer a `UIDocumentPicker` import. Copy the ROM into the container, check the
  CRC32, and never ship or download it. `romdata.c` only needs that folder added to its search list.
- **Paths:** split `portRootPath` into bundle (read-only) and user (Documents) roots, and audit the 77 call sites. Bundle
  the stock `Data/Scripts` modes, and let users drop extra Lua mods into a Files-visible `Mods/` folder.
- **Display:** a full-screen landscape window sized in pixels (`SDL_GetWindowSizeInPixels`), HUD and touch layer inside
  the safe area (notch and Dynamic Island), and `UILaunchScreen` set, or iOS falls back to a low-resolution
  compatibility mode. **120 Hz ProMotion** needs `CADisableMinimumFrameDurationOnPhone` in Info.plist on iPhone (Apple
  docs, not tested here) and the fork's `tickhz` path (the classic 60 stays the default).
- **Input:** touch controls (section 5); gamepads (MFi, Xbox, PlayStation, Switch Pro) through SDL3 on Apple's
  GameController framework, which needs no HIDAPI or Bluetooth permission. Rumble works on supported pads; gyro through
  SDL sensors on pads that report it.
- **Networking:** see section 6.3.

### P5: distribution and CI (1-2 person-weeks)

A GitHub Actions macOS workflow builds an unsigned IPA from source. There is no ROM data in the build: the exe already
carries none of Rare's data (`GE_ROMDATA_FALLBACK=OFF`). Users re-sign it with their own Apple ID. Write user docs for
ROM import and sideloading. TestFlight is an option for a private group (section 6).

### Effort table

| Phase | Person-weeks | Notes |
| --- | ---: | --- |
| P1 64-bit clean (desktop) | 10-20 | the uncertain one; upstreamable |
| P2 SDL3 + macOS arm64 | 2-4 | upstreamable |
| P3 Metal backend | 3-6 (ANGLE: 1-2) | upstreamable |
| P4 iOS shell, touch, lifecycle | 6-10 | touch UX iteration is most of it |
| P5 distribution, CI | 1-2 | |
| **Total** | **22-42** | 20-38 with ANGLE instead of Metal |

For calibration: sm64ex-ios took its author about a week for the iOS build and a few more weeks for touch controls and
menus, on a codebase that was already 64-bit clean with SDL and GL ES. HarkinianPad sits on libultraship, which already
had iOS and Metal. We have neither of those head starts, which is why P1-P3 dominate.

## 5. Touch controls for an N64 FPS on a phone

The sim does not need to change. Touch feeds the same three channels a PC pad does: the move stick, direct-mode look
degrees, and port actions ([player.md](player.md)).

- **Move:** a floating, fully analogue left stick (it appears where the thumb lands, in the left half). HarkinianPad's
  stick is eight-way and its README calls that a limitation, so avoid copying it. The PC pad path already strafes with
  the move stick, so this behaves like the 2.x two-stick styles.
- **Look:** a relative drag in the right half, like a mouse, with sensitivity, an optional acceleration curve and
  invert-Y. It goes into the same per-tick degrees as the mouse, so it stays deterministic under lockstep (milli-degree
  records). Offer **gyro aim** (CoreMotion through SDL sensors) as an addition, the way Splatoon and the Zelda recomp's gyro
  aim do.
- **Fire:** a large button at the right thumb that also accepts a drag, so you can look while firing. Optional "fire
  while a second finger is down anywhere on the right".
- **Aim mode (R):** a toggle button. While aiming, the drag moves the crosshair and the C-button lean/zoom map to small
  buttons or a pinch.
- **Context:** B = use/reload, A = next weapon (swipe on the weapon icon for previous/next), next-inventory for gadgets,
  crouch, and Start/watch at the top centre, clear of the system gestures.
- **Autoaim on by default on touch.** GoldenEye's own autoaim (`chrpropUpdateAutoaimTarget`) does much of the work a
  touch shooter's aim assist would.
- **Menus and the watch:** they are controller-driven. Show an on-screen d-pad, A and B while a menu is up, or map taps on
  menu items to cursor moves (this is more work, so do it later).
- **Layout editor:** move and resize buttons, opacity 25-100 %, separate phone and tablet profiles, all inside the safe
  area. Hide everything when a gamepad connects.
- **Haptics:** a short tap on fire and on hits, using Core Haptics on the phone. SDL3's haptics cover pads; phone haptics
  may need a small Objective-C shim (unverified for SDL3).

## 6. Distribution and legal

### 6.1 Channels

| Channel | Works? | Notes |
| --- | --- | --- |
| App Store | **no, realistically** | Guideline 5.2.1: "don't use protected third-party material such as trademarks, copyrighted works ... without permission". The game code, characters, names and the GoldenEye/007 marks belong to Rare/Nintendo and the Bond rights holders. Guideline 4.7 lets *emulators* download games, but this is a native port, not an emulator, and 4.7 does not cover the IP anyway. |
| TestFlight | for a private group only | Internal testers (people on the developer account) need no beta review. External testers do need a review, so the 5.2.1 problem returns. Needs the paid developer program. |
| Sideload an unsigned IPA (AltStore Classic, SideStore, Sideloadly) | **yes, the realistic channel** | This is what HarkinianPad does: CI or the user's own Mac builds the IPA, and the user signs it with their own Apple ID. A free Apple ID signs for 7 days (then refresh); a paid account signs for a year. SideStore can refresh on the device. |
| EU alternative marketplaces (AltStore PAL, others) | unlikely | They need Apple's notarization and a developer who owns or has licensed the content. |
| Build from source in Xcode | yes | Needs a Mac. Document it for technical users. |

**Is a Mac required?** Building and signing need Xcode, which only runs on macOS. A free GitHub Actions macOS runner can
build the unsigned IPA (HarkinianPad documents this "GitHub's free Mac" route), so contributors and users without a Mac
can still get an IPA. Development and debugging on a device realistically need a Mac (or a rented cloud Mac). Apple also
moves the minimum Xcode/SDK for App Store uploads every spring; that does not matter for sideloading.

### 6.2 Legal (not legal advice)

Project Midas and the fork are MIT-licensed code built on the decompilation. Rare's game content comes only from the
user's own cart image, and nothing ROM-derived is in the repo or the IPA. Keep it that way on iOS: no ROM download, no
bundled assets, a CRC check that accepts the stock US cart only, no GoldenEye logo as the app icon, and a neutral app
name (Project Midas / Oculus-Aureus). This is the same posture as sm64ex-ios and HarkinianPad. It does not make App Store
distribution possible; it only keeps the sideload build clean.

**Lua mods and guideline 2.5.2** ("may not download, install, or execute code which introduces or changes features").
Lua is interpreted, so there is no JIT or W^X issue on iOS. Bundled modes are fine. Downloadable or user-added mods would
be a review problem on the App Store, which is moot for sideloading.

### 6.3 Networking (ENet, co-op, netplay)

- ENet's BSD UDP sockets work on iOS unchanged. **The Local Network privacy prompt** (iOS 14+) appears on the first LAN
  traffic; add `NSLocalNetworkUsageDescription` (and `NSBonjourServices` if discovery is ever added). Peers on the
  internet need no permission.
- **There is no NAT traversal.** `port_net.c` has no hole punching, relay or STUN. A phone on cellular (carrier-grade
  NAT) cannot host, and joining a PC host by IP works. A relay or punch-through would be new work.
- **Backgrounding suspends the app within seconds,** so the partner stalls. Treat "peer went to background" as an
  explicit pause message, and expect sessions to end on a phone call.
- **Phone-to-phone netplay** should work after P1 (the same binary on the same architecture). **Phone-to-PC netplay does
  not**, for the same reason Windows-to-Linux does not today: the libm `sinf`/`cosf` results differ between C runtimes
  (`lab/notes/netplay.md` ~423, `lab` branch; the game links libm's `sinf`/`cosf`, `CMakeLists.txt` ~92-94). Fixing it means vendoring
  one deterministic float maths library for every platform, plus `-ffp-contract=off` everywhere. Lua tables iterated by
  pointer-keyed order could also differ between 32-bit and 64-bit builds (unverified). That fix also benefits Windows-Linux
  play, so it is a good upstream or fork task on its own.
- Rollback snapshots hold raw pointers and are same-exe-only already. They do not cross devices; that is fine.

## 7. Unverified and open

- Whether `gfx_opengl.cpp`'s `gl_es` branch renders correctly (never run in this repo).
- The exact number of silent truncation sites; the 24 explicit casts and 183 `K0`/physical-address uses are counted, but
  ROM-overlay struct fields are known only for the classes listed in 2.1.
- Whether any state-hash field hashes a pointer value (it would differ between 32-bit and 64-bit builds and needs a
  canonical value).
- The arm64 Darwin 4 GB `__PAGEZERO` is widely documented but was not tested here.
- iOS 120 Hz key, SDL3 phone haptics, and whether the N64Recomp mod runtime needs JIT: from documentation and memory, not
  tested.
- GoldenRecomp's current state (the 2025-2026 press coverage says it boots, with multiplayer unfinished).
- OpenGL ES on iOS 26: deprecated since iOS 12 and reported still present on iPadOS 26.1 (a developer forum post).

## 8. Sources

- Perfect Dark port (64-bit, macOS arm64, Switch; GL 3.0/ES 3.0): <https://github.com/fgsfdsfgs/perfect_dark>; its ROM
  preprocessors: <https://github.com/fgsfdsfgs/perfect_dark/tree/port/port/src/preprocess>,
  <https://raw.githubusercontent.com/fgsfdsfgs/perfect_dark/port/port/src/preprocess/filesetup.c>
- libultraship (MIT; SDL3, Metal for macOS/iOS, iOS and Android build steps): <https://github.com/Kenix3/libultraship>,
  backends <https://github.com/Kenix3/libultraship/tree/main/src/fast/backends>
- HarkinianPad (Ship of Harkinian on iOS, Metal, Files import, sideload): <https://github.com/chrissotraidis/harkinianpad>;
  coverage <https://www.tomshardware.com/video-games/retro-gaming/developer-uses-ai-to-bring-a-native-version-of-the-legend-of-zelda-ocarina-of-time-to-ios-no-emulation-required-to-play-one-of-gamings-all-time-greats-on-an-iphone-or-ipad>
- sm64ex-ios: <https://github.com/ckosmic/sm64ex-ios>; <https://9to5mac.com/2021/11/16/developer-ports-super-mario-64-to-the-apple-tv-heres-how-you-can-try-it-video/>
- RT64 (D3D12, Vulkan, Metal; Windows, Linux, macOS): <https://github.com/rt64/rt64>; Zelda64Recomp:
  <https://github.com/Zelda64Recomp/Zelda64Recomp>
- GoldenRecomp: <https://www.androidauthority.com/goldeneye-007-native-recompilation-3547671>
- Project Midas upstream (Windows and Linux, 32-bit): <https://github.com/murkantor/Project-Midas>
- SDL3 GPU (Metal on iOS 13+, A9+; simulator unsupported): <https://wiki.libsdl.org/SDL3/CategoryGPU>; SDL3 iOS notes:
  <https://github.com/libsdl-org/SDL/blob/main/docs/README-ios.md>
- App Review Guidelines (2.5.2, 4.7, 5.2.1): <https://developer.apple.com/app-store/review/guidelines/>; the 2024 emulator
  change: <https://www.macworld.com/article/2290431/apple-now-allows-retro-game-emulators-in-the-app-store.html>,
  <https://appleinsider.com/articles/24/08/01/pc-emulators-are-now-explicitly-allowed-on-the-iphone-and-ipad>
- AltStore / AltStore PAL: <https://altstore.io/>; SideStore on iOS 26:
  <https://iphonesoft.fr/2026/03/11/guide-ios-26-installer-sidestore-livecontainer-sideloader-apps-jailbreak>
- OpenGL ES on iOS (deprecated, still present): <https://developer.apple.com/forums/tags/opengl-es>; MetalANGLE:
  <https://www.sourceforge.net/mirror/metalangle>
- In-repo: [docs/port/abi-and-portability.md](../docs/port/abi-and-portability.md), [engine-overview.md](engine-overview.md),
  [determinism-and-netplay.md](determinism-and-netplay.md), [player.md](player.md)

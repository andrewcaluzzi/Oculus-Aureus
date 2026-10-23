# Project Midas: arena and Lua modding fork

This is an **experimental fork** of [Project Midas](https://github.com/murkantor/Project-Midas), the PC port of the GoldenEye 007 decompilation. It adds:

- **Lua scripting and a game-mode API.** An embedded Lua 5.5 lets scripts register events, spawn characters and pickups at runtime, draw HUD text, add zones and end the mission. See [modding/lua.md](modding/lua.md).
- **The Colosseum arena** (headline feature). A generated gladiator pit on a cut multiplayer stage slot, with a crowd, and a wave-survival mode written entirely in Lua: waves of mixed enemies, a boss every 5 waves, an ammo-and-armour economy, 20 waves or endless. See [modding/arena.md](modding/arena.md).
- Spawning, factions, AI lists and thrown knives for any character, a Mods menu page with a game-mode picker, choosable level music, a hostage-rescue example mode, and a flat test level ("Flatland").
- **Experimental, off by default:** a second Bond (local co-op), lockstep netplay over UDP (ENet), and rollback snapshots. These work in the test setups they were built in but are not polished. See [Co-op and netplay](#co-op-and-netplay-experimental).

### Why a fork and not a pull request?

Upstream asks for contributions rather than forks, and normally that is the right call. The exception here is Lua: official Lua modding support is already coming to Project Midas. I integrated Lua 5.5.1 so I could muck around with scripting, get familiar with the port and try out a wave-survival arena, and I did not want to open a PR that adds a second, competing Lua integration while upstream's is in progress.

So this fork is a place to share builds and let people have a bash at the arena, not a replacement for upstream. When Lua modding is officially supported, the plan is to rewrite the arena (and anything else worth keeping) against the official API and submit it upstream properly. Until then, expect this fork's Lua API to differ from upstream's.

The fork branches from upstream commit `4b5ca71`. Upstream's own README and docs still apply for everything not listed here; this file covers what is different. Feedback and feature ideas for the arena are welcome as issues on this fork.

No game data is included in this repository or in any build of it. See the next section.

## You need your own ROM

Project Midas reads all game data from your own GoldenEye 007 (USA) cartridge image. It is never distributed here and must not be committed. Use a **z64 (big-endian) image** with this SHA-1:

```
abe01e4aeb033b6c0836819f549c791b26cfde83
```

- For building: put it in the repository root as `baserom.u.z64` (git ignores `*.z64`).
- For playing: put a copy in the build output as `Data/baserom.u.z64`, next to the exe.

The Colosseum and Flatland are generated from scratch by `tools/gen_colosseum.py` and `tools/gen_flatland.py` (geometry and texture ids only, no ROM bytes); their textures and everything else the arena draws come out of your ROM at run time.

## Build

The build runs in Docker. Everything below runs in a Linux shell; on Windows use WSL (see the upstream README) and keep the clone on WSL's own filesystem if you can.

1. Build the image once. It is the repo's `Dockerfile` (Ubuntu 24.04, 32-bit gcc, mingw-w64 i686, 32-bit SDL2 2.30.11):
   ```
   docker build --network=host --build-arg HOST_UID=$(id -u) --build-arg HOST_GID=$(id -g) -t goldeneye .
   ```
2. Put `baserom.u.z64` in the repository root, then extract and convert the assets (once):
   ```
   docker run --rm -v $(pwd):/home/dev/project goldeneye ./scripts/extract_baserom.u.sh
   docker run --rm -v $(pwd):/home/dev/project goldeneye make convert_props convert_chrs convert_guns combine_images
   ```
3. Configure and build the Windows exe:
   ```
   docker run --rm -v $(pwd):/home/dev/project goldeneye cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Debug -DGE_VIDEO=ON -DSDL2_MINGW_ROOT=/home/dev/sdl2-mingw/SDL2-2.30.11/i686-w64-mingw32
   docker run --rm -v $(pwd):/home/dev/project goldeneye cmake --build build_win -j$(nproc)
   ```
4. Or the Linux binary:
   ```
   docker run --rm -v $(pwd):/home/dev/project goldeneye cmake -B build_pc -DCMAKE_BUILD_TYPE=Debug -DGE_VIDEO=ON -DSDL2_LINUX_ROOT=/home/dev/sdl2-linux-i686
   docker run --rm -v $(pwd):/home/dev/project goldeneye cmake --build build_pc -j$(nproc)
   ```

**PowerShell note:** PowerShell 5.1 splits `-DFOO=a.b` arguments at the dot, so quote every `"-D..."` argument if you run these from PowerShell.

The build puts the scripts next to the exe: `Data/Scripts/example.lua` and `Data/Scripts/modes/{arena,arena-endless,hostage,hostage-flatland}.lua`. The Windows build also copies `Play Arena.bat` and `Play Arena (Endless).bat` from `launchers/` next to `ProjectMidas.exe`. Finally put your ROM at `build_win/Data/baserom.u.z64` (or `build_pc/Data/`). Copy the whole `build_win` folder to wherever you like to play.

## Play the arena

On Windows, double-click **`Play Arena.bat`** (20 waves, a boss every 5, then you win) or **`Play Arena (Endless).bat`** (waves keep coming until you fall; your wave, kills and time are logged). They set three environment variables and start the exe, so you can do the same by hand or from Linux:

```
GE_STAGE=colosseum GE_SKIPINTRO=1 GE_MODE=arena ./ProjectMidas
```

(`GE_MODE=arena-endless` for the endless one.) The game logs to `Data/Logs/ge_direct.log` on Windows. Settings (difficulty, enemy counts, pickups, ammo timers, health per kill, crowd size) are the settings block at the top of `Data/Scripts/modes/arena.lua`; the source is `tools/mode_arena.lua`. `modding/arena.md` lists them.

Handy while experimenting: `GE_CHEATS=invuln` (cannot die), `GE_CHEATS=invuln,guns`.

Controllers and keyboard + mouse both work. The usual `midas.ini` and `midas_ini_help.txt` apply.

## Write a mode

Mode scripts are plain Lua files in `Data/Scripts/modes/<name>.lua`, run with `GE_MODE=<name>` (or pick one on the new Mods page: Options > Gameplay > Mods). Start from [modding/lua.md](modding/lua.md); it has a complete minimal example.

## Co-op and netplay (experimental)

Treat everything in this section as a research prototype. It works in the scenarios it was tested in (Dam, Facility, Surface, Flatland, the hostage mode) but expect rough edges.

- **Local two-Bond co-op** in a solo mission, with the second Bond driven by a script: `GE_COOP=1 GE_STAGE=dam GE_SKIPINTRO=1 GE_COOP_P2=follow` (also `walk`, `idle`). The console command `coop view 1` switches the camera to the other Bond.
- **Netplay**, two machines or two windows, lockstep over UDP port 27015, same platform on both sides. Host first: `GE_NET=host:27015 GE_STAGE=dam GE_PROFILE=host`; guest: `GE_NET=join:<host-ip>:27015 GE_PROFILE=guest`. `GE_PROFILE` gives each window its own ini, saves and logs. Only the host picks missions; the console command `net info` shows the connection. Allow the exe (UDP 27015) through the host's firewall. Both machines need identical `Data/Scripts` files.
- **Rollback snapshots** exist as groundwork (the exe embeds a table of simulation statics after linking, which needs `python3` at build time) and are not a playable feature.

None of this is on unless you set the variables above. With them unset the sim is untouched: the stock demo replay stays bit-identical (see Tests).

## Tests

`tests/` holds C unit tests for the pure logic, Lua scenario scripts, arena soak presets, and the netplay and rollback harnesses; see `tests/README.md`. The one that matters most: **the demo oracle**. With no mod in play, the game's recorded Dam attract demo must reproduce exactly:

```
GE_DEMO=dam1 GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 ./ProjectMidas | grep '^ge: demo tick' | diff - tests/baseline/dam1.a.txt
```

An empty diff means the changes did not disturb the stock simulation. (On Linux run it under Xvfb: `xvfb-run` or a virtual X server plus `libegl1:i386`.)

## Licence and credits

Same as upstream: the port code is MIT (see `LICENSE`), bundled third-party code keeps its own licence (`tools/THIRD_PARTY_NOTICES.txt`), and this repository contains no GoldenEye assets. This fork adds Lua 5.5.1 (MIT) in `port/lua` and ENet 1.3.18 (MIT) in `port/enet`, both vendored unmodified. See the upstream README for the full list of projects this builds on. Parts of this work were written with Claude (see the commit trailers).

# Knowledge base

What we have learned about how GoldenEye's engine works inside Project Midas, and about the fork's additions on top of
it. It was collected while building the co-op and netplay work, the Colosseum arena, the Bank Heist mode and the
player bot. It is written for modders and for anyone who wants to answer "why does the game do X?" from the code.

This folder explains mechanisms. API references live elsewhere and are linked, not copied:
[`modding/lua.md`](../modding/lua.md) (the Lua API), [`modding/arena.md`](../modding/arena.md) (the Colosseum),
[`modding/levelgen.md`](../modding/levelgen.md) (the level generator), [`designs/bank-heist.md`](../designs/bank-heist.md)
and [`designs/bank-heist-bunker2.md`](../designs/bank-heist-bunker2.md) (the heist), [`tests/README.md`](../tests/README.md).

## What is where

| File | Read it for |
| --- | --- |
| [engine-overview.md](engine-overview.md) | How the N64 game is hosted on a PC: one sim thread, the tick, `tickhz`, the two worlds (sim and presentation), `TARGET_N64`, host pointers, build flags |
| [player.md](player.md) | Bond: the PC input path, movement speeds (measured strafe and speed-strafe), health and armour, how damage reaches him, autoaim |
| [guards-and-ai.md](guards-and-ai.md) | Chrs: lifecycle and slots, the AI bytecode, sight, hearing, how a guard's shot hits Bond, alarms, reactions to deaths, surrender, knock-outs, factions, spawning, routing |
| [objectives-and-setup.md](objectives-and-setup.md) | Setup files, objective types and how their status is computed, stage flags, reading AI scripts, per-level findings (Statue, Bunker 2, photos) |
| [rendering.md](rendering.md) | Fast3D, RSP matrix state, room/portal visibility, why the sim reads render output, the crowd projection bug, camera-feed render walks, headless rendering |
| [audio.md](audio.md) | The audio path, `sndPlaySfx`, voice and sound pools, presentation sounds, the `alAdpcmPull` crash, the WAV mixer |
| [determinism-and-netplay.md](determinism-and-netplay.md) | The demo oracle, the state hash, every nondeterminism cause we found and fixed, lockstep, rollback snapshots |
| [performance.md](performance.md) | `roomGetProps`, dormant chrs, pools that ran dry, benchmark numbers |
| [levels-and-levelgen.md](levels-and-levelgen.md) | Stan tiles, rooms and portals, pads and waypoints, cut stage slots, the generators, Bunker 2's map |
| [lua-modding.md](lua-modding.md) | How the Lua layer and modes hook the engine (a map, not the API) |
| [tooling-and-testing.md](tooling-and-testing.md) | Docker/WSL builds, headless runs, every test harness, a table of `GE_*` variables, disk space |
| [pitfalls.md](pitfalls.md) | Bugs we hit, why they happened, and how to avoid them |

## How to use it

- Looking for a number (a speed, a radius, a timer)? The topic file has a table; each row says where in the code it comes
  from and how it was measured.
- Something behaves oddly? Read [pitfalls.md](pitfalls.md) first, then the topic file.
- Writing new port code? [engine-overview.md](engine-overview.md) "Rules that must not break" and
  [determinism-and-netplay.md](determinism-and-netplay.md) "Patterns to remember".

## Conventions

- **Code references** are `file` + function, with line numbers as `~N`. Line numbers are for the `heist` branch at the
  time of writing and drift; grep the function name.
- **Where a thing lives**, when it matters:
  - **upstream**: Project Midas itself (murkantor/Project-Midas `main`, release 1.0.2b).
  - **fork**: this fork's `heist` branch, which includes the arena work and the experimental co-op/netplay/rollback code.
    All fork changes to `src/` are inside `#ifndef TARGET_N64` and are off unless a mode, a test switch or co-op is on.
  - **bot branch**: the player bot (`bot`), not merged into `heist` yet.
- **Measured** numbers say how they were measured. **(unverified)** marks a claim we could not check in the code.
- Units are GoldenEye world units (roughly a centimetre; Bond's eye is about 150-175 above the floor). Times are in sim
  ticks of 1/60 s unless stated.
- Nothing here contains ROM data. Where a setup file is quoted it is a pad, flag or chr number read from your own
  extracted copy.

## Keeping it current

When you learn something about the code, add it to the topic file in the same change, with the function name. If a
fact turns out wrong, fix it here and say what replaced it.

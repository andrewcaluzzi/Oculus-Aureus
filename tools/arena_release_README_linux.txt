Project Midas - Colosseum Arena (@TAG@), Linux
built from @COMMIT@ (fork branch 'heist', on upstream Project Midas 1.0.2b) on @DATE@

An experimental fork of Project Midas (PC port of GoldenEye 007) with Lua modding and a
wave-survival gladiator arena. https://github.com/andrewcaluzzi/Oculus-Aureus/tree/heist

NEEDS
A 64-bit x86 Linux with 32-bit (i386) libraries: the game is a 32-bit program, like the
Windows one. glibc 2.35 or newer (Ubuntu 22.04, Mint 21, Debian 12, Fedora 36, or newer;
Arch; SteamOS 3). Install the 32-bit SDL2 and OpenGL once:
  Ubuntu, Mint, Debian:  sudo dpkg --add-architecture i386
                         sudo apt update
                         sudo apt install libsdl2-2.0-0:i386 libgl1:i386
  Fedora:                sudo dnf install SDL2.i686 mesa-libGL.i686
  Arch:                  enable [multilib] in /etc/pacman.conf, then
                         sudo pacman -S lib32-sdl2 lib32-mesa
  Steam Deck (SteamOS):  nothing to install, Steam already put the 32-bit graphics and sound
                         libraries there (untested; please report back)
If the system has no 32-bit SDL2, the launchers use the copy in lib/ (X11 and ALSA only).

SETUP
1. Put your own GoldenEye 007 (USA) cart image in the Data folder as Data/baserom.u.z64
   (see Data/README.txt). It is not included and never will be.
2. Run a launcher from a terminal in this folder, for example:  ./"Play Arena.sh"
   (or let your file manager run executable scripts). Running ./ProjectMidas directly plays
   the normal game.
     Play Arena.sh                   the start menu: 20 waves with a boss every 5, or pick a
                                     preset, co-op (and the bot's skill), mutators and more
     Play Arena (Endless).sh         waves keep coming and keep getting harder
     Play Arena (Chaos).sh           20 waves with wave mutators and elite enemies
     Play Arena (Chaos Endless).sh   endless, with mutators and elites
     Play Arena (Co-op).sh           two Bonds: you and a bot partner
     Play Arena (Co-op Chaos).sh     co-op with mutators and elites
     Play Arena (Test Keys).sh       PageDown/PageUp jump waves, Home callouts, End fog, Insert rain (Shift+Insert lightning);
                                     invincible, all guns
     Arena Benchmark.sh, Arena Benchmark (Live).sh   write Data/Logs/arena_bench.txt
     Arena Benchmark (Late Waves).sh   a wave 15-20 scene, the render settings on and off;
                                     writes Data/Logs/arena_bench.txt

If levels draw as a few large blank triangles (seen on some Linux systems), start it with
  GE_FULLHEIGHT=0 GE_EDGEINSET=1 ./"Play Arena.sh"

Enemies drop their guns; ammo boxes and body armour appear in the middle of the pit; every
kill gives back a little health. The crowd keeps a favour meter (HUD line 4): stylish kills
raise it, getting hit, camping and spraying lower it; a happy crowd throws gifts, an angry
one sends more enemies. In co-op a Bond who runs out of health goes down: stand next to him
for 3 s to revive him. Best wave, kills, time and a medal are kept in arena_scores.ini
(solo). The crowd can play your own WAV files (Data/Sounds/README.txt).

All the balance numbers are in the settings block at the top of
Data/Scripts/modes/arena.lua and the _arena_*.lua modules next to it. README-FORK.md has
the full details.

Settings are in midas.ini, written on first run. See midas_ini_help.txt for every setting.
LICENSE.txt covers the Project Midas code; third-party code keeps its own license, see
THIRD_PARTY_NOTICES.txt (lib/libSDL2-2.0.so.0 is SDL2, zlib license).

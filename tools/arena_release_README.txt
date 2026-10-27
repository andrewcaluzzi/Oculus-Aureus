Project Midas - Colosseum Arena (@TAG@)
built from @COMMIT@ (fork branch 'heist', on upstream Project Midas 1.0.2b) on @DATE@

An experimental fork of Project Midas (PC port of GoldenEye 007) with Lua modding and a
wave-survival gladiator arena. https://github.com/andrewcaluzzi/Oculus-Aureus/tree/heist

SETUP
1. Put your own GoldenEye 007 (USA) cart image in the Data folder as Data\baserom.u.z64
   (see Data\README.txt). It is not included and never will be.
2. Double-click a launcher. Running ProjectMidas.exe directly plays the normal game.
     Play Arena.bat                   20 waves, a boss every 5
     Play Arena (Endless).bat         waves keep coming and keep getting harder
     Play Arena (Chaos).bat           20 waves with wave mutators and elite enemies
     Play Arena (Chaos Endless).bat   endless, with mutators and elites
     Play Arena (Co-op).bat           two Bonds: you and a bot partner
     Play Arena (Co-op Chaos).bat     co-op with mutators and elites
     Play Arena (Test Keys).bat       PageDown/PageUp jump waves; invincible, all guns
     Arena Benchmark.bat, Arena Benchmark (Live).bat   write Data\Logs\arena_bench.txt

Enemies drop their guns; ammo boxes and body armour appear in the middle of the pit; every
kill gives back a little health. The crowd keeps a favour meter (HUD line 4): stylish kills
raise it, getting hit, camping and spraying lower it; a happy crowd throws gifts, an angry
one sends more enemies. In co-op a Bond who runs out of health goes down: stand next to him
for 3 s to revive him. Best wave, kills, time and a medal are kept in arena_scores.ini
(solo). The crowd can play your own WAV files (Data\Sounds\README.txt).

All the balance numbers are in the settings block at the top of
Data\Scripts\modes\arena.lua and the _arena_*.lua modules next to it. README-FORK.md has
the full details.

Settings are in midas.ini, written on first run. See midas_ini_help.txt for every setting.
LICENSE.txt covers the Project Midas code; third-party code keeps its own license, see
THIRD_PARTY_NOTICES.txt.

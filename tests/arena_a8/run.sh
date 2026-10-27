#!/bin/sh
# A8 test run inside the build container (Xvfb on :99, build_pc): crowd favour + markers v2 (modding/arena.md "Drop A8").
# Copies the A8 presets next to the mode scripts first.
# usage: run.sh <logname> <mode> [game frames] [cheats | none] [extra env, e.g. "GE_SHOT=100 GE_AUTOFIRE=300"]
# Fixed tick, GE_GFXFRAMES (game frames, not pumps: a busy machine pumps without drawing), no score file writes, no save.
P=/home/dev/project
mkdir -p $P/build_tests/a8/shots
cp $P/tests/arena_a8/*.lua $P/build_pc/Data/Scripts/modes/
# the arena scripts as they are in tools/ (the build copies them only when the exe relinks)
cp $P/tools/mode_arena.lua $P/build_pc/Data/Scripts/modes/arena.lua
cp $P/tools/mode_arena_crowd.lua $P/build_pc/Data/Scripts/modes/_arena_crowd.lua
cp $P/tools/arena_polish.lua $P/build_pc/Data/Scripts/modes/_arena_polish.lua
rm -f $P/build_pc/Data/Saves/eeprom.bin   # a save from an earlier run sends the boot to the file select instead of GE_STAGE
export DISPLAY=:99 GE_MODE=$2 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_GFXFRAMES=${3:-2400} GE_FRAMES=$(( ${3:-2400} * 20 )) GE_FULLHEIGHT=0 GE_EDGEINSET=1
export GE_SCOREFILE=$P/build_tests/a8/scores.ini GE_NOSCORESAVE=1
C=${4:-invuln}
if [ "$C" != none ]; then export GE_CHEATS=$C; fi
for kv in $5; do export "$kv"; done
mkdir -p $P/build_tests/a8/shots/$1
cd $P/build_tests/a8/shots/$1   # GE_SHOT writes shot_NNNNN.bmp here
$P/build_pc/ProjectMidas > $P/build_tests/a8/$1.log 2>&1
echo "exit $?" >> $P/build_tests/a8/$1.log
echo "$1: exit $?"

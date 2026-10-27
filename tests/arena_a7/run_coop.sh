#!/bin/sh
# A7 co-op test run inside the build container (Xvfb on :99, build_pc). Copy the preset into Data/Scripts/modes first (see README in
# modding/arena.md "Drop A7 co-op"), or pass a preset name that is already there.
# usage: run_coop.sh <logname> <mode> [frames] [extra env, e.g. "GE_COOP_GOD=0"]
# Two Bonds (GE_COOP=1), the second one is the arena bot (the preset's coop_bot, default on). Fixed tick, no pacing, no window.
export DISPLAY=:99 GE_MODE=$2 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_TICKHZ=60 GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_COOP=1 GE_SEED=7
export GE_GFXFRAMES=${3:-3600} GE_FRAMES=$(( ${3:-3600} * 20 )) GE_NOMSGBOX=1   # game frames (a busy machine pumps without them)
for kv in $4; do export "$kv"; done
mkdir -p /home/dev/project/build_tests/a7
rm -f /home/dev/project/build_pc/Data/Saves/eeprom.bin   # a save from an earlier run sends the boot to the file select instead of GE_STAGE
/home/dev/project/build_pc/ProjectMidas > /home/dev/project/build_tests/a7/$1.log 2>&1
echo "exit $?"

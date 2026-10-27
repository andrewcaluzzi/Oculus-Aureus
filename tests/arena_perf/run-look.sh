#!/bin/sh
# Look check of the arena render fixes (perf-look.lua), inside the goldeneye image with Xvfb :99 already running (never xvfb-run).
# Shots land in build_pc/look/; the log is build_pc/look/look.log.
cd /home/dev/project/build_pc || exit 1
cp ../tests/arena_perf/perf-look.lua Data/Scripts/modes/perf-look.lua
mkdir -p look
rm -f look/shot_*.bmp
cd look || exit 1
DISPLAY=:99 GE_MODE=perf-look GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_NOINISAVE=1 \
  GE_FRAMES=${FRAMES:-20000} ../ProjectMidas > look.log 2>&1
echo "exit $?"

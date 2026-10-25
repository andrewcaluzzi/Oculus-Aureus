#!/bin/sh
# The generated level's test (designs/bank-heist.md P2.1, modding/levelgen.md), inside the build container with Xvfb on :99 and
# build_pc built: Bond walks room to room, the portal culling, a guard routes through the doorways (tests/levelgen/levelgen_walk.lua).
# Screenshots: build_pc/levelgen_shots/shot_NNNNN.bmp (each follows a "LEVELGEN SHOT <name>" line in the log; GE_FULLHEIGHT=0
# GE_EDGEINSET=1: without them Xvfb shows only the HUD on black). Convert with tests/bmp2png.py.
# usage: run.sh   -> the LEVELGEN lines; the last one is "LEVELGEN DONE <n> passed, <m> failed". Full log: /tmp/levelgen.log
cp /home/dev/project/tests/levelgen/levelgen_walk.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_walk.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
mkdir -p /home/dev/project/build_pc/levelgen_shots
rm -f /home/dev/project/build_pc/levelgen_shots/shot_*.bmp
cd /home/dev/project/build_pc/levelgen_shots || exit 1
DISPLAY=:99 GE_MODE=levelgen_walk GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_FRAMES=${GE_FRAMES:-9000} timeout 400 ../ProjectMidas > /tmp/levelgen.log 2>&1
grep -E "LEVELGEN|gfx: wrote|ge: lua: .*error|setup lint" /tmp/levelgen.log

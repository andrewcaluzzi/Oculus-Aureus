#!/bin/sh
# The Janus bank v2's signature spaces (tests/levelgen/levelgen_bank_shots.lua), inside the build container with Xvfb on :99 and build_pc
# built with tools/levelgen/janus_bank.json on the slot. Screenshots: build_pc/levelgen_bank_shots_v2/shot_NNNNN.bmp, each after a
# "LEVELGEN SHOT <name>" line. Convert with tests/bmp2png.py and delete the BMPs (they are big).
# usage: run_bank_shots.sh   -> the LEVELGEN lines. Full log: ${LOGDIR:-/tmp}/levelgen_bank_shots.log
LOGDIR=${LOGDIR:-/tmp}
cp /home/dev/project/tests/levelgen/levelgen_bank_shots.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_bank_shots.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
mkdir -p /home/dev/project/build_pc/levelgen_bank_shots_v2
rm -f /home/dev/project/build_pc/levelgen_bank_shots_v2/shot_*.bmp
cd /home/dev/project/build_pc/levelgen_bank_shots_v2 || exit 1
DISPLAY=${DISPLAY:-:99} GE_MODE=levelgen_bank_shots GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_FRAMES=${GE_FRAMES:-6000} timeout 900 ../ProjectMidas > $LOGDIR/levelgen_bank_shots.log 2>&1
grep -E "LEVELGEN|gfx: wrote|ge: lua: .*error" $LOGDIR/levelgen_bank_shots.log

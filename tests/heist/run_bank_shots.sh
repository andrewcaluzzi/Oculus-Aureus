#!/bin/sh
# Heist screenshots on the Janus bank (P2.4), inside the build container with Xvfb on :99 and build_pc built with the bank on the slot.
# Writes build_pc/heist_bank_shots/shot_NNNNN.bmp (one per "HEIST SHOT <name>" line in the log). Convert with tests/bmp2png.py and
# delete the BMPs (they are big).
cp /home/dev/project/tests/heist/heist_bank_shots.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_shots.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
mkdir -p /home/dev/project/build_pc/heist_bank_shots
rm -f /home/dev/project/build_pc/heist_bank_shots/shot_*.bmp
cd /home/dev/project/build_pc/heist_bank_shots || exit 1
DISPLAY=:99 GE_MODE=heist_bank_shots GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_FRAMES=${GE_FRAMES:-6000} timeout 500 ../ProjectMidas > ${LOGDIR:-/tmp}/heist_bank_shots.log 2>&1
grep -aE "HEIST|gfx: wrote|ge: lua: .*error" ${LOGDIR:-/tmp}/heist_bank_shots.log

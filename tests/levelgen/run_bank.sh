#!/bin/sh
# The Janus bank (designs/bank-heist.md P2.3, modding/levelgen.md), inside the build container with Xvfb on :99 and build_pc built from
# tools/levelgen/janus_bank.json (python3 tools/gen_level.py tools/levelgen/janus_bank.json, then build): the layout's counts and linked
# double doors, a double door culling, the counter and a shelf blocking, the full route on stick input (stairs down, the service ramp up),
# guards routing between areas (tests/levelgen/levelgen_bank.lua).
# Screenshots: build_pc/levelgen_bank_shots/shot_NNNNN.bmp (each follows a "LEVELGEN SHOT <name>" line in the log). Convert with
# tests/bmp2png.py and delete the BMPs.
# usage: run_bank.sh   -> the LEVELGEN lines; the last one is "LEVELGEN DONE <n> passed, <m> failed". Full log: ${LOGDIR:-/tmp}/levelgen_bank.log
LOGDIR=${LOGDIR:-/tmp}
cp /home/dev/project/tests/levelgen/levelgen_bank.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_bank.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
mkdir -p /home/dev/project/build_pc/levelgen_bank_shots
rm -f /home/dev/project/build_pc/levelgen_bank_shots/shot_*.bmp
cd /home/dev/project/build_pc/levelgen_bank_shots || exit 1
DISPLAY=:99 GE_MODE=levelgen_bank GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_FRAMES=${GE_FRAMES:-45000} timeout 1500 ../ProjectMidas > $LOGDIR/levelgen_bank.log 2>&1
grep -E "LEVELGEN|gfx: wrote|ge: lua: .*error|setup lint" $LOGDIR/levelgen_bank.log

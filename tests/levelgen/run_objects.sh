#!/bin/sh
# The generated level's setup objects (designs/bank-heist.md P2.2, modding/levelgen.md "Objects"), inside the build container with Xvfb
# on :99 and build_pc built from tools/levelgen/objects.json (python3 tools/gen_level.py tools/levelgen/objects.json, then build):
# doors in doorways (keyed, padlocked, the vault door) and their portals, the camera, the alarm box, the monitor, the safe, keys and the
# loot pads (tests/levelgen/levelgen_objects.lua).
# Screenshots: build_pc/levelgen_obj_shots/shot_NNNNN.bmp (each follows a "LEVELGEN SHOT <name>" line in the log). Convert with
# tests/bmp2png.py and delete the BMPs.
# usage: run_objects.sh   -> the LEVELGEN lines; the last one is "LEVELGEN DONE <n> passed, <m> failed". Full log: ${LOGDIR:-/tmp}/levelgen_objects.log
LOGDIR=${LOGDIR:-/tmp}
cp /home/dev/project/tests/levelgen/levelgen_objects.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_objects.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
mkdir -p /home/dev/project/build_pc/levelgen_obj_shots
rm -f /home/dev/project/build_pc/levelgen_obj_shots/shot_*.bmp
cd /home/dev/project/build_pc/levelgen_obj_shots || exit 1
DISPLAY=:99 GE_MODE=levelgen_objects GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_FRAMES=${GE_FRAMES:-12000} timeout 500 ../ProjectMidas > $LOGDIR/levelgen_objects.log 2>&1
grep -E "LEVELGEN|gfx: wrote|ge: lua: .*error|setup lint|domakedefaultobj|setupCctv|not positioned" $LOGDIR/levelgen_objects.log

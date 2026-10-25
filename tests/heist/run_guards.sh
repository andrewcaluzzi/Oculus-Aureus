#!/bin/sh
# Heist guards/civilians test (P0 jobs 3-4; on the Janus bank since P2.4), inside the build container with Xvfb on :99 and build_pc built
# with tools/levelgen/janus_bank.json on the levelgen slot.
# usage: run_guards.sh   -> prints the HEIST lines; the last one is "HEIST DONE <n> passed, <m> failed"
# The full log is kept in ${LOGDIR:-/tmp}/heist_guards.log (so a hang can be read while it runs).
cp /home/dev/project/tests/heist/heist_guards.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_guards.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
cd /home/dev/project/build_pc || exit 1
DISPLAY=:99 GE_MODE=heist_guards GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_FRAMES=${GE_FRAMES:-30000} \
    timeout 560 ./ProjectMidas > ${LOGDIR:-/tmp}/heist_guards.log 2>&1
grep -a -E "HEIST|ge: heist|ge: lua: .*error" ${LOGDIR:-/tmp}/heist_guards.log

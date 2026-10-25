#!/bin/sh
# Heist bindings test (P0 job 2; on the Janus bank since P2.4), inside the build container with Xvfb on :99 and build_pc built with
# tools/levelgen/janus_bank.json on the levelgen slot.
# usage: run.sh   -> prints the HEIST lines; the last one is "HEIST DONE <n> passed, <m> failed". Full log: ${LOGDIR:-/tmp}/heist_bindings.log
cp /home/dev/project/tests/heist/heist_bindings.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bindings.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
cd /home/dev/project/build_pc || exit 1
DISPLAY=:99 GE_MODE=heist_bindings GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_FRAMES=${GE_FRAMES:-9000} \
    timeout 400 ./ProjectMidas > ${LOGDIR:-/tmp}/heist_bindings.log 2>&1
grep -a "HEIST\|ge: lua: .*error" ${LOGDIR:-/tmp}/heist_bindings.log

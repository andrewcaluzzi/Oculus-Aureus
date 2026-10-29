#!/bin/sh
# AC's playtest bug: a knocked-out guard who is then killed must die where he lies (tests/heist/heist_kokill.lua), inside the build
# container with Xvfb on :99 and build_pc built.
# usage: run_kokill.sh   -> the HEIST lines; the last one is "HEIST DONE <n> passed, <m> failed"
B=/home/dev/project/build_pc
L=${LOGDIR:-/home/dev/project/build_tests}/heist_kokill.log
cp /home/dev/project/tests/heist/heist_kokill.lua $B/Data/Scripts/modes/heist_kokill.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua $B/Data/Scripts/modes/levelgen_layout.lua
env DISPLAY=:99 GE_MODE=heist_kokill GE_STAGE=levelgen GE_LEVELGEN=bank GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 \
    GE_FRAMES=${GE_FRAMES:-1500} timeout ${TIMEOUT:-300} $B/ProjectMidas > $L 2>&1
grep -a -E "HEIST|killed where it lies|ge: lua: .*error" $L

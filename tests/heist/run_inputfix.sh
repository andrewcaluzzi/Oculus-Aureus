#!/bin/sh
# Heist input fixes (designs/bank-heist.md 23c, tests/heist/heist_inputfix.lua): the use press in front of a guard, B behind each
# carrier (the pickpocket), drawing the PP7 from the opening slappers. Inside the build container with Xvfb on :99 and build_pc built.
#   run_inputfix.sh [seed ...]     (default: 7 8 11) -> the HEIST lines per seed; each run ends "HEIST DONE <n> passed, <m> failed"
# Logs: ${LOGDIR:-/tmp}/heist_inputfix_s<seed>.log
M=/home/dev/project/build_pc/Data/Scripts/modes
cp /home/dev/project/tests/heist/heist_inputfix.lua $M/heist_inputfix.lua
for f in mode_heist.lua:heist.lua heist_lib.lua heist_bank_map.lua heist_ai.lua heist_intel.lua heist_guide.lua heist_ar.lua heist_gadgets.lua; do
    cp "/home/dev/project/tools/${f%%:*}" "$M/${f#*:}"
done
cp /home/dev/project/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
cd /home/dev/project/build_pc || exit 1
LOGDIR=${LOGDIR:-/tmp}
for s in ${*:-7 8 11}; do
    echo "HEIST SEED $s"
    DISPLAY=:99 GE_MODE=heist_inputfix GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=$s \
        GE_INTEL_NOBRIEF=1 GE_FRAMES=${GE_FRAMES:-9000} timeout ${TIMEOUT:-400} ./ProjectMidas > $LOGDIR/heist_inputfix_s$s.log 2>&1
    grep -a -E "HEIST|ge: lua: .*error" $LOGDIR/heist_inputfix_s$s.log
done

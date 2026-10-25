#!/bin/sh
# Heist frame-time bench (designs/bank-heist.md P2.5), inside the build container with Xvfb on :99 and build_pc built. Runs
# tests/heist/heist_bench.lua at Agent (GE_DIFF=0) and at 007 (GE_DIFF=3) on STAGE (default levelgen: the Janus bank) and keeps each
# report (Data/Logs/arena_bench.txt) as ${LOGDIR:-/tmp}/heist_bench_<stage>_d<n>.txt. Headless the GL numbers are Mesa's software
# renderer (llvmpipe): read the sim column (the game code per frame, display-list building included) and the draw counts.
# usage: [STAGE=bunker2] run_bench.sh   -> the "HEIST BENCH" lines, then each report
STAGE=${STAGE:-levelgen}
LOGDIR=${LOGDIR:-/tmp}
for f in heist_bench; do cp /home/dev/project/tests/heist/$f.lua /home/dev/project/build_pc/Data/Scripts/modes/$f.lua; done
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
cd /home/dev/project/build_pc || exit 1
for d in ${DIFFS:-0 3}; do
    rm -f Data/Logs/arena_bench.txt
    env DISPLAY=:99 GE_MODE=heist_bench GE_STAGE=$STAGE GE_DIFF=$d GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_TICKHZ=60 \
        GE_FRAMES=${GE_FRAMES:-30000} timeout 900 ./ProjectMidas > $LOGDIR/heist_bench_${STAGE}_d$d.log 2>&1
    grep -a "HEIST BENCH\|ge: lua: .*error" $LOGDIR/heist_bench_${STAGE}_d$d.log
    cp Data/Logs/arena_bench.txt $LOGDIR/heist_bench_${STAGE}_d$d.txt 2>/dev/null && grep -v "^ge: sys\|^$" $LOGDIR/heist_bench_${STAGE}_d$d.txt
done

#!/bin/sh
# Sim-step CPU profile of the heist bench (designs/bank-heist.md section 28), inside the build container with Xvfb on :99.
# Runs tests/heist/heist_bench.lua (hall quiet, vault, alarm ramp, hot) fast-forwarded and sim-only (one rendered tick in 60, the
# soak's way), with GE_PROFILE_SIM (port/luavm/port_luavm_simprof.c: samples of the sim thread's own CPU time, so other load on the
# machine does not move them) and GE_HASHLOG (the per-tick state hash: two builds that hash alike simulate alike).
# usage: [BIN=ProjectMidas] [DIFF=0] [TAG=name] [SEED=7] [HZ=1000] run_simprof.sh  -> $OUT/simprof_$TAG.txt, hash_$TAG.txt, $TAG.log
# then: python3 tests/heist/simprof_report.py $OUT/simprof_$TAG.txt build_pc/$BIN
B=/home/dev/project/build_pc
OUT=${OUT:-$B/logs_perf}
BIN=${BIN:-ProjectMidas}
DIFF=${DIFF:-0}
TAG=${TAG:-d$DIFF}
mkdir -p $OUT
cp /home/dev/project/tests/heist/heist_bench.lua $B/Data/Scripts/modes/heist_bench.lua
cd $B || exit 1
rm -f $OUT/simprof_$TAG.txt $OUT/hash_$TAG.txt
env DISPLAY=:99 GE_MODE=heist_bench GE_STAGE=levelgen GE_DIFF=$DIFF GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=${SEED:-7} GE_TICKHZ=60 \
    GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_CLOCKSCALE=${SCALE:-20} GE_FRAMES=2000000000 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_SIMONLY_TEST=${SIMONLY:-2,59,60} LP_NUM_THREADS=1 GE_PROFILE_SIM=${PROF-$OUT/simprof_$TAG.txt} GE_PROFILE_HZ=${HZ:-1000} \
    GE_HASHLOG=$OUT/hash_$TAG.txt timeout ${WALL:-900} ./$BIN > $OUT/$TAG.log 2>&1
echo "exit $?"
grep -a "HEIST BENCH\|simprof\|ge: lua: .*error" $OUT/$TAG.log | grep -v "sim-only ticks so far"

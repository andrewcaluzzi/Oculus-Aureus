#!/bin/sh
# The door interlocks (designs/bank-heist.md section 29): tests/heist/heist_interlock.lua on bank v3, inside the build container with Xvfb
# on :99. One process per run: basic guards tailgate verify power win (RUNS="basic win" picks some). Seeded (GE_SEED, default 7).
# Fast-forwarded the seed sweep's way (GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_CLOCKSCALE, one tick in 60 drawn: the same sim, only sooner) with
# an unbounded GE_FRAMES; the game is killed once it prints "HEIST DONE" (or after WALL seconds). PAR=1 runs the RUNS side by side.
# Logs in ${LOGDIR:-/tmp}/heist_interlock_<run>.log.
# usage: run_interlock.sh   -> the HEIST lines of each run; each run ends with "HEIST DONE <n> passed, <m> failed"
cp /home/dev/project/tests/heist/heist_interlock.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_interlock_test.lua
for f in mode_heist.lua:heist.lua heist_lib.lua heist_bank_map.lua heist_ai.lua heist_interlock.lua heist_gadgets.lua heist_guide.lua heist_intel.lua heist_randomiser.lua heist_planning.lua; do
    src=${f%%:*}; dst=${f#*:}
    cp /home/dev/project/tools/$src /home/dev/project/build_pc/Data/Scripts/modes/$dst
done
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
cd /home/dev/project/build_pc || exit 1
LOGDIR=${LOGDIR:-/tmp}
mkdir -p $LOGDIR
one() {
    run=$1
    log=$LOGDIR/heist_interlock_$run.log
    flags=""
    [ $run != basic ] && flags="GE_IL_$(echo $run | tr a-z A-Z)=1"
    [ $run = delivery ] && flags="$flags GE_PLANNING=1"   # (the planning board's SALLY PORT entry on the delivery day)
    env $flags DISPLAY=:99 GE_MODE=heist_interlock_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 \
        GE_NOSLEEP=1 GE_CLOCKSCALE=${SCALE:-20} GE_FRAMES=2000000000 GE_SEED=${GE_SEED:-7} GE_SIMONLY_TEST=${SIMONLY:-2,59,60} \
        LP_NUM_THREADS=1 ./ProjectMidas > $log 2>&1 &
    pid=$!
    t=0
    while kill -0 $pid 2>/dev/null; do
        if grep -saq "HEIST DONE" $log; then sleep 1; break; fi
        sleep 2; t=$((t + 2))
        [ $t -ge ${WALL:-900} ] && echo "HEIST FAIL $run timed out after $t s" && break
    done
    kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null
}
for run in ${RUNS:-basic guards tailgate verify power win delivery}; do
    if [ "${PAR:-0}" = 1 ]; then one $run & else one $run; fi
done
wait
for run in ${RUNS:-basic guards tailgate verify power win delivery}; do
    echo "HEIST RUN $run"
    grep -aE "HEIST|ge: lua: ge: heist: interlock|ge: lua: .*error" $LOGDIR/heist_interlock_$run.log
done

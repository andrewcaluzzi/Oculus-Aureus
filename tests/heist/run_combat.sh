#!/bin/sh
# The combat layer and the heat (designs/bank-heist.md section 24) on the Janus bank, inside the build container with Xvfb on :99 and
# build_pc built. One run per scenario of tests/heist/heist_combat.lua (RUNS="cover flank retreat heat"; one RUNS=<run> per container to
# run them in parallel). Seeded (GE_SEED, default 7).
# usage: run_combat.sh -> the HEIST lines of each run; each ends with "HEIST DONE <n> passed, <m> failed". Logs: ${LOGDIR:-/tmp}/heist_combat_<run>.log
cp /home/dev/project/tests/heist/heist_combat.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_combat_test.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/tools/heist_ai.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_ai.lua
cp /home/dev/project/tools/heist_combat.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_combat.lua
cp /home/dev/project/tools/heist_ar.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_ar.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
LOGDIR=${LOGDIR:-/tmp}
mkdir -p $LOGDIR
cd /home/dev/project/build_pc || exit 1
for run in ${RUNS:-cover flank retreat heat}; do
    flag=GE_CB_$(echo $run | tr a-z A-Z)=1
    echo "HEIST RUN $run"
    env $flag DISPLAY=:99 GE_MODE=heist_combat_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=${GE_SEED:-7} \
        GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=${GE_FRAMES:-5400} timeout 400 ./ProjectMidas > $LOGDIR/heist_combat_$run.log 2>&1
    grep -aE "HEIST|ge: combat:|ge: lua: ge: heist: (ready|ALARM|combat)|ge: lua: .*error" $LOGDIR/heist_combat_$run.log
done

#!/bin/sh
# Hostages and the human shield (designs/bank-heist.md section 31) on the Janus bank, inside the build container with Xvfb on :99 and
# build_pc built. One run per scenario of tests/heist/heist_hostage.lua (RUNS="grab fire standoff debrief off"). Seeded (GE_SEED, default 7).
# usage: run_hostage.sh -> the HEIST lines of each run; each ends with "HEIST DONE <n> passed, <m> failed". Logs: ${LOGDIR:-/tmp}/heist_hostage_<run>.log
M=/home/dev/project/build_pc/Data/Scripts/modes
cp /home/dev/project/tests/heist/heist_hostage.lua $M/heist_hostage_test.lua
cp /home/dev/project/tools/mode_heist.lua $M/heist.lua
for f in heist_lib heist_bank_map heist_ai heist_combat heist_ar heist_hostages heist_debrief heist_intel heist_guide; do
    cp /home/dev/project/tools/$f.lua $M/$f.lua
done
cp /home/dev/project/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
LOGDIR=${LOGDIR:-/tmp}
mkdir -p $LOGDIR
cd /home/dev/project/build_pc || exit 1
for run in ${RUNS:-grab fire standoff debrief off}; do
    flag=GE_HG_$(echo $run | tr a-z A-Z)=1
    echo "HEIST RUN $run"
    env $flag DISPLAY=:99 GE_MODE=heist_hostage_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=${GE_SEED:-7} \
        GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=${GE_FRAMES:-7200} timeout 500 ./ProjectMidas > $LOGDIR/heist_hostage_$run.log 2>&1
    grep -aE "HEIST|ge: shield:|ge: lua: ge: heist: (ready|ALARM|hostages)|ge: lua: .*error" $LOGDIR/heist_hostage_$run.log
done

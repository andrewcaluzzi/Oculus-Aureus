#!/bin/sh
# Guard hearing, awareness and the cameras' acts and bodies (designs/bank-heist.md section 18) on the Janus bank, inside the build
# container with Xvfb on :99 and build_pc built. One run per scenario of tests/heist/heist_ai.lua (RUNS="hear slap ..." picks; one
# RUNS=<run> per container to run them in parallel). GE_AI_SHOTS=1 adds screenshots (build_pc/heist_ai_shots/shot_NNNNN.bmp; convert
# with tests/bmp2png.py, delete the BMPs).
# usage: run_ai.sh -> the HEIST lines of each run; each ends with "HEIST DONE <n> passed, <m> failed". Logs: ${LOGDIR:-/tmp}/heist_ai_<run>.log
cp /home/dev/project/tests/heist/heist_ai.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_ai_test.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/tools/heist_ai.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_ai.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
LOGDIR=${LOGDIR:-/tmp}
mkdir -p $LOGDIR /home/dev/project/build_pc/heist_ai_shots
cd /home/dev/project/build_pc/heist_ai_shots || exit 1
for run in ${RUNS:-hear slap door giveup camko camact camdown checkin patrol}; do
    flag=GE_AI_$(echo $run | tr a-z A-Z)=1
    echo "HEIST RUN $run"
    env $flag DISPLAY=:99 GE_MODE=heist_ai_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 \
        GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=${GE_FRAMES:-4800} timeout 400 ../ProjectMidas > $LOGDIR/heist_ai_$run.log 2>&1
    grep -aE "HEIST|gfx: wrote|ge: ai:|ge: lua: ge: heist: (ready|ALARM|cover|camera|the security|ai:)|ge: lua: .*error" $LOGDIR/heist_ai_$run.log
done

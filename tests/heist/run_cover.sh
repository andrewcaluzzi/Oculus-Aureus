#!/bin/sh
# The banker cover, the security room's camera link and the power cut (designs/bank-heist.md section 16) on the Janus bank, inside the
# build container with Xvfb on :99 and build_pc built with tools/levelgen/janus_bank.json on the levelgen slot. One run per scenario of
# tests/heist/heist_cover.lua (RUNS="hall gun ..." picks; one RUNS=<run> per container to run them in parallel). GE_COVER_SHOTS=1 adds
# screenshots (build_pc/heist_cover_shots/shot_NNNNN.bmp; convert with tests/bmp2png.py, delete the BMPs).
# usage: run_cover.sh -> the HEIST lines of each run; each ends with "HEIST DONE <n> passed, <m> failed". Logs: ${LOGDIR:-/tmp}/heist_cover_<run>.log
cp /home/dev/project/tests/heist/heist_cover.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_cover.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/tools/heist_ai.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_ai.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
LOGDIR=${LOGDIR:-/tmp}
mkdir -p $LOGDIR /home/dev/project/build_pc/heist_cover_shots
cd /home/dev/project/build_pc/heist_cover_shots || exit 1
for run in ${RUNS:-hall gun loot restricted staff ko cam camdown power interrupt generator doors}; do
    flag=GE_COVER_$(echo $run | tr a-z A-Z)=1
    echo "HEIST RUN $run"
    env $flag DISPLAY=:99 GE_MODE=heist_cover GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 \
        GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=${GE_FRAMES:-4800} timeout 400 ../ProjectMidas > $LOGDIR/heist_cover_$run.log 2>&1
    grep -aE "HEIST|gfx: wrote|ge: lua: ge: heist: (ready|ALARM|cover|camera|the security|POWER|power|Bond [0-9] works|the breaker|the backup|guard)|ge: lua: .*error" $LOGDIR/heist_cover_$run.log
done

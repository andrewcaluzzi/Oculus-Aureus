#!/bin/sh
# The heist's second job (designs/job2-casino.md): Le Grand Casino Janus. Inside the build container with Xvfb on :99 and build_job2
# built with the casino on the levelgen slot:
#   py -3 tools/levelgen/build_casino.py
#   py -3 tools/gen_level.py tools/levelgen/casino.json --out port/levelgen_casino
#   cmake -S . -B build_job2 "-DCMAKE_BUILD_TYPE=Debug" "-DGE_VIDEO=ON" "-DSDL2_LINUX_ROOT=/home/dev/sdl2-linux-i686" "-DGE_LEVELGEN_DIR=port/levelgen_casino"
#   cmake --build build_job2 -j 2          (and a ROM copy in build_job2/Data/)
# usage: run_job2.sh         tests/heist/heist_job2.lua: the job and its INTEL board, the people and cameras, a stick-input walk to the
#                            vault and up to the surveillance room and the catwalks, the scripted win path. Prints the HEIST lines; ends
#                            with "HEIST DONE <n> passed, <m> failed". Full log: ${LOGDIR:-/tmp}/heist_job2.log
#        run_job2.sh shots   tests/heist/heist_job2_shots.lua: 5 screenshots -> build_job2/job2_shots/shot_NNNNN.bmp (convert with
#                            tests/bmp2png.py, delete the BMPs)
# One sim tick per frame (GE_FIXEDTICK=1), seeded (GE_SEED, default 7).
B=/home/dev/project/build_job2
M=$B/Data/Scripts/modes
cp /home/dev/project/tests/heist/heist_job2.lua $M/heist_job2.lua
cp /home/dev/project/tests/heist/heist_job2_shots.lua $M/heist_job2_shots.lua
cp /home/dev/project/tools/mode_heist.lua $M/heist.lua
cp /home/dev/project/tools/heist_bank_map.lua $M/heist_bank_map.lua
cp /home/dev/project/tools/heist_intel.lua $M/heist_intel.lua
cp /home/dev/project/tools/heist_guide.lua $M/heist_guide.lua
cp /home/dev/project/tools/heist_ar.lua $M/heist_ar.lua
cp /home/dev/project/tools/jobs/bank.lua $M/job_bank.lua
cp /home/dev/project/tools/jobs/casino.lua $M/job_casino.lua
cp /home/dev/project/port/levelgen_casino/levelgen_layout.lua $M/levelgen_layout.lua
LOGDIR=${LOGDIR:-/tmp}
mkdir -p $LOGDIR
if [ "$1" = shots ]; then
    mkdir -p $B/job2_shots
    rm -f $B/job2_shots/shot_*.bmp
    cd $B/job2_shots || exit 1
    env DISPLAY=:99 GE_MODE=heist_job2_shots GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=${GE_SEED:-7} GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
        GE_FRAMES=${GE_FRAMES:-6000} timeout ${TIMEOUT:-900} ../ProjectMidas > $LOGDIR/heist_job2_shots.log 2>&1
    grep -aE "HEIST|gfx: wrote|ge: lua: .*error" $LOGDIR/heist_job2_shots.log
    exit 0
fi
cd $B || exit 1
env DISPLAY=:99 GE_MODE=heist_job2 GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=${GE_SEED:-7} \
    GE_FRAMES=${GE_FRAMES:-24000} timeout ${TIMEOUT:-1800} ./ProjectMidas > $LOGDIR/heist_job2.log 2>&1
grep -aE "HEIST|ge: lua: ge: heist: (ready|WIN|FAIL|stage start|not the|VAULT|setup step)|ge: lua: .*error" $LOGDIR/heist_job2.log

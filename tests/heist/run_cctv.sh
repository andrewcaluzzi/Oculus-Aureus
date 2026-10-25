#!/bin/sh
# Heist CCTV test (P1 T1 + T2; on the Janus bank since P2.4), inside the build container with Xvfb on :99 and build_pc built with
# tools/levelgen/janus_bank.json on the levelgen slot. The camera feeds, the controls, static, the damage kick-out and the intel box
# (tests/heist/heist_cctv.lua). Screenshots: build_pc/heist_cctv_shots/shot_NNNNN.bmp (each follows a "HEIST SHOT <name>" line in the
# log; GE_FULLHEIGHT=0 GE_EDGEINSET=1: without them Xvfb shows only the HUD on black). Convert with tests/bmp2png.py, delete the BMPs.
# usage: run_cctv.sh   -> the HEIST lines; the last one is "HEIST DONE <n> passed, <m> failed". Full log: ${LOGDIR:-/tmp}/heist_cctv.log
cp /home/dev/project/tests/heist/heist_cctv.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_cctv.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
mkdir -p /home/dev/project/build_pc/heist_cctv_shots
rm -f /home/dev/project/build_pc/heist_cctv_shots/shot_*.bmp
cd /home/dev/project/build_pc/heist_cctv_shots || exit 1
DISPLAY=:99 GE_MODE=heist_cctv GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FIXEDTICK=1 GE_TICKHZ=60 \
    GE_FRAMES=${GE_FRAMES:-9000} timeout 500 ../ProjectMidas > ${LOGDIR:-/tmp}/heist_cctv.log 2>&1
grep -a -E "HEIST|gfx: wrote|ge: lua: ge: heist: (ready|Bond [0-9]+ (watches|left))|ge: lua: .*error" ${LOGDIR:-/tmp}/heist_cctv.log

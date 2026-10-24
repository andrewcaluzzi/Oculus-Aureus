#!/bin/sh
# Heist CCTV test (P1 T1 + T2), inside the build container with Xvfb on :99 and build_pc built. The camera feeds, the controls, static,
# the damage kick-out and the intel box (tests/heist/heist_cctv.lua). Screenshots: build_pc/heist_cctv_shots/shot_NNNNN.bmp (each follows a
# "HEIST SHOT <name>" line in the log; GE_FULLHEIGHT=0 GE_EDGEINSET=1: without them Xvfb shows only the HUD on black).
# usage: run_cctv.sh   -> the HEIST lines; the last one is "HEIST DONE <n> passed, <m> failed". Full log: /tmp/heist_cctv.log
cp /home/dev/project/tests/heist/heist_cctv.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_cctv.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
mkdir -p /home/dev/project/build_pc/heist_cctv_shots
rm -f /home/dev/project/build_pc/heist_cctv_shots/shot_*.bmp
cd /home/dev/project/build_pc/heist_cctv_shots || exit 1
DISPLAY=:99 GE_MODE=heist_cctv GE_STAGE=bunker2 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_FRAMES=${GE_FRAMES:-9000} timeout 500 ../ProjectMidas > /tmp/heist_cctv.log 2>&1
grep -E "HEIST|gfx: wrote|ge: lua: ge: heist: (ready|Bond [0-9]+ (watches|left))" /tmp/heist_cctv.log

#!/bin/sh
# Heist screenshot tour (P0 job 6), inside the build container with Xvfb on :99 and build_pc built. Writes a BMP every 120 frames into
# build_pc/heist_shots/ (GE_FULLHEIGHT=0 GE_EDGEINSET=1: without them Xvfb shows only the HUD on black). Convert with tests/bmp2png.py.
cp /home/dev/project/tests/heist/heist_shots.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_shots.lua
mkdir -p /home/dev/project/build_pc/heist_shots
rm -f /home/dev/project/build_pc/heist_shots/shot_*.bmp
cd /home/dev/project/build_pc/heist_shots || exit 1
DISPLAY=:99 GE_MODE=heist_shots GE_STAGE=bunker2 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_SHOT=120 \
    GE_FRAMES=${GE_FRAMES:-8000} timeout 500 ../ProjectMidas > /tmp/heist_shots.log 2>&1
grep -E "HEIST|ge: lua: ge: heist: (ready|VAULT)" /tmp/heist_shots.log
ls /home/dev/project/build_pc/heist_shots | head -50

#!/bin/sh
# Heist bindings test (P0 job 2), run inside the build container with Xvfb on :99 and build_pc built.
# usage: run.sh   -> prints the HEIST lines; the last one is "HEIST DONE <n> passed, <m> failed"
cp /home/dev/project/tests/heist/heist_bindings.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bindings.lua
cd /home/dev/project/build_pc || exit 1
DISPLAY=:99 GE_MODE=heist_bindings GE_STAGE=bunker2 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FRAMES=9000 ./ProjectMidas 2>&1 | grep HEIST

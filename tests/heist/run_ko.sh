#!/bin/sh
# Heist knock-out test (P0 job 5), run inside the build container with Xvfb on :99 and build_pc built.
# usage: run_ko.sh   -> prints the HEIST lines; the last one is "HEIST DONE <n> passed, <m> failed"
# The full log is kept in /tmp/heist_ko.log.
cp /home/dev/project/tests/heist/heist_ko.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_ko.lua
cd /home/dev/project/build_pc || exit 1
DISPLAY=:99 GE_MODE=heist_ko GE_STAGE=bunker2 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FRAMES=${GE_FRAMES:-12000} timeout 400 ./ProjectMidas > /tmp/heist_ko.log 2>&1
grep -E "HEIST|ge: heist" /tmp/heist_ko.log

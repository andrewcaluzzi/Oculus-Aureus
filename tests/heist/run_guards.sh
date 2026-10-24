#!/bin/sh
# Heist guards/civilians test (P0 jobs 3-4), run inside the build container with Xvfb on :99 and build_pc built.
# usage: run_guards.sh   -> prints the HEIST lines; the last one is "HEIST DONE <n> passed, <m> failed"
# The full log is kept in /tmp/heist_guards.log (so a hang can be read while it runs).
cp /home/dev/project/tests/heist/heist_guards.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_guards.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cd /home/dev/project/build_pc || exit 1
DISPLAY=:99 GE_MODE=heist_guards GE_STAGE=bunker2 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FRAMES=${GE_FRAMES:-30000} timeout 560 ./ProjectMidas > /tmp/heist_guards.log 2>&1
grep -E "HEIST|ge: heist" /tmp/heist_guards.log

#!/bin/sh
# Xvfb run of crowd-smoke.lua from build_pc (inside the goldeneye Docker image, with xvfb installed)
cd /home/dev/project/build_pc || exit 1
cp ../tests/arena_crowd/crowd-smoke.lua Data/Scripts/modes/crowd-smoke.lua
xvfb-run -a -s "-screen 0 640x480x24" env GE_MODE=crowd-smoke GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln \
  GE_FRAMES=9000 GE_NOINISAVE=1 ./ProjectMidas > crowd_smoke.log 2>&1
echo "exit $?"
grep -a "ge: crowd\|WON\|OVER\|cleared\|pool\|rror\|WARN" crowd_smoke.log | grep -v ALSA | head -30

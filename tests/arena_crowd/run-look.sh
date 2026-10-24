#!/bin/sh
# Xvfb run of crowd-look.lua from build_pc (inside the goldeneye Docker image, with xvfb installed): shots + crowd_atlas.bmp
cd /home/dev/project/build_pc || exit 1
cp ../tests/arena_crowd/crowd-look.lua Data/Scripts/modes/crowd-look.lua
rm -f shot_*.bmp crowd_atlas.bmp
xvfb-run -a -s "-screen 0 1280x720x24" env GE_MODE=crowd-look GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln \
  GE_SHOT=${SHOT:-120} GE_FRAMES=${FRAMES:-2400} GE_CROWD_DUMP=1 GE_NOINISAVE=1 ./ProjectMidas > crowd_look.log 2>&1
echo "exit $?"

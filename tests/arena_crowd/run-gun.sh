#!/bin/sh
# Xvfb run of gun-look.lua (or MODE=gun-look-off) from build_pc (inside the goldeneye Docker image, with xvfb installed):
# shot_*.bmp must show the gun in its first-person place with the impostors in the stands
cd /home/dev/project/build_pc || exit 1
MODE=${MODE:-gun-look}
cp "../tests/arena_crowd/$MODE.lua" "Data/Scripts/modes/$MODE.lua"
rm -f shot_*.bmp
xvfb-run -a -s "-screen 0 1280x720x24" env GE_MODE=$MODE GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln \
  GE_SHOT=${SHOT:-120} GE_FRAMES=${FRAMES:-1200} GE_NOINISAVE=1 ./ProjectMidas > gun_look.log 2>&1
echo "exit $?"

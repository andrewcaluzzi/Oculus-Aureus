#!/bin/sh
# A5 path/first-shot test run inside the build container (Xvfb on :99).
# usage: runpath.sh <logname> <mode> <old 0|1> [frames] [tickhz]
export DISPLAY=:99 GE_MODE=$2 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln GE_NOINISAVE=1 GE_TICKHZ=${5:-60}
export GE_FRAMES=${4:-2400}
if [ "$3" = 1 ]; then export GE_ARENA_OLDAI=1; fi
/home/dev/project/build_pc/ProjectMidas > /home/dev/project/build_tests/a5/$1.log 2>&1

#!/bin/sh
# A6 test run inside the build container (Xvfb on :99, build_pc).
# usage: run.sh <logname> <mode> [frames] [tickhz] [cheats]
export DISPLAY=:99 GE_MODE=$2 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=${5:-invuln} GE_NOINISAVE=1 GE_TICKHZ=${4:-60}
export GE_FRAMES=${3:-2400}
mkdir -p /home/dev/project/build_tests/a6
/home/dev/project/build_pc/ProjectMidas > /home/dev/project/build_tests/a6/$1.log 2>&1

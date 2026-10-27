#!/bin/sh
# A7 polish test run inside the build container (Xvfb already on :99, build_pc).
# usage: run_polish.sh <logname> <mode> [frames] [tickhz] [cheats]
# Presets live in build_pc/Data/Scripts/modes/ (copy tests/arena_a7/polish-*.lua there). GE_FIXEDTICK=1; run with cwd build_pc.
export DISPLAY=:99 GE_MODE=$2 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=${5:-invuln} GE_NOINISAVE=1 GE_TICKHZ=${4:-60}
export GE_FIXEDTICK=1 ${NOSLEEP:+GE_NOSLEEP=1}
export GE_FRAMES=${3:-2400} GE_NOSCORESAVE=${GE_NOSCORESAVE:-1}
mkdir -p /home/dev/project/build_tests/a7
/home/dev/project/build_pc/ProjectMidas > /home/dev/project/build_tests/a7/$1.log 2>&1

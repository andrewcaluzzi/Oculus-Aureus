#!/bin/sh
# A7 test run inside the build container (Xvfb on :99, build_pc). Copies the A7 presets next to the mode scripts first.
# usage: run_mut.sh <logname> <mode> [frames] [tickhz] [cheats | none] [shot interval]
# Scores: GE_SCOREFILE defaults to build_tests/a7/scores.ini and GE_NOSCORESAVE to 1; the score tests set both.
P=/home/dev/project
mkdir -p $P/build_tests/a7/shots
cp $P/tests/arena_a7/*.lua $P/build_pc/Data/Scripts/modes/
export DISPLAY=:99 GE_MODE=$2 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=${4:-60}
export GE_FRAMES=${3:-2400} GE_FULLHEIGHT=0 GE_EDGEINSET=1
export GE_SCOREFILE=${GE_SCOREFILE:-$P/build_tests/a7/scores.ini} GE_NOSCORESAVE=${GE_NOSCORESAVE:-1}
C=${5:-invuln}
if [ "$C" != none ]; then export GE_CHEATS=$C; fi
if [ -n "$6" ]; then export GE_SHOT=$6; fi
cd $P/build_tests/a7/shots
$P/build_pc/ProjectMidas > $P/build_tests/a7/$1.log 2>&1
echo "exit $?" >> $P/build_tests/a7/$1.log

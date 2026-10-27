#!/bin/sh
# Whacky test run inside the build container (Xvfb on :99). Copies the test presets next to the mode scripts first.
# usage: run.sh <logname> <mode> [frames] [cheats | none] [shot interval]
#   B=<build dir> (default build_pc_whacky); extra GE_* variables come from the caller (docker exec -e GE_WFX_GHOST=1 ...)
# Log: build_tests/whacky/<logname>.log; screenshots (BMP) in build_tests/whacky/shots/<logname>/ (convert and delete them: disk).
P=/home/dev/project
B=${B:-build_pc_whacky}
mkdir -p $P/build_tests/whacky/shots/$1
cp $P/tests/arena_whacky/*.lua $P/$B/Data/Scripts/modes/
export DISPLAY=:99 GE_MODE=$2 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60
export GE_FRAMES=${3:-2400} GE_FULLHEIGHT=0 GE_EDGEINSET=1
export GE_SCOREFILE=$P/build_tests/whacky/scores.ini GE_NOSCORESAVE=1
C=${4:-invuln}
if [ "$C" != none ]; then export GE_CHEATS=$C; fi
if [ -n "$5" ]; then export GE_SHOT=$5; fi
cd $P/build_tests/whacky/shots/$1
$P/$B/ProjectMidas > $P/build_tests/whacky/$1.log 2>&1
echo "exit $?" >> $P/build_tests/whacky/$1.log

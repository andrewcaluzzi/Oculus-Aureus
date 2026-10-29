#!/bin/sh
# Probe Surface in the build container (Xvfb on :99, build_pc): usage probe.sh [frames] [probe file in tests/sniper, default probe]
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/sniper
M=$B/Data/Scripts/modes
mkdir -p $OUT
cp $P/tests/sniper/${2:-probe}.lua $M/sniper-probe.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=sniper-probe GE_STAGE=surface GE_SKIPINTRO=1 GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-600} GE_NOSLEEP=1 GE_CLOCKSCALE=20 GE_CHEATS=invuln
L=$OUT/probe.log
cd $OUT
timeout 300 $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $M/sniper-probe.lua
grep -a "SPROBE\|exit " $L | head -${LINES_MAX:-12}

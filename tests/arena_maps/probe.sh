#!/bin/sh
# Probe a stock MP map in the build container (Xvfb on :99, build_pc): usage probe.sh <mp stage name> [frames]
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/maps
M=$B/Data/Scripts/modes
mkdir -p $OUT
cp $P/tests/arena_maps/${3:-probe}.lua $M/maps-probe.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=maps-probe GE_STAGE=${1:-mptemple} GE_SKIPINTRO=1 GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${2:-600} GE_NOSLEEP=1 GE_CLOCKSCALE=20 GE_CHEATS=invuln
L=$OUT/probe_${1:-mptemple}.log
cd $OUT
timeout 300 $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $M/maps-probe.lua
grep -a "PROBE\|ge: startpad\|ge: mp spawn\|GE_STAGE\|exit " $L | head -40

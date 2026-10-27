#!/bin/sh
# Inside the build container (Xvfb on :99, build_pc): a preset that includes arena-coop-chaos loads and the arena starts.
# PASS = no "nested too deep" in the log and "ge: arena: gate pads" (the arena's start line) is there. usage: run_include_depth.sh [frames]
P=/home/dev/project
mkdir -p $P/build_tests/a7
cp $P/tests/arena_a7/include-depth.lua $P/build_pc/Data/Scripts/modes/
rm -f $P/build_pc/Data/Saves/eeprom.bin
L=$P/build_tests/a7/include-depth.log
cd $P/build_pc
DISPLAY=:99 GE_MODE=include-depth GE_COOP=1 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1 \
  GE_FRAMES=${1:-600} GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_SCOREFILE=$P/build_tests/a7/include-depth-scores.ini GE_NOSCORESAVE=1 GE_CHEATS=invuln \
  ./ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $P/build_pc/Data/Scripts/modes/include-depth.lua
if grep -q "nested too deep" $L; then echo "FAIL include depth: nested too deep"; exit 1; fi
if grep -q "ge: arena: gate pads" $L; then echo "PASS include depth: the arena started through arena-coop-chaos"; else echo "FAIL include depth: no arena start line"; exit 1; fi

#!/bin/sh
# Sight matrix on Surface (tests/sniper/matrix.lua), build_pc under Xvfb :99: usage matrix.sh [frames] ["SMAT_LO=6000 SMAT_HI=15000"]
# The settings arrive as globals in front of the script (Lua has no os.getenv). Log: build_tests/sniper/matrix.log
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/sniper
M=$B/Data/Scripts/modes
mkdir -p $OUT
{
  for kv in $2; do
    k=${kv%%=*}; v=${kv#*=}
    case "$v" in
      ''|*[!0-9]*) echo "$k = \"$v\"" ;;
      *) echo "$k = $v" ;;
    esac
  done
  cat $P/tests/sniper/matrix.lua
} > $M/sniper-matrix.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=sniper-matrix GE_STAGE=surface GE_SKIPINTRO=1 GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-14000} GE_NOSLEEP=1 GE_CLOCKSCALE=20 GE_CHEATS=invuln
L=$OUT/matrix.log
cd $OUT
timeout 900 $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $M/sniper-matrix.lua
grep -a "SMAT start\|SMAT done\|exit \|ge: lua: .*error" $L | head
grep -ac "SMAT [0-9]" $L

#!/bin/sh
# Stan vs drawn geometry on Surface (tests/sniper/geom.lua), build_pc under Xvfb :99: usage geom.sh [frames] ["SGEO_LO=6000 SGEO_HI=15000"]
# The settings arrive as globals in front of the script (Lua has no os.getenv). Log: build_tests/sniper/geom.log
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
  cat $P/tests/sniper/geom.lua
} > $M/sniper-geom.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=sniper-geom GE_STAGE=surface GE_SKIPINTRO=1 GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-14000} GE_NOSLEEP=1 GE_CLOCKSCALE=20 GE_CHEATS=invuln
L=$OUT/geom.log
cd $OUT
timeout 900 $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $M/sniper-geom.lua
grep -a "SGEO start\|SGEO done\|exit \|ge: lua: .*error" $L | head
grep -ac "SGEO [0-9]" $L

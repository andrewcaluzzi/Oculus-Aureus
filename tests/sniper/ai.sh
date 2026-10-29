#!/bin/sh
# AI measurement on Surface (tests/sniper/ai.lua), build_pc under Xvfb :99: usage ai.sh [frames] ["SAI_ACC=50 SAI_BEH=snipe SAI_SECS=25"]
# The settings arrive as globals in front of the script (Lua has no os.getenv).
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
  cat $P/tests/sniper/ai.lua
} > $M/sniper-ai.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=sniper-ai GE_STAGE=surface GE_SKIPINTRO=1 GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-6000} GE_NOSLEEP=1 GE_CLOCKSCALE=20 GE_CHEATS=${CHEATS-invuln}
L=$OUT/ai.log
cd $OUT
timeout 600 $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $M/sniper-ai.lua
grep -a "SAI\|exit \|ge: lua: .*error" $L | cut -c1-1500 | head -40

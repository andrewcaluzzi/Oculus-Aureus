#!/bin/sh
# The per-stage HUD test (see amenu_hudstale.lua) inside the build container (Xvfb on :99). usage: hudstale.sh [game frames] [build dir]
# Output: build_tests/amenu_hudstale/hudstale.log and shots/*.bmp (convert to JPG and delete). Look at the run 2 shot at 11 s: no "STALE CARD".
P=/home/dev/project
B=${2:-build_pc}
OUT=$P/build_tests/amenu_hudstale
mkdir -p $OUT/shots
rm -f $OUT/shots/*.bmp
for f in mode_arena:arena arena_flow:_arena_flow arena_menu_defs:_arena_menu_defs mode_arena_menu:arena-menu mode_arena_scores:_arena_scores \
         mode_arena_crowd:_arena_crowd arena_polish:_arena_polish arena_theme:_arena_theme arena_coop:_arena_coop \
         mode_arena_chaos:arena-chaos mode_arena_mutators:_arena_mutators mode_arena_elites:_arena_elites; do
  cp "$P/tools/${f%%:*}.lua" "$P/$B/Data/Scripts/modes/${f##*:}.lua"
done
cp $P/tests/arena_menu/amenu_hudstale.lua $P/$B/Data/Scripts/zz_amenu_hudstale.lua
rm -f $P/$B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=arena-chaos GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_GFXFRAMES=${1:-6000} GE_FRAMES=$(( ${1:-6000} * 20 )) GE_FULLHEIGHT=0 GE_EDGEINSET=1
export GE_SCOREFILE=$OUT/scores.ini GE_NOSCORESAVE=1 GE_ARENAMENU_FILE=$OUT/arena_menu.ini GE_NOMENUSAVE=1
export GE_ARENA_MENU=1 GE_ARENA_TESTKEYS=1
rm -f $OUT/scores.ini $OUT/arena_menu.ini
cd $OUT/shots
$P/$B/ProjectMidas > $OUT/hudstale.log 2>&1
echo "exit $?" >> $OUT/hudstale.log
rm -f $P/$B/Data/Scripts/zz_amenu_hudstale.lua
grep "HUDSTALE" $OUT/hudstale.log

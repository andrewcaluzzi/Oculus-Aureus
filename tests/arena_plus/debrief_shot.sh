#!/bin/sh
# A long run's post-game (20 waves, elites, mutators, credits, ten Requisition cards) as a screenshot, at the window size you give
# (default 1920x1080; try 640x480 for the smallest): build_tests/debrief/<WxH>.jpg. Inside the build container (Xvfb on :99, build_pc).
# usage: debrief_shot.sh [WxH]
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/debrief
S=${1:-1920x1080}
M=$B/Data/Scripts/modes
mkdir -p $OUT/shots
rm -f $OUT/shots/*
for f in mode_arena_menu:arena-menu arena_menu_defs:_arena_menu_defs arena_theme:_arena_theme ui_menu:_ui_menu; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
cp $P/tests/arena_plus/debrief-shot.lua $B/Data/Scripts/zz_debrief_shot.lua
rm -f $B/Data/Saves/eeprom.bin
Xvfb :98 -screen 0 ${S}x24 >/dev/null 2>&1 &
xp=$!
sleep 2
export DISPLAY=:98 GE_MODE=arena-menu GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=1200 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_NOMENUSAVE=1 GE_NOSCORESAVE=1 GE_ARENAMENU_FILE=$OUT/arena_menu.ini GE_NOSLEEP=1 GE_CLOCKSCALE=20
export GE_WINDOW=$S
cd $OUT/shots
timeout 300 $B/ProjectMidas > $OUT/shot.log 2>&1
kill $xp 2>/dev/null
rm -f $B/Data/Scripts/zz_debrief_shot.lua
grep -a "DEBRIEFSHOT\|rror" $OUT/shot.log
f=$(ls $OUT/shots/*.bmp 2>/dev/null | head -1)
if [ -n "$f" ]; then convert "$f" -resize 1280x -quality 82 $OUT/$S.jpg; rm -f $OUT/shots/*.bmp; ls -la $OUT/$S.jpg; fi

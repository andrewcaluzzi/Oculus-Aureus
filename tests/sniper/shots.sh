#!/bin/sh
# The three pictures for the report (build_pc under Xvfb :99; 640x360 JPG into build_tests/sniper/jpg, the BMPs go at once):
#   hub-card.jpg   the hub's SNIPER DUEL card (the dossier) and the card page after SELECT      (tests/sniper/hubshots.lua, a hub boot)
#   glint.jpg      round 1's glint: Bond looking at the perch, the marker over the rifle         (smoke.lua with GE_SNIPER_SHOTS=1)
#   debrief.jpg    the debrief the run ends in, on the hub's stage                                (hubshots.lua, after the smoke run)
# usage: shots.sh [smoke frames]
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/sniper
M=$B/Data/Scripts/modes
mkdir -p $OUT/shots $OUT/jpg
rm -f $OUT/shots/*.bmp
if [ -z "$SKIPHUB" ]; then rm -f $OUT/jpg/*.jpg; fi
for f in mode_sniper:sniper mode_sniper_menu:sniper-menu sniper_defs:_sniper_defs mode_hub:hub hub_defs:_hub_defs ui_menu:_ui_menu; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
cp $P/tests/sniper/smoke.lua $M/sniper-smoke.lua
cp $P/tests/sniper/hubshots.lua $B/Data/Scripts/zz_sniper_shots.lua
rm -f $B/Data/Saves/eeprom.bin $OUT/sniper_bests.ini
export DISPLAY=:99 GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1
export GE_UIMENU_DIR=$OUT GE_NOMENUSAVE=1 GE_SNIPER_MENU=1 GE_NOSLEEP=1 GE_CLOCKSCALE=4
cd $OUT/shots
# 1. the hub's card (SKIPHUB=1 skips it)
if [ -z "$SKIPHUB" ]; then GE_MODE=hub GE_STAGE=control GE_FRAMES=900 timeout 300 $B/ProjectMidas > $OUT/shots_hub.log 2>&1; fi
grep -a "SNSHOT" $OUT/shots_hub.log
n=0
for f in $(ls $OUT/shots/*.bmp 2>/dev/null | sort); do
  n=$((n+1))
  case $n in 1) nm=hub-dossier;; 2) nm=hub-card;; *) nm=hub-extra$n;; esac
  convert "$f" -resize 640x360! -quality 82 "$OUT/jpg/$nm.jpg"
done
rm -f $OUT/shots/*.bmp
# 2. the glint and the debrief
GE_CLOCKSCALE=20 GE_MODE=sniper-smoke GE_STAGE=surface GE_CHEATS=invuln GE_SNIPER_SHOTS=1 GE_FRAMES=${1:-9000} timeout 900 $B/ProjectMidas > $OUT/shots_run.log 2>&1
grep -a "SMOKE shot\|SNSHOT" $OUT/shots_run.log
set -- $(grep -a "SMOKE shot" $OUT/shots_run.log | sed "s/.*shot //") debrief
for f in $(ls $OUT/shots/*.bmp 2>/dev/null | sort); do
  nm=${1:-extra}; shift
  convert "$f" -resize 640x360! -quality 82 "$OUT/jpg/$nm.jpg"
  if [ "$nm" = perch ]; then convert "$f" -gravity center -crop 35%x35%+0+0 +repage -resize 640x360! -quality 82 "$OUT/jpg/perch-zoom.jpg"; fi
done
rm -f $OUT/shots/*.bmp
rm -f $M/sniper-smoke.lua $B/Data/Scripts/zz_sniper_shots.lua
ls -la $OUT/jpg

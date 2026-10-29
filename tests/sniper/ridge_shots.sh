#!/bin/sh
# Pictures of SEVERNAYA RIDGE (build_pc_ridge under Xvfb :99): usage ridge_shots.sh [frames]  ->  build_tests/sniper/ridge/<tag>.jpg (640x360; window also as a 3x crop)
P=/home/dev/project
B=$P/build_pc_ridge
OUT=$P/build_tests/sniper
M=$B/Data/Scripts/modes
mkdir -p $OUT/ridge $OUT/shots
rm -f $OUT/shots/*.bmp $OUT/ridge/*.jpg
cp $P/tests/sniper/ridge_shots.lua $M/sniper-rshots.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=sniper-rshots GE_STAGE=levelgen GE_LEVELGEN=ridge GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-1500} GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_CHEATS=invuln GE_NOSLEEP=1 GE_CLOCKSCALE=4
L=$OUT/ridge_shots.log
cd $OUT/shots
timeout 300 $B/ProjectMidas > $L 2>&1
rm -f $M/sniper-rshots.lua
grep -a "RSHOT\|lua: error" $L
set -- $(grep -a "RSHOT [a-z]*$" $L | sed 's/.*RSHOT //')
for f in $(ls $OUT/shots/*.bmp 2>/dev/null | sort); do
  convert "$f" -resize 640x360! -quality 82 "$OUT/ridge/$1.jpg"
  if [ "$1" = window ]; then convert "$f" -gravity center -crop 33%x33%+0+0 +repage -resize 640x360! -quality 82 "$OUT/ridge/window-zoom.jpg"; fi
  shift
done
rm -f $OUT/shots/*.bmp
ls $OUT/ridge

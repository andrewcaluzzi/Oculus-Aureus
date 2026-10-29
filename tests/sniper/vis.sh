#!/bin/sh
# How far is a man drawn and seen on Surface (tests/sniper/vis.lua): pictures of a man at 40 .. 110 m from Bond's perch, each as a full frame and a 3x crop of
# its centre, 640x360 JPG in build_tests/sniper/vis/. usage: vis.sh [frames]
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/sniper
M=$B/Data/Scripts/modes
mkdir -p $OUT/vis $OUT/shots
rm -f $OUT/shots/*.bmp $OUT/vis/*.jpg
cp $P/tools/sniper_defs.lua $M/_sniper_defs.lua
cp $P/tests/sniper/vis.lua $M/sniper-vis.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=sniper-vis GE_STAGE=surface GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-1500} GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_CHEATS=invuln GE_NOSLEEP=1 GE_CLOCKSCALE=4
L=$OUT/vis.log
cd $OUT/shots
timeout 300 $B/ProjectMidas > $L 2>&1
rm -f $M/sniper-vis.lua
grep -a "SVIS" $L
set -- $(grep -a "SVIS [0-9]* m" $L | sed 's/.*SVIS \([0-9]*\) m.*/\1/')
for f in $(ls $OUT/shots/*.bmp 2>/dev/null | sort); do
  convert "$f" -resize 640x360! -quality 82 "$OUT/vis/m$1.jpg"
  convert "$f" -gravity center -crop 33%x33%+0+0 +repage -resize 640x360! -quality 82 "$OUT/vis/m$1-zoom.jpg"
  shift
done
rm -f $OUT/shots/*.bmp
ls $OUT/vis

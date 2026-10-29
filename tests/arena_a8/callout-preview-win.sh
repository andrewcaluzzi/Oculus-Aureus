#!/bin/sh
# The callout preview (Play Arena (Test Keys).bat's Home key: every chain, streak, event and boss card in turn, a8-callout-preview.lua)
# on Windows in hidden mode at one window size, with GE_CALLOUT_FITLOG=1: every drawn frame's letter span must stay inside the
# middle 90% of the console width. From Git Bash:
#   sh tests/arena_a8/callout-preview-win.sh <build dir> <WxH> [game frames, default 5400]
R=$(cd "$(dirname "$0")/../.." && pwd -W)
B=$1
WIN=$2
OUT=$B/callout
mkdir -p $OUT
cp $R/tests/arena_a8/a8-callout-preview.lua $B/Data/Scripts/modes/
rm -f $B/Data/Saves/eeprom.bin
L=$OUT/preview-$WIN.log
mkdir -p $OUT/shots-$WIN
cd $OUT/shots-$WIN || exit 1   # test.screenshot() writes into the cwd
env GE_HIDDEN=1 GE_WINDOW=$WIN GE_MODE=a8-callout-preview GE_STAGE=colosseum GE_SKIPINTRO=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 \
  GE_NOSCORESAVE=1 GE_NODIRECTLOG=1 GE_ARENA_TESTKEYS=1 GE_CHEATS=invuln GE_CALLOUT_FITLOG=1 GE_GFXFRAMES=${3:-5400} \
  GE_FRAMES=$(( ${3:-5400} * 20 )) "$B/ProjectMidas.exe" > $L 2>&1
echo "exit $?" >> $L
rm -f $B/Data/Scripts/modes/a8-callout-preview.lua
cards=$(grep -c "TEST callout preview" $L)
grep "callout fit:" $L | awk -v win=$WIN -v cards=$cards '{
    for (i = 1; i <= NF; i++) { if ($i == "span") { x0 = $(i+1); x1 = $(i+2) } if ($i == "safe") { s0 = $(i+1); s1 = $(i+2) }
                                if ($i == "k") k = $(i+1); if ($i == "w") w = $(i+1); if ($i == "lv") lv = $(i+1) }
    n++; if (k + 0 < 1.0) { fitted++; fl[lv]++ }
    if (x0 + 0 < s0 - 0.5 || x1 + 0 > s1 + 0.5) { bad++; if (bad <= 3) eg = eg " [" $0 "]" }
  } END { printf "preview %s: w %s, cards %d, frames %d, fitted %d (by level:", win, w, cards, n, fitted
          for (l = 1; l <= 6; l++) printf " %d", fl[l] + 0
          printf "), outside the safe area %d%s\n", bad + 0, eg }'
grep "TEST callout preview" $L | grep -i "WORLD IS NOT ENOUGH"
tail -1 $L

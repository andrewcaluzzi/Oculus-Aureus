#!/bin/sh
# The callout safe-area fit (port_arenahud.c calloutFit), inside the build container (Xvfb :99, build_pc):
# runs a8-callout-fit (the level-5/6 cards) once per entrance (GE_CALLOUT_ENTRANCE 0..6) at one window size and checks every logged
# frame's letter span against the middle 90% of the console width.
# usage: callout-fit.sh <WxH> [fitlog: 1 = fit on (default), 2 = fit off, the before] [entrances, default "0 1 2 3 4 5 6"]
# The game opens borderless fullscreen (no midas.ini), so the aspect is the Xvfb screen's: start Xvfb :99 at <WxH> first
# (e.g. Xvfb :99 -screen 0 2000x1125x24 +extension GLX); the log's "w" is the console width it got (853 at 16:9, 640 at 4:3).
P=/home/dev/project
WIN=${1:-2000x1125}
FL=${2:-1}
ENS=${3:-"0 1 2 3 4 5 6"}
OUT=$P/build_tests/a8
mkdir -p $OUT
fails=0
for en in $ENS; do
  name=fit-$WIN-f$FL-e$en
  sh $P/tests/arena_a8/run.sh $name a8-callout-fit 1700 invuln "GE_WINDOW=$WIN GE_CALLOUT_FITLOG=$FL GE_CALLOUT_ENTRANCE=$en" > /dev/null
  # "... span X0 X1 safe S0 S1 w W text ..."
  r=$(grep "callout fit:" $OUT/$name.log | awk '{
        for (i = 1; i <= NF; i++) { if ($i == "span") { x0 = $(i+1); x1 = $(i+2) } if ($i == "safe") { s0 = $(i+1); s1 = $(i+2) }
                                    if ($i == "k") k = $(i+1); if ($i == "need") nd = $(i+1); if ($i == "half") hf = $(i+1); if ($i == "w") w = $(i+1) }
        n++; if (k + 0 < 1.0) fitted++
        if (nd / hf > worst) worst = nd / hf
        if (x0 + 0 < s0 - 0.5 || x1 + 0 > s1 + 0.5) { bad++; if (bad <= 3) eg = eg sprintf(" [%s %s %s]", $0 ~ /echo/ ? "echo" : "main", x0, x1) }
      } END { printf "frames %d fitted %d over %d worst_need/half %.2f w %s%s", n, fitted, bad, worst, w, eg }')
  cards=$(grep -c "FITTEST card" $OUT/$name.log)
  echo "$name: cards $cards $r"
  case "$r" in *" over 0 "*) ;; *) fails=$((fails + 1)) ;; esac
  case "$r" in "frames 0 "*) fails=$((fails + 1)) ;; esac
done
echo "callout-fit $WIN fitlog=$FL: $fails failing runs"

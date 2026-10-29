#!/bin/sh
# The menu test (run.sh) with the window's X focus handed back every 2 s (xdotool windowfocus): Xvfb has no window manager, so a
# focus loss there is never undone by itself (the overnight "run 2 (Retry) mouse captured" failure). This shows whether the smart
# lock takes the pointer back when focus returns. Inside the build container (Xvfb :99, xdotool installed).
# usage: refocus.sh [game frames]
P=/home/dev/project
OUT=$P/build_tests/amenu
mkdir -p $OUT
sh $P/tests/arena_menu/run.sh ${1:-6000} > $OUT/refocus.out 2>&1 &
pid=$!
n=0
while kill -0 $pid 2>/dev/null; do
  sleep 2
  w=$(DISPLAY=:99 xdotool search --name "Project Midas" 2>/dev/null | head -1)
  if [ -n "$w" ]; then DISPLAY=:99 xdotool windowfocus $w 2>/dev/null; n=$((n + 1)); fi
done
cat $OUT/refocus.out
echo "refocus calls: $n"
echo "focus losses: $(grep -c 'mouse released (window lost focus)' $OUT/drive.log)"
echo "smart-lock captures: $(grep -c 'mouse captured (smart lock: gameplay)' $OUT/drive.log)"
grep -n "ge: input: mouse\|MENUTEST run [12] mouse\|flow settings\|menu: RETRY" $OUT/drive.log | head -30

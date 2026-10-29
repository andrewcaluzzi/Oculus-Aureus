#!/bin/sh
# Windows arena battery in hidden mode (GE_HIDDEN=1: no window is ever shown), from Git Bash, one game at a time:
#   sh tests/win/arena_battery.sh <build dir with Data/baserom.u.z64> [frames per mode, default 1800] [bench: 1 = also the late bench]
# 1. a short run of every arena mode as its launcher sets it (the launcher's env + GE_FRAMES): exit 0, the frame limit reached,
#    no Lua error / crash line;
# 2. the start menu lobby (Play Arena.bat's mode with the flow armed): it comes up;
# 3. with bench = 1: Arena Benchmark (Late Waves).bat's run (GE_BENCH_LATE=1), quitting by itself; prints its summary table
#    (hidden mode runs at BelowNormal priority, 1280x720, dummy audio: the draw counts are what to read, the ms are pessimistic).
B=$1
F=${2:-1800}
OUT=$B/battery
mkdir -p $OUT
cd $OUT || exit 1   # (anything a run writes to its cwd lands here, not in the caller's folder)
rm -f $B/Data/Saves/eeprom.bin
COMMON="GE_HIDDEN=1 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOSCORESAVE=1 GE_NOMENUSAVE=1 GE_NODIRECTLOG=1"
fails=0
one() { # name frames env...
  n=$1; f=$2; shift 2
  L=$OUT/$n.log
  env $COMMON GE_FRAMES=$f "$@" "$B/ProjectMidas.exe" > $L 2>&1
  rc=$?
  lim=$(grep -c "frame limit reached" $L)
  err=$(grep -ciE "lua: .*error|traceback|fatal|crash|assert" $L)
  echo "$n: exit $rc frame-limit $lim errors $err lines $(wc -l < $L) | $(grep -m1 -a 'ge: arena: wave [0-9]\|ge: arena: co-op ON\|menu: lobby\|flow on' $L)"
  if [ $rc != 0 ] || [ $lim = 0 ] || [ $err != 0 ]; then fails=$((fails + 1)); grep -m3 -iE "lua: .*error|traceback|fatal|crash|assert" $L; fi
}
one arena $F GE_MODE=arena
one arena-endless $F GE_MODE=arena-endless
one arena-chaos $F GE_MODE=arena-chaos
one arena-chaos-endless $F GE_MODE=arena-chaos-endless
one arena-coop $F GE_MODE=arena-coop GE_COOP=1
one arena-coop-chaos $F GE_MODE=arena-coop-chaos GE_COOP=1
one arena-whacky $F GE_MODE=arena-whacky
one arena-testkeys $F GE_MODE=arena GE_ARENA_TESTKEYS=1 GE_CHEATS=invuln,guns
one arena-whacky-testkeys $F GE_MODE=arena-whacky GE_ARENA_TESTKEYS=1 GE_CHEATS=invuln,guns
one arena-menu-lobby 900 GE_MODE=arena-menu GE_ARENA_MENU=1
echo "modes: $fails failing"
if [ "${3:-0}" = 1 ]; then
  rm -f $B/Data/Logs/arena_bench.txt
  L=$OUT/bench-late.log
  env GE_HIDDEN=1 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_MODE=arena-bench GE_BENCH_LATE=1 GE_CHEATS=invuln GE_NODIRECTLOG=1 "$B/ProjectMidas.exe" > $L 2>&1
  echo "bench-late: exit $? report $(wc -l < $B/Data/Logs/arena_bench.txt 2>/dev/null) lines"
  cp $B/Data/Logs/arena_bench.txt $OUT/arena_bench_late.txt 2>/dev/null
fi

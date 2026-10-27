#!/bin/sh
# Poor man's profiler (A7 perf): run a preset with sim-only ticks and sample the game thread's stack with gdb.
# usage: gdbsample.sh <preset> <samples> [depth] [render]  (render: normal frames instead of sim-only ticks)   (run as root in the container, cwd build_pc; needs gdb)
export DISPLAY=:99 GE_MODE=$1 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln GE_NOINISAVE=1 GE_TICKHZ=60 GE_FIXEDTICK=1
export GE_FRAMES=9000
if [ "$4" != "render" ]; then export GE_SIMONLY_TEST=700,8000; fi
/home/dev/project/build_pc/ProjectMidas > /home/dev/project/build_tests/a7/gdb-run.log 2>&1 &
sleep 60
PID=$(pidof ProjectMidas)
: > /home/dev/project/build_tests/a7/gdb-samples.txt
i=0
while [ $i -lt $2 ]; do
  gdb -p $PID -batch -ex "thread 1" -ex "bt ${3:-14}" 2>/dev/null | grep "^#" >> /home/dev/project/build_tests/a7/gdb-samples.txt
  echo "----" >> /home/dev/project/build_tests/a7/gdb-samples.txt
  i=$((i+1))
done
kill $PID

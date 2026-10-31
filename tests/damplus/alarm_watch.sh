#!/bin/sh
# Dam+ comms console: the alarm reinforcements really appear (guards with a prop, not just spawn attempts) while the console
# stands, and none after it falls. Runs tests/damplus/alarm_watch.lua (GE_LUA_TEST) on Dam with GE_DAMPLUS=1 and the test
# trigger GE_DAMPLUS_TEST=comms:5:90 (port_damplus.c dpCommsTick: the alarm at 5 s, kept sounding; the console destroyed at
# 90 s through the stock damage path). 00 Agent, Bond invulnerable at the start. Fast-forwarded (sim-only frames).
# Also logged (information, not judged): the motor-pool guards' rooms; the alarm alone does not move them (they run the stock
# idle list 0x0002); tests/damplus/motor_alert.sh checks their route out once they are alerted.
# PASS: at least two reinforcements appeared before the console fell, none after it, and the console was destroyed.
# Usage (container, Xvfb :99): sh tests/damplus/alarm_watch.sh [exe] [log]
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EXE=${1:-$ROOT/build_pc/ProjectMidas}
LOG=${2:-/tmp/alarm_watch.log}
export DISPLAY=${DISPLAY:-:99} GE_STAGE=dam GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_DIFF=2 GE_CHEATS=invuln GE_DAMPLUS=1 \
       GE_DAMPLUS_TEST=comms:5:90 GE_LUA_TEST=$ROOT/tests/damplus/alarm_watch.lua GE_TICKHZ=60 GE_SEED=1 \
       GE_NOSLEEP=1 GE_FIXEDTICK=1 GE_CLOCKSCALE=20 GE_SIMONLY_TEST=2,59,60 GE_FRAMES=${GE_FRAMES:-2000000} \
       GE_FULLHEIGHT=0 GE_EDGEINSET=1
"$EXE" > "$LOG" 2>&1
grep -a "DPALARM\|dam+: comms\|dam+: test\|dam+: verify\|lua: error" "$LOG"
awk '
  /dam\+: test: console shot at .*destroyed/ { gone = 1 }
  /DPALARM .* reinforcement chr [0-9]+ appeared/ { if (gone) after++; else before++ }
  /DPALARM done/ { done = 1 }
  END { printf "DPALARM reinforcements: %d appeared while the console stood, %d after it fell\n", before, after;
        print "DPALARM RESULT " ((done && gone && before >= 2 && after == 0) ? "pass" : "fail") }' "$LOG"

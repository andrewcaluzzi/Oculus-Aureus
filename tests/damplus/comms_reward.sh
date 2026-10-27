#!/bin/sh
# Dam+ concept 5: the comms console stops the alarm reinforcements, and only once it is destroyed.
# Runs Dam with GE_DAMPLUS=comms and the test trigger GE_DAMPLUS_TEST=comms:ALARM:DESTROY (port_damplus.c dpCommsTick: the alarm
# is sounded at ALARM s and kept sounding; the console is shot through the stock damage path at DESTROY s). Bond stands at the
# start, invisible to guards (GE_INVIS=1). Our replacement for background list 0x1002 (stock ai_22) spawns at pad 116 on each
# call and sets its own stage flag; the tick hook logs each call. PASS: at least two calls while the console stands (it keeps
# calling, 20 s apart), the console is destroyed, and no call after it.
# Usage (container, Xvfb :99): sh tests/damplus/comms_reward.sh [exe] [log]
EXE=${1:-/home/dev/project/build_pc/ProjectMidas}
LOG=${2:-/tmp/comms_reward.log}
ALARM=5
DESTROY=60
export DISPLAY=${DISPLAY:-:99} GE_STAGE=dam GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_DIFF=0 GE_INVIS=1 GE_DAMPLUS=comms \
       GE_DAMPLUS_TEST=comms:$ALARM:$DESTROY GE_FRAMES=${GE_FRAMES:-30000} GE_FULLHEIGHT=0 GE_EDGEINSET=1
"$EXE" > "$LOG" 2>&1
grep -a "dam+: comms\|dam+: test\|dam+: verify\|dam+: list" "$LOG"
awk -v d=$DESTROY '
  /dam\+: test: console shot at/ { shot = 1; if ($0 ~ /destroyed/) gone = 1 }
  /dam\+: comms: reinforcement call/ { t = $0; sub(/.* at /, "", t); sub(/ s .*/, "", t); t += 0;
      if (t < d && $0 ~ /console standing/) before++; else if (t >= d) after++ }
  END { ok = (before >= 2 && shot && gone && after == 0);
        printf "DPREWARD calls before %d, after %d, console destroyed %d\n", before, after, gone;
        print "DPREWARD RESULT " (ok ? "pass" : "fail") }' "$LOG"

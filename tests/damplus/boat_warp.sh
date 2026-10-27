#!/bin/sh
# Dam+ concept 3: the launch both ways. Runs Dam with GE_DAMPLUS=island and the test trigger GE_DAMPLUS_TEST=boat:4:16
# (port_damplus.c dpBoatTick: the pier's call box is pressed at 4 s, the island's at 16 s, through the stock activation flag).
# Bond starts in the canyon, invisible to guards. PASS: the first press lands Bond on the island quay (pad 80, room 81), the
# second on the west pier (room 69), in that order. (Not a Lua check: bond.* reads need a mode, and a mode would remove the
# guards and objects this exercises.)
# Usage (container, Xvfb :99): sh tests/damplus/boat_warp.sh [exe] [log]
EXE=${1:-/home/dev/project/build_pc/ProjectMidas}
LOG=${2:-/tmp/boat_warp.log}
export DISPLAY=${DISPLAY:-:99} GE_STAGE=dam GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_DIFF=0 GE_INVIS=1 GE_DAMPLUS=island \
       GE_DAMPLUS_TEST=boat:4:16 GE_FRAMES=${GE_FRAMES:-9000} GE_FULLHEIGHT=0 GE_EDGEINSET=1
"$EXE" > "$LOG" 2>&1
grep -a "dam+: launch\|dam+: test\|dam+: verify\|call box" "$LOG"
awk '
  /dam\+: launch: Bond at the island \(pad 80, room 81\)/ { if (!pier) isle = 1 }
  /dam\+: launch: Bond at the pier \(pad [0-9]+, room 69\)/ { if (isle) pier = 1 }
  END { print "DPBOAT RESULT " ((isle && pier) ? "pass" : "fail") }' "$LOG"

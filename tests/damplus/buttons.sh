#!/bin/sh
# Dam+ B presses through the real input path. The player bot (GE_PLAYERBOT=1, tests/bot/level.sh) plays tests/damplus/buttons.plan
# on Dam with GE_DAMPLUS=1 at Agent: a scripted warp to the west quay, then it walks to the pier's call box and presses B on
# player 1's pad (the launch to the island), B at the island's call box (back), a warp to the spillway balcony, a walk to the valve
# and B, then the bungee. Bond is invulnerable; GE_SOUNDLOG=1 logs every presentation sound. No replay (the plan warps).
# PASS: in order, the fade out and the warp to the island (room 81) with the screen black, the fade back; the same for the pier
# (room 69); then the spillway opened with its three sounds taken (non-zero handles) and the hydraulic sound (219) started (it
# logs before the "opened" line: the sounds are queued first); and the mission completes.
# Usage (container, Xvfb :99): sh tests/damplus/buttons.sh   (output in $BOTOUT/dam.d0/buttons.*; BOTOUT default build_tests/bot)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export TAG=buttons MAXSECS=${MAXSECS:-900} REPLAY=0 \
       EXTRA="GE_PLAYERBOT_PLAN=$ROOT/tests/damplus/buttons.plan GE_DAMPLUS=1 GE_CHEATS=invuln,ammo GE_SOUNDLOG=1 $EXTRA"
OUT=${BOTOUT:-$ROOT/build_tests/bot}/dam.d0
sh "$ROOT/tests/bot/level.sh" dam 0
LOG=$OUT/buttons.log
grep -a "dam+: launch\|dam+: spillway\|sfx: play .*(219)\|sfx: dropped\|playerbot: RESULT\|playerbot: step .*use obj" "$LOG"
awk '
  /dam\+: launch: casting off for the island/ { if (s == 0) s = 1 }
  /dam\+: launch: Bond at the island \(pad 80, room 81\).*screen black 1\.00/ { if (s == 1) s = 2 }
  /dam\+: launch: ready again.*screen black 0\.00/ { if (s == 2) s = 3; else if (s == 5) s = 6 }
  /dam\+: launch: casting off for the pier/ { if (s == 3) s = 4 }
  /dam\+: launch: Bond at the pier \(pad [0-9]+, room 69\).*screen black 1\.00/ { if (s == 4) s = 5 }
  /sfx: play .*\(219\)/ { if (s == 6) hyd = 1 }
  /dam\+: spillway opened.*sounds [1-9][0-9]* [1-9][0-9]* [1-9][0-9]*\)/ { if (s == 6) valve = 1 }
  /playerbot: RESULT.*outcome=COMPLETE/ { done = 1 }
  END { printf "DPBUTTONS launch stage %d of 6, valve %d, hydraulic sound %d, mission complete %d\n", s, valve, hyd, done;
        print "DPBUTTONS RESULT " ((s == 6 && valve && hyd && done) ? "pass" : "fail") }' "$LOG"

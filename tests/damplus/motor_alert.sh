#!/bin/sh
# Dam+ motor pool: once alerted, the guards route out through the shutter (their waypoints W0-W6 joined to stock waypoint 8 in
# room 135). The player bot plays tests/damplus/motor_alert.plan (key from chr 6, into the bay, seen, back out down the canyon)
# with tests/damplus/motor_watch.lua beside it (GE_LUA_TEST) watching the guards' rooms. GE_DAMPLUS=motor, Agent, invulnerable.
# (The alarm alone does not move them: they run the stock idle list 0x0002, which has no alarm branch, like stock Dam's idle
# guards; tests/damplus/alarm_watch.sh.) PASS: the DPMOTOR RESULT line.
# Usage (container, Xvfb :99): sh tests/damplus/motor_alert.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export TAG=motor MAXSECS=${MAXSECS:-120} REPLAY=${REPLAY:-0} \
       EXTRA="GE_PLAYERBOT_PLAN=$ROOT/tests/damplus/motor_alert.plan GE_DAMPLUS=motor GE_CHEATS=invuln,ammo GE_LUA_TEST=$ROOT/tests/damplus/motor_watch.lua $EXTRA"
OUT=${BOTOUT:-$ROOT/build_tests/bot}/dam.d0
sh "$ROOT/tests/bot/level.sh" dam 0
grep -a "DPMOTOR\|playerbot: RESULT\|lua: error" "$OUT/motor.log"
grep -a "DPMOTOR RESULT" "$OUT/motor.log" > /dev/null || echo "DPMOTOR RESULT fail (no result line)"

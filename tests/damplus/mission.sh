#!/bin/sh
# Dam+ full mission at 00 Agent: all four stock objectives (alarms, covert modem 0x100, data backup 0x400, bungee 0x1000) with every
# Dam+ part on (the extra guards, rooms and scripts present), the outro played and the mission ended complete. The player bot plays
# tests/damplus/dam00.plan: everything done at a target is pad input (aim, fire, equip the modem and fire it at the monitor, B at
# the mainframe); between targets the plan uses scripted warps where the bot's navigation stalls on stock Dam too.
# Bond is invulnerable with infinite ammo (GE_CHEATS=invuln,ammo: with invuln alone the bot runs dry and ends up punching an alarm
# box for minutes). No replay (the warps are not input). $1 = GE_DAMPLUS (default 1; 0 = the stock control).
# PASS: "ge: mission end: levelid=33 all_objectives_complete=1" and the bot's COMPLETE with objectives=4/4.
# Usage (container, Xvfb :99): sh tests/damplus/mission.sh [1|0]
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DP=${1:-1}
export TAG=mission$DP MAXSECS=${MAXSECS:-1500} REPLAY=0 \
       EXTRA="GE_PLAYERBOT_PLAN=$ROOT/tests/damplus/dam00.plan GE_DAMPLUS=$DP GE_CHEATS=invuln,ammo $EXTRA"
OUT=${BOTOUT:-$ROOT/build_tests/bot}/dam.d2
sh "$ROOT/tests/bot/level.sh" dam 2
LOG=$OUT/mission$DP.log
grep -a "playerbot: objective .* -> \|playerbot: flags\|mission end\|playerbot: RESULT\|dam+: verify" "$LOG"
if grep -aq "mission end: levelid=33 all_objectives_complete=1" "$LOG" && grep -aq "outcome=COMPLETE.*objectives=4/4" "$LOG"; then
    echo "DPMISSION RESULT pass"
else
    echo "DPMISSION RESULT fail"
fi

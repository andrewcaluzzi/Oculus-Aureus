#!/bin/sh
# Hidden Windows smoke of the sniper duel (Git Bash; no window: GE_HIDDEN=1): the launcher's environment (launchers/Play Sniper Duel.bat) with
# GE_FRAMES so it ends by itself and Bond invulnerable (nobody shoots the marksmen, so only the first rounds' glint / shot / relocate show).
# usage: win_smoke.sh <build dir, e.g. E:/midas-wt/sniper/build_win_sniper> [frames]    Prints the log lines and "exit <code>".
B=${1:?build dir}
export GE_HIDDEN=1 GE_STAGE=${STAGE:-surface} GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_MODE=${MODE:-sniper} GE_FRAMES=${2:-3000} GE_CHEATS=invuln GE_NOINISAVE=1
export GE_SNIPER_MENU=1 GE_NOMENUSAVE=1 GE_SEED=7
"$B/ProjectMidas.exe" > "$B/Data/sniper_smoke.log" 2>&1
echo "exit $?"
grep -a "ge: sniper:\|ge: lua: .*error\|frame limit" "$B/Data/sniper_smoke.log" | cut -c1-170 | head -${LINES_MAX:-30}

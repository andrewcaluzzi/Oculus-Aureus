#!/bin/sh
# Hidden Windows smoke of the SNIPER DUEL v2 on the snowfield (Git Bash; no window: GE_HIDDEN=1): tests/sniper/smoke_v2.lua.
# usage: win_smoke_v2.sh <build dir, e.g. E:/midas-wt/sniper/build_win_sniperv2> [frames] ["GE_X=y ..."]
#   "GE_LEVELGEN=jungle"               the same on the jungle (a build with the jungle level)
#   (default)                          the win path: Bond invulnerable, each marksman shot dead after he has fired twice
#   "GE_SNIPER_LOSE=1 GE_CHEATS="      the loss path: Bond mortal, the marksmen hit often
#   "GE_SNIPER_CLOSE=1"                close range: Bond walked to within 60 m of a marksman
#   "GE_SNIPER_SHOTS=1"                pictures (BMPs in the current folder)
# Prints the checks, the key log lines and "exit <code>". The log is <build>/Data/sniper2_smoke.log.
B=${1:?build dir}
cp "$(dirname "$0")/smoke_v2.lua" "$B/Data/Scripts/modes/sniper2-smoke.lua"
export GE_HIDDEN=1 GE_STAGE=levelgen GE_LEVELGEN=snowfield GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_MODE=sniper2-smoke GE_FRAMES=${2:-9000}
export GE_CHEATS=invuln GE_NOINISAVE=1 GE_NOMENUSAVE=1 GE_SEED=7 GE_NOMSGBOX=1
for kv in $3; do export "$kv"; done
L="$B/Data/sniper2_smoke.log"
if [ -n "$SHOTDIR" ]; then mkdir -p "$SHOTDIR"; cd "$SHOTDIR" || exit 1; fi   # (pictures land in the working folder)
"$B/ProjectMidas.exe" > "$L" 2>&1
echo "exit $?"
rm -f "$B/Data/Scripts/modes/sniper2-smoke.lua"
pass=0; fail=0
chk() { if grep -aq "$2" "$L"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
chk "the duel starts" "ge: sniper2: stage start"
chk "round 1 begins" "ge: sniper2: ROUND 1/"
chk "a marksman arrives unseen" "ge: sniper2: ARRIVE 1: foe 1 on .* unseen"
chk "a glint" "ge: sniper2: GLINT 1:"
chk "a shot" "ge: sniper2: SHOT: foe 1 fired\|ge: sniper2: HIT 1 of"
if grep -aq "SMOKE VIOLATION" "$L"; then echo "FAIL a marksman appeared or vanished in view"; fail=$((fail+1)); else echo "PASS no marksman appeared or vanished in view ($(grep -ac 'SMOKE appear' "$L") appearances, $(grep -ac 'SMOKE vanish' "$L") departures checked)"; pass=$((pass+1)); fi
if [ -n "$GE_SNIPER_LOSE" ]; then
  chk "Bond is hit" "ge: sniper2: HIT 1 of"
  chk "the run ends with Bond down" "ge: sniper2: END: DOWN"
elif [ -n "$GE_SNIPER_P3" ]; then
  if grep -aq "ge: sniper2: stage start: .*storms off" "$L"; then
    echo "SKIP the storm (this stage has none: S.storm = false)"
  else
    chk "the storm rolls in" "ge: sniper2: STORM: rolling in"
    chk "... reaches full" "ge: sniper2: STORM: full"
    chk "... clears" "ge: sniper2: STORM: calm"
  fi
  chk "thermal on (Q)" "ge: sniper2: THERMAL: on for"
  chk "thermal off after its time" "ge: sniper2: THERMAL: off"
  chk "round 1 won" "ge: sniper2: ROUND 1 WON"
  chk "the flush-out sends grunts" "ge: sniper2: FLUSH: [1-9] grunts sent"
  if grep -a "ge: sniper2: FLUSH grunt [0-9]* (chr" "$L" | grep -aq " SEEN "; then echo "FAIL a grunt started in Bond's view"; fail=$((fail+1)); else echo "PASS every grunt started out of Bond's view"; pass=$((pass+1)); fi
  chk "the grunts close in" "ge: sniper2: FLUSH: [0-9] alive, the nearest"
  chk "the flush-out ends" "ge: sniper2: FLUSH over"
  chk "round 2 follows" "ge: sniper2: ROUND 2/"
  grep -a "ge: sniper2: \(STORM\|THERMAL\|FLUSH\)\|SMOKE pressed" "$L" | cut -c1-220 | head -40
elif [ -n "$GE_SNIPER_DETECT" ]; then
  chk "the meter is measured standing" "SMOKE DETECT stand"
  chk "... running" "SMOKE DETECT run"
  chk "... scoping" "SMOKE DETECT scope"
  chk "... prone" "SMOKE DETECT prone"
  chk "... hidden (it falls)" "SMOKE DETECT hidden: average -"
  chk "the binoculars tag him" "ge: sniper2: TAG: foe 1"
  grep -a "SMOKE DETECT\|SMOKE detect\|ge: sniper2: TAG\|ge: sniper2: PRONE\|ge: sniper2: SPOTTED" "$L" | cut -c1-260
elif [ -n "$GE_SNIPER_CLOSE" ]; then
  chk "close-range glints" "GLINT .*(CLOSE RANGE)"
else
  chk "a marksman moves (leaves unseen)" "ge: sniper2: LEAVE 1:"
  chk "Bond kills him" "ge: sniper2: KILL: foe 1 down"
  chk "round 1 won" "ge: sniper2: ROUND 1 WON"
fi
if grep -aq "ge: lua: .*error" "$L"; then echo "FAIL a Lua error"; grep -a "ge: lua: .*error" "$L" | head -3; fail=$((fail+1)); else echo "PASS no Lua errors"; pass=$((pass+1)); fi
echo "-- log"
grep -a "ge: sniper2: \(stage\|perches\|ROUND\|ARRIVE\|LEAVE\|GLINT\|SHOT\|KILL\|HIT\|END\|foe\)\|SMOKE \(appear\|vanish\|close\|VIOL\|bond\|start\|killed\)" "$L" | cut -c1-230 | head -${LINES_MAX:-70}
echo "sniper2: $pass passed, $fail failed"

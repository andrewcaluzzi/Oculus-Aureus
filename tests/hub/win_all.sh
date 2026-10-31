#!/bin/sh
# The consolidated build's hub smoke on Windows, hidden (GE_HIDDEN=1: no window), from Git Bash: the hub boots with the launcher's
# environment (Play Oculus Aureus (All).bat) and tests/hub/all_drive.lua opens one thing through the menus.
# usage: win_all.sh <build dir> <what> [frames]
#   what: arena | bank | casino | ridge | surface | snowfield | jungle | damplus | goldeneye | shots | jungle-debrief
#   (jungle-debrief: the jungle duel from the card, Bond killed 25 s in, the hub's debrief records it)
# The log is <build>/Data/hub_all_<what>.log; SHOTDIR=<dir> puts the pictures (BMP) there.
B=${1:?build dir}
W=${2:?what}
R=$(cd "$(dirname "$0")/../.." && pwd -W)
cp "$R/tests/hub/all_drive.lua" "$B/Data/Scripts/zz_all_drive.lua"
rm -f "$B/Data/Saves/eeprom.bin.alltest"
export GE_HIDDEN=1 GE_MODE=hub GE_STAGE=control GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_HUB_DAMPLUS=${GE_HUB_DAMPLUS:-1} GE_HUB_GOLDENEYE=${GE_HUB_GOLDENEYE:-1}   # (the hidden tiles shown, so every entry can be driven)
export GE_FRAMES=${3:-4000} GE_NOSLEEP=1 GE_CLOCKSCALE=20 GE_UIMENU_DIR="$B/Data/alltest_ui" GE_NOMENUSAVE=1
export GE_SCOREFILE="$B/Data/alltest_ui/scores.ini" GE_NOSCORESAVE=1 GE_ARENAMENU_FILE="$B/Data/alltest_ui/arena_menu.ini"   # (the player's files untouched)
mkdir -p "$B/Data/alltest_ui"
case $W in
  jungle-debrief) export GE_HUBGO_JUNGLE=1 GE_HUBKILL=1 GE_SNIPER_MENU=1 ;;  *) export "GE_HUBGO_$(echo "$W" | tr a-z A-Z)=1" ;;
esac
L="$B/Data/hub_all_$W.log"
if [ -n "$SHOTDIR" ]; then mkdir -p "$SHOTDIR"; cd "$SHOTDIR" || exit 1; fi
"$B/ProjectMidas.exe" > "$L" 2>&1 &
pid=$!
if [ "$W" = goldeneye ]; then   # (the Files screen idles until GE_FRAMES: stop once it is up)
  while kill -0 $pid 2>/dev/null; do
    if grep -aq "ge: front: Files screen up" "$L"; then sleep 2; kill $pid 2>/dev/null; break; fi
    sleep 1
  done
fi
wait $pid
echo "exit $?" >> "$L"
[ -z "$KEEP_DRIVER" ] && rm -f "$B/Data/Scripts/zz_all_drive.lua"   # (KEEP_DRIVER=1: runs in parallel on one build share it)
pass=0; fail=0
chk() { if grep -aq "$2" "$L"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
chk "hidden" "ge: hidden: window never shown"
chk "the hub is up on Control" "ge: hub: menu: stage start: CONTROL"
case $W in
  arena)
    chk "the arena lobby hosted" "ge: hub: menu: arena: lobby up on this stage"
    chk "the arena run is armed" "ge: arenamenu: launch armed: mode 'arena"
    chk "the colosseum loads with the mode" "ge: mode: 'arena[a-z_-]*' on stage"
    chk "a wave starts" "WAVE 1\|wave 1" ;;
  bank|casino)
    chk "the heist lobby hosted" "ge: hub: menu: heist: lobby up on this stage"
    chk "the job card" "ALLTEST heist card: job $W"
    chk "the run is armed" "ge: uimenu: launch armed: mode 'heist'"
    chk "the slot holds the job's level" "ge: levelgen: the levelgen slot holds '$W'"
    chk "the heist is ready" "ge: heist: ready:" ;;
  ridge|surface)
    chk "the sniper card hosted" "ge: hub: menu: sniper: lobby up on this stage"
    chk "the v1 duel is armed" "ge: uimenu: launch armed: mode 'sniper'"
    [ "$W" = ridge ] && chk "the slot holds the ridge" "ge: levelgen: the levelgen slot holds 'ridge'"
    chk "the stage loads with the mode" "ge: mode: 'sniper' on stage"
    chk "round 1 starts" "ge: sniper: ROUND 1/5" ;;
  snowfield|jungle|jungle-debrief)
    S=${W%-debrief}
    chk "the sniper card hosted" "ge: hub: menu: sniper: lobby up on this stage"
    chk "the v2 duel is armed" "ge: uimenu: launch armed: mode 'sniper-v2'"
    chk "the slot holds the $S" "ge: levelgen: the levelgen slot holds '$S'"
    chk "the v2 duel starts on the $S" "ge: sniper2: stage start: .*(stage $S)"
    chk "round 1 starts" "ge: sniper2: ROUND 1/"
    if [ "$W" = jungle-debrief ]; then
      chk "Bond is killed" "ALLTEST killing Bond"
      chk "the run ends" "ge: sniper2: END: DOWN"
      chk "the hub comes back on the sniper debrief" "ge: uimenu: after the stage: launching again: mode 'hub' preset '$S' lobby 'sniper/postgame'"
      chk "the debrief is the hosted sniper card's" "ge: hub: menu: sniper: lobby up on this stage (postgame)"
      chk "the best is recorded" "ge: sniper: menu: recorded: runs 1"
    fi ;;
  damplus)
    chk "the Dam+ card hosted" "ge: hub: menu: damplus: lobby up on this stage"
    chk "the stock mission is armed" "ge: uimenu: launch armed: mode '0'"
    chk "Dam+ is on" "ge: dam+: on (parts 0x"
    chk "its guards spawned" "ge: dam+: verify: guards [1-9][0-9]*/"
    n=$(grep -ac "^ge: mode: '[a-z_-]*' on stage" "$L")
    if [ "$n" = 1 ]; then echo "PASS no mode script on Dam (one mode stage: the hub)"; pass=$((pass+1)); else echo "FAIL mode stages: $n (want 1, the hub)"; fail=$((fail+1)); fi ;;
  goldeneye)
    chk "to the Files screen" "ge: hub: menu: goldeneye: to the Files screen"
    chk "the Files screen came up" "ge: front: Files screen up" ;;
  shots)
    chk "the pictures" "ALLTEST shots done" ;;
esac
if grep -aq "ge: lua: .*error" "$L"; then echo "FAIL a Lua error"; grep -a "ge: lua: .*error" "$L" | head -3; fail=$((fail+1)); else echo "PASS no Lua errors"; pass=$((pass+1)); fi
grep -a "ge: uimenu: \(launch armed\|after the stage\)" "$L" | cut -c1-200 | head -6
echo "hub all $W: $pass passed, $fail failed"

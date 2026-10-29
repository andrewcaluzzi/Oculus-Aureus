#!/bin/sh
# Smoke of the sniper duel on Surface in the build container (Xvfb on :99, build_pc): the real ladder, fast-forward (tests/sniper/smoke.lua).
# usage: run.sh [game frames] ["GE_X=y ..."]   Log: build_tests/sniper/smoke.log (lose.log for the loss variant), pictures: build_tests/sniper/shots
#   (default)            Bond invulnerable and "killing" each marksman with a real shot: the whole ladder, the win, the debrief on the hub's stage
#   "GE_SNIPER_LOSE=1 GE_CHEATS="   nobody shoots back and Bond is mortal: the marksmen kill him (a hit takes 42% of a bar): the loss path and its debrief
# The run ends in the debrief on the hub's Control stage (the hand-off), which the checks read from the lobby's own log lines.
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/sniper
M=$B/Data/Scripts/modes
mkdir -p $OUT/shots
rm -f $OUT/shots/*.bmp
for f in mode_sniper:sniper mode_sniper_menu:sniper-menu sniper_defs:_sniper_defs mode_hub:hub hub_defs:_hub_defs ui_menu:_ui_menu; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
cp $P/tests/sniper/smoke.lua $M/sniper-smoke.lua
rm -f $B/Data/Saves/eeprom.bin $OUT/sniper_bests.ini
export DISPLAY=:99 GE_MODE=sniper-smoke GE_STAGE=surface GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-9000} GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_CHEATS=invuln GE_UIMENU_DIR=$OUT GE_NOMENUSAVE=1 GE_SNIPER_MENU=1
if [ "${FAST:-1}" = 1 ]; then export GE_NOSLEEP=1 GE_CLOCKSCALE=20; fi
for kv in $2; do export "$kv"; done
LOSE=0
if [ -n "$GE_SNIPER_LOSE" ]; then LOSE=1; fi
L=$OUT/smoke.log
if [ $LOSE = 1 ]; then L=$OUT/lose.log; fi
cd $OUT/shots
S=$(date +%s)
timeout ${TIMEOUT:-900} $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
E=$(date +%s)
rm -f $M/sniper-smoke.lua
pass=0; fail=0
chk() { if grep -aq "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
echo "== $((E - S)) s wall for $GE_FRAMES frames"
chk "the run starts on Surface" "ge: mode: 'sniper-smoke' on stage"
chk "Bond's perch found by the sight sweep" "ge: sniper: Bond's perch is pad [0-9]* .* with [0-9]* enemy perches in the band"
chk "round 1 begins" "ge: sniper: ROUND 1/5"
chk "a glint before a shot" "ge: sniper: GLINT 1: foe 1 on pad"
chk "the marksman takes the shot" "ge: sniper: foe 1: the marksman takes the shot"
chk "he fires (spawn.shots)" "ge: sniper: SHOT: foe 1 fired"
chk "he relocates after firing" "ge: sniper: RELOCATE 1: foe 1 leaves pad [0-9]* (fired)"
chk "he glints again on another perch" "ge: sniper: GLINT 2: foe 1 on pad"
if [ $LOSE = 0 ]; then
  chk "Bond kills him" "ge: sniper: KILL: foe 1 down"
  chk "round 1 won" "ge: sniper: ROUND 1 WON"
  chk "round 2 begins" "ge: sniper: ROUND 2/5"
  chk "round 3 sends rushers" "ge: sniper: ROUND 3/5"
  chk "a rusher spawns" "ge: sniper: rusher 1 (chr"
  chk "the rushers close in" "SMOKE rushers: [0-9] alive, the nearest [0-9]* m"
  chk "round 4 has two marksmen" "ge: sniper: ROUND 4/5: .* 2 marksmen"
  chk "round 5 begins" "ge: sniper: ROUND 5/5"
  chk "the run is won" "ge: sniper: END: WON, 5/5"
else
  chk "a marksman hits Bond" "ge: sniper: HIT on Bond by foe"
  chk "the run ends with Bond down" "ge: sniper: END: DOWN"
  chk "the killer is named" "felled by [A-Z]"
fi
chk "the debrief is armed for the hub" "ge: uimenu: after the stage: launching again: mode 'hub' preset 'surface' lobby 'sniper/postgame'"
chk "the hub opens the sniper lobby on the debrief" "ge: hub: menu: sniper: lobby up on this stage (postgame)"
chk "the debrief page" "ge: sniper: menu: lobby: debrief"
chk "the run was recorded" "ge: sniper: menu: recorded: runs 1"
if grep -aq "ge: lua: .*error" $L; then echo "FAIL a Lua error"; grep -a "ge: lua: .*error" $L | head -3; fail=$((fail+1)); else echo "PASS no Lua errors"; pass=$((pass+1)); fi
echo "-- numbers"
grep -a "ge: sniper: sweep:" $L | head -8
grep -a "SMOKE spawn chr" $L | head -14
grep -a "SMOKE rushers" $L | head -12
grep -a "ge: sniper: GLINT\|ge: sniper: SHOT\|ge: sniper: RELOCATE\|ge: sniper: KILL\|ge: sniper: HIT\|ge: sniper: ROUND\|ge: sniper: rusher\|ge: sniper: END\|ge: sniper: difficulty" $L | cut -c1-200 | head -70
grep -a "FIRST SHOT" $L | head -8
echo "sniper: $pass passed, $fail failed"

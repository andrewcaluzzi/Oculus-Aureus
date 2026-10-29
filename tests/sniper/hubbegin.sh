#!/bin/sh
# The hub's SNIPER DUEL card BEGINs the duel (build_pc under Xvfb :99): the hub boots (the launcher's environment), tests/sniper/hubshots.lua moves to the
# SNIPER DUEL tile, SELECTs it and presses BEGIN THE DUEL; the launch (ui.launch{mode = "sniper", stage = "surface"}) loads Surface with the mode and the
# run starts its first round. usage: hubbegin.sh [frames]    Log: build_tests/sniper/hubbegin.log
P=/home/dev/project
B=$P/${BUILD:-build_pc}
OUT=$P/build_tests/sniper
M=$B/Data/Scripts/modes
mkdir -p $OUT/shots
rm -f $OUT/shots/*.bmp
for f in mode_sniper:sniper mode_sniper_menu:sniper-menu sniper_defs:_sniper_defs mode_hub:hub hub_defs:_hub_defs ui_menu:_ui_menu; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
cp $P/tests/sniper/hubshots.lua $B/Data/Scripts/zz_sniper_shots.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=hub GE_STAGE=control GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-3500} GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_UIMENU_DIR=$OUT GE_NOMENUSAVE=1 GE_SNIPER_MENU=1 GE_SNIPER_BEGIN=1 GE_NOSLEEP=1 GE_CLOCKSCALE=20
L=$OUT/hubbegin.log
cd $OUT/shots
timeout 600 $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $B/Data/Scripts/zz_sniper_shots.lua
rm -f $OUT/shots/*.bmp
pass=0; fail=0
chk() { if grep -aq "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
chk "the hub is up" "ge: hub: menu: stage start: CONTROL"
chk "the SNIPER DUEL card opens (hosted)" "ge: hub: menu: sniper: lobby up on this stage (from the main menu)"
chk "BEGIN THE DUEL" "ge: sniper: menu: BEGIN SNIPER DUEL"
chk "the launch is armed with the stage" "ge: uimenu: launch armed: mode 'sniper'"
chk "the stage loads again with the mode" "ge: mode: 'sniper' on stage"
chk "the sight sweep picks a perch" "ge: sniper: Bond's perch is pad"
chk "round 1 starts" "ge: sniper: ROUND 1/5"
chk "a glint" "ge: sniper: GLINT 1:"
if grep -aq "ge: lua: .*error" $L; then echo "FAIL a Lua error"; grep -a "ge: lua: .*error" $L | head -3; fail=$((fail+1)); else echo "PASS no Lua errors"; pass=$((pass+1)); fi
grep -a "ge: uimenu: \(launch armed\|after the stage\)" $L | cut -c1-200 | head -4
echo "hubbegin: $pass passed, $fail failed"

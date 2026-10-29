#!/bin/sh
# The heist start menu / debrief test inside the build container (Xvfb on :99, build_pc): see hm_drive.lua for the flow.
# usage: run.sh [frames] [casino]   (log: build_tests/heist_menu/drive.log or casino.log, shots: build_tests/heist_menu/shots/*.bmp)
# Checks at the end: two runs started with identical settings, a win and a fail reached the debrief, the menu came back with the
# run's settings, EXIT closed the game, no stock report; the casino variant: the lobby moved to the casino at runtime and
# a casino job ended on its debrief.
P=/home/dev/project
OUT=$P/build_tests/heist_menu
mkdir -p $OUT/shots
rm -f $OUT/shots/*.bmp
M=$P/build_pc/Data/Scripts/modes
for f in mode_heist:heist mode_heist_menu:heist-menu ui_menu:_ui_menu heist_theme:_heist_theme heist_menu_defs:_heist_menu_defs heist_flow:_heist_flow \
         heist_debrief:heist_debrief heist_planning:heist_planning heist_lib:heist_lib heist_bank_map:heist_bank_map; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
cp $P/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
cp $P/port/levelgen_casino/levelgen_layout.lua $M/levelgen_layout_casino.lua
cp $P/tests/heist/heist_winpath.lua $M/heist_winpath.lua
cp $P/tests/heist_menu/heist_flowtest.lua $M/heist_flowtest.lua
cp $P/tests/heist_menu/hm_drive.lua $P/build_pc/Data/Scripts/zz_hm_drive.lua
rm -f $P/build_pc/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=heist-menu GE_STAGE=levelgen GE_LEVELGEN=bank GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-26000} GE_FULLHEIGHT=0 GE_EDGEINSET=1
export GE_UIMENU_DIR=$OUT GE_NOMENUSAVE=1 GE_HEIST_MENU=1 GE_HEISTMENU_TEST=1 GE_PLANNING=1
cd $OUT/shots
pass=0; fail=0
if [ "$2" = casino ]; then
  export GE_HM_CASINO=1
  L=$OUT/casino.log
  timeout ${TIMEOUT:-900} $P/build_pc/ProjectMidas > $L 2>&1
  echo "exit $?" >> $L
  rm -f $P/build_pc/Data/Scripts/zz_hm_drive.lua
  chk() { if grep -q "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
  chk "the bank lobby" "MENUTEST bank lobby: page main"
  chk "the casino card moves the lobby" "job casino: moving the lobby to its level"
  chk "the levelgen slot holds the casino" "the levelgen slot holds 'casino' this stage"
  chk "the casino lobby" "MENUTEST casino lobby: page main"
  chk "a casino run at the menu's difficulty" "MENUTEST run seq .*difficulty 1, level casino"
  chk "the casino job's map" "stage start: Le Grand Casino Janus"
  chk "busted" "flow finish: BUSTED"
  chk "the casino debrief" "MENUTEST casino debrief: page debrief"
  chk "back to the bank after the casino" "MENUTEST bank again: page main .* level bank"
  n=$(grep -c "the levelgen slot holds" $L)
  if [ "$n" -ge 5 ]; then echo "PASS five level loads (bank, casino, casino, casino, bank): $n"; pass=$((pass+1)); else echo "FAIL level loads $n"; fail=$((fail+1)); fi
  chk "exit" "ge: ui: exit the game"
  if grep -q "end screen: Mission Failed" $L; then echo "FAIL the stock report showed"; fail=$((fail+1)); else echo "PASS no stock report"; pass=$((pass+1)); fi
  if grep -q "ge: lua: .*error" $L; then echo "FAIL a Lua error"; grep "ge: lua: .*error" $L | head -3; fail=$((fail+1)); else echo "PASS no Lua errors"; pass=$((pass+1)); fi
  echo "heist_menu casino: $pass passed, $fail failed"
  exit 0
fi
L=$OUT/drive.log
timeout ${TIMEOUT:-1500} $P/build_pc/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $P/build_pc/Data/Scripts/zz_hm_drive.lua
chk() { if grep -q "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
chk "the lobby on boot" "MENUTEST boot: page main"
chk "the menu has the mouse in the lobby" "MENUTEST boot mouse: menu_up true"
chk "Q BRANCH" "ge: heist: menu: page settings"
chk "settings changed" "MENUTEST settings: page settings .*deal=AS WRITTEN .*personalities=UNIFORM"
chk "BEGIN" "ge: heist: menu: BEGIN JANUS BANK  -  AGENT"
chk "run 1 on the planning board" "MENUTEST planning: buy and confirm"
chk "run 1: the menu let go of the mouse" "MENUTEST run 1 mouse: menu_up false gameplay_pointer true"
chk "run 1 difficulty from the menu" "MENUTEST run seq 1 (heist_flowtest): 1 Bonds, difficulty 0"
chk "run 1 won" "flow finish: WON"
chk "the debrief after the win" "MENUTEST debrief 1: page debrief"
chk "AGAIN" "ge: heist: menu: AGAIN"
chk "run 2 (seq 3: the lobby's own launch is seq 2)" "MENUTEST run seq 3 (heist_flowtest)"
chk "run 2 busted" "flow finish: BUSTED"
chk "the debrief after the fail" "MENUTEST debrief 2: page debrief"
chk "NEW JOB: the briefing" "MENUTEST after: page main"
chk "exit the game" "ge: ui: exit the game"
n=$(grep -c "ge: heist: flow settings:" $L)
u=$(grep "ge: heist: flow settings:" $L | sort -u | wc -l)
if [ "$n" = 2 ] && [ "$u" = 1 ]; then echo "PASS two runs, same settings"; pass=$((pass+1)); else echo "FAIL runs $n, distinct settings $u"; fail=$((fail+1)); fi
if grep "flow settings:" $L | grep -q "personalities=false" && grep "flow settings:" $L | grep -q "randomise=false"; then
  echo "PASS the menu's choices reached the run"; pass=$((pass+1)); else echo "FAIL the menu's choices"; fail=$((fail+1)); fi
a=$(grep "MENUTEST main:" $L | sed 's/.*| //'); b=$(grep "MENUTEST after:" $L | sed 's/.*| //')
if [ -n "$a" ] && [ "$a" = "$b" ]; then echo "PASS the menu came back with the run's settings"; pass=$((pass+1)); else echo "FAIL menu settings after"; fail=$((fail+1)); fi
if grep -q "end screen: Mission Failed" $L; then echo "FAIL the stock report showed"; fail=$((fail+1)); else echo "PASS no stock report"; pass=$((pass+1)); fi
if grep -q "ge: lua: .*error" $L; then echo "FAIL a Lua error"; grep "ge: lua: .*error" $L | head -3; fail=$((fail+1)); else echo "PASS no Lua errors"; pass=$((pass+1)); fi
grep "HEIST DONE" $L
echo "heist_menu: $pass passed, $fail failed"

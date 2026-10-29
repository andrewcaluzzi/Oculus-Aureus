#!/bin/sh
# The main menu hub test inside the build container (Xvfb on :99, build_pc): see hub_drive.lua for the flow.
# usage: run.sh [frames] [exit|ini]   (log: build_tests/hub/drive.log | exit.log | ini.log, shots: build_tests/hub/jpg/*.jpg)
#   (default)  boot -> ARENA (hosted on Control: its pages) -> MAIN MENU -> HEIST (hosted: the other job's card) -> MAIN MENU -> SETTINGS ->
#              ARENA -> BEGIN (the one load) -> the run ends -> its post-game on Control -> RETRY (load) -> post-game -> MAIN MENU -> HEIST ->
#              BEGIN THE JOB (load) -> the debrief on Control -> MAIN MENU -> GOLDENEYE 007 -> the Files screen.
#              The checks count the stage loads: the boot and the three runs' returns and the runs themselves, none between menu pages.
#   exit       the hub on first boot picks EXIT: the game closes
#   ini        the settings flow with midas.ini writing ON: the changed options are in the file afterwards (build_pc/midas.ini is
#              saved first and put back)
# The fast-forward settings (GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_CLOCKSCALE=20) are on by default; FAST=0 turns them off.
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/hub
mkdir -p $OUT/shots $OUT/jpg
rm -f $OUT/shots/*.bmp
M=$B/Data/Scripts/modes
for f in mode_hub:hub hub_defs:_hub_defs ui_menu:_ui_menu \
         mode_arena:arena arena_flow:_arena_flow arena_menu_defs:_arena_menu_defs arena_maps:_arena_maps mode_arena_menu:arena-menu mode_arena_scores:_arena_scores \
         mode_arena_crowd:_arena_crowd arena_polish:_arena_polish arena_theme:_arena_theme arena_coop:_arena_coop arena_plus:_arena_plus \
         mode_heist:heist mode_heist_menu:heist-menu heist_theme:_heist_theme heist_menu_defs:_heist_menu_defs heist_flow:_heist_flow \
         heist_debrief:heist_debrief heist_planning:heist_planning heist_lib:heist_lib heist_bank_map:heist_bank_map \
         mode_sniper:sniper mode_sniper_menu:sniper-menu sniper_defs:_sniper_defs; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
cp $P/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
cp $P/port/levelgen_casino/levelgen_layout.lua $M/levelgen_layout_casino.lua
cp $P/tests/heist/heist_winpath.lua $M/heist_winpath.lua
cp $P/tests/heist_menu/heist_flowtest.lua $M/heist_flowtest.lua
cp $P/tests/hub/hub_drive.lua $B/Data/Scripts/zz_hub_drive.lua
rm -f $B/Data/Saves/eeprom.bin
# (the launcher's own env: Play Oculus Aureus.bat)
export DISPLAY=:99 GE_MODE=hub GE_STAGE=control GE_NOINTRO=1 GE_STAGE_AFTER=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-40000} GE_FULLHEIGHT=0 GE_EDGEINSET=1
if [ "${FAST:-1}" = 1 ]; then export GE_NOSLEEP=1 GE_CLOCKSCALE=20; fi
export GE_UIMENU_DIR=$OUT GE_NOMENUSAVE=1 GE_SCOREFILE=$OUT/scores.ini GE_NOSCORESAVE=1 GE_ARENAMENU_FILE=$OUT/arena_menu.ini
export GE_ARENA_MENU=1 GE_HEIST_MENU=1 GE_HEISTMENU_TEST=1 GE_HUB_TEST_KILL=1
rm -f $OUT/scores.ini $OUT/arena_menu.ini $OUT/heist_bests.ini $OUT/heist_menu.ini $OUT/hubtest.ini
cd $OUT/shots
pass=0; fail=0
chk() { if grep -q "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
common() {
  if grep -q "end screen: Mission Failed\|mission report" $L; then echo "FAIL the stock report showed"; fail=$((fail+1)); else echo "PASS no stock report"; pass=$((pass+1)); fi
  if grep -q "ge: lua: .*error" $L; then echo "FAIL a Lua error"; grep "ge: lua: .*error" $L | head -3; fail=$((fail+1)); else echo "PASS no Lua errors"; pass=$((pass+1)); fi
}
if [ "$2" = exit ]; then
  export GE_HUB_TEST=1
  L=$OUT/exit.log
  timeout ${TIMEOUT:-600} $B/ProjectMidas > $L 2>&1
  echo "exit $?" >> $L
  rm -f $B/Data/Scripts/zz_hub_drive.lua
  chk "the hub on the Control stage" "ge: hub: menu: stage start: CONTROL"
  chk "the hub is up" "MENUTEST hub up: step 1, launched by boot"
  chk "EXIT" "ge: hub: menu: EXIT the game"
  chk "the game closed" "ge: ui: exit the game"
  chk "exit status 0" "^exit 0"
  n=$(grep -c "^ge: intro:" $L)
  if [ "$n" = 0 ]; then echo "PASS no intro cameras"; pass=$((pass+1)); else echo "FAIL intro lines: $n"; fail=$((fail+1)); fi
  common
  echo "hub exit: $pass passed, $fail failed"
  exit 0
fi
if [ "$2" = ini ]; then
  unset GE_NOINISAVE
  cp $B/midas.ini $OUT/midas.ini.saved
  L=$OUT/ini.log
  timeout ${TIMEOUT:-900} $B/ProjectMidas > $L 2>&1
  echo "exit $?" >> $L
  cp $B/midas.ini $OUT/midas.ini.after
  cp $OUT/midas.ini.saved $B/midas.ini
  rm -f $B/Data/Scripts/zz_hub_drive.lua
  chk "settings applied" "ge: hub: menu: setting mouse_sens_x = .* (applied)"
  chk "the port wrote midas.ini" "ge: midas.ini: saved [0-9]* \[display\] option"
  m=$(grep "setting music_volume" $L | head -1 | sed 's/.*= \([0-9]*\)%.*/\1/')
  if [ -n "$m" ] && grep -q "^music_volume=$m" $OUT/midas.ini.after; then echo "PASS music_volume=$m is in midas.ini"; pass=$((pass+1)); else echo "FAIL music_volume in midas.ini ($m)"; fail=$((fail+1)); fi
  s=$(grep "setting mouse_sens_x" $L | head -1 | sed 's/.*= \([0-9.]*\) .*/\1/')
  if [ -n "$s" ] && grep -q "^mouse_sens_x=$s" $OUT/midas.ini.after; then echo "PASS mouse_sens_x=$s is in midas.ini"; pass=$((pass+1)); else echo "FAIL mouse_sens_x in midas.ini ($s)"; fail=$((fail+1)); fi
  common
  echo "hub ini: $pass passed, $fail failed"
  exit 0
fi
L=$OUT/drive.log
timeout ${TIMEOUT:-1800} $B/ProjectMidas > $L 2>&1 &
pid=$!
# the flow ends on the Files screen, where the game would idle until GE_FRAMES: stop it there
while kill -0 $pid 2>/dev/null; do
  if grep -aq "ge: front: Files screen up" $L; then sleep 3; kill $pid 2>/dev/null; break; fi
  sleep 2
done
wait $pid
echo "exit $?" >> $L
rm -f $B/Data/Scripts/zz_hub_drive.lua
chk "step 1: the hub on first boot, the hub flag set" "MENUTEST hub up: step 1, launched by boot lobby 'nil' hub flag true"
chk "no intro cameras at all" "^ge: GE_STAGE direct launch"
n=$(grep -c "^ge: intro:" $L)
if [ "$n" = 0 ]; then echo "PASS no intro cameras ($n)"; pass=$((pass+1)); else echo "FAIL intro lines: $n"; fail=$((fail+1)); fi
chk "ARENA hosted on the hub's stage" "ge: hub: menu: arena: lobby up on this stage (from the main menu)"
chk "the arena start menu is up in Control" "MENUTEST arena hosted: arena screen main cursor [0-9]* (.*) busy false | music elevator_control | menu_up true"
chk "the arena's advanced page" "MENUTEST arena advanced: arena screen advanced"
chk "back to its main page" "MENUTEST arena back on main: arena screen main"
chk "the arena's exit row is MAIN MENU" "MENUTEST arena exit row: arena screen main cursor [0-9]* (MAIN MENU)"
chk "back to the hub's page" "ge: hub: menu: arena: back to the main menu"
chk "the hub's page again" "MENUTEST hub after arena: page main"
chk "HEIST hosted on the hub's stage" "ge: hub: menu: heist: lobby up on this stage (from the main menu)"
chk "the heist start menu is up in Control" "MENUTEST heist hosted: heist page main .* | music elevator_control"
chk "the other job's card, no stage load" "ge: heist: menu: job casino\|MENUTEST heist casino card: heist page main .* job bank"
chk "the heist exit row is MAIN MENU" "MENUTEST heist exit row: heist page main cursor [0-9]* (MAIN MENU)"
chk "back to the hub's page from the heist" "ge: hub: menu: heist: back to the main menu"
chk "SETTINGS page" "ge: hub: menu: page settings"
chk "music volume applied" "ge: hub: menu: setting music_volume = .* (applied)"
chk "sound volume applied" "ge: hub: menu: setting sfx_volume = .* (applied)"
chk "mouse look X applied" "ge: hub: menu: setting mouse_sens_x = .* (applied)"
chk "BEGIN: an Arena run, on the Colosseum" "ge: arenamenu: launch armed: mode 'arena'"
chk "the run is launched on the colosseum slot" "ge: arenamenu: after the stage: launching the colosseum again: mode 'arena'"
chk "run 1 up" "MENUTEST arena run (step 2)"
chk "run 1 ended, the post-game armed for the hub" "ge: arenamenu: after the stage: launching the control again: mode 'hub' preset '[a-z0-9]*' lobby 'arena/postgame'"
chk "step 3: the hub is up on the post-game" "MENUTEST hub up: step 3, launched by amenu lobby 'arena/postgame'"
chk "the post-game is the hosted arena's" "ge: hub: menu: arena: lobby up on this stage (postgame)"
chk "the post-game screen in Control" "MENUTEST arena debrief 1: arena screen postgame"
chk "RETRY: the second run" "MENUTEST arena run (step 4)"
chk "step 5: the second post-game" "MENUTEST arena debrief 2: arena screen postgame"
chk "NEW ASSIGNMENT: the start menu in Control" "MENUTEST arena new assignment: arena screen main"
chk "MAIN MENU from the post-game" "MENUTEST hub after debrief: page main"
chk "BEGIN THE JOB: a Heist run" "ge: uimenu: launch armed: mode 'heist_flowtest'"
chk "launched on the levelgen slot" "ge: uimenu: after the stage: launching again: mode 'heist_flowtest'"
chk "the heist run ended BUSTED" "flow finish: BUSTED"
chk "the debrief is armed for the hub" "ge: uimenu: after the stage: launching again: mode 'hub' preset '[a-z]*' lobby 'heist/postgame'"
chk "step 7: the hub is up on the debrief" "MENUTEST hub up: step 7, launched by ui lobby 'heist/postgame'"
chk "the debrief is the hosted heist's" "ge: hub: menu: heist: lobby up on this stage (postgame)"
chk "the heist debrief page in Control" "MENUTEST heist debrief: heist page debrief"
chk "MAIN MENU from the debrief" "MENUTEST hub 7: page main"
b=$(grep "MENUTEST settings before:" $L | sed 's/.*| //'); a=$(grep "MENUTEST settings changed:" $L | sed 's/.*| //'); r=$(grep "MENUTEST hub 7:" $L | sed 's/.*| //')
if [ -n "$b" ] && [ -n "$a" ] && [ "$b" != "$a" ]; then echo "PASS the settings changed ($b -> $a)"; pass=$((pass+1)); else echo "FAIL the settings changed ($b -> $a)"; fail=$((fail+1)); fi
if [ -n "$a" ] && [ "$a" = "$r" ]; then echo "PASS the changes held across the stage loads ($r)"; pass=$((pass+1)); else echo "FAIL held across the loads ('$a' vs '$r')"; fail=$((fail+1)); fi
chk "GOLDENEYE 007 to the Files screen" "ge: hub: menu: goldeneye: to the Files screen"
chk "the hand-off to the title menus" "ge: uimenu: after the stage: to the title menus"
chk "the Files screen came up" "ge: front: Files screen up"
# no stage load between the menu pages: the stage loads are the boot, the three runs and the three returns to the hub (7 + the Files screen's none)
n=$(grep -c "^ge: mode: '[a-z_-]*' on stage" $L)
if [ "$n" = 7 ]; then echo "PASS seven stage loads (boot, run, hub, retry, hub, heist run, hub): $n"; pass=$((pass+1)); else echo "FAIL stage loads: $n (want 7)"; fail=$((fail+1)); fi
n=$(grep -c "ge: hub: menu: stage start: CONTROL" $L)
if [ "$n" = 4 ]; then echo "PASS four hub stage loads: $n"; pass=$((pass+1)); else echo "FAIL hub stage loads: $n (want 4)"; fail=$((fail+1)); fi
# the music: every menu page of every hub load played the same track
n=$(grep "MENUTEST .*music " $L | grep -vc "music elevator_control")
if [ "$n" = 0 ]; then echo "PASS the hub's music on every menu page"; pass=$((pass+1)); else echo "FAIL music lines off the hub's track: $n"; grep "MENUTEST .*music " $L | grep -v "music elevator_control" | head -3; fail=$((fail+1)); fi
common
# the screenshots: BMP -> small JPG (disk is tight), the BMPs go
set -- $(grep "MENUTEST shot" $L | sed 's/.*shot //')   # (the names, in the order the script took them)
rm -f $OUT/jpg/*.jpg
for f in $(ls $OUT/shots/*.bmp 2>/dev/null | sort); do
  convert "$f" -resize 800x -quality 80 "$OUT/jpg/$1.jpg"
  shift
done
rm -f $OUT/shots/*.bmp
grep "MENUTEST shot" $L
echo "hub: $pass passed, $fail failed"

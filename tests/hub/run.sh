#!/bin/sh
# The main menu hub test inside the build container (Xvfb on :99, build_pc): see hub_drive.lua for the flow.
# usage: run.sh [frames] [exit|ini]   (log: build_tests/hub/drive.log | exit.log | ini.log, shots: build_tests/hub/jpg/*.jpg)
#   (default)  hub -> ARENA -> the arena lobby -> MAIN MENU -> hub -> HEIST -> the heist lobby -> MAIN MENU -> hub -> SETTINGS (a change
#              is still there after a stage load) -> ARENA -> MAIN MENU -> hub -> GOLDENEYE 007 -> the Files screen
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
         mode_arena:arena arena_flow:_arena_flow arena_menu_defs:_arena_menu_defs mode_arena_menu:arena-menu mode_arena_scores:_arena_scores \
         mode_arena_crowd:_arena_crowd arena_polish:_arena_polish arena_theme:_arena_theme arena_coop:_arena_coop \
         mode_heist:heist mode_heist_menu:heist-menu heist_theme:_heist_theme heist_menu_defs:_heist_menu_defs heist_flow:_heist_flow \
         heist_debrief:heist_debrief heist_planning:heist_planning heist_lib:heist_lib heist_bank_map:heist_bank_map; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
cp $P/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
cp $P/port/levelgen_casino/levelgen_layout.lua $M/levelgen_layout_casino.lua
cp $P/tests/hub/hub_drive.lua $B/Data/Scripts/zz_hub_drive.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=hub GE_STAGE=control GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-30000} GE_FULLHEIGHT=0 GE_EDGEINSET=1
if [ "${FAST:-1}" = 1 ]; then export GE_NOSLEEP=1 GE_CLOCKSCALE=20; fi
export GE_UIMENU_DIR=$OUT GE_NOMENUSAVE=1 GE_SCOREFILE=$OUT/scores.ini GE_NOSCORESAVE=1 GE_ARENAMENU_FILE=$OUT/arena_menu.ini
rm -f $OUT/scores.ini $OUT/arena_menu.ini $OUT/heist_bests.ini $OUT/heist_menu.ini
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
  chk "the hub is up" "MENUTEST hub up: launched with stage 'boot'"
  chk "EXIT" "ge: hub: menu: EXIT the game"
  chk "the game closed" "ge: ui: exit the game"
  chk "exit status 0" "^exit 0"
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
timeout ${TIMEOUT:-1500} $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $B/Data/Scripts/zz_hub_drive.lua
chk "the hub on the Control stage" "ge: hub: menu: stage start: CONTROL, camera on pad 101"
chk "the hub on first boot" "MENUTEST hub up: launched with stage 'boot' seq 0 hub flag false"
chk "ARENA hands off" "ge: hub: menu: arena: arena-menu on the colosseum stage"
chk "the arena lobby: launched on the colosseum, the hub flag set" "MENUTEST arena lobby up: seq 1, launched with stage 'colosseum', hub flag true"
chk "the arena lobby's exit row is MAIN MENU" "MENUTEST arena-menu lobby (seq 1): exit row 'MAIN MENU'"
chk "the arena lobby: BACK lands on MAIN MENU" "MENUTEST arena-menu lobby: cursor on 'MAIN MENU'"
chk "the arena lobby's MAIN MENU" "ge: arena: menu: MAIN MENU"
chk "back on the hub (seq 2), the flag clear" "MENUTEST hub up: launched with stage 'control' seq 2 hub flag false"
chk "HEIST hands off" "ge: hub: menu: heist: heist-menu on the levelgen stage"
chk "the heist lobby: launched on the levelgen slot, the bank, the hub flag set" "MENUTEST heist lobby up: seq 3, launched with stage 'levelgen', level bank, hub flag true"
chk "the heist lobby's exit row is MAIN MENU" "MENUTEST heist-menu lobby (seq 3): exit row 'MAIN MENU'"
chk "the heist lobby's MAIN MENU" "ge: heist: menu: MAIN MENU"
chk "back on the hub (seq 4)" "MENUTEST hub up: launched with stage 'control' seq 4 hub flag false"
chk "SETTINGS page" "ge: hub: menu: page settings"
chk "music volume applied" "ge: hub: menu: setting music_volume = .* (applied)"
chk "sound volume applied" "ge: hub: menu: setting sfx_volume = .* (applied)"
chk "mouse look X applied" "ge: hub: menu: setting mouse_sens_x = .* (applied)"
chk "the arena lobby again (seq 5), MAIN MENU" "MENUTEST arena-menu lobby (seq 5): exit row 'MAIN MENU'"
chk "back on the hub (seq 6)" "MENUTEST hub up: launched with stage 'control' seq 6 hub flag false"
b=$(grep "MENUTEST settings before:" $L | sed 's/.*| //'); a=$(grep "MENUTEST settings changed:" $L | sed 's/.*| //'); r=$(grep "MENUTEST hub 6:" $L | sed 's/.*| //')
if [ -n "$b" ] && [ -n "$a" ] && [ "$b" != "$a" ]; then echo "PASS the settings changed ($b -> $a)"; pass=$((pass+1)); else echo "FAIL the settings changed ($b -> $a)"; fail=$((fail+1)); fi
if [ -n "$a" ] && [ "$a" = "$r" ]; then echo "PASS the changes held across the stage loads ($r)"; pass=$((pass+1)); else echo "FAIL held across the loads ('$a' vs '$r')"; fail=$((fail+1)); fi
chk "GOLDENEYE 007 to the Files screen" "ge: hub: menu: goldeneye: to the Files screen"
chk "the hand-off to the title menus" "ge: uimenu: after the stage: to the title menus"
chk "the Files screen came up" "ge: front: Files screen up"
n=$(grep -c "ge: hub: menu: stage start: CONTROL" $L)
if [ "$n" = 4 ]; then echo "PASS four hub stage loads"; pass=$((pass+1)); else echo "FAIL hub stage loads: $n"; fail=$((fail+1)); fi
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

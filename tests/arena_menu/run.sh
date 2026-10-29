#!/bin/sh
# The arena start menu / post-game test inside the build container (Xvfb on :99, build_pc): see amenu_drive.lua for the flow.
# usage: run.sh [game frames] [bat|coop|map]   (log: build_tests/amenu/drive.log | bat.log | coop.log | map.log, shots: build_tests/amenu/shots/*.bmp)
# Checks at the end: two runs started with identical settings, a win and a fall reached the post-game, the menu came back with the
# run's settings, and EXIT closed the game.
P=/home/dev/project
OUT=$P/build_tests/amenu
mkdir -p $OUT/shots
rm -f $OUT/shots/*.bmp
for f in mode_arena:arena arena_flow:_arena_flow arena_menu_defs:_arena_menu_defs arena_maps:_arena_maps mode_arena_menu:arena-menu mode_arena_scores:_arena_scores \
         mode_arena_crowd:_arena_crowd arena_polish:_arena_polish arena_theme:_arena_theme arena_coop:_arena_coop arena_plus:_arena_plus ui_menu:_ui_menu; do
  cp "$P/tools/${f%%:*}.lua" "$P/build_pc/Data/Scripts/modes/${f##*:}.lua"
done
cp $P/tests/arena_menu/amenu_drive.lua $P/build_pc/Data/Scripts/zz_amenu_drive.lua
rm -f $P/build_pc/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_MODE=arena-menu GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_GFXFRAMES=${1:-6000} GE_FRAMES=$(( ${1:-6000} * 20 )) GE_FULLHEIGHT=0 GE_EDGEINSET=1
export GE_SCOREFILE=$OUT/scores.ini GE_NOSCORESAVE=1 GE_ARENAMENU_FILE=$OUT/arena_menu.ini GE_NOMENUSAVE=1
export GE_ARENA_MENU=1 GE_ARENA_TESTKEYS=1
rm -f $OUT/scores.ini $OUT/arena_menu.ini
cd $OUT/shots
pass=0; fail=0
chk() { if grep -q "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
if [ "$2" = bat ]; then   # run.sh <frames> bat: a launcher's preset (arena-chaos) ends on the debrief, Retry runs it again
  export GE_AMENU_BATTEST=1 GE_MODE=arena-chaos
  L=$OUT/bat.log
  $P/build_pc/ProjectMidas > $L 2>&1
  echo "exit $?" >> $L
  rm -f $P/build_pc/Data/Scripts/zz_amenu_drive.lua
  chk "launcher run: flow on, preset chaos" "flow on: mode arena-chaos preset chaos"
  chk "debrief after the launcher run" "MENUTEST bat postgame 1: screen postgame"
  chk "retry the preset" "RETRY SPECTRE PROTOCOL (mode arena-chaos"
  chk "second run" "MENUTEST bat run 2"
  chk "debrief 2" "MENUTEST bat postgame 2: screen postgame"
  chk "exit" "ge: arenamenu: exit the game"
  if grep -q "end screen: Mission Failed" $L; then echo "FAIL the stock report showed"; fail=$((fail+1)); else echo "PASS no stock report"; pass=$((pass+1)); fi
  echo "amenu bat: $pass passed, $fail failed"
  exit 0
fi
if [ "$2" = coop ]; then   # run.sh <frames> coop: the co-op variant
  export GE_AMENU_COOPTEST=1
  L=$OUT/coop.log
  $P/build_pc/ProjectMidas > $L 2>&1
  echo "exit $?" >> $L
  rm -f $P/build_pc/Data/Scripts/zz_amenu_drive.lua
  chk() { if grep -q "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
  chk "co-op picked" "MENUTEST coop pick: screen main cursor 12 preset coop"   # (BEGIN MISSION: item 12 now the ARENA row is in the list)
  chk "two Bonds" "MENUTEST coop run: 2 Bonds"
  chk "co-op rules on" "ge: arena: co-op ON (2 Bonds)"
  chk "the menu's bot skill in the run's settings" "flow settings:.*coop_bot_skill=4"
  chk "the menu's bot skill reached the bot" "ge: arena: coop bot skill 4: reaction 0.18s"
  chk "debrief" "MENUTEST coop postgame: screen postgame"
  chk "exit" "ge: arenamenu: exit the game"
  echo "amenu coop: $pass passed, $fail failed"
  exit 0
fi
L=$OUT/drive.log
SFX=""   # the label's map name: run.sh <frames> map sets the ARENA row to THE TEMPLE (a run on a stock multiplayer map, its post-game on the Colosseum, Retry on the map)
if [ "$2" = map ]; then export GE_AMENU_MAP=1; L=$OUT/map.log; SFX=" - THE TEMPLE"; fi
$P/build_pc/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $P/build_pc/Data/Scripts/zz_amenu_drive.lua
chk() { if grep -q "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
if [ "$2" = map ]; then
  chk "the settings carry the map" "ge: arena: flow settings:.*map=temple"
  chk "the run is launched on the Temple's own setup" "ge: arenamenu: after the stage: launching the mptemple again: mode 'arena'"
  chk "the Temple's path graph was built" "ge: mpnav: [0-9]* pieces; the largest: [0-9]* waypoints"
  chk "the arena ran on the map" "ge: arena: MAP THE TEMPLE (mptemple)"
  chk "the post-game is on the Colosseum" "ge: arenamenu: after the stage: launching the colosseum again: mode 'arena-menu'"
  chk "Retry loads the map again" "ge: arenamenu: after the stage: launching the mptemple again: mode 'arena'.*"
  n=$(grep -c "ge: arenamenu: after the stage: launching the mptemple again" $L)
  if [ "$n" = 2 ]; then echo "PASS the map was loaded twice (the run and Retry)"; pass=$((pass+1)); else echo "FAIL map loads: $n (want 2)"; fail=$((fail+1)); fi
fi
chk "menu page advanced" "ge: arena: menu: page advanced"
chk "menu START" "ge: arena: menu: START UNSANCTIONED OPERATION - DOUBLE-O$SFX"
chk "run 1 won" "ge: arena: flow finish: WON"
chk "post-game 1" "MENUTEST postgame 1: screen postgame"
chk "retry" "ge: arena: menu: RETRY"
chk "run 2 fell" "ge: arena: flow finish: FELL"
chk "post-game 2" "MENUTEST postgame 2: screen postgame"
chk "change settings" "MENUTEST after: screen main"
chk "exit the game" "ge: arenamenu: exit the game"
chk "the menu has the mouse in the lobby" "MENUTEST boot mouse: menu_up true"
chk "run 1: the menu let go of the mouse, gameplay owns it" "MENUTEST run 1 mouse: menu_up false gameplay_pointer true captured true"
chk "run 2 (Retry): likewise" "MENUTEST run 2 mouse: menu_up false gameplay_pointer true captured true"
n=$(grep -c "ge: arena: flow settings:" $L)
u=$(grep "ge: arena: flow settings:" $L | sort -u | wc -l)
if [ "$n" = 2 ] && [ "$u" = 1 ]; then echo "PASS two runs, same settings"; pass=$((pass+1)); else echo "FAIL runs $n, distinct settings $u"; fail=$((fail+1)); fi
if grep "flow settings:" $L | grep -q "mutators=true" && grep "flow settings:" $L | grep -q "waves=10" && grep "flow settings:" $L | grep -q "whacky=1"; then
  echo "PASS the menu's choices reached the run"; pass=$((pass+1)); else echo "FAIL the menu's choices"; fail=$((fail+1)); fi
a=$(grep "MENUTEST advanced:" $L | sed 's/.* rows #\([0-9]*\) |.*/\1/'); b=$(grep "MENUTEST after:" $L | sed 's/.* rows #\([0-9]*\) |.*/\1/')   # (a checksum of every row: the log line is cut)
if [ -n "$a" ] && [ "$a" = "$b" ]; then echo "PASS the menu came back with the run's settings"; pass=$((pass+1)); else echo "FAIL menu settings after"; fail=$((fail+1)); fi
if grep -q "mission report\|end screen: Mission Failed" $L; then echo "FAIL the stock report showed"; fail=$((fail+1)); else echo "PASS no stock report"; pass=$((pass+1)); fi
echo "amenu: $pass passed, $fail failed"

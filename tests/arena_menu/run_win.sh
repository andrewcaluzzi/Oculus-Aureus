#!/bin/sh
# The arena start menu / post-game test on Windows in hidden mode (GE_HIDDEN=1: no window is ever shown), from Git Bash:
#   sh tests/arena_menu/run_win.sh <build dir, e.g. E:/midas-wt/v02/build_win_test> [game frames]
# The same flow and checks as run.sh (amenu_drive.lua), plus the mouse-capture lines of every run. A hidden window never has the
# input focus, so "captured" reads false there by design (input_pc.c: capture needs focus; portMouseApplyCapture never grabs in
# hidden mode); what this checks is that the capture state of run 1 and run 2 (Retry) is the same and that no focus loss
# releases it in between.
R=$(cd "$(dirname "$0")/../.." && pwd -W)
B=$1
OUT=$B/amenu
mkdir -p $OUT
for f in mode_arena:arena arena_flow:_arena_flow arena_menu_defs:_arena_menu_defs mode_arena_menu:arena-menu mode_arena_scores:_arena_scores \
         mode_arena_crowd:_arena_crowd arena_polish:_arena_polish arena_theme:_arena_theme arena_coop:_arena_coop; do
  cp "$R/tools/${f%%:*}.lua" "$B/Data/Scripts/modes/${f##*:}.lua"
done
cp $R/tests/arena_menu/amenu_drive.lua $B/Data/Scripts/zz_amenu_drive.lua
rm -f $B/Data/Saves/eeprom.bin
export GE_HIDDEN=1 GE_MODE=arena-menu GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_GFXFRAMES=${2:-6000} GE_FRAMES=$(( ${2:-6000} * 20 )) GE_NODIRECTLOG=1
export GE_SCOREFILE=$OUT/scores.ini GE_NOSCORESAVE=1 GE_ARENAMENU_FILE=$OUT/arena_menu.ini GE_NOMENUSAVE=1
export GE_ARENA_MENU=1 GE_ARENA_TESTKEYS=1
rm -f $OUT/scores.ini $OUT/arena_menu.ini
L=$OUT/drive.log
mkdir -p $OUT/shots
rm -f $OUT/shots/*.bmp
cd $OUT/shots || exit 1   # test.screenshot() writes into the cwd
"$B/ProjectMidas.exe" > $L 2>&1
echo "exit $?" >> $L
rm -f $B/Data/Scripts/zz_amenu_drive.lua
pass=0; fail=0
chk() { if grep -q "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
chk "hidden mode" "ge: hidden: window never shown"
chk "menu START" "ge: arena: menu: START UNSANCTIONED OPERATION - DOUBLE-O"
chk "run 1 won" "ge: arena: flow finish: WON"
chk "retry" "ge: arena: menu: RETRY"
chk "run 2 fell" "ge: arena: flow finish: FELL"
chk "change settings" "MENUTEST after: screen main"
chk "quit to title" "after the stage: to the title menus"
chk "run 1: the menu let go, gameplay owns the pointer" "MENUTEST run 1 mouse: menu_up false gameplay_pointer true"
chk "run 2 (Retry): likewise" "MENUTEST run 2 mouse: menu_up false gameplay_pointer true"
c1=$(grep "MENUTEST run 1 mouse:" $L | sed 's/.*captured //'); c2=$(grep "MENUTEST run 2 mouse:" $L | sed 's/.*captured //')
if [ -n "$c1" ] && [ "$c1" = "$c2" ]; then echo "PASS capture the same in run 1 and run 2 (captured $c1)"; pass=$((pass+1)); else echo "FAIL capture run 1 '$c1' run 2 '$c2'"; fail=$((fail+1)); fi
if grep -q "mouse released (window lost focus)" $L; then echo "FAIL a focus loss released the mouse"; fail=$((fail+1)); else echo "PASS no focus-loss release"; pass=$((pass+1)); fi
echo "mouse lines:"; grep "ge: input: mouse" $L | head -20
n=$(grep -c "ge: arena: flow settings:" $L)
u=$(grep "ge: arena: flow settings:" $L | sort -u | wc -l)
if [ "$n" = 2 ] && [ "$u" = 1 ]; then echo "PASS two runs, same settings"; pass=$((pass+1)); else echo "FAIL runs $n, distinct settings $u"; fail=$((fail+1)); fi
echo "amenu win: $pass passed, $fail failed"

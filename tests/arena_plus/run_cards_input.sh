#!/bin/sh
# The Requisition cards under REAL input: xdotool into the game window (Xvfb on :99, build_pc, real time: no fast-forward), with the
# mouse button held down when the last enemy dies (AC's playtest, wave 9: "the Requisition screen was unclickable"). See cards-input.lua.
# Cases (one per cleared wave):
#   1 left button HELD when the cards open, released later, then a CLICK on a card
#   2 left button held when they open, then the KEYBOARD (S, Enter) with the button still down
#   3 a plain click (no button held)
#   4 plain keyboard (W, Return)
#   5 left button held, released, W/S + the pad's SELECT (a board event, as a pad's A makes)
# usage: run_cards_input.sh [seconds per case, default 90]
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/cardsin
M=$B/Data/Scripts/modes
mkdir -p $OUT/shots
rm -f $OUT/shots/*
for f in mode_arena:arena arena_flow:_arena_flow arena_menu_defs:_arena_menu_defs arena_maps:_arena_maps mode_arena_scores:_arena_scores mode_arena_crowd:_arena_crowd \
         mode_arena_mutators:_arena_mutators mode_arena_elites:_arena_elites arena_polish:_arena_polish arena_theme:_arena_theme \
         arena_coop:_arena_coop arena_plus:_arena_plus ui_menu:_ui_menu; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
cp $P/tests/arena_plus/cards-input.lua $M/cards-input.lua
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99
L=$OUT/cards.log
rm -f $L
cd $OUT/shots
env GE_MODE=cards-input GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_CHEATS=invuln GE_ARENA_CARDS=ui GE_UIMENU_DIR=$OUT GE_NOMENUSAVE=1 GE_NOSCORESAVE=1 GE_NOAUDIO=1 \
    $B/ProjectMidas > $L 2>&1 &
pid=$!
T=${1:-90}
waitfor() {   # waitfor "<text>" -> 0 when it shows up in the log within T seconds
  n=0
  while ! grep -aq "$1" $L 2>/dev/null; do
    sleep 1; n=$((n + 1))
    [ $n -gt $T ] && return 1
    kill -0 $pid 2>/dev/null || return 1
  done
  return 0
}
waitfor "CARDSTEST arm 1" || { echo "FAIL the run never started"; kill $pid; exit 1; }
T=40   # (the first wave takes the start-up; the rest are quick)
sleep 2
w=$(xdotool search --name "Project Midas" | head -1)
xdotool getwindowgeometry $w > $OUT/geometry.txt
eval $(xdotool getwindowgeometry --shell $w)
echo "window $w ${WIDTH}x${HEIGHT}"
xdotool windowfocus --sync $w 2>/dev/null; xdotool mousemove 640 360
CX=$((WIDTH / 2)); CY=$((HEIGHT / 2))
# card rows: the lobby layout puts the list in the left column; ROW1_Y .. ROW3_Y as fractions of the window height (see cards.jpg)
# (measured on cards.jpg at 1280x720: the three cards are centred at x 400, y 203 / 278 / 353)
XC=$((WIDTH * 400 / 1280))
y1=$((HEIGHT * 203 / 720)); y2=$((HEIGHT * 278 / 720)); y3=$((HEIGHT * 353 / 720))
[ -n "$CARD_X" ] && XC=$CARD_X
[ -n "$CARD_Y1" ] && y1=$CARD_Y1 && y2=$CARD_Y2 && y3=$CARD_Y3
click_card() {   # a real click: move (so the menu sees the pointer move), then press
  xdotool windowfocus --sync $w 2>/dev/null
  xdotool mousemove $XC $((${1:-$y2} - 6)); sleep 0.2
  xdotool mousemove $XC ${1:-$y2}; sleep 0.3
  xdotool mousedown 1; sleep 0.15; xdotool mouseup 1
}
press() { xdotool keydown $1; sleep 0.15; xdotool keyup $1; }   # (a real key is down for a while: a press and release between two polls would be missed)
pass=0; fail=0
result() {   # result <k> <name>
  if waitfor "CARDSTEST taken $1 " && ! grep -aq "CARDSTEST STUCK $1 " $L; then echo "PASS case $1: $2"; pass=$((pass + 1))
  else echo "FAIL case $1: $2 (the cards stayed up)"; fail=$((fail + 1)); fi
}
# (a case that sticks is given up on after 60 s of game time: cards-input.lua takes a card itself and the next case goes on)
# case 1: a plain click
waitfor "CARDSTEST open 1" || { echo "FAIL case 1 never opened"; kill $pid; exit 1; }
sleep 2
click_card $y1; result 1 "plain click"
# case 2: button held across the kill, released after the screen is up, then a click
waitfor "CARDSTEST arm 2"; xdotool mousedown 1
waitfor "CARDSTEST open 2"; sleep 2; xdotool mouseup 1; sleep 0.5
click_card; result 2 "mouse held at the kill, released, click"
# case 3: held again, keyboard with the button still down
waitfor "CARDSTEST arm 3"; xdotool mousedown 1
waitfor "CARDSTEST open 3"; sleep 2
press s; sleep 0.3; press Return
result 3 "mouse held, keyboard S + Enter"
xdotool mouseup 1
# case 4: plain keyboard
waitfor "CARDSTEST arm 4"
waitfor "CARDSTEST open 4"; sleep 2
press w; sleep 0.3; press Return; result 4 "plain keyboard W + Return"
# case 5: held, released, then click without moving first (the pointer sat still since the last case)
waitfor "CARDSTEST arm 5"; xdotool mousedown 1
waitfor "CARDSTEST open 5"; sleep 2; xdotool mouseup 1; sleep 0.5
xdotool mousedown 1; sleep 0.15; xdotool mouseup 1; sleep 1
click_card $y3; result 5 "held, released, a click without moving, then a moved click"
kill $pid 2>/dev/null
rm -f $M/cards-input.lua
grep -a "CARDSTEST" $L
echo "cards input: $pass passed, $fail failed"

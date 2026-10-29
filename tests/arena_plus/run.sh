#!/bin/sh
# Arena plus (batch A) test inside the build container (Xvfb on :99, build_pc): credits, power-ups, the Q-Branch crate, the Requisition
# cards. Drives tests/arena_plus/plus-test.lua (GE_MODE=plus-test, invulnerable, fast-forward). Log: build_tests/plus/plus.log.
# usage: run.sh [game frames]
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/plus
M=$B/Data/Scripts/modes
mkdir -p $OUT/shots
rm -f $OUT/shots/*
# the arena scripts as they are in tools/ (the build copies them only when the exe relinks)
for f in mode_arena:arena arena_flow:_arena_flow arena_menu_defs:_arena_menu_defs arena_maps:_arena_maps mode_arena_scores:_arena_scores mode_arena_crowd:_arena_crowd \
         mode_arena_mutators:_arena_mutators mode_arena_elites:_arena_elites arena_polish:_arena_polish arena_theme:_arena_theme \
         arena_coop:_arena_coop arena_plus:_arena_plus ui_menu:_ui_menu; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
TEST=${TEST:-plus-test}   # TEST=plus-shots: the screenshot scenario (JPGs are made by the caller)
cp $P/tests/arena_plus/$TEST.lua $M/$TEST.lua
rm -f $B/Data/Saves/eeprom.bin   # a save from an earlier run sends the boot to the file select instead of GE_STAGE
export DISPLAY=:99 GE_MODE=$TEST GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1
export GE_FRAMES=${1:-40000} GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_CHEATS=invuln
export GE_ARENA_CARDS=ui GE_UIMENU_DIR=$OUT GE_NOMENUSAVE=1 GE_SCOREFILE=$OUT/scores.ini GE_NOSCORESAVE=1
if [ "${FAST:-1}" = 1 ]; then export GE_NOSLEEP=1 GE_CLOCKSCALE=20; fi
if [ "$TEST" = plus-soak ]; then unset GE_ARENA_CARDS; fi   # (the soak takes its cards at once: nobody presses a key)
for kv in $2; do export "$kv"; done   # extra env, e.g. "GE_SHOT=100"
L=$OUT/plus.log
cd $OUT/shots
timeout ${TIMEOUT:-900} $B/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $M/$TEST.lua
if [ "$TEST" != plus-test ]; then echo "$TEST: exit, log $L, shots in $OUT/shots"; exit 0; fi
pass=0; fail=0
chk() { if grep -q "$2" $L; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
chk "the module loaded and started" "ge: arena: PLUS ON: credits true, power-ups true, crate true, cards true"
chk "Q is in the pit" "ge: arena: PLUS Q-Branch crate: Q is chr"
chk "a run finished (won)" "ge: arena: WON wave 3"
chk "exit status 0" "^exit 0"
if grep -q "ge: lua: .*error\|A7 error\|PLUS error" $L; then echo "FAIL a Lua error"; grep "ge: lua: .*error\|A7 error" $L | head -5; fail=$((fail+1)); else echo "PASS no Lua errors"; pass=$((pass+1)); fi
tp=$(grep -ac "PLUSTEST PASS" $L); tf=$(grep -ac "PLUSTEST FAIL" $L)
grep -a "PLUSTEST FAIL" $L | head -20
echo "scripted checks: $tp passed, $tf failed"
if [ "$tf" != 0 ] || [ "$tp" = 0 ]; then fail=$((fail+1)); else pass=$((pass+1)); fi
grep -a "PLUSTEST DONE" $L
echo "arena plus: $pass passed, $fail failed"

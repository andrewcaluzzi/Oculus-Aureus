#!/bin/sh
# The generic menu layer against a hostile mode script (tests/ui_security/ui_hostile.lua), inside the build container (Xvfb on :99,
# build_pc built). Checks the script's own UISEC lines (prefs names / keys / values refused, launches with a bad mode refused, only
# the allowlisted variable reaching the next stage) and, from outside, the files: every preference file landed in Data/Saves/ui, no
# file named evil anywhere, midas.ini and arena_menu.ini untouched, nothing written to /tmp/pwn.
# usage: run.sh   -> "PASS|FAIL ..." lines, last "ui_security: <n> passed, <m> failed"
P=/home/dev/project
B=$P/build_pc
OUT=$P/build_tests/ui_security
mkdir -p $OUT
cp $P/tests/ui_security/ui_hostile.lua $B/Data/Scripts/modes/ui_hostile.lua
cp $P/tools/ui_menu.lua $B/Data/Scripts/modes/_ui_menu.lua
rm -rf $B/Data/Saves/ui /tmp/pwn /tmp/pwn.lua
touch $B/midas.ini
a1=$(md5sum < $B/midas.ini)
a2=$( [ -f $B/Data/Saves/arena_menu.ini ] && md5sum < $B/Data/Saves/arena_menu.ini || echo none)
L=$OUT/run.log
env -u GE_UIMENU_DIR -u GE_NOMENUSAVE DISPLAY=:99 GE_MODE=ui_hostile GE_STAGE=dam GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FIXEDTICK=1 \
    GE_TICKHZ=60 GE_FRAMES=${GE_FRAMES:-2400} timeout ${TIMEOUT:-300} $B/ProjectMidas > $L 2>&1
pass=$(grep -c "ge: UISEC PASS" $L)
fail=$(grep -c "ge: UISEC FAIL" $L)
grep "ge: UISEC" $L
grep "ge: uimenu: refused" $L
ok() { if [ "$2" = 1 ]; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
ok "both stages ran" $(grep -q "UISEC DONE" $L && echo 1)
ok "the prefs landed in Data/Saves/ui (midas, arena_menu, hostile)" $([ -f $B/Data/Saves/ui/midas.ini ] && [ -f $B/Data/Saves/ui/arena_menu.ini ] && [ -f $B/Data/Saves/ui/hostile.ini ] && echo 1)
ok "no line of hostile.ini was injected" $(grep -q "evil" $B/Data/Saves/ui/hostile.ini || echo 1)
ok "no evil file anywhere" $([ -z "$(find $B /tmp -name 'evil*' 2>/dev/null)" ] && echo 1)
ok "midas.ini untouched" $([ "$(md5sum < $B/midas.ini)" = "$a1" ] && echo 1)
ok "arena_menu.ini untouched" $([ "$( [ -f $B/Data/Saves/arena_menu.ini ] && md5sum < $B/Data/Saves/arena_menu.ini || echo none)" = "$a2" ] && echo 1)
ok "nothing written to /tmp/pwn" $([ ! -e /tmp/pwn ] && echo 1)
ok "the variables were refused in C" $(grep -q "refused the variable GE_UIMENU_DIR" $L && grep -q "refused the variable GE_LUA_TEST" $L && echo 1)
ok "the next stage started as ui_hostile on the allowlisted variable" $(grep -q "launching again: mode 'ui_hostile'.* GE_LEVELGEN=bank" $L && echo 1)
ok "no Lua errors" $(grep -q "ge: lua: .*error" $L || echo 1)
rm -rf $B/Data/Saves/ui
echo "ui_security: $pass passed, $fail failed"

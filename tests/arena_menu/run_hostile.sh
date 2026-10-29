#!/bin/sh
# The arena menu against a hostile mode script (amenu_hostile.lua), inside the build container (Xvfb :99, build_pc).
# PASS lines from the script, then the preferences file it wrote (GE_ARENAMENU_FILE, read once at process start) must hold the good
# value and no injected line.
P=/home/dev/project
OUT=$P/build_tests/amenu_hostile
mkdir -p $OUT
F=$OUT/arena_menu.ini
rm -f $F $P/build_pc/Data/Saves/eeprom.bin
cp $P/tests/arena_menu/amenu_hostile.lua $P/build_pc/Data/Scripts/modes/amenu-hostile.lua
cp $P/tools/arena_menu_defs.lua $P/build_pc/Data/Scripts/modes/_arena_menu_defs.lua
cp $P/tools/arena_theme.lua $P/build_pc/Data/Scripts/modes/_arena_theme.lua
L=$OUT/run.log
env DISPLAY=:99 GE_MODE=amenu-hostile GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 GE_NOMSGBOX=1 \
  GE_FRAMES=600 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_ARENAMENU_FILE=$F timeout -s KILL 300 $P/build_pc/ProjectMidas > $L 2>&1
echo "exit $?" >> $L
rm -f $P/build_pc/Data/Scripts/modes/amenu-hostile.lua
pass=$(grep -c "AMHOSTILE PASS" $L); fail=$(grep -c "AMHOSTILE FAIL" $L)
grep "AMHOSTILE FAIL" $L
chk() { if eval "$2"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
chk "the script ran to the end" "grep -q 'AMHOSTILE done' $L"
chk "the file was written where GE_ARENAMENU_FILE said" "test -f $F"
chk "the good value is in the file" "grep -q '^good=SPRINT 10\$' $F"
chk "no injected line in the file" "! grep -q 'GE_MODE\|evil\|^x\$' $F"
chk "exactly the header, a comment and good lines" "test \$(grep -c . $F) -eq 3"
chk "the refusals were logged" "grep -q 'refused a preference' $L"
chk "the bad modes were logged" "test \$(grep -c 'launch refused: mode name' $L) -ge 4"
chk "the parser refusals were logged" "grep -q 'refused a table text that is not plain data' $L"
chk "no Lua error" "! grep -qiE 'lua: .*error|traceback' $L"
echo "amenu hostile: $pass passed, $fail failed"

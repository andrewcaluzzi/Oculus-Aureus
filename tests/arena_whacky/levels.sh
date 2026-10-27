#!/bin/sh
# Whacky menu-level test inside the build container (Xvfb on :99, build_pc): whacky mutators roll at each menu level and never when it is off.
# usage: levels.sh [frames]   Logs build_tests/whacky/levels-<W>-<M>.log; prints PASS/FAIL lines and a summary.
P=/home/dev/project
F=${1:-9000}
mkdir -p $P/build_tests/whacky
pass=0; fail=0
chk() { if [ "$2" = ok ]; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
WH="ghost chameleon shadow glass giants fastfwd slowmo rockets double glitch swap crowd minions mayhem"
for wm in "1 2" "2 1" "3 1" "4 1" "2 2" "4 2"; do
  set -- $wm; W=$1; M=$2
  sed "s/@W@/$W/; s/@M@/$M/" $P/tests/arena_whacky/whacky-levels.lua.in > $P/build_pc/Data/Scripts/modes/ovn-whacky-$W-$M.lua
  rm -f $P/build_pc/Data/Saves/eeprom.bin
  L=$P/build_tests/whacky/levels-$W-$M.log
  ( cd $P/build_pc; DISPLAY=:99 GE_MODE=ovn-whacky-$W-$M GE_STAGE=colosseum GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 \
    GE_NOMSGBOX=1 GE_FRAMES=$F GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_SCOREFILE=$P/build_tests/whacky/scores.ini GE_NOSCORESAVE=1 GE_CHEATS=invuln \
    ./ProjectMidas > $L 2>&1; echo "exit $?" >> $L )
  rm -f $P/build_pc/Data/Scripts/modes/ovn-whacky-$W-$M.lua
  grep -q "^exit 0" $L && e=ok || e=bad; chk "w$W m$M: exit 0" $e
  waves=$(grep -c "arena: wave .* start\|WAVE [0-9]* " $L)
  rolled=$(grep "arena: MUTATOR wave" $L | sed 's/.*(\([a-z_0-9]*\)).*/\1/')
  nroll=$(echo "$rolled" | grep -c .)
  nwh=0; for n in $rolled; do case " $WH " in *" $n "*) nwh=$((nwh+1));; esac; done
  echo "  w$W m$M: rolls=$nroll whacky-rolls=$nwh [$(echo $rolled | tr '\n' ' ')]"
  if [ "$W" = 1 ]; then
    [ "$nwh" = 0 ] && e=ok || e=bad; chk "w$W m$M: no whacky mutator when off" $e
    grep -q "ge: whacky: menu level" $L && e=bad || e=ok; chk "w$W m$M: no menu-level line when off" $e
  else
    grep -q "ge: whacky: menu level $((W-1)):" $L && e=ok || e=bad; chk "w$W m$M: menu level $((W-1)) line" $e
    [ "$nwh" -gt 0 ] && e=ok || e=bad; chk "w$W m$M: at least one whacky mutator rolled" $e
  fi
  grep "MUTATOR .* off" $L | grep -q FAILED && e=bad || e=ok; chk "w$W m$M: mutator resets ok" $e
done
echo "whacky levels: $pass passed, $fail failed"

#!/bin/sh
# The arena test keys with REAL key events (xdotool into the game window), with Play Arena (Test Keys).bat's own environment
# (GE_STAGE=colosseum GE_SKIPINTRO=1 GE_MODE=arena GE_ARENA_TESTKEYS=1 GE_CHEATS=invuln,guns) and the menu flow on as a launcher run
# has it (GE_ARENA_MENU=1: a GE_FRAMES run turns it off otherwise). Inside the build container: Xvfb :99, xdotool.
# usage: realkeys.sh [mode, default arena] [log tag]
# PageDown -> "TEST next", Home -> "TEST callout preview", End -> "TEST fog on", Insert -> "TEST rain on"; mode arena-whacky: F5 too.
P=/home/dev/project
M=${1:-arena}
T=${2:-$M}
O=$P/build_tests/realkeys
mkdir -p $O
L=$O/$T.log
rm -f $L $P/build_pc/Data/Saves/eeprom.bin
cd $O || exit 1
# modes: arena / arena-whacky (the Test Keys bats), arena-coop (GE_COOP=1: local co-op, where the keys used to be off), off (GE_MODE=arena
# WITHOUT GE_ARENA_TESTKEYS: a key must say the test keys are off)
TK="GE_ARENA_TESTKEYS=1"
GM=$M
EXTRA=
case "$M" in
  off) TK="GE_ARENA_TESTKEYS=0"; GM=arena ;;
  *coop*) EXTRA="GE_COOP=1" ;;
esac
if [ "$3" = exact ]; then   # ONLY the bat's variables (no GE_FRAMES: the run is killed at the end); the build's midas.ini is used
  env -i HOME=$HOME PATH=$PATH DISPLAY=:99 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_MODE=$GM $TK GE_CHEATS=invuln,guns $EXTRA \
    $P/build_pc/ProjectMidas > $L 2>&1 &
else
  env DISPLAY=:99 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_MODE=$GM $TK GE_CHEATS=invuln,guns GE_ARENA_MENU=1 GE_NOINISAVE=1 $EXTRA \
    GE_NOSCORESAVE=1 GE_NOMENUSAVE=1 GE_NOMSGBOX=1 GE_FRAMES=7000 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_KEYLOG=1 \
    $P/build_pc/ProjectMidas > $L 2>&1 &
fi
pid=$!
t=0
while ! grep -aq "ge: arena: wave 1 " $L 2>/dev/null; do sleep 1; t=$((t + 1)); [ $t -gt 240 ] && break; done
sleep 3
export DISPLAY=:99
w=$(xdotool search --name "Project Midas" | head -1)
echo "game window $w after ${t}s" > $O/$T.out
key() { # name keysym expected-log-line (a NEW one must appear after the press)
  n0=$(grep -ac "$3" $L)
  xdotool windowfocus --sync $w 2>/dev/null
  xdotool key --window $w $2
  sleep 2
  n1=$(grep -ac "$3" $L)
  if [ "$n1" -gt "$n0" ]; then echo "PASS $1 ($2)" >> $O/$T.out; else echo "FAIL $1 ($2): no new '$3'" >> $O/$T.out; fi
}
if [ "$M" = off ]; then
  key PageDown-off Next "the test keys are off"
else
  key PageDown Next "TEST next"
  key Home Home "TEST callout preview"
  key End End "TEST fog on"
  key Insert Insert "TEST rain on"
  if [ "$M" = arena-whacky ]; then key F5 F5 "MUTATOR wave"; fi
fi
kill $pid 2>/dev/null
wait $pid 2>/dev/null
cat $O/$T.out
grep -a "ge: input: key\|keylog\|test_key" $L | head -10

#!/bin/sh
# The RAIN mutator's sim is deterministic: rain-net.lua as local co-op (2 Bonds: the player and the bot partner), twice with the same
# seed and fixed tick, must log the same per-tick state hash (GE_HASHLOG). Inside the build container, Xvfb :99. usage: rain-hash.sh [frames]
P=/home/dev/project
B=$P/build_pc
O=$P/build_tests/rainhash
mkdir -p $O
cp $P/tests/arena_a8/rain-net.lua $B/Data/Scripts/modes/
cd $O || exit 1
export DISPLAY=:99 GE_COOP=1 GE_MODE=rain-net GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 \
  GE_FRAMES=${1:-4000} GE_NOINISAVE=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_NOSCORESAVE=1 GE_NOMSGBOX=1
for r in a b; do
  rm -f $B/Data/Saves/eeprom.bin
  GE_HASHLOG=$O/$r.txt $B/ProjectMidas > $O/$r.log 2>&1
done
echo "rain lines: $(grep -ac 'arena: rain on' $O/a.log) on, $(grep -ac 'RAIN slip:' $O/a.log) slips (b: $(grep -ac 'RAIN slip:' $O/b.log))"
na=$(wc -l < $O/a.txt); nb=$(wc -l < $O/b.txt); n=$na; [ $nb -lt $n ] && n=$nb
head -n $n $O/a.txt > $O/a.prefix; head -n $n $O/b.txt > $O/b.prefix
if cmp -s $O/a.prefix $O/b.prefix; then echo "rain hash: IDENTICAL over $n lines (a $na, b $nb)"; else echo "rain hash: DIFFER"; diff $O/a.prefix $O/b.prefix | head -4; fi

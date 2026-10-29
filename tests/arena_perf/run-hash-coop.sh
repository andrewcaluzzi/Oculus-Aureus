#!/bin/sh
# The render fixes in co-op: an arena co-op run (2 Bonds: the player and the bot partner; real waves, so chrs, AI, dropped guns and
# corpses fill the hash's chrs / chrai / objs fields) twice with a fixed tick and seed, once with the defaults (crowd_lod,
# chr_batch, corpse_bake on) and once with all three forced off. The per-tick state hash must be the same.
# Inside the goldeneye image with Xvfb :99 running. Usage: run-hash-coop.sh [frames] [mode, default arena-coop]
B=/home/dev/project/build_pc
O=$B/hash-coop
F=${1:-5000}
M=${2:-arena-coop}
mkdir -p $O
cd $O || exit 1
rm -f $B/Data/Saves/eeprom.bin
export DISPLAY=:99 GE_COOP=1 GE_MODE=$M GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln GE_SEED=7 GE_FIXEDTICK=1 \
  GE_TICKHZ=60 GE_FRAMES=$F GE_NOINISAVE=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_NOSCORESAVE=1 GE_NOMSGBOX=1
GE_HASHLOG=$O/$M-on.txt GE_BAKE_LOG=1 $B/ProjectMidas > $O/$M-on.log 2>&1
GE_CROWD_LOD=0 GE_CHR_BATCH=0 GE_CORPSE_BAKE=0 GE_HASHLOG=$O/$M-off.txt $B/ProjectMidas > $O/$M-off.log 2>&1
n1=$(grep -c "^tick" $O/$M-on.txt)
n2=$(grep -c "^tick" $O/$M-off.txt)
if cmp -s $O/$M-on.txt $O/$M-off.txt; then r=IDENTICAL; else r=DIFFERS; fi
echo "$M hash lines on $n1 off $n2: $r"
for fld in chrs chrai objs onscreen; do
  e=$(grep -c " $fld 811c9dc5" $O/$M-on.txt)
  d=$(grep "^tick" $O/$M-on.txt | sed "s/.* $fld \([0-9a-f]*\).*/\1/" | sort -u | wc -l)
  echo "  $fld: empty in $e ticks, $d distinct values"
done
echo "  kills: $(grep -c 'ge: arena: kill\|killed by' $O/$M-on.log) wave lines: $(grep -c 'WAVE' $O/$M-on.log) co-op: $(grep -m1 'co-op ON' $O/$M-on.log)"
for s in on off; do   # the fixes act: draws per frame from the "ge: stats" lines (max and the mean of the last 10)
  echo "  draws $s: $(grep -a 'ge: stats:' $O/$M-$s.log | sed 's/.*draws \([0-9]*\).*/\1/' | awk '{ a[NR] = $1; if ($1 > m) m = $1 } END { s = 0; k = 0; for (i = NR - 9; i <= NR; i++) if (i > 0) { s += a[i]; k++ } printf "max %d last-10 mean %.0f (%d lines)", m, k ? s / k : 0, NR }')"
done
echo "  bake refusals logged: $(grep -c 'ge: bake:' $O/$M-on.log)"

#!/bin/sh
# The render fixes change only what is drawn: the late-wave bench scene run twice with a fixed tick and seed, once with the
# defaults (crowd_lod, chr_batch, corpse_bake on) and once with all three forced off, must log the same per-tick state hash.
# Inside the goldeneye image with Xvfb :99 running. Usage: run-hash.sh [frames]
B=/home/dev/project/build_pc
O=$B/hash
F=${1:-2400}
mkdir -p $O
cd $O || exit 1
export DISPLAY=:99 GE_MODE=arena-bench GE_BENCH_LATE=1 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln GE_SEED=7 GE_FIXEDTICK=1 \
  GE_TICKHZ=60 GE_FRAMES=$F GE_NOINISAVE=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1
GE_HASHLOG=$O/on.txt $B/ProjectMidas > $O/on.log 2>&1
GE_CROWD_LOD=0 GE_CHR_BATCH=0 GE_CORPSE_BAKE=0 GE_HASHLOG=$O/off.txt $B/ProjectMidas > $O/off.log 2>&1
n1=$(wc -l < $O/on.txt)
n2=$(wc -l < $O/off.txt)
if cmp -s $O/on.txt $O/off.txt; then r=IDENTICAL; else r=DIFFERS; fi
echo "hash lines on $n1 off $n2: $r"

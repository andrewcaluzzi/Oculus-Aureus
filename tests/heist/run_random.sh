#!/bin/sh
# The heist randomiser (designs/bank-heist.md section 25), inside the build container with Xvfb on :99 and build_pc built.
#   layout  every seed in SEEDS deals its bank (the mode's "randomiser:" log line, at stage start); the layouts must differ (at least
#           MINDIFF distinct), and the first seed run twice must deal the same bank, hash-identical tick for tick (GE_HASHLOG)
#   win     the win path (tests/heist/heist_winpath.lua, GE_HEIST_TEST_RANDOM=1) on every seed: the carriers come from the mode's truth,
#           the INTEL board and the guide must name them; every seed must end "HEIST DONE n passed, 0 failed" and win
# usage: run_random.sh [layout|win|all]; SEEDS="1 2 ..." (default 1..10), PAR=3 (games at once), OUT=<dir> (default /tmp/heist_random)
# Last line: "HEIST RANDOM DONE <n> passed, <m> failed".
WHICH=${1:-all}
SEEDS=${SEEDS:-"1 2 3 4 5 6 7 8 9 10"}
PAR=${PAR:-3}
MINDIFF=${MINDIFF:-8}
OUT=${OUT:-/tmp/heist_random}
mkdir -p $OUT
M=/home/dev/project/build_pc/Data/Scripts/modes
cp /home/dev/project/tests/heist/heist_winpath.lua $M/heist_winpath.lua
cp /home/dev/project/tools/mode_heist.lua $M/heist.lua
for f in heist_lib heist_bank_map heist_ai heist_intel heist_guide heist_randomiser heist_gadgets heist_ar; do
    cp /home/dev/project/tools/$f.lua $M/$f.lua
done
cp /home/dev/project/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
cd /home/dev/project/build_pc || exit 1
PASS=0
FAIL=0
ok() { echo "HEIST RANDOM PASS $*"; PASS=$((PASS + 1)); }
bad() { echo "HEIST RANDOM FAIL $*"; FAIL=$((FAIL + 1)); }

# game <name> <seed> <frames> <extra env...>
game() {
    n=$1; s=$2; fr=$3
    shift 3
    env DISPLAY=:99 GE_MODE=heist_winpath GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=$s \
        GE_HEIST_TEST_RANDOM=1 GE_FRAMES=$fr "$@" timeout ${TIMEOUT:-600} ./ProjectMidas > $OUT/$n.log 2>&1
}

if [ "$WHICH" = layout ] || [ "$WHICH" = all ]; then
    first=""
    k=0
    for s in $SEEDS; do
        [ -z "$first" ] && first=$s
        game layout_$s $s 1400 GE_HEIST_TEST_COUNT=1 &
        k=$((k + 1))
        if [ $k -ge $PAR ]; then wait; k=0; fi
    done
    game layout_again $first 1400 GE_HEIST_TEST_COUNT=1 GE_HASHLOG=$OUT/again_b.hash &
    wait
    game layout_again_a $first 1400 GE_HEIST_TEST_COUNT=1 GE_HASHLOG=$OUT/again_a.hash
    : > $OUT/layouts.txt
    for s in $SEEDS; do
        l=$(grep -a "heist: randomiser: GREEN=" $OUT/layout_$s.log | head -n 1 | sed 's/.*randomiser: //')
        if [ -z "$l" ]; then bad "layout seed $s: no deal in the log"; else echo "seed $s: $l" | tee -a $OUT/layouts.txt; fi
    done
    nd=$(sed 's/^seed [0-9]*: //' $OUT/layouts.txt | sort -u | wc -l)
    ns=$(wc -l < $OUT/layouts.txt)
    if [ "$nd" -ge "$MINDIFF" ]; then ok "layouts differ: $nd distinct of $ns seeds"; else bad "layouts: only $nd distinct of $ns seeds (need $MINDIFF)"; fi
    a=$(grep -a "heist: randomiser: GREEN=" $OUT/layout_$first.log | head -n 1 | sed 's/.*randomiser: //')
    b=$(grep -a "heist: randomiser: GREEN=" $OUT/layout_again.log | head -n 1 | sed 's/.*randomiser: //')
    c=$(grep -a "heist: randomiser: GREEN=" $OUT/layout_again_a.log | head -n 1 | sed 's/.*randomiser: //')
    if [ -n "$a" ] && [ "$a" = "$b" ] && [ "$a" = "$c" ]; then ok "seed $first deals the same bank three times"; else bad "seed $first: '$a' / '$b' / '$c'"; fi
    na=$(grep -c '^tick' $OUT/again_a.hash)
    nb=$(grep -c '^tick' $OUT/again_b.hash)
    n=$na
    [ "$nb" -lt "$n" ] && n=$nb
    grep '^tick' $OUT/again_a.hash | head -n $n > $OUT/cmp.a
    grep '^tick' $OUT/again_b.hash | head -n $n > $OUT/cmp.b
    if [ "$n" -ge 500 ] && cmp -s $OUT/cmp.a $OUT/cmp.b; then ok "seed $first twice: $n ticks hash-identical"
    else bad "seed $first twice: $n ticks, first difference: $(diff $OUT/cmp.a $OUT/cmp.b | grep '^<' | head -n 1 | cut -c1-160)"; fi
fi

if [ "$WHICH" = win ] || [ "$WHICH" = all ]; then
    k=0
    for s in $SEEDS; do
        game win_$s $s ${GE_FRAMES:-14000} GE_HEIST_TEST_WIN=1 &
        k=$((k + 1))
        if [ $k -ge $PAR ]; then wait; k=0; fi
    done
    wait
    for s in $SEEDS; do
        d=$(grep -a "HEIST DONE" $OUT/win_$s.log | tail -n 1)
        l=$(grep -a "HEIST RANDOM layout" $OUT/win_$s.log | head -n 1 | sed 's/.*layout //' | cut -c1-150)
        if echo "$d" | grep -q " 0 failed" && grep -aq "HEIST event mission.win" $OUT/win_$s.log; then ok "seed $s winnable: $d ($l)"
        else bad "seed $s: ${d:-no HEIST DONE} ($l)"; grep -a "HEIST FAIL" $OUT/win_$s.log | head -n 5; fi
    done
fi
echo "HEIST RANDOM DONE $PASS passed, $FAIL failed"

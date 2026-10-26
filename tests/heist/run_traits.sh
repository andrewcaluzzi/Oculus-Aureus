#!/bin/sh
# Guard personalities (designs/bank-heist.md section 32) on the Janus bank, inside the build container with Xvfb on :99 and build_pc
# built. One run per scenario of tests/heist/heist_traits.lua (RUNS="off deal rookie book patrol bribe shot"), one game at a time:
#   deal is run with seed 7 twice and seeds 8 and 9: the same seed must deal the same guards, another seed a different deal;
#   bribe also checks the debrief line lists "Paid Off"; shot writes the PERSONNEL page (and the AR view) to $SHOTS.
# Then (WHICH=all, or det): the heist determinism scenario with the traits dealt (GE_HEIST_TEST_TRAITS=1), solo and co-op
# (tests/heist/run_determinism.sh). The "off = today" hash proof is tests/heist/run_traits_base.sh, run on a build without the stream.
# usage: run_traits.sh [runs|det|all] -> "HEIST ..." lines; last line "HEIST TRAITS DONE <n> passed, <m> failed". Logs: $OUT
WHICH=${1:-runs}
OUT=${OUT:-/tmp/heist_traits}
SHOTS=${SHOTS:-$OUT/shots}
mkdir -p $OUT $SHOTS
B=/home/dev/project/build_pc
T=/home/dev/project/tools
cp /home/dev/project/tests/heist/heist_traits.lua $B/Data/Scripts/modes/heist_traits_test.lua
cp $T/mode_heist.lua $B/Data/Scripts/modes/heist.lua
for f in heist_lib heist_bank_map heist_ai heist_combat heist_ar heist_intel heist_debrief heist_traits; do cp $T/$f.lua $B/Data/Scripts/modes/$f.lua; done
cp /home/dev/project/port/levelgen/levelgen_layout.lua $B/Data/Scripts/modes/levelgen_layout.lua
PASS=0
FAIL=0

# one <run> <seed> [frames]
one() {
    run=$1
    seed=$2
    frames=${3:-3600}
    log=$OUT/$run.$seed.log
    rm -rf $OUT/shotdir
    mkdir -p $OUT/shotdir
    (cd $OUT/shotdir && env GE_TR_$(echo $run | tr a-z A-Z)=1 DISPLAY=:99 GE_MODE=heist_traits_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 \
        GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=$seed GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=$frames timeout 900 $B/ProjectMidas > $log 2>&1)
    echo "HEIST RUN $run (seed $seed)"
    grep -aE "^ge: lua: HEIST (PASS|FAIL|DONE|SHOT|  )|ge: traits:|ge: lua: .*error" $log | grep -av "HEIST TRAITS DEAL" | head -n 60
    p=$(grep -a "HEIST DONE" $log | tail -n 1 | sed 's/.*DONE \([0-9]*\) passed, \([0-9]*\) failed.*/\1 \2/')
    if [ -n "$p" ]; then PASS=$((PASS + ${p% *})); FAIL=$((FAIL + ${p#* })); else FAIL=$((FAIL + 1)); echo "HEIST FAIL $run: no DONE line"; fi
    names=$(grep -a "HEIST SHOT" $log | sed 's/.*HEIST SHOT \([a-z0-9_]*\).*/\1/')
    i=0
    for f in $(ls $OUT/shotdir/shot_*.bmp 2>/dev/null); do
        i=$((i + 1))
        n=$(echo "$names" | sed -n "${i}p")
        python3 /home/dev/project/tests/bmp2png.py $f $SHOTS/${n:-shot_$i}.png && rm -f $f
    done
}
pass_if() {
    if [ "$1" = 1 ]; then echo "HEIST PASS $2"; PASS=$((PASS + 1)); else echo "HEIST FAIL $2"; FAIL=$((FAIL + 1)); fi
}

if [ "$WHICH" = runs ] || [ "$WHICH" = all ]; then
    for run in ${RUNS:-off deal rookie book patrol bribe shot}; do
        case $run in
            deal)
                one deal 7
                cp $OUT/deal.7.log $OUT/deal.7a.log
                one deal 7
                one deal 8
                one deal 9
                a=$(grep -a "HEIST TRAITS DEAL" $OUT/deal.7a.log | md5sum | cut -c1-8)
                b=$(grep -a "HEIST TRAITS DEAL" $OUT/deal.7.log | md5sum | cut -c1-8)
                c=$(grep -a "HEIST TRAITS DEAL" $OUT/deal.8.log | md5sum | cut -c1-8)
                d=$(grep -a "HEIST TRAITS DEAL" $OUT/deal.9.log | md5sum | cut -c1-8)
                ok=0; [ -n "$(grep -a 'HEIST TRAITS DEAL' $OUT/deal.7.log)" ] && [ "$a" = "$b" ] && ok=1
                pass_if $ok "deal: seed 7 twice deals the same guards ($a $b)"
                ok=0; [ "$a" != "$c" ] && [ "$a" != "$d" ] && [ "$c" != "$d" ] && ok=1
                pass_if $ok "deal: seeds 7, 8, 9 deal three different banks ($a $c $d)"
                ;;
            patrol) one patrol 7 26000 ;;
            bribe)
                one bribe 7
                ok=0; grep -a "HEIST DEBRIEF" $OUT/bribe.7.log | grep -q "Paid Off" && ok=1
                pass_if $ok "bribe: the debrief lists the Paid Off commendation"
                ;;
            *) one $run 7 ;;
        esac
    done
fi
if [ "$WHICH" = det ] || [ "$WHICH" = all ]; then
    for c in solo coop; do
        OUT=$OUT/det GE_HEIST_TEST_TRAITS=1 sh /home/dev/project/tests/heist/run_determinism.sh $c > $OUT/det_$c.txt 2>&1
        grep -a "HEIST DET" $OUT/det_$c.txt
        p=$(grep -a "HEIST DET DONE" $OUT/det_$c.txt | sed 's/.*DONE \([0-9]*\) passed, \([0-9]*\) failed.*/\1 \2/')
        if [ -n "$p" ]; then PASS=$((PASS + ${p% *})); FAIL=$((FAIL + ${p#* })); else FAIL=$((FAIL + 1)); fi
        ok=0; grep -aq "traits: dealt" $OUT/det/$c.a.log && ok=1
        pass_if $ok "det $c: the traits were dealt in the scenario"
    done
fi
echo "HEIST TRAITS DONE $PASS passed, $FAIL failed"

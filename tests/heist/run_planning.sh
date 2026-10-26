#!/bin/sh
# Heist PLANNING phase tests (designs/bank-heist.md section 27), inside the build container with Xvfb on :99 and build_pc built.
# Runs of tests/heist/heist_planning_test.lua on the real mode (GE_SEED 7, GE_FIXEDTICK=1):
#   off     planning off (an automated run): no plan, the default kit, Bond on the street, the clock runs
#   plan    GE_PLANNING=1: the hold, the budget, the scripted plan (lockpicks, 2 coins, the roof) and the quick job: the payout less the
#           planning cost; two screenshots of the board -> $SHOTS/plan_loadout.png, plan_entry.png
#   keys    GE_PLANNING=1 with GE_JOYPORT_AUTO next_weapon presses: the board's page keys flip the plan's tabs (sim input)
#   det     the scripted plan then 20 s of play, twice solo and twice in local co-op (the second view-switched): per-tick state hashes
#           (GE_HASHLOG) identical over >= NEED ticks, and the scenario's notes identical
#   offdet  planning off is today's start: the determinism heist (tests/heist/heist_determinism.lua) as is and with planning = false:
#           hash-identical
# usage: [RUNS="off plan keys det offdet"] run_planning.sh   -> "HEIST ..." lines; last line "HEIST PLANNING DONE <n> passed, <m> failed"
OUT=${OUT:-/tmp/heist_planning}
SHOTS=${SHOTS:-$OUT/shots}
mkdir -p $OUT $SHOTS
B=/home/dev/project/build_pc
M=$B/Data/Scripts/modes
cp /home/dev/project/tests/heist/heist_planning_test.lua $M/heist_planning_test.lua
cp /home/dev/project/tools/mode_heist.lua $M/heist.lua
cp /home/dev/project/tools/heist_planning.lua $M/heist_planning.lua
cp /home/dev/project/tools/heist_debrief.lua $M/heist_debrief.lua
cp /home/dev/project/tools/heist_gadgets.lua $M/heist_gadgets.lua
cp /home/dev/project/tools/heist_lib.lua $M/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua $M/heist_bank_map.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
PASS=0
FAIL=0
NEED=${NEED:-1500}

game() {   # game <name> <frames> <env...>
    n=$1; f=$2; shift 2
    rm -rf $OUT/shot_$n; mkdir -p $OUT/shot_$n
    (cd $OUT/shot_$n && env "$@" DISPLAY=:99 GE_MODE=heist_planning_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=${GE_SEED:-7} \
        GE_FIXEDTICK=1 GE_TICKHZ=60 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=$f timeout ${TIMEOUT:-600} $B/ProjectMidas > $OUT/$n.log 2>&1)
}
tally() {   # tally <name>: the run's HEIST lines and its DONE count
    grep -a "HEIST\|ge: lua: .*error\|ge: heist: plan" $OUT/$1.log | grep -av "HEIST DEBRIEF {" | sed "s/^/[$1] /"
    p=$(grep -a "HEIST DONE" $OUT/$1.log | tail -n 1 | sed 's/.*DONE \([0-9]*\) passed, \([0-9]*\) failed.*/\1 \2/')
    if [ -n "$p" ]; then PASS=$((PASS + ${p% *})); FAIL=$((FAIL + ${p#* })); else FAIL=$((FAIL + 1)); echo "HEIST FAIL $1: no DONE line"; fi
}
hcmp() {   # hcmp <a> <b> <need>: per-tick hashes identical
    grep '^tick' $OUT/$1.hash > $OUT/cmp.a; grep '^tick' $OUT/$2.hash > $OUT/cmp.b
    na=$(wc -l < $OUT/cmp.a); nb=$(wc -l < $OUT/cmp.b); n=$na; [ "$nb" -lt "$n" ] && n=$nb
    head -n $n $OUT/cmp.a > $OUT/cmp.a2; head -n $n $OUT/cmp.b > $OUT/cmp.b2
    if cmp -s $OUT/cmp.a2 $OUT/cmp.b2 && [ "$n" -ge "$3" ]; then echo "HEIST PASS det $1 vs $2: $n ticks hash-identical"; PASS=$((PASS + 1))
    else first=$(diff $OUT/cmp.a2 $OUT/cmp.b2 | grep '^<' | head -n 1 | cut -c1-200)
         echo "HEIST FAIL det $1 vs $2: $n ticks (need $3), first difference: ${first:-none}"; FAIL=$((FAIL + 1)); fi
}

for run in ${RUNS:-off plan keys det offdet}; do
    echo "HEIST RUN $run"
    case $run in
    off)  game off 1500 GE_PLAN_OFF=1; tally off ;;
    plan) game plan 6500 GE_PLANNING=1 GE_PLAN_SHOTS=1; tally plan
          i=0
          for f in $(ls $OUT/shot_plan/shot_*.bmp 2>/dev/null | head -n 2); do
              i=$((i + 1)); nm=plan_loadout; [ $i = 2 ] && nm=plan_entry
              python3 /home/dev/project/tests/bmp2png.py $f $SHOTS/$nm.png && echo "HEIST shot: $SHOTS/$nm.png"
          done
          rm -f $OUT/shot_plan/shot_*.bmp ;;
    keys) game keys 1500 GE_PLANNING=1 GE_PLAN_KEYS=1 GE_JOYPORT_AUTO=next_weapon:300,next_weapon:360; tally keys ;;
    det)  for d in solo1 solo2 coop1 coop2; do
              x=""; case $d in coop1) x="GE_COOP=1" ;; coop2) x="GE_COOP=1 GE_COOP_VIEW=1" ;; esac
              game $d 3200 GE_PLANNING=1 GE_PLAN_DET=1 GE_HASHLOG=$OUT/$d.hash $x
              tally $d
          done
          hcmp solo1 solo2 $NEED; hcmp coop1 coop2 $NEED
          for pr in "solo1 solo2" "coop1 coop2"; do
              set -- $pr
              grep -a "HEIST DET" $OUT/$1.log | sed 's/^.*HEIST DET/HEIST DET/' > $OUT/n.a; grep -a "HEIST DET" $OUT/$2.log | sed 's/^.*HEIST DET/HEIST DET/' > $OUT/n.b
              if [ -s $OUT/n.a ] && cmp -s $OUT/n.a $OUT/n.b; then echo "HEIST PASS det $1 vs $2: the notes agree"; PASS=$((PASS + 1))
              else echo "HEIST FAIL det $1 vs $2: the notes differ"; FAIL=$((FAIL + 1)); fi
          done ;;
    offdet)
          cp /home/dev/project/tests/heist/heist_determinism.lua $M/heist_determinism.lua
          sed 's/^HEIST_SETTINGS = {/HEIST_SETTINGS = { planning = false,/' /home/dev/project/tests/heist/heist_determinism.lua > $M/heist_det_noplan.lua
          for m in heist_determinism heist_det_noplan; do
              (cd $B && env DISPLAY=:99 GE_MODE=$m GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 \
                  GE_FRAMES=${DET_FRAMES:-4500} GE_HASHLOG=$OUT/$m.hash timeout 600 ./ProjectMidas > $OUT/$m.log 2>&1)
          done
          if grep -q "planning = false" $M/heist_det_noplan.lua; then hcmp heist_determinism heist_det_noplan 3000
          else echo "HEIST FAIL offdet: the settings line was not patched"; FAIL=$((FAIL + 1)); fi ;;
    esac
done
echo "HEIST PLANNING DONE $PASS passed, $FAIL failed"

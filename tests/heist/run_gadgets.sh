#!/bin/sh
# The heist's gadgets (designs/bank-heist.md section 21, tools/heist_gadgets.lua) on the Janus bank, inside the build container with Xvfb
# on :99 and build_pc built (tools/levelgen/janus_bank.json on the levelgen slot). One run per scenario of tests/heist/heist_gadgets.lua.
#   run_gadgets.sh                 every scenario: piton lockpick pocket coin loop disguise vent gas thermal safe (RUNS="piton coin" picks)
#   run_gadgets.sh det             determinism: the gadget scenario twice solo, twice in local co-op (the second view-switched: the
#                                  netplay condition); each pair's per-tick state hashes (GE_HASHLOG) must be identical over >= NEED ticks
# GE_GADGETS_SHOTS=1 adds screenshots (in ${SHOTDIR:-build_pc/heist_gadget_shots}; convert with tests/bmp2png.py, delete the BMPs).
# Output: the HEIST lines of each run; each ends "HEIST DONE <n> passed, <m> failed". Logs: ${LOGDIR:-/tmp}/heist_gadgets_<run>.log
cp /home/dev/project/tests/heist/heist_gadgets.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_gadgets_test.lua   # (the mode heist_gadgets is the gadgets module)
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_gadgets.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_gadgets.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
LOGDIR=${LOGDIR:-/tmp}
SHOTDIR=${SHOTDIR:-/home/dev/project/build_pc/heist_gadget_shots}
mkdir -p $LOGDIR $SHOTDIR
cd $SHOTDIR || exit 1

if [ "$1" = "det" ]; then
    NEED=${NEED:-2400}
    PASS=0
    FAIL=0
    drun() {   # drun <name> <env...>
        n=$1
        shift
        env GE_GADGETS_DET=1 DISPLAY=:99 GE_MODE=heist_gadgets_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 \
            GE_SEED=7 GE_FRAMES=${GE_FRAMES:-4000} GE_HASHLOG=$LOGDIR/gdet_$n.hash "$@" timeout 600 /home/dev/project/build_pc/ProjectMidas \
            > $LOGDIR/gdet_$n.log 2>&1
        grep -a "HEIST DET\|ge: lua: .*error" $LOGDIR/gdet_$n.log | sed "s/^/[$n] /"
    }
    dcmp() {   # dcmp <a> <b>
        grep '^tick' $LOGDIR/gdet_$1.hash > $LOGDIR/gdet_cmp.a
        grep '^tick' $LOGDIR/gdet_$2.hash > $LOGDIR/gdet_cmp.b
        na=$(wc -l < $LOGDIR/gdet_cmp.a)
        nb=$(wc -l < $LOGDIR/gdet_cmp.b)
        n=$na
        [ "$nb" -lt "$n" ] && n=$nb
        head -n $n $LOGDIR/gdet_cmp.a > $LOGDIR/gdet_cmp.a2
        head -n $n $LOGDIR/gdet_cmp.b > $LOGDIR/gdet_cmp.b2
        if cmp -s $LOGDIR/gdet_cmp.a2 $LOGDIR/gdet_cmp.b2 && [ "$n" -ge "$NEED" ]; then
            echo "HEIST DET PASS $1 vs $2: $n ticks hash-identical"
            PASS=$((PASS + 1))
        else
            first=$(diff $LOGDIR/gdet_cmp.a2 $LOGDIR/gdet_cmp.b2 | grep '^<' | head -n 1 | cut -c1-200)
            echo "HEIST DET FAIL $1 vs $2: $n ticks (need $NEED), first difference: ${first:-none}"
            FAIL=$((FAIL + 1))
        fi
        grep -a "HEIST DET" $LOGDIR/gdet_$1.log | sed 's/^.*HEIST DET/HEIST DET/' > $LOGDIR/gdet_n.a
        grep -a "HEIST DET" $LOGDIR/gdet_$2.log | sed 's/^.*HEIST DET/HEIST DET/' > $LOGDIR/gdet_n.b
        if cmp -s $LOGDIR/gdet_n.a $LOGDIR/gdet_n.b; then
            echo "HEIST DET PASS $1 vs $2: the scenario's notes agree"
            PASS=$((PASS + 1))
        else
            echo "HEIST DET FAIL $1 vs $2: the scenario's notes differ"
            FAIL=$((FAIL + 1))
        fi
    }
    drun solo1
    drun solo2
    dcmp solo1 solo2
    drun coop1 GE_COOP=1
    drun coop2 GE_COOP=1 GE_COOP_VIEW=1
    dcmp coop1 coop2
    echo "HEIST DET DONE $PASS passed, $FAIL failed"
    exit 0
fi

for run in ${RUNS:-piton lockpick pocket coin loop disguise vent gas thermal safe}; do
    flag=GE_GADGETS_$(echo $run | tr a-z A-Z)=1
    echo "HEIST RUN $run"
    frames=5200
    [ "$run" = coin ] && frames=8000   # (with the AI layer the coin's guard searches 30 s, gives up and walks back)
    env $flag DISPLAY=:99 GE_MODE=heist_gadgets_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 \
        GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=${GE_FRAMES:-$frames} timeout ${TIMEOUT:-600} /home/dev/project/build_pc/ProjectMidas > $LOGDIR/heist_gadgets_$run.log 2>&1
    grep -aE "HEIST|gfx: wrote|ge: lua: ge: heist: (gadgets|ready|ALARM|cover)|ge: gadgets|ge: lua: .*error" $LOGDIR/heist_gadgets_$run.log
done

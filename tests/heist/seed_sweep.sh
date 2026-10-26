#!/bin/bash
# Seed sweep of the heist suites (designs/bank-heist.md section 23f), inside the build container with Xvfb on :99 and build_pc built.
# Runs each (suite, run, seed) job as one fast-forwarded game: GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_CLOCKSCALE (the bot harness's mode: the
# same sim, only sooner) with an unbounded GE_FRAMES, and kills the game once it prints "HEIST DONE" (the suites' own run_*.sh scripts
# are paced in real time because GE_FRAMES counts pumps, which run out under GE_NOSLEEP before the scenario ends).
#   seed_sweep.sh job <suite> <run> <seed>     one job; appends "RESULT <suite> <run> <seed> <passed> <failed> <DONE|TIMEOUT|NODONE> <s>"
#                                              to $OUT/results.txt and keeps the log as $OUT/logs/<suite>_<run>_s<seed>.log
#   seed_sweep.sh copy                         copy the suites' Lua into build_pc/Data/Scripts/modes (once, before any job)
#   seed_sweep.sh list <seeds...>             print the job list (suite run seed) for SUITES (default: winpath guards ai gadgets cover firstplay)
#   seed_sweep.sh all <seeds...>               run the list, P jobs at once (default 6)
#   seed_sweep.sh table                        per suite: seeds clean / seeds complete, and each failing seed:run
# Env: OUT (default /soak/sweep), P, BIN (the game; a copy in build_pc, e.g. ProjectMidas_sweep, lets a rebuild run beside a sweep), SIMONLY (GE_SIMONLY_TEST, default 2,59,60), SCALE (GE_CLOCKSCALE, default 20), WALL (wall-clock limit per job, default 600 s), LP_NUM_THREADS
# (llvmpipe's render threads per game, default 1). Suites/runs: winpath win trapped fail quiet count | guards main | ai <run_ai.sh runs> |
# gadgets <run_gadgets.sh runs> | cover <run_cover.sh runs> | firstplay main.
B=${B:-/home/dev/project/build_pc}   # (B: another run dir, a copy of build_pc's game + Data, to try a change beside a running sweep)
T=/home/dev/project/tests/heist
OUT=${OUT:-/soak/sweep}
SUITES=${SUITES:-winpath guards ai gadgets cover firstplay}
SIMONLY=${SIMONLY-2,59,60}   # port_simonly.c: render one tick in 60 (the sim is the same; the shot scenarios still get frames). SIMONLY= renders every tick

runs_of() {
    case $1 in
        winpath) echo "win trapped fail quiet count" ;;
        guards|firstplay) echo "main" ;;
        ai) echo "hear slap door giveup camko camact camdown checkin patrol backup lure camwarn holdcard" ;;
        gadgets) echo "piton lockpick pocket coin loop disguise vent gas thermal safe" ;;
        cover) echo "hall gun loot restricted staff ko cam camdown power interrupt generator doors" ;;
    esac
}

copy_scripts() {
    M=$B/Data/Scripts/modes
    cp $T/heist_winpath.lua $M/heist_winpath.lua
    cp $T/heist_guards.lua $M/heist_guards.lua
    cp $T/heist_ai.lua $M/heist_ai_test.lua
    cp $T/heist_gadgets.lua $M/heist_gadgets_test.lua
    cp $T/heist_cover.lua $M/heist_cover.lua
    cp $T/heist_firstplay.lua $M/heist_firstplay.lua
    for f in mode_heist:heist heist_lib heist_bank_map heist_ai heist_intel heist_ar heist_gadgets heist_guide; do
        cp /home/dev/project/tools/${f%%:*}.lua $M/${f##*:}.lua
    done
    cp /home/dev/project/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
}

job() {
    suite=$1; run=$2; seed=$3
    up=$(echo $run | tr a-z A-Z)
    case $suite in
        winpath)
            mode=heist_winpath
            case $run in
                trapped) flags="GE_HEIST_TEST_TRAPPED=1" ;;
                fail) flags="GE_HEIST_TEST_FAIL=1" ;;
                quiet) flags="GE_HEIST_TEST_QUIET=1" ;;
                count) flags="GE_HEIST_TEST_COUNT=1 GE_DIFF=3" ;;
                *) flags="GE_HEIST_TEST_WIN=1" ;;
            esac ;;
        guards) mode=heist_guards; flags="" ;;
        ai) mode=heist_ai_test; flags="GE_AI_$up=1" ;;
        gadgets) mode=heist_gadgets_test; flags="GE_GADGETS_$up=1" ;;
        cover) mode=heist_cover; flags="GE_COVER_$up=1" ;;
        firstplay) mode=heist_firstplay; flags="" ;;
        *) echo "unknown suite $suite"; return 1 ;;
    esac
    mode=${MODE:-$mode}                    # (MODE: a debug copy of the suite's script under another mode name)
    mkdir -p $OUT/logs
    log=$OUT/logs/${suite}_${run}_s$seed.log
    wd=$(mktemp -d /tmp/sweep_XXXXXX)      # each game its own cwd: the shot scenarios write BMPs there
    t0=$(date +%s)
    ( cd $wd && exec env $flags DISPLAY=:99 GE_MODE=$mode GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 \
        GE_NOSLEEP=1 GE_CLOCKSCALE=${SCALE:-20} GE_FRAMES=2000000000 GE_SEED=$seed GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
        GE_SIMONLY_TEST=$SIMONLY LP_NUM_THREADS=${LP_NUM_THREADS:-1} ${BIN:-$B/ProjectMidas} ) > $log 2>&1 &
    pid=$!
    status=TIMEOUT
    while kill -0 $pid 2>/dev/null; do
        if grep -saq "HEIST DONE\|HEIST firstplay error" $log; then sleep 1; status=DONE; break; fi
        if [ $(( $(date +%s) - t0 )) -ge ${WALL:-600} ]; then break; fi
        sleep 1
    done
    if kill -0 $pid 2>/dev/null; then kill $pid; sleep 1; kill -9 $pid 2>/dev/null; elif [ $status = TIMEOUT ]; then status=NODONE; fi
    wait $pid 2>/dev/null
    rm -rf $wd
    line=$(grep -a "HEIST DONE" $log | tail -n 1)
    p=$(echo "$line" | sed -n 's/.*DONE[^0-9]*\([0-9]*\) passed, \([0-9]*\) failed.*/\1/p')
    f=$(echo "$line" | sed -n 's/.*DONE[^0-9]*\([0-9]*\) passed, \([0-9]*\) failed.*/\2/p')
    echo "RESULT $suite $run $seed ${p:--} ${f:--} $status $(( $(date +%s) - t0 ))" | tee -a $OUT/results.txt
}

case $1 in
    job) shift; job "$@" ;;
    copy) copy_scripts ;;
    table)   # per suite: seeds where every run finished clean / seeds with every run finished, and the failing (seed:run) list
        for su in $SUITES; do   # (a seed is complete when it has every run the file has for that suite)
            awk -v su=$su '$1 == "RESULT" && $2 == su { n[$4]++; if (!($3 in r)) { r[$3] = 1; nr++ }
                                                       if ($6 != "0" || $7 != "DONE") { bad[$4] = 1; f = f " " $4 ":" $3 } }
                END { t = 0; ok = 0; for (s in n) if (n[s] >= nr) { t++; if (!bad[s]) ok++ }
                      printf "%-10s %d/%d seeds clean;%s\n", su, ok, t, f }' $OUT/results.txt
        done ;;
    list) shift; for s in "$@"; do for su in $SUITES; do for r in $(runs_of $su); do echo "$su $r $s"; done; done; done ;;
    all)
        shift
        copy_scripts
        mkdir -p $OUT
        "$0" list "$@" | xargs -P ${P:-6} -L 1 "$0" job ;;
    *) sed -n '2,16p' "$0" ;;
esac

#!/bin/sh
# The A7 merged-branch test sweep (modding/arena.md "A7 merged"): the three branches' own presets plus the cross-feature ones, in
# three lanes that can run side by side (each in its own docker exec, user 1000, cwd build_pc, Xvfb on :99). Netplay, the coop
# determinism test, the dam1 oracle and the perf A/B are NOT here: run those alone.
#   sh tests/arena_a7/merge_sweep.sh polish | mut | coop
# Logs: build_tests/a7/<name>.log (A6: build_tests/a6/). Each run starts without a save (an eeprom.bin sends the boot to the file select).
P=/home/dev/project
T=$P/tests/arena_a7
mkdir -p $P/build_tests/a7 $P/build_tests/a6
cp $T/*.lua $P/tests/arena_a6/*.lua $P/build_pc/Data/Scripts/modes/
nosave() { rm -f $P/build_pc/Data/Saves/eeprom.bin; }
case "$1" in
polish)
    nosave; sh $T/run_polish.sh m-knife-keep polish-knife-keep 3600
    nosave; sh $T/run_polish.sh m-seats-fix100 polish-seats-fix100 500
    nosave; sh $T/run_polish.sh m-seats-fix200 polish-seats-fix200 500
    nosave; sh $P/tests/arena_a6/run.sh m-heal arena-heal 2400
    nosave; sh $P/tests/arena_a6/run.sh m-ammobox arena-ammobox 2400
    nosave; sh $P/tests/arena_a6/run.sh m-walk arena-walk 7000
    ;;
mut)
    nosave; sh $T/run_mut.sh m-mut arena7-mut 12000
    nosave; sh $T/run_mut.sh m-knives arena7-knives 2700 60 none
    nosave; sh $T/run_mut.sh m-knivesctl arena7-knivesctl 2700 60 none
    nosave; sh $T/run_mut.sh m-elites arena7-elites 6000
    nosave; sh $T/run_mut.sh m-regen arena7-regen 1500
    rm -f $P/build_tests/a7/m-score.ini
    nosave; GE_NOSCORESAVE=0 GE_SCOREFILE=$P/build_tests/a7/m-score.ini sh $T/run_mut.sh m-score arena7-score 1500
    nosave; sh $T/run_mut.sh m-die arena7-die 2400 60 none
    nosave; sh $T/run_mut.sh m-boom arena7-boom 12000
    nosave; sh $T/run_mut.sh m-soak arena7-soak 14000
    ;;
coop)
    sh $T/run_coop.sh m-coop-smoke coop-smoke 5000 "GE_COOP_GOD=0"
    sh $T/run_coop.sh m-coop-revive coop-revive 4800 "GE_COOP_GOD=0 GE_MODEHUD_LOG=1"
    sh $T/run_coop.sh m-coop-bleed coop-bleed 3000 "GE_COOP_GOD=0"
    sh $T/run_coop.sh m-coop-end coop-end 1500 "GE_COOP_GOD=0"
    sh $T/run_coop.sh m-coop-wipe coop-wipe 1500 "GE_COOP_HURT=600:1:50"
    sh $T/run_coop.sh m-coop-box coop-box 1200
    sh $T/run_coop.sh m-coop-loot coop-loot 3000 "GE_COOP_GOD=0"
    sh $T/run_coop.sh m-coopchaos merge-coopchaos 14000 "GE_COOP_GOD=0"
    sh $T/run_coop.sh m-coop-waves coop-waves 16000 "GE_COOP_GOD=0"
    ;;
*) echo "usage: merge_sweep.sh polish | mut | coop"; exit 2 ;;
esac
echo "lane $1 done"

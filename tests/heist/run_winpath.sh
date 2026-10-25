#!/bin/sh
# Heist mode test on the Janus bank (P0 job 6; the bank since P2.4), inside the build container with Xvfb on :99 and build_pc built with
# tools/levelgen/janus_bank.json on the levelgen slot. Five runs: the win path, the trapped-in-the-vault path, the civilian-limit fail
# path, the quiet win (an empty vault seals with no alarm; the quiet bonus), and the full force on 007 (GE_DIFF=3: 100+ guards).
# One sim tick per frame (GE_FIXEDTICK=1). Not GE_NOSLEEP: it pumps the retrace faster than the game uses it, and GE_FRAMES (which counts
# pumps) runs out during the boot. For speed, run the RUNS in parallel containers (one RUNS=<run> each).
# usage: run_winpath.sh   -> prints the HEIST lines of each run; each run ends with "HEIST DONE <n> passed, <m> failed"
# The full logs are kept in ${LOGDIR:-/tmp}/heist_winpath_{win,trapped,fail,quiet,count}.log.
# Seeded (GE_SEED, default 7; 2026-10-26, like run_guards.sh): every run is the same game. Pass another GE_SEED to try other games.
cp /home/dev/project/tests/heist/heist_winpath.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_winpath.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/tools/heist_ai.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_ai.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
cd /home/dev/project/build_pc || exit 1
LOGDIR=${LOGDIR:-/tmp}
mkdir -p $LOGDIR
for run in ${RUNS:-win trapped fail quiet count}; do
    frames=${GE_FRAMES:-14000}
    case $run in
        trapped) flags="GE_HEIST_TEST_TRAPPED=1" ;;
        fail) flags="GE_HEIST_TEST_FAIL=1" ;;
        quiet) flags="GE_HEIST_TEST_QUIET=1" ;;
        count) flags="GE_HEIST_TEST_COUNT=1 GE_DIFF=3"; frames=3000 ;;
        *) flags="GE_HEIST_TEST_WIN=1" ;;
    esac
    echo "HEIST RUN $run"
    env $flags DISPLAY=:99 GE_MODE=heist_winpath GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=${GE_SEED:-7} \
        GE_FRAMES=$frames timeout ${TIMEOUT:-500} ./ProjectMidas > $LOGDIR/heist_winpath_$run.log 2>&1
    grep -E "HEIST|ge: lua: ge: heist: (ready|WIN|FAIL|HIGH|VAULT|ALARM|vault|re-crack|the cracker|cracker|heavy|crack|security console|setup step|civilian)|ge: lua: .*error" $LOGDIR/heist_winpath_$run.log
done

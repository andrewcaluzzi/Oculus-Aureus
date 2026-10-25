#!/bin/sh
# Speedrun one stage with the segment optimiser, then verify the result by a clean run and its replay.
#
#   sh tests/bot/speedrun.sh <stage> [seed]          (inside the build container, Xvfb on :99)
#
# 1. optimiser: GE_PLAYERBOT=speedrun GE_PLAYERBOT_OPT=1 - each route step is played with every variant from a solo
#    savestate (port_snap.c) and the fastest kept; decisions go to $OUT/opt.state. A run whose trial ends the mission
#    exits 3 and is started again (it carries on from the state file).
# 2. clean run: the chosen variants without savestates, recorded (GE_INPUTREC), then replayed without the bot
#    (tests/bot/level.sh): the result line and replay verdict of the optimised run.
# 3. base run: the same route with variant 0 everywhere (what the optimiser started from).
# Env: OUT (default ${BOTOUT:-build_tests/bot}/speedrun/<stage>), MAXSECS (900), BUILD, OPTRUNS (max optimiser
# restarts, 40), NOBASE=1 skips step 3. Prints: base line, optimised line (level.sh format), idle lines.
HERE=$(cd "$(dirname "$0")" && pwd)
ST=$1
SEED=${2:-1}
OUT=${OUT:-${BOTOUT:-$HERE/../../build_tests/bot}/speedrun/$ST}
mkdir -p "$OUT"
rm -f "$OUT/opt.state" "$OUT"/opt*.log
n=0
while [ $n -lt "${OPTRUNS:-40}" ]; do
    n=$((n + 1))
    OUT="$OUT" TAG=opt$n SEED=$SEED REPLAY=0 NOSHOTS=1 BOTMODE=speedrun \
        EXTRA="GE_PLAYERBOT_OPT=1 GE_PLAYERBOT_OPTSTATE=$OUT/opt.state $EXTRA" sh "$HERE/level.sh" "$ST" 0 > "$OUT/opt$n.row"
    rm -f "$OUT/opt$n.rec"
    grep -aq "exit 3 to be started again" "$OUT/opt$n.log" || break
done
echo "optimiser: $n run(s), $(grep -c '^try' "$OUT/opt.state" 2>/dev/null) trials, $(grep -c '^fix' "$OUT/opt.state" 2>/dev/null) steps fixed"
best=$(OUT="$OUT" TAG=best SEED=$SEED NOSHOTS=1 BOTMODE=speedrun EXTRA="GE_PLAYERBOT_OPTSTATE=$OUT/opt.state $EXTRA" \
       sh "$HERE/level.sh" "$ST" 0)
rc=$?
if [ "${NOBASE:-0}" != 1 ]; then
    base=$(OUT="$OUT" TAG=base SEED=$SEED NOSHOTS=1 BOTMODE=speedrun sh "$HERE/level.sh" "$ST" 0)
    echo "base:      $base"
    grep -a "playerbot: idle:" "$OUT/base.log" | tail -1
fi
echo "optimised: $best"
grep -a "playerbot: idle:" "$OUT/best.log" | tail -1
printf '%s\n' "$best" > "$OUT/best.row"
exit $rc

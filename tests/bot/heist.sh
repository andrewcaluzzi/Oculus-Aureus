#!/bin/sh
# The Bank Heist played by the player bot, over several seeds: each run is recorded and replayed without the bot (level.sh).
#   sh tests/bot/heist.sh [mode] [first_seed] [count] [parallel]      (inside the build container, Xvfb on :99)
# mode: heist_bot (the vault job, default) or heist_bot_cells (the deposit cells). DIFF (default 0), MAXSECS (default 900),
# REPLAY (default 1) and EXTRA (more KEY=VALUE env) pass through. Output: build_tests/bot/heist/<mode>.s<seed>/ and one line per
# seed: seed, the bot's result line fields, then the heist's own verdict (WIN/FAIL, score, alarm) - and a summary.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MODE=${1:-heist_bot}
FIRST=${2:-1}
COUNT=${3:-10}
PAR=${4:-4}
DIFF=${DIFF:-0}
OUTROOT=$ROOT/build_tests/bot/heist
NAME=$MODE.d$DIFF${RUNTAG:+.$RUNTAG}       # RUNTAG: keep parallel batches (A/B settings) apart
mkdir -p "$OUTROOT"
SUMMARY=$OUTROOT/$NAME.summary.txt
: > "$SUMMARY"

one() {
    s=$1
    out=$OUTROOT/$NAME.s$s
    line=$(OUT=$out SEED=$s MAXSECS=${MAXSECS:-900} EXTRA="GE_MODE=$MODE GE_PLAYERBOT_LOG=${LOG:-2} $EXTRA" \
           sh "$ROOT/tests/bot/level.sh" bunker2 "$DIFF")
    verdict=$(grep -a "ge: heist: \(WIN\|FAIL\)" "$out/run.log" | tail -1 | sed 's/.*ge: heist: //')
    alarm=$(grep -a "ge: heist: ALARM" "$out/run.log" | head -1 | sed 's/.*at \([0-9]*\) s.*/\1/')
    shots=$(grep -a "ge: playerbot: RESULT" "$out/run.log" | tail -1 | sed -n 's/.*\(shots=[0-9]* hits=[0-9/]*\).*/\1/p')
    printf 'seed %s\t%s\t%s\talarm_at=%s\t%s\n' "$s" "$line" "${shots:--}" "${alarm:--}" "${verdict:--}" >> "$SUMMARY"
}

s=$FIRST
last=$((FIRST + COUNT - 1))
while [ $s -le $last ]; do
    n=0
    while [ $n -lt "$PAR" ] && [ $s -le $last ]; do
        one $s &
        s=$((s + 1))
        n=$((n + 1))
    done
    wait
done
sort -t' ' -k2 -n "$SUMMARY"
wins=$(grep -c "WIN:" "$SUMMARY")
quiet=$(grep "WIN:" "$SUMMARY" | grep -c "alarm no")
same=$(grep -c "	same	" "$SUMMARY")
echo "$MODE diff $DIFF: $wins/$COUNT wins ($quiet without the alarm), replays same: $same/$COUNT"

#!/bin/sh
# Replay a level.sh recording without the bot and print its RESULT line (and the DRIFT count): a check of whether a
# drift comes from the bot's run or from the replay itself (replay the same recording twice and compare).
#   sh tests/bot/replay.sh <stage> <recording> [seed] [KEY=VALUE ...]    (inside the build container, Xvfb on :99)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ST=$1
REC=$2
SEED=${3:-1}
shift 3 2>/dev/null
LOG=$(mktemp)
DIR=$(mktemp -d)
( cd "$DIR" && env DISPLAY=${DISPLAY:-:99} GE_NOINISAVE=1 GE_TICKHZ=60 GE_STAGE=$ST GE_DIFF=${DIFF:-0} GE_SEED=$SEED \
    GE_NOSLEEP=1 GE_FIXEDTICK=1 GE_CLOCKSCALE=${SCALE:-20} GE_FRAMES=2000000000 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_PLAYERBOT_PLANDIR=$ROOT/tools/bot/plans GE_PLAYERBOT_MAXSECS=${MAXSECS:-900} \
    GE_SIMONLY_TEST=${REPLAYSIM:-2,59,60} GE_INPUTPLAY="$REC" GE_PLAYERBOT=watch "$@" "$ROOT/${BUILD:-build_pc}/ProjectMidas" ) \
    > "$LOG" 2>&1
grep -a "ge: playerbot: RESULT" "$LOG" | tail -1
echo "drift lines: $(grep -ac 'inputrec: DRIFT' "$LOG")"
grep -a "inputrec: DRIFT" "$LOG" | head -2
rm -rf "$LOG" "$DIR"

#!/bin/sh
# Developer loop: one fast headless player-bot run of a stage, then the bot's log lines.
#   sh tests/bot/dev.sh <stage> [maxsecs] [KEY=VALUE ...]
# Log: build_tests/bot/dev.<stage>.log; screenshots (one rendered tick in 60, captured at events) in
# build_tests/bot/dev.<stage>.shots/. Plans come from tools/bot/plans (no rebuild needed).
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ST=$1; shift
MAX=${1:-600}; [ $# -gt 0 ] && shift
OUT="${BOTOUT:-$ROOT/build_tests/bot}"
mkdir -p "$OUT/dev.$ST.shots"
rm -f "$OUT/dev.$ST.shots"/*.bmp "$OUT/dev.$ST.shots"/*.png
LOG="$OUT/dev.$ST.log"
cd "$OUT/dev.$ST.shots" || exit 2
env DISPLAY=${DISPLAY:-:99} GE_NOINISAVE=1 GE_TICKHZ=60 GE_STAGE="$ST" GE_DIFF=${DIFF:-0} \
    GE_SEED=${SEED:-1} GE_PLAYERBOT=1 GE_PLAYERBOT_PLANDIR="$ROOT/tools/bot/plans" GE_PLAYERBOT_MAXSECS="$MAX" \
    GE_NOSLEEP=1 GE_FIXEDTICK=1 GE_CLOCKSCALE=${SCALE:-20} GE_SIMONLY_TEST=${SIMONLY:-2,59,60} GE_FRAMES=2000000000 \
    GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_WINDOW=640x480 \
    "$@" "$ROOT/${BUILD:-build_pc}/ProjectMidas" > "$LOG" 2>&1
rc=$?
grep -a "playerbot" "$LOG" | grep -av "playerbot: t=\|playerbot: trace\|playerbot: probe\|playerbot: fight" | tail -${TAILN:-40}
echo "rc=$rc"

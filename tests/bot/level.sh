#!/bin/sh
# Player-bot level test: play one stage headless with GE_PLAYERBOT=1 while recording the input (GE_INPUTREC), then
# replay the recording WITHOUT the bot (GE_INPUTPLAY, the bot only watches) and check the replay ends the same way.
#
#   sh tests/bot/level.sh <stage> [difficulty 0-3]          (inside the build container, Xvfb on :99)
#
# Environment knobs:
#   MAXSECS  bot timeout in mission seconds (default 900)     REPLAY  1 = verify by replay (default 1)
#   SEED     GE_SEED (default 1)                              SCALE   GE_CLOCKSCALE (default 20)
#   OUT      output dir (default build_tests/bot/<stage>.d<diff>)  BUILD build dir (default build_pc)
#   TAG      run tag in file names (default "run")           EXTRA   extra KEY=VALUE env for both runs
#   BOTMODE  GE_PLAYERBOT of the record run (default 1; `speedrun` = routes + the speedrun movement)
# Writes $OUT/$TAG.log, $OUT/$TAG.rec, $OUT/$TAG.replay.log, screenshots in $OUT/$TAG.shots/ and one result line
# (tab separated) to stdout:  stage diff outcome mission_s ticks health_lost kills objectives replay blocked
# Exit: 0 = completed and (if checked) the replay matched; 1 = failed; 3 = completed but the replay differed.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ST=$1
DIFF=${2:-0}
MAXSECS=${MAXSECS:-900}
REPLAY=${REPLAY:-1}
SEED=${SEED:-1}
SCALE=${SCALE:-20}
TAG=${TAG:-run}
OUT=${OUT:-${BOTOUT:-$ROOT/build_tests/bot}/$ST.d$DIFF}   # BOTOUT: root for run output (e.g. a scratch disk)
BIN="$ROOT/${BUILD:-build_pc}/ProjectMidas"
mkdir -p "$OUT/$TAG.shots"
rm -f "$OUT/$TAG.shots"/*.bmp "$OUT/$TAG.shots"/*.png "$OUT/$TAG.rec"

common() {
    echo DISPLAY=${DISPLAY:-:99} GE_NOINISAVE=1 GE_TICKHZ=60 GE_STAGE=$ST GE_DIFF=$DIFF GE_SEED=$SEED \
        GE_NOSLEEP=1 GE_FIXEDTICK=1 GE_CLOCKSCALE=$SCALE GE_FRAMES=2000000000 \
        GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_PLAYERBOT_PLANDIR=$ROOT/tools/bot/plans GE_PLAYERBOT_ROUTEDIR=$ROOT/tools/bot/routes \
        GE_PLAYERBOT_MAXSECS=$MAXSECS $EXTRA
}

start=$(date +%s)
# BOTMODE: the record run's GE_PLAYERBOT (1 = the normal bot, speedrun = the speedrun mode)
( cd "$OUT/$TAG.shots" && env $(common) GE_SIMONLY_TEST=2,59,60 GE_PLAYERBOT=${BOTMODE:-1} GE_INPUTREC="$OUT/$TAG.rec" "$BIN" ) \
    > "$OUT/$TAG.log" 2>&1
rc=$?
wall=$(( $(date +%s) - start ))
res=$(grep -a "ge: playerbot: RESULT" "$OUT/$TAG.log" | tail -1)
field() { echo "$res" | tr ' ' '\n' | grep "^$1=" | head -1 | cut -d= -f2-; }
outcome=$(field outcome)
[ -z "$outcome" ] && outcome="CRASH(rc=$rc)"
blocked=$(echo "$res" | sed -n 's/.*blocked=//p')
[ -z "$blocked" ] && blocked=$(grep -a "STUCK\|Bond died" "$OUT/$TAG.log" | tail -1 | sed 's/.*playerbot: //')

rep="-"
if [ "$REPLAY" = 1 ] && [ -s "$OUT/$TAG.rec" ]; then
    # replay drawing the same ticks as the record run (1 in 60). The photo objective is not render-free: it reads the
    # object's render matrices (objective_status.c, objGetOnscreenRenderBounds), so a replay that never draws can miss a
    # picture the record run took (bunker1 / silo drifted that way). REPLAYSIM=2,100000000 = the strict no-draw replay.
    ( cd "$OUT/$TAG.shots" && env $(common) GE_SIMONLY_TEST=${REPLAYSIM:-2,59,60} GE_INPUTPLAY="$OUT/$TAG.rec" GE_PLAYERBOT=watch "$BIN" ) \
        > "$OUT/$TAG.replay.log" 2>&1
    rres=$(grep -a "ge: playerbot: RESULT" "$OUT/$TAG.replay.log" | tail -1)
    a=$(echo "$res" | sed 's/.*outcome=\([^ ]*\).*mission_time=\([^ ]*\).*objectives=\([^ ]*\).*/\1 \2 \3/')
    b=$(echo "$rres" | sed 's/.*outcome=\([^ ]*\).*mission_time=\([^ ]*\).*objectives=\([^ ]*\).*/\1 \2 \3/')
    drift=$(grep -ac "inputrec: DRIFT" "$OUT/$TAG.replay.log")
    if [ -n "$rres" ] && [ "$a" = "$b" ] && [ "$drift" = 0 ]; then
        rep="same"
    else
        rep="DIFF(drift=$drift;replay=$(echo "$b" | tr ' ' '/'))"
    fi
fi

# NOSHOTS=1: drop the screenshots (a BMP every 30 s of mission time, ~1 MB each: batches filled the disk once)
[ "${NOSHOTS:-0}" = 1 ] && rm -f "$OUT/$TAG.shots"/*.bmp

printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%ss\t%s\n' "$ST" "$DIFF" "$outcome" "$(field mission_time)" "$(field ticks)" \
    "$(field health_lost)" "$(field kills)" "$(field objectives)" "$rep" "$wall" "${blocked:--}"
case "$outcome" in
    COMPLETE) case "$rep" in same|-) exit 0 ;; *) exit 3 ;; esac ;;
    *) exit 1 ;;
esac

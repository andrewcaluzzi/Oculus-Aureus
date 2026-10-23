#!/bin/sh
# Solo game-mode hooks, run twice (an OLD exe and the NEW one) and compared: the modes-in-co-op job must leave solo modes unchanged.
#   sh solo_modes.sh <exe> <tag>        run the hooks, logs to $OUT/<hook>.<tag>.log
#   sh solo_modes.sh compare <tagA> <tagB>   diff the normalised mode lines of the two runs (the log format gained "by player N")
# Environment: OUT (default build_tests/solochk), PROFILE (default solochk), DISPLAY etc. for a Linux exe; for a Windows exe run it from Git Bash
# (a window opens for a moment; SDL_AUDIODRIVER=dummy keeps it silent). The hooks are the ones documented in modding/lua.md.
OUT=${OUT:-/c/workspace/project-midas/build_tests/solochk}
PROFILE=${PROFILE:-solochk}
EVT=${EVT:-$(pwd)/tests/lua/events_test.lua}
mkdir -p "$OUT"

norm() {   # the mode lines of a log, without the log-format additions of the modes-in-co-op job
    grep -a '^ge: \(hostage\|mode\|event\): \|^ge: lua: EVT ' "$1" | grep -av '^ge: lua: ge: hostage:' \
        | sed -e 's/, by player [0-9]//' -e 's/ (leader)//' -e 's/use press by player [0-9]/use press/' -e 's/^ge: hostage: use press: no hostage in reach.*/ge: hostage: use press: no hostage in reach/' \
              -e "s/ (hostage state [-0-9]*)//" -e "s/EVT bond_died -1 0/EVT bond_died -1/"
}

if [ "$1" = compare ]; then
    bad=0
    for h in hostage hostagenone hostagefire blocks bonddie joy script events; do
        norm "$OUT/$h.$2.log" > "$OUT/$h.$2.norm"
        norm "$OUT/$h.$3.log" > "$OUT/$h.$3.norm"
        n=$(wc -l < "$OUT/$h.$2.norm")
        if cmp -s "$OUT/$h.$2.norm" "$OUT/$h.$3.norm" && [ "$n" -gt 3 ]; then
            echo "PASS solo $h: $n mode lines identical between $2 and $3"
        else
            echo "FAIL solo $h: $n lines, differs"
            diff "$OUT/$h.$2.norm" "$OUT/$h.$3.norm" | head -n 8
            bad=$((bad + 1))
        fi
    done
    echo "solo_modes: $bad failed"
    exit $bad
fi

EXE=$1
TAG=$2
run() {   # <hook> KEY=VALUE ...
    h=$1; shift
    env GE_PROFILE=$PROFILE GE_NOINISAVE=1 GE_SKIPINTRO=1 GE_TICKHZ=60 GE_STAGE=dam GE_FIXEDTICK=1 GE_SEED=7 SDL_AUDIODRIVER=dummy "$@" timeout -s KILL 300 "$EXE" > "$OUT/$h.$TAG.log" 2>&1
}
run hostage GE_MODE=reset GE_MODE_TEST=hostage GE_FRAMES=1700
run hostagenone GE_MODE=reset GE_MODE_TEST=hostagenone GE_FRAMES=300
run hostagefire GE_MODE=reset GE_MODE_TEST=hostagefire GE_ENEMYFF_LOG=1 GE_FRAMES=700
run blocks GE_MODE=reset GE_MODE_TEST=blocks GE_FRAMES=700
run bonddie GE_MODE=reset GE_MODE_TEST=bonddie GE_FRAMES=500
run joy GE_MODE=reset GE_MODE_TEST=hostage GE_HOSTAGE_TEST_USE=joy GE_JOYPORT_AUTO=use:450,use:700 GE_FRAMES=1000
run script GE_MODE=hostage_old GE_HOSTAGE_TEST_KILL=2 GE_FRAMES=1000
run events GE_DIFF=1 GE_CHEATS=guns GE_EVENT_TEST=1 GE_LUA_TEST="$EVT" GE_FRAMES=900
echo "solo_modes: ran the hooks with $EXE as $TAG"

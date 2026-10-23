#!/bin/sh
# Rollback R1, solo / demo path: sim-only stretches (GE_SIMONLY_TEST=<start>,<n>[,<every>]) must leave the per-tick demo
# fingerprint (`ge: demo tick` lines: RNG draw counts, pad input, Bond position) bit-identical to a normal run.
#
# Windows (Git Bash, silent): SDL_AUDIODRIVER=dummy CHECKBASE=1 BIN=/c/.../build_win_dev/ProjectMidas.exe sh tests/simonly_solo.sh
# Linux container:            docker exec --user 1000 -e DISPLAY=:99 midas-r1 sh /home/dev/project/tests/simonly_solo.sh
#   BIN        the game (default build_pc/ProjectMidas)
#   OUT        output directory (default build_tests/r1)
#   DEMOS      demos to run (default "dam1 facility1"; baselines exist in tests/baseline/<demo>.a.txt)
#   SPECS      the GE_SIMONLY_TEST values (default: one stretch, a short stretch repeating, every tick, single ticks alternating, the tail)
#   CHECKBASE  1 = also require the NORMAL run to equal tests/baseline/<demo>.a.txt (the oracle: only valid on the Windows build the
#              baselines were taken with); default 0 = every run is compared with the normal run of the same binary
# Exit status = number of failures.
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
OUT=${OUT:-/home/dev/project/build_tests/r1}
HERE=$(dirname "$0")
BASE=${BASE:-$HERE/baseline}
DEMOS=${DEMOS:-"dam1 facility1"}
SPECS=${SPECS:-"100,60 5,3,40 1,100000 200,1,2 400,300"}
CHECKBASE=${CHECKBASE:-0}
FAILS=0
mkdir -p "$OUT"

run() {   # <demo> <spec or empty> <outfile>
    if [ -n "$2" ]; then
        env GE_DEMO="$1" GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 GE_SIMONLY_TEST="$2" GE_SIMONLY_STATS=1 "$BIN" > "$3.raw" 2>&1
    else
        env GE_DEMO="$1" GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 "$BIN" > "$3.raw" 2>&1
    fi
    grep -a '^ge: demo tick' "$3.raw" | tr -d '\r' > "$3"
}

for demo in $DEMOS; do
    run "$demo" "" "$OUT/solo.$demo.plain"
    n=$(wc -l < "$OUT/solo.$demo.plain")
    if [ "$n" -lt 300 ]; then echo "FAIL solo $demo: normal run only produced $n tick lines"; FAILS=$((FAILS + 1)); continue; fi
    if [ "$CHECKBASE" = 1 ]; then
        if cmp -s "$OUT/solo.$demo.plain" "$BASE/$demo.a.txt"; then echo "PASS solo $demo: normal run == oracle baseline ($n lines)"; else echo "FAIL solo $demo: normal run differs from the baseline"; FAILS=$((FAILS + 1)); fi
    fi
    for spec in $SPECS; do
        tag=$(echo "$spec" | tr ',' '_')
        run "$demo" "$spec" "$OUT/solo.$demo.so$tag"
        ran=$(grep -ac 'simonly: stretch starts' "$OUT/solo.$demo.so$tag.raw")
        if cmp -s "$OUT/solo.$demo.plain" "$OUT/solo.$demo.so$tag"; then same=1; else same=0; fi
        if [ "$same" = 1 ] && [ "$ran" -ge 1 ]; then
            echo "PASS solo $demo GE_SIMONLY_TEST=$spec: $n tick lines identical to the normal run (sim-only stretches logged: $ran)"
        else
            echo "FAIL solo $demo GE_SIMONLY_TEST=$spec: identical=$same, sim-only stretches logged=$ran"
            diff "$OUT/solo.$demo.plain" "$OUT/solo.$demo.so$tag" | head -n 6
            FAILS=$((FAILS + 1))
        fi
    done
done
echo "simonly_solo: $FAILS failed"
exit $FAILS

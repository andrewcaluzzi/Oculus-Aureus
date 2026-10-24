#!/bin/sh
# dam1 determinism oracle (bot off): the attract demo's per-tick fingerprints must
# match tests/baseline/dam1.a.txt exactly. Run inside the build container with
# Xvfb on :99.  Usage: tests/bot/dam1_check.sh [builddir]   (default build_pc)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=${1:-build_pc}
OUT="$ROOT/$B/dam1.check.txt"
cd "$ROOT/$B" || exit 2
DISPLAY=${DISPLAY:-:99} GE_DEMO=dam1 GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 \
    ./ProjectMidas 2>&1 | grep '^ge: demo tick' > "$OUT"
n=$(wc -l < "$OUT")
if cmp -s "$OUT" "$ROOT/tests/baseline/dam1.a.txt"; then
    echo "dam1: IDENTICAL ($n lines)"
    exit 0
fi
echo "dam1: DIFFERS ($n lines, baseline $(wc -l < "$ROOT/tests/baseline/dam1.a.txt"))"
exit 1

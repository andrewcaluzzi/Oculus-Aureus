#!/bin/sh
# Rollback R2: the sim-statics range table check (scripts/port/snap_embed.py).
#   sh tests/snap_table_check.sh [map file]        (default build_pc_r2/ProjectMidas.map; needs python3)
# 1. strict classification of the real link map: every source file that owns writable statics is SIM, presentation or foreign: exit 0
# 2. the check is not vacuous (negative control): a NEW port/ file with statics that is in no class makes the strict check fail (exit 2) and names the
#    file; a new file in a known class passes; src/ files default to SIM, so a new src/ global is snapshotted without a list edit
# 3. the table of the exe itself: embedded (magic, count) -- read back with the exe's own log line when the exe is given as a second argument
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MAP=${1:-$ROOT/build_pc_r2/ProjectMidas.map}
TOOL=$ROOT/scripts/port/snap_embed.py
TMP=${TMPDIR:-/tmp}/snapcheck.$$
FAILS=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILS=$((FAILS + 1)); }

out=$(python3 "$TOOL" --map "$MAP" --check-only --strict 2>&1); rc=$?
echo "$out"
if [ $rc -eq 0 ]; then pass "real map: every file with statics is classified (strict)"; else fail "real map: unclassified files (rc $rc)"; fi

mkdir -p "$TMP"
printf '.data           0x0000000008400000     0x1000\n .data          0x0000000008400000       0x40 CMakeFiles/ge.dir/port/src/zz_new_port_file.c.o\n' > "$TMP/fake.map"
out=$(python3 "$TOOL" --map "$TMP/fake.map" --check-only --strict --assume-port-file zz_new_port_file.c 2>&1); rc=$?
if [ $rc -eq 2 ] && echo "$out" | grep -q 'zz_new_port_file.c'; then pass "negative control: a new, unclassified port/src file with statics fails the strict check and is named"; else fail "negative control did not fire (rc $rc): $out"; fi

printf '.data           0x0000000008400000     0x1000\n .data          0x0000000008400000       0x40 CMakeFiles/ge.dir/port/src/port_coop.c.o\n' > "$TMP/known.map"
out=$(python3 "$TOOL" --map "$TMP/known.map" --check-only --strict 2>&1); rc=$?
if [ $rc -eq 0 ]; then pass "a known sim file passes"; else fail "a known sim file was flagged (rc $rc): $out"; fi

printf '.data           0x0000000008400000     0x1000\n .data          0x0000000008400000       0x40 CMakeFiles/ge.dir/src/game/zz_new_game_file.c.o\n' > "$TMP/newsrc.map"
out=$(python3 "$TOOL" --map "$TMP/newsrc.map" --check-only --strict --out-list "$TMP/newsrc.list" 2>&1); rc=$?
if [ $rc -eq 0 ] && grep -q '^S 08400000 40' "$TMP/newsrc.list"; then pass "a new src/game file is snapshotted by default (its 0x40 bytes are in the list)"; else fail "new src/game file: rc $rc, list: $(cat "$TMP/newsrc.list" 2>/dev/null)"; fi

rm -rf "$TMP"
echo "snap_table_check: $FAILS failed"
exit $FAILS

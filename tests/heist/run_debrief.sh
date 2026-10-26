#!/bin/sh
# Heist DEBRIEF tests (designs/bank-heist.md section 26), inside the build container with Xvfb on :99 and build_pc built.
# Two runs of tests/heist/heist_debrief_test.lua on the real mode, in order, on a fresh records file (build_pc/Data/Saves/heist_records.json
# is deleted first, then checked after each run):
#   quiet   a quiet job (pickpocket, the console, the crack, the van): a high rating, the quiet commendations, NEW BEST; one screenshot
#           of the panel -> $SHOTS/debrief.png
#   loud    the alarm up from the start and a civilian killed: a lower rating, the alarm counted, the run count up, no NEW BEST
# Each run's "HEIST DEBRIEF {json}" line must parse (python3 json) and carry the expected fields; the records file and the "HEIST RECORD
# {json}" line must count the run (NEW BEST on the quiet run only).
#   det     presentation only: the determinism heist with and without the debrief, hash-identical (below)
# usage: [WHICH=all|runs|det] run_debrief.sh   -> "HEIST ..." lines; last line "HEIST DEBRIEF DONE <n> passed, <m> failed"
OUT=${OUT:-/tmp/heist_debrief}
SHOTS=${SHOTS:-$OUT/shots}
mkdir -p $OUT $SHOTS
B=/home/dev/project/build_pc
cp /home/dev/project/tests/heist/heist_debrief_test.lua $B/Data/Scripts/modes/heist_debrief_test.lua
cp /home/dev/project/tools/mode_heist.lua $B/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_debrief.lua $B/Data/Scripts/modes/heist_debrief.lua
cp /home/dev/project/tools/heist_gadgets.lua $B/Data/Scripts/modes/heist_gadgets.lua
cp /home/dev/project/tools/heist_lib.lua $B/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua $B/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua $B/Data/Scripts/modes/levelgen_layout.lua
REC=$B/Data/Saves/heist_records.json
rm -f $REC
PASS=0
FAIL=0
for run in $([ "${WHICH:-all}" = det ] || echo quiet loud); do
    # the loud run also takes the input as a player's game would (GE_DEBRIEF_CAPTURE=1) and presses INTEL every 300 polls: the first
    # press 0.75 s or more after the panel opens must dismiss it ("ge: debrief: dismissed")
    AUTO=$(seq 1200 300 5700 | sed 's/^/intel:/' | paste -sd, -)
    if [ $run = loud ]; then X="GE_DEBRIEF_LOUD=1 GE_DEBRIEF_CAPTURE=1 GE_JOYPORT_AUTO=$AUTO"; else X="GE_DEBRIEF_QUIET=1"; fi
    rm -rf $OUT/shotdir
    mkdir -p $OUT/shotdir
    echo "HEIST RUN $run"
    (cd $OUT/shotdir && env $X DISPLAY=:99 GE_MODE=heist_debrief_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=${GE_SEED:-7} \
        GE_FIXEDTICK=1 GE_TICKHZ=60 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=${GE_FRAMES:-5500} timeout ${TIMEOUT:-500} $B/ProjectMidas > $OUT/$run.log 2>&1)
    grep -a "HEIST\|ge: lua: .*error\|ge: debrief\|ge: heist: debrief" $OUT/$run.log | grep -av "HEIST DEBRIEF {"
    p=$(grep -a "HEIST DONE" $OUT/$run.log | tail -n 1 | sed 's/.*DONE \([0-9]*\) passed, \([0-9]*\) failed.*/\1 \2/')
    if [ -n "$p" ]; then PASS=$((PASS + ${p% *})); FAIL=$((FAIL + ${p#* })); else FAIL=$((FAIL + 1)); echo "HEIST FAIL $run: no DONE line"; fi
    # the machine-readable line
    grep -a "HEIST DEBRIEF {" $OUT/$run.log | tail -n 1 | sed 's/.*HEIST DEBRIEF //' > $OUT/$run.json
    if python3 - "$OUT/$run.json" "$run" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
run = sys.argv[2]
need = ["result", "rating", "payout", "time", "alarm", "alarms", "spotted", "bodies_found", "guards_ko", "guards_killed", "civ_killed",
        "gadgets", "route", "keys", "commendations", "difficulty"]
missing = [k for k in need if k not in d]
assert not missing, "missing %s" % missing
if run == "quiet":
    assert d["rating"] in ("GHOST", "PROFESSIONAL") and d["alarms"] == 0 and "Never Seen" in d["commendations"], d["rating"]
else:
    assert d["rating"] in ("COWBOY", "CHAOS") and d["alarms"] == 1 and d["civ_killed"] == 1, d["rating"]
print("HEIST PASS log line (%s): parses: %s %s, payout %d, %d commendations" % (run, d["result"], d["rating"], d["payout"], len(d["commendations"])))
EOF
    then PASS=$((PASS + 1)); else FAIL=$((FAIL + 1)); echo "HEIST FAIL log line ($run): $(head -c 300 $OUT/$run.json)"; fi
    # the records file after the run, and its HEIST RECORD line (NEW BEST on the fresh file's quiet run, not on the loud one)
    grep -a "HEIST RECORD {" $OUT/$run.log | tail -n 1 | sed 's/.*HEIST RECORD //' > $OUT/$run.rec.json
    if python3 - "$REC" "$run" "$OUT/$run.rec.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))["difficulty"]["0"]
r = json.load(open(sys.argv[3]))
want = 1 if sys.argv[2] == "quiet" else 2
assert d["runs"] == want and d["wins"] == want and d["best_payout"] > 0, d
assert r["new_best"] == (sys.argv[2] == "quiet") and r["runs"] == want and r["saved"], r
print("HEIST PASS records file (%s): runs %d, wins %d, best %d (%s); RECORD line new_best %s" % (sys.argv[2], d["runs"], d["wins"],
      d["best_payout"], d.get("best_rating"), r["new_best"]))
EOF
    then PASS=$((PASS + 1)); else FAIL=$((FAIL + 1)); echo "HEIST FAIL records file ($run): $(head -c 300 $REC 2>&1)"; fi
    if [ $run = loud ]; then
        if grep -aq "ge: debrief: dismissed" $OUT/$run.log; then echo "HEIST PASS dismiss: an INTEL press closed the panel (input taken)"; PASS=$((PASS + 1))
        else echo "HEIST FAIL dismiss: no 'ge: debrief: dismissed' after the INTEL presses"; FAIL=$((FAIL + 1)); fi
    fi
    if [ $run = quiet ]; then
        f=$(ls $OUT/shotdir/shot_*.bmp 2>/dev/null | tail -n 1)
        if [ -n "$f" ]; then python3 /home/dev/project/tests/bmp2png.py $f $SHOTS/debrief.png; echo "HEIST shot: $SHOTS/debrief.png"; fi
        rm -f $OUT/shotdir/shot_*.bmp
    fi
done
# presentation only: the scripted determinism heist (tests/heist/heist_determinism.lua) with the debrief left out (S.debrief = false)
# against the same scenario with it in: the per-tick state hashes must be identical (the debrief's handlers only read). RUNS=quiet,loud
# skips it: WHICH=det runs only it.
if [ "${WHICH:-all}" != runs ]; then
    FRAMES=${DET_FRAMES:-4500}
    cp /home/dev/project/tests/heist/heist_determinism.lua $B/Data/Scripts/modes/heist_determinism.lua
    sed 's/^HEIST_SETTINGS = {/HEIST_SETTINGS = { debrief = false,/' /home/dev/project/tests/heist/heist_determinism.lua > $B/Data/Scripts/modes/heist_det_nodebrief.lua
    for m in heist_determinism heist_det_nodebrief; do
        (cd $B && env DISPLAY=:99 GE_MODE=$m GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60             GE_FRAMES=$FRAMES GE_HASHLOG=$OUT/det.$m.hash timeout 600 ./ProjectMidas > $OUT/det.$m.log 2>&1)
    done
    grep '^tick' $OUT/det.heist_determinism.hash > $OUT/cmp.a
    grep '^tick' $OUT/det.heist_det_nodebrief.hash > $OUT/cmp.b
    na=$(wc -l < $OUT/cmp.a); nb=$(wc -l < $OUT/cmp.b); n=$na; [ "$nb" -lt "$n" ] && n=$nb
    head -n $n $OUT/cmp.a > $OUT/cmp.a2; head -n $n $OUT/cmp.b > $OUT/cmp.b2
    if grep -q "debrief = false" $B/Data/Scripts/modes/heist_det_nodebrief.lua && cmp -s $OUT/cmp.a2 $OUT/cmp.b2 && [ "$n" -ge 3000 ]; then
        echo "HEIST PASS det: the debrief in vs left out: $n ticks hash-identical"; PASS=$((PASS + 1))
    else
        first=$(diff $OUT/cmp.a2 $OUT/cmp.b2 | grep '^<' | head -n 1 | cut -c1-200)
        echo "HEIST FAIL det: $n ticks (need 3000), first difference: ${first:-none}"; FAIL=$((FAIL + 1))
    fi
fi
echo "HEIST DEBRIEF DONE $PASS passed, $FAIL failed"

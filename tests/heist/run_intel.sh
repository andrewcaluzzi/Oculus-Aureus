#!/bin/sh
# Heist INTEL board tests (designs/bank-heist.md section 19), inside the build container with Xvfb on :99 and build_pc built.
#   content   tests/heist/heist_intel_test.lua: the board's content builds for bank v2 (pages, floors, every keyed door on a key card), the
#             plan ticks off, the hints fire and are rate-limited; one screenshot per page -> $SHOTS/intel_<n>.png
#   det       the board is presentation only: the scripted determinism heist (heist_determinism.lua) runs twice with the same seed, once
#             with the board opened and flipped through every page and floor (GE_INTEL_AUTO), once untouched; the per-tick state hashes
#             must be identical
# usage: run_intel.sh [content|det|all]   -> "HEIST ..." lines; last line "HEIST INTEL DONE <n> passed, <m> failed"
WHICH=${1:-all}
OUT=${OUT:-/tmp/heist_intel}
SHOTS=${SHOTS:-$OUT/shots}
mkdir -p $OUT $SHOTS
B=/home/dev/project/build_pc
for f in heist_intel_test.lua heist_determinism.lua; do cp /home/dev/project/tests/heist/$f $B/Data/Scripts/modes/$f; done
cp /home/dev/project/tools/mode_heist.lua $B/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_intel.lua $B/Data/Scripts/modes/heist_intel.lua
cp /home/dev/project/tools/heist_lib.lua $B/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua $B/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua $B/Data/Scripts/modes/levelgen_layout.lua
PASS=0
FAIL=0

if [ "$WHICH" = content ] || [ "$WHICH" = all ]; then
    rm -rf $OUT/shotdir
    mkdir -p $OUT/shotdir
    (cd $OUT/shotdir && env DISPLAY=:99 GE_MODE=heist_intel_test GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FULLHEIGHT=0 \
        GE_EDGEINSET=1 GE_FRAMES=${GE_FRAMES:-7000} timeout 500 $B/ProjectMidas > $OUT/content.log 2>&1)
    grep -a "HEIST\|ge: lua: .*error\|ge: intel" $OUT/content.log | grep -av "HEIST walk"
    p=$(grep -a "HEIST DONE" $OUT/content.log | tail -n 1 | sed 's/.*DONE \([0-9]*\) passed, \([0-9]*\) failed.*/\1 \2/')
    if [ -n "$p" ]; then PASS=$((PASS + ${p% *})); FAIL=$((FAIL + ${p#* })); else FAIL=$((FAIL + 1)); echo "HEIST FAIL content: no DONE line"; fi
    # the screenshots: shot_NNNNN.bmp in the order of the "HEIST SHOT" lines
    names=$(grep -a "HEIST SHOT" $OUT/content.log | sed 's/.*HEIST SHOT \([a-z0-9_]*\).*/\1/')
    i=0
    for f in $(ls $OUT/shotdir/shot_*.bmp 2>/dev/null); do
        i=$((i + 1))
        n=$(echo "$names" | sed -n "${i}p")
        python3 /home/dev/project/tests/bmp2png.py $f $SHOTS/${n:-shot_$i}.png && rm -f $f
    done
    echo "HEIST INTEL shots: $i -> $SHOTS"
fi

if [ "$WHICH" = det ] || [ "$WHICH" = all ]; then
    FRAMES=${DET_FRAMES:-4500}
    # open at frame 700 (the stage is up), every page and every floor of the map, closed, opened again, toggled with the key itself
    AUTO="700:open,800:page2,820:floor1,840:floor2,860:floor3,880:floor4,900:floor5,920:floor6,1000:page3,1100:page4,1200:page5,1300:close,1500:open,1500:page2,2600:close,3000:open,3000:page1"
    for r in plain board; do
        if [ $r = board ]; then X="GE_INTEL_AUTO=$AUTO GE_JOYPORT_AUTO=intel:3200,intel:3400"; else X="GE_INTEL_NONE=1"; fi
        (cd $B && env DISPLAY=:99 GE_MODE=heist_determinism GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 \
            GE_FRAMES=$FRAMES GE_HASHLOG=$OUT/det.$r.hash $X timeout 600 ./ProjectMidas > $OUT/det.$r.log 2>&1)
    done
    na=$(grep -c '^tick' $OUT/det.plain.hash)
    nb=$(grep -c '^tick' $OUT/det.board.hash)
    n=$na
    [ "$nb" -lt "$n" ] && n=$nb
    grep '^tick' $OUT/det.plain.hash | head -n $n > $OUT/cmp.a
    grep '^tick' $OUT/det.board.hash | head -n $n > $OUT/cmp.b
    opened=$(grep -ac "ge: intel: auto open" $OUT/det.board.log)
    pages=$(grep -ac "ge: intel: auto page" $OUT/det.board.log)
    keyed=$(grep -ac "ge: intel: board .* (the intel key)" $OUT/det.board.log)
    if cmp -s $OUT/cmp.a $OUT/cmp.b && [ "$n" -ge 3000 ] && [ "$opened" -ge 2 ]; then
        echo "HEIST PASS det: board opened ($opened opens, $pages page flips, $keyed toggles by the intel key) vs untouched: $n ticks hash-identical"
        PASS=$((PASS + 1))
    else
        first=$(diff $OUT/cmp.a $OUT/cmp.b | grep '^<' | head -n 1 | cut -c1-200)
        echo "HEIST FAIL det: $n ticks (need 3000), opens $opened, first difference: ${first:-none}"
        FAIL=$((FAIL + 1))
    fi
    if [ "$keyed" -ge 2 ]; then echo "HEIST PASS key: the intel action (GE_JOYPORT_AUTO) toggled the board $keyed times"; PASS=$((PASS + 1));
    else echo "HEIST FAIL key: the intel action toggled the board $keyed times (want 2)"; FAIL=$((FAIL + 1)); fi
fi

if [ "$WHICH" = nointel ] || [ "$WHICH" = all ]; then
    # the content builder and the hints change no sim state either: the same scenario with the board switched off (S.intel = false)
    # against the plain run above (run "det" first)
    FRAMES=${DET_FRAMES:-4500}
    sed 's/^HEIST_SETTINGS = {/HEIST_SETTINGS = { intel = false,/' /home/dev/project/tests/heist/heist_determinism.lua > $B/Data/Scripts/modes/heist_det_nointel.lua
    (cd $B && env DISPLAY=:99 GE_MODE=heist_det_nointel GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 \
        GE_FRAMES=$FRAMES GE_HASHLOG=$OUT/det.nointel.hash timeout 600 ./ProjectMidas > $OUT/det.nointel.log 2>&1)
    na=$(grep -c '^tick' $OUT/det.plain.hash)
    nb=$(grep -c '^tick' $OUT/det.nointel.hash)
    n=$na
    [ "$nb" -lt "$n" ] && n=$nb
    grep '^tick' $OUT/det.plain.hash | head -n $n > $OUT/cmp.a
    grep '^tick' $OUT/det.nointel.hash | head -n $n > $OUT/cmp.c
    if grep -q "intel = false" $B/Data/Scripts/modes/heist_det_nointel.lua && cmp -s $OUT/cmp.a $OUT/cmp.c && [ "$n" -ge 3000 ]; then
        echo "HEIST PASS nointel: no board at all vs the board's content and hints: $n ticks hash-identical"
        PASS=$((PASS + 1))
    else
        first=$(diff $OUT/cmp.a $OUT/cmp.c | grep '^<' | head -n 1 | cut -c1-200)
        echo "HEIST FAIL nointel: $n ticks (need 3000), first difference: ${first:-none}"
        FAIL=$((FAIL + 1))
    fi
fi
echo "HEIST INTEL DONE $PASS passed, $FAIL failed"

#!/bin/sh
# Crosshair styles (modding/lua.md "Crosshair styles"), inside the build container with Xvfb on :99 and build_pc built. Two parts:
#   api      tests/crosshair/xhair_test.lua as a mode on Dam: the crosshair.* API, the pack, the draw hook ("XHAIR PASS|FAIL" lines,
#            "XHAIR DONE n passed, m failed"); GE_XHAIR_SHOTS=1 adds one screenshot per pack style (build_pc/xhair_shots/shot_NNNNN.bmp;
#            tests/crosshair/sheet.py makes the contact sheet; delete the BMPs)
#   proof    presentation only: the scripted determinism heist (tests/heist/heist_determinism.lua) twice at GE_TICKHZ=60, once with no
#            crosshair style and once with the pack's "tactical" style (dynamic spread, hit marker, per-weapon overrides) plus a
#            draw_crosshair hook (tests/crosshair/xhair_hook.lua); the per-tick state hashes (GE_HASHLOG) must be identical over NEED ticks
# usage: run.sh [api|proof|all]  -> "XHAIR ..." lines; the last is "XHAIR RUN DONE <n> passed, <m> failed"
WHICH=${1:-all}
OUT=${OUT:-/tmp/xhair}
mkdir -p $OUT
P=/home/dev/project
cp $P/tests/crosshair/xhair_test.lua $P/build_pc/Data/Scripts/modes/xhair_test.lua
mkdir -p $P/build_pc/Data/Scripts/mods
cp $P/tools/crosshair_pack.lua $P/build_pc/Data/Scripts/mods/crosshair_pack.lua
PASS=0
FAIL=0
if [ "$WHICH" = api ] || [ "$WHICH" = all ]; then
    mkdir -p $P/build_pc/xhair_shots
    rm -f $P/build_pc/xhair_shots/shot_*.bmp
    ( cd $P/build_pc/xhair_shots && env DISPLAY=:99 GE_MODE=xhair_test GE_STAGE=dam GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 \
        GE_FIXEDTICK=1 GE_TICKHZ=60 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=${GE_FRAMES:-2600} timeout 300 ../ProjectMidas > $OUT/api.log 2>&1 )
    grep -aE "XHAIR|ge: crosshair|ge: lua: .*error" $OUT/api.log
    p=$(grep -ac "XHAIR PASS" $OUT/api.log)
    f=$(grep -ac "XHAIR FAIL" $OUT/api.log)
    if grep -aq "XHAIR DONE" $OUT/api.log && [ "$f" = 0 ] && ! grep -aq "ge: lua: .*error" $OUT/api.log; then
        echo "XHAIR RUN PASS api: $p checks"; PASS=$((PASS + 1))
    else
        echo "XHAIR RUN FAIL api: $f failed"; FAIL=$((FAIL + 1))
    fi
fi
if [ "$WHICH" = proof ] || [ "$WHICH" = all ]; then
    FRAMES=${GE_FRAMES_PROOF:-4500}
    NEED=${NEED:-3000}
    cp $P/tests/heist/heist_determinism.lua $P/build_pc/Data/Scripts/modes/heist_determinism.lua
    cp $P/tools/mode_heist.lua $P/build_pc/Data/Scripts/modes/heist.lua
    for f in heist_lib heist_bank_map heist_ar heist_ai heist_intel heist_guide heist_gadgets; do
        [ -f $P/tools/$f.lua ] && cp $P/tools/$f.lua $P/build_pc/Data/Scripts/modes/$f.lua
    done
    cp $P/port/levelgen/levelgen_layout.lua $P/build_pc/Data/Scripts/modes/levelgen_layout.lua
    cd $P/build_pc || exit 1
    for c in off on; do
        if [ $c = off ]; then x="GE_XHAIR_NONE=1"; else x="GE_CROSSHAIR=tactical GE_LUA_TEST=$P/tests/crosshair/xhair_hook.lua"; fi
        env $x DISPLAY=:99 GE_MODE=heist_determinism GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 \
            GE_FRAMES=$FRAMES GE_HASHLOG=$OUT/proof.$c.hash timeout 600 ./ProjectMidas > $OUT/proof.$c.log 2>&1
    done
    na=$(grep -c '^tick' $OUT/proof.off.hash)
    nb=$(grep -c '^tick' $OUT/proof.on.hash)
    n=$na
    [ "$nb" -lt "$n" ] && n=$nb
    grep '^tick' $OUT/proof.off.hash | head -n $n > $OUT/cmp.a
    grep '^tick' $OUT/proof.on.hash | head -n $n > $OUT/cmp.b
    if cmp -s $OUT/cmp.a $OUT/cmp.b && [ "$n" -ge "$NEED" ]; then
        echo "XHAIR RUN PASS proof: no style vs tactical + draw hook: $n ticks hash-identical"
        PASS=$((PASS + 1))
    else
        first=$(diff $OUT/cmp.a $OUT/cmp.b | grep '^<' | head -n 1 | cut -c1-200)
        echo "XHAIR RUN FAIL proof: $n ticks (need $NEED), first difference: ${first:-none}"
        FAIL=$((FAIL + 1))
    fi
    grep -aq "ge: crosshair: custom sight drawing" $OUT/proof.on.log || { echo "XHAIR RUN FAIL proof: the custom sight never drew in the 'on' run"; FAIL=$((FAIL + 1)); }
    grep -aq "XHAIR HOOK drew" $OUT/proof.on.log || { echo "XHAIR RUN FAIL proof: the draw hook never ran in the 'on' run"; FAIL=$((FAIL + 1)); }
    grep -aq "ge: lua: .*error" $OUT/proof.on.log && { echo "XHAIR RUN FAIL proof: a Lua error with the style on"; grep -a "ge: lua: .*error" $OUT/proof.on.log | head -n 3; FAIL=$((FAIL + 1)); }
fi
echo "XHAIR RUN DONE $PASS passed, $FAIL failed"

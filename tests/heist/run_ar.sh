#!/bin/sh
# The AR specs and the diagnostics layer (designs/bank-heist.md section 20) on the Janus bank, inside the build container with Xvfb on :99
# and build_pc built. Two parts:
#   api      tests/heist/heist_arhud.lua: the ar.* / diag.* API and the heist wiring ("HEIST PASS|FAIL" lines, "HEIST DONE n passed, m failed");
#            GE_AR_SHOTS=1 adds the showcase screenshots (build_pc/heist_ar_shots/shot_NNNNN.bmp: convert with tests/bmp2png.py, delete
#            the BMPs)
#   proof    presentation only: the scripted determinism heist (tests/heist/heist_determinism.lua) twice at GE_TICKHZ=60, once with the
#            AR wiring not even attached (GE_AR_OFF=1) and once with the specs, the diagnostics layer and the radar on (GE_DIAG=1); the
#            per-tick state hashes (GE_HASHLOG) must be identical over at least NEED ticks
# usage: run_ar.sh [api|proof|all]  -> "HEIST AR ..." lines; the last is "HEIST AR DONE <n> passed, <m> failed"
WHICH=${1:-all}
OUT=${OUT:-/tmp/heist_ar}
mkdir -p $OUT
for f in heist_arhud heist_determinism; do cp /home/dev/project/tests/heist/$f.lua /home/dev/project/build_pc/Data/Scripts/modes/$f.lua; done
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
for f in heist_lib heist_bank_map heist_ar; do cp /home/dev/project/tools/$f.lua /home/dev/project/build_pc/Data/Scripts/modes/$f.lua; done
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
PASS=0
FAIL=0
if [ "$WHICH" = api ] || [ "$WHICH" = all ]; then
    mkdir -p /home/dev/project/build_pc/heist_ar_shots
    rm -f /home/dev/project/build_pc/heist_ar_shots/shot_*.bmp
    ( cd /home/dev/project/build_pc/heist_ar_shots && env DISPLAY=:99 GE_MODE=heist_arhud GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 \
        GE_FIXEDTICK=1 GE_TICKHZ=60 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FRAMES=${GE_FRAMES:-4000} timeout 400 ../ProjectMidas > $OUT/api.log 2>&1 )
    grep -aE "HEIST|gfx: wrote|ge: lua: .*error" $OUT/api.log
    p=$(grep -ac "HEIST PASS" $OUT/api.log)
    f=$(grep -ac "HEIST FAIL" $OUT/api.log)
    if grep -aq "HEIST DONE" $OUT/api.log && [ "$f" = 0 ]; then echo "HEIST AR PASS api: $p checks"; PASS=$((PASS + 1)); else echo "HEIST AR FAIL api: $f failed"; FAIL=$((FAIL + 1)); fi
fi
if [ "$WHICH" = proof ] || [ "$WHICH" = all ]; then
    FRAMES=${GE_FRAMES:-4500}
    NEED=${NEED:-3000}
    cd /home/dev/project/build_pc || exit 1
    for c in off on; do
        if [ $c = off ]; then x=GE_AR_OFF=1; else x=GE_DIAG=1; fi
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
        echo "HEIST AR PASS proof: AR off vs AR + diag + radar on: $n ticks hash-identical"
        PASS=$((PASS + 1))
    else
        first=$(diff $OUT/cmp.a $OUT/cmp.b | grep '^<' | head -n 1 | cut -c1-200)
        echo "HEIST AR FAIL proof: $n ticks (need $NEED), first difference: ${first:-none}"
        FAIL=$((FAIL + 1))
    fi
    grep -aq "ge: arhud: diagnostics layer drawing" $OUT/proof.on.log || { echo "HEIST AR FAIL proof: the diagnostics layer never drew in the 'on' run"; FAIL=$((FAIL + 1)); }
    grep -aq "ge: lua: .*error" $OUT/proof.on.log && { echo "HEIST AR FAIL proof: a Lua error with AR on"; grep -a "ge: lua: .*error" $OUT/proof.on.log | head -n 3; FAIL=$((FAIL + 1)); }
fi
echo "HEIST AR DONE $PASS passed, $FAIL failed"

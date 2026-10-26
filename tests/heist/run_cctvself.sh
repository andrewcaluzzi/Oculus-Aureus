#!/bin/sh
# Heist CCTV self-view test (designs/bank-heist.md 23c): Bond 0 watches a camera that sees him at the banking hall's screens, then the
# security camera with him on its pad; the feed must draw his own body (test.cctv_info().bonds). Inside the build container with
# Xvfb on :99 and build_pc built. Screenshots: build_pc/heist_cctvself_shots/shot_NNNNN.bmp (convert with tests/bmp2png.py, delete the BMPs).
#   run_cctvself.sh [coop|solo|all]   coop: local co-op (GE_COOP=1), the Bonds' own bodies; solo: one Bond, port_cctvbody.c's
#                                     presentation-only body. Default all. Last line "HEIST CCTVSELF DONE <n> passed, <m> failed".
# Full logs: ${LOGDIR:-/tmp}/heist_cctvself.<case>.log
WHICH=${1:-all}
M=/home/dev/project/build_pc/Data/Scripts/modes
cp /home/dev/project/tests/heist/heist_cctvself.lua $M/heist_cctvself.lua
for f in mode_heist.lua:heist.lua heist_lib.lua heist_bank_map.lua heist_ai.lua; do
    cp "/home/dev/project/tools/${f%%:*}" "$M/${f#*:}"
done
cp /home/dev/project/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
mkdir -p /home/dev/project/build_pc/heist_cctvself_shots
cd /home/dev/project/build_pc/heist_cctvself_shots || exit 1
PASS=0
FAIL=0
run() {
    c=$1
    shift
    env DISPLAY=:99 GE_MODE=heist_cctvself GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FIXEDTICK=1 \
        GE_SEED=${GE_SEED:-7} GE_TICKHZ=60 GE_FRAMES=${GE_FRAMES:-3000} "$@" timeout 300 ../ProjectMidas > ${LOGDIR:-/tmp}/heist_cctvself.$c.log 2>&1
    grep -a -E "HEIST (PASS|FAIL|INFO|SHOT)|gfx: wrote|cctv: the solo|lua: .*error" ${LOGDIR:-/tmp}/heist_cctvself.$c.log | sed "s/^/$c: /"
    p=$(grep -a -c "HEIST PASS" ${LOGDIR:-/tmp}/heist_cctvself.$c.log)
    f=$(grep -a -c "HEIST FAIL" ${LOGDIR:-/tmp}/heist_cctvself.$c.log)
    grep -aq "HEIST DONE" ${LOGDIR:-/tmp}/heist_cctvself.$c.log || f=$((f + 1))
    PASS=$((PASS + p))
    FAIL=$((FAIL + f))
}
if [ "$WHICH" = coop ] || [ "$WHICH" = all ]; then run coop GE_COOP=1; fi
if [ "$WHICH" = solo ] || [ "$WHICH" = all ]; then run solo; fi
if [ "$WHICH" = hash ] || [ "$WHICH" = all ]; then
    # the solo body is presentation: the same solo run with the feed drawn (body included) and with no feed render at all
    # (GE_CCTV_NORENDER=1: the view's sim side only) must hash the same every sim tick (GE_HASHLOG)
    H=${LOGDIR:-/tmp}
    sp=$PASS
    sf=$FAIL
    run hashdraw GE_HASHLOG=$H/cctvself.draw.hash > /dev/null
    run hashnone GE_HASHLOG=$H/cctvself.none.hash GE_CCTV_NORENDER=1 > /dev/null   # (its body checks fail by design: not counted)
    PASS=$sp
    FAIL=$sf
    na=$(grep -c '^tick' $H/cctvself.draw.hash)
    nb=$(grep -c '^tick' $H/cctvself.none.hash)
    [ "$nb" -lt "$na" ] && na=$nb   # (the common prefix: a run ends on a frame count)
    grep '^tick' $H/cctvself.draw.hash | head -n $na > $H/cctvself.a
    grep '^tick' $H/cctvself.none.hash | head -n $na > $H/cctvself.b
    if [ "$na" -ge 1000 ] && cmp -s $H/cctvself.a $H/cctvself.b && grep -aq "cctv: the solo Bond's feed body" $H/heist_cctvself.hashdraw.log; then
        echo "hash: HEIST PASS the solo body drawn vs no feed render: $na ticks hash-identical"; PASS=$((PASS + 1))
    else
        echo "hash: HEIST FAIL the solo body drawn vs no feed render: $na ticks, first difference: $(diff $H/cctvself.a $H/cctvself.b | grep '^<' | head -n 1 | cut -c1-160)"
        FAIL=$((FAIL + 1))
    fi
fi
echo "HEIST CCTVSELF DONE $PASS passed, $FAIL failed"

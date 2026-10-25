#!/bin/sh
# Heist determinism test (designs/bank-heist.md P3), inside the build container with Xvfb on :99 and build_pc built.
# The scripted heist (tests/heist/heist_determinism.lua: the camera feeds, knock-outs, the alarm and its waves, the crack with a jam, the
# vault open and sealed) runs twice with the same seed at GE_TICKHZ=60 (GE_FIXEDTICK=1: one sim tick per frame). Every sim tick of the
# stage writes the state hash and its 12 fields (GE_HASHLOG); the two logs must be identical over their common prefix, at least
# 3000 ticks. Cases:
#   solo   Bond 0 alone
#   coop   local co-op, two Bonds (GE_COOP=1): Bond 0 watches the feeds, Bond 1 slaps and cracks (GE_HEIST_DET_P1=1); run again with
#          the local player switched (GE_COOP_VIEW=1), which is the netplay condition: two machines differ only in who is local
#   net    lockstep netplay: a host and a guest process on loopback (GE_NET=host / join), the same scenario; the two processes'
#          hash logs must be identical tick for tick, and neither may report a DESYNC (the link's own 30-tick hash check)
# On the Janus bank (GE_STAGE=levelgen) since P2.4. OUT=<dir> keeps parallel runs apart.
# usage: run_determinism.sh [solo|coop|net|all]   -> "HEIST DET <case>: ..." lines; last line "HEIST DET DONE <n> passed, <m> failed"
# Logs: /tmp/heist_det/<case>.<run>.{hash,log}. On a difference the first differing tick is printed; rerun one side with
# GE_HASHLOG_DUMP=<tick>-<tick> for the text dump of that tick (both runs, then diff the dumps).
WHICH=${1:-all}
STAGE=levelgen   # the Janus bank (P2.4; Bunker 2 until then)
OUT=${OUT:-/tmp/heist_det}
mkdir -p $OUT
cp /home/dev/project/tests/heist/heist_determinism.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_determinism.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
cp /home/dev/project/tools/heist_ai.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_ai.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
cd /home/dev/project/build_pc || exit 1
FRAMES=${GE_FRAMES:-4500}   # (about 540 of them are the boot and the intro before the stage's first-person play)
NEED=${NEED:-3000}
PASS=0
FAIL=0

# run <case> <run> <extra env...>
run() {
    c=$1
    r=$2
    shift 2
    env DISPLAY=:99 GE_MODE=heist_determinism GE_STAGE=$STAGE GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 \
        GE_FRAMES=$FRAMES GE_HASHLOG=$OUT/$c.$r.hash "$@" timeout 600 ./ProjectMidas > $OUT/$c.$r.log 2>&1
    grep -a "HEIST DET" $OUT/$c.$r.log > $OUT/$c.$r.det
}

# compare <case> <a> <b>
compare() {
    c=$1
    a=$OUT/$c.$2.hash
    b=$OUT/$c.$3.hash
    na=$(grep -c '^tick' $a)
    nb=$(grep -c '^tick' $b)
    n=$na
    [ "$nb" -lt "$n" ] && n=$nb
    grep '^tick' $a | head -n $n > $OUT/cmp.a
    grep '^tick' $b | head -n $n > $OUT/cmp.b
    if cmp -s $OUT/cmp.a $OUT/cmp.b && [ "$n" -ge "$NEED" ]; then
        echo "HEIST DET PASS $c $2 vs $3: $n ticks hash-identical"
        PASS=$((PASS + 1))
    else
        first=$(diff $OUT/cmp.a $OUT/cmp.b | grep '^<' | head -n 1 | cut -c1-200)
        echo "HEIST DET FAIL $c $2 vs $3: $n ticks (need $NEED), first difference: ${first:-none}"
        FAIL=$((FAIL + 1))
    fi
    # the scenario's own notes (sim time, phases) must agree too, over the common prefix (a run ends on a frame or tick count)
    la=$(wc -l < $OUT/$c.$2.det)
    lb=$(wc -l < $OUT/$c.$3.det)
    l=$la
    [ "$lb" -lt "$l" ] && l=$lb
    head -n $l $OUT/$c.$2.det > $OUT/cmp.da
    head -n $l $OUT/$c.$3.det > $OUT/cmp.db
    if cmp -s $OUT/cmp.da $OUT/cmp.db && [ "$l" -ge 20 ]; then
        echo "HEIST DET PASS $c $2 vs $3: scenario notes identical ($l lines)"
        PASS=$((PASS + 1))
    else
        echo "HEIST DET FAIL $c $2 vs $3: scenario notes differ ($l lines in common)"
        diff $OUT/cmp.da $OUT/cmp.db | head -n 6
        FAIL=$((FAIL + 1))
    fi
}

# coverage: the scenario reached every step (alarm, waves, crack, jam, vault open and sealed, knock-outs, a feed on at the end)
coverage() {
    c=$1
    f=$OUT/$c.a.det
    l=$OUT/$c.a.log
    ok=1
    for want in "feeds open: viewing [0-9]" "after A: viewing [0-9]" "after B: viewing nil" "civilian [0-9]* ko true" "guard [0-9]* ko true" \
                "hot true" "cracking cracking" "jammed jammed" "restarted cracking" "vault open" "vault sealed" "viewing [0-9]* ko"; do
        if ! grep -q "$want" $f; then echo "HEIST DET FAIL $c coverage: no '$want'"; ok=0; fi
    done
    if ! grep -aq "heist: ALARM" $l; then echo "HEIST DET FAIL $c coverage: no ALARM"; ok=0; fi
    if ! grep -aq "the cracker JAMMED" $l; then echo "HEIST DET FAIL $c coverage: no jam"; ok=0; fi
    last=$(grep " w=" $f | tail -n 1)
    if ! echo "$last" | grep -q " w=[1-9][0-9]*/[1-9]"; then echo "HEIST DET FAIL $c coverage: no reinforcement wave"; ok=0; fi
    if [ $ok = 1 ]; then echo "HEIST DET PASS $c coverage: every step reached ($(echo "$last" | sed 's/.*HEIST DET *//' | cut -c1-60) ...$(echo "$last" | grep -o ' w=[0-9/]*'))"; PASS=$((PASS + 1)); else FAIL=$((FAIL + 1)); fi
}

if [ "$WHICH" = solo ] || [ "$WHICH" = all ]; then
    run solo a
    run solo b
    compare solo a b
    coverage solo
fi
if [ "$WHICH" = coop ] || [ "$WHICH" = all ]; then
    # the co-op runs need both Bonds scripted by the mode, so the pads stay idle (the puppet does nothing)
    run coop a GE_COOP=1 GE_COOP_P2=idle GE_HEIST_DET_P1=1 GE_COOP_VIEW=0
    run coop b GE_COOP=1 GE_COOP_P2=idle GE_HEIST_DET_P1=1 GE_COOP_VIEW=0
    run coop v1 GE_COOP=1 GE_COOP_P2=idle GE_HEIST_DET_P1=1 GE_COOP_VIEW=1
    compare coop a b
    compare coop a v1
    coverage coop
fi
if [ "$WHICH" = net ] || [ "$WHICH" = all ]; then
    # lockstep netplay, two processes on loopback: the host (player 0) runs the mode, the guest (player 1) takes it from the host;
    # neither has local input (both Bonds are scripted by the mode). Their per-tick hash logs must be identical, and no DESYNC.
    PORT=${PORT:-27411}
    NETTICKS=${NETTICKS:-$((FRAMES - 400))}
    rm -f $OUT/net.*.hash
    for pr in hdet_h hdet_g; do mkdir -p Data/Profiles/$pr; done
    NETCOMMON="DISPLAY=:99 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_HEIST_DET_P1=1 GE_FRAMES=$((FRAMES + 2000)) GE_NET_STOP=$NETTICKS"
    env $NETCOMMON GE_PROFILE=hdet_h GE_NET=host:$PORT GE_STAGE=$STAGE GE_SEED=7 GE_MODE=heist_determinism GE_HASHLOG=$OUT/net.host.hash \
        timeout -s KILL 1500 ./ProjectMidas > $OUT/net.host.log 2>&1 &
    HP=$!
    sleep 2
    env $NETCOMMON GE_PROFILE=hdet_g GE_NET=join:127.0.0.1:$PORT GE_HASHLOG=$OUT/net.guest.hash \
        timeout -s KILL 1500 ./ProjectMidas > $OUT/net.guest.log 2>&1 &
    GP=$!
    wait $HP
    wait $GP
    grep -a "HEIST DET" $OUT/net.host.log > $OUT/net.host.det
    grep -a "HEIST DET" $OUT/net.guest.log > $OUT/net.guest.det
    cp $OUT/net.host.det $OUT/net.a.det
    cp $OUT/net.host.log $OUT/net.a.log
    NEED_SAVED=$NEED
    NEED=$((NETTICKS - 600))
    compare net host guest
    NEED=$NEED_SAVED
    d=$(grep -ac "DESYNC" $OUT/net.host.log $OUT/net.guest.log | awk -F: '{s += $2} END {print s + 0}')
    h=$(grep -ac "ge: net: hash" $OUT/net.host.log)
    if [ "$d" = 0 ]; then echo "HEIST DET PASS net: no DESYNC reported (host hash lines: $h)"; PASS=$((PASS + 1));
    else echo "HEIST DET FAIL net: $d DESYNC lines"; grep -a DESYNC $OUT/net.host.log | head -n 3; FAIL=$((FAIL + 1)); fi
    coverage net
fi
echo "HEIST DET DONE $PASS passed, $FAIL failed"

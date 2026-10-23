#!/bin/sh
# Mission-end harness (mission-end job): the level exit under lockstep. Called by net_lockstep.sh phases 28-31 and usable alone:
#   docker exec --user 1000 -e DISPLAY=:99 midas-mend sh /home/dev/project/tests/net_missionend.sh <tag> <port> <jumper 0|1> <presser none|0|1> <press-from-tick> [extra KEY=VALUE...]
# Dam: at sim tick 5 the jumper Bond is put on the bungee-jump trigger pad (GE_COOP_MOVETOPAD=5:<jumper>:330); every objective reads complete
# (GE_TEST_ALLOBJ=1), so the level's success exit runs (fade, "press a button" exit camera, fade, stage ends). The presser Bond (scripted record
# file) taps A every 10 ticks from <press-from-tick>; "none" presses nothing (then the jumper taps from tick 100, so the exit still happens).
# Output: $OUT/<tag>.host.fp / .guest.fp (the fingerprint files, with the 'mission end' and 'end-state' lines), <tag>.host.log / .guest.log.
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
OUT=${OUT:-/home/dev/project/build_tests/net2}
HERE=$(dirname "$0")
TAG=$1; PORT=$2; JUMPER=$3; PRESSER=$4; FROM=$5
if [ $# -gt 5 ]; then shift 5; else shift $#; fi
EXTRA="$*"
RECDIR=$OUT/$TAG.rec
mkdir -p "$RECDIR"
TICKS=${TICKS:-1600}
RECTICKS=${RECTICKS:-2400}

gen() {   # <file> <press-from or 0 for never>
    awk -v from="$2" -v n="$RECTICKS" 'BEGIN { for (t = 1; t <= n; t++) { b = "0"; if (from > 0 && t >= from && t <= from + 420 && (t - from) % 10 == 0) b = "8000"; print "R", t, b, 0, 0, 0, 0 } }' > "$1"
}
if [ "$PRESSER" = none ]; then
    P0=0; P1=0
    if [ "$JUMPER" = 0 ]; then P0=100; else P1=100; fi
else
    P0=0; P1=0
    if [ "$PRESSER" = 0 ]; then P0=$FROM; else P1=$FROM; fi
fi
gen "$RECDIR/recA.txt" $P0
gen "$RECDIR/recB.txt" $P1

if [ -n "$SHOT" ]; then   # SHOT=<frames>: a BMP of each window every that many frames, into $OUT/<tag>.shots/{host,guest}
    mkdir -p "$OUT/$TAG.shots/host" "$OUT/$TAG.shots/guest"
    rm -f "$OUT/$TAG.shots"/host/*.bmp "$OUT/$TAG.shots"/guest/*.bmp
    export HCWD="$OUT/$TAG.shots/host" GCWD="$OUT/$TAG.shots/guest"
    EXTRA="$EXTRA GE_SHOT=$SHOT GE_WINDOW=640x360"
fi
env REC="$RECDIR" DREC="$RECDIR" RECA=recA.txt RECB=recB.txt OUT="$OUT" BIN="$BIN" \
    sh "$HERE/net_replay2.sh" "$TAG" "$PORT" "$TICKS" none GE_MODS=invincible=1,coop_exits=${EXITS:-first} GE_NET_LOG=1 GE_COOP_MOVETOPAD=${MOVETICK:-5}:$JUMPER:330${PARTNERMOVE:+,$PARTNERMOVE:$((1 - JUMPER)):330} GE_TEST_ALLOBJ=1 $EXTRA

#!/bin/sh
# Modes in co-op / netplay: one scenario run, local co-op or two netplay instances (the building block of net_lockstep.sh phases 40-43).
#
#   sh net_modes.sh local <tag> <ticks> [KEY=VALUE ...]        one co-op instance, both Bonds scripted
#   sh net_modes.sh net   <tag> <port> <ticks> [KEY=VALUE ...] host + guest over UDP (net_replay2.sh), both Bonds scripted
#
# Environment: BIN, OUT (default build_tests/modes), DOUT, STAGE (default flatland), MODE (default reset; the HOST gets GE_MODE, the guest must
# take it from the host), LUATEST (default tests/lua/coop_modes_test.lua; "" = none), TAPS0 / TAPS1 (B-tap ticks of P0 / P1), YAWS0 / YAWS1,
# MOVES (GE_COOP_MOVETOPAD list), HPROFILE / GPROFILE, HOSTENV / GUESTENV (one side only).
# The scenario: see tests/lua/coop_modes_test.lua; the defaults below are phase 40's (Flatland, 3 hostages).
# Output: $OUT/<tag>.* (the fingerprint files, the logs). Exit status 0 when the run reached <ticks> sim ticks.
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
ROOT=${ROOT:-$(dirname "$BIN")}
OUT=${OUT:-/home/dev/project/build_tests/modes}
DOUT=${DOUT:-$OUT}
HERE=$(dirname "$0")
KIND=$1
TAG=$2
shift 2
if [ "$KIND" = net ]; then PORT=$1; shift; fi
TICKS=$1
shift
EXTRA=
for a in "$@"; do   # H:KEY=VALUE = host only, G:KEY=VALUE = guest only, else both
    case "$a" in H:*) HOSTENV="$HOSTENV ${a#H:}" ;; G:*) GUESTENV="$GUESTENV ${a#G:}" ;; *) EXTRA="$EXTRA $a" ;; esac
done
STAGE=${STAGE:-flatland}
MODE=${MODE-reset}
LUATEST=${LUATEST-/home/dev/project/tests/lua/coop_modes_test.lua}
TAPS0=${TAPS0-110,600,660}
TAPS1=${TAPS1-112,400,440,790}
YAWS0=${YAWS0-590:180000}
YAWS1=${YAWS1-780:180000}
MOVES=${MOVES-100:0:113,100:1:111,200:0:157,640:0:111,700:0:112,760:1:157,800:1:112}
HPROFILE=${HPROFILE:-netbase_h}
GPROFILE=${GPROFILE:-netbase_g}
mkdir -p "$OUT/$TAG.rec"
awk -v n=$((TICKS + 200)) -v taps="$TAPS0" -v yaws="$YAWS0" -f "$HERE/net/modes_rec.awk" > "$OUT/$TAG.rec/recA.txt"
awk -v n=$((TICKS + 200)) -v taps="$TAPS1" -v yaws="$YAWS1" -f "$HERE/net/modes_rec.awk" > "$OUT/$TAG.rec/recB.txt"
for pr in $HPROFILE $GPROFILE; do
    mkdir -p "$ROOT/Data/Profiles/$pr"
    printf '[input]\nlook_updown=1\nauto_aim=0\naim_control=0\nlook_ahead=1\n' > "$ROOT/Data/Profiles/$pr/midas.ini"
done
LUA=
if [ -n "$LUATEST" ]; then LUA="GE_LUA_TEST=$LUATEST"; fi
if [ "$KIND" = local ]; then
    LREC=recA.txt; PREC=recB.txt   # the inputs belong to the players, not the seats: in view 1 the local Bond is player 1
    if [ "${VIEW:-0}" = 1 ]; then LREC=recB.txt; PREC=recA.txt; fi
    rm -f "$OUT/$TAG.local.fp"
    env GE_PROFILE=$HPROFILE GE_COOP=1 GE_STAGE=$STAGE GE_DIFF=1 GE_SEED=7 GE_COOP_RESEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SKIPINTRO=1 GE_NOINISAVE=1 \
        GE_STAGE_AFTER=20 GE_MODS=invincible=0,enemy_damage=400 GE_COOP_LOG=1 GE_COOP_FP=1 GE_COOP_CLOCKDBG=1 GE_COOP_VIEW=${VIEW:-0} \
        GE_FRAMES=$((TICKS + 60)) GE_COOP_LOCAL=replay:$DOUT/$TAG.rec/$LREC GE_COOP_P2=replay:$DOUT/$TAG.rec/$PREC \
        ${MODE:+GE_MODE=$MODE} $LUA GE_COOP_MOVETOPAD="$MOVES" ${MOVECHR:+GE_COOP_MOVETOCHR=$MOVECHR} GE_COOP_FPFILE="$DOUT/$TAG.local.fp" $EXTRA \
        timeout -s KILL 400 "$BIN" > "$OUT/$TAG.local.log" 2>&1
    n=$(grep -ac 'ge: coop: fp ' "$OUT/$TAG.local.fp")
    echo "$TAG: local run, $n fingerprints"
    [ "$n" -ge "$TICKS" ]
    exit $?
fi
env STAGE=$STAGE BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$OUT/$TAG.rec" DREC="$DOUT/$TAG.rec" RECA=recA.txt RECB=recB.txt \
    HPROFILE=$HPROFILE GPROFILE=$GPROFILE HOSTENV="${MODE:+GE_MODE=$MODE} $HOSTENV" GUESTENV="$GUESTENV" \
    sh "$HERE/net_replay2.sh" "$TAG" "$PORT" "$TICKS" "${LAG:-none}" GE_NET_LOG=1 GE_MODEHUD_LOG=1 $LUA GE_COOP_MOVETOPAD="$MOVES" ${MOVECHR:+GE_COOP_MOVETOCHR=$MOVECHR} $EXTRA

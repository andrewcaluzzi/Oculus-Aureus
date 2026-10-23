#!/bin/sh
# Netplay job 7: the render-visibility walk-through. Both Bonds tour Dam through the start tunnel, the gate end, the dam road and the tunnels
# under it (teleports, GE_ROOMTEST_TP, 4 headings at each spot) in a netplay session; every machine logs, per tick and Bond, the rooms the
# canonical SIM walk found and the rooms the view actually DRAWS (GE_ROOMLOG). The same spots are then visited in plain SOLO runs (the vanilla
# walk: no co-op, no render walk) and each view's drawn set is compared with the solo walk of the same position, tick for tick. Screenshots of
# both views are taken at the spawn tunnel and at the first waypoints.
#
#   docker exec --user 1000 -e DISPLAY=:99 midas-j7 sh /home/dev/project/tests/net_roomwalk.sh <tag> <port> [mode]
#     mode  same   (default) host and guest windows both 960x540, the canonical 16:9 view: no render walk runs, the drawn set is the sim walk
#           mixed  host 1280x720 widescreen, guest 640x480 4:3 with a wide FOV: each view runs its own render walk (the case the job is for)
#           forced both windows 960x540 but the render walk forced on (GE_NET_RENDERWALK=1): it must give the same sets as the sim walk
#   DOORS=<tick>:o,<tick>:c  (environment) opens / closes every door of the stage at those sim ticks, in the netplay run and the solo runs alike
# Output: build_tests/net7/<tag>.* ; exit status = number of FAILs.
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
ROOT=${ROOT:-$(dirname "$BIN")}
OUT=${OUT:-/home/dev/project/build_tests/net7}
DOUT=${DOUT:-$OUT}
HERE=$(dirname "$0")
TAG=${1:-rw}
PORT=${2:-27400}
MODE=${3:-same}
FAILS=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILS=$((FAILS + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }
mkdir -p "$OUT"
rm -f "$OUT/$TAG".*

mkprof() {   # <name> <width> <height> <widescreen 0|1> <hfov_wide>
    mkdir -p "$ROOT/Data/Profiles/$1"
    printf '[input]\nlook_updown=1\nauto_aim=0\naim_control=0\nlook_ahead=1\n[display]\nfullscreen=windowed\nwindow_width=%s\nwindow_height=%s\nwidescreen=%s\nhfov_wide=%s\n' \
        "$2" "$3" "$4" "$5" > "$ROOT/Data/Profiles/$1/midas.ini"
}
case "$MODE" in
    mixed) mkprof rw_h 1280 720 1 90; mkprof rw_g 640 480 0 110 ;;
    *)     mkprof rw_h 960 540 1 90;  mkprof rw_g 960 540 1 90 ;;
esac
FORCE=
if [ "$MODE" = forced ]; then FORCE="GE_NET_RENDERWALK=1"; fi

# idle input records for both Bonds (every tick: no buttons, no stick, no look)
awk 'BEGIN { for (t = 1; t <= 760; t++) printf "R %d 0 0 0 0 0 0 0 0 0\n", t }' > "$OUT/$TAG.idle.txt"

# the waypoints (x y z: the point the floor is searched below): the start tunnel, the tunnel mouth, the dam road, the plant and the tunnels under it
WPS="20108 160 16898
19500 160 16900
18892 101 17660
17223 250 19560
16222 101 20309
13756 101 17454
15729 101 5085
17463 101 8903
16898 44 8239
14779 102 1905
13598 -434 2114
8440 -440 -1117
1712 -434 -3343"
NW=13
# <player> <reverse 0|1> <target player in the spec>: the TP list. Each waypoint takes 40 ticks, 4 headings 10 ticks apart.
mktp() {
    echo "$WPS" | awk -v rev="$2" -v tgt="$3" -v nw="$NW" 'BEGIN { n = 0 } NF == 3 { x[n] = $1; y[n] = $2; z[n] = $3; n++ }
        END { split("90 180 270 0", yaw, " ");
              for (i = 0; i < nw; i++) { k = rev ? nw - 1 - i : i;
                  for (h = 1; h <= 4; h++) printf "%s%d:%d:%d:%d:%d:%d", (i || h > 1) ? "," : "", 30 + 40 * i + (h - 1) * 10, tgt, x[k], y[k], z[k], yaw[h] } }'
}
TP0=$(mktp 0 0 0)
TP1=$(mktp 1 1 1)
SOLO0=$(mktp 0 0 0)    # the solo run of Bond 0's tour: player 0
SOLO1=$(mktp 1 1 0)    # the solo run of Bond 1's tour: player 0 again
# screenshots of both views: the host tours forward and the guest backward, so tick 35 / 55 are the host in the start tunnel (heading -x: the tunnel; heading +x: the gate door
# 180 units behind the spawn point) and tick 515 / 535 are the guest in the same two places; the other ticks are one heading of waypoints along the way
SHOTS="35,55,115,195,275,355,435,515,535"
STOP=$((30 + 40 * NW + 20))

echo "== walk-through: $MODE windows, $NW waypoints per Bond, $STOP ticks"
mkdir -p "$OUT/$TAG.shots/host" "$OUT/$TAG.shots/guest"
env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$OUT" DREC="$DOUT" RECA="$TAG.idle.txt" RECB="$TAG.idle.txt" HPROFILE=rw_h GPROFILE=rw_g \
    HCWD="$OUT/$TAG.shots/host" GCWD="$OUT/$TAG.shots/guest" HOSTENV="GE_ROOMLOG=$DOUT/$TAG.host.rooms" GUESTENV="GE_ROOMLOG=$DOUT/$TAG.guest.rooms" \
    sh "$HERE/net_replay2.sh" "$TAG.net" "$PORT" "$STOP" none GE_MODS=invincible=1 GE_ROOMTEST_TP="$TP0,$TP1" GE_ROOMTEST_SHOT="$SHOTS" $FORCE $EXTRAENV ${DOORS:+GE_ROOMTEST_DOORS=$DOORS} \
    > "$OUT/$TAG.net.run.txt" 2>&1
check "the netplay run finished with identical fingerprints and no DESYNC" "grep -q IDENTICAL $OUT/$TAG.net.run.txt && ! grep -aq \"ge: net: DESYNC\" $OUT/$TAG.net.host.log && ! grep -aq \"ge: net: DESYNC\" $OUT/$TAG.net.guest.log"
check "the teleports ran on both machines" "[ \$(grep -ac 'roomtest: tick .* player [01] moved to' $OUT/$TAG.net.host.fp) -ge $((NW * 8)) ]"

solo() {   # <name> <profile> <tp list>
    env GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_STAGE_AFTER=20 GE_DIFF=1 GE_STAGE=dam GE_SEED=7 GE_FRAMES=$((STOP + 120)) GE_FIXEDTICK=1 GE_TICKHZ=60 \
        GE_PROFILE="$2" GE_ROOMTEST_NOINTRO=1 ${DOORS:+GE_ROOMTEST_DOORS=$DOORS} GE_MODS=invincible=1 GE_ROOMLOG="$DOUT/$TAG.$1.rooms" GE_ROOMTEST_TP="$3" GE_COOP_FPFILE="$DOUT/$TAG.$1.fp" \
        timeout -s KILL 300 "$BIN" > "$OUT/$TAG.$1.log" 2>&1
}
solo solo0 rw_h "$SOLO0"
solo solo1 rw_g "$SOLO1"

# <rooms file> <player> -> "tick start tile sim draw" per line
rows() { sed -n 's/^ge: rooms: tick \([0-9]*\) p'"$2"' start \([-0-9]*\) tile \([-0-9]*\) sim [0-9]*: \([0-9,]*\) draw [0-9]*: \([0-9,]*\) vis.*/\1 \2 \3 \4 \5/p' "$1"; }
rows "$OUT/$TAG.host.rooms" 0 > "$OUT/$TAG.h0"
rows "$OUT/$TAG.host.rooms" 1 > "$OUT/$TAG.h1"
rows "$OUT/$TAG.guest.rooms" 0 > "$OUT/$TAG.g0"
rows "$OUT/$TAG.guest.rooms" 1 > "$OUT/$TAG.g1"
rows "$OUT/$TAG.solo0.rooms" 0 > "$OUT/$TAG.s0"
rows "$OUT/$TAG.solo1.rooms" 0 > "$OUT/$TAG.s1"
echo "rows: host p0 $(wc -l < $OUT/$TAG.h0), host p1 $(wc -l < $OUT/$TAG.h1), guest p0 $(wc -l < $OUT/$TAG.g0), guest p1 $(wc -l < $OUT/$TAG.g1), solo0 $(wc -l < $OUT/$TAG.s0), solo1 $(wc -l < $OUT/$TAG.s1)"

# <view rows> <solo rows> <col> -> "<ticks compared> <ticks with a room missing> <rooms missing in all> <ticks with extra rooms>" and up to 5 examples of a miss.
# A room the solo walk draws that the view does not = something that should be in sight and is not. Extras (the canonical view is a superset of a
# window's: wider, full height) cost nothing: the z-buffer hides them.
cmpsets() {
    awk -v col="$3" 'NR == FNR { a[$1] = $col; next }
        ($1 in a) { n++; ns = split($col, s, ","); nv = split(a[$1], v, ","); delete inv; for (i = 1; i <= nv; i++) inv[v[i]] = 1
                    miss = ""; for (i = 1; i <= ns; i++) if (s[i] != "" && !(s[i] in inv)) { miss = miss s[i] " "; mr++ }
                    if (miss != "") { m++; if (m <= 5) print "  tick " $1 ": solo draws [" miss "] the view does not (view: " a[$1] ")" }
                    delete ins; for (i = 1; i <= ns; i++) ins[s[i]] = 1
                    for (i = 1; i <= nv; i++) if (v[i] != "" && !(v[i] in ins)) { e++; break } }
        END { print n + 0, m + 0, mr + 0, e + 0 }' "$1" "$2"
}
cmpline() { cmpsets "$1" "$2" "$3" | tail -n 1; }
nomiss() { l=$(cmpline "$1" "$2" "$3"); [ "$(echo $l | cut -d' ' -f1)" -ge 200 ] && [ "$(echo $l | cut -d' ' -f2)" -eq 0 ]; }
# identical sets on the common ticks (machine against machine)
cmpsame() { awk -v col="$3" 'NR == FNR { a[$1] = $col; next } ($1 in a) { n++; if (a[$1] != $col) { m++; if (m <= 5) print "  tick " $1 ": " a[$1] " | " $col } } END { print n + 0, m + 0 }' "$1" "$2"; }
same() { l=$(cmpsame "$1" "$2" "$3" | tail -n 1); [ "$(echo $l | cut -d' ' -f1)" -ge 200 ] && [ "$(echo $l | cut -d' ' -f2)" -eq 0 ]; }

echo "-- each view's drawn set against a fresh solo walk of the same spot (host view = Bond 0 on the host; guest view = Bond 1 on the guest)"
echo "host p0 view vs solo0:  compared / ticks missing a room / rooms missing / ticks with extra rooms = $(cmpline $OUT/$TAG.h0 $OUT/$TAG.s0 5)"; cmpsets "$OUT/$TAG.h0" "$OUT/$TAG.s0" 5 | head -n 5
echo "guest p1 view vs solo1: compared / ticks missing a room / rooms missing / ticks with extra rooms = $(cmpline $OUT/$TAG.g1 $OUT/$TAG.s1 5)"; cmpsets "$OUT/$TAG.g1" "$OUT/$TAG.s1" 5 | head -n 5
check "24 the host's own view draws every room a fresh solo walk from the same spot draws, every tick" "nomiss $OUT/$TAG.h0 $OUT/$TAG.s0 5"
check "24 the guest's own view draws every room a fresh solo walk from the same spot draws, every tick" "nomiss $OUT/$TAG.g1 $OUT/$TAG.s1 5"
check "24 the sim walks agree between the machines (both Bonds, host against guest)" "same $OUT/$TAG.h0 $OUT/$TAG.g0 4 && same $OUT/$TAG.h1 $OUT/$TAG.g1 4"
# the walk starts in the room the Bond really stands in: the start room column against the floor-tile room, per tick, for the local view
badstart() { awk '$2 != $3 { b++ } END { print b + 0 }' "$1"; }
echo "walk start room != floor-tile room: host p0 $(badstart $OUT/$TAG.h0) / $(wc -l < $OUT/$TAG.h0), guest p1 $(badstart $OUT/$TAG.g1) / $(wc -l < $OUT/$TAG.g1) ticks"
check "24 every walk of the local views starts in the room of the Bond's own floor tile" "[ \$(badstart $OUT/$TAG.h0) -eq 0 ] && [ \$(badstart $OUT/$TAG.g1) -eq 0 ]"

echo "screenshots: host $(ls $OUT/$TAG.shots/host | wc -l), guest $(ls $OUT/$TAG.shots/guest | wc -l) (BMPs in $OUT/$TAG.shots/{host,guest}, one per GE_ROOMTEST_SHOT tick: $SHOTS)"
echo "net_roomwalk: $FAILS failed"
exit $FAILS

#!/bin/sh
# Netplay job 3c: the sim must not depend on the window. Host and guest run with DIFFERENT window sizes, widescreen and FOV
# settings; a replay of either side's session log under yet another window must reproduce it; and the negative control
# (GE_NET_TEST_NOSIMVIEW=1 puts the pre-3c behaviour back: the window feeds the sim) must fail.
#
#   docker exec --user 1000 -e DISPLAY=:99 midas-net sh /home/dev/project/tests/net_simview.sh <tag> <port> [ticks=470]
# Uses the "everything" session (recEA / recEB) so characters are shot at and watched. Output in build_tests/net3c/<tag>.*
# Exit status = number of FAILs.
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
ROOT=${ROOT:-$(dirname "$BIN")}
OUT=${OUT:-/home/dev/project/build_tests/net3c}
DOUT=${DOUT:-$OUT}
REC=${REC:-/home/dev/project/tests/net}
DREC=${DREC:-$REC}
HERE=$(dirname "$0")
TAG=${1:-sv}
PORT=${2:-28300}
TICKS=${3:-470}
FAILS=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILS=$((FAILS + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }
mkdir -p "$OUT"

# Four windows. Same [input] block everywhere (the settings blocks are equal), only the display differs.
mkprof() {   # <name> <width> <height> <widescreen 0|1> <hfov_wide>
    mkdir -p "$ROOT/Data/Profiles/$1"
    printf '[input]\nlook_updown=1\nauto_aim=1\naim_control=0\nlook_ahead=1\n[display]\nfullscreen=windowed\nwindow_width=%s\nwindow_height=%s\nwidescreen=%s\nhfov_wide=%s\n' \
        "$2" "$3" "$4" "$5" > "$ROOT/Data/Profiles/$1/midas.ini"
}
mkprof simv_h 1280 720 1 90     # 16:9, vanilla FOV
mkprof simv_g 640 480 0 110     # 4:3, not widescreen, a wide field of view
mkprof simv_r1 960 400 1 80     # 2.4:1 ultrawide, narrower FOV: a third window for the replays
mkprof simv_r2 800 800 1 90     # square

rm -f "$OUT/$TAG".*
run() {   # <subtag> <port> [extra KEY=VALUE for both sides]
    t=$1; p=$2; shift 2
    env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" RECA=recEA.txt RECB=recEB.txt HPROFILE=simv_h GPROFILE=simv_g \
        sh "$HERE/net_replay2.sh" "$t" "$p" "$TICKS" none GE_NET_LOG=1 GE_NET_RNGTRACE=1 \
        GE_COOP_HURT=160:0:0.5,160:1:0.5,215:1:10,350:0:10,410:1:10,430:0:10,460:1:10 GE_COOP_RESPAWN=40 GE_MODS=invincible=0,enemy_damage=400 "$@" > "$OUT/$t.run.txt" 2>&1
}
nodesync() { ! grep -aq 'DESYNC' "$1"; }

echo "== host 1280x720 widescreen fov 90, guest 640x480 4:3 fov 110: $TICKS ticks"
run "$TAG.mixed" "$PORT"
check "windows differ: no DESYNC on the host" "nodesync $OUT/$TAG.mixed.host.log"
check "windows differ: no DESYNC on the guest" "nodesync $OUT/$TAG.mixed.guest.log"
check "windows differ: fingerprints identical host/guest" "grep -q IDENTICAL $OUT/$TAG.mixed.run.txt"
check "windows differ: both Bonds' watches really opened (state 5 for 20+ ticks each: the pause-box / watch-view regression, playtest 2)" "awk '\$3 == \"fp\" && \$15 == 5 { a++ } \$3 == \"fp\" && \$16 == 5 { b++ } END { exit !(a >= 20 && b >= 20) }' $OUT/$TAG.mixed.host.fp"
check "windows differ: hash checks ran" "[ \$(grep -ac 'state hash: ' $OUT/$TAG.mixed.host.log) -ge 1 ] && grep -a 'state hash: ' $OUT/$TAG.mixed.host.log | grep -q ' 0 mismatched'"
HL="$ROOT/Data/Profiles/simv_h/Logs/netlog_run1_host.txt"
GL="$ROOT/Data/Profiles/simv_g/Logs/netlog_run1_guest.txt"
cp "$HL" "$OUT/$TAG.mixed.hostlog.txt" 2>/dev/null
cp "$GL" "$OUT/$TAG.mixed.guestlog.txt" 2>/dev/null
grep -a '^T ' "$OUT/$TAG.mixed.hostlog.txt" | cut -d' ' -f1,2,5- | head -n "$TICKS" > "$OUT/$TAG.mixed.host.cut"
grep -a '^T ' "$OUT/$TAG.mixed.guestlog.txt" | cut -d' ' -f1,2,5- | head -n "$TICKS" > "$OUT/$TAG.mixed.guest.cut"
check "windows differ: per-tick fingerprints of the two logs identical ($(wc -l < $OUT/$TAG.mixed.host.cut) ticks)" "cmp -s $OUT/$TAG.mixed.host.cut $OUT/$TAG.mixed.guest.cut"

# replays under other windows: the host's log in the ultrawide, the guest's in the square one, each in both views
for spec in "host $HL simv_r1 0" "host $HL simv_r2 1" "guest $GL simv_r1 1" "guest $GL simv_r2 0"; do
    set -- $spec
    env BIN="$BIN" OUT="$OUT/$TAG.rp.$1.$3.v$4.out" sh "$HERE/net_replay_log.sh" "$2" "$4" "$3" > "$OUT/$TAG.rp.$1.$3.v$4.txt" 2>&1
    check "replay of the $1 log, window $3, view $4: ge: replay: OK" "grep -aq 'ge: replay: OK' $OUT/$TAG.rp.$1.$3.v$4.txt"
done

echo "== negative control: GE_NET_TEST_NOSIMVIEW=1 (the old behaviour) with the same two windows"
run "$TAG.old" "$((PORT + 10))" GE_NET_TEST_NOSIMVIEW=1
if grep -aq 'DESYNC' "$OUT/$TAG.old.host.log" "$OUT/$TAG.old.guest.log"; then
    pass "old behaviour desyncs: $(grep -ah 'DESYNC at tick' $OUT/$TAG.old.host.log | head -n 1 | cut -c1-200)"
else
    # the live pair may still agree (the windows can see the same things); the replay under a third window is the sharper test
    env BIN="$BIN" OUT="$OUT/$TAG.old.rp.out" GE_NET_TEST_NOSIMVIEW=1 sh "$HERE/net_replay_log.sh" "$ROOT/Data/Profiles/simv_h/Logs/netlog_run1_host.txt" 0 simv_r1 > "$OUT/$TAG.old.rp.txt" 2>&1
    check "old behaviour: a replay under another window does NOT reproduce the log" "grep -aq 'ge: replay: FAILED' $OUT/$TAG.old.rp.txt"
fi
echo "net_simview: $FAILS failed"
exit $FAILS

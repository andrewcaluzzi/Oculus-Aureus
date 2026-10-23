#!/bin/sh
# Rollback R2 harness: save / restore / re-simulate in co-op and netplay runs, for many (T, K) pairs, against an unrestored run.
#
# Runs inside the build container as user 1000 with DISPLAY set (build_pc_r2 + Xvfb, README-FORK.md):
#   wsl -d Ubuntu -- docker exec --user 1000 -e DISPLAY=:99 midas-r2 sh /home/dev/project/tests/snap_resim.sh <tag> <port> [parts]
#     tag    output prefix in build_tests/snap/
#     port   first UDP port; the script uses port .. port+39
#     parts  words: coop stress net lua (default all)       Windows: BIN=/c/.../ProjectMidas.exe OUT=... DOUT=C:/... ROOT=... as net_lockstep.sh
#
# The drivers (port/src/port_snap.c, env): GE_SNAPRESIM=T,K[,every] saves at tick T and, at tick T+K, restores and re-simulates T..T+K-1 sim-only
# inside that frame; GE_SNAPSTRESS=D[,start] does it at EVERY tick with depth D; GE_SNAPCMP=1 compares the whole snapshotted state byte for byte
# (statics, memp pools, the Lua arena, the sim high heap, the model texture pool) after each re-simulation against the state the restore began from.
# A run passes when
#   - its fingerprint stream (the main file) equals the unrestored run's (the re-simulation is invisible: the run goes on from a state equal to the
#     one it left), and every fingerprint line a re-simulated tick wrote (<fp>.resim) is a line of the unrestored run,
#   - every compare says BIT-IDENTICAL and at least the expected number of rollbacks happened,
#   - netplay: host and guest agree, no DESYNC, the hash exchange matched.
HERE=$(dirname "$0")
BIN=${BIN:-/home/dev/project/build_pc_r2/ProjectMidas}
ROOT=${ROOT:-$(dirname "$BIN")}
OUT=${OUT:-/home/dev/project/build_tests/snap}
DOUT=${DOUT:-$OUT}
REC=${REC:-/home/dev/project/tests/net}
DREC=${DREC:-$REC}
TAG=${1:-sr}
PORT=${2:-27700}
PARTS=${3:-"coop stress net lua"}
FAILS=0
export BIN OUT

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILS=$((FAILS + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }
mkdir -p "$OUT"

norm() { sed -e 's/ local [01] / /' -e 's/ yaw -\{0,1\}[0-9]* err [-0-9.]* / /' "$1"; }

# same <a> <b>: equal over the common prefix (a run ends on a frame count, so one may hold a tick more)
same() {
    na=$(wc -l < "$1"); nb=$(wc -l < "$2"); n=$na
    [ "$nb" -lt "$n" ] && n=$nb
    n=$((n - 8))
    head -n "$n" "$1" > "$OUT/cmp.$$.a"; head -n "$n" "$2" > "$OUT/cmp.$$.b"
    cmp -s "$OUT/cmp.$$.a" "$OUT/cmp.$$.b"
}
# resim_in <ref.txt> <resim.txt>: every fingerprint line a re-simulated tick wrote is a line of the unrestored run (a run ends on a frame count, so
# the last window may lie past the end of the reference: its lines, from the first tick line beyond the reference's last tick, are not judged)
resim_in() {
    [ -s "$2" ] || return 1
    mx=$(grep -a '^ge: coop: fp ' "$1" | tail -n 1 | awk '{ print $4 }')
    awk -v mx="${mx:-0}" '($3 == "fp" || $3 == "tick") && $4 + 0 > mx + 0 { exit } { print }' "$2" > "$2.cut"
    [ -s "$2.cut" ] && [ -z "$(grep -vxFf "$1" "$2.cut" | head -n 1)" ]
}
# cmp_ok <log>: the byte compare. Most windows are bit-identical; the rest may differ by a few words of RENDER-TIME state that a sim-only tick does not produce
# (wall-clock stamps of the HUD/FPS text, texture tile registers, the order a lazily loaded texture / model conversion lands in the texture pools, mema list order, vertices):
# never more than 32 KB in a window (of 4.8 MB), and the stream check below is what proves the sim.
cmp_ok() {
    t=$(grep -ac 'ge: snap: COMPARE' "$1"); b=$(grep -ac 'BIT-IDENTICAL' "$1")
    [ "$t" -gt 0 ] && ! grep -a 'ge: snap: COMPARE' "$1" | sed -n 's/.*: \([0-9]*\) differing bytes.*/\1/p' | awk '$1 > 32768 { bad = 1 } END { exit !bad }'
}
cmp_n() { grep -ac 'ge: snap: COMPARE' "$1"; }
lines_prefix() { n=$(wc -l < "$1"); m=$(wc -l < "$2"); [ "$m" -lt "$n" ] && n=$m; [ "$n" -gt 5 ] && [ "$(head -n $n "$1" | cksum)" = "$(head -n $n "$2" | cksum)" ]; }
no_desync() { ! grep -aq 'DESYNC' "$1"; }
hashline() { grep -a 'ge: net: state hash: ' "$1" | tail -n 1; }
hash_ok() {
    h=$(hashline "$1")
    c=$(echo "$h" | sed -n 's/.* \([0-9][0-9]*\) compared.*/\1/p'); b=$(echo "$h" | sed -n 's/.* \([0-9][0-9]*\) mismatched.*/\1/p')
    [ -n "$c" ] && [ -n "$b" ] && [ "$b" -eq 0 ] && [ "$c" -ge $((${2:-130} / 30 - 1)) ]
}
costs() {   # <log...>: average restore / re-sim cost lines
    grep -ah 'ge: snap: ROLLBACK' "$@" | awk '{ for (i = 1; i <= NF; i++) { if ($i == "restore") { r += $(i + 1); nr++ } if ($i == "re-sim") { s += $(i + 1); ns++ } if ($i == "(" ) {} } } END { if (nr) printf "restore %.3f ms avg, re-sim of the window %.3f ms avg over %d rollbacks (first 3 + every 300th are logged)\n", r / nr, s / ns, nr }'
}

# ---- co-op ------------------------------------------------------------------------------------------------------------------------------
if echo " $PARTS " | grep -q ' coop '; then
    echo "== co-op: Dam (the guards kill player 1 three times) and Depot (key pickup, keyed door, a kill): repeating save / restore / re-sim windows of depth K"
    for sc in dam depot; do
        sh "$HERE/snap_resim_one.sh" $sc "$TAG.$sc.ref" > /dev/null
        norm "$OUT/$TAG.$sc.ref.fp" > "$OUT/$TAG.$sc.ref.txt"
    done
    # K, period: distinct periods so the windows land on different ticks (deaths, respawns, pickups, doors) in every run
    for kp in 1:41 2:43 3:47 5:53 8:59 10:61; do
        k=${kp%:*}; p=${kp#*:}
        for sc in dam depot; do
            ( sh "$HERE/snap_resim_one.sh" $sc "$TAG.$sc.k$k" GE_SNAPRESIM=$((10 + k)),$k,$p GE_SNAPCMP=1 > /dev/null ) &
        done
        wait
    done
    for sc in dam depot; do
        for kp in 1:41 2:43 3:47 5:53 8:59 10:61; do
            k=${kp%:*}; p=${kp#*:}; t=$TAG.$sc.k$k
            n=$(cmp_n "$OUT/$t.raw")
            check "coop $sc K=$k every $p: $n rollbacks, byte compare within 32 KB (render-time bytes only), stream equals the unrestored run, re-simulated ticks' lines are its lines" \
                "[ $n -ge 5 ] && cmp_ok $OUT/$t.raw && same $OUT/$TAG.$sc.ref.txt $OUT/$t.txt && resim_in $OUT/$TAG.$sc.ref.txt $OUT/$t.resim.txt"
        done
        costs "$OUT/$TAG.$sc.k8.raw" "$OUT/$TAG.$sc.k10.raw"
    done
fi

# ---- stress -----------------------------------------------------------------------------------------------------------------------------
if echo " $PARTS " | grep -q ' stress '; then
    echo "== stress: a restore and a re-simulation of depth D at EVERY tick (constant rollback), the whole state compared after each"
    [ -s "$OUT/$TAG.dam.ref.txt" ] || { FRAMES=${FRAMES:-4000} sh "$HERE/snap_resim_one.sh" dam "$TAG.dam.ref" > /dev/null; norm "$OUT/$TAG.dam.ref.fp" > "$OUT/$TAG.dam.ref.txt"; }
    for d in 1 3 8; do
        ( FRAMES=900 sh "$HERE/snap_resim_one.sh" dam "$TAG.dam.s$d" GE_SNAPSTRESS=$d,20 GE_SNAPCMP=1 > /dev/null ) &
    done
    wait
    FRAMES=900 sh "$HERE/snap_resim_one.sh" dam "$TAG.dam.s0" > /dev/null
    norm "$OUT/$TAG.dam.s0.fp" > "$OUT/$TAG.dam.s0.txt"
    for d in 1 3 8; do
        t=$TAG.dam.s$d
        n=$(cmp_n "$OUT/$t.raw")
        check "stress D=$d: $n rollbacks (one per tick), byte compare within 32 KB (render-time bytes only), stream equals the unrestored 900-frame run, re-simulated lines are its lines" \
            "[ $n -ge 500 ] && cmp_ok $OUT/$t.raw && same $OUT/$TAG.dam.s0.txt $OUT/$t.txt && resim_in $OUT/$TAG.dam.ref.txt $OUT/$t.resim.txt"
    done
fi

# ---- netplay ----------------------------------------------------------------------------------------------------------------------------
HP=netbase_h
GP=netbase_g
for pr in $HP $GP; do
    mkdir -p "$ROOT/Data/Profiles/$pr"
    printf '[input]\nlook_updown=1\nauto_aim=0\naim_control=0\nlook_ahead=1\n' > "$ROOT/Data/Profiles/$pr/midas.ini"
done
# netrun <tag> <port> <stage> <ticks> <lag> [KEY=VALUE...]: net_replay2.sh with HOSTENV / GUESTENV from the caller's variables
netrun() {
    t=$1; p=$2; st=$3; tk=$4; lg=$5; shift 5
    env STAGE="$st" BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE="$HP" GPROFILE="$GP" RECA="${CRECA:-recA.txt}" RECB="${CRECB:-recB.txt}" \
        HOSTENV="$HOSTENV" GUESTENV="$GUESTENV" sh "$HERE/net_replay2.sh" "$t" "$p" "$tk" "$lg" GE_NET_LOG=1 GE_COOP_INVLOG=1 "$@" > "$OUT/$t.run.txt" 2>&1
}
netside() {   # <tag> <side>: the normalised fingerprint of one side
    norm "$OUT/$1.$2.fp" > "$OUT/$1.$2.n"
    [ -f "$OUT/$1.$2.fp.resim" ] && norm "$OUT/$1.$2.fp.resim" > "$OUT/$1.$2.resim.n"
}
netcheck() {   # <label> <tag> <ref tag> <ticks> <min rollbacks per side>
    for s in host guest; do netside "$2" $s; done
    check "$1: identical host/guest, no DESYNC on either side, hash exchange matched" "grep -q IDENTICAL $OUT/$2.run.txt && no_desync $OUT/$2.host.log && no_desync $OUT/$2.guest.log && hash_ok $OUT/$2.host.log $4 && hash_ok $OUT/$2.guest.log $4"
    check "$1: both sides' fingerprint streams equal the unrestored session's" "same $OUT/$3.host.n $OUT/$2.host.n && same $OUT/$3.host.n $OUT/$2.guest.n"
    check "$1: $(cmp_n $OUT/$2.host.log) host / $(cmp_n $OUT/$2.guest.log) guest rollbacks, byte compare within 32 KB (render-time bytes only); their re-simulated lines are the session's lines" \
        "[ $(cmp_n $OUT/$2.host.log) -ge $5 ] && [ $(cmp_n $OUT/$2.guest.log) -ge $5 ] && cmp_ok $OUT/$2.host.log && cmp_ok $OUT/$2.guest.log && resim_in $OUT/$3.host.n $OUT/$2.host.resim.n && resim_in $OUT/$3.guest.n $OUT/$2.guest.resim.n"
}
if echo " $PARTS " | grep -q ' net '; then
    echo "== netplay: Dam with session commands (spawned guards, a key, a KF7), a death and a respawn; Depot key drop; Flatland with spawns from both windows; lag"
    CMDS=GE_NET_TEST_CMD=60:g:spawn+guard+4+800,100:g:give+goldeneye_key+p2,110:h:give+kf7+p2+30,115:g:give+kf7+p1+5,116:h:spawn+guard+2+500
    DAMENV="GE_COOP_RESPAWN=120 GE_COOP_KILL=160:1 GE_COOP_MOVETO=175:0:1:1"
    netrun "$TAG.n1ref" $((PORT + 0)) dam 320 none $DAMENV $CMDS
    netside "$TAG.n1ref" host; netside "$TAG.n1ref" guest
    # different windows on the two machines: the host goes back 10 ticks every 37, the guest 6 every 41, 3 every 29 (covers 65 spawn, 105 key, 115 KF7, 160 death)
    HOSTENV="GE_SNAPRESIM=30,10,37 GE_SNAPCMP=1" GUESTENV="GE_SNAPRESIM=40,6,41 GE_SNAPCMP=1" netrun "$TAG.n1" $((PORT + 2)) dam 320 none $DAMENV $CMDS
    netcheck "net Dam/commands/death (host K=10, guest K=6)" "$TAG.n1" "$TAG.n1ref" 320 5
    grep -ac 'session command .* applied at tick' "$OUT/$TAG.n1.host.log" | sed 's/^/  session commands applied on the host (incl. re-applications are not logged): /'
    HOSTENV="GE_SNAPRESIM=55,12,23 GE_SNAPCMP=1" GUESTENV="GE_SNAPRESIM=52,8,31 GE_SNAPCMP=1" netrun "$TAG.n2" $((PORT + 4)) dam 320 none $DAMENV $CMDS
    netcheck "net Dam/commands/death (host K=12 every 23, guest K=8 every 31)" "$TAG.n2" "$TAG.n1ref" 320 8
    HOSTENV="GE_SNAPRESIM=30,9,37 GE_SNAPCMP=1" GUESTENV="GE_SNAPRESIM=40,7,41 GE_SNAPCMP=1" netrun "$TAG.n3" $((PORT + 6)) dam 320 100,20,5 $DAMENV $CMDS
    netrun "$TAG.n3ref" $((PORT + 8)) dam 320 100,20,5 $DAMENV $CMDS
    netside "$TAG.n3ref" host; netside "$TAG.n3ref" guest
    netcheck "net Dam, lag 100,20,5" "$TAG.n3" "$TAG.n3ref" 320 5
    # Depot: the guest's Bond holds a key object, dies, the key drops, the host picks it up
    DEPENV="GE_COOP_RESPAWN=120 GE_COOP_KILL=120:1 GE_COOP_MOVETO=140:0:1:1 GE_NET_TEST_CMD=80:g:give+key+p2"
    netrun "$TAG.n4ref" $((PORT + 10)) depot 300 none $DEPENV
    netside "$TAG.n4ref" host; netside "$TAG.n4ref" guest
    HOSTENV="GE_SNAPRESIM=60,12,31 GE_SNAPCMP=1" GUESTENV="GE_SNAPRESIM=66,10,37 GE_SNAPCMP=1" netrun "$TAG.n4" $((PORT + 12)) depot 300 none $DEPENV
    netcheck "net Depot key give / death drop / pickup" "$TAG.n4" "$TAG.n4ref" 300 5
    # Flatland: 12 guards spawned from both windows, they fight both Bonds
    FLENV="GE_HITLOG=1 GE_NET_TEST_CMD=60:h:spawn+guard+8+1200,62:g:spawn+guard+4+800"
    CRECA=recFA.txt CRECB=recFB.txt netrun "$TAG.n5ref" $((PORT + 14)) flatland 420 none $FLENV
    netside "$TAG.n5ref" host; netside "$TAG.n5ref" guest
    HOSTENV="GE_SNAPRESIM=50,10,29 GE_SNAPCMP=1" GUESTENV="GE_SNAPRESIM=58,8,33 GE_SNAPCMP=1" CRECA=recFA.txt CRECB=recFB.txt netrun "$TAG.n5" $((PORT + 16)) flatland 420 none $FLENV
    netcheck "net Flatland: 12 guards spawned from both windows, combat" "$TAG.n5" "$TAG.n5ref" 420 8
    check "net Flatland: the spawns and the fight really happened (12 spawned, damage events in the fingerprint)" "[ \$(grep -ac 'ge: arena: spawned guard' $OUT/$TAG.n5.host.log) -ge 12 ] && grep -aq 'ge: coop: damage' $OUT/$TAG.n5.host.fp"
fi

# ---- Lua ----------------------------------------------------------------------------------------------------------------------------------
if echo " $PARTS " | grep -q ' lua '; then
    echo "== Lua modes: the hostage scenario on Flatland (spawns, zones, hostage toggles, extraction), and the headline hostage-flatland (10 hostages, 20 guards, combat, deaths)"
    mn() { BIN="$BIN" OUT="$OUT" DOUT="$DOUT" ROOT="$ROOT" HPROFILE="$HP" GPROFILE="$GP" sh "$HERE/net_modes.sh" "$@" > "$OUT/$LT.run.txt" 2>&1; }
    LT=$TAG.lref; mn local $LT 800
    norm "$OUT/$LT.local.fp" > "$OUT/$LT.n"
    grep -a '^ge: lua: T ' "$OUT/$LT.local.log" > "$OUT/$LT.lua"
    LT=$TAG.l1; mn local $LT 800 GE_FRAMES=1500 GE_SNAPRESIM=40,8,29 GE_SNAPCMP=1
    norm "$OUT/$LT.local.fp" > "$OUT/$LT.n"; norm "$OUT/$LT.local.fp.resim" > "$OUT/$LT.resim.n"
    grep -a '^ge: lua: T ' "$OUT/$LT.local.log" > "$OUT/$LT.lua"
    check "lua Flatland hostage, local co-op, windows of 8 every 29 ticks ($(cmp_n $OUT/$LT.local.log) rollbacks): byte compare ok (Lua arena included), stream equals the unrestored run, the script printed the same lines (re-simulated ones are marked and filtered)" \
        "[ $(cmp_n $OUT/$LT.local.log) -ge 20 ] && cmp_ok $OUT/$LT.local.log && same $OUT/$TAG.lref.n $OUT/$LT.n && resim_in $OUT/$TAG.lref.n $OUT/$LT.resim.n && lines_prefix $OUT/$TAG.lref.lua $OUT/$LT.lua && [ \$(grep -ac '^ge: lua: T ' $OUT/$LT.local.log) -ge 30 ]"
    check "lua: a Lua handler ran inside a re-simulated window (the compare covers a live script)" "[ \$(grep -ac '^\[resim\] ge: lua: T' $OUT/$LT.local.log) -ge 3 ]"
    LT=$TAG.l2; mn local $LT 800 GE_FRAMES=1500 GE_SNAPRESIM=95,12,17 GE_SNAPCMP=1
    norm "$OUT/$LT.local.fp" > "$OUT/$LT.n"; norm "$OUT/$LT.local.fp.resim" > "$OUT/$LT.resim.n"
    grep -a '^ge: lua: T ' "$OUT/$LT.local.log" > "$OUT/$LT.lua"
    check "lua Flatland hostage, local co-op, windows of 12 every 17 ticks ($(cmp_n $OUT/$LT.local.log) rollbacks): byte compare ok, same stream, same script lines" \
        "[ $(cmp_n $OUT/$LT.local.log) -ge 30 ] && cmp_ok $OUT/$LT.local.log && same $OUT/$TAG.lref.n $OUT/$LT.n && resim_in $OUT/$TAG.lref.n $OUT/$LT.resim.n && lines_prefix $OUT/$TAG.lref.lua $OUT/$LT.lua"
    # netplay, different windows on the two machines
    LT=$TAG.lnref; mn net $LT $((PORT + 20)) 800
    for s in host guest; do norm "$OUT/$LT.$s.fp" > "$OUT/$LT.$s.n"; done
    LT=$TAG.ln; HOSTENV="GE_SNAPRESIM=40,8,29 GE_SNAPCMP=1" GUESTENV="GE_SNAPRESIM=47,10,37 GE_SNAPCMP=1" mn net $LT $((PORT + 22)) 800
    netcheck "lua Flatland hostage, netplay (host K=8, guest K=10)" "$TAG.ln" "$TAG.lnref" 800 10
    check "lua netplay: the script's lines are the same on both machines and equal the unrestored session's" \
        "[ \"\$(grep -a '^ge: lua: T ' $OUT/$TAG.ln.host.log)\" = \"\$(grep -a '^ge: lua: T ' $OUT/$TAG.ln.guest.log)\" ] && [ \"\$(grep -a '^ge: lua: T ' $OUT/$TAG.ln.host.log)\" = \"\$(grep -a '^ge: lua: T ' $OUT/$TAG.lnref.host.log)\" ]"
    # the headline: Flatland, 10 hostages and 20 guards, mortal Bonds in the middle of the guards (net_lockstep.sh phase 42), local co-op
    HFENV="MODE=hostage-flatland LUATEST=$HERE/lua/damage_log.lua MOVES=100:0:145,100:1:130 TAPS0=105 TAPS1=107 YAWS0=101:270000 YAWS1=101:270000"
    LT=$TAG.hfref; ( eval "export $HFENV"; mn local $LT 1500 GE_FRAMES=1700 )
    norm "$OUT/$LT.local.fp" > "$OUT/$LT.n"; grep -a '^ge: lua: [TD]' "$OUT/$LT.local.log" > "$OUT/$LT.lua"
    LT=$TAG.hf; ( eval "export $HFENV"; mn local $LT 1500 GE_FRAMES=1700 GE_SNAPRESIM=90,8,31 GE_SNAPCMP=1 )
    norm "$OUT/$LT.local.fp" > "$OUT/$LT.n"; norm "$OUT/$LT.local.fp.resim" > "$OUT/$LT.resim.n"; grep -a '^ge: lua: [TD]' "$OUT/$LT.local.log" > "$OUT/$LT.lua"
    check "lua headline (hostage-flatland: 10 hostages, 20 guards, combat, deaths), local co-op, windows of 8 every 31 ($(cmp_n $OUT/$LT.local.log) rollbacks): byte compare ok, same stream, same script lines" \
        "[ $(cmp_n $OUT/$LT.local.log) -ge 30 ] && cmp_ok $OUT/$LT.local.log && same $OUT/$TAG.hfref.n $OUT/$LT.n && resim_in $OUT/$TAG.hfref.n $OUT/$LT.resim.n && lines_prefix $OUT/$TAG.hfref.lua $OUT/$LT.lua && [ \$(grep -ac 'ge: hostage: spawned chr' $OUT/$LT.local.log) -ge 10 ]"
    check "lua headline: the combat happened (damage lines from the script)" "[ \$(grep -ac '^ge: lua: D ' $OUT/$LT.local.log) -ge 3 ]"
fi

echo "snap_resim: $FAILS failed"
exit $FAILS

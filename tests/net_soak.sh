#!/bin/sh
# Netplay soak / fuzz harness: two Bonds (host + guest, one container, Xvfb) driven by seeded random input and a scripted scenario,
# looking for a DESYNC. Linux build_pc only; nothing here opens a window on the Windows desktop (the Windows exe is never used).
#
# Runs inside the build container as user 1000 (see README-FORK.md for the container and Xvfb recipe; this harness
# uses Xvfb displays :99 :100 :101, start them first):
#   docker exec --user 1000 midas-soak sh /home/dev/project/tests/net_soak.sh matrix
#   docker exec --user 1000 midas-soak sh /home/dev/project/tests/net_soak.sh one <seed> <stage> <lag> <ticks> [slot]
#
#   one      a single pair run: generate the files for <seed> / <stage> (net_soak_gen.py), start host and guest, compare, classify.
#            lag is "none" or a GE_NET_LAG value (80,20,3  200,40,5); slot (default 1) picks the UDP ports, the profiles and the display.
#   matrix   SEEDS x STAGES x LAGS, JOBS pairs in parallel (each worker takes every JOBS-th run of the list).
#   replay   <rundir> [view]: reproduce a saved failing run offline (GE_NET_REPLAY on its host or guest session log; view 0/1)
#
# Environment (matrix and one):
#   SEEDS="1 2 3"  STAGES="dam depot surface flatland"  LAGS="none 80,20,3 200,40,5"  TICKS=3000  JOBS=5
#   SOAK=/home/dev/project/build_tests/soak   output root: results.tsv (one line per run) and one folder per failing run
#   TMO=900     hard kill (seconds) per instance
#   WINDOWS=random|fixed   per-side window / FOV (random from the seed, default) or 320x240 4:3 / 400x225 wide
# Verdicts: PASS, DESYNC (a DESYNC line or desync dump), DIFF (the per-tick fingerprints or session-log T lines differ without a DESYNC
# line), SHORT (an instance died or timed out before the last tick: the Mesa llvmpipe crashes of long dual-instance runs), SETUP.
# A run that is not PASS keeps everything in $SOAK/<run>/: both stdout logs, fingerprint files, session logs, desync dumps, the RNG trace
# ring, the generated records and scenario, the profiles' ini. A PASS keeps one line in results.tsv.
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
ROOT=${ROOT:-$(dirname "$BIN")}
SOAK=${SOAK:-/home/dev/project/build_tests/soak}
HERE=$(dirname "$0")
MODE=${1:-matrix}

hashline() { grep -a 'ge: net: state hash: ' "$1" | tail -n 1; }

mkprof() {   # <profile> <width> <height> <widescreen> <hfov> <auto_aim> <look_ahead>
    mkdir -p "$ROOT/Data/Profiles/$1"
    rm -rf "$ROOT/Data/Profiles/$1/Logs"
    printf '[input]\nlook_updown=1\nauto_aim=%s\naim_control=0\nlook_ahead=%s\n[display]\nfullscreen=windowed\nwindow_width=%s\nwindow_height=%s\nwidescreen=%s\nhfov_wide=%s\nfix_shootthrough=1\n' \
        "$6" "$7" "$2" "$3" "$4" "$5" > "$ROOT/Data/Profiles/$1/midas.ini"
}

one() {
    seed=$1; stage=$2; lag=$3; ticks=$4; slot=${5:-1}
    case "$lag" in none) lt=none ;; *) lt=l$(echo "$lag" | cut -d, -f1) ;; esac
    run=s${seed}_${stage}_${lt}_t${ticks}
    W=$SOAK/work/$run
    rm -rf "$W" "$SOAK/$run"
    mkdir -p "$W"
    python3 "$HERE/net_soak_gen.py" "$seed" "$stage" "$ticks" "$W" || { echo "$run SETUP gen failed"; return 2; }
    if [ -n "$NOWATCH" ]; then   # NOWATCH=1: no Escape / watch_objectives in either record file (isolates the watch from a desync)
        python3 - "$W" <<'PYEOF'
import sys
for f in ("recA", "recB"):
    p = sys.argv[1] + "/" + f + ".txt"
    out = []
    for l in open(p):
        w = l.split()
        w[7] = "%x" % (int(w[7], 16) & ~0x2000)
        w[10] = "0"
        out.append(" ".join(w))
    open(p, "w").write(chr(10).join(out) + chr(10))
PYEOF
    fi
    HPN=sk${slot}h
    GPN=sk${slot}g
    set -- $(grep '^h ' "$W/prof.txt"); mkprof $HPN $2 $3 $4 $5 $6 $7
    set -- $(grep '^g ' "$W/prof.txt"); mkprof $GPN $2 $3 $4 $5 $6 $7
    if [ "$WINDOWS" = fixed ]; then
        mkprof $HPN 320 240 0 90 0 1
        mkprof $GPN 400 225 1 70 0 1
    fi
    port=$((28000 + slot * 10))
    disp=$((99 + (slot % 3)))
    # GE_FRAMES is only a backstop (vsync frames; a held tick still counts one): enough for the lag, and short enough that a stage that ends on its own
    # does not idle for minutes. lag-free runs take ~1.3 frames per tick, 80 ms ~3, 200 ms ~7.5
    case "$lag" in none) fmul=2 ;; 80*) fmul=5 ;; *) fmul=10 ;; esac
    t0=$(date +%s)
    env DISPLAY=:$disp STAGE="$stage" BIN="$BIN" OUT="$W" DOUT="$W" REC="$W" DREC="$W" RECA=recA.txt RECB=recB.txt HPROFILE=$HPN GPROFILE=$GPN \
        TMO=${TMO:-900} sh "$HERE/net_replay2.sh" "$run" "$port" "$ticks" "$lag" GE_NET_LOG=1 GE_NET_RNGTRACE=1 GE_COOP_INVLOG=1 \
        GE_FRAMES=$((ticks * fmul + 3000)) GE_SEED=${GESEED:-7} $(cat "$W/env.txt") > "$W/$run.run.txt" 2>&1
    secs=$(( $(date +%s) - t0 ))
    # collect what the two sides wrote under their profiles
    mkdir -p "$W/logs_h" "$W/logs_g"
    cp "$ROOT/Data/Profiles/$HPN/Logs/"* "$W/logs_h/" 2>/dev/null
    cp "$ROOT/Data/Profiles/$GPN/Logs/"* "$W/logs_g/" 2>/dev/null
    cp "$ROOT/Data/Profiles/$HPN/midas.ini" "$W/host.ini"
    cp "$ROOT/Data/Profiles/$GPN/midas.ini" "$W/guest.ini"
    hl=$W/logs_h/netlog_run1_host.txt
    gl=$W/logs_g/netlog_run1_guest.txt
    nh=$(grep -ac 'ge: coop: fp ' "$W/$run.host.txt" 2>/dev/null)
    ng=$(grep -ac 'ge: coop: fp ' "$W/$run.guest.txt" 2>/dev/null)
    [ -z "$nh" ] && nh=0
    [ -z "$ng" ] && ng=0
    # the session logs' per-tick T lines (this side's fingerprint of each tick), common prefix compared
    grep -a '^T ' "$hl" > "$W/T.host" 2>/dev/null
    grep -a '^T ' "$gl" > "$W/T.guest" 2>/dev/null
    nt=$(wc -l < "$W/T.host"); mt=$(wc -l < "$W/T.guest")
    cm=$nt; [ "$mt" -lt "$cm" ] && cm=$mt
    head -n "$cm" "$W/T.host" > "$W/T.h.cmp"; head -n "$cm" "$W/T.guest" > "$W/T.g.cmp"
    verdict=PASS
    why=
    if grep -aq 'DESYNC' "$W/$run.host.log" "$W/$run.guest.log" || ls "$W"/logs_h/desync_* "$W"/logs_g/desync_* >/dev/null 2>&1; then
        verdict=DESYNC
        why=$(grep -aoh 'DESYNC at tick [0-9]*' "$W/$run.host.log" "$W/$run.guest.log" | sed 's/.*tick //' | sort -n | head -n 1)
    elif [ "$nh" -lt "$ticks" ] || [ "$ng" -lt "$ticks" ]; then
        verdict=SHORT
        why="host $nh guest $ng of $ticks"
        # the mission ended on its own (a mission-complete / failed screen): both sides stop on the same tick with identical fingerprints
        if grep -aq 'ge: net: stage 1 ended' "$W/$run.host.log" && grep -aq 'ge: net: stage 1 ended' "$W/$run.guest.log" && [ "$nh" -eq "$ng" ] \
            && cmp -s "$W/$run.cmp.host" "$W/$run.cmp.guest" && cmp -s "$W/T.h.cmp" "$W/T.g.cmp"; then
            verdict=END
            why="stage ended on its own at tick $nh (not a failure)"
        fi
        if grep -aq 'ge: fatal signal' "$W/$run.host.log" "$W/$run.guest.log"; then
            verdict=CRASH   # a native crash (after the stage ended, or a Mesa llvmpipe one); the first sim-side difference, if any, was checked above
            why="$why; $(grep -ah -A4 'ge: fatal signal' "$W/$run.host.log" "$W/$run.guest.log" | grep -ao 'strcmp\|ProjectMidas\[0x[0-9a-f]*\]\|libGL[^ ]*\|swrast[^ ]*\|llvm[^ ]*' | head -n 3 | tr '
' ' ')"
        fi
    elif ! cmp -s "$W/$run.cmp.host" "$W/$run.cmp.guest"; then
        verdict=DIFF
        why="fingerprints"
    elif ! cmp -s "$W/T.h.cmp" "$W/T.g.cmp" || [ "$cm" -lt "$ticks" ]; then
        verdict=DIFF
        why="T lines ($cm)"
    fi
    hh=$(hashline "$W/$run.host.log" | sed 's/.*state hash: //')
    gh=$(hashline "$W/$run.guest.log" | sed 's/.*state hash: //')
    case "$hh" in *" 0 mismatched"*) ;; *) [ "$verdict" = PASS ] && { verdict=DESYNC; why="hash mismatch: $hh"; } ;; esac
    # coverage numbers for the report
    deaths=$(grep -ac 'player [01] died' "$W/$run.host.fp")
    spawned=$(grep -ac 'ge: arena: spawned guard' "$W/$run.host.log")
    drops=$(grep -ac 'drops p' "$W/$run.host.fp")
    dmg=$(grep -ac 'ge: coop: damage' "$W/$run.host.fp")
    [ -z "$deaths" ] && deaths=0
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%ss\n' "$run" "$seed" "$stage" "$lag" "$ticks" "$verdict" "$nh" "$ng" \
        "$deaths" "$spawned" "$drops" "$dmg" "$why" "$secs" >> "$SOAK/results.tsv"
    echo "$run $verdict host=$nh guest=$ng deaths=$deaths spawned=$spawned drops=$drops dmg=$dmg ${why:+($why)} ${secs}s"
    if [ "$verdict" != PASS ]; then
        mkdir -p "$SOAK/$run"
        cp -r "$W"/. "$SOAK/$run/"
        ls "$ROOT/Data/Profiles/$HPN/Logs/" "$ROOT/Data/Profiles/$GPN/Logs/" > "$SOAK/$run/logs.ls" 2>&1
    fi
    if [ -z "$KEEP" ]; then rm -rf "$W"; fi   # KEEP=1 keeps the work folder of a PASS too (work/<run>)
    [ "$verdict" = PASS ]
}

replay() {
    d=$1; view=${2:-0}; side=${3:-}   # side h|g: which side's session log (default: the host's for view 0, the guest's for view 1); h with view 1 = the cross replay
    run=$(basename "$d")
    n=$(ls "$d"/logs_h/netlog_run1_host.txt 2>/dev/null)
    [ "$view" = 1 ] && n=$d/logs_g/netlog_run1_guest.txt
    [ "$view" = 0 ] && n=$d/logs_h/netlog_run1_host.txt
    [ "$side" = h ] && n=$d/logs_h/netlog_run1_host.txt
    [ "$side" = g ] && n=$d/logs_g/netlog_run1_guest.txt
    prof=$(basename "$ROOT")_replay
    mkdir -p "$ROOT/Data/Profiles/rp$view"
    cp "$d/host.ini" "$ROOT/Data/Profiles/rp$view/midas.ini"
    [ "$view" = 1 ] && cp "$d/guest.ini" "$ROOT/Data/Profiles/rp$view/midas.ini"
    # the scenario hooks that are not in the session log (teleports and door toggles) must be set again
    for kv in $(cat "$d/env.txt"); do
        case "$kv" in GE_ROOMTEST_*) export "$kv" ;; esac
    done
    GE_NET_RNGTRACE=1 DISPLAY=:${DISPLAY_N:-99} OUT=$d/replay$view.out sh "$HERE/net_replay_log.sh" "$n" "$view" "rp$view"
}

matrix() {
    SEEDS=${SEEDS:-"1 2 3"}
    STAGES=${STAGES:-"dam depot surface flatland"}
    LAGS=${LAGS:-"none 80,20,3 200,40,5"}
    TICKS=${TICKS:-3000}
    JOBS=${JOBS:-5}
    mkdir -p "$SOAK/work"
    list=$SOAK/work/list.$$
    : > "$list"
    for s in $SEEDS; do for st in $STAGES; do for l in $LAGS; do echo "$s $st $l" >> "$list"; done; done; done
    total=$(wc -l < "$list")
    echo "matrix: $total runs of $TICKS ticks, $JOBS in parallel"
    w=1
    while [ "$w" -le "$JOBS" ]; do
        (
            i=0
            while read -r s st l; do
                i=$((i + 1))
                if [ $(( (i - 1) % JOBS + 1 )) -eq "$w" ]; then
                    one "$s" "$st" "$l" "$TICKS" "$w"
                fi
            done < "$list"
        ) &
        w=$((w + 1))
    done
    wait
    rm -f "$list"
    echo "matrix done: $(grep -c PASS "$SOAK/results.tsv") PASS lines in $SOAK/results.tsv"
}

mkdir -p "$SOAK"
case "$MODE" in
    one) shift; one "$@" ;;
    replay) shift; replay "$@" ;;
    matrix) matrix ;;
    *) echo "usage: net_soak.sh one <seed> <stage> <lag> <ticks> [slot] | matrix | replay <rundir> [view]"; exit 2 ;;
esac

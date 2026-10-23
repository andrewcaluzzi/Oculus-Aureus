#!/bin/sh
# Find where a live netplay session first went wrong, from the two sides' session logs ("Tracing a desync").
#
#   net_bisect.sh <host-log> <guest-log> [profile] [outdir]
#     host-log, guest-log   Data/Profiles/<name>/Logs/netlog_run<N>_host.txt and ..._guest.txt of one GE_NET_LOG=1 session
#     profile               the profile whose midas.ini matches the session (default: the exe's own ini; the replay needs the same
#                           ini, the sim-settings line of the log is checked and a difference is named)
#     outdir                where the report goes (default: bisect.<host-log name>/ next to the host log)
# Environment: BIN (default /home/dev/project/build_pc/ProjectMidas), DISPLAY for the window (Xvfb is fine).
#
# What it does:
#   1. Compares the two logs' per-tick fingerprints: the first tick where HOST and GUEST differ, and which sub-hashes (rng, players,
#      chrs, chrai, objs, proj, doors, props, flags, onscreen, clock, settings) differ there.
#   2. Replays each log offline as local co-op (net_replay_log.sh; the sim runs the logged records, nothing live): the first tick where
#      the replay differs from THAT SIDE'S OWN log. A live side that does not replay is the side whose machine read something the
#      records do not carry (a live value, the window, the render pass). If both replay, the divergence is in the records' timing.
#   3. Takes F = the earliest of those ticks and writes the full state dump (every sub-hash, every chr, every chr's AI state, every
#      player, the RNG with its draw count, objects, projectiles, onscreen flags) of the tick before and at F from each replay, plus
#      the sim RNG trace around F (GE_NET_RNGTRACE) with the call sites named.
# Output: <outdir>/summary.txt (read this first), dump_host.txt, dump_guest.txt (replay state dumps), rngtrace_host.txt,
# rngtrace_guest.txt (each replay's draws; net_rngdiff.sh compares them), the replays' full output.
HOSTLOG=$1
GUESTLOG=$2
PROFILE=${3:-}
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
HERE=$(dirname "$0")
if [ ! -f "$HOSTLOG" ] || [ ! -f "$GUESTLOG" ]; then
    echo "usage: net_bisect.sh <host-log> <guest-log> [profile] [outdir]"
    exit 2
fi
OUTDIR=${4:-$(dirname "$HOSTLOG")/bisect.$(basename "$HOSTLOG" .txt)}
mkdir -p "$OUTDIR"
SUM="$OUTDIR/summary.txt"
FIELDS="rng players chrs chrai objs proj doors props flags onscreen clock settings"
: > "$SUM"
say() { echo "$*" | tee -a "$SUM"; }

# 1. host log against guest log
D=$(awk 'NR == FNR { if ($1 == "T") h[$2] = $5; next } $1 == "T" { if (!($2 in h)) { print "short " $2; exit } if (h[$2] != $5) { print $2; exit } }' "$HOSTLOG" "$GUESTLOG")
say "net_bisect: host log $HOSTLOG"
say "            guest log $GUESTLOG"
case "$D" in
    "") say "1. the two logs' fingerprints agree on every tick they share: no desync was recorded";;
    short*) say "1. the guest log is shorter than the host's: it ends at tick $(( ${D#short } - 1 ))";;
    *)
        say "1. the logs first differ at tick $D"
        hl=$(awk -v t="$D" '$1 == "T" && $2 == t { for (i = 6; i <= NF; i++) printf "%s ", $i; print "" }' "$HOSTLOG")
        gl=$(awk -v t="$D" '$1 == "T" && $2 == t { for (i = 6; i <= NF; i++) printf "%s ", $i; print "" }' "$GUESTLOG")
        names=$(awk -v a="$hl" -v b="$gl" -v n="$FIELDS" 'BEGIN { na = split(a, x, " "); split(b, y, " "); split(n, nm, " "); for (i = 1; i <= na; i++) if (x[i] != y[i]) printf "%s ", (i <= 12 ? nm[i] : "field" i) }')
        say "   sub-hashes that differ at that tick: ${names:-(none: only the total)}"
        say "   host  $hl"
        say "   guest $gl"
        ;;
esac

# 2. each log replayed offline, in the view of the machine that wrote it
replay() {   # <role> <log> <view>
    role=$1; log=$2; view=$3
    out="$OUTDIR/replay_$role.out"
    env BIN="$BIN" OUT="$out" GE_NET_RNGTRACE=1 sh "$HERE/net_replay_log.sh" "$log" "$view" "$PROFILE" > "$OUTDIR/replay_$role.txt" 2>&1
    first=$(sed -n 's/.*ge: replay: first mismatch at tick \([0-9]*\).*/\1/p' "$out" | head -n 1)
    echo "$first"
}
verdict() { if [ -n "$1" ]; then echo "first MISMATCH at tick $1 (this side's live sim differed from the sim of its own logged records)"; else echo "reproduces every tick of its own log"; fi; }
FH=$(replay host "$HOSTLOG" 0)
FG=$(replay guest "$GUESTLOG" 1)
say "2. offline replay of the host log (view 0): $(verdict "$FH")"
[ -n "$FH" ] && grep -a 'ge: replay: MISMATCH' "$OUTDIR/replay_host.out" | head -n 3 | sed 's/^/   /' | tee -a "$SUM"
say "   offline replay of the guest log (view 1): $(verdict "$FG")"
[ -n "$FG" ] && grep -a 'ge: replay: MISMATCH' "$OUTDIR/replay_guest.out" | head -n 3 | sed 's/^/   /' | tee -a "$SUM"
grep -a 'WARNING the sim settings' "$OUTDIR/replay_host.out" "$OUTDIR/replay_guest.out" | sed 's/^/   /' | tee -a "$SUM"

# 3. F = the earliest suspicious tick
F=
for t in "$FH" "$FG" "$(echo "$D" | grep -E '^[0-9]+$')"; do
    [ -z "$t" ] && continue
    if [ -z "$F" ] || [ "$t" -lt "$F" ]; then F=$t; fi
done
if [ -z "$F" ]; then
    say "3. nothing differs: no state dump needed"
    exit 0
fi
FROM=$((F > 1 ? F - 1 : 1))
say "3. first suspicious tick: $F. State dumps of ticks $FROM-$F: dump_host.txt / dump_guest.txt (the replays' own sim, fields and every chr / player / object)"
for spec in "host $HOSTLOG 0" "guest $GUESTLOG 1"; do
    set -- $spec
    env BIN="$BIN" OUT="$OUTDIR/dump_$1.out" GE_NET_REPLAY_STOP="$((F + 1))" GE_COOP_STATEDUMP="$FROM-$F" GE_NET_RNGTRACE=1 sh "$HERE/net_replay_log.sh" "$2" "$3" "$PROFILE" > /dev/null 2>&1
    awk '/ge: coop: statedump tick/ { p = 1 } p { print } /ge: replay: GE_NET_REPLAY_STOP/ { p = 0 }' "$OUTDIR/dump_$1.out" > "$OUTDIR/dump_$1.txt"
    rm -f "$OUTDIR/dump_$1.out"
    # this replay's RNG trace (the profile's Logs folder, or the exe's)
    ROOT=$(dirname "$BIN")
    LOGS="$ROOT/Data/Logs"
    [ -n "$PROFILE" ] && LOGS="$ROOT/Data/Profiles/$PROFILE/Logs"
    if [ -f "$LOGS/rngtrace_run1_replay.txt" ]; then
        cp "$LOGS/rngtrace_run1_replay.txt" "$OUTDIR/rngtrace_$1.txt"
        say "   RNG draws of tick $F in the $1 replay, by call site (rngtrace_$1.txt):"
        sh "$HERE/net_rngsym.sh" "$OUTDIR/rngtrace_$1.txt" "$BIN" "$F" "$F" 2>/dev/null | awk '{ c[$5 " " $6]++ } END { for (k in c) printf "     %s x%d\n", k, c[k] }' | sort | tee -a "$SUM"
    fi
done
say "Compare against the live side's own desync_<N>.txt (the 30-tick dump the game wrote) with diff; a draw-count mismatch is named by"
say "net_rngdiff.sh on two GE_NET_RNGTRACE=1 runs."

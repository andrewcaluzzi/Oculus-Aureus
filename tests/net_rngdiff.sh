#!/bin/sh
# Compare two GE_NET_RNGTRACE=1 traces ("Tracing a desync") and print the first sim RNG draw that differs.
#
#   net_rngdiff.sh <traceA> <traceB> [exe]
#     traceA, traceB   Data/Profiles/<name>/Logs/rngtrace_run<N>_<host|guest|replay>.txt of two runs of the same session
#     exe              the binary that wrote them: names the call sites with net_rngsym.sh (nm + addr2line); optional
# A trace line is "<tick> <S|C> <n> p<player> <caller offset> <value>" (S = sim RNG, C = chr/obj RNG, n = draw number since the
# stage reseed). Two lines are the same draw when tick, kind, n, caller and value all agree (the player column is only context:
# the pass order differs between machines' views and that is not a difference).
# Prints the first difference with the draws just before it, and how many draws each side made in that tick. Exit 0 = the traces
# are identical, 1 = they differ, 2 = usage.
A=$1
B=$2
EXE=$3
HERE=$(dirname "$0")
if [ ! -f "$A" ] || [ ! -f "$B" ]; then
    echo "usage: net_rngdiff.sh <traceA> <traceB> [exe]"
    exit 2
fi
RES=$(mktemp)
awk -v A="$A" -v B="$B" '
function same(x, y,   nx, ny, fx, fy) {
    nx = split(x, fx, " "); ny = split(y, fy, " ")
    return fx[1] == fy[1] && fx[2] == fy[2] && fx[3] == fy[3] && fx[5] == fy[5] && fx[6] == fy[6]
}
BEGIN {
    n = 0; ctx = 0
    while (1) {
        ra = (getline la < A); rb = (getline lb < B)
        while (ra > 0 && la ~ /^#/) ra = (getline la < A)
        while (rb > 0 && lb ~ /^#/) rb = (getline lb < B)
        if (ra <= 0 && rb <= 0) { print "IDENTICAL " n; exit 0 }
        n++
        if (ra <= 0 || rb <= 0 || !same(la, lb)) {
            print "FIRST " n
            print "A " (ra > 0 ? la : "(trace A ends here)")
            print "B " (rb > 0 ? lb : "(trace B ends here)")
            for (i = ctx; i >= 1; i--) print "BEFORE " hist[(n - i) % 8]
            split((ra > 0 ? la : lb), f, " ")
            print "TICK " f[1]
            exit 1
        }
        hist[n % 8] = la
        if (ctx < 7) ctx++
    }
}' > "$RES"
rc=$?
head -n 1 "$RES" | grep -q '^IDENTICAL' && { echo "net_rngdiff: the traces are identical ($(head -n 1 "$RES" | cut -d' ' -f2) draws)"; rm -f "$RES"; exit 0; }
n=$(sed -n 's/^FIRST //p' "$RES")
tick=$(sed -n 's/^TICK //p' "$RES")
echo "net_rngdiff: the traces first differ at draw #$n, tick $tick"
sed -n 's/^BEFORE /  same: /p' "$RES"
echo "  A:    $(sed -n 's/^A //p' "$RES")"
echo "  B:    $(sed -n 's/^B //p' "$RES")"
ca=$(awk -v t="$tick" '/^#/ {next} $1 == t {c++} END {print c + 0}' "$A")
cb=$(awk -v t="$tick" '/^#/ {next} $1 == t {c++} END {print c + 0}' "$B")
echo "  draws in tick $tick: A made $ca, B made $cb"
if [ -n "$EXE" ] && [ -f "$EXE" ]; then
    echo "  call sites in tick $tick (A, then B):"
    sh "$HERE/net_rngsym.sh" "$A" "$EXE" "$tick" "$tick" | sed 's/^/    A /'
    sh "$HERE/net_rngsym.sh" "$B" "$EXE" "$tick" "$tick" | sed 's/^/    B /'
fi
rm -f "$RES"
exit 1

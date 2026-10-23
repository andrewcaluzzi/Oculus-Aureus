#!/bin/sh
# Name the call sites in a GE_NET_RNGTRACE=1 trace ("Tracing a desync").
#
#   net_rngsym.sh <trace> <exe> [from-tick [to-tick]]
#     trace    Data/Profiles/<name>/Logs/rngtrace_run<N>_<host|guest|replay>.txt
#     exe      the binary that wrote it (same build; Windows exe: set ADDR2LINE=i686-w64-mingw32-addr2line)
# Prints "tick kind n player function file:line" for the draws in the tick range (default: all, which is a lot).
# A trace line holds the caller as an offset from randomGetNext; its header holds randomGetNext's address in the process that
# wrote it (an exe without ASLR, or a relocated one, is handled the same way: the offset is relative).
# Needs the exe's own symbols: nm (to find randomGetNext in the file) and addr2line.
TRACE=$1
EXE=$2
FROM=${3:-0}
TO=${4:-999999999}
A2L=${ADDR2LINE:-addr2line}
NM=${NM:-nm}
if [ ! -f "$TRACE" ] || [ ! -f "$EXE" ]; then
    echo "usage: net_rngsym.sh <trace> <exe> [from-tick [to-tick]]"
    exit 2
fi
# randomGetNext in the exe file (static address) and in the process that wrote the trace: a relocated exe differs by a constant
FILEREF=$($NM "$EXE" | awk '$3 == "randomGetNext" {print $1; exit}')
PROCREF=$(awk '/^# ref randomGetNext/ {print $4; exit}' "$TRACE")
if [ -z "$FILEREF" ] || [ -z "$PROCREF" ]; then
    echo "net_rngsym: cannot find randomGetNext in $EXE or the trace header"
    exit 2
fi
LIST=$(mktemp)
awk -v from="$FROM" -v to="$TO" -v ref="$FILEREF" '
function hex(s,   i, c, v, d) { v = 0; s = tolower(s); for (i = 1; i <= length(s); i++) { c = substr(s, i, 1); d = index("0123456789abcdef", c) - 1; v = v * 16 + d } return v }
BEGIN { base = hex(ref) }
/^#/ { next }
$1 + 0 >= from && $1 + 0 <= to { off[$5] = 1 }
END { for (o in off) printf "%x\n", base + o }' "$TRACE" | sort -u > "$LIST"
RES=$(mktemp)
$A2L -f -C -e "$EXE" $(cat "$LIST") > "$RES"
paste -d' ' "$LIST" - - < "$RES" > "$LIST.map"
awk -v from="$FROM" -v to="$TO" -v ref="$FILEREF" '
function hex(s,   i, c, v, d) { v = 0; s = tolower(s); for (i = 1; i <= length(s); i++) { c = substr(s, i, 1); d = index("0123456789abcdef", c) - 1; v = v * 16 + d } return v }
BEGIN { base = hex(ref) }
FNR == NR { m[$1] = $2 " " $3; next }
/^#/ { next }
$1 + 0 >= from && $1 + 0 <= to { printf "%s %s %s %s %s\n", $1, $2, $3, $4, m[sprintf("%x", base + $5)] }' "$LIST.map" "$TRACE"
rm -f "$LIST" "$RES" "$LIST.map"

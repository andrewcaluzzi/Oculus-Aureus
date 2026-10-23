#!/bin/sh
# Replay a netplay session log (GE_NET_LOG=1) offline as local co-op and compare it with what the live side logged.
# The core netplay debugging tool
#
#   net_replay_log.sh <session-log> [view 0|1] [profile]
#     session-log   Data/Profiles/<name>/Logs/netlog_run<N>_<host|guest>.txt of a GE_NET_LOG=1 session
#     view          which Bond this window plays (0 = host's, 1 = guest's); default 0. The sim must not depend on it
#     profile       the profile whose ini the session used (the log records the modifiers, not the profile); default: the exe's own ini
#   Environment: BIN (default /home/dev/project/build_pc/ProjectMidas), OUT (default /tmp/net_replay_log.out: the full game output),
#   GE_NET_REPLAY_STOP=<tick> to stop early, GE_NET_LOG=1 to write the replay's own log (netlog_run1_replay.txt) for a line-by-line
#   diff against the original, GE_COOP_STATEDUMP=<tick> or <from>-<to> to print the world dump of those ticks (to the fp file when
#   GE_COOP_FPFILE is set, else to stdout) when hunting the first differing field.
# Prints the replay's verdict ("ge: replay: OK ..." or "FAILED" with the first mismatching ticks) and exits 0 / 1.
LOGFILE=$1
VIEW=${2:-0}
PROFILE=${3:-}
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
OUT=${OUT:-/tmp/net_replay_log.out}
if [ ! -f "$LOGFILE" ]; then
    echo "net_replay_log: no such log: $LOGFILE"
    exit 2
fi
if [ -n "$PROFILE" ]; then export GE_PROFILE="$PROFILE"; fi
export GE_NOINISAVE=1
export GE_FRAMES=${GE_FRAMES:-100000}
export GE_NET_REPLAY="$LOGFILE"
export GE_NET_REPLAY_VIEW="$VIEW"
export GE_COOP_LOG=1
"$BIN" > "$OUT" 2>&1
grep -a 'ge: replay\|ge: net: sim reads\|is blocked in netplay' "$OUT"
grep -aq 'ge: replay: OK' "$OUT"

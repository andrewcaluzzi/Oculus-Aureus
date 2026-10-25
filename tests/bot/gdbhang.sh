#!/bin/sh
# Debug aid: run a dev bot run under gdb, interrupt it after <secs> and print every thread's stack.
#   sh tests/bot/gdbhang.sh <stage> <secs> [KEY=VALUE ...]
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ST=$1; shift
SECS=$1; shift
cd "$ROOT/${BUILD:-build_pc}" || exit 2
cat > /tmp/gdbcmds <<EOF
handle SIGPIPE nostop noprint
run
thread apply all bt 25
kill
quit
EOF
( sleep "$SECS"; pkill -INT -f "build_pc/ProjectMidas|^./ProjectMidas" ) &
env DISPLAY=${DISPLAY:-:99} GE_NOINISAVE=1 GE_TICKHZ=60 GE_STAGE="$ST" GE_DIFF=${DIFF:-0} \
    GE_PLAYERBOT=1 GE_PLAYERBOT_PLANDIR="$ROOT/tools/bot/plans" GE_PLAYERBOT_MAXSECS=600 \
    GE_NOSLEEP=1 GE_FIXEDTICK=1 GE_CLOCKSCALE=20 GE_SIMONLY_TEST=2,100000000 GE_FRAMES=2000000000 "$@" \
    gdb -q -batch -x /tmp/gdbcmds ./ProjectMidas 2>&1 | grep -v "^\[New Thread\|^\[Thread\|^ge: romdata\|^gfx" | tail -80

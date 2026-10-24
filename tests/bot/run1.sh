#!/bin/sh
# Generic one-shot runner used while developing: runs build_pc/ProjectMidas with the
# environment given on the command line (KEY=VALUE ...), log to $LOG.
# Usage (in the container): sh tests/bot/run1.sh <log> KEY=VALUE ...
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LOG=$1; shift
cd "$ROOT/${BUILD:-build_pc}" || exit 2
start=$(date +%s)
env DISPLAY=${DISPLAY:-:99} GE_NOINISAVE=1 GE_TICKHZ=60 "$@" ./ProjectMidas > "$LOG" 2>&1
rc=$?
end=$(date +%s)
echo "rc=$rc wall=$((end - start))s"
exit $rc

#!/bin/sh
# Netplay job 3b, test only: live key, mouse-button, wheel and pointer events at the GUEST's window while a netplay session runs
# (the "leak test", net_lockstep.sh phase 7). The events are real X events (xdotool, XTEST) that SDL turns into key presses and
# mouse input exactly as a human's would be, but the guest's records come from a scripted file, so NOTHING typed here may change
# the sim on either machine. If any live read still reaches the sim outside the record, the guest diverges from the host and the
# 30-tick state hash reports DESYNC.
#   net_leak_keys.sh <guest-log> [max-seconds]
# Waits for "ge: net: GO" in the guest's log (the stage is running), then cycles through the keys the game reads until the guest
# process is gone or the time is up. The newest ProjectMidas process is the guest (the host was started first).
# Needs DISPLAY and xdotool; Linux build_pc + Xvfb only.
LOGF=$1
MAXS=${2:-120}
i=0
while ! grep -aq 'ge: net: GO' "$LOGF" 2>/dev/null; do
    sleep 0.2
    i=$((i + 1))
    if [ $i -gt 600 ]; then echo "net_leak_keys: no GO in $LOGF"; exit 1; fi
done
PID=$(pgrep -n -f ProjectMidas)
[ -n "$PID" ] || { echo "net_leak_keys: no game process"; exit 1; }
WIN=$(xdotool search --pid "$PID" | tail -n 1)
[ -n "$WIN" ] || { echo "net_leak_keys: no window for pid $PID"; exit 1; }
echo "net_leak_keys: guest pid $PID window $WIN"
xdotool windowfocus "$WIN"
xdotool mousemove 640 360
# Escape (watch), Tab (classic mode), Return (START / confirm), the move and look keys, reload / use / inventory binds, the number
# row, arrows, F1 (mouse unlock), modifiers, Backspace / Delete; then the three mouse buttons, the wheel and some pointer travel.
# One xdotool process per round (a process per key is too slow to matter): 35 keys 25 ms apart, then the mouse.
KEYS="Escape Tab Return w a s d r e q bracketleft comma period space 1 2 3 4 5 6 7 8 9 Left Right Up Down F1 Control_L Shift_L BackSpace Delete"
N=0
START=$(date +%s)
while kill -0 "$PID" 2>/dev/null; do
    xdotool key --delay 25 $KEYS
    N=$((N + 35))
    echo "net_leak_keys: round $((N / 35)) done ($N key events so far)"
    xdotool click 4 click 5 click 4 click 4 click 5 mousemove_relative -- 37 -23 click 1 click 3 click 2 mousemove_relative -- -37 23
    NOW=$(date +%s)
    if [ $((NOW - START)) -gt "$MAXS" ]; then break; fi
done
echo "net_leak_keys: $N key events sent"

#!/bin/sh
# Netplay job 6: net_graph screenshots of a lagged two-instance session (build_pc + Xvfb, one container).
#   wsl -d Ubuntu -- docker exec --user 1000 -e DISPLAY=:99 midas-net sh /home/dev/project/tests/net_graph_shots.sh <tag> <port> <level> [window WxH] [lag] [ticks]
#     tag     output prefix; the BMPs land in build_tests/net6/<tag>/{host,guest}/shot_*.bmp (GE_SHOT every 40 frames)
#     level   GE_NET_GRAPH 0..3 (0 = off, for the control)
#     window  GE_WINDOW (default 960x540)
#     lag     GE_NET_LAG (default 80,20,3)
#     ticks   GE_NET_STOP (default 420)
# Replays the committed recA/recB inputs (the Bonds walk about on Dam), like net_replay2.sh.
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
OUT=${OUT:-/home/dev/project/build_tests/net6}
REC=${REC:-/home/dev/project/tests/net}
TAG=${1:-g}
PORT=${2:-27061}
LEVEL=${3:-2}
WIN=${4:-960x540}
LAG=${5:-80,20,3}
STOP=${6:-420}

mkdir -p "$OUT/$TAG/host" "$OUT/$TAG/guest"
rm -f "$OUT/$TAG"/host/shot_*.bmp "$OUT/$TAG"/guest/shot_*.bmp

COMMON="GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_STAGE_AFTER=20 GE_DIFF=1 GE_MODS=invincible=0,enemy_damage=400 GE_FRAMES=3000 GE_NET_STOP=$STOP GE_NET_LAG=$LAG GE_NET_GRAPH=$LEVEL GE_WINDOW=$WIN GE_SHOT=40 GE_SEED=7 GE_STAGE=dam $EXTRA"

(
    cd "$OUT/$TAG/host" || exit 1
    env $COMMON GE_PROFILE=nethost GE_NET=host:$PORT GE_COOP_LOCAL=replay:$REC/recA.txt \
        timeout -s KILL 300 "$BIN" > "$OUT/$TAG.host.log" 2>&1
) &
HP=$!
sleep 2
(
    cd "$OUT/$TAG/guest" || exit 1
    env $COMMON GE_PROFILE=netguest GE_NET=join:127.0.0.1:$PORT GE_COOP_LOCAL=replay:$REC/recB.txt \
        timeout -s KILL 300 "$BIN" > "$OUT/$TAG.guest.log" 2>&1
) &
GP=$!
wait $HP
wait $GP
echo "$TAG: host $(ls "$OUT/$TAG/host" | wc -l) shots, guest $(ls "$OUT/$TAG/guest" | wc -l) shots; DESYNC lines: $(grep -ac 'DESYNC' "$OUT/$TAG.host.log")"

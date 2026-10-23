#!/bin/sh
# Netplay job 3e: the watch affects only its opener (build_pc + Xvfb, one local co-op process, no netplay; the netplay side
# is net_lockstep.sh phase 19).
#   wsl -d Ubuntu -- docker exec --user 1000 -e DISPLAY=:99 midas-net sh /home/dev/project/tests/coop_watch.sh <scenario> [outdir]
# Bond 0 (the puppet / opener) presses START at tick 12 and leaves its watch open (state 5 from about tick 130). Bond 1 is the
# local Bond (view 1: its window is what the BMPs show), on the Dam start next to the Dam guards:
#   late    Bond 1 stands still facing the first Dam guard and holds fire from tick 30 (a reload every 70 ticks; the guard has
#           4x health and Bond is invincible, so the fight lasts): from tick ~140 on every shot lands WHILE Bond 0's watch is open
#           (state 5 from about tick 130). Hits print as "ge: hit: chr N ... by player 1 at timer T".
#   both    both Bonds open the watch (see below), view 1 shows Bond 1's watch alone: the layering check
#   walk    Bond 1 walks forward for $WALK ticks (default 250) and then fires: the old bug showed sky only here.
# The scenario name picks nothing else: WATCH=0 (docker exec -e WATCH=0) is the control without a watch; EXTRA='K=V ...' adds
# environment (GE_WATCH_SHAREDGATE=1 = the pre-3e shared world gate, the negative control). Output: <outdir>/<tag>.fp (the
# fingerprint lines: watch states, magazines), <tag>.raw (stdout), <tag>/shot_*.bmp (every 20 frames, Bond 1's view).
BIN=/home/dev/project/build_pc/ProjectMidas
MODE=${1:-late}
TAG=${TAG:-$MODE}
OUT=${2:-/home/dev/project/build_tests/net3e}
mkdir -p "$OUT/$TAG"
awk -v w="${WATCH:-1}" 'BEGIN { for (t = 1; t <= 900; t++) { b = 0; if (w == 1 && t >= 12 && t <= 13) b = 4096; printf "R %d %x 0 0 0 0 0 0 0 0\n", t, b } }' > "$OUT/$TAG.rec0.txt"
if [ "$MODE" = late ]; then
    awk 'BEGIN { for (t = 1; t <= 900; t++) { b = 0; yaw = 0; act = 0
        if (t >= 30) b = 8192
        if (t >= 100 && (t % 70) < 2) act = 1
        printf "R %d %x 0 0 %d 0 %x 0 0 0\n", t, b, yaw, act } }' > "$OUT/$TAG.rec1.txt"
elif [ "$MODE" = both ]; then
    # both Bonds open their watch (P0 at tick 12, P1 at tick 40; P1 also turns to the next watch page at tick 230): each view must show
    # its own Bond's watch only (P0's stays on the status page, P1's moves on), a normal single watch, no layering
    awk 'BEGIN { for (t = 1; t <= 900; t++) { b = 0; act = 0; if (t >= 40 && t <= 41) b = 4096; if (t >= 230 && t <= 231) act = 4096; printf "R %d %x 0 0 0 0 %x 0 0 0\n", t, b, act } }' > "$OUT/$TAG.rec1.txt"
else
    awk -v walk="${WALK:-250}" 'BEGIN { for (t = 1; t <= 900; t++) { b = 0; sy = 0; if (t >= 30 && t < 30 + walk) sy = 70; else if (t >= 30 + walk) b = 8192; printf "R %d %x 0 %d 0 0 0 0 0 0\n", t, b, sy } }' > "$OUT/$TAG.rec1.txt"
fi
cd "$OUT/$TAG" || exit 1
env GE_COOP=1 GE_SKIPINTRO=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_NOINISAVE=1 GE_COOP_LOG=1 GE_COOP_FP=1 GE_HITLOG=1 GE_MODS=invincible=1,enemy_health=400 \
    GE_STAGE=dam GE_SPAWNXYZ=19300,-100,17300,53 GE_COOP_VIEW=1 GE_COOP_LOCAL=replay:$OUT/$TAG.rec1.txt GE_COOP_P2=replay:$OUT/$TAG.rec0.txt \
    $EXTRA GE_SHOT=20 GE_FRAMES=${FRAMES:-560} GE_COOP_FPFILE="$OUT/$TAG.fp" "$BIN" > "$OUT/$TAG.raw" 2>&1
echo "done $TAG: $(grep -ac 'ge: hit:' "$OUT/$TAG.raw") hits"

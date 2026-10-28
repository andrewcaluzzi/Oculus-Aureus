#!/bin/sh
# Netplay job 2 check: two instances (host = player 0, guest = player 1) each replay a recorded input file for their
# local Bond over the lockstep link and must finish N ticks with identical per-tick fingerprints.
#
# Runs inside the build container as user 1000 with DISPLAY set (build_pc + Xvfb, README-FORK.md):
#   wsl -d Ubuntu -- docker exec --user 1000 -e DISPLAY=:99 midas-net sh /home/dev/project/tests/net_replay2.sh <tag> <port> <ticks> [lag]
#     tag    output file prefix in build_tests/net2/
#     port   UDP port (use a new one per run)
#     ticks  GE_NET_STOP: both instances leave after this many sim ticks (default 200)
#     lag    optional GE_NET_LAG value on both sides, e.g. 100,20,5 ("none" for no lag when passing extra env)
#     more   optional KEY=VALUE environment for both instances (GE_DIFF=3 GE_SPAWNXYZ=19300,-100,17300,53 ...)
# HPROFILE / GPROFILE (environment) name the host's and guest's profiles (default nethost / netguest); tests/net_lockstep.sh
# runs the job 3 checks on top of this script.
# The stage defaults to dam; set STAGE=depot (docker exec -e STAGE=depot) for another one.
# Replays tests/net/recA.txt (host's local input) and recB.txt (guest's): recorded on Dam with GE_COOP_REC,
# The output goes to build_tests/net2/.
# Exit status: 0 = fingerprints identical over the common prefix and at least <ticks> ticks, 1 otherwise.
#
# GE_FRAMES is only a backstop here: it counts vsync frames, and a held tick still counts one. Rendered frames stay near
# <ticks> plus the menu ticks, which keeps a llvmpipe run short enough (long two-instance runs can crash inside Mesa).
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
OUT=${OUT:-/home/dev/project/build_tests/net2}
DOUT=${DOUT:-$OUT}   # the same directory as the game process sees it (a Windows exe wants C:/... paths)
REC=${REC:-/home/dev/project/tests/net}   # recA.txt (host's local input) and recB.txt (guest's): recorded inputs, not shipped (see tests/README.md)
DREC=${DREC:-$REC}   # REC as the game process sees it
TAG=${1:-run}
PORT=${2:-27031}
STOP=${3:-200}
LAG=${4:-}
if [ $# -gt 4 ]; then shift 4; else shift $#; fi
EXTRA="$*"   # more KEY=VALUE environment for both instances, e.g. GE_DIFF=3 GE_SPAWNXYZ=19300,-100,17300,53

COMMON="GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_STAGE_AFTER=20 GE_DIFF=1 GE_MODS=invincible=0,enemy_damage=400 GE_COOP_LOG=1 GE_COOP_FP=1 GE_COOP_CLOCKDBG=1 GE_FRAMES=3000 GE_NET_STOP=$STOP"
if [ -n "$LAG" ] && [ "$LAG" != none ]; then COMMON="$COMMON GE_NET_LAG=$LAG"; fi
COMMON="$COMMON $EXTRA"

# Each instance replays its own recorded file for its local Bond. LIVE=1 (docker exec -e LIVE=1) leaves the local Bond on
# the live devices instead, which exercises the capture-and-schedule path; HOSTENV / GUESTENV add environment to one side only
# (for example HOSTENV=GE_FAKEMOUSE=40,0,100,100000 to turn the host's mouse).
if [ -n "$LIVE" ]; then
    HLOC=""
    GLOC=""
else
    HLOC="GE_COOP_LOCAL=replay:$DREC/${RECA:-recA.txt}"   # RECA / RECB name other input files (job 3b: recEA.txt / recEB.txt)
    GLOC="GE_COOP_LOCAL=replay:$DREC/${RECB:-recB.txt}"
    # A missing file is not an error to the game: it logs "cannot open the replay file" and that Bond stands still, so both
    # machines stay in sync and every hash check passes while the scripted input never happens. Refuse to run instead.
    for f in "${RECA:-recA.txt}" "${RECB:-recB.txt}"; do
        if [ ! -s "$REC/$f" ]; then
            echo "$TAG: MISSING input file $REC/$f (see tests/README.md; REC= names another folder)"
            exit 1
        fi
    done
fi

rm -f "$OUT/$TAG.host.fp" "$OUT/$TAG.guest.fp"

env ${HCWD:+-C "$HCWD"} $COMMON GE_PROFILE=${HPROFILE:-nethost} GE_NET=host:$PORT GE_STAGE=${STAGE:-dam} GE_SEED=7 \
    $HLOC $HOSTENV GE_COOP_FPFILE="$DOUT/$TAG.host.fp" \
    timeout -s KILL ${TMO:-300} "$BIN" > "$OUT/$TAG.host.log" 2>&1 &
HP=$!
sleep 2
env ${GCWD:+-C "$GCWD"} $COMMON GE_PROFILE=${GPROFILE:-netguest} GE_NET=join:127.0.0.1:$PORT \
    $GLOC $GUESTENV GE_COOP_FPFILE="$DOUT/$TAG.guest.fp" \
    timeout -s KILL ${TMO:-300} "$BIN" > "$OUT/$TAG.guest.log" 2>&1 &
GP=$!
wait $HP
wait $GP

# the log's "local N" and the puppet's "yaw/err" debug columns name the role; cut them as coop_determinism.sh does
for s in host guest; do
    sed -e 's/ local [01] / /' -e 's/ yaw -\{0,1\}[0-9]* err [-0-9.]* / /' "$OUT/$TAG.$s.fp" > "$OUT/$TAG.$s.txt"
done
nh=$(grep -ac 'ge: coop: fp ' "$OUT/$TAG.host.txt")
ng=$(grep -ac 'ge: coop: fp ' "$OUT/$TAG.guest.txt")
lh=$(wc -l < "$OUT/$TAG.host.txt")
lg=$(wc -l < "$OUT/$TAG.guest.txt")
n=$lh
if [ "$lg" -lt "$n" ]; then n=$lg; fi
head -n "$n" "$OUT/$TAG.host.txt" > "$OUT/$TAG.cmp.host"
head -n "$n" "$OUT/$TAG.guest.txt" > "$OUT/$TAG.cmp.guest"
dmg=$(grep -ac 'ge: coop: damage' "$OUT/$TAG.host.txt")
held=$(grep -a 'ge: net: tick' "$OUT/$TAG.host.log" | tail -n 1)
if cmp -s "$OUT/$TAG.cmp.host" "$OUT/$TAG.cmp.guest" && [ "$nh" -ge "$STOP" ] && [ "$ng" -ge "$STOP" ]; then
    echo "$TAG: IDENTICAL over $n lines; host $nh ticks, guest $ng ticks, $dmg damage events (lag '${LAG:-none}')"
    echo "$TAG: last host fp: $(grep -a 'ge: coop: fp ' "$OUT/$TAG.host.txt" | tail -n 1)"
    echo "$TAG: ${held:-no 'ge: net: tick' line}"
    exit 0
fi
echo "$TAG: DIFFERENT or short: host $nh ticks / $lh lines, guest $ng ticks / $lg lines (lag '${LAG:-none}')"
diff "$OUT/$TAG.cmp.host" "$OUT/$TAG.cmp.guest" | head -n 8
exit 1

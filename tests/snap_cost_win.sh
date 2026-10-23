#!/bin/sh
# Rollback R2: what a snapshot ring and a rollback cost on Windows (silent run, SDL_AUDIODRIVER=dummy; Git Bash).
#   sh tests/snap_cost_win.sh [build dir with ProjectMidas.exe] [tag]
# Runs the co-op Dam scenario (the guards, 2 Bonds) and Flatland hostage (Lua) with
#   ring   GE_SNAPRING=10: one save per tick, no restore: the per-tick cost of keeping the ring
#   stress GE_SNAPSTRESS=D (D = 1, 4, 8): a restore + D re-simulated ticks at EVERY tick (constant rollback), the whole state compared after each
# and prints the "ge: snap:" cost lines. A run without any GE_SNAP* is the reference for the frame time.
BDIR=${1:-build_win}
TAG=${2:-wcost}
EXE=$BDIR/ProjectMidas.exe
OUT=${OUT:-build_tests/snap}
mkdir -p "$OUT"
export SDL_AUDIODRIVER=dummy
COMMON="GE_PROFILE=r2w_cost GE_COOP=1 GE_SKIPINTRO=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_NOINISAVE=1 GE_COOP_LOG=1 GE_COOP_FP=1 GE_MODS=invincible=0,enemy_damage=400"
DAM="GE_STAGE=dam GE_DIFF=3 GE_SPAWNXYZ=19300,-100,17300,53 GE_COOP_GOD=0 GE_COOP_LOCAL=walk GE_COOP_P2=follow GE_FRAMES=${FRAMES:-1500}"
run() {   # <name> [KEY=VALUE...]
    n=$1; shift
    t0=$(date +%s)
    env $COMMON $DAM GE_COOP_FPFILE="$OUT/$TAG.$n.fp" "$@" "$EXE" > "$OUT/$TAG.$n.raw" 2>&1
    t1=$(date +%s)
    echo "== $n: $((t1 - t0)) s wall for $(grep -ac 'ge: coop: fp ' "$OUT/$TAG.$n.fp") ticks"
    grep -a 'ge: snap: first save\|ge: snap: SAVE\|ge: snap: ring of\|ge: snap: TOTALS\|ge: snap: ROLLBACK' "$OUT/$TAG.$n.raw" | tail -n 4
}
run ref
run ring GE_SNAPRING=10
run s1 GE_SNAPSTRESS=1,20
run s4 GE_SNAPSTRESS=4,20
run s8 GE_SNAPSTRESS=8,20

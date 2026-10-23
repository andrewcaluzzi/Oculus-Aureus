#!/bin/sh
# Rollback R2: ONE co-op run of the determinism scenarios with the snapshot test drivers (the building block of snap_resim.sh).
#   sh snap_resim_one.sh <scenario dam|depot> <tag> [KEY=VALUE ...]
# BIN (default build_pc_r2/ProjectMidas), OUT (default build_tests/snap). The scenario is tests/coop_determinism.sh's: Dam (walk / follow, the guards
# kill player 1) and Depot (a key picked up, a keyed door opened, a kill). The fingerprint goes to $OUT/<tag>.fp (the re-simulated ticks' lines to
# $OUT/<tag>.fp.resim), the log to $OUT/<tag>.raw.
BIN=${BIN:-/home/dev/project/build_pc_r2/ProjectMidas}
OUT=${OUT:-/home/dev/project/build_tests/snap}
mkdir -p "$OUT"
SC=$1
TAG=$2
shift 2
COMMON="GE_COOP=1 GE_SKIPINTRO=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_NOINISAVE=1 GE_COOP_LOG=1 GE_COOP_FP=1 GE_MODS=invincible=0,enemy_damage=400"
case "$SC" in
    dam) SCEN="GE_STAGE=dam GE_DIFF=3 GE_SPAWNXYZ=19300,-100,17300,53 GE_COOP_GOD=0 GE_FRAMES=${FRAMES:-4000} GE_COOP_LOCAL=walk GE_COOP_P2=follow" ;;
    depot) SCEN="GE_STAGE=depot GE_COOP_GIVEKEY=2 GE_COOP_KILL=200:1 GE_FRAMES=${FRAMES:-1300} GE_COOP_LOCAL=walk GE_COOP_P2=walk" ;;
    *) echo "unknown scenario $SC"; exit 2 ;;
esac
rm -f "$OUT/$TAG.fp" "$OUT/$TAG.fp.resim"
env $COMMON GE_COOP_VIEW=0 $SCEN GE_COOP_FPFILE="$OUT/$TAG.fp" "$@" "$BIN" > "$OUT/$TAG.raw" 2>&1
sed -e 's/ local [01] / /' -e 's/ yaw -\{0,1\}[0-9]* err [-0-9.]* / /' "$OUT/$TAG.fp" > "$OUT/$TAG.txt"
if [ -f "$OUT/$TAG.fp.resim" ]; then
    sed -e 's/ local [01] / /' -e 's/ yaw -\{0,1\}[0-9]* err [-0-9.]* / /' "$OUT/$TAG.fp.resim" > "$OUT/$TAG.resim.txt"
fi
echo "$TAG: $(grep -ac 'ge: coop: fp ' "$OUT/$TAG.txt") fingerprinted ticks"

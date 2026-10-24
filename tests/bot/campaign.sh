#!/bin/sh
# Player-bot campaign: play every solo stage in campaign order with tests/bot/level.sh, retrying a failed stage with a
# new seed up to RETRIES times, carrying on after a failure. Writes a table.
#
#   sh tests/bot/campaign.sh [difficulty 0-3]               (inside the build container, Xvfb on :99)
#
#   STAGES   space-separated list (default: the 18 missions, then aztec and egypt)
#   RETRIES  attempts per stage (default 3)      MAXSECS  per-attempt timeout in mission seconds (default 900)
#   REPLAY   verify each completed run by replaying its recording without the bot (default 1)
#   OUT      output dir (default build_tests/bot/campaign.d<diff>)
# Output: $OUT/results.tsv (every attempt), $OUT/campaign.md (one row per stage), per-attempt logs and recordings in
# $OUT/<stage>/.  Exit status: the number of stages that never completed.
# Aztec and Egypt are bonus stages that the save only unlocks after the campaign on Secret Agent / 00 Agent; the
# direct launch (GE_STAGE) plays them regardless, so they are in the list.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
DIFF=${1:-0}
STAGES=${STAGES:-"dam facility runway surface bunker1 silo frigate surface2 bunker2 statue archives streets depot train jungle control caverns cradle aztec egypt"}
RETRIES=${RETRIES:-3}
OUT=${OUT:-$ROOT/build_tests/bot/campaign.d$DIFF}
mkdir -p "$OUT"
TSV="$OUT/results.tsv"
MD="$OUT/campaign.md"
printf 'stage\tdiff\tattempt\toutcome\tmission\tticks\thealth_lost\tkills\tobjectives\treplay\twall\tfailure_point\n' > "$TSV"
{
    echo "| # | Stage | Result | Attempts | Mission time | Health lost | Kills | Objectives | Replay | Failure point (last attempt) |"
    echo "|---|---|---|---|---|---|---|---|---|---|"
} > "$MD"
fails=0
n=0
for st in $STAGES; do
    n=$((n + 1))
    a=1
    done_ok=0
    line=""
    while [ $a -le "$RETRIES" ]; do
        line=$(OUT="$OUT/$st" TAG="a$a" SEED=$a sh "$HERE/level.sh" "$st" "$DIFF")
        rc=$?
        printf '%s\n' "$line" | awk -F'\t' -v a=$a 'BEGIN{OFS="\t"} {print $1,$2,a,$3,$4,$5,$6,$7,$8,$9,$10,$11}' >> "$TSV"
        echo "[$n] $st attempt $a: $line"
        if [ $rc = 0 ] || [ $rc = 3 ]; then
            done_ok=1
            break
        fi
        a=$((a + 1))
    done
    [ $a -gt "$RETRIES" ] && a=$RETRIES
    echo "$line" | awk -F'\t' -v n=$n -v a=$a -v ok=$done_ok '{
        res = ok ? "**pass**" : "FAIL (" $3 ")";
        fp = ok ? "-" : $11;
        printf "| %d | %s | %s | %d | %s | %s | %s | %s | %s | %s |\n", n, $1, res, a, $4, $6, $7, $8, $9, fp }' >> "$MD"
    [ $done_ok = 0 ] && fails=$((fails + 1))
done
echo
cat "$MD"
exit $fails

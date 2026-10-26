#!/bin/sh
# The bank heist played end to end by the player bot (tools/bot/heist_bot.lua, designs/bank-heist.md 23e): Bond 0 is the bot, no
# human, no warps. Inside the build container with Xvfb on :99 and build_pc built (the compiled bank: v3).
#   sh tests/heist/run_botheist.sh [seed ...]          (default: seed 7)
# Env: PAR (runs at once, default 1), MAXSECS (mission seconds before the bot gives up, default 900), DIFF (default 0), EXTRA (more
# KEY=VALUE env, e.g. "GE_HEIST_TUNE1=1"), LOGDIR (default /tmp/botheist), SHOTS=1 (keep the bot's BMP every 30 s, in LOGDIR/shots_s<seed>).
# Fast-forward like tests/bot/level.sh: GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_CLOCKSCALE=20 and GE_SIMONLY_TEST (draw 1 tick in 60). GE_FRAMES
# is set out of reach: with GE_NOSLEEP it counts retrace pumps, not ticks, and the heist suites ran out of frames that way; the run ends
# on the mission's own end or the bot's MAXSECS (the bot exits the process after its RESULT line).
# Prints, per seed, one PASS / FAIL line per checkpoint (ready inside green blue console red vault_level cracking vault_open gold out
# win) and the bot's END line; then the reach rate per checkpoint over the seeds.
ROOT=/home/dev/project
B=$ROOT/build_pc
M=$B/Data/Scripts/modes
for f in mode_heist.lua:heist.lua heist_lib.lua heist_bank_map.lua heist_ai.lua heist_intel.lua heist_guide.lua heist_ar.lua heist_gadgets.lua \
         bot/heist_bot.lua:heist_bot.lua; do
    src=${f%%:*}; dst=${f#*:}
    cp "$ROOT/tools/$src" "$M/$dst"
done
cp $ROOT/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
LOGDIR=${LOGDIR:-/tmp/botheist}
mkdir -p "$LOGDIR"
SEEDS=${*:-7}
STAGES="ready inside green blue console red vault_level cracking vault_open gold out win"

one() {
    s=$1
    log=$LOGDIR/botheist_s$s.log
    shots=$LOGDIR/shots_s$s     # the bot saves a BMP every 30 s into the working directory (~2.7 MB each)
    mkdir -p "$shots"
    rm -f "$shots"/*.bmp
    ( cd "$shots" && env DISPLAY=:99 GE_MODE=heist_bot GE_STAGE=levelgen GE_DIFF=${DIFF:-0} GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_TICKHZ=60 \
        GE_SEED=$s GE_FIXEDTICK=1 GE_NOSLEEP=1 GE_CLOCKSCALE=20 GE_FRAMES=2000000000 GE_SIMONLY_TEST=2,59,60 GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
        GE_PLAYERBOT=1 GE_PLAYERBOT_HOLSTER=1 GE_PLAYERBOT_LOG=${LOG:-1} GE_PLAYERBOT_MAXSECS=${MAXSECS:-900} GE_INTEL_NOBRIEF=1 $EXTRA \
        timeout ${TIMEOUT:-1500} "$B/ProjectMidas" ) > "$log" 2>&1
    [ "${SHOTS:-0}" = 1 ] || rm -f "$shots"/*.bmp   # SHOTS=1 keeps them
}

n=0
for s in $SEEDS; do
    one $s &
    n=$((n + 1))
    if [ $n -ge "${PAR:-1}" ]; then wait; n=0; fi
done
wait

runs=0
for s in $SEEDS; do
    log=$LOGDIR/botheist_s$s.log
    runs=$((runs + 1))
    echo "BOTHEIST SEED $s"
    for st in $STAGES; do
        line=$(grep -a "BOTHEIST REACHED $st " "$log" | head -1 | sed 's/.*BOTHEIST REACHED //')
        if [ -n "$line" ]; then echo "BOTHEIST PASS $line"; else echo "BOTHEIST FAIL $st"; fi
    done
    grep -a "BOTHEIST END\|ge: playerbot: RESULT\|lua: .*error" "$log" | tail -3 | sed 's/^/  /'
done
echo "BOTHEIST REACH RATE over $runs seed(s): $SEEDS"
for st in $STAGES; do
    k=0
    for s in $SEEDS; do grep -aq "BOTHEIST REACHED $st " "$LOGDIR/botheist_s$s.log" && k=$((k + 1)); done
    echo "  $st $k/$runs"
done

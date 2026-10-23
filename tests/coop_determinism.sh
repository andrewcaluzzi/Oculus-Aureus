#!/bin/sh
# Co-op determinism test (the netcode condition): the same co-op scenario must produce an identical
# per-tick fingerprint (a) run twice in the same view and (b) with the local player switched
# (GE_COOP_VIEW=0 vs 1), because two machines in a session differ only in who is local.
#
# Runs inside the build container as user 1000 with DISPLAY set (build_pc + Xvfb, README-FORK.md):
#   wsl -d Ubuntu -- docker exec --user 1000 -e DISPLAY=:99 midas-test sh /home/dev/project/tests/coop_determinism.sh
# Optional argument: the output directory (default build_tests/coop4/det). Exit status = number of failed comparisons.
#
# Per tick the log carries both Bonds' positions and facings, both RNG seeds, a hash of every active prop
# (position, chr action, door state, so pickups and doors count), both Bonds' dead flags and health, and the
# game-field clock (GE_COOP_LOG=1 GE_COOP_FP=1), plus every damage event and every death/respawn line.
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
OUT=${1:-/home/dev/project/build_tests/coop4/det}
mkdir -p "$OUT"
FAIL=0

COMMON="GE_COOP=1 GE_SKIPINTRO=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_NOINISAVE=1 GE_COOP_LOG=1 GE_COOP_FP=1 GE_MODS=invincible=0,enemy_damage=400"

# run <name> <view> <player 0's script> <player 1's script> <extra env...>
# The scripted inputs belong to the PLAYERS, not to the roles: in view 0 player 0 is local (GE_COOP_LOCAL) and
# player 1 the puppet (GE_COOP_P2); in view 1 the roles swap, so the same script reaches the same player.
# Cut from the log before comparing: "local N" and the puppet's "yaw/err" debug columns (they name the role).
run() {
    name=$1
    view=$2
    s0=$3
    s1=$4
    shift 4
    if [ "$view" = 0 ]; then loc=$s0; pup=$s1; else loc=$s1; pup=$s0; fi
    # the fingerprint goes to its own file (GE_COOP_FPFILE), so another thread's stdout line can never split it
    env $COMMON GE_COOP_VIEW=$view GE_COOP_LOCAL=$loc GE_COOP_P2=$pup GE_COOP_FPFILE="$OUT/$name.fp" "$@" "$BIN" > "$OUT/$name.raw" 2>&1
    sed -e 's/ local [01] / /' -e 's/ yaw -\{0,1\}[0-9]* err [-0-9.]* / /' "$OUT/$name.fp" > "$OUT/$name.txt"
}

# same <a> <b>: identical over the common prefix (a run ends on a frame count, so one may hold a tick more)
same() {
    na=$(wc -l < "$1")
    nb=$(wc -l < "$2")
    n=$na
    [ "$nb" -lt "$n" ] && n=$nb
    n=$((n - 4))
    head -n "$n" "$1" > "$OUT/cmp.a"
    head -n "$n" "$2" > "$OUT/cmp.b"
    cmp -s "$OUT/cmp.a" "$OUT/cmp.b"
}

# scenario <name> <player 0 script> <player 1 script> <extra env...>: view 0, view 0 again, view 1
scenario() {
    sc=$1
    s0=$2
    s1=$3
    shift 3
    run "$sc.v0" 0 "$s0" "$s1" "$@"
    run "$sc.v0b" 0 "$s0" "$s1" "$@"
    run "$sc.v1" 1 "$s0" "$s1" "$@"
    # rollback R1: the sim-only tick. Stretches of ticks run with NO render step (GE_SIMONLY_TEST=<start>,<n>[,<every>]); the
    # fingerprints must equal the normal run's. SO1 = repeating stretches (60 of every 140 ticks), SO2 = every tick sim-only,
    # SO3 = the other view with stretches (the sim-only tick must not care who is local)
    run "$sc.so1" 0 "$s0" "$s1" "$@" GE_SIMONLY_TEST=30,60,140
    run "$sc.so2" 0 "$s0" "$s1" "$@" GE_SIMONLY_TEST=1,100000
    run "$sc.so3" 1 "$s0" "$s1" "$@" GE_SIMONLY_TEST=75,40,97
    ticks=$(grep -ac 'ge: coop: fp ' "$OUT/$sc.v0.txt")
    deaths=$(grep -ac 'died at field' "$OUT/$sc.v0.txt")
    respawns=$(grep -ac 'respawned at field' "$OUT/$sc.v0.txt")
    dmg=$(grep -ac 'ge: coop: damage' "$OUT/$sc.v0.txt")
    if same "$OUT/$sc.v0.txt" "$OUT/$sc.v0b.txt"; then r1=identical; else r1=DIFFERENT; FAIL=$((FAIL + 1)); fi
    if same "$OUT/$sc.v0.txt" "$OUT/$sc.v1.txt"; then r2=identical; else r2=DIFFERENT; FAIL=$((FAIL + 1)); fi
    echo "scenario $sc: $ticks fingerprinted ticks, $deaths deaths, $respawns respawns, $dmg damage events"
    grep -a 'DOORTEST tick 70\|DOORTEST tick 90\|GIVEKEY: player 0' "$OUT/$sc.v0.raw" | head -n 3
    echo "  view 0 vs view 0 (repeat): $r1"
    echo "  view 0 vs view 1:          $r2"
    if [ "$ticks" -lt 100 ] || [ "$r1" = DIFFERENT ] || [ "$r2" = DIFFERENT ]; then
        diff "$OUT/$sc.v0.txt" "$OUT/$sc.v1.txt" | head -n 6
    fi
    for so in so1 so2 so3; do
        base=v0
        [ "$so" = so3 ] && base=v1
        nso=$(grep -ac 'simonly: stretch starts' "$OUT/$sc.$so.raw")
        need=2
        [ "$so" = so2 ] && need=1
        if same "$OUT/$sc.$base.txt" "$OUT/$sc.$so.txt"; then r=identical; else r=DIFFERENT; FAIL=$((FAIL + 1)); fi
        # the test is vacuous unless sim-only stretches really ran
        if [ "$nso" -lt "$need" ]; then r="$r (but too few sim-only stretches ran!)"; FAIL=$((FAIL + 1)); fi
        echo "  sim-only run $so vs $base (stretches logged: $nso): $r"
        if [ "$r" != identical ]; then
            diff "$OUT/$sc.$base.txt" "$OUT/$sc.$so.txt" | head -n 6
        fi
    done
}

# A: Dam, guards shoot player 1 dead (three times) and it respawns beside player 0 (player 0 cannot die, so the stage goes on)
scenario dam walk follow GE_STAGE=dam GE_DIFF=3 GE_SPAWNXYZ=19300,-100,17300,53 GE_COOP_GOD=0 GE_FRAMES=4000

# B: Depot, player 0 picks up the first key (tick 60), player 1 opens the keyed door (tick 70), both walk,
#    player 1 is killed at tick 200 and respawns at field 800
scenario depot walk walk GE_STAGE=depot GE_COOP_GIVEKEY=2 GE_COOP_KILL=200:1 GE_FRAMES=1300

echo "coop determinism: $FAIL failed comparisons"
exit $FAIL

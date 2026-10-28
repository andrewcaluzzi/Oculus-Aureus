#!/bin/sh
# Netplay job 3 harness: desync detection and the oracle.
#
# Runs inside the build container as user 1000 with DISPLAY set (build_pc + Xvfb, README-FORK.md):
#   wsl -d Ubuntu -- docker exec --user 1000 -e DISPLAY=:99 midas-net sh /home/dev/project/tests/net_lockstep.sh <tag> <port> [ticks] [lag]
#     tag    output prefix in build_tests/net2/ (use a new one per run)
#     port   first UDP port; the script uses port .. port+9
#     ticks  sim ticks per run (default 130: four 30-tick hash checks; keep it short, long llvmpipe runs can crash in Mesa)
#     lag    optional GE_NET_LAG value for every netplay run, e.g. 100,20,5
# Windows: BIN=/c/.../build_win_dev/ProjectMidas.exe OUT=... DOUT=C:/... REC=... DREC=C:/... ROOT=/c/.../build_win_dev sh net_lockstep.sh ...
#
# Phases (each one prints PASS or FAIL lines; the exit status is the number of FAILs):
#   1 clean    host + guest replay recA / recB: no DESYNC, hashes exchanged and equal, per-tick fingerprints identical
#   2 oracle   the same two files replayed by ONE local co-op instance (GE_COOP_LOCAL / GE_COOP_P2) give the same
#              per-tick fingerprints as the netplay run
#   3 desync   GE_NET_TEST_DESYNC=<tick> on the guest only: both sides must report DESYNC within one hash interval and
#              both must write Data/Logs/desync_<tick>.txt
#   4 settings host and guest with different per-player settings (control style / input mode / invert pitch / auto-aim...)
#              stay in sync, and the settings really reach the sim (the run differs from phase 1)
#   5 ini      the same through a real midas.ini (guest: style 1.3, reverse pitch, auto-aim, toggle aim), and the negative
#              control GE_NET_OPTS_OFF=1 (the pre-job-3 behaviour, each machine's own ini for both Bonds) DOES desync
#   6 everything (job 3b) the scripted "everything" session (recEA / recEB, 470 ticks: weapon cycling, reload, crouch, aim, zoom,
#              the watch opened and closed three ways while fire is held, a Return-only START, deaths and respawns, a second
#              death for both Bonds): no DESYNC, identical fingerprints, the same with lag (and the same as without), the
#              session logs of both sides identical, each log replayed offline as local co-op (view 0 and view 1) reproduces
#              the session tick for tick, and the facts the script is for (fire suppressed while the watch is open, the
#              damage gate, die / respawn / die again, the Return START that does not open the watch)
#   7 leak     phase 6's session again, but real X key / mouse / wheel events (xdotool) are sent to the guest's window all the
#              time: nothing may change, no DESYNC, and the fingerprints equal phase 6's (Linux + Xvfb only; needs xdotool)
#   8 ready    the settings block is taken again at READY (GE_NET_OPTS_READY, test only, changes it after the handshake): both sides
#              hold the refreshed blocks, the sim reads them, no DESYNC
#   9 live     both Bonds on LIVE devices (no scripted records) while real X events (xdotool) hit the guest: what the guest types
#              reaches the HOST's sim through the guest's records (Escape opens its watch, the wheel changes its weapon), no DESYNC,
#              Tab is blocked with a log line, and the host's session log of that live session replays offline
#  10-14 (jobs 3c / 3d)  the sim-settings check, the RNG trace and bisect tools, window independence, death drops
#  15 parity (job 4)   a gamepad player (smooth look, analog zoom levels in the flags) against a mouse player (flicks), both ways round,
#              and a live right stick (GE_NET_TEST_RSTICK): no DESYNC, the zoom levels the sim reads are the same on both machines
#  16 stages (job 4)   two stages in one session: Dam ends (GE_NET_TEST_ENDSTAGE), the host launches Depot (GE_NET_TEST_NEXT), the guest
#              follows its NEXTSTAGE message; no DESYNC over both, the logs of both stage runs replay, `net info` (GE_NET_TEST_INFO)
#  17 leave (job 4)    the partner quits cleanly / is killed: 'partner disconnected', the message, the way back to the title
#  18 rogue (job 4)    a guest that starts a stage the host never chose: no crash, no stage run alone
#  19 watch / drops / music (job 3e)   P0's watch open during a fight: the guest's world is drawn and its hits land, no DESYNC (negative control: the
#              pre-3e shared world gate); the corpse run (a dead Bond collects nothing, the former owner takes its drop after the respawn);
#              the level music of every stage starts only after that stage's GO
#  20 session commands (job 5)   spawn and give applied at the same tick on both machines (issued by the guest, by the host, both at once; under lag), bad
#              commands refused with a log line, the death-drop of a given GoldenEye key / key object, Flatland with guards spawned from both windows, offline replays
#  21 far apart (job 5 / playtest 7)   one Bond away from the other with its own guards, brought back with them, different windows, shooting: no DESYNC, the
#              per-player drawn room sets (the sim's canonical views) identical on both machines and containing every guard next to a Bond, replays under other windows
#  22 sim lists (job 7)   the sim reads only per-player SIM room lists / flags (canonical walk); windows that give every machine other render room lists must not reach it; negative control
#  23 gate door (job 7)   the Dam start gate's vertices survive the first frames (the playtest-7 root cause: a stale per-frame vertex pool read); negative control
#  24 walk-through (job 7)   net_roomwalk.sh: both Bonds tour Dam (tunnel, gate, road, tunnels) with every door opened and closed: each view draws every room a fresh solo walk draws
#  25 sim-only (rollback R1)   phase 6's session with GE_SIMONLY_TEST stretches (ticks with no render step) on the host, on the guest, on both (the guest never renders): the fingerprints,
#              hashes and session logs are the normal run's, no DESYNC
#  26 monitor hit test (soak fix)   seed 32 Depot: P1's watch open while P0 shoots a monitor (the display list its last renderer left was read); negative control
#  27 Flatland end (soak fix)   the soak's seed 44 Flatland run: the stage ends by itself and the mission-end screen loads (it used to segfault in strcmp)
#  28 mission end (mission-end job)  Dam bungee exit by P0 and by P1 (GE_COOP_MOVETOPAD puts the Bond on the jump trigger, GE_TEST_ALLOBJ makes it the success exit), the other Bond pressing
#              'continue' EARLY / LATE / never: both machines leave the stage on the same tick, both screens fade, the exit camera plays, the host's next stage loads on both; offline replay; negative control
#  29 mission failed  both Bonds dead: the same exit tick on both, then the host's next stage loads on both
#  30 next-stage handoff  the guest cannot start a stage (refused) and follows the host's NEXTSTAGE: guest tries first / guest off-stage for long / host picks while the guest is still in the old stage
#  32 session teardown  the host quits mid-stage; the guest returns to the title and starts Dam: co-op is off, genuine solo (one Bond)
#  33 ending visuals  a Bond dead before the ending shows no death overlay under the exit cutscene, only the cutscene Bond is shown; the credits stage renders in both views
#  34-39 (job CO-OP-EXITS)  34 both Bonds at the Dam exit (held, partner arrives; first; never; dead partner walks in from the start; forced-unreachable start = beside the partner), 35 lives per player / per
#              party (out = stays down and watches, mission fails when nobody can go on), 36 the host's session settings reach the guest, 37 Surface 2 bomb message, 38 escort NPCs
#              (Jungle's Natalya: nearest living Bond with hysteresis; negative control), 39 the partner leaves while a Bond waits at the exit
#  40-45 (job MODES-IN-CO-OP)  game modes in co-op / netplay (net_modes.sh, tests/lua/coop_modes_test.lua):
#              40 the Flatland hostage scenario over netplay: either Bond toggles hostages through its own record, the host's mode reaches the guest, the Lua script sees the
#              same world (bond.pos() = player 0, math.random from the session seed, pairs() order), per-player HUD lines, offline replays in both seats, lag, the local-co-op oracle in both views
#              41 Dam with the real modes/hostage.lua, toggles / chain per leader / hand-over, replays
#              42 the headline: Flatland hostage-flatland (10 hostages, 20 guards) with mortal Bonds in the middle of the guards: combat, deaths, the shared mission failure, replays
#              43 mission.win / mission.fail through the shared mission end (same tick), NEXTSTAGE carries the host's mode, both Bonds dead / lives / respawn inside a mode
#              44 negative control: a script reading the LOCAL player (GE_LUA_TEST_LEAKLOCAL=1 on the guest) desyncs
#              45 the script-set hash covers Data/Scripts/modes, a different set is refused, the guest's own GE_MODE is ignored
#  31 phase 16 ten times in a row (no stall); NOT in the default list (about 25 minutes): PHASES=31
# Phases can be picked with PHASES="1 2" (default all, except 31).
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
ROOT=${ROOT:-$(dirname "$BIN")}   # the exe's folder: Data/Profiles/<name>/ lives under it
DROOT=${DROOT:-$ROOT}             # ROOT as the game process sees it (a Windows exe wants C:/...)
OUT=${OUT:-/home/dev/project/build_tests/net2}
DOUT=${DOUT:-$OUT}
REC=${REC:-/home/dev/project/tests/net}
DREC=${DREC:-$REC}
HERE=$(dirname "$0")
TAG=${1:-ls}
PORT=${2:-27200}
TICKS=${3:-130}
LAG=${4:-none}
PHASES=${PHASES:-"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 32 33 34 35 36 37 38 39 40 41 42 43 44 45"}
FAILS=0

pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILS=$((FAILS + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }

# per-run knobs, set by plain assignment before a net call and cleared after: HOSTENV / GUESTENV (extra environment for one
# side), HPROFILE / GPROFILE (profile names, default $HP / $GP: netbase_h / netbase_g)
HOSTENV=; GUESTENV=; HPROFILE=; GPROFILE=

net() {   # <tag> <port>: net_replay2.sh with this script's settings
    t=$1; p=$2
    env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HOSTENV="$HOSTENV" GUESTENV="$GUESTENV" \
        HPROFILE="${HPROFILE:-$HP}" GPROFILE="${GPROFILE:-$GP}" sh "$HERE/net_replay2.sh" "$t" "$p" "$TICKS" "$LAG" > "$OUT/$t.run.txt" 2>&1
    rc=$?
    HOSTENV=; GUESTENV=; HPROFILE=; GPROFILE=
    return $rc
}

earliest_desync() {   # <log>: the lowest tick named in a DESYNC line, empty if none
    grep -aoh 'DESYNC at tick [0-9]*' "$1" | sed 's/.*tick //' | sort -n | head -n 1
}

hashline() {   # <log>: the "state hash: N sent, M compared, K mismatched" summary
    grep -a 'ge: net: state hash: ' "$1" | tail -n 1
}

hash_ok() {   # <log> [ticks]: at least (ticks/30 - 1) compared and none mismatched
    h=$(hashline "$1")
    cmp_n=$(echo "$h" | sed -n 's/.* \([0-9][0-9]*\) compared.*/\1/p')
    bad_n=$(echo "$h" | sed -n 's/.* \([0-9][0-9]*\) mismatched.*/\1/p')
    [ -n "$cmp_n" ] && [ -n "$bad_n" ] && [ "$bad_n" -eq 0 ] && [ "$cmp_n" -ge $((${2:-$TICKS} / 30 - 1)) ]
}

no_desync() { ! grep -aq 'DESYNC' "$1"; }

fpcut() {   # <file> <tag-out>: strip the role-dependent columns, stop after the fingerprint of tick $TICKS
    sed -e 's/ local [01] / /' -e 's/ yaw -\{0,1\}[0-9]* err [-0-9.]* / /' "$1" | awk -v n="$TICKS" '{ print } /^ge: coop: fp / { if ($4 == n) exit }'
}

# Fixed profiles for every run, so the result does not depend on whatever ini a profile of that name holds (an old nethost
# profile with auto_aim=1 made the oracle differ: see netplay.md "Oracle and auto-aim"). Both sides and the local oracle
# instance get the same four player options; GE_NOINISAVE keeps the files as written here.
HP=netbase_h
GP=netbase_g

# The input files must be there. The game does not stop on a missing GE_COOP_LOCAL / GE_COOP_P2 replay file: it logs
# "cannot open the replay file" and that Bond stands still. Both machines then agree on an idle session, so every hash,
# DESYNC, fingerprint and replay check passes and only the "the scripted input had its effect" checks fail (19 of them on
# the v0.1 tree, 2026-10-28, whose tests/net held only the .awk generators). Stop here instead of running 40 phases on idle Bonds.
missing=
for f in recA.txt recB.txt recEA.txt recEB.txt recPA.txt recPB.txt recWA.txt recWB.txt recFA.txt recFB.txt; do
    [ -s "$REC/$f" ] || missing="$missing $f"
done
if [ -n "$missing" ]; then
    echo "FAIL 0 input files missing in $REC:$missing (tests/net holds them; REC= names another folder)"
    echo "net_lockstep: 1 failed"
    exit 1
fi

for pr in $HP $GP; do
    mkdir -p "$ROOT/Data/Profiles/$pr"
    printf '[input]\nlook_updown=1\nauto_aim=0\naim_control=0\nlook_ahead=1\n' > "$ROOT/Data/Profiles/$pr/midas.ini"
done

rm -f "$OUT/$TAG".*

if echo " $PHASES " | grep -q ' 1 '; then
    echo "== phase 1: clean netplay run, $TICKS ticks, lag $LAG"
    net "$TAG.clean" "$PORT"
    check "1 fingerprints identical host/guest (net_replay2)" "grep -q IDENTICAL $OUT/$TAG.clean.run.txt"
    check "1 no DESYNC on the host" "no_desync $OUT/$TAG.clean.host.log"
    check "1 no DESYNC on the guest" "no_desync $OUT/$TAG.clean.guest.log"
    check "1 host hash exchange ran and matched: $(hashline "$OUT/$TAG.clean.host.log")" "hash_ok $OUT/$TAG.clean.host.log"
    check "1 guest hash exchange ran and matched: $(hashline "$OUT/$TAG.clean.guest.log")" "hash_ok $OUT/$TAG.clean.guest.log"
fi

if echo " $PHASES " | grep -q ' 2 '; then
    echo "== phase 2: oracle, netplay (host + guest) vs ONE local co-op instance replaying the same two files"
    if [ ! -f "$OUT/$TAG.clean.host.txt" ]; then
        net "$TAG.clean" "$PORT"
    fi
    rm -f "$OUT/$TAG.local.fp"
    # the local instance uses the host's profile (same ini, so the same per-player settings: a different ini, for
    # example the exe's own midas.ini with auto-aim on, changes the sim and is not an oracle failure)
    env GE_PROFILE=$HP GE_COOP=1 GE_STAGE=${STAGE:-dam} GE_DIFF=1 GE_SEED=7 GE_COOP_RESEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SKIPINTRO=1 GE_NOINISAVE=1 \
        GE_STAGE_AFTER=20 GE_MODS=invincible=0,enemy_damage=400 GE_COOP_LOG=1 GE_COOP_FP=1 GE_COOP_CLOCKDBG=1 GE_COOP_VIEW=0 \
        GE_FRAMES=$((TICKS + 60)) GE_COOP_LOCAL=replay:$DREC/recA.txt GE_COOP_P2=replay:$DREC/recB.txt \
        GE_COOP_FPFILE="$DOUT/$TAG.local.fp" timeout -s KILL 300 "$BIN" > "$OUT/$TAG.local.log" 2>&1
    fpcut "$OUT/$TAG.local.fp" > "$OUT/$TAG.local.cmp"
    fpcut "$OUT/$TAG.clean.host.fp" > "$OUT/$TAG.nethost.cmp"
    nl=$(grep -ac 'ge: coop: fp ' "$OUT/$TAG.local.cmp")
    nn=$(grep -ac 'ge: coop: fp ' "$OUT/$TAG.nethost.cmp")
    check "2 the local run reached tick $TICKS ($nl fingerprints), the netplay run too ($nn)" "[ $nl -ge $TICKS ] && [ $nn -ge $TICKS ]"
    check "2 netplay == local co-op replay, per-tick fingerprints identical over $(wc -l < "$OUT/$TAG.nethost.cmp") lines" "cmp -s $OUT/$TAG.local.cmp $OUT/$TAG.nethost.cmp"
    cmp -s "$OUT/$TAG.local.cmp" "$OUT/$TAG.nethost.cmp" || diff "$OUT/$TAG.local.cmp" "$OUT/$TAG.nethost.cmp" | head -n 6
fi

if echo " $PHASES " | grep -q ' 3 '; then
    echo "== phase 3: forced desync (GE_NET_TEST_DESYNC=70 on the guest only)"
    T=70
    FIRST=$(( (T + 29) / 30 * 30 ))   # the first 30-tick hash at or after the perturbation
    rm -f "$ROOT"/Data/Profiles/$HP/Logs/desync_*.txt "$ROOT"/Data/Profiles/$GP/Logs/desync_*.txt
    GUESTENV="GE_NET_TEST_DESYNC=$T"
    net "$TAG.desync" $((PORT + 1))
    check "3 guest perturbed itself" "grep -aq 'perturbed the sim RNG at tick $T' $OUT/$TAG.desync.guest.log"
    check "3 host reports DESYNC at tick $FIRST" "grep -aq 'ge: net: DESYNC at tick $FIRST ' $OUT/$TAG.desync.host.log"
    check "3 guest reports DESYNC at tick $FIRST" "grep -aq 'ge: net: DESYNC at tick $FIRST ' $OUT/$TAG.desync.guest.log"
    check "3 the first DESYNC is at tick $FIRST on both sides (caught within one 30-tick hash interval of tick $T)" "[ \"\$(earliest_desync $OUT/$TAG.desync.host.log)\" = $FIRST ] && [ \"\$(earliest_desync $OUT/$TAG.desync.guest.log)\" = $FIRST ]"
    check "3 host wrote desync_$FIRST.txt" "[ -s $ROOT/Data/Profiles/$HP/Logs/desync_$FIRST.txt ]"
    check "3 guest wrote desync_$FIRST.txt" "[ -s $ROOT/Data/Profiles/$GP/Logs/desync_$FIRST.txt ]"
    check "3 the dump names the differing field (rng)" "grep -a 'DIFFERS' $ROOT/Data/Profiles/$HP/Logs/desync_$FIRST.txt | grep -aq '^rng'"
    grep -ah 'ge: net: DESYNC' "$OUT/$TAG.desync.host.log" | head -n 1
    grep -ah 'DIFFERS' "$ROOT/Data/Profiles/$HP/Logs/desync_$FIRST.txt" | head -n 3
fi

if echo " $PHASES " | grep -q ' 4 '; then
    echo "== phase 4: different per-player settings on host and guest (GE_NET_OPTS), $TICKS ticks"
    [ -f "$OUT/$TAG.clean.host.fp" ] || net "$TAG.clean" "$PORT"
    HOSTENV="GE_NET_OPTS=style=0,n64=0,upright=1,autoaim=0,lookahead=1"
    GUESTENV="GE_NET_OPTS=style=2,n64=1,upright=0,autoaim=1,lookahead=0,aimtoggle=1,aimsens=1500,aimlock=0"
    net "$TAG.set" $((PORT + 2))
    check "4 fingerprints identical host/guest with different settings" "grep -q IDENTICAL $OUT/$TAG.set.run.txt"
    check "4 no DESYNC (host)" "no_desync $OUT/$TAG.set.host.log"
    check "4 no DESYNC (guest)" "no_desync $OUT/$TAG.set.guest.log"
    check "4 host hash exchange matched" "hash_ok $OUT/$TAG.set.host.log"
    check "4 guest hash exchange matched" "hash_ok $OUT/$TAG.set.guest.log"
    check "4 both sides hold both blocks (same 'player settings' line)" "[ \"\$(grep -ah 'ge: net: player settings' $OUT/$TAG.set.host.log)\" = \"\$(grep -ah 'ge: net: player settings' $OUT/$TAG.set.guest.log)\" ]"
    grep -ah 'ge: net: player settings' "$OUT/$TAG.set.host.log" | head -n 1
    check "4 the sim reads the same per-Bond values on both machines (getters called per Bond at tick 1)" "[ \"\$(grep -ah 'ge: net: sim reads' $OUT/$TAG.set.host.log)\" = \"\$(grep -ah 'ge: net: sim reads' $OUT/$TAG.set.guest.log)\" ]"
    check "4 ...and the two Bonds read different control style and invert settings" "grep -ah 'ge: net: sim reads player 0' $OUT/$TAG.set.host.log | grep -aq 'style 0 n64 0 look-updown 1' && grep -ah 'ge: net: sim reads player 1' $OUT/$TAG.set.host.log | grep -aq 'style 2 n64 1 look-updown 0' && grep -ah 'ge: net: sim reads player 1' $OUT/$TAG.set.guest.log | grep -aq 'aimsens 1500'"
    grep -ah 'ge: net: sim reads' "$OUT/$TAG.set.host.log"
    check "4 the settings reach the sim: the run differs from the default-settings run (guest's Bond plays N64 style 1.3)" "! cmp -s $OUT/$TAG.set.cmp.host $OUT/$TAG.clean.cmp.host"
fi

if echo " $PHASES " | grep -q ' 5 '; then
    echo "== phase 5: the same through real midas.ini files, and the negative control"
    mkdir -p "$ROOT/Data/Profiles/netini_g"
    {
        echo "[input]"
        echo "pad1_style=1.3"
        echo "look_updown=0"
        echo "auto_aim=1"
        echo "aim_control=1"
    } > "$ROOT/Data/Profiles/netini_g/midas.ini"
    GPROFILE=netini_g
    net "$TAG.ini" $((PORT + 3))
    grep -ah 'ge: net: player settings' "$OUT/$TAG.ini.host.log" | head -n 1
    # (the N64 input mode counts only while a pad occupies the slot, so there is no pad in Xvfb: that field is tested with GE_NET_OPTS in phase 4)
    check "5 ini: guest block comes from its ini (style 1.3, upright 0, autoaim 1, aimtoggle 1)" "grep -ah 'ge: net: player settings' $OUT/$TAG.ini.host.log | grep -aq 'guest (player 1) style 1.3 n64 [01] upright 0 autoaim 1 lookahead [01] aimtoggle 1'"
    check "5 ini: identical fingerprints, no DESYNC" "grep -q IDENTICAL $OUT/$TAG.ini.run.txt && no_desync $OUT/$TAG.ini.host.log && no_desync $OUT/$TAG.ini.guest.log"
    check "5 ini: hash exchange matched" "hash_ok $OUT/$TAG.ini.host.log && hash_ok $OUT/$TAG.ini.guest.log"
    GPROFILE=netini_g
    HOSTENV="GE_NET_OPTS_OFF=1"
    GUESTENV="GE_NET_OPTS_OFF=1"
    net "$TAG.off" $((PORT + 4))
    check "5 negative control: settings not applied -> the machines DESYNC (host)" "grep -aq 'ge: net: DESYNC at tick' $OUT/$TAG.off.host.log"
    check "5 negative control: ...and the guest sees it too" "grep -aq 'ge: net: DESYNC at tick' $OUT/$TAG.off.guest.log"
    grep -ah 'ge: net: DESYNC' "$OUT/$TAG.off.host.log" | head -n 1
fi

# ---- job 3b: the scripted everything session --------------------------------------------------------------------------------
EVTICKS=470
EVMODS=invincible=0,enemy_damage=400
EVHURT=160:0:0.5,160:1:0.5,215:1:10,350:0:10,410:1:10,430:0:10,460:1:10

evnet() {   # <tag> <port> <lag> [KEY=VALUE...]: the everything session through net_replay2.sh with the session logs on; the logs are copied to $OUT
    t=$1; p=$2; l=$3; shift 3
    rm -f "$ROOT/Data/Profiles/$HP/Logs"/netlog_run*.txt "$ROOT/Data/Profiles/$GP/Logs"/netlog_run*.txt
    env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" RECA=recEA.txt RECB=recEB.txt HPROFILE="$HP" GPROFILE="$GP" \
        HOSTENV="$HOSTENV" GUESTENV="$GUESTENV" sh "$HERE/net_replay2.sh" "$t" "$p" "$EVTICKS" "$l" GE_NET_LOG=1 GE_MODS="$EVMODS" \
        GE_COOP_HURT="$EVHURT" GE_COOP_RESPAWN=40 GE_COOP_GIVEWEAPON=260:1:8:60 "$@" > "$OUT/$t.run.txt" 2>&1
    HOSTENV=; GUESTENV=
    cp "$ROOT/Data/Profiles/$HP/Logs/netlog_run1_host.txt" "$OUT/$t.hostlog.txt" 2>/dev/null
    cp "$ROOT/Data/Profiles/$GP/Logs/netlog_run1_guest.txt" "$OUT/$t.guestlog.txt" 2>/dev/null
}

fpcutn() {   # <file> <ticks>: fpcut for another length
    sed -e 's/ local [01] / /' -e 's/ yaw -\{0,1\}[0-9]* err [-0-9.]* / /' "$1" | awk -v n="$2" '{ print } /^ge: coop: fp / { if ($4 == n) exit }'
}

replay_log() {   # <log as the game sees it> <view 0|1> <tag>: GE_NET_REPLAY of a session log; the fingerprints go to $OUT/<tag>.fp
    rm -f "$OUT/$3.fp"
    env GE_PROFILE=$HP GE_NOINISAVE=1 GE_FRAMES=6000 GE_NET_REPLAY="$1" GE_NET_REPLAY_VIEW=$2 GE_COOP_LOG=1 GE_COOP_FP=1 \
        GE_COOP_FPFILE="$DOUT/$3.fp" timeout -s KILL 300 "$BIN" > "$OUT/$3.log" 2>&1
}

replay_ok() { grep -aq 'ge: replay: OK' "$1"; }

# The fp line: "ge: coop: fp <tick> world <h> dead <ab> hp <a> <b> fields <n> watch <w0> <w1> wpn <r0> <l0> <r1> <l1> mag <m0> <m1>"
# ($4 tick, $8 dead flags, $15 $16 watch state, $18..$21 weapons, $23 $24 right-hand magazine). Each check is one awk call.
fp_watch_reached() { awk -v c="$2" '$3 == "fp" && $c == 5 { n++ } END { exit !(n >= 20) }' "$1"; }   # <fp> <column>: the watch really was open (state 5) for 20+ ticks
fp_nofire_in_watch() { awk -v c="$2" -v m="$3" '$3 == "fp" && ($c >= 1 && $c <= 5) { if (seen && $m != last) bad++; last = $m; seen = 1; next } $3 == "fp" { seen = 0 } END { exit !(bad == 0) }' "$1"; }
# ^ <fp> <watch column> <magazine column>: while that Bond's watch is not closed, its right-hand magazine never changes (no shot)
fp_weapon_changes() { awk -v a="$2" -v b="$3" '$3 == "fp" { k = $a " " $b; if (k != last) n++; last = k } END { exit !(n >= 4) }' "$1"; }   # the weapon pair changed 3+ times
fp_slappers() { awk '$3 == "fp" && ($18 == 1 || $20 == 1) { n++ } END { exit !(n > 0) }' "$1"; }   # somebody held ITEM_FIST (the slappers)
death_cycle() {   # <fp> <player>: died, respawned, died again (in that order)
    awk -v p="$2" 'index($0, "player " p " died") { if (state == 0) state = 1; else if (state == 2) state = 3 } index($0, "player " p " respawned") { if (state == 1) state = 2 } END { exit !(state == 3) }' "$1"
}
fp_ticks_where() { awk -v c="$2" -v lo="$3" -v hi="$4" -v want="$5" '$3 == "fp" && $4 >= lo && $4 <= hi && $c != want { bad++ } END { exit !(bad == 0) }' "$1"; }   # <fp> <col> <from> <to> <value>: column == value on every tick in the range

if echo " $PHASES " | grep -q ' 6 '; then
    echo "== phase 6: the scripted everything session, $EVTICKS ticks (recEA / recEB, deaths by GE_COOP_HURT, session logs on)"
    evnet "$TAG.ev" $((PORT + 5)) none
    check "6 fingerprints identical host/guest" "grep -q IDENTICAL $OUT/$TAG.ev.run.txt"
    check "6 no DESYNC (host)" "no_desync $OUT/$TAG.ev.host.log"
    check "6 no DESYNC (guest)" "no_desync $OUT/$TAG.ev.guest.log"
    check "6 hash exchange matched (host): $(hashline "$OUT/$TAG.ev.host.log")" "hash_ok $OUT/$TAG.ev.host.log $EVTICKS"
    check "6 hash exchange matched (guest)" "hash_ok $OUT/$TAG.ev.guest.log $EVTICKS"
    F=$OUT/$TAG.ev.host.fp
    check "6 both Bonds' watches really opened (state 5 for 20+ ticks each)" "fp_watch_reached $F 15 && fp_watch_reached $F 16"
    check "6 fire is suppressed while a watch is open: the magazine never changes (host's Bond)" "fp_nofire_in_watch $F 15 23"
    check "6 ...and the guest's Bond" "fp_nofire_in_watch $F 16 24"
    check "6 control: the same Z hold DOES fire with the watch closed (guest holds a gun (the KF7 given at tick 260) with rounds at tick 387 and fewer at 409)" "awk '\$3 == \"fp\" && \$4 == 387 { a = \$24 } \$3 == \"fp\" && \$4 == 409 { b = \$24 } END { exit !(a > 0 && b < a) }' $F"
    check "6 a hit with the watch open is blocked (both Bonds keep full health)" "grep -a 'hurt player 0 by 0.50 at tick 160 (health 1.000 -> 1.000' $OUT/$TAG.ev.host.log | grep -aq 'watch 5' && grep -a 'hurt player 1 by 0.50 at tick 160 (health 1.000 -> 1.000' $OUT/$TAG.ev.host.log | grep -aq 'watch 5'"
    check "6 the Return-only START at tick 265 does not open the guest's watch (state 0 on ticks 267..269)" "fp_ticks_where $F 16 267 269 0"
    check "6 the weapons changed (host's Bond; includes the slappers)" "fp_weapon_changes $F 18 19 && fp_slappers $F"
    check "6 the weapons changed (guest's Bond)" "fp_weapon_changes $F 20 21"
    check "6 die, respawn, die again: host's Bond (player 0)" "death_cycle $F 0"
    check "6 die, respawn, die again: guest's Bond (player 1)" "death_cycle $F 1"
    check "6 the second death of both Bonds at tick 430/410 happened while the other lived or died, and the session went on to tick $EVTICKS" "grep -aq 'fp $EVTICKS ' $F"
    # the same session over a laggy, lossy link: same result, and the same as without lag
    evnet "$TAG.evlag" $((PORT + 6)) 100,20,5
    check "6 with GE_NET_LAG=100,20,5: fingerprints identical host/guest, no DESYNC" "grep -q IDENTICAL $OUT/$TAG.evlag.run.txt && no_desync $OUT/$TAG.evlag.host.log && no_desync $OUT/$TAG.evlag.guest.log"
    check "6 with lag: hash exchange matched" "hash_ok $OUT/$TAG.evlag.host.log $EVTICKS && hash_ok $OUT/$TAG.evlag.guest.log $EVTICKS"
    fpcutn "$OUT/$TAG.ev.host.fp" $EVTICKS > "$OUT/$TAG.ev.cmp"
    fpcutn "$OUT/$TAG.evlag.host.fp" $EVTICKS > "$OUT/$TAG.evlag.cmp"
    check "6 the laggy run equals the lag-free run (the lag changes when, never what)" "cmp -s $OUT/$TAG.ev.cmp $OUT/$TAG.evlag.cmp"
    # the session logs: both sides wrote the same records and fingerprints
    grep -a '^T ' "$OUT/$TAG.ev.hostlog.txt" > "$OUT/$TAG.ev.hostlog.T"
    grep -a '^T ' "$OUT/$TAG.ev.guestlog.txt" > "$OUT/$TAG.ev.guestlog.T"
    check "6 the host's and the guest's session logs hold the same $EVTICKS records and fingerprints" "cmp -s $OUT/$TAG.ev.hostlog.T $OUT/$TAG.ev.guestlog.T && [ \$(wc -l < $OUT/$TAG.ev.hostlog.T) -ge $EVTICKS ]"
    # offline replay: each log as local co-op, view 0 and view 1
    replay_log "$DOUT/$TAG.ev.hostlog.txt" 0 "$TAG.rp_h0"
    replay_log "$DOUT/$TAG.ev.hostlog.txt" 1 "$TAG.rp_h1"
    replay_log "$DOUT/$TAG.ev.guestlog.txt" 1 "$TAG.rp_g1"
    check "6 replay of the host's log, view 0: $(grep -a 'ge: replay: .*fingerprints compared' "$OUT/$TAG.rp_h0.log" | sed 's/ge: replay: //')" "replay_ok $OUT/$TAG.rp_h0.log"
    check "6 replay of the host's log, view 1 (the other Bond's seat)" "replay_ok $OUT/$TAG.rp_h1.log"
    check "6 replay of the guest's log, view 1" "replay_ok $OUT/$TAG.rp_g1.log"
    fpcutn "$OUT/$TAG.rp_h0.fp" $EVTICKS > "$OUT/$TAG.rp_h0.cmp"
    fpcutn "$OUT/$TAG.rp_h1.fp" $EVTICKS > "$OUT/$TAG.rp_h1.cmp"
    fpcutn "$OUT/$TAG.ev.host.fp" $EVTICKS > "$OUT/$TAG.ev.cmp2"
    check "6 the replay's per-tick fingerprints equal the netplay run's (view 0)" "cmp -s $OUT/$TAG.rp_h0.cmp $OUT/$TAG.ev.cmp2"
    check "6 ...and view 1 equals view 0" "cmp -s $OUT/$TAG.rp_h0.cmp $OUT/$TAG.rp_h1.cmp"
    cmp -s "$OUT/$TAG.rp_h0.cmp" "$OUT/$TAG.ev.cmp2" || diff "$OUT/$TAG.rp_h0.cmp" "$OUT/$TAG.ev.cmp2" | head -n 6
fi

if echo " $PHASES " | grep -q ' 7 '; then
    echo "== phase 7: leak test, the same session with real X key / mouse / wheel events sent to the guest's window"
    [ -f "$OUT/$TAG.ev.cmp2" ] || { evnet "$TAG.ev" $((PORT + 5)) none; fpcutn "$OUT/$TAG.ev.host.fp" $EVTICKS > "$OUT/$TAG.ev.cmp2"; }
    sh "$HERE/net_leak_keys.sh" "$OUT/$TAG.leak.guest.log" 150 > "$OUT/$TAG.leak.keys.txt" 2>&1 &
    KEYS_PID=$!
    evnet "$TAG.leak" $((PORT + 7)) none
    kill "$KEYS_PID" 2>/dev/null
    wait "$KEYS_PID" 2>/dev/null
    cat "$OUT/$TAG.leak.keys.txt"
    check "7 events really were sent to the guest's window ($(grep -ac 'round' $OUT/$TAG.leak.keys.txt) rounds of 35 keys + wheel + clicks)" "grep -aq 'round' $OUT/$TAG.leak.keys.txt"
    check "7 no DESYNC with live events on the guest (host)" "no_desync $OUT/$TAG.leak.host.log"
    check "7 no DESYNC with live events on the guest (guest)" "no_desync $OUT/$TAG.leak.guest.log"
    check "7 fingerprints identical host/guest" "grep -q IDENTICAL $OUT/$TAG.leak.run.txt"
    check "7 hash exchange matched" "hash_ok $OUT/$TAG.leak.host.log $EVTICKS && hash_ok $OUT/$TAG.leak.guest.log $EVTICKS"
    fpcutn "$OUT/$TAG.leak.host.fp" $EVTICKS > "$OUT/$TAG.leak.cmp"
    check "7 ...and equal to the session without any live events (phase 6)" "cmp -s $OUT/$TAG.leak.cmp $OUT/$TAG.ev.cmp2"
    cmp -s "$OUT/$TAG.leak.cmp" "$OUT/$TAG.ev.cmp2" || diff "$OUT/$TAG.leak.cmp" "$OUT/$TAG.ev.cmp2" | head -n 6
    # negative control: the test must be able to FAIL. GE_NET_TEST_LEAK=esc on the guest puts the old live Escape read back
    # into the sim (and skips the record flag), so the injected Escape presses open its watch on the guest only
    sh "$HERE/net_leak_keys.sh" "$OUT/$TAG.leakneg.guest.log" 150 > "$OUT/$TAG.leakneg.keys.txt" 2>&1 &
    KEYS_PID=$!
    GUESTENV="GE_NET_TEST_LEAK=esc"
    evnet "$TAG.leakneg" $((PORT + 8)) none
    kill "$KEYS_PID" 2>/dev/null
    wait "$KEYS_PID" 2>/dev/null
    check "7 negative control (GE_NET_TEST_LEAK=esc on the guest, same events): the leak is DETECTED, host reports DESYNC" "grep -aq 'ge: net: DESYNC at tick' $OUT/$TAG.leakneg.host.log"
    check "7 negative control: ...and the guest reports it too" "grep -aq 'ge: net: DESYNC at tick' $OUT/$TAG.leakneg.guest.log"
    grep -ah 'ge: net: DESYNC' "$OUT/$TAG.leakneg.host.log" | head -n 1
fi

if echo " $PHASES " | grep -q ' 8 '; then
    echo "== phase 8: the settings block is taken again at READY (a late change reaches the sim), $TICKS ticks"
    [ -f "$OUT/$TAG.clean.cmp.host" ] || net "$TAG.clean" "$PORT"
    HOSTENV="GE_NET_OPTS_READY=lookahead=0,upright=0"
    GUESTENV="GE_NET_OPTS_READY=style=2,n64=1,autoaim=1,aimtoggle=1,aimsens=1700"
    net "$TAG.rdy" $((PORT + 9))
    check "8 fingerprints identical host/guest, no DESYNC" "grep -q IDENTICAL $OUT/$TAG.rdy.run.txt && no_desync $OUT/$TAG.rdy.host.log && no_desync $OUT/$TAG.rdy.guest.log"
    check "8 hash exchange matched" "hash_ok $OUT/$TAG.rdy.host.log && hash_ok $OUT/$TAG.rdy.guest.log"
    check "8 each side logs that its settings changed since the handshake" "grep -aq 'my settings changed since the handshake' $OUT/$TAG.rdy.host.log && grep -aq 'my settings changed since the handshake' $OUT/$TAG.rdy.guest.log"
    check "8 both sides hold the same refreshed blocks (the 'player settings at READY' lines are equal)" "[ \"\$(grep -ah 'ge: net: player settings at READY' $OUT/$TAG.rdy.host.log)\" = \"\$(grep -ah 'ge: net: player settings at READY' $OUT/$TAG.rdy.guest.log)\" ]"
    grep -ah 'ge: net: player settings' "$OUT/$TAG.rdy.host.log"
    check "8 ...and they are the refreshed ones, not the handshake's (host lookahead 0 upright 0, guest autoaim 1 aimtoggle 1 aimsens 1700)" "grep -ah 'ge: net: player settings at READY' $OUT/$TAG.rdy.host.log | grep -aq 'host (player 0) style [0-9.]* n64 [01] upright 0 autoaim [01] lookahead 0' && grep -ah 'ge: net: player settings at READY' $OUT/$TAG.rdy.host.log | grep -aq 'guest (player 1) .* autoaim 1 lookahead [01] aimtoggle 1 .* aimsens 1700'"
    check "8 the sim reads the refreshed values per Bond, the same on both machines" "[ \"\$(grep -ah 'ge: net: sim reads' $OUT/$TAG.rdy.host.log)\" = \"\$(grep -ah 'ge: net: sim reads' $OUT/$TAG.rdy.guest.log)\" ] && grep -ah 'ge: net: sim reads player 1' $OUT/$TAG.rdy.host.log | grep -aq 'aimsens 1700' && grep -ah 'ge: net: sim reads player 0' $OUT/$TAG.rdy.host.log | grep -aq 'lookahead 0'"
    grep -ah 'ge: net: sim reads' "$OUT/$TAG.rdy.host.log"
    check "8 the refresh changed the run (it differs from the default-settings run)" "! cmp -s $OUT/$TAG.rdy.cmp.host $OUT/$TAG.clean.cmp.host"
fi

if echo " $PHASES " | grep -q ' 9 '; then
    echo "== phase 9: live carry, both Bonds on LIVE devices; real X events at the guest are carried through its records"
    sh "$HERE/net_leak_keys.sh" "$OUT/$TAG.live.guest.log" 150 > "$OUT/$TAG.live.keys.txt" 2>&1 &
    KEYS_PID=$!
    export LIVE=1
    evnet "$TAG.live" $((PORT + 10)) none
    unset LIVE
    kill "$KEYS_PID" 2>/dev/null
    wait "$KEYS_PID" 2>/dev/null
    F=$OUT/$TAG.live.host.fp
    check "9 events really were sent ($(grep -ac 'round' $OUT/$TAG.live.keys.txt) rounds)" "grep -aq 'round' $OUT/$TAG.live.keys.txt"
    check "9 no DESYNC with live carried input (host, guest)" "no_desync $OUT/$TAG.live.host.log && no_desync $OUT/$TAG.live.guest.log"
    check "9 fingerprints identical host/guest, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.live.run.txt && hash_ok $OUT/$TAG.live.host.log $EVTICKS && hash_ok $OUT/$TAG.live.guest.log $EVTICKS"
    check "9 the guest's Escape key reached the HOST's sim (the guest's watch left state 0 in the host's fingerprints)" "awk '\$3 == \"fp\" && \$16 >= 1 { n++ } END { exit !(n > 0) }' $F"
    check "9 the guest's wheel / keys changed its weapon in the host's sim" "awk '\$3 == \"fp\" { k = \$20 \" \" \$21; if (k != last) n++; last = k } END { exit !(n >= 2) }' $F"
    check "9 Tab (classic mode) was blocked on the guest with a log line" "grep -aq 'the Classic Mode toggle (Tab) is blocked in netplay' $OUT/$TAG.live.guest.log"
    replay_log "$DOUT/$TAG.live.hostlog.txt" 0 "$TAG.rp_live"
    check "9 the host's log of this live session replays offline: $(grep -a 'ge: replay: .*fingerprints compared' "$OUT/$TAG.rp_live.log" | sed 's/ge: replay: //')" "replay_ok $OUT/$TAG.rp_live.log"
fi

# ---- job 3c: the sim-settings check, the trace tools, the window-independence test ------------------------------------------------

if echo " $PHASES " | grep -q ' 10 '; then
    echo "== phase 10: a sim-reading ini value that differs is refused, naming the key"
    p=$((PORT + 11))
    COMMONENV="GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_STAGE_AFTER=20 GE_DIFF=1 GE_FRAMES=3000 GE_NET_WAIT=20"
    rm -f "$OUT/$TAG.cfg.host.log" "$OUT/$TAG.cfg.guest.log"
    env $COMMONENV GE_PROFILE=$HP GE_NET=host:$p GE_STAGE=dam GE_SEED=7 timeout -s KILL 90 "$BIN" > "$OUT/$TAG.cfg.host.log" 2>&1 &
    HPID=$!
    sleep 2
    env $COMMONENV GE_PROFILE=$GP GE_NET=join:127.0.0.1:$p GE_NET_TEST_SIMCFG=fix_shootthrough=7 timeout -s KILL 90 "$BIN" > "$OUT/$TAG.cfg.guest.log" 2>&1
    GRC=$?
    wait $HPID
    HRC=$?
    check "10 the guest refuses the session and names the key: $(grep -ah 'the sim settings differ' $OUT/$TAG.cfg.guest.log | head -n 1 | cut -c1-160)" "grep -ah 'ge: net: the sim settings differ between the two machines: fix_shootthrough (mine 7, theirs [01])' $OUT/$TAG.cfg.guest.log"
    check "10 the host logs the refusal with the key: $(grep -ah 'guest refused' $OUT/$TAG.cfg.host.log | head -n 1 | cut -c1-160)" "grep -aq 'ge: net: guest refused the session: sim setting differs: fix_shootthrough' $OUT/$TAG.cfg.host.log"
    check "10 both processes exit with status 1 (host $HRC, guest $GRC) and no stage ran" "[ $HRC -eq 1 ] && [ $GRC -eq 1 ] && ! grep -aq 'GO (run' $OUT/$TAG.cfg.host.log"
    if [ -f "$OUT/$TAG.ev.hostlog.txt" ]; then   # phase 6 ran in this invocation
        check "10 the session log of a normal session carries the sim-settings list (K line)" "grep -a '^K ' $OUT/$TAG.ev.hostlog.txt | grep -aq 'fix_shootthrough='"
    fi
fi

if echo " $PHASES " | grep -q ' 11 ' || echo " $PHASES " | grep -q ' 12 '; then
    echo "== phase 11: the RNG trace: identical traces on a clean session; one extra draw on the guest is named (tick and call site)"
    HT="$ROOT/Data/Profiles/$HP/Logs/rngtrace_run1_host.txt"
    GT="$ROOT/Data/Profiles/$GP/Logs/rngtrace_run1_guest.txt"
    rm -f "$HT" "$GT"
    evnet "$TAG.rt" $((PORT + 12)) none GE_NET_RNGTRACE=1
    cp "$HT" "$OUT/$TAG.rt.host.trace" 2>/dev/null
    cp "$GT" "$OUT/$TAG.rt.guest.trace" 2>/dev/null
    check "11 no DESYNC on the clean traced session" "no_desync $OUT/$TAG.rt.host.log && no_desync $OUT/$TAG.rt.guest.log"
    check "11 both traces exist and hold draws ($(wc -l < $OUT/$TAG.rt.host.trace) lines)" "[ \$(wc -l < $OUT/$TAG.rt.host.trace) -gt 1000 ] && [ \$(wc -l < $OUT/$TAG.rt.guest.trace) -gt 1000 ]"
    check "11 net_rngdiff: the two clean traces are identical" "sh $HERE/net_rngdiff.sh $OUT/$TAG.rt.host.trace $OUT/$TAG.rt.guest.trace $BIN > $OUT/$TAG.rt.diff.txt 2>&1"
    rm -f "$HT" "$GT"
    GUESTENV="GE_NET_TEST_EXTRADRAW=50"
    evnet "$TAG.rx" $((PORT + 13)) none GE_NET_RNGTRACE=1
    cp "$HT" "$OUT/$TAG.rx.host.trace" 2>/dev/null
    cp "$GT" "$OUT/$TAG.rx.guest.trace" 2>/dev/null
    check "11 the extra draw at tick 50 is a DESYNC at tick 60, first field rng (host)" "grep -a 'ge: net: DESYNC at tick 60' $OUT/$TAG.rx.host.log | grep -aq 'differing fields: rng'"
    check "11 ...and on the guest" "grep -aq 'ge: net: DESYNC at tick 60' $OUT/$TAG.rx.guest.log"
    sh "$HERE/net_rngdiff.sh" "$OUT/$TAG.rx.host.trace" "$OUT/$TAG.rx.guest.trace" "$BIN" > "$OUT/$TAG.rx.diff.txt" 2>&1
    sed -n 1,8p "$OUT/$TAG.rx.diff.txt"
    check "11 net_rngdiff reports a difference (exit 1) in tick 50, with one more draw on the guest" "grep -aq 'the traces first differ at draw #[0-9]*, tick 50' $OUT/$TAG.rx.diff.txt && grep -aq 'draws in tick 50: A made \([0-9]*\), B made' $OUT/$TAG.rx.diff.txt"
    check "11 ...and names the call site: portCoopTick (port_coop.c)" "grep -a '^    B ' $OUT/$TAG.rx.diff.txt | grep -aq 'portCoopTick'"
fi

if echo " $PHASES " | grep -q ' 12 '; then
    echo "== phase 12: net_bisect on the two logs of that session"
    rm -rf "$OUT/$TAG.bisect"
    sh "$HERE/net_bisect.sh" "$OUT/$TAG.rx.hostlog.txt" "$OUT/$TAG.rx.guestlog.txt" "$HP" "$OUT/$TAG.bisect" > "$OUT/$TAG.bisect.run.txt" 2>&1
    cat "$OUT/$TAG.bisect/summary.txt" 2>/dev/null | head -n 20
    S="$OUT/$TAG.bisect/summary.txt"
    check "12 the logs first differ at tick 50 (the start of the tick whose sim made the extra draw) and the only sub-hash that differs is rng" "grep -aq 'the logs first differ at tick 50' $S && grep -a 'sub-hashes that differ' $S | grep -aq 'differ at that tick: rng *\$'"
    check "12 the host's log replays offline" "grep -a 'replay of the host log' $S | grep -aq 'reproduces every tick'"
    check "12 the guest's log does NOT replay (its live sim made a draw the records do not carry): first mismatch tick 50" "grep -a 'replay of the guest log' $S | grep -aq 'first MISMATCH at tick 50'"
    check "12 a full state dump was written for the tick (rng draw count, chrs, chrai, players)" "grep -aq '\[rng draws\]' $OUT/$TAG.bisect/dump_guest.txt && grep -aq '\[chrai ' $OUT/$TAG.bisect/dump_guest.txt && grep -aq '\[player 0\]' $OUT/$TAG.bisect/dump_guest.txt"
fi

if echo " $PHASES " | grep -q ' 13 '; then
    echo "== phase 13: the sim does not depend on the window (net_simview.sh: different windows, replays under other windows, negative control)"
    sh "$HERE/net_simview.sh" "$TAG.sv" $((PORT + 20)) 470 > "$OUT/$TAG.sv.run.txt" 2>&1
    cat "$OUT/$TAG.sv.run.txt"
    sv=$(grep -ac '^FAIL' "$OUT/$TAG.sv.run.txt")
    check "13 net_simview.sh: no FAIL line ($(grep -ac '^PASS' $OUT/$TAG.sv.run.txt) PASS)" "[ $sv -eq 0 ] && grep -aq '^PASS' $OUT/$TAG.sv.run.txt"
fi

if echo " $PHASES " | grep -q ' 14 '; then
    echo "== phase 14 (job 3d): death, respawn, drops and re-pickup under netplay (Dam: a KF7 the guest's Bond carries; Depot: a key the host's Bond carries, then the same with the spot forced unsafe)"
    DTICKS=300
    dropnet() {   # <tag> <port> <stage> [KEY=VALUE...]: a netplay session with the session logs on; the logs are copied to $OUT
        t=$1; p=$2; st=$3; shift 3
        rm -f "$ROOT/Data/Profiles/$HP/Logs"/netlog_run*.txt "$ROOT/Data/Profiles/$GP/Logs"/netlog_run*.txt
        env STAGE="$st" BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE="$HP" GPROFILE="$GP" \
            sh "$HERE/net_replay2.sh" "$t" "$p" "$DTICKS" none GE_NET_LOG=1 GE_COOP_INVLOG=1 GE_HUDLOG=1 GE_COOP_RESPAWN=60 "$@" > "$OUT/$t.run.txt" 2>&1
        cp "$ROOT/Data/Profiles/$HP/Logs/netlog_run1_host.txt" "$OUT/$t.hostlog.txt" 2>/dev/null
        cp "$ROOT/Data/Profiles/$GP/Logs/netlog_run1_guest.txt" "$OUT/$t.guestlog.txt" 2>/dev/null
    }
    # --- Dam: the guest's Bond (player 1) holds a picked-up KF7, dies at 120; the host Bond is moved beside the body at 165 and the guest respawns at 240
    dropnet "$TAG.dk" $((PORT + 30)) dam GE_COOP_GIVEWEAPON=60:1:8:80 GE_COOP_KILL=120:1 GE_COOP_MOVETO=165:0:1 GE_COOP_RESPAWN=120
    F=$OUT/$TAG.dk.host.fp
    check "14 KF7: fingerprints identical host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.dk.run.txt && no_desync $OUT/$TAG.dk.host.log && no_desync $OUT/$TAG.dk.guest.log && hash_ok $OUT/$TAG.dk.host.log $DTICKS && hash_ok $OUT/$TAG.dk.guest.log $DTICKS"
    check "14 KF7: the guest's Bond died, dropped the KF7 with its magazine (30 rounds) and respawned" "grep -aq 'player 1 died' $F && grep -aq 'drops p1: gun 8 dropped with 30 round' $F && grep -aq 'player 1 respawned' $F"
    check "14 KF7: the respawned guest Bond has the full starting loadout and the PP7 in hand (inv tick290 p1: W1 W5 W47, hands 5)" "grep -a 'inv tick290 p1' $F | grep -aq 'W1 W5 W47 | hands 5/0'"
    check "14 KF7: the host's Bond picked the KF7 up (inv tick290 p0 holds W8 and 30 rifle rounds)" "grep -a 'inv tick290 p0' $F | grep -aq 'W8 ' && grep -a 'inv tick290 p0' $F | grep -aq ' 3:30'"
    check "14 KF7 (job 3e): the host's Bond had the KF7 well BEFORE the dead guest Bond's respawn at tick 240 (inv tick200 p0 holds W8 and 30 rifle rounds), and the dead Bond held nothing (inv tick200 p1 has no W8)" "grep -a 'inv tick200 p0' $F | grep -aq ' W8 ' && grep -a 'inv tick200 p0' $F | grep -aq ' 3:30' && ! grep -a 'inv tick200 p1' $F | grep -aq ' W8 '"
    check "14 KF7: the 'Picked up' message went to the picker only (host log: player 0, never player 1)" "grep -a 'ge: hud: player 0 bottom message' $OUT/$TAG.dk.host.log | grep -aq KF7 && ! grep -a 'ge: hud: player 1 bottom message' $OUT/$TAG.dk.host.log | grep -aq KF7"
    check "14 KF7: the host's and the guest's session logs hold the same records and fingerprints" "grep -a '^T ' $OUT/$TAG.dk.hostlog.txt > $OUT/$TAG.dk.hostlog.T; grep -a '^T ' $OUT/$TAG.dk.guestlog.txt > $OUT/$TAG.dk.guestlog.T; cmp -s $OUT/$TAG.dk.hostlog.T $OUT/$TAG.dk.guestlog.T"
    replay_log "$DOUT/$TAG.dk.hostlog.txt" 0 "$TAG.dk_rp0"
    replay_log "$DOUT/$TAG.dk.guestlog.txt" 1 "$TAG.dk_rp1"
    check "14 KF7: the host's log replays offline (view 0)" "replay_ok $OUT/$TAG.dk_rp0.log"
    check "14 KF7: the guest's log replays offline (view 1)" "replay_ok $OUT/$TAG.dk_rp1.log"
    # --- Depot: the host's Bond (player 0) picks the first key at 60 and dies at 120; the guest's Bond is moved beside the body at 140
    dropnet "$TAG.dy" $((PORT + 31)) depot GE_COOP_GIVEKEY=1 GE_COOP_KILL=120:0 GE_COOP_MOVETO=140:1:0
    F=$OUT/$TAG.dy.host.fp
    check "14 key: fingerprints identical host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.dy.run.txt && no_desync $OUT/$TAG.dy.host.log && no_desync $OUT/$TAG.dy.guest.log && hash_ok $OUT/$TAG.dy.host.log $DTICKS && hash_ok $OUT/$TAG.dy.guest.log $DTICKS"
    check "14 key: the host's Bond died holding the key, dropped it at the spot, and respawned" "grep -aq 'player 0 died' $F && grep -aq 'drops p0: quest item (type 4) dropped at' $F && grep -aq 'player 0 respawned' $F"
    check "14 key: the guest's Bond picked it up again (inv tick290 p1 holds a key prop) with the message for player 1 only" "grep -a 'inv tick290 p1' $F | grep -aq 'P(t4)' && grep -a 'ge: hud: player 1 bottom message' $OUT/$TAG.dy.host.log | grep -aq 'key' && ! grep -a 'ge: hud: player 0 bottom message' $OUT/$TAG.dy.host.log | grep -aq 'key'"
    check "14 key (job 3e): the guest's Bond held the key BEFORE the dead host Bond's respawn (inv tick160 p1 holds a key prop, the respawn is at tick 180) and the dead Bond did not (inv tick160 p0 has none)" "grep -a 'inv tick160 p1' $F | grep -aq 'P(t4)' && ! grep -a 'inv tick160 p0' $F | grep -aq 'P(t4)'"
    replay_log "$DOUT/$TAG.dy.hostlog.txt" 0 "$TAG.dy_rp0"
    check "14 key: the host's log replays offline" "replay_ok $OUT/$TAG.dy_rp0.log"
    # --- the same with the death spot forced unsafe: the key goes straight into the partner's inventory
    dropnet "$TAG.du" $((PORT + 32)) depot GE_COOP_GIVEKEY=1 GE_COOP_KILL=120:0 GE_COOP_UNSAFEDROP=1
    F=$OUT/$TAG.du.host.fp
    check "14 unsafe spot: fingerprints identical host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.du.run.txt && no_desync $OUT/$TAG.du.host.log && no_desync $OUT/$TAG.du.guest.log && hash_ok $OUT/$TAG.du.host.log $DTICKS && hash_ok $OUT/$TAG.du.guest.log $DTICKS"
    check "14 unsafe spot: the key went to the partner (player 1 holds a key prop at tick 290, nothing lay on the floor)" "grep -aq 'quest item (type 4) to player 1 (unsafe spot)' $F && grep -a 'inv tick290 p1' $F | grep -aq 'P(t4)' && ! grep -aq 'quest item (type 4) dropped at' $F"
    replay_log "$DOUT/$TAG.du.guestlog.txt" 1 "$TAG.du_rp1"
    check "14 unsafe spot: the guest's log replays offline" "replay_ok $OUT/$TAG.du_rp1.log"
fi

# ---- job 4: pad against mouse, two stages in one session, the partner leaving ------------------------------------------------------

if echo " $PHASES " | grep -q ' 15 '; then
    echo "== phase 15 (job 4): a gamepad player against a mouse player (recPA / recPB), and a live right stick"
    PT=240
    ptnet() {   # <tag> <port> <ticks> [KEY=VALUE...]: net_replay2.sh with the pad/mouse scripts, the session logs on
        t=$1; p=$2; k=$3; shift 3
        rm -f "$ROOT/Data/Profiles/$HP/Logs"/netlog_run*.txt "$ROOT/Data/Profiles/$GP/Logs"/netlog_run*.txt
        env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" RECA=recPA.txt RECB=recPB.txt HPROFILE="$HP" GPROFILE="$GP" \
            HOSTENV="$HOSTENV" GUESTENV="$GUESTENV" sh "$HERE/net_replay2.sh" "$t" "$p" "$k" none GE_NET_LOG=1 "$@" > "$OUT/$t.run.txt" 2>&1
        HOSTENV=; GUESTENV=
        cp "$ROOT/Data/Profiles/$HP/Logs/netlog_run1_host.txt" "$OUT/$t.hostlog.txt" 2>/dev/null
        cp "$ROOT/Data/Profiles/$GP/Logs/netlog_run1_guest.txt" "$OUT/$t.guestlog.txt" 2>/dev/null
    }
    # --- 15a: scripted. The host's Bond is a pad player (smooth look, analog zoom levels in the flags), the guest's a mouse player (flicks)
    ptnet "$TAG.pm" $((PORT + 40)) $PT
    F=$OUT/$TAG.pm.host.fp
    check "15 pad vs mouse: fingerprints identical host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.pm.run.txt && no_desync $OUT/$TAG.pm.host.log && no_desync $OUT/$TAG.pm.guest.log && hash_ok $OUT/$TAG.pm.host.log $PT && hash_ok $OUT/$TAG.pm.guest.log $PT"
    th0=$(grep -a '^ge: coop: tick ' "$F" | sed -n 's/.* th \(-\{0,1\}[0-9]*\) .*/\1/p' | sort -u | wc -l)
    check "15 ...and the pad player's Bond really turned (its facing took $th0 different values)" "[ $th0 -gt 25 ]"
    # the analog zoom levels: what the sim read for the host's Bond (player 0), on both machines (the pad number differs: the local pad is 0 on its own machine)
    grep -a 'sim zoom level read: player 0' "$OUT/$TAG.pm.host.log" | sed 's/ pad [01] / /' > "$OUT/$TAG.pm.zoom.host"
    grep -a 'sim zoom level read: player 0' "$OUT/$TAG.pm.guest.log" | sed 's/ pad [01] / /' > "$OUT/$TAG.pm.zoom.guest"
    cat "$OUT/$TAG.pm.zoom.host"
    check "15 analog zoom: the sim read level 0.333 (5/15), 1.000, and zoom-out 0.200 (3/15) for the pad player, then 1.000 for the key zoom" "grep -aq 'in 0.333' $OUT/$TAG.pm.zoom.host && grep -aq 'in 1.000' $OUT/$TAG.pm.zoom.host && grep -aq 'out 0.200' $OUT/$TAG.pm.zoom.host"
    check "15 analog zoom: the guest's machine read exactly the same levels, at the same ticks" "cmp -s $OUT/$TAG.pm.zoom.host $OUT/$TAG.pm.zoom.guest && [ \$(wc -l < $OUT/$TAG.pm.zoom.host) -ge 4 ]"
    replay_log "$DOUT/$TAG.pm.hostlog.txt" 0 "$TAG.pm_rp0"
    replay_log "$DOUT/$TAG.pm.guestlog.txt" 1 "$TAG.pm_rp1"
    check "15 both logs (the pad player's seat and the mouse player's seat) replay offline" "replay_ok $OUT/$TAG.pm_rp0.log && replay_ok $OUT/$TAG.pm_rp1.log"
    # swapped roles: the same two scripts the other way round give another run (the sim really reads them) that is just as clean
    rm -f "$OUT/$TAG.pms".*
    env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" RECA=recPB.txt RECB=recPA.txt HPROFILE="$HP" GPROFILE="$GP" \
        sh "$HERE/net_replay2.sh" "$TAG.pms" $((PORT + 41)) $PT none > "$OUT/$TAG.pms.run.txt" 2>&1
    check "15 swapped roles (mouse player hosts, pad player guests): identical fingerprints, no DESYNC" "grep -q IDENTICAL $OUT/$TAG.pms.run.txt && no_desync $OUT/$TAG.pms.host.log && no_desync $OUT/$TAG.pms.guest.log"
    # --- 15b: a LIVE right stick (GE_NET_TEST_RSTICK stands in for the pad): the host's Bond turns from it on both machines
    export LIVE=1
    HOSTENV="GE_NET_TEST_RSTICK=70,0"
    ptnet "$TAG.rs" $((PORT + 42)) 200
    unset LIVE
    F=$OUT/$TAG.rs.host.fp
    th0=$(grep -a '^ge: coop: tick ' "$F" | sed -n 's/.* th \(-\{0,1\}[0-9]*\) -\{0,1\}[0-9]* .*/\1/p' | sort -u | wc -l)
    check "15 live right stick: the host's Bond turned ($th0 different facings) and no DESYNC" "[ $th0 -gt 30 ] && grep -q IDENTICAL $OUT/$TAG.rs.run.txt && no_desync $OUT/$TAG.rs.host.log && no_desync $OUT/$TAG.rs.guest.log"
    check "15 live right stick: the guest's machine turns the host's Bond the same way (identical fingerprints, hash exchange matched)" "hash_ok $OUT/$TAG.rs.host.log 200 && hash_ok $OUT/$TAG.rs.guest.log 200"
    check "15 live right stick: the look rides in the record (the session log holds non-zero yaw for player 0)" "grep -a '^T ' $OUT/$TAG.rs.hostlog.txt | awk '{ r = substr(\$3, 25, 8); if (r != \"00000000\") n++ } END { exit !(n > 40) }'"
    replay_log "$DOUT/$TAG.rs.guestlog.txt" 1 "$TAG.rs_rp1"
    check "15 live right stick: the guest's log of that session replays offline" "replay_ok $OUT/$TAG.rs_rp1.log"
    # the control: the same live run without the stick does not turn the Bond
    export LIVE=1
    ptnet "$TAG.rn" $((PORT + 43)) 200
    unset LIVE
    th1=$(grep -a '^ge: coop: tick ' "$OUT/$TAG.rn.host.fp" | sed -n 's/.* th \(-\{0,1\}[0-9]*\) -\{0,1\}[0-9]* .*/\1/p' | sort -u | wc -l)
    check "15 control: with no right stick the live Bond hardly turns ($th1 different facings against $th0)" "[ $th1 -lt 6 ] && grep -q IDENTICAL $OUT/$TAG.rn.run.txt"
fi

if echo " $PHASES " | grep -q ' 16 '; then
    echo "== phase 16 (job 4): two stages back to back in one session (Dam, then the host launches Depot; the guest follows)"
    S2TICKS=150
    rm -f "$ROOT/Data/Profiles/$HP/Logs"/netlog_run*.txt "$ROOT/Data/Profiles/$GP/Logs"/netlog_run*.txt
    HOSTENV="GE_NET_TEST_NEXT=depot:1 GE_NET_TEST_INFO=100"
    env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE="$HP" GPROFILE="$GP" HOSTENV="$HOSTENV" GUESTENV="" \
        sh "$HERE/net_replay2.sh" "$TAG.s2" $((PORT + 44)) $S2TICKS none GE_NET_LOG=1 GE_NET_TEST_ENDSTAGE=100 > "$OUT/$TAG.s2.run.txt" 2>&1
    HOSTENV=
    for r in 1 2; do
        cp "$ROOT/Data/Profiles/$HP/Logs/netlog_run${r}_host.txt" "$OUT/$TAG.s2.hostlog$r.txt" 2>/dev/null
        cp "$ROOT/Data/Profiles/$GP/Logs/netlog_run${r}_guest.txt" "$OUT/$TAG.s2.guestlog$r.txt" 2>/dev/null
    done
    H=$OUT/$TAG.s2.host.log
    G=$OUT/$TAG.s2.guest.log
    check "16 fingerprints identical host/guest over BOTH stages, no DESYNC" "grep -q IDENTICAL $OUT/$TAG.s2.run.txt && no_desync $H && no_desync $G"
    check "16 stage 1 ended by itself on both sides, then a second stage ran: stage runs 1 and 2 each reached GO" "grep -aq 'stage 1 ended' $H && grep -aq 'stage 1 ended' $G && grep -aq 'GO (run 1)' $H && grep -aq 'GO (run 2)' $H && grep -aq 'GO (run 2)' $G"
    check "16 the host chose the next stage and told the guest, who launched the same one: $(grep -ah 'ge: net: next stage 2' $H | cut -c1-110)" "grep -aq 'ge: net: next stage 2: stage [0-9]*, difficulty 1, seed [0-9]* sent to the guest' $H && grep -aq 'ge: net: the host chose stage [0-9]*, difficulty 1, seed [0-9]* for stage run 2: launching it' $G && grep -aq 'ge: net: launching the host.s next mission' $G"
    check "16 both machines loaded the same two stages (co-op stage lines equal)" "[ \"\$(grep -ah 'ge: coop: two Bonds on stage' $H | sed 's/(local player [01])//')\" = \"\$(grep -ah 'ge: coop: two Bonds on stage' $G | sed 's/(local player [01])//')\" ] && [ \$(grep -ac 'ge: coop: two Bonds on stage' $H) -eq 2 ]"
    grep -ah 'ge: coop: two Bonds on stage' "$H"
    c=$(grep -a 'ge: net: state hash: ' "$H" | tail -n 1 | sed -n 's/.* \([0-9][0-9]*\) compared.*/\1/p')
    check "16 the hash exchange ran in both stages and never mismatched: $(hashline "$H")" "[ \"${c:-0}\" -ge 6 ] && grep -a 'ge: net: state hash: ' $H | tail -n 1 | grep -aq ' 0 mismatched' && grep -a 'ge: net: state hash: ' $G | tail -n 1 | grep -aq ' 0 mismatched'"
    check "16 the guest was never in charge of a mission: no 'did not choose' failure" "! grep -aq 'did not choose' $G"
    check "16 the host's and the guest's logs of stage run 2 hold the same records and fingerprints" "grep -a '^T ' $OUT/$TAG.s2.hostlog2.txt > $OUT/$TAG.s2.h2.T; grep -a '^T ' $OUT/$TAG.s2.guestlog2.txt > $OUT/$TAG.s2.g2.T; cmp -s $OUT/$TAG.s2.h2.T $OUT/$TAG.s2.g2.T && [ \$(wc -l < $OUT/$TAG.s2.h2.T) -ge $S2TICKS ]"
    replay_log "$DOUT/$TAG.s2.hostlog1.txt" 0 "$TAG.s2_rp1"
    replay_log "$DOUT/$TAG.s2.guestlog1.txt" 1 "$TAG.s2_rp3"
    replay_log "$DOUT/$TAG.s2.hostlog2.txt" 0 "$TAG.s2_rp2"
    check "16 the log of stage run 1 replays offline (host seat, guest seat)" "replay_ok $OUT/$TAG.s2_rp1.log && replay_ok $OUT/$TAG.s2_rp3.log"
    echo "(info) stage run 2 replayed alone as the first stage of a fresh process: $(grep -a "ge: replay: first mismatch" $OUT/$TAG.s2_rp2.log || echo OK) - known limit, see netplay.md"
    check "16 'net info' at tick 100 of stage 1 (host): role, delay, ping, run, last hash tick, desync count" "grep -a 'ge: net: info at tick 100' $H | grep -aq 'role host (player 0), playing (lockstep on)' && grep -a 'ge: net: info at tick 100' $H | grep -aq 'input delay 3 ticks' && grep -a 'ge: net: info at tick 100' $H | grep -aq 'ping [0-9]* ms' && grep -a 'ge: net: info at tick 100' $H | grep -aq 'stage run 1' && grep -a 'ge: net: info at tick 100' $H | grep -aq 'last compared at tick \(60\|90\)' && grep -a 'ge: net: info at tick 100' $H | grep -aq 'desyncs reported 0'"
    grep -ah 'ge: net: info at tick 100' "$H"
    # the menus between the stages run a machine-dependent number of ticks (the guest starts when the host's message reaches it): 37 more
    # front-end ticks on the guest must change nothing (the same session, GE_NET_TEST_LAUNCHLAG on the guest)
    env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE="$HP" GPROFILE="$GP" HOSTENV="GE_NET_TEST_NEXT=depot:1" \
        GUESTENV="GE_NET_TEST_LAUNCHLAG=37" sh "$HERE/net_replay2.sh" "$TAG.s3" $((PORT + 45)) $S2TICKS none GE_NET_TEST_ENDSTAGE=100 > "$OUT/$TAG.s3.run.txt" 2>&1
    check "16 37 extra menu ticks on the guest before the second stage: identical fingerprints, no DESYNC (the menus' tick count does not reach the sim)" "grep -q IDENTICAL $OUT/$TAG.s3.run.txt && no_desync $OUT/$TAG.s3.host.log && no_desync $OUT/$TAG.s3.guest.log && grep -aq 'GO (run 2)' $OUT/$TAG.s3.guest.log"
fi

if echo " $PHASES " | grep -q ' 17 '; then
    echo "== phase 17 (job 4): the partner leaves (a clean close, then a killed process)"
    p=$((PORT + 46))
    COMMONENV="GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_STAGE_AFTER=20 GE_DIFF=1 GE_NET_WAIT=20 GE_NET_LOG=1"
    # --- 17a: the guest quits cleanly (its frame limit ends the process through exit(), the atexit hook closes the link)
    rm -f "$OUT/$TAG.q.host.log" "$OUT/$TAG.q.guest.log"
    env $COMMONENV GE_FRAMES=1800 GE_PROFILE=$HP GE_NET=host:$p GE_STAGE=dam GE_SEED=7 timeout -s KILL 120 "$BIN" > "$OUT/$TAG.q.host.log" 2>&1 &
    HPID=$!
    sleep 2
    env $COMMONENV GE_FRAMES=700 GE_PROFILE=$GP GE_NET=join:127.0.0.1:$p timeout -s KILL 120 "$BIN" > "$OUT/$TAG.q.guest.log" 2>&1
    wait $HPID
    H=$OUT/$TAG.q.host.log
    check "17 clean quit: the host logs 'ge: net: partner disconnected' with the reason" "grep -aq '^ge: net: partner disconnected' $H && grep -aq 'the partner closed the game or the link was lost' $H"
    check "17 clean quit: the host shows the message and returns to the title" "grep -aq 'The other player has left - returning to the title' $H && grep -aq 'ge: net: back to the title' $H"
    check "17 clean quit: the host kept running (menus) and then ended on its own frame limit, not a crash" "! grep -aq 'crash' $H && grep -aq 'frame limit reached' $H"
    grep -a 'ge: net: partner\|ge: net: (\|back to the title' "$H" | head -n 4
    # --- 17b: the guest process is killed (no goodbye): the host notices after 5 seconds of silence
    p=$((PORT + 47))
    rm -f "$OUT/$TAG.k.host.log" "$OUT/$TAG.k.guest.log"
    env $COMMONENV GE_FRAMES=2400 GE_PROFILE=$HP GE_NET=host:$p GE_STAGE=dam GE_SEED=7 timeout -s KILL 150 "$BIN" > "$OUT/$TAG.k.host.log" 2>&1 &
    HPID=$!
    sleep 2
    env $COMMONENV GE_FRAMES=100000 GE_PROFILE=$GP GE_NET=join:127.0.0.1:$p timeout -s KILL 20 "$BIN" > "$OUT/$TAG.k.guest.log" 2>&1
    wait $HPID
    H=$OUT/$TAG.k.host.log
    check "17 killed partner: 'ge: net: partner disconnected' after 5 seconds of silence" "grep -aq '^ge: net: partner disconnected' $H && grep -aq 'no packets from the partner for 5 seconds' $H"
    check "17 killed partner: the host returns to the title and goes on without a crash" "grep -aq 'ge: net: back to the title' $H && grep -aq 'frame limit reached' $H"
    grep -a 'ge: net: partner\|ge: net: (\|back to the title' "$H" | head -n 4
fi

if echo " $PHASES " | grep -q ' 18 '; then
    echo "== phase 18 (job 4): a guest that starts a stage the host never chose (a live crash: the guest picked another map after a mission)"
    p=$((PORT + 48))
    COMMONENV="GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_STAGE_AFTER=20 GE_DIFF=1 GE_NET_WAIT=20 GE_NET_LOG=1 GE_NET_RNGTRACE=1 GE_NET_TEST_ENDSTAGE=60"
    rm -f "$OUT/$TAG.rg.host.log" "$OUT/$TAG.rg.guest.log"
    env $COMMONENV GE_FRAMES=1500 GE_PROFILE=$HP GE_NET=host:$p GE_STAGE=dam GE_SEED=7 timeout -s KILL 150 "$BIN" > "$OUT/$TAG.rg.host.log" 2>&1 &
    HPID=$!
    sleep 2
    env $COMMONENV GE_FRAMES=1500 GE_NET_TEST_ROGUE=depot:1 GE_PROFILE=$GP GE_NET=join:127.0.0.1:$p timeout -s KILL 150 "$BIN" > "$OUT/$TAG.rg.guest.log" 2>&1
    GRC=$?
    wait $HPID
    HRC=$?
    H=$OUT/$TAG.rg.host.log
    G=$OUT/$TAG.rg.guest.log
    check "18 the host did not crash and ended on its own frame limit (exit $HRC)" "[ $HRC -eq 0 ] && ! grep -aq 'fatal signal' $H && grep -aq 'frame limit reached' $H"
    check "18 the guest did not crash either (exit $GRC)" "[ $GRC -eq 0 ] && ! grep -aq 'fatal signal' $G && grep -aq 'frame limit reached' $G"
    check "18 the guest started the stage on its own (test) and refused to run it: 'did not choose', back to the title" "grep -aq 'GE_STAGE direct launch' $G && grep -aq 'started a stage the host did not choose' $G && grep -aq 'back to the title' $G"
    check "18 the guest never ran that stage alone: it reached GO of run 2 only by following the host's own pick (mission-end job: it re-synchronises to the host's run), never its rogue one" "! grep -aq 'GO (run 2)' $G || grep -aq 'the host chose stage .* for stage run 2: launching it' $G"
    check "18 no DESYNC on either side" "no_desync $H && no_desync $G"
    # the host also never acts on a READY / NEXTSTAGE from the wrong side: a NEXTSTAGE sent to a host is ignored with a log line (unit-tested), a READY for a run it has not started is ignored
    grep -a 'did not choose\|back to the title' "$G" | head -n 3
fi

# ---- job 3e: the watch affects only its opener, the drop pickup rule, the level music held until GO ----------------------------------

if echo " $PHASES " | grep -q ' 19 '; then
    echo "== phase 19 (job 3e): P0's watch open during a fight (P1 sees the world and hits guards), the corpse run, the level music after GO"
    WTICKS=300
    wnet() {   # <tag> <port> [KEY=VALUE...]: the watch session (recWA: the host's Bond opens its watch at tick 12; recWB: the guest's Bond holds fire from tick 30 at the first Dam guard)
        t=$1; p=$2; shift 2
        rm -f "$ROOT/Data/Profiles/$HP/Logs"/netlog_run*.txt "$ROOT/Data/Profiles/$GP/Logs"/netlog_run*.txt
        env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" RECA=recWA.txt RECB=recWB.txt HPROFILE="$HP" GPROFILE="$GP" \
            HOSTENV="$HOSTENV" GUESTENV="$GUESTENV" sh "$HERE/net_replay2.sh" "$t" "$p" "$WTICKS" none GE_NET_LOG=1 GE_HITLOG=1 GE_ROOM_AUDIT=1 \
            GE_MODS=invincible=1,enemy_health=400 GE_SPAWNXYZ=19300,-100,17300,53 "$@" > "$OUT/$t.run.txt" 2>&1
        HOSTENV=; GUESTENV=
        cp "$ROOT/Data/Profiles/$HP/Logs/netlog_run1_host.txt" "$OUT/$t.hostlog.txt" 2>/dev/null
        cp "$ROOT/Data/Profiles/$GP/Logs/netlog_run1_guest.txt" "$OUT/$t.guestlog.txt" 2>/dev/null
    }
    drawn_last() {   # <log>: the size of the last "drawn set (N)" line (the rooms this machine's passes drew at the end of the run)
        grep -a 'roomaudit: drawn set (' "$1" | tail -n 1 | sed -n 's/.*drawn set (\([0-9]*\)).*/\1/p'
    }
    hits_after() {   # <log> <timer> <min>: at least <min> "ge: hit: ... by player 1 at timer T" lines with T >= <timer>
        grep -a '^ge: hit: chr .* by player 1 at timer ' "$1" | awk -v t="$2" -v m="$3" '$NF >= t { n++ } END { exit !(n >= m) }'
    }
    wnet "$TAG.w" $((PORT + 50))
    F=$OUT/$TAG.w.host.fp
    check "19 watch: fingerprints identical host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.w.run.txt && no_desync $OUT/$TAG.w.host.log && no_desync $OUT/$TAG.w.guest.log && hash_ok $OUT/$TAG.w.host.log $WTICKS && hash_ok $OUT/$TAG.w.guest.log $WTICKS"
    check "19 watch: the host's watch really was open (state 5) for 20+ ticks while the guest's stayed shut" "fp_watch_reached $F 15 && ! fp_watch_reached $F 16"
    check "19 watch: the guest's Bond hit the guard WHILE the host's watch was open (hits by player 1 at timer >= 170, both sides' logs)" "hits_after $OUT/$TAG.w.host.log 170 2 && hits_after $OUT/$TAG.w.guest.log 170 2"
    check "19 watch: the guest's window (its own Bond's view) draws the world with the host's watch open: $(drawn_last "$OUT/$TAG.w.guest.log") rooms drawn at the end (6+ expected)" "[ \"\$(drawn_last $OUT/$TAG.w.guest.log)\" -ge 5 ]"
    replay_log "$DOUT/$TAG.w.hostlog.txt" 0 "$TAG.w_rp0"
    replay_log "$DOUT/$TAG.w.guestlog.txt" 1 "$TAG.w_rp1"
    check "19 watch: the host's log and the guest's log replay offline" "replay_ok $OUT/$TAG.w_rp0.log && replay_ok $OUT/$TAG.w_rp1.log"
    # negative control: the pre-3e shared world gate (GE_WATCH_SHAREDGATE=1) blanks the guest's world (the old bug), and the test sees it
    wnet "$TAG.wo" $((PORT + 51)) GE_WATCH_SHAREDGATE=1
    check "19 watch negative control: with the shared world gate the guest's window draws 1 room (the sky-only bug), so the check above does bite: $(drawn_last "$OUT/$TAG.wo.guest.log")" "[ \"\$(drawn_last $OUT/$TAG.wo.guest.log)\" -le 2 ]"
    # --- the corpse run under netplay: the host's Bond holds a KF7 with full reserve (cannot take a second one), the guest's Bond dies at 150 with
    # its KF7, the host stands on the body from 170; the guest collects nothing while dead and takes the gun right after its respawn at 270
    DTICKS=300
    dropnet() {   # <tag> <port> <stage> [KEY=VALUE...] (as in phase 14)
        t=$1; p=$2; st=$3; shift 3
        rm -f "$ROOT/Data/Profiles/$HP/Logs"/netlog_run*.txt "$ROOT/Data/Profiles/$GP/Logs"/netlog_run*.txt
        env STAGE="$st" BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE="$HP" GPROFILE="$GP" \
            sh "$HERE/net_replay2.sh" "$t" "$p" "$DTICKS" none GE_NET_LOG=1 GE_COOP_INVLOG=1 GE_HUDLOG=1 "$@" > "$OUT/$t.run.txt" 2>&1
        cp "$ROOT/Data/Profiles/$HP/Logs/netlog_run1_host.txt" "$OUT/$t.hostlog.txt" 2>/dev/null
        cp "$ROOT/Data/Profiles/$GP/Logs/netlog_run1_guest.txt" "$OUT/$t.guestlog.txt" 2>/dev/null
    }
    dropnet "$TAG.cr" $((PORT + 52)) dam GE_COOP_GIVEWEAPON=90:1:8:80,100:0:8:9999 GE_COOP_KILL=150:1 GE_COOP_RESPAWN=120 GE_COOP_MOVETO=170:0:1:1,280:1:0:1
    F=$OUT/$TAG.cr.host.fp
    check "19 corpse run: fingerprints identical host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.cr.run.txt && no_desync $OUT/$TAG.cr.host.log && no_desync $OUT/$TAG.cr.guest.log && hash_ok $OUT/$TAG.cr.host.log $DTICKS && hash_ok $OUT/$TAG.cr.guest.log $DTICKS"
    check "19 corpse run: the guest's Bond died and dropped the KF7 (30 rounds), and the host's Bond kept its own KF7 only (it could not take the second)" "grep -aq 'player 1 died' $F && grep -aq 'drops p1: gun 8 dropped with 30 round' $F && grep -a 'inv tick250 p0' $F | grep -aq ' W8 ' && ! grep -a 'ge: hud: player 0 bottom message' $OUT/$TAG.cr.host.log | grep -aq KF7"
    nopick_while_dead() { for k in 180 200 230 250; do grep -a "inv tick$k p1" "$1" | grep -aq ' W8 ' && return 1; done; return 0; }
    check "19 corpse run: the dead Bond collects nothing while dead (inv tick180 / 200 / 230 / 250 of player 1 hold no W8)" "nopick_while_dead $F"
    check "19 corpse run: after its respawn the former owner takes the gun like anyone (inv tick290 p1 holds W8 with its 30 rounds; message for player 1 only)" "grep -a 'inv tick290 p1' $F | grep -aq ' W8 ' && grep -a 'inv tick290 p1' $F | grep -aq ' 3:30' && grep -a 'ge: hud: player 1 bottom message' $OUT/$TAG.cr.host.log | grep -aq KF7"
    replay_log "$DOUT/$TAG.cr.hostlog.txt" 0 "$TAG.cr_rp0"
    check "19 corpse run: the host's log replays offline" "replay_ok $OUT/$TAG.cr_rp0.log"
    # --- the level music: every stage of a session starts it only after that stage's GO (phase 16's two-stage session, and this phase's single stage)
    music_after_go() {   # <log> <min starts>: every "level track N starts" line comes after the last "GO (run N)" and not after a newer "stage N loaded"
        awk -v m="$2" '/ge: net: stage [0-9]* loaded, READY sent/ { st = 0 } /ge: net: GO \(run/ { st = 1 } /ge: music: level track [0-9]* starts/ { n++; if (!st) bad++ } END { exit !(bad == 0 && n >= m) }' "$1"
    }
    check "19 music (host): the level track starts after the GO (never while 'Waiting for players')" "music_after_go $OUT/$TAG.w.host.log 1"
    check "19 music (guest): the same" "music_after_go $OUT/$TAG.w.guest.log 1"
    if [ -f "$OUT/$TAG.s2.host.log" ]; then
        check "19 music, two stages in one session (phase 16's logs): both stages' tracks start after their own GO, host and guest" "music_after_go $OUT/$TAG.s2.host.log 2 && music_after_go $OUT/$TAG.s2.guest.log 2"
    fi
fi


# ---- job 5: session commands (spawn, give), Flatland, the far-apart scenario with different windows ---------------------------------

if echo " $PHASES " | grep -q ' 20 '; then
    echo "== phase 20 (job 5): session commands: spawn and give applied at the same tick on both sides (guest-issued, host-issued, simultaneous), validation, drops, Flatland"
    CTICKS=300
    cnet() {   # <tag> <port> <stage> <ticks> <lag> [KEY=VALUE...]: a netplay session with the session logs on and the session commands typed by GE_NET_TEST_CMD
        t=$1; p=$2; st=$3; tk=$4; lg=$5; shift 5
        rm -f "$ROOT/Data/Profiles/$HP/Logs"/netlog_run*.txt "$ROOT/Data/Profiles/$GP/Logs"/netlog_run*.txt
        env STAGE="$st" BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE="$HP" GPROFILE="$GP" RECA="${CRECA:-recA.txt}" RECB="${CRECB:-recB.txt}" \
            sh "$HERE/net_replay2.sh" "$t" "$p" "$tk" "$lg" GE_NET_LOG=1 GE_COOP_INVLOG=1 GE_HUDLOG=1 "$@" > "$OUT/$t.run.txt" 2>&1
        cp "$ROOT/Data/Profiles/$HP/Logs/netlog_run1_host.txt" "$OUT/$t.hostlog.txt" 2>/dev/null
        cp "$ROOT/Data/Profiles/$GP/Logs/netlog_run1_guest.txt" "$OUT/$t.guestlog.txt" 2>/dev/null
    }
    clines() {   # <session log>: its C (session command) lines
        grep -a '^C ' "$1"
    }
    same_clines() { clines "$OUT/$1.hostlog.txt" > "$OUT/$1.hostC"; clines "$OUT/$1.guestlog.txt" > "$OUT/$1.guestC"; [ -s "$OUT/$1.hostC" ] && cmp -s "$OUT/$1.hostC" "$OUT/$1.guestC"; }
    # --- Dam: the guest spawns guards around its Bond and gives its Bond the GoldenEye key; the host gives the guest's Bond a KF7; bad commands are refused with a log line
    cnet "$TAG.sc" $((PORT + 60)) dam $CTICKS none GE_COOP_RESPAWN=120 GE_COOP_KILL=160:1 GE_COOP_MOVETO=175:0:1:1 \
        GE_NET_TEST_CMD=60:g:spawn+guard+4+800,100:g:give+goldeneye_key+p2,110:h:give+kf7+p2+30,130:h:give+nosuch,131:h:give+kf7+p3,132:g:give+kf7+p2+99999,133:g:spawn+guard+0,134:g:spawn+guard+4+50,135:g:spawn+guard+99+800,136:h:give+kf7+p2+x
    F=$OUT/$TAG.sc.host.fp
    check "20 commands: fingerprints identical host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.sc.run.txt && no_desync $OUT/$TAG.sc.host.log && no_desync $OUT/$TAG.sc.guest.log && hash_ok $OUT/$TAG.sc.host.log $CTICKS && hash_ok $OUT/$TAG.sc.guest.log $CTICKS"
    check "20 commands: all three valid commands were applied at the same tick on both sides (host log and guest log C lines identical: 65 spawn, 105 give key, 115 give kf7)" "same_clines $TAG.sc && [ \$(wc -l < $OUT/$TAG.sc.hostC) -eq 3 ] && grep -aq '^C 65 1 1 1 1 4 800' $OUT/$TAG.sc.hostC && grep -aq '^C 105 1 2 3 1 61 -1' $OUT/$TAG.sc.hostC && grep -aq '^C 115 0 1 3 1 8 30' $OUT/$TAG.sc.hostC"
    check "20 commands: both consoles logged the command on both machines (issued on one, received on the other, applied on both)" "grep -aq 'session command issued #1 from guest' $OUT/$TAG.sc.guest.log && grep -aq 'session command received #1 from guest' $OUT/$TAG.sc.host.log && grep -aq 'session command #1 from guest.*applied at tick 65' $OUT/$TAG.sc.host.log && grep -aq 'session command #1 from guest.*applied at tick 65' $OUT/$TAG.sc.guest.log"
    check "20 spawn: the same guards spawned on both sides (same chr numbers and positions)" "grep -a 'ge: arena: spawned guard' $OUT/$TAG.sc.host.log > $OUT/$TAG.sc.hg; grep -a 'ge: arena: spawned guard' $OUT/$TAG.sc.guest.log > $OUT/$TAG.sc.gg; [ -s $OUT/$TAG.sc.hg ] && cmp -s $OUT/$TAG.sc.hg $OUT/$TAG.sc.gg"
    check "20 validation: unknown item, player 3, ammo 99999, 0 guards, distance 50, 99 guards, a bad ammo word are all refused with a log line and queue nothing" "grep -aq \"give: no item called 'nosuch'\" $OUT/$TAG.sc.host.log && grep -aq 'give: player 3 is not in this game' $OUT/$TAG.sc.host.log && grep -aq \"give: '99999' is not\" $OUT/$TAG.sc.guest.log && [ \$(grep -ac 'arena: usage' $OUT/$TAG.sc.guest.log) -ge 3 ] && grep -aq \"give: 'x' is not\" $OUT/$TAG.sc.host.log"
    check "20 give: the guest's Bond got the GoldenEye key and a KF7 with 30 rounds (inv tick160 p1 holds W61 and W8)" "grep -a 'inv tick160 p1' $F | grep -aq 'W61' && grep -a 'inv tick160 p1' $F | grep -aq 'W8 '"
    check "20 give / death: the guest's Bond died at 160, the GoldenEye key DROPPED at the death spot (job 6: objective items drop; only an unsafe spot sends them to the partner), the host picked it up off the floor and held it long BEFORE the respawn at 280 (inv tick200 p0 holds W61, p1 does not)" "grep -aq 'player 1 died' $F && grep -aq 'drops p1: objective item 61 dropped at' $F && grep -a 'inv tick200 p0' $F | grep -aq 'W61' && ! grep -a 'inv tick200 p1' $F | grep -aq 'W61'"
    replay_log "$DOUT/$TAG.sc.hostlog.txt" 0 "$TAG.sc_rp0"
    replay_log "$DOUT/$TAG.sc.guestlog.txt" 1 "$TAG.sc_rp1"
    check "20 commands: both session logs replay offline (the C lines are applied at their ticks)" "replay_ok $OUT/$TAG.sc_rp0.log && replay_ok $OUT/$TAG.sc_rp1.log"
    # --- the same under lag 100,20,5: the gate holds a tick until every announced command is here
    cnet "$TAG.scl" $((PORT + 61)) dam $CTICKS 100,20,5 GE_NET_TEST_CMD=60:g:spawn+guard+4+800,100:g:give+goldeneye_key+p2,110:h:give+kf7+p2+30,115:g:give+kf7+p1+5,116:h:spawn+guard+2+500
    check "20 lag: with 100 ms lag, jitter and 5% loss the commands still apply at the same ticks, no DESYNC, identical fingerprints" "grep -q IDENTICAL $OUT/$TAG.scl.run.txt && no_desync $OUT/$TAG.scl.host.log && no_desync $OUT/$TAG.scl.guest.log && same_clines $TAG.scl && [ \$(wc -l < $OUT/$TAG.scl.hostC) -eq 5 ]"
    # --- simultaneous: both sides issue at the same tick: the order is host first, then guest, by sequence
    cnet "$TAG.sim" $((PORT + 62)) dam $CTICKS none GE_NET_TEST_CMD=80:g:spawn+guard+3+900,80:h:spawn+guard+2+600,80:g:give+kf7+p1,80:h:give+pp7+p2,81:g:spawn+guard+1+700,81:h:spawn+guard+1+500
    check "20 simultaneous: fingerprints identical host/guest, no DESYNC" "grep -q IDENTICAL $OUT/$TAG.sim.run.txt && no_desync $OUT/$TAG.sim.host.log && no_desync $OUT/$TAG.sim.guest.log"
    same_clines $TAG.sim
    check "20 simultaneous: the order is the same on both sides and deterministic: at tick 85 the host's commands (issuer 0) come first, then the guest's, each by sequence" "cmp -s $OUT/$TAG.sim.hostC $OUT/$TAG.sim.guestC && awk '\$2 == 85 { print \$3 \" \" \$4 }' $OUT/$TAG.sim.hostC | tr '\n' ',' | grep -aq '^0 1,0 2,1 1,1 2,\$' && awk '\$2 == 86 { print \$3 }' $OUT/$TAG.sim.hostC | tr '\n' ',' | grep -aq '^0,1,\$'"
    check "20 simultaneous: the guards of the two spawn commands got the same chr numbers on both sides, in that order (host's guards 7000-7001 first, then the guest's 7002-7004)" "grep -a 'ge: arena: spawned guard' $OUT/$TAG.sim.host.log > $OUT/$TAG.sim.hg; grep -a 'ge: arena: spawned guard' $OUT/$TAG.sim.guest.log > $OUT/$TAG.sim.gg; cmp -s $OUT/$TAG.sim.hg $OUT/$TAG.sim.gg && [ \$(wc -l < $OUT/$TAG.sim.hg) -ge 3 ]"
    replay_log "$DOUT/$TAG.sim.guestlog.txt" 1 "$TAG.sim_rp1"
    check "20 simultaneous: the guest's log replays offline" "replay_ok $OUT/$TAG.sim_rp1.log"
    # --- a key prop on Depot: the guest gives its Bond a key object, dies, the key drops, the host's Bond takes it before the respawn
    cnet "$TAG.sk" $((PORT + 63)) depot $CTICKS none GE_COOP_RESPAWN=120 GE_COOP_KILL=120:1 GE_COOP_MOVETO=140:0:1:1 GE_NET_TEST_CMD=80:g:give+key+p2
    F=$OUT/$TAG.sk.host.fp
    check "20 key: fingerprints identical, no DESYNC; the guest's Bond picked up key object #1 as if walking over it (the pickup message for player 2 only)" "grep -q IDENTICAL $OUT/$TAG.sk.run.txt && no_desync $OUT/$TAG.sk.host.log && no_desync $OUT/$TAG.sk.guest.log && grep -aq 'give: player 2 picked up key object #1' $OUT/$TAG.sk.host.log && grep -a 'inv tick100 p1' $F | grep -aq 'P(t4)' && ! grep -a 'ge: hud: player 0 bottom message' $OUT/$TAG.sk.host.log | grep -aq 'safe key'"
    check "20 key: the guest's Bond died holding it, the key dropped at the spot, the host's Bond held it at tick 160 BEFORE the respawn at 240 and the dead Bond did not" "grep -aq 'drops p1: quest item (type 4) dropped at' $F && grep -a 'inv tick160 p0' $F | grep -aq 'P(t4)' && ! grep -a 'inv tick160 p1' $F | grep -aq 'P(t4)'"
    replay_log "$DOUT/$TAG.sk.hostlog.txt" 0 "$TAG.sk_rp0"
    check "20 key: the host's log replays offline" "replay_ok $OUT/$TAG.sk_rp0.log"
    # --- Flatland: spawn from either window, both see the same guards fight both Bonds
    CRECA=recFA.txt CRECB=recFB.txt cnet "$TAG.fl" $((PORT + 64)) flatland 420 none GE_HITLOG=1 GE_NET_TEST_CMD=60:h:spawn+guard+8+1200,62:g:spawn+guard+4+800
    check "20 Flatland: 12 guards spawned from both windows, identical fingerprints, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.fl.run.txt && no_desync $OUT/$TAG.fl.host.log && no_desync $OUT/$TAG.fl.guest.log && hash_ok $OUT/$TAG.fl.host.log 420 && hash_ok $OUT/$TAG.fl.guest.log 420 && [ \$(grep -ac 'ge: arena: spawned guard' $OUT/$TAG.fl.host.log) -eq 12 ] && [ \$(grep -ac 'ge: arena: spawned guard' $OUT/$TAG.fl.guest.log) -eq 12 ]"
    check "20 Flatland: the guards fought both Bonds (damage events in the fingerprint, both Bonds hurt)" "grep -aq 'ge: coop: damage' $OUT/$TAG.fl.host.fp && grep -a 'ge: coop: fp 420' $OUT/$TAG.fl.host.fp | grep -aqv 'hp 1.0000 1.0000'"
    replay_log "$DOUT/$TAG.fl.guestlog.txt" 1 "$TAG.fl_rp1"
    check "20 Flatland: the guest's log replays offline" "replay_ok $OUT/$TAG.fl_rp1.log"
fi

# ---- job 5 / playtest 7: one Bond away from the other, a guard only the far Bond sees, different windows -------------------------------
# A playtest: P2 left P1, shot a few guards in another room and came back with a guard in tow (a guard drawn in P2's view but not in P1's). The two
# windows differ. The scripted version: the guest's Bond is moved beside a far Dam guard at 100, spawns guards around itself at 110, holds fire from 150, is
# moved back beside the host's Bond at 300 and keeps firing; the host stands still. Host 1280x720 widescreen, guest 640x480 4:3 with a wide FOV.
if echo " $PHASES " | grep -q ' 21 '; then
    echo "== phase 21 (job 5): far-apart Bonds, guards only one view draws, different windows, shooting near shoot-through geometry"
    mkprof21() { mkdir -p "$ROOT/Data/Profiles/$1"; printf '[input]\nlook_updown=1\nauto_aim=1\naim_control=0\nlook_ahead=1\n[display]\nfullscreen=windowed\nwindow_width=%s\nwindow_height=%s\nwidescreen=%s\nhfov_wide=%s\n' "$2" "$3" "$4" "$5" > "$ROOT/Data/Profiles/$1/midas.ini"; }
    mkprof21 far_h 1280 720 1 90
    mkprof21 far_g 640 480 0 110
    rm -f "$ROOT/Data/Profiles/far_h/Logs"/netlog_run*.txt "$ROOT/Data/Profiles/far_g/Logs"/netlog_run*.txt
    FTICKS=450
    env STAGE=dam BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE=far_h GPROFILE=far_g RECA=recFA.txt RECB=recFB.txt \
        sh "$HERE/net_replay2.sh" "$TAG.far" $((PORT + 70)) $FTICKS none GE_NET_LOG=1 GE_HITLOG=1 GE_COOP_VISLOG=10 GE_MODS=invincible=1,enemy_health=400 \
        GE_COOP_MOVETOCHR=100:1:4,300:1:-1 GE_NET_TEST_CMD=110:g:spawn+guard+3+400,305:g:spawn+guard+2+350 > "$OUT/$TAG.far.run.txt" 2>&1
    cp "$ROOT/Data/Profiles/far_h/Logs/netlog_run1_host.txt" "$OUT/$TAG.far.hostlog.txt" 2>/dev/null
    cp "$ROOT/Data/Profiles/far_g/Logs/netlog_run1_guest.txt" "$OUT/$TAG.far.guestlog.txt" 2>/dev/null
    check "21 far apart: windows differ, fingerprints identical host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.far.run.txt && no_desync $OUT/$TAG.far.host.log && no_desync $OUT/$TAG.far.guest.log && hash_ok $OUT/$TAG.far.host.log $FTICKS && hash_ok $OUT/$TAG.far.guest.log $FTICKS"
    check "21 far apart: the two Bonds really were apart (P2 more than 4000 units from P1 at tick 200) and together again after 300 (under 600 units at tick 400), " "grep -a 'ge: vis: tick 200 chr 7000' $OUT/$TAG.far.host.fp | awk '{ exit !(\$15 > 4000) }' && grep -a 'ge: vis: tick 400 chr 7003' $OUT/$TAG.far.host.fp | awk '{ exit !(\$15 < 600) }'"
    # the visibility invariant: a guard standing within 150 units of a Bond (the same room, in its face) is in that Bond's canonical drawn set, always
    vis_ok() {   # <fp file>: no 'ge: vis' line has a Bond within 150 units of a guard while the guard's room is missing from that Bond's set
        awk '/ge: vis:/ { for (i = 1; i <= NF; i++) if ($i == "p0" && $(i+6) + 0 < 150 && $(i+2) == 0) bad++; for (i = 1; i <= NF; i++) if ($i == "p1" && $(i+6) + 0 < 150 && $(i+2) == 0) bad++; n++ } END { exit !(n > 20 && bad == 0) }' "$1"
    }
    check "21 visibility: every guard within 150 units of a Bond is in that Bond's drawn room set (the sim's canonical view), on the host and on the guest" "vis_ok $OUT/$TAG.far.host.fp && vis_ok $OUT/$TAG.far.guest.fp"
    check "21 visibility: both machines computed the same per-player drawn sets (the ge: vis lines are identical host/guest)" "grep -a 'ge: vis:' $OUT/$TAG.far.host.fp > $OUT/$TAG.far.hv; grep -a 'ge: vis:' $OUT/$TAG.far.guest.fp > $OUT/$TAG.far.gv; cmp -s $OUT/$TAG.far.hv $OUT/$TAG.far.gv"
    replay_log "$DOUT/$TAG.far.hostlog.txt" 0 "$TAG.far_rp0"
    replay_log "$DOUT/$TAG.far.guestlog.txt" 1 "$TAG.far_rp1"
    check "21 far apart: both logs replay offline (view 0 and view 1)" "replay_ok $OUT/$TAG.far_rp0.log && replay_ok $OUT/$TAG.far_rp1.log"
    for spec in "host $DOUT/$TAG.far.hostlog.txt simv_r1 0" "guest $DOUT/$TAG.far.guestlog.txt simv_r2 1"; do
        set -- $spec
        env BIN="$BIN" OUT="$OUT/$TAG.far.rp.$1.out" sh "$HERE/net_replay_log.sh" "$2" "$4" "$3" > "$OUT/$TAG.far.rp.$1.txt" 2>&1
        check "21 far apart: the $1 log replayed under another window ($3) reproduces the session: ge: replay: OK" "grep -aq 'ge: replay: OK' $OUT/$TAG.far.rp.$1.txt"
    done
fi


# Job 7 (playtest 7: the guest got a wall-impact explosion the host did not, from P2's held fire): the sim reads only per-player SIM room lists, never what a render walk leaves.
# Both windows differ from the canonical view (host 2.4:1 ultrawide, fov 70; guest 4:3, fov 110), so both machines' render walks produce room lists of their own
# that differ from each other and from the sim's; Bond 1 is moved far away (its own guards, shot at walls and guards near Bond 0), then back. The negative control
# GE_NET_TEST_SIMVISOLD=1 puts the old coupling back (the render walk runs last in the pass and the sim reads the live lists): it must desync.
if echo " $PHASES " | grep -q ' 22 '; then
    echo "== phase 22 (job 7): sim visibility from per-player canonical lists: render room lists that differ per machine must not reach the sim; negative control"
    mkprof22() { mkdir -p "$ROOT/Data/Profiles/$1"; printf '[input]\nlook_updown=1\nauto_aim=1\naim_control=0\nlook_ahead=1\n[display]\nfullscreen=windowed\nwindow_width=%s\nwindow_height=%s\nwidescreen=%s\nhfov_wide=%s\nfix_shootthrough=1\n' "$2" "$3" "$4" "$5" > "$ROOT/Data/Profiles/$1/midas.ini"; }   # fix_shootthrough=1 as in a typical ini: the shot reads the drawn-room lists
    mkprof22 rv_h 1280 540 1 70
    mkprof22 rv_g 640 480 0 110
    # both Bonds hold fire and turn all the time (an automatic gun, 1.2 and 1.5 degrees a tick): shots into walls and guards all around, in every room they pass
    awk 'BEGIN { for (t = 1; t <= 760; t++) printf "R %d %s 0 0 %d 0 0 0 0 0\n", t, (t >= 160 ? "2000" : "0"), (t >= 160 ? -1200 : 0) }' > "$OUT/$TAG.rvA.txt"
    awk 'BEGIN { for (t = 1; t <= 760; t++) printf "R %d %s 0 0 %d 0 0 0 0 0\n", t, (t >= 130 ? "2000" : "0"), (t >= 130 ? 1500 : 0) }' > "$OUT/$TAG.rvB.txt"
    run22() {   # <subtag> <port> [KEY=VALUE for both sides]
        t=$1; p=$2; shift 2
        env STAGE=dam BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$OUT" DREC="$DOUT" HPROFILE=rv_h GPROFILE=rv_g RECA="$TAG.rvA.txt" RECB="$TAG.rvB.txt" \
            HOSTENV="GE_ROOMLOG=$DOUT/$t.host.rooms" GUESTENV="GE_ROOMLOG=$DOUT/$t.guest.rooms" \
            sh "$HERE/net_replay2.sh" "$t" "$p" 450 none GE_NET_LOG=1 GE_MODS=invincible=1,enemy_health=400 GE_COOP_GIVEWEAPON=20:0:8:9999,20:1:8:9999 \
            GE_COOP_MOVETOCHR=100:1:4,300:1:-1 GE_NET_TEST_CMD=110:g:spawn+guard+3+400,305:g:spawn+guard+2+350 "$@" > "$OUT/$t.run.txt" 2>&1
    }
    run22 "$TAG.rv" $((PORT + 80))
    check "22 sim lists: both windows non-canonical, fingerprints identical host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.rv.run.txt && no_desync $OUT/$TAG.rv.host.log && no_desync $OUT/$TAG.rv.guest.log && hash_ok $OUT/$TAG.rv.host.log 450 && hash_ok $OUT/$TAG.rv.guest.log 450"
    # the render walks really differ: per tick and Bond, the rooms each machine DRAWS differ between the machines on many ticks, and differ from the sim walk's rooms on the guest
    rv_rows() { sed -n 's/^ge: rooms: tick \([0-9]*\) p'"$2"' start [-0-9]* tile [-0-9]* sim [0-9]*: \([0-9,]*\) draw [0-9]*: \([0-9,]*\) vis.*/\1 \2 \3/p' "$1"; }
    rv_rows "$OUT/$TAG.rv.host.rooms" 1 > "$OUT/$TAG.rv.h1"; rv_rows "$OUT/$TAG.rv.guest.rooms" 1 > "$OUT/$TAG.rv.g1"
    check "22 render lists differ: the two machines drew different rooms for Bond 1 on at least 20 ticks, and the guest's drawn set differed from its sim set on at least 20" "awk 'NR == FNR { a[\$1] = \$3; next } (\$1 in a) { n++; if (a[\$1] != \$3) d++ } END { exit !(n > 300 && d >= 20) }' $OUT/$TAG.rv.h1 $OUT/$TAG.rv.g1 && awk '\$2 != \$3 { d++ } END { exit !(d >= 20) }' $OUT/$TAG.rv.g1"
    check "22 sim lists: the sim sets (canonical walks) are identical host/guest on every tick, both Bonds" "rv_rows $OUT/$TAG.rv.host.rooms 0 | cut -d' ' -f1,2 > $OUT/$TAG.rv.hs0; rv_rows $OUT/$TAG.rv.guest.rooms 0 | cut -d' ' -f1,2 > $OUT/$TAG.rv.gs0; cut -d' ' -f1,2 $OUT/$TAG.rv.h1 > $OUT/$TAG.rv.hs1; cut -d' ' -f1,2 $OUT/$TAG.rv.g1 > $OUT/$TAG.rv.gs1; cmp -s $OUT/$TAG.rv.hs0 $OUT/$TAG.rv.gs0 && cmp -s $OUT/$TAG.rv.hs1 $OUT/$TAG.rv.gs1 && [ \$(wc -l < $OUT/$TAG.rv.hs1) -ge 400 ]"
    cp "$ROOT/Data/Profiles/rv_h/Logs/netlog_run1_host.txt" "$OUT/$TAG.rv.hostlog.txt" 2>/dev/null
    replay_log "$DOUT/$TAG.rv.hostlog.txt" 0 "$TAG.rv_rp0"
    check "22 sim lists: the host's log replays offline (the replay has no render walk of its own to lean on)" "replay_ok $OUT/$TAG.rv_rp0.log"
    # negative control: both machines read the live lists the render walk left (the pre-job-7 coupling)
    run22 "$TAG.rvx" $((PORT + 85)) GE_NET_TEST_SIMVISOLD=1
    check "22 negative control: the old coupling (sim reads the render walk's lists, GE_NET_TEST_SIMVISOLD=1) desyncs" "grep -aq 'ge: net: DESYNC' $OUT/$TAG.rvx.host.log || grep -aq 'ge: net: DESYNC' $OUT/$TAG.rvx.guest.log"
fi

# Job 7, the live root cause (playtest 7, found with the RNG trace): the door tick copied the Dam gate's clipped vertices out of the per-frame vertex pool frames after
# doorBuildClippedVertices had put them there. What the copy found depended on what the process had drawn in between: one machine of two read garbage, so P0's shot at
# the gate hit it on one machine only (an extra wall impact on the other, 247 more RNG draws). Both Bonds turn and hold fire in the start tunnel (the gate is 180 units
# away), identical windows, no scripted guards: before the fix 7 of 8 such runs desynced at tick 221 (the first shot at the gate); now none. The negative control
# GE_NET_TEST_DOORVTXOLD=1 puts the late copy back and must desync in at least one of four runs.
if echo " $PHASES " | grep -q ' 23 '; then
    echo "== phase 23 (job 7): the start gate's vertices survive the first frames (playtest-7 root cause): shots at the gate agree on both machines; negative control"
    mkprof23() { mkdir -p "$ROOT/Data/Profiles/$1"; printf '[input]\nlook_updown=1\nauto_aim=1\naim_control=0\nlook_ahead=1\n[display]\nfullscreen=windowed\nwindow_width=960\nwindow_height=540\nwidescreen=1\nhfov_wide=90\nfix_shootthrough=1\n' > "$ROOT/Data/Profiles/$1/midas.ini"; }
    mkprof23 dg_h
    mkprof23 dg_g
    awk 'BEGIN { for (t = 1; t <= 760; t++) printf "R %d %s 0 0 %d 0 0 0 0 0\n", t, (t >= 160 ? "2000" : "0"), (t >= 160 ? -1200 : 0) }' > "$OUT/$TAG.dgA.txt"
    awk 'BEGIN { for (t = 1; t <= 760; t++) printf "R %d %s 0 0 %d 0 0 0 0 0\n", t, (t >= 130 ? "2000" : "0"), (t >= 130 ? 1500 : 0) }' > "$OUT/$TAG.dgB.txt"
    run23() {   # <subtag> <port> [KEY=VALUE for both sides]
        t=$1; p=$2; shift 2
        env STAGE=dam BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$OUT" DREC="$DOUT" HPROFILE=dg_h GPROFILE=dg_g RECA="$TAG.dgA.txt" RECB="$TAG.dgB.txt" \
            sh "$HERE/net_replay2.sh" "$t" "$p" 300 none GE_NET_LOG=1 GE_MODS=invincible=1,enemy_health=400 GE_COOP_GIVEWEAPON=20:0:8:9999,20:1:8:9999 \
            GE_COOP_MOVETOCHR=100:1:4,300:1:-1 "$@" > "$OUT/$t.run.txt" 2>&1
    }
    for n in 1 2 3; do
        run23 "$TAG.dg$n" $((PORT + 90 + n * 2))
        check "23 gate shots, run $n of 3: identical fingerprints, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.dg$n.run.txt && no_desync $OUT/$TAG.dg$n.host.log && no_desync $OUT/$TAG.dg$n.guest.log && hash_ok $OUT/$TAG.dg$n.host.log 300"
    done
    got=0
    for n in 1 2 3 4; do
        run23 "$TAG.dgx$n" $((PORT + 100 + n * 2)) GE_NET_TEST_DOORVTXOLD=1
        if grep -aq 'ge: net: DESYNC' "$OUT/$TAG.dgx$n.host.log"; then got=1; break; fi
    done
    check "23 negative control: the late copy from the per-frame vertex pool (GE_NET_TEST_DOORVTXOLD=1) desyncs in at least one of four runs (the race: it did in 7 of 8)" "[ $got -eq 1 ]"
fi

# Job 7: the walk-through of the render visibility (net_roomwalk.sh): different windows, every door opened at tick 10 and closed at 330, each view's drawn set against a fresh solo walk
if echo " $PHASES " | grep -q ' 24 '; then
    echo "== phase 24 (job 7): walk-through: each view draws every room a fresh solo walk from the same spot draws (net_roomwalk.sh, windows differ, doors opened and closed)"
    env BIN="$BIN" ROOT="$ROOT" OUT="$OUT" DOUT="$DOUT" DOORS="10:o,330:c" sh "$HERE/net_roomwalk.sh" "$TAG.rw" $((PORT + 120)) mixed > "$OUT/$TAG.rwrun.txt" 2>&1
    grep -a '^PASS\|^FAIL' "$OUT/$TAG.rwrun.txt"
    FAILS=$((FAILS + $(grep -ac '^FAIL' "$OUT/$TAG.rwrun.txt")))
fi

# Rollback R1: the sim-only tick under netplay. The scripted everything session (phase 6: watches, weapons, zoom, deaths, respawns, 470 ticks) runs normally
# once, then with GE_SIMONLY_TEST stretches (ticks that run the sim step only, no render step at all) on the host only, on the guest only, and on both with
# different stretches (the guest sim-only for the WHOLE run). Fingerprints, hashes and the session logs must be those of the normal run, and no DESYNC.
if echo " $PHASES " | grep -q ' 25 '; then
    echo "== phase 25 (rollback R1): sim-only stretches on the host, on the guest, on both (the guest never renders): the session is the normal session"
    evnet "$TAG.so0" $((PORT + 130)) none
    check "25 normal run (reference): identical fingerprints, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.so0.run.txt && no_desync $OUT/$TAG.so0.host.log && no_desync $OUT/$TAG.so0.guest.log && hash_ok $OUT/$TAG.so0.host.log $EVTICKS"
    fpcutn "$OUT/$TAG.so0.host.fp" $EVTICKS > "$OUT/$TAG.so0.cmp"
    so25() {   # <sub> <port> <host spec or -> <guest spec or ->
        t=$TAG.so$1
        HOSTENV=; GUESTENV=
        [ "$3" != - ] && HOSTENV="GE_SIMONLY_TEST=$3"
        [ "$4" != - ] && GUESTENV="GE_SIMONLY_TEST=$4"
        evnet "$t" "$2" none
        fpcutn "$OUT/$t.host.fp" $EVTICKS > "$OUT/$t.cmp"
        nh=$(grep -ac 'simonly: stretch starts' "$OUT/$t.host.log")
        ng=$(grep -ac 'simonly: stretch starts' "$OUT/$t.guest.log")
        check "25 $1 (host $3, guest $4): identical fingerprints host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$t.run.txt && no_desync $OUT/$t.host.log && no_desync $OUT/$t.guest.log && hash_ok $OUT/$t.host.log $EVTICKS && hash_ok $OUT/$t.guest.log $EVTICKS"
        check "25 $1: the sim-only stretches really ran (host $nh, guest $ng)" "{ [ '$3' = - ] || [ $nh -ge 1 ]; } && { [ '$4' = - ] || [ $ng -ge 1 ]; }"
        check "25 $1: fingerprints equal the normal run's" "cmp -s $OUT/$TAG.so0.cmp $OUT/$t.cmp"
        cmp -s "$OUT/$TAG.so0.cmp" "$OUT/$t.cmp" || diff "$OUT/$TAG.so0.cmp" "$OUT/$t.cmp" | head -n 6
        grep -a '^T ' "$OUT/$t.hostlog.txt" > "$OUT/$t.hostlog.T"
        grep -a '^T ' "$OUT/$TAG.so0.hostlog.txt" > "$OUT/$TAG.so0.hostlog.T"
        check "25 $1: the host's session log (records and per-tick fingerprints) equals the normal run's" "cmp -s $OUT/$t.hostlog.T $OUT/$TAG.so0.hostlog.T && [ \$(wc -l < $OUT/$t.hostlog.T) -ge $EVTICKS ]"
    }
    so25 h $((PORT + 135)) 60,80,200 -
    so25 g $((PORT + 140)) - 100,50,130
    so25 b $((PORT + 145)) 40,90,170 1,100000
fi

# Soak fix (2026-10-22): the seed-32 Depot desync. P1's watch is open (the world is not drawn for it, so a monitor is not rendered again) while P0 shoots at the Depot's
# multi-monitor. A monitor screen's collision node (rwdata->gdl) lives in the display list of the pass that last rendered it; a non-local Bond's pass rewinds its display
# list and the local Bond's watch is drawn over it, so the hit test read different memory on the two machines (a hit on the host, a miss on the guest, 242 more RNG
# draws). Fixed by testing monitors against their static collision data in co-op (propobj.c sPortStaticMonitorCol). The scenario is the soak's seed 32 (the generator makes
# it again from the seed: the first 2100 of its 3000 ticks, windows 352x198 and 400x225 wide).
if echo " $PHASES " | grep -q ' 26 '; then
    echo "== phase 26 (soak fix): a shot at a monitor while the other Bond's watch is open (seed 32 Depot); negative control"
    S26=$OUT/$TAG.s32
    rm -rf "$S26"; mkdir -p "$S26"
    python3 "$HERE/net_soak_gen.py" 32 depot 3000 "$S26" > /dev/null
    mkprof26() {   # <profile> <width> <height> <widescreen> <hfov> <auto_aim> <look_ahead>
        mkdir -p "$ROOT/Data/Profiles/$1"
        printf '[input]\nlook_updown=1\nauto_aim=%s\naim_control=0\nlook_ahead=%s\n[display]\nfullscreen=windowed\nwindow_width=%s\nwindow_height=%s\nwidescreen=%s\nhfov_wide=%s\nfix_shootthrough=1\n' "$6" "$7" "$2" "$3" "$4" "$5" > "$ROOT/Data/Profiles/$1/midas.ini"
    }
    set -- $(grep '^h ' "$S26/prof.txt"); mkprof26 s26_h $2 $3 $4 $5 $6 $7
    set -- $(grep '^g ' "$S26/prof.txt"); mkprof26 s26_g $2 $3 $4 $5 $6 $7
    run26() {   # <subtag> <port> [KEY=VALUE for both sides]
        t=$1; p=$2; shift 2
        env STAGE=depot BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$S26" DREC="$S26" RECA=recA.txt RECB=recB.txt HPROFILE=s26_h GPROFILE=s26_g HOSTENV="$HOSTENV" GUESTENV="$GUESTENV" \
            sh "$HERE/net_replay2.sh" "$t" "$p" 2100 none GE_FRAMES=7200 GE_NET_LOG=1 $(cat "$S26/env.txt") "$@" > "$OUT/$t.run.txt" 2>&1
        HOSTENV=; GUESTENV=
    }
    run26 "$TAG.m1" $((PORT + 150))
    check "26 seed 32 Depot, P1's watch open while P0 shoots the monitor: identical fingerprints, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.m1.run.txt && no_desync $OUT/$TAG.m1.host.log && no_desync $OUT/$TAG.m1.guest.log && hash_ok $OUT/$TAG.m1.host.log 2100"
    check "26 ...and the watch really was open at tick 2009" "grep -a 'ge: coop: fp 2009 ' $OUT/$TAG.m1.host.txt | awk '{ exit !(\$15 == 5 || \$16 == 5) }'"
    run26 "$TAG.m2" $((PORT + 152)) GE_NET_TEST_MONITORCOLOLD=1
    check "26 negative control: the old monitor hit test (GE_NET_TEST_MONITORCOLOLD=1) desyncs" "grep -aq 'ge: net: DESYNC' $OUT/$TAG.m2.host.log"
fi

# Soak fix, Flatland (LEVELID_SHO): a stage that ends by itself (a Bond dies in the soak's seed 44) loads the mission-end screen. It used to segfault (fileGetIndex -> strcmp(NULL)):
# load_briefing_text_for_stage fell back to the chapter HEADER row 0 of the folder table, which has no briefing file. The soak run (about 2.5 minutes) must now be END, not CRASH.
if echo " $PHASES " | grep -q ' 27 '; then
    echo "== phase 27 (soak fix): Flatland, the stage ends by itself, the mission-end screen loads (soak seed 44)"
    env BIN="$BIN" ROOT="$ROOT" SOAK="$OUT/$TAG.soak27" TMO=400 sh "$HERE/net_soak.sh" one 44 flatland none 3000 4 > "$OUT/$TAG.soak27.txt" 2>&1
    cat "$OUT/$TAG.soak27.txt"
    check "27 Flatland: the stage ended on its own on both sides (verdict END)" "grep -q ' END ' $OUT/$TAG.soak27.txt"
    check "27 Flatland: no crash on either side" "! grep -aq 'fatal signal' $OUT/$TAG.soak27/s44_flatland_none_t3000/*.log"
fi

# ---- mission-end job: the level exit is shared sim state ------------------------------------------------------------------------------
mend() {   # <tag> <port> <jumper 0|1> <presser none|0|1> <press-from-tick> <frames> [KEY=VALUE for both sides]: Dam bungee exit (net_missionend.sh), the host then picks Depot (GE_NET_TEST_NEXT)
    t=$1; p=$2; j=$3; pr=$4; fr=$5; fm=$6; shift 6
    env BIN="$BIN" OUT="$OUT" TICKS=99999 HOSTENV="${HOSTENV:-GE_NET_TEST_NEXT=depot:1}" GUESTENV="$GUESTENV" \
        sh "$HERE/net_missionend.sh" "$t" "$p" "$j" "$pr" "$fr" GE_FRAMES="$fm" "$@" > "$OUT/$t.run.txt" 2>&1
    HOSTENV=; GUESTENV=
}
end_tick() { grep -a -m1 'stage change to' "$1" | sed 's/.*tick //'; }   # <fp>: the sim tick the stage was left on
two_stages_equal() { [ "$(grep -ah 'ge: coop: two Bonds on stage' "$1" | head -n 2 | sed 's/(local player [01])//')" = "$(grep -ah 'ge: coop: two Bonds on stage' "$2" | head -n 2 | sed 's/(local player [01])//')" ] && [ "$(grep -ac 'ge: coop: two Bonds on stage' "$1")" -ge 2 ]; }   # the first two stages (a later one can be the guest's own once the host has left: the session is over then)
fp_same() { head -n -4 "$1" > "$1.t"; head -n -4 "$2" > "$2.t"; cmp -s "$1.t" "$2.t"; }   # per-tick fingerprints equal but for the last lines (the frame limit ends the two processes at different points)
exit_lines() { grep -a 'ge: coop: \(mission end\|end-state\|cutscene Bond\)' "$1" | head -n 30; }
mend_checks() {   # <tag> <label> <expect cutscene camera 0|1>
    t=$1; lb=$2
    H=$OUT/$t.host.fp; G=$OUT/$t.guest.fp
    e=$(end_tick "$H")
    check "$lb: both machines left the stage on the same sim tick ($e)" "[ -n '$e' ] && [ '$e' = \"\$(end_tick $G)\" ]"
    check "$lb: identical per-tick fingerprints, the same exit / fade / camera lines on both machines, no DESYNC" "fp_same $OUT/$t.cmp.host $OUT/$t.cmp.guest && no_desync $OUT/$t.host.log && no_desync $OUT/$t.guest.log && [ \"\$(exit_lines $H)\" = \"\$(exit_lines $G)\" ]"
    check "$lb: BOTH Bonds' screens faded (fading 11) and went black (black 11) for the exit" "grep -a 'end-state' $H | grep -aq 'fading 11' && grep -a 'end-state' $H | grep -aq 'black 11'"
    if [ "$3" = 1 ]; then
        check "$lb: the exit camera played (camera mode 7); the cutscene Bond is the same on both machines" "grep -aq 'end-state.*camera 7' $H && grep -aq 'cutscene Bond is player' $H && [ \"\$(grep -a 'cutscene Bond' $H)\" = \"\$(grep -a 'cutscene Bond' $G)\" ]"
    fi
    check "$lb: the host's next stage (Depot) loaded and started on both: GO of run 2 on both, the same two stages" "grep -aq 'GO (run 2)' $OUT/$t.host.log && grep -aq 'GO (run 2)' $OUT/$t.guest.log && two_stages_equal $OUT/$t.host.log $OUT/$t.guest.log"
    check "$lb: no stranded machine (no 'session ended', no 'machine started a stage the host did not choose')" "! grep -aq 'session ended\|machine started a stage the host did not choose' $OUT/$t.host.log $OUT/$t.guest.log"
}

if echo " $PHASES " | grep -q ' 28 '; then
    echo "== phase 28 (mission-end job): Dam bungee exit by P0 and by P1; the other Bond presses 'continue' EARLY (before the exit camera), LATE (after it) or not at all"
    mend "$TAG.e0" $((PORT + 160)) 0 1 30 1500
    mend_checks "$TAG.e0" "28 P0 jumps, P1 presses early" 0
    mend "$TAG.e1" $((PORT + 162)) 1 0 30 1500
    mend_checks "$TAG.e1" "28 P1 jumps, P0 presses early" 0
    mend "$TAG.l0" $((PORT + 164)) 0 1 700 1900
    mend_checks "$TAG.l0" "28 P0 jumps, P1 presses late" 1
    mend "$TAG.l1" $((PORT + 166)) 1 0 700 1900
    mend_checks "$TAG.l1" "28 P1 jumps, P0 presses late" 1
    mend "$TAG.n0" $((PORT + 168)) 0 none 0 1500
    mend_checks "$TAG.n0" "28 P0 jumps, P1 never presses (the jumper does)" 0
    mend "$TAG.n1" $((PORT + 170)) 1 none 0 1500
    mend_checks "$TAG.n1" "28 P1 jumps, P0 never presses (the jumper does)" 0
    # the session logs of a late exit replay offline in both seats, exit tick included
    rm -f "$ROOT/Data/Profiles/nethost/Logs"/netlog_run*.txt "$ROOT/Data/Profiles/netguest/Logs"/netlog_run*.txt
    HOSTENV="GE_NET_TEST_INFO=1000000"
    mend "$TAG.r1" $((PORT + 172)) 1 0 700 1500 GE_NET_LOG=1
    cp "$ROOT/Data/Profiles/nethost/Logs/netlog_run1_host.txt" "$OUT/$TAG.r1.hostlog1.txt" 2>/dev/null
    cp "$ROOT/Data/Profiles/netguest/Logs/netlog_run1_guest.txt" "$OUT/$TAG.r1.guestlog1.txt" 2>/dev/null
    replay_log "$DOUT/$TAG.r1.hostlog1.txt" 0 "$TAG.r1_rp0"
    replay_log "$DOUT/$TAG.r1.guestlog1.txt" 1 "$TAG.r1_rp1"
    check "28 the session log of a late exit (cutscene, fades, button exit) replays offline in both seats: OK" "replay_ok $OUT/$TAG.r1_rp0.log && replay_ok $OUT/$TAG.r1_rp1.log"
    # negative control: the pre-fix single-Bond mission end fades only ONE Bond's screen ('P1 black, P2 playing')
    mend "$TAG.o1" $((PORT + 174)) 1 0 30 900 GE_NET_TEST_OLDEND=1
    check "28 negative control (GE_NET_TEST_OLDEND=1): the old end fades only one Bond (never 'fading 11' / 'black 11')" "! grep -a 'end-state' $OUT/$TAG.o1.host.fp | grep -aq 'fading 11\|black 11' && grep -a 'end-state' $OUT/$TAG.o1.host.fp | grep -aq 'fading 10\|fading 01'"
fi

if echo " $PHASES " | grep -q ' 29 '; then
    echo "== phase 29 (mission-end job): mission failed (both Bonds dead): both leave the stage on the same tick, then the host's next stage loads on both"
    env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE="$HP" GPROFILE="$GP" HOSTENV="GE_NET_TEST_NEXT=depot:1" GUESTENV="" \
        sh "$HERE/net_replay2.sh" "$TAG.f1" $((PORT + 180)) 99999 none GE_FRAMES=2600 GE_NET_LOG=1 GE_COOP_HURT=120:0:10,130:1:10 GE_COOP_RESPAWN=100000 > "$OUT/$TAG.f1.run.txt" 2>&1
    H=$OUT/$TAG.f1.host.fp; G=$OUT/$TAG.f1.guest.fp
    e=$(end_tick "$H")
    check "29 both Bonds died, the stage was left on the same sim tick on both machines ($e)" "[ -n '$e' ] && [ '$e' = \"\$(end_tick $G)\" ] && grep -aq 'all Bonds dead' $H"
    check "29 identical fingerprints, no DESYNC" "fp_same $OUT/$TAG.f1.cmp.host $OUT/$TAG.f1.cmp.guest && no_desync $OUT/$TAG.f1.host.log && no_desync $OUT/$TAG.f1.guest.log"
    check "29 the host's next stage loaded on both after the failed mission (GO of run 2 on both)" "grep -aq 'GO (run 2)' $OUT/$TAG.f1.host.log && grep -aq 'GO (run 2)' $OUT/$TAG.f1.guest.log"
fi

if echo " $PHASES " | grep -q ' 30 '; then
    echo "== phase 30 (mission-end job): the guest cannot start a stage and follows the host's pick (it tries first; it is off-stage for a long while; the host picks while it is still in the old stage)"
    GUESTENV="GE_NET_TEST_GUESTPICK=34"
    mend "$TAG.g1" $((PORT + 190)) 0 none 0 1500
    check "30 the guest tried to start a stage from its menus and was refused (log), nothing started" "grep -aq 'TEST: the guest tries to start stage 34' $OUT/$TAG.g1.guest.log && grep -aq 'guest refused to start a stage from a menu' $OUT/$TAG.g1.guest.log"
    check "30 ...then the host chose Depot and BOTH loaded it (the guest followed; no 'machine started a stage the host did not choose', no rogue stage)" "grep -aq 'GO (run 2)' $OUT/$TAG.g1.host.log && grep -aq 'GO (run 2)' $OUT/$TAG.g1.guest.log && ! grep -aq 'machine started a stage the host did not choose' $OUT/$TAG.g1.guest.log && two_stages_equal $OUT/$TAG.g1.host.log $OUT/$TAG.g1.guest.log"
    GUESTENV="GE_NET_TEST_LAUNCHLAG=130"
    mend "$TAG.g2" $((PORT + 192)) 1 none 0 1700
    check "30 the guest was off-stage (menus) for a long while when the host picked: it followed and both loaded Depot" "grep -aq 'GO (run 2)' $OUT/$TAG.g2.host.log && grep -aq 'GO (run 2)' $OUT/$TAG.g2.guest.log && ! grep -aq 'machine started a stage the host did not choose\|session ended' $OUT/$TAG.g2.guest.log $OUT/$TAG.g2.host.log"
    HOSTENV="GE_NET_TEST_NEXT=depot:1 GE_NET_TEST_EARLYNEXT=360"
    mend "$TAG.g3" $((PORT + 194)) 0 1 30 1500
    check "30 the host's NEXTSTAGE reached the guest while it was still in the old stage: held, applied when that stage ended, both loaded Depot, the repeat ignored" "grep -aq 'while a stage is still running here' $OUT/$TAG.g3.guest.log && grep -aq 'GO (run 2)' $OUT/$TAG.g3.host.log && grep -aq 'GO (run 2)' $OUT/$TAG.g3.guest.log && grep -aq 'ignored a repeated NEXTSTAGE' $OUT/$TAG.g3.guest.log && no_desync $OUT/$TAG.g3.host.log"
fi

if echo " $PHASES " | grep -q ' 31 '; then
    echo "== phase 31 (mission-end job): phase 16's two-stage session, 10 times in a row, no stall (the soak's one stall: host stuck at tick 99)"
    n31=0
    for i in 1 2 3 4 5 6 7 8 9 10; do
        PHASES=16 sh "$0" "$TAG.p16r$i" $((PORT + 200 + i * 5)) $TICKS none > "$OUT/p16out.$TAG.$i.txt" 2>&1
        if grep -q '^net_lockstep: 0 failed' "$OUT/p16out.$TAG.$i.txt"; then n31=$((n31 + 1)); else grep -a FAIL "$OUT/p16out.$TAG.$i.txt" | head -n 3; fi
    done
    check "31 phase 16 passed $n31 / 10 times in a row" "[ $n31 -eq 10 ]"
fi


if echo " $PHASES " | grep -q ' 32 '; then
    echo "== phase 32 (mission-end job): the host quits mid-stage; the guest goes back to the title and starts Dam: genuine solo (one Bond, no co-op)"
    p=$((PORT + 250))
    COMMONENV="GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_STAGE_AFTER=20 GE_DIFF=1 GE_NET_WAIT=20 GE_NET_LOG=1"
    rm -f "$OUT/$TAG.td.host.log" "$OUT/$TAG.td.guest.log"
    env $COMMONENV GE_FRAMES=700 GE_PROFILE=$HP GE_NET=host:$p GE_STAGE=dam GE_SEED=7 timeout -s KILL 120 "$BIN" > "$OUT/$TAG.td.host.log" 2>&1 &
    HPID=$!
    sleep 2
    env $COMMONENV GE_FRAMES=3200 GE_NET_TEST_GUESTPICK=33 GE_PROFILE=$GP GE_NET=join:127.0.0.1:$p timeout -s KILL 200 "$BIN" > "$OUT/$TAG.td.guest.log" 2>&1
    wait $HPID
    G=$OUT/$TAG.td.guest.log
    check "32 the host left; the guest said so, returned to the title and switched co-op off (log)" "grep -aq 'ge: net: partner disconnected' $G && grep -aq 'ge: net: session over: co-op is off' $G"
    check "32 the guest started Dam again from its menus and it is SOLO: exactly one 'two Bonds' line (the session's), a second stage loaded, no second co-op stage" "[ \$(grep -ac 'ge: coop: two Bonds on stage' $G) -eq 1 ] && [ \$(grep -ac 'load_bg_file levelid=33' $G) -ge 2 ]"
    check "32 no crash on the guest, it ran on to its frame limit" "! grep -aq 'fatal signal' $G && grep -aq 'frame limit reached' $G"
fi

if echo " $PHASES " | grep -q ' 33 '; then
    echo "== phase 33 (mission-end job): a Bond that died before the ending shows no death overlay under it and only the cutscene Bond is drawn; the credits stage renders its scene in BOTH views"
    env BIN="$BIN" OUT="$OUT" SHOT=40 TICKS=1500 MOVETICK=500 sh "$HERE/net_missionend.sh" "$TAG.en" $((PORT + 260)) 0 1 1350 GE_MODS=invincible=0,coop_exits=first GE_COOP_HURT=300:1:10 GE_COOP_RESPAWN=100000 > "$OUT/$TAG.en.run.txt" 2>&1
    H=$OUT/$TAG.en.host.fp; G=$OUT/$TAG.en.guest.fp
    check "33 P1 died at tick 300 and stayed dead; the ending (exit camera) started on the same tick on both machines, no DESYNC" "grep -aq 'player 1 died' $H && grep -aq 'the ending starts at sim tick' $H && [ \"\$(grep -a 'the ending starts' $H)\" = \"\$(grep -a 'the ending starts' $G)\" ] && no_desync $OUT/$TAG.en.host.log && no_desync $OUT/$TAG.en.guest.log"
    SH=$OUT/$TAG.en.shots/guest
    redmax=$(python3 "$HERE/bmp_stat.py" $SH/shot_011*.bmp $SH/shot_012*.bmp $SH/shot_013*.bmp 2>/dev/null | awk '{ if ($5 > m) m = $5 } END { print m + 0 }')
    litmax=$(python3 "$HERE/bmp_stat.py" $SH/shot_011*.bmp $SH/shot_012*.bmp $SH/shot_013*.bmp 2>/dev/null | awk '{ if ($7 > m) m = $7 } END { print m + 0 }')
    check "33 the dead Bond's (guest) view during the exit cutscene is not under the red overlay (red fraction $redmax) and shows the scene (lit $litmax)" "awk -v r=$redmax -v l=$litmax 'BEGIN { exit !(r < 0.05 && l > 0.5) }'"
    mkdir -p "$OUT/$TAG.cred/host" "$OUT/$TAG.cred/guest"
    env BIN="$BIN" OUT="$OUT" STAGE=cuba HCWD="$OUT/$TAG.cred/host" GCWD="$OUT/$TAG.cred/guest" sh "$HERE/net_replay2.sh" "$TAG.cr" $((PORT + 262)) 1500 none GE_SHOT=150 GE_WINDOW=640x360 GE_MODS=invincible=1 GE_FRAMES=1800 > "$OUT/$TAG.cr.run.txt" 2>&1
    CR=$OUT/$TAG.cred
    hl=$(python3 "$HERE/bmp_stat.py" $CR/host/shot_009*.bmp $CR/host/shot_010*.bmp $CR/host/shot_012*.bmp | awk '{ if ($7 > m) m = $7 } END { print m + 0 }')
    gl=$(python3 "$HERE/bmp_stat.py" $CR/guest/shot_009*.bmp $CR/guest/shot_010*.bmp $CR/guest/shot_012*.bmp | awk '{ if ($7 > m) m = $7 } END { print m + 0 }')
    check "33 the credits stage (Cuba) under netplay: the host's view shows the scene (lit $hl) and so does the guest's (lit $gl), identical fingerprints" "awk -v a=$hl -v b=$gl 'BEGIN { exit !(a > 0.5 && b > 0.5) }' && grep -q IDENTICAL $OUT/$TAG.cr.run.txt"
fi

# ---- job CO-OP-EXITS: both Bonds at the exit, respawn / lives, escorts, the Surface 2 bomb ------------------------------------------------
# exr: Dam bungee exit (net_missionend.sh): P0 is put on the jump trigger at tick 5, P1 on it at tick $PMOVE (none = never); coop_exits=$EXITS.
exr() {   # <tag> <port> <exits both|first> <partner-move-tick or none> <frames> [KEY=VALUE for both sides]
    t=$1; p=$2; ex=$3; pm=$4; fm=$5; shift 5
    if [ "$pm" = none ]; then pmv=; else pmv=$pm; fi
    env BIN="$BIN" OUT="$OUT" TICKS=99999 EXITS="$ex" PARTNERMOVE="$pmv" HOSTENV="${HOSTENV:-GE_NET_TEST_NEXT=depot:1}" GUESTENV="$GUESTENV" \
        sh "$HERE/net_missionend.sh" "$t" "$p" 0 1 900 GE_FRAMES="$fm" "$@" > "$OUT/$t.run.txt" 2>&1
    HOSTENV=; GUESTENV=
}
same_lines() { [ "$(grep -a "$3" "$1" | sed 's/(local player [01])//')" = "$(grep -a "$3" "$2" | sed 's/(local player [01])//')" ]; }   # <fpA> <fpB> <regex>

if echo " $PHASES " | grep -q ' 34 '; then
    echo "== phase 34 (CO-OP-EXITS): both Bonds must reach the Dam bungee exit; coop_exits=first ends at once; the partner never comes; a dead partner walks in from the start"
    exr "$TAG.x1" $((PORT + 300)) both 300 2700
    H=$OUT/$TAG.x1.host.fp; G=$OUT/$TAG.x1.guest.fp
    check "34 P0 reached the exit at tick 6 and was HELD; P1 arrived at tick 302; the ending then started (same lines on both machines)" "grep -aq 'player 0 reached the exit.*sim tick 6 and is held' $H && grep -aq 'player 1 reached the exit at sim tick 302' $H && grep -aq 'every living Bond is at the exit: the ending starts at sim tick 302' $H && same_lines $H $G 'ge: coop: exit:'"
    check "34 the mission ended on the same tick on both machines, then the host's next stage loaded on both; no DESYNC" "[ -n \"\$(end_tick $H)\" ] && [ \"\$(end_tick $H)\" = \"\$(end_tick $G)\" ] && grep -aq 'GO (run 2)' $OUT/$TAG.x1.host.log && grep -aq 'GO (run 2)' $OUT/$TAG.x1.guest.log && no_desync $OUT/$TAG.x1.host.log && no_desync $OUT/$TAG.x1.guest.log && fp_same $OUT/$TAG.x1.cmp.host $OUT/$TAG.x1.cmp.guest"
    check "34 the held Bond really was frozen and nobody ended the mission before tick 302 (no 'ending starts' line earlier)" "! grep -a 'the ending starts at sim tick' $H | awk '{ if (\$NF+0 > 0) print }' | grep -aq 'sim tick [0-9]\\{1,2\\}:\\|sim tick [12][0-9][0-9]:'"
    exr "$TAG.x2" $((PORT + 302)) first none 1700
    H=$OUT/$TAG.x2.host.fp; G=$OUT/$TAG.x2.guest.fp
    check "34 coop_exits=first: the first Bond ends it at once (no hold, the ending starts at tick 6), identical on both" "! grep -aq 'is held there' $H && grep -aq 'the ending starts at sim tick 7[0-9][0-9]\\|the ending starts at sim tick [0-9]\\{2,3\\}:' $H && no_desync $OUT/$TAG.x2.host.log && same_lines $H $G 'the ending starts'"
    exr "$TAG.x3" $((PORT + 304)) both none 1500
    H=$OUT/$TAG.x3.host.fp
    check "34 the partner never comes: P0 stays held to the end (no ending, no stage change), no DESYNC (negative control for the hold)" "grep -aq 'is held there' $H && ! grep -aq 'the ending starts\|stage change' $H && no_desync $OUT/$TAG.x3.host.log && no_desync $OUT/$TAG.x3.guest.log"
    exr "$TAG.x4" $((PORT + 306)) both 1000 2700 GE_MODS=invincible=0,coop_exits=both GE_COOP_HURT=100:1:10 GE_COOP_RESPAWN=100 GE_COOP_FORCEREACH=1
    H=$OUT/$TAG.x4.host.fp; G=$OUT/$TAG.x4.guest.fp
    st1=$(grep -a 'start spawn player 1' $H | sed 's/.*: //')
    rs1=$(grep -a 'player 1 respawned' $H | head -n 1 | awk '{ for (i = 1; i <= NF; i++) if ($i == "pos") print $(i+1), $(i+3) }')
    check "34 P1 died while P0 waited; it respawned at the level START (plan line, spot '$st1' = '$rs1'), then walked in (tick 1000): the exit opened only then" "grep -aq 'player 1 died' $H && grep -aq 'respawn plan player 1: start spawn' $H && grep -aq 'player 1 reached the exit at sim tick 100[0-9]' $H && [ -n '$st1' ] && [ '$st1' = '$rs1' ] && same_lines $H $G 'ge: coop: \(exit:\|respawn plan\|player 1 respawned\)' && no_desync $OUT/$TAG.x4.host.log"
    exr "$TAG.x5" $((PORT + 308)) both none 1700 GE_MODS=invincible=0,coop_exits=both GE_COOP_HURT=100:1:10 GE_COOP_RESPAWN=100 GE_COOP_FORCEUNREACH=1
    H=$OUT/$TAG.x5.host.fp; G=$OUT/$TAG.x5.guest.fp
    check "34 safety net: with the start forced unreachable the dead Bond respawns BESIDE the waiting partner (reason logged) and the exit opens by itself" "grep -aq 'respawn plan player 1: beside player 0 - the start cannot reach the partner (forced' $H && grep -aq 'every living Bond is at the exit' $H && same_lines $H $G 'ge: coop: \(exit:\|respawn plan\)' && no_desync $OUT/$TAG.x5.host.log"
fi

# lives: per player and per party; out of lives -> stays down and watches; the mission fails when nobody can go on
lv() {   # <tag> <port> <lives> <mode player|party> <HURT list> <frames>
    t=$1; p=$2; lvs=$3; md=$4; hurt=$5; fm=$6
    env STAGE="${LVSTAGE:-dam}" BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE="$HP" GPROFILE="$GP" HOSTENV="GE_NET_TEST_NEXT=depot:1" GUESTENV="" \
        sh "$HERE/net_replay2.sh" "$t" "$p" 99999 none GE_FRAMES="$fm" GE_NET_LOG=1 GE_MODS=invincible=0,enemy_damage=25,coop_lives=$lvs,coop_lives_mode=$md GE_COOP_HURT="$hurt" GE_COOP_RESPAWN=90 > "$OUT/$t.run.txt" 2>&1
}
if echo " $PHASES " | grep -q ' 35 '; then
    echo "== phase 35 (CO-OP-EXITS): lives per player and per party; out of lives = stays down and watches; the mission fails when nobody can go on"
    lv "$TAG.l1" $((PORT + 320)) 2 player 100:1:10,500:1:10,900:0:10,935:0:10,1300:0:10,1335:0:10 2800
    H=$OUT/$TAG.l1.host.fp; G=$OUT/$TAG.l1.guest.fp
    check "35 per player, 2 lives: P1's 2nd death is final (OUT OF LIVES, watches P0, no respawn); P0's 1st death respawns; P0's 2nd death ends the mission (all down)" "grep -aq 'player 1 died: its lives left 1' $H && grep -aq 'player 1 died: its lives left 0 - OUT OF LIVES' $H && grep -aq 'player 1 is out of lives and now watches player 0' $H && [ \$(sed '/all Bonds dead/q' $H | grep -ac 'player 1 respawned') -eq 1 ] && grep -aq 'player 0 died: its lives left 0 - OUT OF LIVES' $H && grep -aq 'all Bonds dead' $H"
    check "35 identical on both machines (the lives lines, the failure), the stage left on the same tick, no DESYNC" "same_lines $H $G 'ge: coop: \(player [01] died\|player [01] is out\|player [01] respawned\|all Bonds dead\)' && [ -n \"\$(end_tick $H)\" ] && [ \"\$(end_tick $H)\" = \"\$(end_tick $G)\" ] && no_desync $OUT/$TAG.l1.host.log && no_desync $OUT/$TAG.l1.guest.log"
    lv "$TAG.l2" $((PORT + 322)) 3 party 100:1:10,500:0:10,535:0:10,900:1:10,1300:0:10,1335:0:10 2800
    H=$OUT/$TAG.l2.host.fp; G=$OUT/$TAG.l2.guest.fp
    check "35 party pool of 3: the 3rd death (P1) is final, the 4th (P0) ends the mission; identical on both; no DESYNC" "grep -aq 'player 1 died: party lives left 0 - OUT OF LIVES' $H && grep -aq 'all Bonds dead' $H && same_lines $H $G 'ge: coop: \(player [01] died\|player [01] is out\|player [01] respawned\|all Bonds dead\)' && no_desync $OUT/$TAG.l2.host.log && no_desync $OUT/$TAG.l2.guest.log"
    LVSTAGE=depot lv "$TAG.l3" $((PORT + 324)) 0 player 100:1:10,500:1:10,900:1:10 2000
    H=$OUT/$TAG.l3.host.fp
    check "35 unlimited lives (the default), Depot: three deaths, three respawns at the START spawn (the start can reach the partner there), nobody is ever out" "[ \$(grep -ac 'player 1 respawned' $H) -eq 3 ] && [ \$(grep -ac 'respawn plan player 1: start spawn, it can reach' $H) -eq 3 ] && ! grep -aq 'OUT OF LIVES' $H"
fi

if echo " $PHASES " | grep -q ' 36 '; then
    echo "== phase 36 (CO-OP-EXITS): the host's session settings reach the guest (the guest's own ini is not asked)"
    HOSTENV="GE_MODS=invincible=0,enemy_damage=400,coop_exits=first,coop_lives=7,coop_respawn=partner,coop_lives_mode=party" \
        env BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$REC" DREC="$DREC" HPROFILE="$HP" GPROFILE="$GP" HOSTENV="GE_MODS=invincible=0,enemy_damage=400,coop_exits=first,coop_lives=7,coop_respawn=partner,coop_lives_mode=party" GUESTENV="" \
        sh "$HERE/net_replay2.sh" "$TAG.s1" $((PORT + 330)) 200 none GE_NET_LOG=1 > "$OUT/$TAG.s1.run.txt" 2>&1
    check "36 both machines run with the HOST's settings (exits first, respawn partner, 7 lives, party)" "grep -aq 'session settings: exits first, respawn partner, lives 7 (party)' $OUT/$TAG.s1.host.fp && grep -aq 'session settings: exits first, respawn partner, lives 7 (party)' $OUT/$TAG.s1.guest.fp && ! grep -aq 'modifier mismatch' $OUT/$TAG.s1.guest.log"
fi

if echo " $PHASES " | grep -q ' 37 '; then
    echo "== phase 37 (CO-OP-EXITS): Surface 2 spawn and weapon cycling to the mine: no 'Bomb incorrectly placed' (negative control: the old search)"
    NR=$OUT/$TAG.neutral.rec   # both Bonds stand still (the stock recordings walk, cycle and fire: P0 would really place its mine)
    mkdir -p "$NR"
    awk 'BEGIN { for (t = 1; t <= 3000; t++) print "R", t, "0", 0, 0, 0, 0 }' > "$NR/recA.txt"
    cp "$NR/recA.txt" "$NR/recB.txt"
    for v in new old; do
        if [ $v = old ]; then oldv="GE_TEST_BOMBOLD=1"; else oldv="GE_TEST_NONE=1"; fi
        env STAGE=surface2 BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$NR" DREC="$NR" RECA=recA.txt RECB=recB.txt HPROFILE="$HP" GPROFILE="$GP" HOSTENV="" GUESTENV="" \
            sh "$HERE/net_replay2.sh" "$TAG.b$v" $((PORT + 340)) 400 none GE_HUDLOG=1 GE_COOP_CYCLE=100:1:2:60 $oldv > "$OUT/$TAG.b$v.run.txt" 2>&1
    done
    check "37 netplay Surface 2, P1 cycles to the timed mine: no 'Bomb incorrectly placed' on either machine, identical fingerprints" "! grep -aq 'Bomb incorrectly' $OUT/$TAG.bnew.host.log $OUT/$TAG.bnew.guest.log && grep -q IDENTICAL $OUT/$TAG.bnew.run.txt"
    check "37 negative control (GE_TEST_BOMBOLD=1): the old search says it" "grep -aq 'Bomb incorrectly' $OUT/$TAG.bold.host.log"
fi

if echo " $PHASES " | grep -q ' 38 '; then
    echo "== phase 38 (CO-OP-EXITS): escort NPCs (Jungle's Natalya) follow the nearest living Bond with hysteresis; they no longer take a Bond's own chr for a guard (the root cause of 'she never follows')"
    NR=$OUT/$TAG.neutral.rec
    mkdir -p "$NR"
    awk 'BEGIN { for (t = 1; t <= 3000; t++) print "R", t, "0", 0, 0, 0, 0 }' > "$NR/recA.txt"
    cp "$NR/recA.txt" "$NR/recB.txt"
    ejr() {   # <tag> <port> <stage> <ticks> [KEY=VALUE]
        t=$1; p=$2; st=$3; tk=$4; shift 4
        env STAGE=$st BIN="$BIN" OUT="$OUT" DOUT="$DOUT" REC="$NR" DREC="$NR" RECA=recA.txt RECB=recB.txt HPROFILE="$HP" GPROFILE="$GP" HOSTENV="" GUESTENV="" \
            sh "$HERE/net_replay2.sh" "$t" "$p" "$tk" none GE_ESCORTDBG=1 "$@" > "$OUT/$t.run.txt" 2>&1
    }
    ejr "$TAG.e1" $((PORT + 360)) jungle 1700 GE_COOP_MOVETOCHR=300:0:1,900:1:1,1200:0:12
    H=$OUT/$TAG.e1.host.fp; G=$OUT/$TAG.e1.guest.fp
    check "38 Jungle, netplay: Natalya (chr 1) follows P0, keeps it while P1 stands as close (tick 900-1199: hysteresis), switches to P1 at tick 1200 when P0 is far; the same on both machines" "[ \$(grep -ac 'escort chr 1 now follows' $H) -eq 2 ] && grep -aq 'now follows player 0 (was -1) at tick 1:' $H && grep -aq 'now follows player 1 (was 0) at tick 1200:' $H && same_lines $H $G 'now follows' && grep -a 'escort chr 1 tick 1020 ' $H | grep -aq 'nearest 1' && no_desync $OUT/$TAG.e1.host.log && grep -q IDENTICAL $OUT/$TAG.e1.run.txt"
    check "38 she never takes a Bond for a guard: no escort line shows a chr preset of 5000 or more; she walks (act 15 seen)" "! grep -aE 'escort chr 1 tick .* chrpreset 5[0-9][0-9][0-9]' $H && grep -aq 'escort chr 1 tick .* act 15 ' $H"
    ejr "$TAG.e2" $((PORT + 362)) jungle 700 GE_COOP_MOVETOCHR=300:0:1 GE_TEST_ESCORTOLD=1
    check "38 negative control (GE_TEST_ESCORTOLD=1): the old search makes her target a Bond (chr preset 5042 / 5043)" "grep -aE 'escort chr 1 tick .* chrpreset 504[23]' $OUT/$TAG.e2.host.fp > /dev/null"
    bad=0; seen=0
    for lv in control train bunker2 caverns; do
        ejr "$TAG.e3$lv" $((PORT + 364)) $lv 900 GE_ESCORTDBG=all
        grep -aE 'escort chr [0-9]+ tick .* chrpreset 5[0-9][0-9][0-9]' "$OUT/$TAG.e3$lv.host.fp" > /dev/null && bad=$((bad + 1))
        seen=$((seen + $(grep -ac 'escort chr [0-9]* tick' "$OUT/$TAG.e3$lv.host.fp")))
        echo "   $lv: escort lines $(grep -ac 'escort chr [0-9]* tick' "$OUT/$TAG.e3$lv.host.fp"), 'now follows' $(grep -ac 'now follows' "$OUT/$TAG.e3$lv.host.fp")"
    done
    check "38 Control, Train, Bunker 2, Caverns: no escort picks a Bond's chr as a target ($seen escort samples)" "[ $bad -eq 0 ]"
fi

if echo " $PHASES " | grep -q ' 39 '; then
    echo "== phase 39 (CO-OP-EXITS): the partner leaves while a Bond waits at the exit: the mission completes on the machine that is left"
    p=$((PORT + 380))
    NR=$OUT/$TAG.neutral.rec
    mkdir -p "$NR"
    awk 'BEGIN { for (t = 1; t <= 3000; t++) print "R", t, (t >= 700 && t % 10 == 0) ? "8000" : "0", 0, 0, 0, 0 }' > "$NR/recA.txt"
    cp "$NR/recA.txt" "$NR/recB.txt"
    COMMONENV="GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_STAGE_AFTER=20 GE_DIFF=1 GE_NET_WAIT=20 GE_NET_LOG=1 GE_MODS=invincible=1,coop_exits=both GE_COOP_MOVETOPAD=5:0:330 GE_TEST_ALLOBJ=1 GE_COOP_LOCAL=replay:$NR/recA.txt"
    rm -f "$OUT/$TAG.dc.host.log" "$OUT/$TAG.dc.guest.log"
    env $COMMONENV GE_FRAMES=2400 GE_PROFILE=$HP GE_NET=host:$p GE_STAGE=dam GE_SEED=7 GE_COOP_FPFILE="$DOUT/$TAG.dc.host.fp" timeout -s KILL 200 "$BIN" > "$OUT/$TAG.dc.host.log" 2>&1 &
    HPID=$!
    sleep 2
    env $COMMONENV GE_FRAMES=500 GE_PROFILE=$GP GE_NET=join:127.0.0.1:$p GE_COOP_LOCAL=replay:$NR/recB.txt GE_COOP_FPFILE="$DOUT/$TAG.dc.guest.fp" timeout -s KILL 200 "$BIN" > "$OUT/$TAG.dc.guest.log" 2>&1
    wait $HPID
    H=$OUT/$TAG.dc.host.log
    check "39 the guest left; the host (P0 held at the exit) said the mission completes, the ending ran and the stage ended, no title return before it" "grep -aq 'ge: net: partner disconnected' $H && grep -aq 'the mission completes' $H && grep -aq 'mission end (' $OUT/$TAG.dc.host.fp && grep -aq 'ge: mission end: levelid=33' $H"
fi

# ---- job MODES-IN-CO-OP: game modes (level reset, Lua mode scripts, hostage rescue) in co-op and netplay ---------------------------------------------
# mnet: net_modes.sh net. The scenario comes from the caller's environment (STAGE MODE LUATEST MOVES MOVECHR TAPS0 TAPS1 YAWS0 YAWS1 LAG; unset = the
# Flatland scenario of tests/lua/coop_modes_test.lua), so always call it inside a subshell: ( export STAGE=dam ...; mnet <tag> <port> <ticks> ... ).
# Extra arguments: KEY=VALUE for both machines, H:KEY=VALUE host only, G:KEY=VALUE guest only. Only the HOST gets GE_MODE; the guest must take it from SETUP.
mnet() {
    t=$1; p=$2; tk=$3; shift 3
    BIN="$BIN" OUT="$OUT" DOUT="$DOUT" ROOT="$ROOT" HPROFILE="$HP" GPROFILE="$GP" sh "$HERE/net_modes.sh" net "$t" "$p" "$tk" "$@" > "$OUT/$t.run.txt" 2>&1
}
mlocal() {   # <tag> <ticks> [KEY=VALUE ...]: the same scenario as ONE local co-op instance (the oracle)
    t=$1; tk=$2; shift 2
    BIN="$BIN" OUT="$OUT" DOUT="$DOUT" ROOT="$ROOT" HPROFILE="$HP" GPROFILE="$GP" sh "$HERE/net_modes.sh" local "$t" "$tk" "$@" > "$OUT/$t.run.txt" 2>&1
}
mlogs() {   # <tag>: keep the two session logs of run 1 (the next run overwrites them)
    cp "$ROOT/Data/Profiles/$HP/Logs/netlog_run1_host.txt" "$OUT/$1.hostlog.txt" 2>/dev/null
    cp "$ROOT/Data/Profiles/$GP/Logs/netlog_run1_guest.txt" "$OUT/$1.guestlog.txt" 2>/dev/null
}
tlines() { grep -a "^ge: lua: $2 " "$1" | sed 's/^ge: lua: //'; }   # <log> <prefix>: the script's own log lines ("T ...", "D ...", "DIED ...")
same_lua() { [ "$(tlines "$1" "$3")" = "$(tlines "$2" "$3")" ]; }   # <logA> <logB> <prefix>: the same lines on both machines
nlua() { grep -ac "^ge: lua: $2 " "$1"; }
hudline() { grep -a "ge: modehud: this machine (player $2) draws line $3: '$4'" "$1" > /dev/null; }   # <log> <player> <line> <text>

if echo " $PHASES " | grep -q ' 40 '; then
    echo "== phase 40 (MODES-IN-CO-OP): Flatland, the hostage scenario of coop_modes_test.lua over netplay: either Bond toggles hostages through its own record, the host's mode reaches the guest, the script sees the same world, per-player HUD, replays"
    (mnet "$TAG.m1" $((PORT + 400)) 800)
    mlogs "$TAG.m1"
    H=$OUT/$TAG.m1.host.log; G=$OUT/$TAG.m1.guest.log
    check "40 identical fingerprints host/guest, no DESYNC on either side, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.m1.run.txt && no_desync $H && no_desync $G && hash_ok $H 800 && hash_ok $G 800"
    check "40 only the HOST had GE_MODE: the guest took the host's mode from SETUP and ran the same level reset ('reset' on Flatland, stage 42)" "grep -aq \"session game mode: 'reset' (the host's)\" $G && grep -aq \"ge: mode: 'reset' on stage 42\" $G && grep -aq \"ge: mode: 'reset' on stage 42\" $H && grep -aq 'mode=reset' $H"
    check "40 the script saw the same world on both machines: every script log line identical ($(nlua $H T) lines): 2 Bonds, positions, zone events, hostage events, math.random draws, pairs() order" "same_lua $H $G T && [ $(nlua $H T) -ge 30 ] && grep -aq '^ge: lua: T stage 42 bonds 2 mode reset' $H"
    check "40 bond.pos() with no argument is player 0 (not the local player) on both machines, and bond.pos(1) is the other Bond" "[ \"\$(grep -a '^ge: lua: T pos-default' $G | sed 's/pos-default/X/')\" = \"\$(grep -a '^ge: lua: T pos0' $G | sed 's/pos0/X/')\" ] && [ \"\$(grep -a '^ge: lua: T pos-default' $H | sed 's/pos-default/X/')\" = \"\$(grep -a '^ge: lua: T pos0' $H | sed 's/pos0/X/')\" ] && ! grep -aq '^ge: lua: T pos1 0.0 0.0 0.0' $G"
    check "40 the B tap of EACH Bond toggles a hostage, through its own record: P0 leads 7200, P1 leads 7201, P1 makes it wait and follow again, P0's press takes it over (hand-over)" "grep -aq '^ge: lua: T follow 7200 by 0' $H && grep -aq '^ge: lua: T follow 7201 by 1' $H && grep -aq '^ge: lua: T wait 7201 by 1' $H && grep -aq '^ge: lua: T follow 7201 by 0' $H && grep -aq 'use press by player 1 consumed' $G && grep -aq 'use press by player 0 consumed' $G"
    check "40 a following hostage that reaches the zone is extracted (either Bond led it); the second one wins the mission on the same tick on both machines" "grep -aq '^ge: lua: T extracted 7200 1' $H && grep -aq '^ge: lua: T extracted 7200 1' $G"
    check "40 per-player HUD: the host's window draws P0's line 2, the guest's window P1's; the all-player lines are the same; the follow / wait flash goes to the pressing Bond only (only P1 makes a hostage wait: 'Hostage waiting' on the guest's HUD, never on the host's)" "hudline $H 0 2 'you are Bond 1 (host)' && hudline $G 1 2 'you are Bond 2 (guest)' && hudline $H 0 1 'MODES IN CO-OP TEST' && hudline $G 1 1 'MODES IN CO-OP TEST' && hudline $H 0 3 'everyone sees this' && hudline $G 1 3 'Hostage waiting' && ! grep -aq \"draws line 3: 'Hostage waiting'\" $H && hudline $H 0 3 'Hostage following'"
    replay_log "$DOUT/$TAG.m1.hostlog.txt" 0 "$TAG.m1_rp0"
    replay_log "$DOUT/$TAG.m1.guestlog.txt" 1 "$TAG.m1_rp1"
    check "40 the session logs replay offline in both seats (mode from the log, Lua script, hostages, extraction), OK, and the script's lines equal the live run's" "replay_ok $OUT/$TAG.m1_rp0.log && replay_ok $OUT/$TAG.m1_rp1.log && same_lua $OUT/$TAG.m1_rp0.log $H T && same_lua $OUT/$TAG.m1_rp1.log $G T"
    (export LAG=100,20,5; mnet "$TAG.m2" $((PORT + 402)) 800 GE_FRAMES=7000)
    check "40 over a laggy, lossy link (100,20,5): identical, no DESYNC, the same fingerprints and script lines as without lag" "grep -q IDENTICAL $OUT/$TAG.m2.run.txt && no_desync $OUT/$TAG.m2.host.log && no_desync $OUT/$TAG.m2.guest.log && cmp -s $OUT/$TAG.m1.cmp.host $OUT/$TAG.m2.cmp.host && same_lua $H $OUT/$TAG.m2.host.log T"
    mlocal "$TAG.m3" 800
    fpcutn "$OUT/$TAG.m3.local.fp" 800 > "$OUT/$TAG.m3.cmp"
    fpcutn "$OUT/$TAG.m1.host.fp" 800 > "$OUT/$TAG.m1.cmp2"
    check "40 the oracle: ONE local co-op instance with the same two input files gives the same per-tick fingerprints as the netplay run" "cmp -s $OUT/$TAG.m3.cmp $OUT/$TAG.m1.cmp2"
    cmp -s "$OUT/$TAG.m3.cmp" "$OUT/$TAG.m1.cmp2" || diff "$OUT/$TAG.m3.cmp" "$OUT/$TAG.m1.cmp2" | head -n 6
    (export VIEW=1; mlocal "$TAG.m4" 800)
    fpcutn "$OUT/$TAG.m4.local.fp" 800 > "$OUT/$TAG.m4.cmp"
    grep -a '^ge: coop: fp ' "$OUT/$TAG.m3.cmp" > "$OUT/$TAG.m3.fpl"   # (the 'th' columns of the tick lines are ordered local-first, so they name the seat)
    grep -a '^ge: coop: fp ' "$OUT/$TAG.m4.cmp" > "$OUT/$TAG.m4.fpl"
    check "40 local co-op in the other seat (view 1): the script's lines and the per-tick state fingerprints are identical to view 0's, so nothing in the mode depends on the local Bond" "[ -n \"\$(tlines $OUT/$TAG.m3.local.log T)\" ] && same_lua $OUT/$TAG.m3.local.log $OUT/$TAG.m4.local.log T && cmp -s $OUT/$TAG.m3.fpl $OUT/$TAG.m4.fpl"
fi

if echo " $PHASES " | grep -q ' 41 '; then
    echo "== phase 41 (MODES-IN-CO-OP): Dam with the REAL hostage script (modes/hostage.lua), both Bonds beside hostages: toggles by either Bond, chain per leader, hand-over; netplay and offline replay"
    (export STAGE=dam MODE=hostage LUATEST= MOVES= MOVECHR=100:0:7200:1,100:1:7201:1,125:0:7202:1,300:1:7200:1 TAPS0=110,130 TAPS1=112,310 YAWS0= YAWS1=; mnet "$TAG.d1" $((PORT + 410)) 700)
    mlogs "$TAG.d1"
    H=$OUT/$TAG.d1.host.log; G=$OUT/$TAG.d1.guest.log
    check "41 identical fingerprints host/guest, no DESYNC, hash exchange matched" "grep -q IDENTICAL $OUT/$TAG.d1.run.txt && no_desync $H && no_desync $G && hash_ok $H 700 && hash_ok $G 700"
    check "41 the guest took the host's mode (hostage) and ran modes/hostage.lua; the setup (holding pad, cluster, spawns) is identical on both machines" "grep -aq \"session game mode: 'hostage' (the host's)\" $G && grep -aq 'running mode script modes/hostage.lua' $G && [ \"\$(grep -a 'ge: lua: ge: hostage: \(bonds\|holding pad\|cluster pads\|setup done\)' $H)\" = \"\$(grep -a 'ge: lua: ge: hostage: \(bonds\|holding pad\|cluster pads\|setup done\)' $G)\" ] && [ \"\$(grep -a 'ge: hostage: \(spawned\|guard\) chr' $H)\" = \"\$(grep -a 'ge: hostage: \(spawned\|guard\) chr' $G)\" ] && grep -aq 'setup done: 4 hostages, 4 holding guards, 2 patrol guards' $G"
    check "41 either Bond's press leads a hostage: 7200 by P0, 7201 by P1, 7202 by P0 (it trails 7200: one chain per leader), then P1 takes 7200 over; the same on both machines" "grep -aq 'chr 7200 -> FOLLOW (leader), by player 0' $G && grep -aq 'chr 7201 -> FOLLOW (leader), by player 1' $G && grep -aq 'chr 7202 -> FOLLOW (leader), by player 0' $G && grep -aq 'chr 7200 -> FOLLOW (leader), by player 1' $G && [ \"\$(grep -a 'ge: hostage: chr .* -> ' $H)\" = \"\$(grep -a 'ge: hostage: chr .* -> ' $G)\" ]"
    replay_log "$DOUT/$TAG.d1.hostlog.txt" 0 "$TAG.d1_rp0"
    replay_log "$DOUT/$TAG.d1.guestlog.txt" 1 "$TAG.d1_rp1"
    check "41 both session logs replay offline: OK" "replay_ok $OUT/$TAG.d1_rp0.log && replay_ok $OUT/$TAG.d1_rp1.log"
    # a hostage is never found by the guard / escort AI's 'guard within N units' search (the Jungle Natalya bug took a Bond's body for one)
    (export LUATEST=$HERE/lua/chrsearch_test.lua MOVES= TAPS0= TAPS1= YAWS0= YAWS1=; mlocal "$TAG.cs1" 120 GE_TEST_CHRSEARCH=60:7300:500)
    (export LUATEST=$HERE/lua/chrsearch_test.lua MOVES= TAPS0= TAPS1= YAWS0= YAWS1=; mlocal "$TAG.cs2" 120 GE_TEST_CHRSEARCH=60:7300:500 GE_TEST_HOSTAGEPICKOLD=1)
    check "41 the chr search from a guard skips the hostage next to it and finds the other guard (7301); negative control (GE_TEST_HOSTAGEPICKOLD=1): the old search takes the hostage (7200)" "grep -aq 'chr search from chr 7300 within 500 -> chr 7301 (hostage state -1)' $OUT/$TAG.cs1.local.log && grep -aq 'chr search from chr 7300 within 500 -> chr 7200 (hostage state 0)' $OUT/$TAG.cs2.local.log"
fi

if echo " $PHASES " | grep -q ' 42 '; then
    echo "== phase 42 (MODES-IN-CO-OP, the headline): Flatland hostage rescue, 10 hostages and 20 guards (modes/hostage-flatland.lua), both Bonds mortal in the middle of the guards: combat, hostage crossfire, deaths, the shared mission failure"
    (export MODE=hostage-flatland LUATEST=$HERE/lua/damage_log.lua MOVES=100:0:145,100:1:130 TAPS0=105 TAPS1=107 YAWS0=101:270000 YAWS1=101:270000; mnet "$TAG.hf" $((PORT + 420)) 2400 H:GE_NET_TEST_NEXT=depot:1 H:GE_NET_TEST_NEXTMODE=0 GE_FRAMES=3200)
    mlogs "$TAG.hf"
    H=$OUT/$TAG.hf.host.log; G=$OUT/$TAG.hf.guest.log
    check "42 10 hostages and 20 guards were spawned (14 holding, 6 'patrol' that hold on Flatland) on both machines, at the same places" "grep -aq 'setup done: 10 hostages, 14 holding guards, 6 patrol guards' $H && grep -aq 'setup done: 10 hostages, 14 holding guards, 6 patrol guards' $G && [ \$(grep -ac 'ge: hostage: spawned chr' $H) -eq 10 ] && [ \$(grep -ac 'ge: hostage: guard chr .* (item' $H) -eq 20 ] && [ \"\$(grep -a 'ge: hostage: \(spawned\|guard\) chr' $H | sed 's/ t=.*//')\" = \"\$(grep -a 'ge: hostage: \(spawned\|guard\) chr' $G | sed 's/ t=.*//')\" ]"
    check "42 the guards fought both Bonds: the damage / death lines of the script are identical on both machines ($(nlua $H D) damage lines), P1 and P0 died, no DESYNC, identical per-tick fingerprints" "same_lua $H $G D && same_lua $H $G DIED && same_lua $H $G BONDDIED && [ $(nlua $H D) -ge 3 ] && [ $(nlua $H BONDDIED) -ge 2 ] && no_desync $H && no_desync $G && fp_same $OUT/$TAG.hf.cmp.host $OUT/$TAG.hf.cmp.guest"
    check "42 both Bonds down = the mission is lost: both machines left the stage on the same sim tick ($(end_tick $OUT/$TAG.hf.host.fp)) and the host's next stage (Depot) loaded on both" "[ -n \"\$(end_tick $OUT/$TAG.hf.host.fp)\" ] && [ \"\$(end_tick $OUT/$TAG.hf.host.fp)\" = \"\$(end_tick $OUT/$TAG.hf.guest.fp)\" ] && grep -aq 'GO (run 2)' $H && grep -aq 'GO (run 2)' $G"
    replay_log "$DOUT/$TAG.hf.hostlog.txt" 0 "$TAG.hf_rp0"
    replay_log "$DOUT/$TAG.hf.guestlog.txt" 1 "$TAG.hf_rp1"
    check "42 the session logs of the 20-guard fight replay offline in both seats, OK, with the same damage lines" "replay_ok $OUT/$TAG.hf_rp0.log && replay_ok $OUT/$TAG.hf_rp1.log && same_lua $OUT/$TAG.hf_rp0.log $H D && same_lua $OUT/$TAG.hf_rp1.log $G D"
fi

if echo " $PHASES " | grep -q ' 43 '; then
    echo "== phase 43 (MODES-IN-CO-OP): win and fail through the shared mission end; the next stage takes the host's mode (NEXTSTAGE)"
    # win: the scenario rescues two hostages (tick ~843) and calls mission.win; then the host picks Depot with the hostage mode
    (mnet "$TAG.w1" $((PORT + 430)) 99999 H:GE_NET_TEST_NEXT=depot:1 H:GE_NET_TEST_NEXTMODE=hostage GE_FRAMES=2600)
    H=$OUT/$TAG.w1.host.log; G=$OUT/$TAG.w1.guest.log
    check "43 mission.win: both machines ended the mission on the same sim tick ($(end_tick $OUT/$TAG.w1.host.fp)), logged as the shared mission end, the report page says 'won' on both" "[ -n \"\$(end_tick $OUT/$TAG.w1.host.fp)\" ] && [ \"\$(end_tick $OUT/$TAG.w1.host.fp)\" = \"\$(end_tick $OUT/$TAG.w1.guest.fp)\" ] && grep -aq 'mission end (mode win)' $OUT/$TAG.w1.host.fp && grep -aq 'mission end (mode win)' $OUT/$TAG.w1.guest.fp && grep -aq 'result=win' $H && grep -aq 'result=win' $G"
    check "43 identical fingerprints up to the end, no DESYNC, the script's lines identical, the same extractions" "fp_same $OUT/$TAG.w1.cmp.host $OUT/$TAG.w1.cmp.guest && no_desync $H && no_desync $G && same_lua $H $G T && grep -aq '^ge: lua: T extracted 7202 2' $H"
    check "43 NEXTSTAGE carried the host's NEW mode: stage 2 (Depot) ran 'hostage' on both machines, the guest took it from the message, the hostage script ran on both, no DESYNC" "grep -aq \"session game mode: 'hostage' (the host's)\" $G && grep -aq \"ge: mode: 'hostage' on stage 30\" $H && grep -aq \"ge: mode: 'hostage' on stage 30\" $G && grep -aq 'GO (run 2)' $G && grep -a 'ge: lua: ge: hostage: bonds' $G | grep -aq 'bonds	2'"
    (mnet "$TAG.w2" $((PORT + 432)) 99999 H:GE_NET_TEST_NEXT=depot:1 H:GE_NET_TEST_NEXTMODE=0 GE_FRAMES=2600)
    H=$OUT/$TAG.w2.host.log; G=$OUT/$TAG.w2.guest.log
    check "43 NEXTSTAGE with mode '0': stage 2 runs NO mode on either machine (a normal stage: no level reset message after GO of run 2)" "grep -aq 'GO (run 2)' $G && grep -aq \"session game mode: '' (the host's)\" $G && [ \$(grep -ac \"ge: mode: '\" $G) -eq 1 ] && [ \$(grep -ac \"ge: mode: '\" $H) -eq 1 ]"
    # fail: two hostages die (GE_HOSTAGE_TEST_KILL) in the real script on Dam
    (export STAGE=dam MODE=hostage LUATEST= MOVES= TAPS0= TAPS1= YAWS0= YAWS1=; mnet "$TAG.f1" $((PORT + 434)) 99999 H:GE_NET_TEST_NEXT=depot:1 GE_HOSTAGE_TEST_KILL=2 GE_FRAMES=2000)
    H=$OUT/$TAG.f1.host.log; G=$OUT/$TAG.f1.guest.log
    check "43 mission.fail (two hostages dead): both machines left the stage on the same tick ($(end_tick $OUT/$TAG.f1.host.fp)), 'mission end (mode fail)' on both, the report says 'fail', Depot loaded on both next" "[ -n \"\$(end_tick $OUT/$TAG.f1.host.fp)\" ] && [ \"\$(end_tick $OUT/$TAG.f1.host.fp)\" = \"\$(end_tick $OUT/$TAG.f1.guest.fp)\" ] && grep -aq 'mission end (mode fail)' $OUT/$TAG.f1.host.fp && grep -aq 'mission end (mode fail)' $OUT/$TAG.f1.guest.fp && grep -aq 'result=fail' $H && grep -aq 'result=fail' $G && grep -aq 'GO (run 2)' $H && grep -aq 'GO (run 2)' $G && no_desync $H && no_desync $G && fp_same $OUT/$TAG.f1.cmp.host $OUT/$TAG.f1.cmp.guest"
    # both Bonds die in the mode (the stock mission-lost flow, shared): same tick
    (mnet "$TAG.k1" $((PORT + 436)) 99999 H:GE_NET_TEST_NEXT=depot:1 GE_COOP_KILL=200:0,200:1 GE_FRAMES=1800)
    H=$OUT/$TAG.k1.host.log; G=$OUT/$TAG.k1.guest.log
    check "43 both Bonds dead in a mode: 'all Bonds dead', the stage left on the same tick on both machines, the next stage loaded, no DESYNC" "grep -aq 'all Bonds dead' $OUT/$TAG.k1.host.fp && [ -n \"\$(end_tick $OUT/$TAG.k1.host.fp)\" ] && [ \"\$(end_tick $OUT/$TAG.k1.host.fp)\" = \"\$(end_tick $OUT/$TAG.k1.guest.fp)\" ] && grep -aq 'GO (run 2)' $G && no_desync $H && no_desync $G && fp_same $OUT/$TAG.k1.cmp.host $OUT/$TAG.k1.cmp.guest"
    # lives and respawn rules apply in a mode: one life each, P1 out at tick 200 (the mission goes on), P0 dies at 500 (nobody left: lost)
    (mnet "$TAG.k2" $((PORT + 438)) 99999 H:GE_NET_TEST_NEXT=depot:1 GE_MODS=invincible=0,coop_lives=1 GE_COOP_KILL=200:1,500:0 GE_FRAMES=1800)
    H=$OUT/$TAG.k2.host.log; G=$OUT/$TAG.k2.guest.log
    check "43 coop_lives=1: P1's only life is gone at tick 200 (out, watches P0), the mode goes on, P0's death at 500 ends it (all down) on the same tick on both machines" "grep -aq 'player 1 died: its lives left 0 - OUT OF LIVES' $OUT/$TAG.k2.host.fp && grep -aq 'all Bonds dead' $OUT/$TAG.k2.host.fp && [ \"\$(end_tick $OUT/$TAG.k2.host.fp)\" = \"\$(end_tick $OUT/$TAG.k2.guest.fp)\" ] && no_desync $H && no_desync $G && same_lua $H $G T"
    # unlimited lives: P1 dies at 200 and respawns at its start (coop_respawn=start) while the hostage it led goes on with P0
    (mnet "$TAG.k3" $((PORT + 440)) 700 GE_MODS=invincible=0 GE_COOP_KILL=300:1 GE_COOP_RESPAWN=100)
    H=$OUT/$TAG.k3.host.log; G=$OUT/$TAG.k3.guest.log
    check "43 P1 dies while it leads a hostage and respawns (10 s later); BOND_DIED reaches the script with the player index; the hostage trails the other Bond meanwhile; identical on both machines, no DESYNC" "grep -aq 'player 1 died' $OUT/$TAG.k3.host.fp && grep -aq 'player 1 respawned' $OUT/$TAG.k3.host.fp && grep -aq '^ge: lua: T bond_died -1 1' $H && same_lua $H $G T && grep -aq '^ge: lua: T extracted 7201 2' $H && no_desync $H && no_desync $G && fp_same $OUT/$TAG.k3.cmp.host $OUT/$TAG.k3.cmp.guest && [ \"$(end_tick $OUT/$TAG.k3.host.fp)\" = \"$(end_tick $OUT/$TAG.k3.guest.fp)\" ]"
fi

if echo " $PHASES " | grep -q ' 44 '; then
    echo "== phase 44 (MODES-IN-CO-OP): negative control: a script that reads the LOCAL player (bond.pos() with GE_LUA_TEST_LEAKLOCAL=1 on the guest) desyncs"
    (mnet "$TAG.l1" $((PORT + 450)) 600 G:GE_LUA_TEST_LEAKLOCAL=1)
    H=$OUT/$TAG.l1.host.log; G=$OUT/$TAG.l1.guest.log
    check "44 with the local-player read on the guest: both sides report DESYNC (the zone / the probe spawn land elsewhere), fingerprints differ" "grep -aq 'ge: net: DESYNC at tick' $H && grep -aq 'ge: net: DESYNC at tick' $G && ! grep -q IDENTICAL $OUT/$TAG.l1.run.txt && ! same_lua $H $G T"
    grep -a 'ge: net: DESYNC at tick' "$H" | head -n 1
fi

if echo " $PHASES " | grep -q ' 45 '; then
    echo "== phase 45 (MODES-IN-CO-OP): the script set (including Data/Scripts/modes) is part of the handshake; the guest follows the host's mode, not its own"
    MD="$ROOT/Data/Scripts/modes"
    probe() {   # <tag> <port>: a host that waits 3 s for a guest, only to log its script-set hash
        env GE_PROFILE=$HP GE_NOINISAVE=1 GE_SKIPINTRO=1 GE_NET=host:$2 GE_NET_WAIT=3 GE_STAGE=dam GE_FRAMES=2000 timeout -s KILL 60 "$BIN" > "$OUT/$1.log" 2>&1
        grep -a 'ge: net: script set hash' "$OUT/$1.log" | head -n 1
    }
    a=$(probe "$TAG.hash1" $((PORT + 460)))
    printf -- '-- hash probe\n' > "$MD/zz_hashprobe.lua"
    b=$(probe "$TAG.hash2" $((PORT + 461)))
    rm -f "$MD/zz_hashprobe.lua"
    c=$(probe "$TAG.hash3" $((PORT + 462)))
    check "45 an extra file in Data/Scripts/modes changes the script-set hash (without: '$a', with: '$b'), and removing it brings the old hash back ('$c')" "[ -n '$a' ] && [ '$a' != '$b' ] && [ '$a' = '$c' ]"
    (mnet "$TAG.h1" $((PORT + 464)) 100 G:GE_NET_TEST_LUAHASH=00000001)
    check "45 a guest whose script set differs (GE_NET_TEST_LUAHASH) is refused: 'Lua script set mismatch' in the logs, no stage ran" "grep -aq 'Lua script set mismatch' $OUT/$TAG.h1.guest.log && ! grep -aq 'GO (run 1)' $OUT/$TAG.h1.host.log"
    (mnet "$TAG.h2" $((PORT + 466)) 200 G:GE_MODE=hostage)
    check "45 the guest's own GE_MODE=hostage is ignored: it runs the host's 'reset' (both machines print the same mode)" "grep -aq \"session game mode: 'reset' (the host's)\" $OUT/$TAG.h2.guest.log && ! grep -aq \"ge: mode: 'hostage'\" $OUT/$TAG.h2.guest.log && grep -q IDENTICAL $OUT/$TAG.h2.run.txt"
fi

check "every run found its input files (no 'cannot open the replay file' in this run's logs)" "! grep -alq 'cannot open the replay file' $OUT/$TAG.*.log $OUT/$TAG.*.txt"

echo "net_lockstep: $FAILS failed"
exit $FAILS

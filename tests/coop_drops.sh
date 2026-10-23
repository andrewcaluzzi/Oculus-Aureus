#!/bin/sh
# Netplay job 3d: co-op respawn loadout, death drops, pickup messages, audio volume (build_pc + Xvfb, no netplay).
#   wsl -d Ubuntu -- docker exec --user 1000 -e DISPLAY=:99 midas-net sh /home/dev/project/tests/coop_drops.sh <scenario> [outdir]
# Scenarios: respawn_surface, respawn_dam, drop_kf7, drop_key, drop_unsafe, drop_gekey, drop_dattape, drop_gekey_unsafe, audio0, audio1.
# (GE_HUDLOG=1 in drop_kf7 / drop_key logs which Bond's HUD queue each "Picked up ..." message goes to.)
# Each writes <outdir>/<scenario>.fp (the fingerprint / inventory log) and <scenario>.raw (stdout); what to look for is in
# Netplay job 3d (co-op death drops).
BIN=${BIN:-/home/dev/project/build_pc/ProjectMidas}
SC=$1
OUT=${2:-/home/dev/project/build_tests/coop3d}
mkdir -p "$OUT"
cd /home/dev/project/build_pc || exit 1

COMMON="GE_COOP=1 GE_SKIPINTRO=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_NOINISAVE=1 GE_COOP_LOG=1 GE_COOP_FP=1 GE_COOP_INVLOG=1 GE_MODS=invincible=0 GE_COOP_LOCAL=idle GE_COOP_P2=idle"

run() {
    name=$1
    shift
    env $COMMON GE_COOP_FPFILE="$OUT/$name.fp" "$@" "$BIN" > "$OUT/$name.raw" 2>&1
}

case "$SC" in
respawn_surface)
    # P1 dies at tick 150 and respawns 60 fields later; then cycles weapons 6 times
    run respawn_surface GE_STAGE=surface GE_COOP_KILL=150:1 GE_COOP_RESPAWN=60 GE_COOP_CYCLE=300:1:5:60 GE_FRAMES=700
    ;;
respawn_dam)
    run respawn_dam GE_STAGE=dam GE_COOP_KILL=150:1 GE_COOP_RESPAWN=60 GE_COOP_CYCLE=300:1:5:60 GE_FRAMES=700
    ;;
drop_kf7)
    # P1 holds a picked-up KF7 (item 8, rifle ammo type 3) with 80 rounds, dies at 150; P0 is moved beside the body at 170 and picks it up
    run drop_kf7 GE_STAGE=dam GE_COOP_GIVEWEAPON=90:1:8:80 GE_COOP_KILL=150:1 GE_COOP_RESPAWN=120 GE_COOP_MOVETO=170:0:1 GE_FRAMES=420 GE_HUDLOG=1
    ;;
drop_kf7_hurt)
    # the same through the REAL damage path (GE_COOP_HURT: record_damage_kills, death camera) instead of GE_COOP_KILL; the partner
    # is moved beside the body at 170 and must hold the KF7 well before the dead Bond respawns at 270 (inv tick180 / 200 / 230 / 250)
    run drop_kf7_hurt GE_STAGE=dam GE_COOP_GIVEWEAPON=90:1:8:80 GE_COOP_HURT=150:1:10 GE_COOP_RESPAWN=120 GE_COOP_MOVETO=170:0:1 GE_FRAMES=420 GE_HUDLOG=1
    ;;
drop_corpse)
    # the corpse run: P0 already holds a KF7 with full reserve ammo (so it cannot take a second one), P1 dies at 150 and drops its KF7, P0 stands
    # beside the body from 170 and leaves the gun lying. P1 never collects it while dead (inv tick180 / 200 / 230 / 250 p1 without W8) and does
    # right after its respawn at 270 (inv tick290 p1 holds W8 again)
    run drop_corpse GE_STAGE=dam GE_COOP_GIVEWEAPON=90:1:8:80,100:0:8:9999 GE_COOP_HURT=150:1:10 GE_COOP_RESPAWN=120 GE_COOP_MOVETO=170:0:1:1,280:1:0:1 GE_FRAMES=520 GE_HUDLOG=1
    ;;
drop_key)
    # Depot: P0 picks the first key at tick 60 and dies at 120; P1 is moved beside the body at 160 and picks the key up again
    run drop_key GE_STAGE=depot GE_COOP_GIVEKEY=1 GE_COOP_KILL=120:0 GE_COOP_RESPAWN=120 GE_COOP_MOVETO=160:1:0 GE_FRAMES=420 GE_HUDLOG=1
    ;;
drop_gekey|drop_dattape|drop_gekey_unsafe)
    # Netplay job 6: an objective item that is a ring "gadget" drops at the death spot too (item 61 GOLDENEYEKEY, 73 DATTAPE): P1 is given it at
    # 90, dies at 150 (real damage path); P0 is moved beside the body at 170 and must hold it afterwards (inv tick200 p0 with W61 / W73, p1 without).
    # drop_gekey_unsafe: the spot forced unsafe, so it goes straight to the partner instead
    case "$SC" in
    drop_gekey*) item=61 ;;
    *) item=73 ;;
    esac
    extra=
    case "$SC" in
    *_unsafe) extra=GE_COOP_UNSAFEDROP=1 ;;
    esac
    run "$SC" GE_STAGE=dam GE_COOP_GIVEWEAPON=90:1:$item GE_COOP_HURT=150:1:10 GE_COOP_RESPAWN=120 GE_COOP_MOVETO=170:0:1 GE_FRAMES=420 GE_HUDLOG=1 $extra
    ;;
drop_unsafe)
    # the same with the spot forced unsafe: the key goes straight to the partner
    run drop_unsafe GE_STAGE=depot GE_COOP_GIVEKEY=1 GE_COOP_KILL=120:0 GE_COOP_RESPAWN=120 GE_COOP_UNSAFEDROP=1 GE_FRAMES=300 GE_HUDLOG=1
    ;;
audio0|audio1)
    # guards shoot at idle Bonds; every positional sound logs the volume each Bond alone would hear and the one used (the local Bond's)
    v=${SC#audio}
    if [ "$v" = 0 ]; then lw=walk; pw=idle; else lw=idle; pw=walk; fi
    run "$SC" GE_STAGE=dam GE_SPAWNXYZ=19300,-100,17300,53 GE_COOP_VIEW=$v GE_COOP_LOCAL=$lw GE_COOP_P2=$pw GE_SNDLOG=1 GE_MODS=invincible=1 GE_FRAMES=700
    ;;
shot0|shot1)
    # the KF7 scenario with a BMP every 20 frames (GE_SHOT) in <outdir>/<scenario>/: shot0 is Bond 0's view (the picker), shot1 Bond 1's
    v=${SC#shot}
    mkdir -p "$OUT/$SC"
    cd "$OUT/$SC" || exit 1
    run "$SC" GE_STAGE=dam GE_COOP_VIEW=$v GE_SHOT=20 GE_COOP_GIVEWEAPON=90:1:8:80 GE_COOP_KILL=150:1 GE_COOP_RESPAWN=120 GE_COOP_MOVETO=170:0:1 GE_FRAMES=330 GE_HUDLOG=1
    ;;
*)
    echo "unknown scenario $SC"
    exit 2
    ;;
esac
echo "done $SC"

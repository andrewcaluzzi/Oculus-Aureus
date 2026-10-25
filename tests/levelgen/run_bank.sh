#!/bin/sh
# The Janus bank v2 (designs/bank-heist.md section 17, modding/levelgen.md), inside the build container with Xvfb on :99 :100 :101 :102
# and build_pc built from tools/levelgen/janus_bank.json (python3 tools/gen_level.py tools/levelgen/janus_bank.json, then build).
# tests/levelgen/levelgen_bank.lua in four parts, side by side (a walk round six levels is long): static (the layout's counts, linked
# double doors, the heist map, a double door culling, the openings, the counter, a shelf), route_a (the building on foot, top to
# bottom), route_b (the plant level, the tunnel to the canal, the maintenance stair), guards (guards routing between areas and floors).
# One sim tick per frame (GE_FIXEDTICK=1). Screenshots: build_pc/levelgen_bank_shots_<part>/shot_NNNNN.bmp (each follows a
# "LEVELGEN SHOT <name>" line in the log). Convert with tests/bmp2png.py and delete the BMPs.
# usage: run_bank.sh [parts]   -> the LEVELGEN lines of each part; each ends "LEVELGEN DONE <n> passed, <m> failed".
# Full logs: ${LOGDIR:-/tmp}/levelgen_bank_<part>.log
LOGDIR=${LOGDIR:-/tmp}
cp /home/dev/project/tests/levelgen/levelgen_bank.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_bank.lua
cp /home/dev/project/port/levelgen/levelgen_layout.lua /home/dev/project/build_pc/Data/Scripts/modes/levelgen_layout.lua
cp /home/dev/project/tools/heist_bank_map.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_bank_map.lua
d=99
for part in ${*:-static route_a route_b guards}; do
    case $part in
        route_a) flags="GE_BANK_ROUTE_A=1"; frames=400000 ;;
        route_b) flags="GE_BANK_ROUTE_B=1"; frames=250000 ;;
        guards) flags="GE_BANK_GUARDS=1"; frames=150000 ;;
        *) flags=""; frames=60000 ;;
    esac
    dir=/home/dev/project/build_pc/levelgen_bank_shots_$part
    mkdir -p $dir
    rm -f $dir/shot_*.bmp
    (cd $dir && env $flags DISPLAY=:$d GE_MODE=levelgen_bank GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FULLHEIGHT=0 \
        GE_EDGEINSET=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_FRAMES=${GE_FRAMES:-$frames} timeout 5400 ../ProjectMidas > $LOGDIR/levelgen_bank_$part.log 2>&1) &
    d=$((d + 1))
done
wait
for part in ${*:-static route_a route_b guards}; do
    echo "LEVELGEN PART $part"
    grep -E "LEVELGEN (PASS|FAIL|DONE|SHOT|error)|ge: lua: .*error|setup lint" $LOGDIR/levelgen_bank_$part.log
done

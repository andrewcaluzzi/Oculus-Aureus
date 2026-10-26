#!/bin/sh
# Heist CCTV self-view test (designs/bank-heist.md 23c): in local co-op (GE_COOP=1, the Bonds have bodies) Bond 0 watches a camera that
# sees him at the banking hall's screens; the feed must draw his own body (cctv.render_info().bonds). Inside the build container with
# Xvfb on :99 and build_pc built. Screenshots: build_pc/heist_cctvself_shots/shot_NNNNN.bmp (convert with tests/bmp2png.py, delete the BMPs).
# usage: run_cctvself.sh   -> the HEIST lines; the last one is "HEIST DONE <n> passed, <m> failed". Full log: ${LOGDIR:-/tmp}/heist_cctvself.log
M=/home/dev/project/build_pc/Data/Scripts/modes
cp /home/dev/project/tests/heist/heist_cctvself.lua $M/heist_cctvself.lua
for f in mode_heist.lua:heist.lua heist_lib.lua heist_bank_map.lua heist_ai.lua; do
    cp "/home/dev/project/tools/${f%%:*}" "$M/${f#*:}"
done
cp /home/dev/project/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
mkdir -p /home/dev/project/build_pc/heist_cctvself_shots
rm -f /home/dev/project/build_pc/heist_cctvself_shots/shot_*.bmp
cd /home/dev/project/build_pc/heist_cctvself_shots || exit 1
DISPLAY=:99 GE_MODE=heist_cctvself GE_STAGE=levelgen GE_COOP=1 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FULLHEIGHT=0 GE_EDGEINSET=1 GE_FIXEDTICK=1 \
    GE_TICKHZ=60 GE_FRAMES=${GE_FRAMES:-3000} timeout 300 ../ProjectMidas > ${LOGDIR:-/tmp}/heist_cctvself.log 2>&1
grep -a -E "HEIST|gfx: wrote|cctvdbg|ge: lua: .*error" ${LOGDIR:-/tmp}/heist_cctvself.log | head -n 60

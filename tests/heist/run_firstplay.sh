#!/bin/sh
# First-play screenshots of the real heist (tests/heist/heist_firstplay.lua), inside the build container with Xvfb on :99.
# Writes build_pc/firstplay_shots/shot_NNNNN.bmp and converts them to PNG (tests/bmp2png.py), deleting the BMPs.
# The HUD lines and the panel are printed at each shot, so the log alone shows the text.
cp /home/dev/project/tests/heist/heist_firstplay.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_firstplay.lua
for f in mode_heist:heist heist_lib heist_bank_map heist_ai heist_intel heist_ar heist_gadgets heist_guide; do
    src=${f%%:*}; dst=${f##*:}
    cp /home/dev/project/tools/$src.lua /home/dev/project/build_pc/Data/Scripts/modes/$dst.lua
done
mkdir -p /home/dev/project/build_pc/firstplay_shots
rm -f /home/dev/project/build_pc/firstplay_shots/shot_*.bmp /home/dev/project/build_pc/firstplay_shots/shot_*.png
cd /home/dev/project/build_pc/firstplay_shots || exit 1
DISPLAY=:99 GE_MODE=heist_firstplay GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=${GE_SEED:-7} GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
    GE_FRAMES=${GE_FRAMES:-5000} timeout 400 ../ProjectMidas > ${LOGDIR:-/tmp}/heist_firstplay.log 2>&1
grep -aE "HEIST|gfx: wrote|ge: lua: .*error" ${LOGDIR:-/tmp}/heist_firstplay.log
for b in shot_*.bmp; do [ -f "$b" ] && python3 /home/dev/project/tests/bmp2png.py "$b" "${b%.bmp}.png" && rm -f "$b"; done
ls -la

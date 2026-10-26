#!/bin/sh
# One solo heist determinism scenario run with a hash log (the personalities stream's "off = today" proof, designs/bank-heist.md 32):
# run it on a build without the change and on one with it (personalities off); the two hash logs must be identical.
# usage: run_traits_base.sh <out.hash>   (inside the build container, Xvfb on :99, build_pc built)
OUTH=${1:-/tmp/traits_base.hash}
cp /home/dev/project/tests/heist/heist_determinism.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_determinism.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cd /home/dev/project/build_pc || exit 1
env DISPLAY=:99 GE_MODE=heist_determinism GE_STAGE=levelgen GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_SEED=7 GE_FIXEDTICK=1 GE_TICKHZ=60 \
    GE_FRAMES=${GE_FRAMES:-4500} GE_HASHLOG=$OUTH timeout 600 ./ProjectMidas > $OUTH.log 2>&1
echo "ticks: $(grep -c '^tick' $OUTH)"

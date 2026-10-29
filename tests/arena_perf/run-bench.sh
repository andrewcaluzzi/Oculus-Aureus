#!/bin/sh
# The late-wave arena benchmark headless (inside the goldeneye image, Xvfb :99 running): build_pc/Data/Logs/arena_bench.txt.
# Under llvmpipe the draw counts, triangles and the spectator split mean what they say; the ms do not (software GL).
cd /home/dev/project/build_pc || exit 1
mkdir -p bench
cd bench || exit 1
DISPLAY=:99 GE_MODE=arena-bench GE_BENCH_LATE=1 GE_STAGE=colosseum GE_SKIPINTRO=1 GE_CHEATS=invuln GE_FULLHEIGHT=0 GE_EDGEINSET=1 \
  GE_NOINISAVE=1 GE_FRAMES=${FRAMES:-200000} ../ProjectMidas > bench.log 2>&1
echo "exit $?"

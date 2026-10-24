#!/bin/sh
# the dam1 demo oracle on the Linux build (inside the goldeneye Docker image): identical = "dam1: IDENTICAL"
cd /home/dev/project/build_pc || exit 1
GE_DEMO=dam1 GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 xvfb-run -a ./ProjectMidas 2>/dev/null | grep '^ge: demo tick' > /tmp/dam1.txt
wc -l < /tmp/dam1.txt
cmp /tmp/dam1.txt ../tests/baseline/dam1.a.txt && echo "dam1: IDENTICAL"

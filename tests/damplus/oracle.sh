#!/bin/sh
# Attract-demo oracle in the container (Xvfb :99). $1 = demo (dam1|dam2), $2 = tag, $3 = GE_DAMPLUS value ("" = unset), $4 = exe (default build_pc).
# Writes /home/dev/out/oracle/<demo>.<tag>.txt: the first demo's 'ge: demo tick' lines (cut at a second "tick 1").
mkdir -p /home/dev/out/oracle /home/dev/out/shots
cd /home/dev/out/shots || exit 1
export DISPLAY=:99 GE_DEMO=$1 GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 GE_ROM=/home/dev/project/baserom.u.z64
if [ -n "$3" ]; then export GE_DAMPLUS=$3; else unset GE_DAMPLUS; fi
EXE=${4:-/home/dev/project/build_pc/ProjectMidas}
"$EXE" > /home/dev/out/oracle/$1.$2.full.log 2>&1
grep '^ge: demo tick' /home/dev/out/oracle/$1.$2.full.log | awk 'BEGIN{n=0} /demo tick 1 /{n++} n<=1' > /home/dev/out/oracle/$1.$2.txt
wc -l /home/dev/out/oracle/$1.$2.txt
grep -c 'dam+' /home/dev/out/oracle/$1.$2.full.log

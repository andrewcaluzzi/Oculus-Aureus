#!/bin/sh
# Turns the BMPs a run left in build_tests/maps/shots (bench.shot) into small JPGs and deletes the BMPs.
# usage: shots2jpg.sh <name prefix> [width] [outdir]    -> <outdir>/<prefix>-<n>.jpg   (default 640 wide, /shots/arena-maps = E:\midas-temp\arena-maps)
S=/home/dev/project/build_tests/maps/shots
W=${2:-640}
J=${3:-/shots/arena-maps}
mkdir -p $J
n=0
for f in $(ls $S/shot_*.bmp 2>/dev/null | sort); do
  n=$((n + 1))
  convert "$f" -resize ${W}x -quality 80 "$J/$1-$n.jpg"
  rm -f "$f"
done
ls -l $J | tail -n +2

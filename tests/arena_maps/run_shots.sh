#!/bin/sh
# One mid-wave picture per stock multiplayer map (tests/arena_maps/maps-smoke.lua, GE_MAPSMOKE_SHOT1=1): Bond is put 5 m from the nearest
# enemy at 22 s into the run, looking at it, bench.shot() takes one frame; each is turned into a 640x360 JPG in the output dir and the BMP goes.
# usage: run_shots.sh [outdir]   (default /shots/arena-maps in the container = E:\midas-temp\arena-maps)
P=/home/dev/project
OUT=${1:-/shots/arena-maps}
for m in temple complex caves library stack basement facility; do
  rm -f $P/build_tests/maps/shots/shot_*.bmp
  sh $P/tests/arena_maps/run.sh $m 3600 "GE_MAPSMOKE_SHOT1=1" > /dev/null 2>&1
  if ls $P/build_tests/maps/shots/shot_*.bmp > /dev/null 2>&1; then
    sh $P/tests/arena_maps/shots2jpg.sh $m 640 $OUT > /dev/null
    echo "$m: $(ls -l $OUT/$m-1.jpg | awk '{print $5}') bytes"
    mv $OUT/$m-1.jpg $OUT/$m.jpg
  else
    echo "$m: NO PICTURE"
  fi
done

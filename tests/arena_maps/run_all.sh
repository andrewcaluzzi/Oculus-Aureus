#!/bin/sh
# The smoke on every stock multiplayer map (tests/arena_maps/run.sh) in turn: usage run_all.sh [frames] ["GE_X=y ..."]
P=/home/dev/project
for m in temple complex caves library stack basement facility; do
  sh $P/tests/arena_maps/run.sh $m ${1:-14000} "$2"
done

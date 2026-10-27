#!/bin/sh
# A7 perf matrix (uid 1000 in the container, cwd build_pc). Heavy wave 12, up to 40 enemies; crowd 200 or 0.
# sim-only stats (GE_SIMONLY_TEST window, per-section ms), then full-frame fps (GE_PERFLOG), render thread off / on.
R=/home/dev/project/tests/arena_a7/perfrun.sh
mkdir -p /home/dev/project/build_tests/a7
GE_ARENA_CROWDCOLLIDE=1 sh $R polish-heavy m-sim-collide 1500 0 500,700
sh $R polish-heavy m-sim-pin 1500 0 500,700
sh $R polish-heavy0 m-sim-crowd0 1500 0 500,700
GE_ARENA_CROWDCOLLIDE=1 sh $R polish-heavy m-fps-collide-rt0 700 0
sh $R polish-heavy m-fps-pin-rt0 700 0
sh $R polish-heavy m-fps-pin-rt1 700 1

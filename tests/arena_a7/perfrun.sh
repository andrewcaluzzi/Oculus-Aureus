#!/bin/sh
# perf runs: usage perfrun.sh <preset> <log> <frames> [renderthread 0|1] [simonly start,n]
export GE_PERFLOG=1
if [ -n "$4" ]; then export GE_RENDERTHREAD=$4; fi
if [ -n "$5" ]; then export GE_SIMONLY_TEST=$5 GE_SIMONLY_STATS=1; fi
sh /home/dev/project/tests/arena_a7/run_polish.sh $2 $1 $3

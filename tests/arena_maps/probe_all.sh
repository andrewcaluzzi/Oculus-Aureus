#!/bin/sh
# Probe every stock MP map (tests/arena_maps/probe.sh) and print the path graph's numbers: usage probe_all.sh [frames]
P=/home/dev/project
for m in mptemple mpcomplex mpcaves mplibrary mpbasement mpstack mpfacility; do
  sh $P/tests/arena_maps/probe.sh $m ${1:-600} > /dev/null 2>&1
  echo "== $m"
  grep -a "mpnav\|PROBE stage\|PROBE pads\|^exit" $P/build_tests/maps/probe_$m.log
done

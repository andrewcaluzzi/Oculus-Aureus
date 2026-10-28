#!/bin/sh
# The render fixes in netplay: the four arena netplay presets (host + guest over UDP, tests/net_modes.sh) with crowd_lod / chr_batch /
# corpse_bake forced on on both machines. Per run: the host and guest per-tick fingerprints must be identical (cmp of the .txt files
# net_replay2.sh writes), no DESYNC, and every periodic state-hash comparison ok. Runs one at a time (two game processes).
# Inside the goldeneye image with Xvfb :99 running. Usage: run-net.sh [fixes: on (default) | off]
P=/home/dev/project
OUT=$P/build_tests/v02/net
mkdir -p $OUT
cp $P/tests/arena_a7/coop-net.lua $P/tests/arena_a7/merge-coopchaos-net.lua $P/tests/arena_a8/a8-net.lua $P/tests/arena_whacky/whacky-net.lua \
  $P/build_pc/Data/Scripts/modes/
if [ "${1:-on}" = on ]; then FX="GE_CROWD_LOD=2500 GE_CHR_BATCH=1 GE_CORPSE_BAKE=1"; else FX="GE_CROWD_LOD=0 GE_CHR_BATCH=0 GE_CORPSE_BAKE=0"; fi
run() { # name mode ticks port
  STAGE=colosseum MODE=$2 LUATEST= MOVES= OUT=$OUT sh $P/tests/net_modes.sh net $1 $4 $3 $FX > $OUT/$1.out 2>&1
  h=$OUT/$1.host.txt; g=$OUT/$1.guest.txt
  if [ -s $h ] && cmp -s $h $g; then same=IDENTICAL; else same=DIFFER; fi
  ok=$(grep -ac "state hash ok" $OUT/$1.host.log)
  bad=$(grep -aci "state hash mismatch\|hash MISMATCH" $OUT/$1.host.log $OUT/$1.guest.log | awk -F: '{ s += $NF } END { print s + 0 }')
  ds=$(cat $OUT/$1.host.log $OUT/$1.guest.log | grep -ac "DESYNC")
  last=$(grep -a "state hash ok" $OUT/$1.host.log | tail -1)
  fx=$(grep -a -c "overrides the" $OUT/$1.host.log)
  echo "$1 ($2): fingerprints host/guest $same ($(grep -c . $h) lines), DESYNC $ds, hash ok $ok mismatched $bad, override lines $fx | $last"
}
run v02-coop coop-net 1700 27600
run v02-chaos merge-coopchaos-net 1900 27620
run v02-a8 a8-net 2400 27640
run v02-whacky whacky-net 2200 27660
echo "arena net ($1) done"

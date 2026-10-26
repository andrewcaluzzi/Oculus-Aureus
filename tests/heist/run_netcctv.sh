#!/bin/sh
# Netplay check for the CCTV feed's Port binds (designs/bank-heist.md 23c): port_cctvview.c portCctvViewTick takes next / previous / leave
# from the Port actions (joy_port.c portJoyPortCctvTake), in the watching Bond's own tick. Under lockstep those latches come from the
# synced records (portJoyPortNetApply on pads 0 and 1, portCoopPadSwap), so both machines must apply a press to the same Bond.
# The heist determinism scenario (heist_determinism.lua, GE_HEIST_DET_P1=1: Bond 0, the host's Bond, watches the feeds) runs as a host
# and a guest on loopback; the HOST's local Bond also gets a scripted next_weapon action press every 45 ticks (GE_COOP_LOCAL=replay:,
# the record's action bits, never the guest's input). While Bond 0 watches, each press cycles his feed (cctv_view why=1).
# Pass: the per-tick hash logs agree (>= NEED ticks), no DESYNC, and both sides logged the same cycles (at least one).
# Inside the build container with Xvfb on :99 and build_pc built. Run it alone (two processes; a busy machine misses the tick budget).
#   run_netcctv.sh  -> "HEIST NETCCTV PASS|FAIL ..." lines, last "HEIST NETCCTV DONE <n> passed, <m> failed"
OUT=${OUT:-/tmp/heist_netcctv}
mkdir -p $OUT
M=/home/dev/project/build_pc/Data/Scripts/modes
cp /home/dev/project/tests/heist/heist_determinism.lua $M/heist_determinism.lua
for f in mode_heist.lua:heist.lua heist_lib.lua heist_bank_map.lua heist_ai.lua heist_intel.lua heist_guide.lua heist_ar.lua heist_gadgets.lua; do
    cp "/home/dev/project/tools/${f%%:*}" "$M/${f#*:}"
done
cp /home/dev/project/port/levelgen/levelgen_layout.lua $M/levelgen_layout.lua
cd /home/dev/project/build_pc || exit 1
FRAMES=${GE_FRAMES:-2600}   # (the scenario's long feed, step 5, runs from about 19 s to the vault's opening at about 33 s)
NETTICKS=${NETTICKS:-$((FRAMES - 400))}
NEED=${NEED:-$((NETTICKS - 600))}
PORT=${PORT:-27431}
PASS=0
FAIL=0
# next_weapon is Port action 6 (joy_port.c): bit 0x40; "R tick buttons sx sy yaw pitch actions dx dy flags"
t=300
: > $OUT/rec_host.txt
while [ $t -lt $NETTICKS ]; do
    echo "R $t 0 0 0 0 0 40 0 0 0" >> $OUT/rec_host.txt
    t=$((t + 45))
done
for pr in hnc_h hnc_g; do mkdir -p Data/Profiles/$pr; done
rm -f $OUT/net.*.hash
NETCOMMON="DISPLAY=:99 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_HEIST_DET_P1=1 GE_FRAMES=$((FRAMES + 2000)) GE_NET_STOP=$NETTICKS"
env $NETCOMMON GE_PROFILE=hnc_h GE_NET=host:$PORT GE_STAGE=levelgen GE_SEED=7 GE_MODE=heist_determinism GE_HASHLOG=$OUT/net.host.hash \
    GE_COOP_LOCAL=replay:$OUT/rec_host.txt timeout -s KILL 1500 ./ProjectMidas > $OUT/net.host.log 2>&1 &
HP=$!
sleep 2
env $NETCOMMON GE_PROFILE=hnc_g GE_NET=join:127.0.0.1:$PORT GE_HASHLOG=$OUT/net.guest.hash \
    timeout -s KILL 1500 ./ProjectMidas > $OUT/net.guest.log 2>&1 &
GP=$!
wait $HP
wait $GP

grep '^tick' $OUT/net.host.hash > $OUT/cmp.a
grep '^tick' $OUT/net.guest.hash > $OUT/cmp.b
na=$(wc -l < $OUT/cmp.a)
nb=$(wc -l < $OUT/cmp.b)
n=$na
[ "$nb" -lt "$n" ] && n=$nb
head -n $n $OUT/cmp.a > $OUT/cmp.a2
head -n $n $OUT/cmp.b > $OUT/cmp.b2
if cmp -s $OUT/cmp.a2 $OUT/cmp.b2 && [ "$n" -ge "$NEED" ]; then
    echo "HEIST NETCCTV PASS host and guest: $n ticks hash-identical"; PASS=$((PASS + 1))
else
    echo "HEIST NETCCTV FAIL host and guest: $n ticks (need $NEED), first difference: $(diff $OUT/cmp.a2 $OUT/cmp.b2 | grep '^<' | head -n 1 | cut -c1-160)"
    FAIL=$((FAIL + 1))
fi
d=$(grep -ac "DESYNC" $OUT/net.host.log $OUT/net.guest.log | awk -F: '{s += $2} END {print s + 0}')
if [ "$d" = 0 ]; then echo "HEIST NETCCTV PASS no DESYNC"; PASS=$((PASS + 1)); else echo "HEIST NETCCTV FAIL $d DESYNC lines"; FAIL=$((FAIL + 1)); fi
grep -a "HEIST DET .*cctv_view p=0 .* why=1" $OUT/net.host.log | sed 's/.*HEIST DET *//' > $OUT/cyc.host
grep -a "HEIST DET .*cctv_view p=0 .* why=1" $OUT/net.guest.log | sed 's/.*HEIST DET *//' > $OUT/cyc.guest
c=$(wc -l < $OUT/cyc.host)
if [ "$c" -ge 1 ] && cmp -s $OUT/cyc.host $OUT/cyc.guest; then
    echo "HEIST NETCCTV PASS the host's next_weapon presses cycled Bond 0's feed $c time(s), the same on both machines"; PASS=$((PASS + 1))
else
    echo "HEIST NETCCTV FAIL feed cycles: host $c, guest $(wc -l < $OUT/cyc.guest) (or they differ)"; FAIL=$((FAIL + 1))
fi
echo "HEIST NETCCTV DONE $PASS passed, $FAIL failed"

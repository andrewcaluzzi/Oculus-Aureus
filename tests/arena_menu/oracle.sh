#!/bin/sh
# the demo oracles (port/testing.md): dam1 and facility1, one after the other, on Xvfb :99, against tests/baseline
P=/home/dev/project
O=$P/build_tests/amenu
mkdir -p $O
export DISPLAY=:99 GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1
for d in dam1 facility1; do
  GE_DEMO=$d $P/build_pc/ProjectMidas 2>/dev/null | grep '^ge: demo tick' > $O/$d.txt
  if cmp -s $O/$d.txt $P/tests/baseline/$d.a.txt; then r=IDENTICAL; else r=DIFFERS; fi
  echo "$d: $(wc -l < $O/$d.txt) lines, $r"
done

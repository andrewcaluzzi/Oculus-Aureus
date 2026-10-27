#!/bin/sh
# The dam1 demo oracle on build_pc in the build container (Xvfb :99), the same settings as tests/demo_diag.ps1:
# GE_DEMO=dam1 GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400. Prints the line count and whether it equals tests/baseline/dam1.a.txt.
# Run it alone: GE_FRAMES counts paced frames, so a busy machine logs fewer demo ticks.
OUT=/home/dev/project/build_tests/a7/dam1.x.txt
mkdir -p /home/dev/project/build_tests/a7
rm -f /home/dev/project/build_pc/Data/Saves/eeprom.bin
env DISPLAY=:99 GE_DEMO=dam1 GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 /home/dev/project/build_pc/ProjectMidas 2>&1 \
    | grep -a '^ge: demo tick' > "$OUT"
echo "lines $(wc -l < "$OUT")"
if cmp -s "$OUT" /home/dev/project/tests/baseline/dam1.a.txt; then echo "dam1: IDENTICAL"; else echo "dam1: DIFFERENT"; fi

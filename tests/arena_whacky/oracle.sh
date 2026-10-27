#!/bin/sh
# A demo oracle on the Whacky Linux build in the build container (Xvfb :99), the settings of tests/demo_diag.ps1:
# GE_DEMO=<demo> GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400. usage: oracle.sh dam1|facility1 [build dir]
# Prints the line count and whether it equals tests/baseline/<demo>.a.txt (dam1 453 lines, facility1 419). Run it alone.
D=${1:-dam1}
B=${2:-build_pc_whacky}
OUT=/home/dev/project/build_tests/whacky/$D.x.txt
mkdir -p /home/dev/project/build_tests/whacky
rm -f /home/dev/project/$B/Data/Saves/eeprom.bin
env DISPLAY=:99 GE_DEMO=$D GE_DEMODIAG=1 GE_TICKHZ=60 GE_FRAMES=2400 GE_NOINISAVE=1 SDL_AUDIODRIVER=dummy /home/dev/project/$B/ProjectMidas 2>&1 \
    | grep -a '^ge: demo tick' > "$OUT"
echo "lines $(wc -l < "$OUT")"
if cmp -s "$OUT" /home/dev/project/tests/baseline/$D.a.txt; then echo "$D: IDENTICAL"; else echo "$D: DIFFERENT"; fi

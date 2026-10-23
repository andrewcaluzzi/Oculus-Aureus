#!/bin/sh
# Spike helper (rollback snapshot): relink the exe with a linker map, then derive
# snap_ranges.txt (game-owned .data/.bss ranges) next to the exe.
# Run inside the goldeneye container, AFTER a normal cmake --build:
#   wsl -d Ubuntu -- docker run --rm -v "$(pwd)":/home/dev/project goldeneye sh /home/dev/project/tests/snap_map.sh build_win_dev
set -e
B=/home/dev/project/$1
cd "$B"
# last line of link.txt is the g++ link; the earlier lines (re)build objects.a
tail -n 1 CMakeFiles/ge.dir/link.txt | sed -e "s#-o ProjectMidas.exe#-o ProjectMidas.exe -Wl,-Map=$B/ProjectMidas.map#" | sh
python3 /home/dev/project/tests/snap_ranges.py "$B/ProjectMidas.map" "$B/snap_ranges.txt"

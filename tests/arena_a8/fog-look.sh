#!/bin/sh
# The LOW VISIBILITY fog look test (a8-fog-look.lua) inside the build container: Xvfb :99 at the screen size to test (borderless
# fullscreen takes the Xvfb screen). usage: fog-look.sh <tag> [extra env, e.g. "GE_CHR_BATCH=0"]
# Prints the fog log lines, the shot list, and the rooms re-patched; shots in build_tests/a8/shots/fog-<tag>/.
P=/home/dev/project
sh $P/tests/arena_a8/run.sh fog-$1 a8-fog-look 2400 invuln "GE_ARENA_TESTKEYS=1 $2" > /dev/null
L=$P/build_tests/a8/fog-$1.log
grep -a "arena: fog\|TEST fog\|A8TEST fog shot\|roomfog" $L | head -40
echo "rooms re-patched: $(grep -ac 'ge: roomfog:' $L), lua errors: $(grep -aciE 'lua: .*error|traceback' $L), $(tail -1 $L)"

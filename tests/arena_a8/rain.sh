#!/bin/sh
# The RAIN mutator test (a8-rain.lua) inside the build container (Xvfb :99 at the screen size to shoot). usage: rain.sh <tag> [extra env]
# Checks: the rain rolled in and out, Bond coasts further when it rains, enemies slipped, no Lua error. Shots in build_tests/a8/shots/rain-<tag>/.
P=/home/dev/project
sh $P/tests/arena_a8/run.sh rain-$1 a8-rain 2300 invuln "GE_ARENA_TESTKEYS=1 $2" > /dev/null
L=$P/build_tests/a8/rain-$1.log
grep -a "arena: rain\|TEST rain\|A8TEST rain\|MUTATOR\|RAIN over" $L | head -30
echo "slips: $(grep -ac 'RAIN slip:' $L)"
pass=0; fail=0
chk() { if eval "$2"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
chk "rain rolled in" "grep -aq 'arena: rain on' $L"
chk "rain rolled out" "grep -aq 'rain out: dry again' $L"
dry=$(grep -a 'A8TEST rain coast wet' $L | sed 's/.*(dry \([0-9.]*\)).*/\1/'); wet=$(grep -a 'A8TEST rain coast wet' $L | sed 's/.*coast wet \([0-9.]*\).*/\1/')
chk "Bond coasts further in the rain (dry $dry, wet $wet)" "awk -v d=$dry -v w=$wet 'BEGIN { exit !(w > d + 20) }'"
chk "enemies slipped" "test \$(grep -ac 'RAIN slip:' $L) -ge 1"
chk "the script ran to the end" "grep -aq 'A8TEST rain done' $L"
chk "no Lua error" "! grep -aqiE 'lua: .*error|traceback' $L"
echo "rain: $pass passed, $fail failed"

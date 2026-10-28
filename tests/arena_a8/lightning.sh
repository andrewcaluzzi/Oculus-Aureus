#!/bin/sh
# THUNDERBALL's lightning test (a8-lightning.lua) inside the build container (Xvfb :99). usage: lightning.sh <tag> [extra env]
# Checks: strikes on their own while it rains, some at Bond and some at enemies, each one lands as an explosion, the test key's
# strike, the aimed one; no Lua error. Shots in build_tests/a8/shots/lightning-<tag>/.
P=/home/dev/project
sh $P/tests/arena_a8/run.sh lightning-$1 a8-lightning 2400 invuln "GE_ARENA_TESTKEYS=1 $2" > /dev/null
L=$P/build_tests/a8/lightning-$1.log
grep -a "RAIN lightning\|LIGHTNING\|TEST strike\|A8TEST lightning\|RAIN over" $L | head -40
aimed=$(grep -ac 'LIGHTNING [0-9]* aimed' $L); landed=$(grep -ac 'LIGHTNING [0-9]* lands' $L)
pass=0; fail=0
chk() { if eval "$2"; then echo "PASS $1"; pass=$((pass+1)); else echo "FAIL $1"; fail=$((fail+1)); fi; }
chk "strikes while it rains (>= 4 on their own)" "test \$(grep -ac 'RAIN lightning [0-9]* at' $L) -ge 4"
chk "some at a Bond" "grep -aq 'RAIN lightning [0-9]* at bond' $L"
chk "some at an enemy" "grep -aq 'RAIN lightning [0-9]* at chr' $L"
chk "every aimed strike landed ($aimed aimed, $landed landed)" "test $aimed -ge 1 -a $aimed -eq $landed"
chk "the test key's strike" "grep -aq 'TEST strike near Bond' $L"
chk "the aimed strike" "grep -aq 'A8TEST lightning aimed [0-9]' $L"
chk "the script ran to the end" "grep -aq 'A8TEST lightning done' $L"
chk "no Lua error" "! grep -aqiE 'lua: .*error|traceback' $L"
echo "lightning: $pass passed, $fail failed"

#!/bin/sh
# The partner bot's skill (coop_bot_skill): coop-skill.lua at step 5 (the bot as built) and at step 1 (ROOKIE, coop-skill1.lua) over
# the same seeded scene, inside the build container (Xvfb on :99, build_pc). Step 1 must get fewer kills and hits and fire its first
# round at a new enemy later. usage: run_skill.sh [frames]
P=/home/dev/project
L=$P/build_tests/a7
cp $P/tests/arena_a7/coop-skill.lua $P/tests/arena_a7/coop-skill1.lua $P/build_pc/Data/Scripts/modes/
sh $P/tests/arena_a7/run_coop.sh skill5 coop-skill ${1:-3200}
sh $P/tests/arena_a7/run_coop.sh skill1 coop-skill1 ${1:-3200}
r5=$(grep -a "TEST skill RESULT" $L/skill5.log | sed 's/.*RESULT //')
r1=$(grep -a "TEST skill RESULT" $L/skill1.log | sed 's/.*RESULT //')
echo "$r5"
echo "$r1"
grep -a "coop bot skill" $L/skill5.log | head -n 1
grep -a "coop bot skill" $L/skill1.log | head -n 1
# fields: step N: foes F kills K fired S hits H first M ticks
k5=$(echo "$r5" | awk '{print $6}'); h5=$(echo "$r5" | awk '{print $10}'); f5=$(echo "$r5" | awk '{print $12}')
k1=$(echo "$r1" | awk '{print $6}'); h1=$(echo "$r1" | awk '{print $10}'); f1=$(echo "$r1" | awk '{print $12}')
if [ -n "$k5" ] && [ -n "$k1" ] && [ "$k1" -lt "$k5" ] && [ "$h1" -lt "$h5" ] && awk "BEGIN { exit !($f1 > $f5 && $f5 >= 0) }"; then
  echo "skill: PASS (kills $k1 < $k5, hits $h1 < $h5, first round $f1 > $f5 ticks)"
else
  echo "skill: FAIL (kills $k1 / $k5, hits $h1 / $h5, first round $f1 / $f5 ticks)"
fi

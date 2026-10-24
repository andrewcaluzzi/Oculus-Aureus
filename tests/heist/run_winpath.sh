#!/bin/sh
# Heist mode test (P0 job 6), run inside the build container with Xvfb on :99 and build_pc built (the build installs heist.lua and
# heist_lib.lua next to the other modes). Three runs: the win path, the trapped-in-the-vault path, the civilian-limit fail path.
# usage: run_winpath.sh   -> prints the HEIST lines of each run; each run ends with "HEIST DONE <n> passed, <m> failed"
# The full logs are kept in /tmp/heist_winpath_{win,trapped,fail}.log.
cp /home/dev/project/tests/heist/heist_winpath.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_winpath.lua
cp /home/dev/project/tools/mode_heist.lua /home/dev/project/build_pc/Data/Scripts/modes/heist.lua
cp /home/dev/project/tools/heist_lib.lua /home/dev/project/build_pc/Data/Scripts/modes/heist_lib.lua
cd /home/dev/project/build_pc || exit 1
for run in win trapped fail; do
    case $run in
        trapped) flags="GE_HEIST_TEST_TRAPPED=1" ;;
        fail) flags="GE_HEIST_TEST_FAIL=1" ;;
        *) flags="GE_HEIST_TEST_WIN=1" ;;
    esac
    echo "HEIST RUN $run"
    env $flags DISPLAY=:99 GE_MODE=heist_winpath GE_STAGE=bunker2 GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FRAMES=${GE_FRAMES:-14000} \
        timeout 500 ./ProjectMidas > /tmp/heist_winpath_$run.log 2>&1
    grep -E "HEIST|ge: lua: ge: heist: (ready|WIN|FAIL|HIGH|VAULT|ALARM|vault|re-crack|the cracker|cracker|heavy|crack)" /tmp/heist_winpath_$run.log
done

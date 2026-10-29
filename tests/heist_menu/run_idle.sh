#!/bin/sh
# AC's playtest bug: the heist start menu raised the alarm after a while. Boots the lobby and leaves it alone for 330 s of sim time
# (hm_idle.lua): no alarm, no camera spot, no damage, the people behind the menu unmoved and unaware.
# usage: run_idle.sh [bank|casino]   -> "IDLE ..." lines; last "IDLE DONE <n> passed, <m> failed"
P=/home/dev/project
OUT=$P/build_tests/heist_menu
mkdir -p $OUT
M=$P/build_pc/Data/Scripts/modes
for f in mode_heist_menu:heist-menu ui_menu:_ui_menu heist_theme:_heist_theme heist_menu_defs:_heist_menu_defs; do
  cp "$P/tools/${f%%:*}.lua" "$M/${f##*:}.lua"
done
cp $P/tests/heist_menu/hm_idle.lua $P/build_pc/Data/Scripts/zz_hm_idle.lua
L=$OUT/idle_${1:-bank}.log
env DISPLAY=:99 GE_MODE=heist-menu GE_STAGE=levelgen GE_LEVELGEN=${1:-bank} GE_SKIPINTRO=1 GE_NOINISAVE=1 GE_FIXEDTICK=1 GE_TICKHZ=60 GE_SEED=7 \
    GE_NOMENUSAVE=1 GE_UIMENU_DIR=$OUT GE_FRAMES=${GE_FRAMES:-30000} timeout ${TIMEOUT:-1500} $P/build_pc/ProjectMidas > $L 2>&1
rm -f $P/build_pc/Data/Scripts/zz_hm_idle.lua
grep -E "ge: IDLE" $L
